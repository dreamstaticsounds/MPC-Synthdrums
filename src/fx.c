/* MPC Synthdrums -- the voice's effects: the stutter gate, the waveshaper, the peaking EQ and the FX row
 * (ring mod, operator FM, comb, gated reverb).
 *
 * They sit between the voice's filters and its pan, in chain order:
 *
 *   sources -> filters -> stutter -> dist -> tone -> EQ x3 -> ring -> FM (gated by the osc amp env) -> comb -> gated reverb -> vol/pan
 *
 * Stutter: a square gate whose rate the "length" knob sets (0.5 .. 40 Hz, an exponential sweep: the
 * roll/echo-machine feel) and whose depth the "strength" knob sets (0 = off, exactly; 1 = the gate
 * closes completely between the bursts). The gate's edges are 5 ms raised cosines, so the gate never
 * clicks: a hard edge would step the signal by its full amplitude and spray that step over the spectrum
 * as a click. The gate's phase is per-hit state and is reset at every trigger, so two hits with the same
 * settings are bit-identical.
 *
 * Dist: a tanh-style waveshaper. The drive knob is a pre-gain (1 + 8*drive) into the curve, and the
 * makeup (1 + 8*drive)^-0.5 keeps the perceived level roughly where it was: the curve saturates, so the
 * harmonics grow as the fundamental shrinks. drive = 0 is exactly unity (the signal passes through
 * untouched, so the knob is a true bypass).
 *
 * EQ: an RBJ peaking (bell) filter -- one band, the classic three-knob EQ. A gain of 0 dB is exactly
 * unity (the RBJ peaking filter's numerator and denominator coincide when A = 1), so the knob is a true
 * bypass at the centre.
 */
#include <math.h>
#include "synthdrums.h"

/* The gate's edges: 5 ms of raised cosine, whatever the rate is. */
#define STUT_EDGE_S 0.005f

float fx_stutter_step(Stutter *s, float x, float rate_hz, float depth)
{
    float g;

    /* Off: the signal passes through exactly (no multiply, no rounding, no state change). */
    if (depth <= 0.0f) return x;
    if (rate_hz <= 0.0f) return x;

    {
        /* The edge's half width, in phase units: the edge lasts 5 ms whatever the gate's rate is. */
        const float e = 0.5f * STUT_EDGE_S * rate_hz;
        const float p = s->phase;

        if (e >= 0.25f) {
            /* A very fast gate: the two edges would overlap, so the gate is a plain square. */
            g = (p < 0.5f) ? 1.0f : 0.0f;
        } else if (p < e) {
            /* The rising edge (the off -> on transition), centred on the phase's wrap. */
            g = 0.5f * (1.0f - cosf(TWO_PI * 0.5f * (p + e) / (2.0f * e)));
        } else if (p < 0.5f - e) {
            g = 1.0f;                       /* open */
        } else if (p < 0.5f + e) {
            /* The falling edge (the on -> off transition), centred on the half phase. */
            g = 0.5f * (1.0f + cosf(TWO_PI * 0.5f * (p - (0.5f - e)) / (2.0f * e)));
        } else {
            g = 0.0f;                       /* closed */
        }

        s->phase += rate_hz * (1.0f / SD_SR);
        while (s->phase >= 1.0f) s->phase -= 1.0f;
    }

    /* The depth scales the gate: 0 leaves the signal alone, 1 closes it completely. */
    return x * (1.0f - depth * (1.0f - g));
}

float fx_dist(float x, float drive01)
{
    float pre, makeup;

    /* Clean: exactly unity, so the knob is a true bypass. */
    if (drive01 <= 0.0f) return x;

    pre = 1.0f + 8.0f * drive01;          /* the pre-gain: 1 .. 9 */
    makeup = 1.0f / sqrtf(pre);            /* the makeup: (1 + 8*drive)^-0.5 */
    return tanhf(pre * x) * makeup;
}

/* 1.0.2: the dist tone knob's one-pole lowpass, right after the waveshaper. The coefficient
 * a = 1 - exp(-2*pi*fc/sr) is computed once a block by the caller; the knob at 127 skips this
 * stage entirely (exact bypass), so the coefficient is never applied with a "neutral" value. */
float fx_tone(float x, float *y, float a)
{
    *y += a * (x - *y);
    return *y;
}

void eq_set(PeakEQ *e, float hz, float gain_db, float q)
{
    float A, w0, c, s, alpha, a0;

    if (hz < EQ_MIN_HZ) hz = EQ_MIN_HZ;
    else if (hz > EQ_MAX_HZ) hz = EQ_MAX_HZ;
    if (q < 0.1f) q = 0.1f;   /* a guard: alpha = sin(w0)/(2Q) would divide by zero */

    A = powf(10.0f, gain_db / 40.0f);   /* the amplitude of the bell (1 = 0 dB) */
    w0 = TWO_PI * hz / SD_SR;
    c = cosf(w0);
    s = sinf(w0);
    alpha = s / (2.0f * q);
    a0 = 1.0f + alpha / A;

    /* RBJ's peaking EQ. With A = 1 (0 dB) the numerator and the denominator are the same, so the
     * filter is exactly unity: the knob's centre is a true bypass. */
    e->b0 = (1.0f + alpha * A) / a0;
    e->b1 = (-2.0f * c) / a0;
    e->b2 = (1.0f - alpha * A) / a0;
    e->a1 = (-2.0f * c) / a0;
    e->a2 = (1.0f - alpha / A) / a0;
}

float eq_step(PeakEQ *e, float x)
{
    /* Direct Form I: the state is the input's and the output's last two samples. */
    const float y = e->b0 * x + e->b1 * e->x1 + e->b2 * e->bx1 - e->a1 * e->y1 - e->a2 * e->by1;
    e->bx1 = e->x1;
    e->x1 = x;
    e->by1 = e->y1;
    e->y1 = y;
    return y;
}

/* ---- The FX row (spec §8): ring mod, operator FM, comb, gated reverb ---- */

/* Ring mod: the carrier multiplies the voice, y = x*((1-g) + g*sin(2*pi*ph)), ph advancing at hz. The
 * depth ramps to its target over 1 ms — a step in the multiplier would click mid-sample. g == 0 is
 * exact bypass (and parks the ramp at 0, so the next engage ramps from silence). */
float fx_ring(RingSt *r, float x, float hz, float g)
{
    const float step = 1.0f / (SD_SR * 0.001f);   /* the 1 ms ramp's per-sample step */

    if (g == 0.0f) { r->g = 0.0f; return x; }
    if (r->g < g) { r->g += step; if (r->g > g) r->g = g; }
    else if (r->g > g) { r->g -= step; if (r->g < g) r->g = g; }
    r->ph += hz / SD_SR;
    r->ph -= floorf(r->ph);
    return x * ((1.0f - r->g) + r->g * sinf(TWO_PI * r->ph));
}

/* Operator FM: a 150 Hz operator modulated by the voice itself, blended in: y = x + g*sin(2*pi*ph + I*x).
 * g == 0 is exact bypass (the operator restarts at the next engage). The caller scales g by the voice's
 * osc amp env, so the operator dies with the envelope (1.0.2: no drone after the env ends). */
float fx_fm(FmSt *f, float x, float idx, float g)
{
    float y;

    if (g == 0.0f) { f->ph = 0.0f; return x; }
    y = sinf(TWO_PI * f->ph + idx * x);
    f->ph += 150.0f / SD_SR;
    f->ph -= floorf(f->ph);
    return x + g * y;
}

/* Comb: a feedback delay line (the write is x + fb*read, the output is the read head — a resonant
 * comb). The delay moves with the knob's ms; the read head interpolates linearly. The buffer covers
 * 30 ms (1323 samples) plus interp margin; g == 0 is exact bypass (a retrigger resets the buffer, so
 * an old tail can never leak into a fresh hit). */
float fx_comb(CombSt *cb, float x, float ms, float fb, float g)
{
    float rp, s;
    int i0;

    if (g == 0.0f) return x;
    rp = (float)cb->w - ms * (SD_SR * 0.001f);   /* the read head: d samples behind the write */
    while (rp < 0.0f) rp += (float)COMB_BUF;
    i0 = (int)rp;
    s = cb->buf[i0] + (cb->buf[(i0 + 1) % COMB_BUF] - cb->buf[i0]) * (rp - (float)i0);
    cb->buf[cb->w] = x + fb * s;
    cb->w = (cb->w + 1) % COMB_BUF;
    return s;
}

/* Gated reverb: a Schroeder bank (4 combs at 29.7/37.1/41.1/43.7 ms, fb 0.80, then 2 allpass at
 * 5.0/1.7 ms, g 0.7) behind a per-hit gate: open for GVERB_GATE_S from the hit, then a 5 ms fade —
 * the wet stops dead (the classic gated look). The dry always passes (x*(1-m)); m == 0 is exact
 * bypass and skips the whole bank, and once the gate has shut the bank is skipped too (the output is
 * dry-only then; a retrigger resets it). */
float fx_gverb(GVerbSt *gv, float x, float m, float t_hit)
{
    static const float c_ms[4] = { 29.7f, 37.1f, 41.1f, 43.7f };   /* documented for the reader only */
    static const int c_n[4] = { 1310, 1636, 1812, 1927 };          /* ceil(ms*44.1); each < 2048 */
    static const int a_n[2] = { 220, 75 };                         /* 5.0 / 1.7 ms; each < 224 */
    float wet;
    int j;

    /* The struct's buffers must cover their delays (5.0 ms is 221 samples — 128 was too small). */
    _Static_assert(221 <= 224, "the allpass buffers cover their delays");
    _Static_assert(1927 <= 2048, "the comb buffers cover their delays");
    _Static_assert(1323 + 1 <= COMB_BUF, "the comb buffer covers 30 ms plus the interp read");
    (void)c_ms;
    if (m == 0.0f) return x;
    if (t_hit >= GVERB_GATE_S + 0.005f) return x * (1.0f - m);   /* the gate has shut: the wet is 0 */

    wet = 0.0f;
    for (j = 0; j < 4; j++) {
        const int D = c_n[j], w = gv->ci[j];
        int rp = w - D;
        float y;
        if (rp < 0) rp += D;
        y = gv->c[j][rp];
        gv->c[j][w] = x + 0.80f * y;
        gv->ci[j] = (w + 1) % D;
        wet += y;
    }
    wet *= 0.25f;
    for (j = 0; j < 2; j++) {
        const int D = a_n[j], w = gv->ai[j];
        const float y = -0.7f * wet + gv->a[j][w];   /* buf[w] holds what was written D samples ago */
        gv->a[j][w] = wet + 0.7f * y;
        gv->ai[j] = (w + 1) % D;
        wet = y;
    }

    /* The gate: 1 while open, a 5 ms linear fade, then 0 (the tail is cut dead). */
    {
        float gate = 1.0f;
        if (t_hit > GVERB_GATE_S) gate = 1.0f - (t_hit - GVERB_GATE_S) * (1.0f / 0.005f);
        return x * (1.0f - m) + wet * (m * gate);
    }
}
