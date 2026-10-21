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
 *  - the velocity routings: m = 1 - depth/127 + (depth/127)*vel on the amp, the two sources and the penv depth */
#include <math.h>
#include <stdint.h>
#include "synthdrums.h"

/* The engine runs at 44100 Hz (the wrapper's contract). */
#define SD_SR 44100.0f
#define TWO_PI 6.28318530717958648f
#define DT (1.0f / SD_SR)

/* A voice whose amp envelope has fallen below this is silent: the one-shot is over and the voice drops out
 * of the 12-voice loop (the floor is about -80 dB, far below anything audible). */
#define ENV_FLOOR 1e-4f

/* The noise source: a 32-bit Galois LFSR (x^32 + x^22 + x^2 + x + 1), re-seeded at every trigger so that the
 * noise is reproducible and two hits with the same settings are bit-identical. */
#define LFSR_TAPS 0xD0000001u
#define LFSR_SEED 0x12345678u

#define VP(v, P_) ((v)->p[(P_) - P_VEL_TO_VOL])

/* The velocity crossfade law (the brief's): m = 1 - depth/127 + (depth/127)*vel. A depth of 0 leaves the level
 * alone, a depth of 127 makes it proportional to the velocity. */
static float vel_xfade(float depth, float vel)
{
    const float d = depth * (1.0f / 127.0f);
    return 1.0f - d + d * vel;
}

/* A knob that runs exponentially between two times in seconds (the amp decays: 2 ms .. 4 s, the pitch
 * envelope: 1 ms .. 300 ms). */
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
    v->phase = 0.0f;
    v->amp = 0.0f;
    v->penv = 0.0f;
    v->nenv = 0.0f;
    v->t_penv = 0.0f;
    v->t_nenv = 0.0f;
    v->brown = 0.0f;
    v->nfilt = 0.0f;
    v->lfsr = LFSR_SEED;
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

void sd_voice_render(Voice *v, float *mono, int frames)
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
    const float tau_p = knob_time(VP(v, P_PENV_DECAY), 0.001f, 0.3f);
    const float ta_p = knob_attack(VP(v, P_PENV_ATTACK), 0.3f);
    const float g_p = VP(v, P_PENV_SHAPE) * (1.0f / 127.0f);
    /* The voice's own key, the played note's octave offset and the pitch knob give the base pitch; the pitch
     * envelope sweeps on top of it. The phase runs in cycles, so the increment is f/sr. */
    const float key = (float)(v->base_note + v->index);
    const float track = VP(v, P_PITCH_TRACK) * (1.0f / 127.0f);
    const float base_semis = key + VP(v, P_PITCH) + ((float)v->note - key) * track;
    const float inc0 = (440.0f * powf(2.0f, (base_semis - 69.0f) / 12.0f)) / SD_SR;
    int i;

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

        /* The osc/noise mix and the velocity routings. */
        noise = v->nfilt;
        out = osc * v->amp * osc_gain + noise * noise_gain;
        out *= v->vel_vol;
        mono[i] += out;
    }

    /* The one-shot is over when nothing audible is left: the voice drops out of the 12-voice loop. */
    {
        const int amp_dead = (v->amp < ENV_FLOOR);
        const int noise_dead = (v->t_nenv > ta_n) && (v->nenv < ENV_FLOOR);
        if ((amp_dead || osc_gain <= 0.0f) && (noise_dead || noise_gain <= 0.0f)) v->sounding = 0;
    }
}
