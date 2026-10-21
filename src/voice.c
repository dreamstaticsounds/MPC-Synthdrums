/* MPC Synthdrums -- one voice: the oscillator, the noise generator, the pitch envelope, the two amp envelopes,
 * the x-modulation and the velocity routings. Everything a hit needs lives in the Voice, and sd_voice_note()
 * resets all of it, so two hits with the same settings are bit-identical (the engine's per-hit guarantee).
 *
 * The mappings (one line each, the brief's):
 *  - the oscillator's key is base_note + the voice's index: f = 440*2^((key + pitch + pitch_env - 69)/12)
 *  - the osc amp env: osc_decay 0..127 = ~2 ms..~4 s, exponential, coefficient exp(-1/(sr*t)), one-shot
 *  - osc_shape: the sine's phase is warped, sinf(2*pi*phi^k), k from 1 (pure sine) to 0.35 (curved ramp)
 *  - osc_xmod: the noise modulates the phase increment, an FM index of xmod/127*3 rad
 *  - the pitch env: the depth is penv_amount*(48/63) semitones, penv_attack ramps it 0->full, penv_decay
 *    brings it back to 0 exponentially, penv_shape blends the curve from exponential to linear
 *  - pitch_track: the played octave above the voice's own key, scaled by track/127, in semitones
 *  - the noise: a white LFSR -> the colour's tilt -> its own amp env -> its own low-pass -> the osc/noise mix
 *  - the filter: the mix -> a resonant 12 dB/oct biquad (LP/BP/HP) -> the level
 *  - the effects: the stutter gate -> the waveshaper -> the peaking EQ -> the pan and the level
 *  - the velocity routings: m = 1 - depth/127 + (depth/127)*vel on the amp, the two sources and the penv depth */
#include <math.h>
#include <stdint.h>
#include "synthdrums.h"

/* The sample step: the engine runs at 44100 Hz (the wrapper's contract, SD_SR). */
#define DT (1.0f / SD_SR)

/* A voice whose amp envelope has fallen below this is silent: the one-shot is over and the voice drops out
 * of the 12-voice loop (the floor is about -80 dB, far below anything audible). */
#define ENV_FLOOR 1e-4f

/* The noise source: a 32-bit Galois LFSR (x^32 + x^22 + x^2 + x + 1), re-seeded at every trigger so that the
 * noise is reproducible and two hits with the same settings are bit-identical. */
#define LFSR_TAPS 0xD0000001u
#define LFSR_SEED 0x12345678u

#define VP(v, P_) ((v)->p[(P_) - P_VEL_TO_VOL])

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The velocity crossfade law (the brief's): m = 1 - depth/127 + (depth/127)*vel. A depth of 0 leaves the level
 * alone, a depth of 127 makes it proportional to the velocity. */
static float vel_xfade(float depth, float vel)
{
    const float d = depth * (1.0f / 127.0f);
    return 1.0f - d + d * vel;
}

/* The filter's cutoff knob: a log sweep from 30 Hz to 16 kHz, so the bottom of the range is a dark thud
 * and the top is wide open. */
static float knob_freq(float v)
{
    return 30.0f * powf(16000.0f / 30.0f, v * (1.0f / 127.0f));
}

/* A knob that runs exponentially between two times in seconds (the amp decays: 2 ms .. 4 s, the pitch
 * envelope: 1 ms .. 300 ms). The pitch envelope's range is deliberately short: a drum's pitch sweep is
 * over in well under a second, so a long sweep would sound like a siren rather than a drum. It is a
 * musical choice, not a test requirement -- the engine's pitch-sweep property test passes with either
 * range. */
static float knob_time(float v, float lo, float hi)
{
    return lo * powf(hi / lo, v * (1.0f / 127.0f));
}

/* An attack knob: 0 means "no attack at all" (the depth is full from the first sample, so the pitch env
 * starts at its peak and sweeps down), otherwise the attack runs from 1 ms to hi seconds. */
static float knob_attack(float v, float hi)
{
    if (v <= 0.0f) return 0.0f;
    return 0.001f * powf(hi / 0.001f, (v - 1.0f) * (1.0f / 126.0f));
}

/* penv_shape / noise_shape: 0 = snappy (exponential), 127 = linear. The two curves are blended by the shape. */
static float env_attack(float t, float ta, float g)
{
    const float u = ta > 0.0f && t < ta ? t / ta : 1.0f;
    return powf(u, 1.0f + 3.0f * (1.0f - g));   /* g = 1: a plain ramp, g = 0: a snappy one */
}

static float env_decay(float t, float tau, float g)
{
    const float e = expf(-t / tau);
    const float l = t < 3.0f * tau ? 1.0f - t / (3.0f * tau) : 0.0f;
    return (1.0f - g) * e + g * l;
}

/* The envelope: the attack ramps the depth 0 -> full, the decay brings it back to 0. */
static float env_level(float t, float ta, float tau, float g)
{
    if (t < ta) return env_attack(t, ta, g);
    return env_decay(t - ta, tau, g);
}

/* The white noise: the LFSR's top 16 bits as a signed sample in [-1, 1). */
static float lfsr_next(uint32_t *st)
{
    const uint32_t s = *st;
    *st = (s >> 1) ^ ((s & 1u) ? LFSR_TAPS : 0u);
    return (float)(s >> 16) * (2.0f / 65535.0f) - 1.0f;
}

void sd_voice_reset(Voice *v)
{
    v->vel = 0.0f;          /* the trigger's velocity and the four crossfade scalars it feeds */
    v->vel_vol = 0.0f;
    v->vel_osc = 0.0f;
    v->vel_noise = 0.0f;
    v->vel_penv = 0.0f;
    v->phase = 0.0f;
    v->amp = 0.0f;
    v->penv = 0.0f;
    v->nenv = 0.0f;
    v->t_penv = 0.0f;
    v->t_nenv = 0.0f;
    v->brown = 0.0f;
    v->nfilt = 0.0f;
    v->lfsr = LFSR_SEED;
    /* the filter: the coefficients, the delay line and the filter env's offset are per-hit state too, so
     * two hits with the same settings are bit-identical */
    v->filt.b0 = 0.0f;
    v->filt.b1 = 0.0f;
    v->filt.b2 = 0.0f;
    v->filt.a1 = 0.0f;
    v->filt.a2 = 0.0f;
    v->filt.x1 = 0.0f;
    v->filt.x2 = 0.0f;
    v->filt.y1 = 0.0f;
    v->filt.y2 = 0.0f;
    v->filt.env = 0.0f;
    v->fenv_t = 0.0f;
    /* the stutter's gate phase: per-hit state, so a retrigger starts the gate from the same place */
    v->stut.phase = 0.0f;
    /* the EQ's delay line: per-hit state too (the coefficients are recomputed every block) */
    v->eq.x1 = 0.0f;
    v->eq.y1 = 0.0f;
    v->eq.bx1 = 0.0f;
    v->eq.by1 = 0.0f;
    /* the choke fade: a fresh hit is free (the fade only starts when another voice chokes this one) */
    v->choke_gain = 1.0f;
    v->choke_t = 0.0f;
    v->choke_fading = 0;
    v->sounding = 0;
}

void sd_voice_note(Voice *v, float vel)
{
    sd_voice_reset(v);   /* every bit of per-hit state: the phase, both envelopes, the noise */
    v->vel = vel;
    v->vel_vol = vel_xfade(VP(v, P_VEL_TO_VOL), vel);
    v->vel_osc = vel_xfade(VP(v, P_VEL_TO_OSC), vel);
    v->vel_noise = vel_xfade(VP(v, P_VEL_TO_NOISE), vel);
    v->vel_penv = vel_xfade(VP(v, P_VEL_TO_PENV), vel);
    v->amp = 1.0f;        /* the osc amp env starts at full: a drum hits hard */
    v->nenv = 1.0f;       /* the noise env starts at full (its attack ramps it from 0) */
    v->sounding = 1;
}

void sd_voice_render(Voice *v, float *bus, int frames)
{
    /* The block's settings: the knobs may be turned while the hit rings. */
    const float mix = VP(v, P_SRC_MIX) * (1.0f / 127.0f);
    const float osc_gain = (1.0f - mix) * v->vel_osc;
    const float noise_gain = mix * v->vel_noise;
    const float shape = VP(v, P_OSC_SHAPE) * (1.0f / 127.0f);
    const float warp = 1.0f - 0.65f * shape;                     /* the phase warp's exponent: 1 = sine, 0.35 = ramp */
    const float fm = (VP(v, P_OSC_XMOD) * (1.0f / 127.0f)) * 3.0f;   /* the x-mod's FM index, in radians */
    const float penv_amt = VP(v, P_PENV_AMOUNT) * (48.0f / 63.0f);   /* the pitch env's depth, in semitones */
    const float a_col = 0.02f + 0.98f * (VP(v, P_NOISE_COLOUR) * (1.0f / 127.0f));   /* the colour's tilt */
    const float a_nf = 1.0f - expf(-TWO_PI * (100.0f * powf(1000.0f, VP(v, P_NOISE_FILTER) * (1.0f / 127.0f))) / SD_SR);
    const float tau_amp = knob_time(VP(v, P_OSC_DECAY), 0.002f, 4.0f);
    const float coef_amp = expf(-1.0f / (SD_SR * tau_amp));       /* the osc amp env's coefficient */
    const float tau_n = knob_time(VP(v, P_NOISE_DECAY), 0.002f, 4.0f);
    const float ta_n = knob_attack(VP(v, P_NOISE_ATTACK), 0.3f);
    const float g_n = VP(v, P_NOISE_SHAPE) * (1.0f / 127.0f);
    const float tau_p = knob_time(VP(v, P_PENV_DECAY), 0.001f, 0.3f);   /* the pitch sweep is short by design */
    const float ta_p = knob_attack(VP(v, P_PENV_ATTACK), 0.3f);
    const float g_p = VP(v, P_PENV_SHAPE) * (1.0f / 127.0f);
    /* The voice's own key, the played note's octave offset and the pitch knob give the base pitch; the pitch
     * envelope sweeps on top of it. The phase runs in cycles, so the increment is f/sr. */
    const float key = (float)(v->base_note + v->index);
    const float track = VP(v, P_PITCH_TRACK) * (1.0f / 127.0f);
    const float base_semis = key + VP(v, P_PITCH) + ((float)v->note - key) * track;
    const float inc0 = (440.0f * powf(2.0f, (base_semis - 69.0f) / 12.0f)) / SD_SR;
    /* The filter's settings: the cutoff knob (30 Hz .. 16 kHz, a log sweep), the resonance (Q 0.7 .. 9, a
     * bounded peak -- it is a drum, not a siren), the mode and the velocity routing. The velocity moves the
     * cutoff around the middle: depth/127 * (vel - 0.5) * 3 kHz, so a soft hit is darker than a hard one. */
    const float cut_knob = knob_freq(VP(v, P_FILTER_CUTOFF));
    const float q = 0.7f + 8.3f * (VP(v, P_FILTER_RES) * (1.0f / 127.0f));
    const int mode = (int)(VP(v, P_FILTER_MODE) + 0.5f);
    const float vel_cut = VP(v, P_VEL_TO_CUT) * (1.0f / 127.0f) * (v->vel - 0.5f) * 3000.0f;
    /* The filter env: a one-shot cutoff envelope that starts amount * 4 kHz / 63 above (or below) the
     * cutoff and decays back to it. An amount of 0 is the env off: no sweep at all. */
    const int fenv_on = (VP(v, P_FENV_AMOUNT) != 0.0f);
    const float fenv_amt = VP(v, P_FENV_AMOUNT) * (4000.0f / 63.0f);
    const float tau_f = knob_time(VP(v, P_FENV_DECAY), 0.002f, 2.0f);
    const float cut_base = cut_knob + vel_cut;
    float fenv0 = 0.0f;   /* the env's start offset, in Hz */
    int i;

    /* The stutter: the gate's rate (0.5 .. 40 Hz, an exponential sweep -- the roll/echo-machine feel)
     * and the gate's depth (0 = off, exactly; 1 = the gate closes completely between the bursts). */
    const float stut_rate = 0.5f * powf(80.0f, VP(v, P_STUTTER_LENGTH) * (1.0f / 127.0f));
    const float stut_depth = VP(v, P_STUTTER_STRENGTH) * (1.0f / 127.0f);
    /* The waveshaper's drive: 0 = clean (the shaper is then exactly unity). */
    const float drive01 = VP(v, P_DIST_DRIVE) * (1.0f / 127.0f);
    /* The peaking EQ: the bell's frequency (a log sweep, 40 Hz .. 12 kHz), its gain (+/-18 dB) and its
     * width (Q 0.5 .. 6, logarithmic: a Q knob is a musical control, so its ends are far apart and its
     * middle is where a bell usually sits). */
    const float eq_hz = EQ_MIN_HZ * powf(EQ_MAX_HZ / EQ_MIN_HZ, VP(v, P_EQ_FREQ) * (1.0f / 127.0f));
    const float eq_db = VP(v, P_EQ_GAIN) * (18.0f / 63.0f);
    const float eq_q = 0.5f * powf(12.0f, VP(v, P_EQ_Q) * (1.0f / 127.0f));
    /* The output stage: the pan (equal power: -64 = hard left, 0 = centre, +63 = hard right) and the
     * level (0 .. ~1.4x, the init patch's 100 is a little over unity). The "out_sel" knob is state-only:
     * the MPC sums its three output buses to the main pair, so all three selections sound the same. */
    const float pan_theta = (VP(v, P_OUT_PAN) + 64.0f) * (TWO_PI * 0.25f * (1.0f / 128.0f));
    const float pan_l = cosf(pan_theta);
    const float pan_r = sinf(pan_theta);
    const float vol = 1.4f * (VP(v, P_OUT_VOL) * (1.0f / 127.0f));

    /* The env's start is clamped to the filter's range, so the sweep always starts inside it and decays
     * back to the cutoff the knob (and the velocity) asked for. */
    if (fenv_on) fenv0 = clampf(cut_base + fenv_amt, FILT_MIN_HZ, FILT_MAX_HZ) - cut_base;
    else v->filt.env = 0.0f;

    /* The filter's coefficients: once a block, or every sample while the env sweeps the cutoff. */
    filt_set(&v->filt, clampf(cut_base + fenv0, FILT_MIN_HZ, FILT_MAX_HZ), q, mode);

    /* The EQ's coefficients: once a block (the EQ has no envelope). */
    eq_set(&v->eq, eq_hz, eq_db, eq_q);

    for (i = 0; i < frames; i++) {
        float w, inc, osc, noise, out;

        /* The noise generator runs every sample: it feeds the x-mod as well as the noise source. */
        w = lfsr_next(&v->lfsr);

        /* The pitch envelope: the depth ramps up over the attack and decays back to zero. */
        v->t_penv += DT;
        if (penv_amt != 0.0f) {
            v->penv = env_level(v->t_penv, ta_p, tau_p, g_p) * v->vel_penv;
            inc = inc0 * powf(2.0f, (v->penv * penv_amt) / 12.0f);
        } else {
            v->penv = 0.0f;
            inc = inc0;
        }

        /* The x-mod: the noise modulates the oscillator's frequency. The index is the classic FM deviation
         * (up to 3 rad), so the instantaneous frequency is f*(1 + index*noise): the 808's metallic rattle. */
        inc *= 1.0f + fm * w;

        /* The oscillator: the phase runs on, the shape warps it. */
        v->phase += inc;
        v->phase -= floorf(v->phase);
        osc = (shape == 0.0f) ? sinf(TWO_PI * v->phase) : sinf(TWO_PI * powf(v->phase, warp));

        /* The osc amp env: exponential, one-shot. */
        v->amp *= coef_amp;

        /* The noise: the colour's leaky integrator, then the amp envelope, then the noise tone filter. */
        v->brown += a_col * (w - v->brown);
        v->t_nenv += DT;
        v->nenv = env_level(v->t_nenv, ta_n, tau_n, g_n);
        v->nfilt += a_nf * (v->brown * v->nenv - v->nfilt);

        /* The osc/noise mix. */
        noise = v->nfilt;
        out = osc * v->amp * osc_gain + noise * noise_gain;

        /* The filter env: the offset decays to zero, so the cutoff sweeps back to the knob's. */
        if (fenv_on) {
            v->fenv_t += DT;
            v->filt.env = fenv0 * expf(-v->fenv_t / tau_f);
            filt_set(&v->filt, clampf(cut_base + v->filt.env, FILT_MIN_HZ, FILT_MAX_HZ), q, mode);
        }

        /* The filter sits between the mix and the level: it colours the whole voice, the velocity's level
         * rides on top of it. */
        out = filt_step(&v->filt, out);
        out *= v->vel_vol;

        /* The stutter gate: it chops the voice's amplitude (the roll/echo-machine effect). */
        out = fx_stutter_step(&v->stut, out, stut_rate, stut_depth);

        /* The waveshaper: the drive knob's saturation (exactly unity when the knob is at zero). */
        out = fx_dist(out, drive01);

        /* The peaking EQ: the one band of the FX section. */
        out = eq_step(&v->eq, out);

        /* The choke fade: it lives in the voice's own output path (never in the master bus, which would
         * fade the choking voice's own hit as well). The fade is a raised cosine over 5 ms, so it starts
         * and ends without a step. */
        if (v->choke_fading) {
            float k;
            v->choke_t += DT;
            k = v->choke_t * (1.0f / CHOKE_FADE_S);
            if (k >= 1.0f) {
                v->choke_fading = 0;
                v->choke_gain = 0.0f;
            } else {
                /* A raised cosine from full level to silence: it starts and ends without a step. */
                v->choke_gain = 0.5f * (1.0f + cosf(TWO_PI * 0.5f * k));
            }
        }
        out *= v->choke_gain;

        /* The pan and the level: the voice's tail. */
        out *= vol;
        bus[2 * i] += out * pan_l;
        bus[2 * i + 1] += out * pan_r;
    }

    /* The one-shot is over when nothing audible is left: the voice drops out of the 12-voice loop. */
    {
        const int amp_dead = (v->amp < ENV_FLOOR);
        const int noise_dead = (v->t_nenv > ta_n) && (v->nenv < ENV_FLOOR);
        if ((amp_dead || osc_gain <= 0.0f) && (noise_dead || noise_gain <= 0.0f)) v->sounding = 0;
        if (v->choke_gain <= 0.0f) v->sounding = 0;   /* the choke fade has run out: the voice is silent */
    }
}
