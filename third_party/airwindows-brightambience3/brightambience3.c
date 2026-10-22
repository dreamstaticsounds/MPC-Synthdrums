/* MPC Synthdrums -- the gated verb: Airwindows Bright Ambience 3, vendored and adapted.
 *
 * Adapted from Airwindows Bright Ambience 3
 * (plugins/LinuxVST/src/BrightAmbience/BrightAmbience3Proc.cpp), Copyright (c) 2016 airwindows
 * -- MIT License (third_party/airwindows-brightambience3/LICENSE).
 *
 * Adapted to this engine's contract -- float, fixed 44.1 kHz, one stereo sample per call, per-hit
 * determinism:
 *   - three of the vendor's four knobs are baked: start = (int)(0.13*400)+88 = 140 (A), length =
 *     (int)(pow(0,2)*487)+1 = 1 (B), feedbackAmount = 0*0.25 = 0 (C). What remains is ONE prime tap
 *     per channel (primeL[140] = 6247, primeR[140] = 6229 -- the vendor taps a different prime per
 *     channel: stereo decorrelation) through the vendor's fixed 1 kHz "figure" resonator, wet
 *     blended by the GVERB knob (the vendor's D);
 *   - the vendor's 32768-long descending-gcount rings collapse to ascending float rings exactly one
 *     tap long: read-then-write, so the read returns the sample written exactly tap steps ago -- the
 *     same modulo distance as the vendor's pL[count + primeL[start]] with its wrap;
 *   - the figure's coefficients come from the vendor's own per-block formula (double math) with the
 *     baked values, computed once by verb_reset instead of per block; no magic numbers;
 *   - the sample-rate plumbing (cycleEnd/lastRef interpolation), the denormal dither and the VST
 *     plumbing are gone (cycleEnd is 1 at 44.1 kHz, so lastRef collapses to identity anyway).
 *
 * Per-hit determinism: verb_reset zeroes the rings and the resonator state and recomputes the
 * coefficients identically, so two hits with the same settings are bit-identical. The wet path's
 * state is float (the vendor uses double); the coefficients are computed in double and stored
 * float, once per reset.
 */
#include <math.h>
#include <string.h>
#include "synthdrums.h"

#define BA3_START 140            /* (int)(A * 400) + 88 with the baked A = 0.13 */
#define BA3_LENGTH 1             /* (int)(pow(B,2) * 487) + 1 with the baked B = 0 */
#define BA3_FEEDBACK 0.0         /* C * 0.25 with the baked C = 0 */

void verb_reset(VerbSt *v)
{
    /* The figure's resonance from the vendor's formula with the baked length/feedback: with
     * feedback 0 it is the constant 0.01, kept symbolic so the numbers stay traceable. */
    const double res = pow((double)BA3_LENGTH * 0.037 * BA3_FEEDBACK, 2) + 0.01;
    const double K = tan(M_PI * (1000.0 / 44100.0));   /* the figure's fixed frequency, 1 kHz */
    const double norm = 1.0 / (1.0 + K / res + K * K);

    memset(v, 0, sizeof *v);    /* rings + resonator state: bit-exact per hit */
    v->b2 = (float)(K / res * norm);
    v->b4 = -v->b2;
    v->b5 = (float)(2.0 * (K * K - 1.0) * norm);
    v->b6 = (float)((1.0 - K / res + K * K) * norm);
}

void verb_step(VerbSt *v, float inl, float inr, float wet, float *ol, float *orr)
{
    float tl, tr, ts;

    if (wet <= 0.0f) { *ol = inl; *orr = inr; return; }   /* exact bypass; the rings are untouched */

    /* read the tap (the sample written exactly tap steps ago), then write, then advance */
    tl = v->l[v->wl];
    tr = v->r[v->wr];
    v->l[v->wl] = inl;
    v->r[v->wr] = inr;
    v->wl = (v->wl + 1) % BA3_TAP_L;
    v->wr = (v->wr + 1) % BA3_TAP_R;

    /* the figure: the vendor's 2-pole update, per channel (transposed direct form; the vendor
     * carries the state in double, this port in float to match the engine's state width) */
    ts = tl * v->b2 + v->fy1;
    v->fy1 = -(ts * v->b5) + v->fy2;
    v->fy2 = tl * v->b4 - (ts * v->b6);
    tl = ts;
    ts = tr * v->b2 + v->fr1;
    v->fr1 = -(ts * v->b5) + v->fr2;
    v->fr2 = tr * v->b4 - (ts * v->b6);
    tr = ts;

    /* the vendor's dry/wet blend, exactly: wet 1 is the pure wet (no dry arithmetic) */
    if (wet != 1.0f) {
        *ol = tl * wet + inl * (1.0f - wet);
        *orr = tr * wet + inr * (1.0f - wet);
    } else {
        *ol = tl;
        *orr = tr;
    }
}
