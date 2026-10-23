/* MPC Synthdrums -- one voice: the oscillator, the noise generator, the pitch envelope, the two amp envelopes,
 * the x-modulation and the velocity routings. Everything a hit needs lives in the Voice, and sd_voice_note()
 * resets all of it, so two hits with the same settings are bit-identical (the engine's per-hit guarantee).
 *
 * The mappings (one line each, the brief's):
 *  - the oscillator's key is base_note + the voice's index: f = 440*2^((key + pitch + pitch_env - 69)/12)
 *  - the osc amp env: osc_decay 0..127 = ~2 ms..~4 s, osc_shape holds the level at full for 0.8*g*T then
 *    releases exponentially, exactly 0 at T = 9*tau (g = shape/127; at g = 0 it is 1.0.0's pure exp)
 *  - osc_xmod: the noise modulates the phase increment, an FM index of xmod/127*3 rad
 *  - the pitch env: the depth is penv_amount*(48/63) semitones, penv_attack ramps it 0->full, penv_decay's
 *    stage holds then releases per penv_shape (the same hold-then-release law as the amp env)
 *  - pitch_track: the played octave above the voice's own key, scaled by track/127, in semitones
 *  - the noise: a white LFSR -> the colour's tilt -> its own amp env -> its own biquad -> the osc/noise mix
 *  - the filters: each branch (osc, noise) has its own resonant 12 dB/oct biquad (LP/BP/HP) and its own
 *    one-shot envelope; the mix happens after both
 *  - the effects: the stutter gate -> the waveshaper -> the tone one-pole -> the three peaking EQ bands
 *    -> ring mod, FM, comb -> the level and the pan -> the stereo delay -> the gated verb (BA3, 1.0.2)
 *    -> the stereo widener (1.0.2) -> the choke fade (the choke goes last so it cuts the delay's
 *    feedback tails too)
 *  - the velocity routings: m = 1 - depth/127 + (depth/127)*vel on the amp, the two sources and the penv depth */
#include <math.h>
#include <stdint.h>
#include <string.h>
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

#define VP(v, P_) ((v)->p[P2E[P_]])
/* The block's working copy of the knobs (sd_voice_render): the LFOs modulate pv, never p[], so live
 * knob turns and LFO modulation compose instead of fighting over the same storage. */
#define PP(P_) pv[P2E[P_]]

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

/* penv_shape / noise_shape on the attack stage: 0 = snappy (exponential), 127 = a plain ramp. */
static float env_attack(float t, float ta, float g)
{
    const float u = ta > 0.0f && t < ta ? t / ta : 1.0f;
    return powf(u, 1.0f + 3.0f * (1.0f - g));   /* g = 1: a plain ramp, g = 0: a snappy one */
}

/* The decay stage (penv_shape / noise_shape / osc_shape): hold-then-release, per the shape curve spec
 * figure. g = shape/127; T = 9*tau (the time the 1.0.0 exponential took to reach its floor); hold =
 * 0.8*g*T: the level is exactly 1 while t < hold, then releases exponentially with tau' = (T - hold)/9
 * (landing at e^-9 ~ 1e-4 at t = T), and is exactly 0 at t >= T -- the voice must reach silence.
 * At g = 0 the hold is 0 and tau' is tau: the per-sample law is 1.0.0's exponential, bit for bit. */
static float env_release(float t, float tau, float g)
{
    const float T = 9.0f * tau, hold = 0.8f * g * T;
    if (t >= T) return 0.0f;
    if (t <= hold) return 1.0f;
    /* At hold = 0 (g = 0) the tail's tau is tau itself, not (T - 0)/9 recomputed: bit-identical 1.0.0. */
    return expf(-(t - hold) / (hold > 0.0f ? (T - hold) * (1.0f / 9.0f) : tau));
}

/* The envelope: the attack ramps the depth 0 -> full, the decay holds then releases it to 0. */
static float env_level(float t, float ta, float tau, float g)
{
    if (t < ta) return env_attack(t, ta, g);
    return env_release(t - ta, tau, g);
}

/* The white noise: the LFSR's top 16 bits as a signed sample in [-1, 1). */
static float lfsr_next(uint32_t *st)
{
    const uint32_t s = *st;
    *st = (s >> 1) ^ ((s & 1u) ? LFSR_TAPS : 0u);
    return (float)(s >> 16) * (2.0f / 65535.0f) - 1.0f;
}

/* The LFOs' parameters, per LFO (Task 9's append order: rate, sync, div, retrig, wave, dest, depth). */
static const int lfo_rate_k[3] = { P_LFO1_RATE, P_LFO2_RATE, P_LFO3_RATE };
static const int lfo_sync_k[3] = { P_LFO1_SYNC, P_LFO2_SYNC, P_LFO3_SYNC };
static const int lfo_div_k[3] = { P_LFO1_DIV, P_LFO2_DIV, P_LFO3_DIV };
static const int lfo_retrig_k[3] = { P_LFO1_RETRIG, P_LFO2_RETRIG, P_LFO3_RETRIG };
static const int lfo_wave_k[3] = { P_LFO1_WAVE, P_LFO2_WAVE, P_LFO3_WAVE };
static const int lfo_dest_k[3] = { P_LFO1_DEST, P_LFO2_DEST, P_LFO3_DEST };
static const int lfo_depth_k[3] = { P_LFO1_DEPTH, P_LFO2_DEPTH, P_LFO3_DEPTH };
/* The synced rate's division, by the lfoN_div option's index: the cycles per beat (a quarter note), so
 * freq = mult * bpm/60 -- "1/4" (index 6) is one cycle per beat, "1 bar" (index 8) four beats, the T
 * (triplet) and D (dotted) entries sit between as the musical grid puts them. */
static const float BEAT_MULT[12] = { 8.0f, 6.0f, 4.0f, 3.0f, 2.0f, 1.5f, 1.0f, 0.5f, 0.25f, 4.0f / 3.0f,
                                     2.0f / 3.0f, 1.0f / 3.0f };

/* The 1.0.2 division grid (synthdrums.h): beats per cycle and the readout words, index 0 = the longest.
 * The LFO syncs keep their own 12-entry BEAT_MULT above (unchanged from 1.0.1). */
const float SD_DIV_BEATS[SD_DIV_COUNT] = {
    16.0f, 8.0f, 4.0f, 3.0f, 2.0f, 4.0f / 3.0f, 1.5f, 1.0f, 2.0f / 3.0f,
    0.75f, 0.5f, 1.0f / 3.0f, 0.375f, 0.25f, 1.0f / 6.0f, 0.1875f, 0.125f, 1.0f / 12.0f };
const char *const SD_DIV_WORDS[SD_DIV_COUNT] = {
    "4 bars", "2 bars", "1 bar", "1/2D", "1/2", "1/2T", "1/4D", "1/4", "1/4T",
    "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32D", "1/32", "1/32T" };

void sd_voice_reset(Voice *v)
{
    v->vel = 0.0f;          /* the trigger's velocity and the four crossfade scalars it feeds */
    v->vel_vol = 0.0f;
    v->vel_osc = 0.0f;
    v->vel_noise = 0.0f;
    v->vel_penv = 0.0f;
    v->phase = 0.0f;
    v->amp = 0.0f;
    v->t_amp = 0.0f;
    v->penv = 0.0f;
    v->nenv = 0.0f;
    v->t_penv = 0.0f;
    v->t_nenv = 0.0f;
    v->brown = 0.0f;
    v->lfsr = LFSR_SEED;
    /* the filters: the coefficients, the delay lines and both filter envs' offsets are per-hit state too, so
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
    v->nfil.b0 = 0.0f;
    v->nfil.b1 = 0.0f;
    v->nfil.b2 = 0.0f;
    v->nfil.a1 = 0.0f;
    v->nfil.a2 = 0.0f;
    v->nfil.x1 = 0.0f;
    v->nfil.x2 = 0.0f;
    v->nfil.y1 = 0.0f;
    v->nfil.y2 = 0.0f;
    v->nfil.env = 0.0f;
    v->nfenv_t = 0.0f;
    /* the stutter's gate phase: per-hit state, so a retrigger starts the gate from the same place */
    v->stut.phase = 0.0f;
    v->tone_y = 0.0f;
    /* the EQ's delay lines: per-hit state too (the coefficients are recomputed every block) */
    for (int i = 0; i < 3; i++) {
        v->eq[i].x1 = 0.0f;
        v->eq[i].y1 = 0.0f;
        v->eq[i].bx1 = 0.0f;
        v->eq[i].by1 = 0.0f;
    }
    /* the FX row's states: per-hit, so two hits with the same settings are bit-identical */
    v->ring.ph = 0.0f;
    v->ring.g = 0.0f;
    v->fm.ph = 0.0f;
    memset(&v->comb, 0, sizeof v->comb);
    /* The verb: verb_reset zeroes the rings and state AND sets the figure's coefficients (computed
     * identically every hit, so the reset is bit-exact like dly_reset's). */
    verb_reset(&v->verb);
    v->verb_quiet_t = 0.0f;
    /* the stereo delay: fully reinitialized at every hit (the doubler's heads return to their starting
     * positions), so two hits with the same settings are bit-identical */
    dly_reset(&v->dly);
    v->dly_quiet_t = 0.0f;
    /* The LFOs' random source must never be all-zero (an LFSR stuck at 0 never advances): the calloc'd
     * engine start gets seeded here. Everything else in lfo[] is continuous per-voice state that this
     * reset deliberately leaves alone -- retrig OFF runs on across hits; sd_voice_note() applies the
     * retrig knob (that is why the per-hit reset and the LFOs' continuity can coexist). */
    {
        int l;
        for (l = 0; l < 3; l++) if (v->lfo[l].rng == 0) v->lfo[l].rng = LFSR_SEED;
    }
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
    v->t_amp = 0.0f;
    v->nenv = 1.0f;       /* the noise env starts at full (its attack ramps it from 0) */
    /* The LFOs (Task 9): retrig ON (the default) restarts the phase and re-seeds the random source, so
     * two hits with the same settings are bit-identical even on s&h/rnd. retrig OFF lets the phase and
     * the rng run on across hits -- the one deliberate break in the per-hit determinism, and only when
     * the user asks for it (sd_voice_reset leaves the LFO state alone so this can hold). */
    {
        int l;
        for (l = 0; l < 3; l++) {
            if (VP(v, lfo_retrig_k[l]) != 0.0f) {
                v->lfo[l].ph = 0.0f;
                v->lfo[l].rng = LFSR_SEED;
                v->lfo[l].hold = lfsr_next(&v->lfo[l].rng);   /* the first cycle's value (s&h) ... */
                v->lfo[l].tgt = lfsr_next(&v->lfo[l].rng);    /* ... and the first slew's target (rnd) */
            }
        }
    }
    v->sounding = 1;
}

void sd_voice_render(Voice *v, float *bus, int frames, float bpm)
{
    /* The block's working copy of the knobs: the LFOs modulate it (block rate), so every block-level
     * read below goes through PP() and the modulation lands on all of them. Live knob turns still take
     * effect at the next block -- the copy keeps p[] pristine and the modulation composable. */
    float pv[EDIT_PARAMS];
    int l;
    memcpy(pv, v->p, sizeof pv);

    /* The three LFOs (spec §6): each is evaluated at the block's start phase, applies to its
     * destination (half the knob's range around the current value, bidirectional, clamped to the
     * knob's ends), then advances its phase by the block's length. The LFOs read their own
     * rate/depth from pv, in chain order 1..3, so one LFO's destination on another LFO's rate
     * cross-modulates it with a block of latency (LFO-FM). Synced rates come from the host's tempo
     * (set_param "lfo_bpm"); free-run is the knob's 0.02..20 Hz exponential sweep. */
    for (l = 0; l < 3; l++) {
        const int sync = PP(lfo_sync_k[l]) != 0.0f;
        const int kind = (int)(PP(lfo_wave_k[l]) + 0.5f);
        const int di = (int)(PP(lfo_dest_k[l]) + 0.5f);
        const float rate_hz = sync
            ? BEAT_MULT[(int)(PP(lfo_div_k[l]) + 0.5f)] * (bpm * (1.0f / 60.0f))
            : 0.02f * powf(1000.0f, PP(lfo_rate_k[l]) * (1.0f / 127.0f));
        const float depth = PP(lfo_depth_k[l]) * (1.0f / 63.0f);   /* -1..1: bidirectional */
        LFO *st = &v->lfo[l];
        float val;
        switch (kind) {   /* the wave at this block's start phase, in -1..1 */
        case 0: val = sinf(TWO_PI * st->ph); break;
        case 1: val = 4.0f * fabsf(st->ph - 0.5f) - 1.0f; break;   /* tri: +1 at 0, -1 at mid */
        case 2: val = 2.0f * st->ph - 1.0f; break;                 /* saw up */
        case 3: val = 1.0f - 2.0f * st->ph; break;                 /* saw down */
        case 4: val = st->ph < 0.5f ? 1.0f : -1.0f; break;         /* square */
        case 5: val = st->hold; break;                             /* s&h: the value held this cycle */
        default: val = st->hold + (st->tgt - st->hold) * st->ph; break;  /* rnd: slewed per cycle */
        }
        {
            const int e = P2E[DEST2P[di]];
            const ParamDef *d = &PDEF[DEST2P[di]];
            pv[e] = clampf(pv[e] + val * depth * 0.5f * (d->max - d->min), d->min, d->max);
        }
        /* advance the phase by the block; a wrap draws the random waves' next value */
        st->ph += rate_hz * (float)frames * DT;
        if (st->ph >= 1.0f) {
            st->ph -= 1.0f;
            if (kind == 5) st->hold = lfsr_next(&st->rng);
            else if (kind == 6) { st->hold = st->tgt; st->tgt = lfsr_next(&st->rng); }
        }
    }

    /* The block's settings: the knobs may be turned while the hit rings. */
    const float mix = PP(P_SRC_MIX) * (1.0f / 127.0f);
    const float osc_gain = (1.0f - mix) * v->vel_osc;
    const float noise_gain = mix * v->vel_noise;
    const float fm = (PP(P_OSC_XMOD) * (1.0f / 127.0f)) * 3.0f;   /* the x-mod's FM index, in radians */
    const float penv_amt = PP(P_PENV_AMOUNT) * (48.0f / 63.0f);   /* the pitch env's depth, in semitones */
    const float a_col = 0.02f + 0.98f * (PP(P_NOISE_COLOUR) * (1.0f / 127.0f));   /* the colour's tilt */
    /* The osc amp env (the osc_shape knob's job since 1.0.1: the shape holds the level at full, then
     * releases). tau_tail is tau_amp itself when the hold is 0, so at shape 0 coef_tail equals 1.0.0's
     * coef_amp bit for bit. */
    const float tau_amp = knob_time(PP(P_OSC_DECAY), 0.002f, 4.0f);
    const float g_amp = PP(P_OSC_SHAPE) * (1.0f / 127.0f);
    const float T_amp = 9.0f * tau_amp, hold_amp = 0.8f * g_amp * T_amp;
    const float tau_tail = (hold_amp > 0.0f) ? (T_amp - hold_amp) * (1.0f / 9.0f) : tau_amp;
    const float coef_tail = expf(-1.0f / (SD_SR * tau_tail));
    const float tau_n = knob_time(PP(P_NOISE_DECAY), 0.002f, 4.0f);
    const float ta_n = knob_attack(PP(P_NOISE_ATTACK), 0.3f);
    const float g_n = PP(P_NOISE_SHAPE) * (1.0f / 127.0f);
    const float tau_p = knob_time(PP(P_PENV_DECAY), 0.001f, 0.3f);   /* the pitch sweep is short by design */
    const float ta_p = knob_attack(PP(P_PENV_ATTACK), 0.3f);
    const float g_p = PP(P_PENV_SHAPE) * (1.0f / 127.0f);
    /* The voice's own key, the played note's octave offset and the pitch knob give the base pitch; the pitch
     * envelope sweeps on top of it. The phase runs in cycles, so the increment is f/sr. */
    const float key = (float)(v->base_note + v->index);
    const float track = PP(P_PITCH_TRACK) * (1.0f / 127.0f);
    const float base_semis = key + PP(P_PITCH) + ((float)v->note - key) * track;
    const float inc0 = (440.0f * powf(2.0f, (base_semis - 69.0f) / 12.0f)) / SD_SR;
    /* The filter's settings: the cutoff knob (30 Hz .. 16 kHz, a log sweep), the resonance (Q 0.7 .. 9, a
     * bounded peak -- it is a drum, not a siren), the mode and the velocity routing. The velocity moves the
     * cutoff around the middle: depth/127 * (vel - 0.5) * 3 kHz, so a soft hit is darker than a hard one. */
    const float cut_knob = knob_freq(PP(P_FILTER_CUTOFF));
    const float q = 0.7f + 8.3f * (PP(P_FILTER_RES) * (1.0f / 127.0f));
    const int mode = (int)(PP(P_FILTER_MODE) + 0.5f);
    const float vel_cut = PP(P_VEL_TO_CUT) * (1.0f / 127.0f) * (v->vel - 0.5f) * 3000.0f;
    /* The filter env: a one-shot cutoff envelope that starts amount * 4 kHz / 63 above (or below) the
     * cutoff and decays back to it. An amount of 0 is the env off: no sweep at all. */
    const int fenv_on = (PP(P_FENV_AMOUNT) != 0.0f);
    const float fenv_amt = PP(P_FENV_AMOUNT) * (4000.0f / 63.0f);
    const float tau_f = knob_time(PP(P_FENV_DECAY), 0.002f, 2.0f);
    const float cut_base = cut_knob + vel_cut;
    float fenv0 = 0.0f;   /* the env's start offset, in Hz */
    /* The noise branch's filter: the same RBJ biquad on its own cutoff knob (20 Hz .. 18 kHz, a wider log
     * sweep than the osc filter's), its own Q/mode and its own envelope + velocity routing (the velocity
     * offset follows the same law as vel_to_cut). */
    const float ncut_knob = 20.0f * powf(900.0f, PP(P_NOISE_FILTER) * (1.0f / 127.0f));
    const float nq = 0.7f + 8.3f * (PP(P_NFILT_RES) * (1.0f / 127.0f));
    const int nmode = (int)(PP(P_NFILT_MODE) + 0.5f);
    const float nvel_cut = PP(P_NFILT_VEL) * (1.0f / 127.0f) * (v->vel - 0.5f) * 3000.0f;
    const int nfenv_on = (PP(P_NFILT_ENV_AMOUNT) != 0.0f);
    const float nfenv_amt = PP(P_NFILT_ENV_AMOUNT) * (4000.0f / 63.0f);
    const float ntau_f = knob_time(PP(P_NFILT_ENV_DECAY), 0.002f, 2.0f);
    const float ncut_base = ncut_knob + nvel_cut;
    float nfenv0 = 0.0f;
    int i;

    /* The stutter: the gate's rate from the tempo grid (1.0.2) — the knob picks a division: 0 = OFF
     * (fx_stutter_step passes the signal through exactly), 1..18 = 4 bars .. 1/32 trip; rate =
     * (bpm/60)/beats-per-cycle. The depth is unchanged (0 = off, exactly; 1 = fully closed bursts).
     * The quantize expression is duplicated in synth.c's display branch: keep the two in sync. */
    const int stut_step = (int)(PP(P_STUTTER_LENGTH) * 18.0f / 127.0f + 0.5f);
    const float stut_rate = stut_step == 0 ? 0.0f
                            : (bpm * (1.0f / 60.0f)) / SD_DIV_BEATS[stut_step - 1];
    const float stut_depth = PP(P_STUTTER_STRENGTH) * (1.0f / 127.0f);
    /* The waveshaper's drive: 0 = clean (the shaper is then exactly unity). */
    const float drive01 = PP(P_DIST_DRIVE) * (1.0f / 127.0f);
    /* The dist tone knob (1.0.2): its one-pole cutoff, 200 Hz * 80^(v/127) (200 Hz .. ~16 kHz);
     * 127 skips the stage entirely (exact bypass). The coefficient is block-rate. */
    const float tone127 = PP(P_DIST_TONE);
    const float tone_a = tone127 < 127.0f
                         ? 1.0f - expf(-TWO_PI * (200.0f * powf(80.0f, tone127 * (1.0f / 127.0f))) / SD_SR)
                         : 1.0f;
    /* The peaking EQ: the bell's frequency (a log sweep, 40 Hz .. 12 kHz), its gain (+/-18 dB) and its
     * width (Q 0.5 .. 6, logarithmic: a Q knob is a musical control, so its ends are far apart and its
     * middle is where a bell usually sits). */
    /* The three EQ bands' settings: the same mapping each (freq 40 Hz..12 kHz log, gain ±18 dB, Q 0.5..6). */
    const float eq_hz[3] = {
        EQ_MIN_HZ * powf(EQ_MAX_HZ / EQ_MIN_HZ, PP(P_EQ_FREQ) * (1.0f / 127.0f)),
        EQ_MIN_HZ * powf(EQ_MAX_HZ / EQ_MIN_HZ, PP(P_EQ2_FREQ) * (1.0f / 127.0f)),
        EQ_MIN_HZ * powf(EQ_MAX_HZ / EQ_MIN_HZ, PP(P_EQ3_FREQ) * (1.0f / 127.0f)) };
    const float eq_db[3] = { PP(P_EQ_GAIN) * (18.0f / 63.0f), PP(P_EQ2_GAIN) * (18.0f / 63.0f),
                             PP(P_EQ3_GAIN) * (18.0f / 63.0f) };
    const float eq_q[3] = { 0.5f * powf(12.0f, PP(P_EQ_Q) * (1.0f / 127.0f)),
                            0.5f * powf(12.0f, PP(P_EQ2_Q) * (1.0f / 127.0f)),
                            0.5f * powf(12.0f, PP(P_EQ3_Q) * (1.0f / 127.0f)) };
    /* The FX row's knobs (all off at 0): the ring carrier (1 Hz..8 kHz log), the FM index (0..8 rad,
     * its gate the same knob), the comb's delay (0.5..30 ms) and the gated reverb's mix. */
    const float ring01 = PP(P_RING_MOD) * (1.0f / 127.0f);
    const float ring_hz = powf(8000.0f, ring01);          /* 1 Hz at 0 .. 8 kHz at 127 */
    /* design §8: the knob is the carrier frequency only — once engaged the ring is fully wet (Koala
     * "slide up for faster modulation"), so the depth is 1 above 0, not a blend that fades in. */
    const float ring_wet = ring01 > 0.0f ? 1.0f : 0.0f;
    const float fm_g = PP(P_FM_INDEX) * (1.0f / 127.0f);
    const float fm_idx = 8.0f * fm_g;
    const float comb01 = PP(P_COMB) * (1.0f / 127.0f);
    const float comb_ms = 0.5f * powf(60.0f, comb01);      /* 0.5 ms at 0 .. 30 ms at 127 */
    const float gverb_m = PP(P_GVERB) * (1.0f / 127.0f);
    /* the gated verb's length (1.0.3, design §4): the knob maps to BA3's tap count, exactly the
     * vendor's (int)(pow(B,2)*487)+1 with B = knob/127 -- 0 -> 1 tap (the 1.0.2 sound, bit-
     * identical), 127 -> 488. Double math; the same expression prints the readout in synth.c's
     * `_display` (keep the two in sync). */
    const int gverb_len = (int)(pow((double)PP(P_GVERB_LENGTH) / 127.0, 2.0) * 487.0) + 1;
    /* The stereo widener knob (1.0.2): 0 = the stage is skipped entirely (exact bypass). */
    const float width01 = PP(P_WIDTH) * (1.0f / 127.0f);
    /* The stereo delay (spec §8, LAST in the chain): the two times (1..1000 ms, a log knob -- the
     * mapping lives here, the module takes milliseconds), the detune (0..1 -> the vendor's +-2.5%
     * quadratic taper), the feedback (capped at 0.95, the spec's runaway-tail clamp) and the mix
     * (0 = the module is skipped entirely: exact bypass and no CPU). */
    /* Per-side delay time (1.0.2): a synced side quantizes its knob to the shared division grid
     * (18 steps, 4 bars .. 1/32 trip; the switch is the off) and follows the BPM; a free side keeps
     * 1.0.1's log law verbatim. Either way it clamps to the line's cap: the horizon math below must
     * never see 8000 ms for a synced "4 bars" (design section 8). The quantize expression is
     * duplicated in synth.c's display branch: keep the two in sync. */
    const int dly_l_step = (int)(PP(P_DELAY_L) * 17.0f / 127.0f + 0.5f);
    const int dly_r_step = (int)(PP(P_DELAY_R) * 17.0f / 127.0f + 0.5f);
    float dly_l_ms = PP(P_DELAY_L_SYNC) > 0.5f
                     ? SD_DIV_BEATS[dly_l_step] * 60000.0f / bpm
                     : powf(1000.0f, PP(P_DELAY_L) * (1.0f / 127.0f));
    float dly_r_ms = PP(P_DELAY_R_SYNC) > 0.5f
                     ? SD_DIV_BEATS[dly_r_step] * 60000.0f / bpm
                     : powf(1000.0f, PP(P_DELAY_R) * (1.0f / 127.0f));
    if (dly_l_ms > 1000.0f) dly_l_ms = 1000.0f;
    if (dly_r_ms > 1000.0f) dly_r_ms = 1000.0f;
    const float dly_det = PP(P_DELAY_DETUNE) * (1.0f / 127.0f);
    /* the delay's feedback (1.0.3, design §2): the two loops each have their own knob. The old
     * delay_fb param is retired -- kept in the enum for append-only compat, read by nothing (its
     * LFO dest modulates nothing). At equal knob values dly_step reduces to the 1.0.2 expression,
     * so a chunk saved with delay_fb = N sounds like both knobs at N. */
    const float dly_fb_l = PP(P_DELAY_L_FB) * (0.95f / 127.0f);
    const float dly_fb_r = PP(P_DELAY_R_FB) * (0.95f / 127.0f);
    const float dly_wet = PP(P_DELAY_WET) * (1.0f / 127.0f);
    float dly_in_peak = 0.0f, dly_wet_peak = 0.0f;   /* the block's peaks: the tail-liveness clock */
    float verb_in_peak = 0.0f, verb_wet_peak = 0.0f; /* the verb's own tail-liveness peaks (1.0.2) */
    /* The output stage: the pan (equal power: -64 = hard left, 0 = centre, +63 = hard right) and the
     * level (0 .. ~1.4x, the init patch's 100 is a little over unity). The "out_sel" knob is state-only:
     * the MPC sums its three output buses to the main pair, so all three selections sound the same. */
    const float pan_theta = (PP(P_OUT_PAN) + 64.0f) * (TWO_PI * 0.25f * (1.0f / 128.0f));
    const float pan_l = cosf(pan_theta);
    const float pan_r = sinf(pan_theta);
    const float vol = 1.4f * (PP(P_OUT_VOL) * (1.0f / 127.0f));

    /* The env's start is clamped to the filter's range, so the sweep always starts inside it and decays
     * back to the cutoff the knob (and the velocity) asked for. */
    /* The filters' coefficients: once a block, at the env's current value. A Direct Form I biquad's
     * coefficients must NOT be re-computed per sample: per-sample modulation parametrically pumps a loud
     * coherent input (the swept cutoff crosses the tone and injects energy no frozen coefficient set
     * would), ringing the voice up to the state clamp long after the env has settled. At block rate the
     * filter is LTI between updates, each update step is ~3 ms (the shortest env tau is ~2 ms, so the
     * fastest sweeps get a couple of steps), and the per-sample expf() calls disappear (two per sample
     * before: a real CPU saving on the MPC). */
    if (fenv_on) {
        fenv0 = clampf(cut_base + fenv_amt, FILT_MIN_HZ, FILT_MAX_HZ) - cut_base;
        v->filt.env = fenv0 * expf(-v->fenv_t / tau_f);
    } else v->filt.env = 0.0f;
    if (nfenv_on) {
        nfenv0 = clampf(ncut_base + nfenv_amt, FILT_MIN_HZ, FILT_MAX_HZ) - ncut_base;
        v->nfil.env = nfenv0 * expf(-v->nfenv_t / ntau_f);
    } else v->nfil.env = 0.0f;
    filt_set(&v->filt, clampf(cut_base + v->filt.env, FILT_MIN_HZ, FILT_MAX_HZ), q, mode);
    filt_set(&v->nfil, clampf(ncut_base + v->nfil.env, FILT_MIN_HZ, FILT_MAX_HZ), nq, nmode);

    /* The EQ's coefficients: once a block (the EQ has no envelope), one set per band. */
    for (i = 0; i < 3; i++) eq_set(&v->eq[i], eq_hz[i], eq_db[i], eq_q[i]);

    for (i = 0; i < frames; i++) {
        float w, inc, osc, noise, out, ol, orr;

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

        /* The oscillator: a plain sine (the shape knob's waveform-warp role moved to the amp env). */
        v->phase += inc;
        v->phase -= floorf(v->phase);
        osc = sinf(TWO_PI * v->phase);

        /* The osc amp env: hold at full for the shape's hold, then release exponentially, and reach
         * exactly zero at T (the voice must fall silent). At shape 0 this is one multiply, exactly
         * 1.0.0's coef_amp update. */
        v->t_amp += DT;
        if (v->t_amp >= T_amp) v->amp = 0.0f;
        else if (v->t_amp >= hold_amp) v->amp *= coef_tail;
        else v->amp = 1.0f;

        /* The noise: the colour's leaky integrator, then the amp envelope, then its own filter. */
        v->brown += a_col * (w - v->brown);
        v->t_nenv += DT;
        v->nenv = env_level(v->t_nenv, ta_n, tau_n, g_n);
        noise = filt_step(&v->nfil, v->brown * v->nenv);

        /* The noise filter env's clock (the coefficients follow at the block rate). */
        if (nfenv_on) v->nfenv_t += DT;

        /* The osc branch's filter: it colours the oscillator only, the velocity's level rides on top. */
        osc = filt_step(&v->filt, osc) * v->amp;

        /* The filter env's clock: the offset decays to zero, so the cutoff sweeps back to the knob's. */
        if (fenv_on) v->fenv_t += DT;

        /* The mix: both branches are filtered before they meet. */
        out = osc * osc_gain + noise * noise_gain;
        out *= v->vel_vol;

        /* The stutter gate: it chops the voice's amplitude (the roll/echo-machine effect). */
        out = fx_stutter_step(&v->stut, out, stut_rate, stut_depth);

        /* The waveshaper: the drive knob's saturation (exactly unity when the knob is at zero). */
        out = fx_dist(out, drive01);

        /* The tone knob's one-pole lowpass (1.0.2), right after the shaper; 127 = exact bypass. */
        if (tone127 < 127.0f) out = fx_tone(out, &v->tone_y, tone_a);

        /* The three peaking EQ bands, in series (the FX section's EQ). */
        out = eq_step(&v->eq[0], out);
        out = eq_step(&v->eq[1], out);
        out = eq_step(&v->eq[2], out);

        /* The FX row: ring mod, operator FM, comb — each exact bypass at 0. The FM operator is gated
         * by the osc amp env (g * v->amp): it dies with the envelope, so a hit that has run out leaves
         * no 150 Hz drone behind (1.0.2). The gated verb left this row: it now runs after the delay. */
        out = fx_ring(&v->ring, out, ring_hz, ring_wet);
        out = fx_fm(&v->fm, out, fm_idx, fm_g * v->amp);
        out = fx_comb(&v->comb, out, comb_ms, 0.75f, comb01);

        /* The level and the pan: the voice's mono signal becomes stereo here (spec §8: everything up
         * to and including the comb is mono; the delay runs true stereo after the pan, its wet
         * independent of the pan; the verb follows the delay in stereo). */
        out *= vol;
        ol = out * pan_l;
        orr = out * pan_r;

        /* The stereo delay: last in the chain (wet 0 = the module is skipped entirely). Its input and
         * wet peaks feed the tail-liveness clock below: a hit whose dry has died must live on until
         * every written sample has been heard out of the line. */
        if (fabsf(ol) > dly_in_peak) dly_in_peak = fabsf(ol);
        if (fabsf(orr) > dly_in_peak) dly_in_peak = fabsf(orr);
        if (dly_wet > 0.0f) {
            dly_step(&v->dly, ol, orr, dly_l_ms, dly_r_ms, dly_det, dly_fb_l, dly_fb_r, dly_wet, &ol, &orr);
            if (fabsf(ol) > dly_wet_peak) dly_wet_peak = fabsf(ol);
            if (fabsf(orr) > dly_wet_peak) dly_wet_peak = fabsf(orr);
        }

        /* The gated verb (1.0.2): Bright Ambience 3 after the delay, its input the post-delay stereo
         * signal (design section 7). wet 0 = the module is skipped entirely. Its input and wet peaks
         * feed the verb's own tail-liveness clock below — the tap keeps sounding long after the dry
         * has died, and the ring empties on its own. */
        if (gverb_m > 0.0f) {
            if (fabsf(ol) > verb_in_peak) verb_in_peak = fabsf(ol);
            if (fabsf(orr) > verb_in_peak) verb_in_peak = fabsf(orr);
            verb_step(&v->verb, ol, orr, gverb_len, gverb_m, &ol, &orr);
            if (fabsf(ol) > verb_wet_peak) verb_wet_peak = fabsf(ol);
            if (fabsf(orr) > verb_wet_peak) verb_wet_peak = fabsf(orr);
        }

        /* The stereo widener (1.0.2): M/S, the side scaled 1x..2x, the mid untouched (mono-safe).
         * The chain's last stage before the choke fade. width 0 = exact bypass. */
        fx_width(&ol, &orr, width01);

        /* The choke fade AFTER the delay: it lives in the voice's own output path (never in the master
         * bus, which would fade the choking voice's own hit as well) and it must cut the delay's
         * feedback tails too -- fading the input would silence the dry while the loop kept ringing.
         * The fade is a raised cosine over 5 ms, so it starts and ends without a step. */
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
        ol *= v->choke_gain;
        orr *= v->choke_gain;

        bus[2 * i] += ol;
        bus[2 * i + 1] += orr;
    }

    /* The delay's tail-liveness clock: any block that carries energy (into the line, or out of it)
     * resets it. The voice may drop once it has been quiet longer than the current delay time -- that
     * horizon covers the pending samples still travelling down the line, which the wet output alone
     * cannot see (at full wet the output is silent until the first echo arrives). */
    if (dly_wet > 0.0f && (dly_in_peak > ENV_FLOOR || dly_wet_peak > ENV_FLOOR)) v->dly_quiet_t = 0.0f;
    else if (dly_wet > 0.0f) v->dly_quiet_t += (float)frames * DT;

    /* The verb's tail-liveness clock, same pattern: any block with energy into or out of the verb
     * resets it. VERB_HORIZON_S covers the ring's decay past the last energy. */
    if (gverb_m > 0.0f && (verb_in_peak > ENV_FLOOR || verb_wet_peak > ENV_FLOOR)) v->verb_quiet_t = 0.0f;
    else if (gverb_m > 0.0f) v->verb_quiet_t += (float)frames * DT;

    /* The one-shot is over when nothing audible is left: the voice drops out of the 12-voice loop. */
    {
        const int amp_dead = (v->amp < ENV_FLOOR);
        const int noise_dead = (v->t_nenv > ta_n) && (v->nenv < ENV_FLOOR);
        const int src_dead = (amp_dead || osc_gain <= 0.0f) && (noise_dead || noise_gain <= 0.0f);
        const float horizon = 0.001f * (dly_l_ms > dly_r_ms ? dly_l_ms : dly_r_ms) + 0.02f;
        const int delay_alive = (dly_wet > 0.0f) && (v->dly_quiet_t <= horizon);
        const int verb_alive = (gverb_m > 0.0f) && (v->verb_quiet_t <= VERB_HORIZON_S);
        if (src_dead && !delay_alive && !verb_alive) v->sounding = 0;
        if (v->choke_gain <= 0.0f) v->sounding = 0;   /* the choke fade has run out: the voice is silent */
    }
}
