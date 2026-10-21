/* MPC Synthdrums -- the voice's effects: the stutter gate, the waveshaper and the peaking EQ.
 *
 * The three effects sit between the voice's filter and its pan:
 *
 *   sources -> filter -> stutter -> dist -> EQ -> pan/vol
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
