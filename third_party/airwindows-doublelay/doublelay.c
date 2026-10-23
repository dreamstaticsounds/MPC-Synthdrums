/* MPC Synthdrums -- the voice's stereo delay: Airwindows Doublelay, vendored and adapted.
 *
 * Adapted from Airwindows Doublelay (plugins/LinuxVST/src/Doublelay/DoublelayProc.cpp),
 * Copyright (c) 2016 airwindows -- MIT License (third_party/airwindows-doublelay/LICENSE).
 *
 * Adapted to this engine's contract -- float, fixed 44.1 kHz, one stereo sample per call, per-hit
 * determinism:
 *   - the sample-rate scaling is gone: at 44.1 kHz the vendor's cycleEnd is 1, so the doubler runs
 *     every sample and the lastRef interpolator collapses to identity -- both are removed;
 *   - the main lines span 44101 samples (1 s at 44.1 kHz; the vendor's 48005 covers 1 s at 48 kHz),
 *     and the longest unambiguous delay is 44099 samples (44100 would alias to zero);
 *   - the knob->time mapping lives in the caller: this port takes milliseconds (the vendor's Doublelay
 *     maps its knobs linearly to samples itself), and converts them to integer samples here;
 *   - the output dither and the VST plumbing are gone (the dither is inaudible and would break the
 *     engine's bit-identical-per-hit guarantee); the vendor's denormal guard is unnecessary: the
 *     voice stops rendering once the wet peak falls below 1e-4, far above denormal range;
 *   - dly_reset() initializes every field (the vendor's constructor leaves tracking[] unset and relies
 *     on the first process call's prevwidth check for the head positions), so two hits with the same
 *     settings render bit-identical audio.
 * The DSP itself is the vendor's, kept structurally verbatim: the "air" treble compensator, the
 * golden-ratio direct+cross feedback around the write, the integer read heads, and the 8-head splicing
 * doubler per channel (positions in double, as the vendor keeps them). */
#include <math.h>
#include <string.h>
#include "synthdrums.h"

/* The doubler's per-channel state: a circular read buffer double-written at [b] and [b + width] so a
 * head near the seam can interpolate across it, the 8 heads' positions, and the splice bookkeeping. */

/* The "air" compensator (vendor verbatim): the heads' interpolation costs highs; this feeds the
 * difference between consecutive inputs back in, alternating even/odd samples. */
static float dly_air(float *prev, float *even, float *odd, int flip, float in)
{
    float f = *prev - in;
    if (flip) { *even += f; *odd -= f; f = *even; }
    else { *odd += f; *even -= f; f = *odd; }
    *odd = (*odd - ((*odd - *even) / 256.0f)) / 1.0001f;
    *even = (*even - ((*even - *odd) / 256.0f)) / 1.0001f;
    *prev = in;
    return in + f;
}

/* One channel's doubler (the vendor's per-channel section, ported verbatim): write the delayed sample
 * into the circular buffer, read the 8 interpolated heads, and splice between heads as their positions
 * cross -- the speed difference is what makes the "double". Returns the doubled sample. */
static float dly_doubler(DlyChan *c, float speed, float in)
{
    float temp[8];
    int count, base;

    c->gcount++;
    for (count = 0; count < 8; count++) c->pos[count] += speed;

    /* Splice detection (vendor verbatim): did the write cursor or the head just cross somebody? */
    {
        int gplus = c->gcount + DL_ADJUST, lastplus = c->lastcount + DL_ADJUST;
        if (gplus > DL_WIDTH) gplus -= DL_WIDTH;
        if (lastplus > DL_WIDTH) lastplus -= DL_WIDTH;
        if (c->tracking[c->active] == 0.0f) {
            double posplus = c->pos[c->active] + DL_ADJUST;
            double lastposplus = c->lastpos[c->active] + DL_ADJUST;
            if (posplus > DL_WIDTH) posplus -= DL_WIDTH;
            if (lastposplus > DL_WIDTH) lastposplus -= DL_WIDTH;
            if ((double)gplus > c->pos[c->active] && (double)lastplus < c->lastpos[c->active])
                c->tracking[c->active] = 1.0f;
            if (posplus > (double)c->gcount && lastposplus < (double)c->lastcount)
                c->tracking[c->active] = 1.0f;
        }
    }

    for (count = 0; count < 8; count++) {
        if (c->pos[count] > DL_WIDTH) c->pos[count] -= DL_WIDTH;
        c->lastpos[count] = c->pos[count];
    }
    if (c->gcount < 0 || c->gcount > DL_WIDTH) c->gcount -= DL_WIDTH;
    c->lastcount = c->gcount;

    c->p[c->gcount + DL_WIDTH] = c->p[c->gcount] = in;

    /* The heads' reads: a 3-tap interpolation with the vendor's curvature hack. */
    for (count = 0; count < 8; count++) {
        double frac;
        base = (int)floor(c->pos[count]);
        frac = c->pos[count] - base;
        temp[count] = (c->p[base] * (float)(1.0 - frac)
                     + c->p[base + 1]
                     + c->p[base + 2] * (float)frac
                     - ((c->p[base] - c->p[base + 1]) - (c->p[base + 1] - c->p[base + 2])) / 50.0f) * 0.5f;
    }

    if (c->tracking[c->active] > 0.0f) {
        float out, depth;
        float crossfade = sinf(c->tracking[c->bestsplice] * 1.57f);
        out = temp[c->active] * crossfade + temp[c->bestsplice] * (1.0f - crossfade);
        for (count = 0; count < 8; count++) {
            depth = 0.5f - fabsf(temp[c->active] - temp[count]);
            if (depth > 0.0f && count != c->active) {
                c->tracking[count] -= depth / (float)DL_ADJUST;
                c->bestsplice = count;
            }
        }
        c->bestyet = 1.0f;
        for (count = 0; count < 8; count++) {
            if (c->tracking[count] < c->bestyet && count != c->active) {
                c->bestsplice = count;
                c->bestyet = c->tracking[count];
            }
        }
        if (c->tracking[c->bestsplice] < 0.0f) {
            for (count = 0; count < 8; count++) c->tracking[count] = 1.0f;
            c->active = c->bestsplice;
            c->tracking[c->active] = 0.0f;
        }
        return out;
    }
    return temp[c->active];
}

void dly_step(DlySt *d, float inl, float inr, float tl_ms, float tr_ms, float detune01, float fbl,
              float fbr, float wet, float *ol, float *or_)
{
    float dryl = inl, dryr = inr;
    float trim, speedl, speedr, xl, xr;
    int delayl, delayr, count, read;

    if (wet == 0.0f) { *ol = inl; *or_ = inr; return; }   /* exact bypass (the voice skips it too) */

    /* The detune (vendor semantics): a quadratic taper, +-2.5% at the ends -- the two channels' heads
     * pull apart in speed, which is the "double" of the name. */
    trim = detune01 * 2.0f - 1.0f;
    trim *= fabsf(trim);
    trim *= 1.0f / 40.0f;
    speedl = trim + 1.0f;
    speedr = 1.0f - trim;
    if (speedl < 0.0f) speedl = 0.0f;
    if (speedr < 0.0f) speedr = 0.0f;

    /* The knob's time, in samples: the vendor's read head is integer -- the heads do the pitch
     * shifting, not the main read. DL_LINE - 1 is the longest unambiguous delay in the span. */
    delayl = (int)(tl_ms * (SD_SR * 0.001f));
    delayr = (int)(tr_ms * (SD_SR * 0.001f));
    if (delayl < 0) delayl = 0;
    if (delayr < 0) delayr = 0;
    if (delayl > DL_LINE - 1) delayl = DL_LINE - 1;
    if (delayr > DL_LINE - 1) delayr = DL_LINE - 1;

    /* The air, left then right on the same flip (vendor order). */
    xl = dly_air(&d->airPrevL, &d->airEvenL, &d->airOddL, d->flip, inl);
    xr = dly_air(&d->airPrevR, &d->airEvenR, &d->airOddR, d->flip, inr);
    d->flip = !d->flip;

    /* the golden-ratio direct+cross feedback, per side (1.0.3 design §2): each line's two terms
     * scale by its OWN knob -- at fbl == fbr this is the 1.0.2 expression, operation for
     * operation, so equal knobs reduce bit-identically (the verb-delay golden pins it) */
    xl += d->feedbackL * (fbl * 0.618034f) + d->feedbackR * (fbl * (1.0f - 0.618034f));
    xr += d->feedbackR * (fbr * 0.618034f) + d->feedbackL * (fbr * (1.0f - 0.618034f));

    /* Write both main lines, read the delayed samples (integer, vendor), step the descending cursor. */
    if (d->dcount < 1 || d->dcount > DL_LINE) d->dcount = DL_LINE;
    d->l[d->dcount] = xl;
    d->r[d->dcount] = xr;
    count = d->dcount + delayl; read = count > DL_LINE ? count - DL_LINE : count;
    xl = d->l[read];
    count = d->dcount + delayr; read = count > DL_LINE ? count - DL_LINE : count;
    xr = d->r[read];
    d->dcount--;

    /* The doubler: one channel each; the feedback takes the doubled outputs (vendor). */
    d->feedbackL = dly_doubler(&d->lc, speedl, xl);
    d->feedbackR = dly_doubler(&d->rc, speedr, xr);

    /* Wet/dry: the dry is the ORIGINAL input (vendor: pre-air, pre-feedback). */
    *ol = d->feedbackL * wet + dryl * (1.0f - wet);
    *or_ = d->feedbackR * wet + dryr * (1.0f - wet);
}

void dly_reset(DlySt *d)
{
    /* The vendor's init (its constructor plus the first call's prevwidth block), unconditional per
     * hit: zeros everywhere, then the 8 heads at the vendor's starting positions (the fractions of the
     * width: 0, 1/3, 2/3, 1/5, 2/5, 3/5, 4/5, 1/2), the splice bookkeeping, and the cursor. */
    static const double pos0[8] = { 0, 766, 1532, 460, 920, 1380, 1840, 1150 };
    int i;
    memset(d, 0, sizeof *d);
    for (i = 0; i < 8; i++) {
        d->lc.pos[i] = d->lc.lastpos[i] = pos0[i];
        d->rc.pos[i] = d->rc.lastpos[i] = pos0[i];
    }
    d->lc.bestsplice = d->rc.bestsplice = 4;
    d->lc.bestyet = d->rc.bestyet = 1.0f;
    d->dcount = DL_LINE;
}
