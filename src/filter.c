/* MPC Synthdrums -- the voice's filter: an RBJ biquad (12 dB/oct) with three modes and a resonant peak.
 *
 * The coefficients follow Robert Bristow-Johnson's cookbook: all three modes share one denominator
 * (alpha = sin(w0)/(2Q)), so only the numerator changes from mode to mode. The cutoff's range is clamped
 * to 20 Hz .. 18 kHz: the knob's top (16 kHz) plus the filter env's reach (4 kHz) is the widest the
 * cutoff ever gets.
 *
 * The band pass is RBJ's constant-0 dB peak gain variant with the numerator scaled by Q, so its resonant
 * peak is Q -- the same height as the low pass's. That keeps the modes comparable in level and makes the
 * res knob raise the ringing in every mode: turning the mode knob changes the shape of the spectrum, not
 * how loud the voice is.
 *
 * The delay line is Direct Form I (four states, five multiplies a sample). The filter env's value lives
 * in f->env (the cutoff's offset in Hz): the voice decays it and asks for a new coefficient set as the
 * cutoff sweeps.
 *
 * A swept resonator is not unconditionally stable: while an env moves the cutoff, coefficient changes can
 * inject energy no frozen coefficient set ever would. The voice therefore updates the coefficients at block
 * rate (LTI between updates), which keeps sweeps well-behaved; the state clamp here is only a safety net
 * (~36 dB over full scale): a normal voice never reaches it, and a pathological setting rings at a bounded
 * level instead of overflowing to inf (which the master bus would have to silence). */
#include <math.h>
#include "synthdrums.h"

#define FILTER_STATE_MAX 64.0f

void filt_set(Filt *f, float cutoff_hz, float q, int mode)
{
    float w0, c, s, alpha, a0, b0, b1, b2;

    if (cutoff_hz < FILT_MIN_HZ) cutoff_hz = FILT_MIN_HZ;
    else if (cutoff_hz > FILT_MAX_HZ) cutoff_hz = FILT_MAX_HZ;
    if (q < 0.1f) q = 0.1f;   /* a guard: alpha = sin(w0)/(2Q) would divide by zero */

    w0 = TWO_PI * cutoff_hz / SD_SR;
    c = cosf(w0);
    s = sinf(w0);
    alpha = s / (2.0f * q);
    a0 = 1.0f + alpha;

    switch (mode) {
    case 1:   /* BP: constant 0 dB peak gain, scaled by Q so the resonant peak is Q (like the low pass's) */
        b0 = alpha * q;
        b1 = 0.0f;
        b2 = -b0;
        break;
    case 2:   /* HP */
        b0 = (1.0f + c) * 0.5f;
        b1 = -(1.0f + c);
        b2 = b0;
        break;
    default:  /* LP */
        b0 = (1.0f - c) * 0.5f;
        b1 = 1.0f - c;
        b2 = b0;
        break;
    }

    /* a0 is divided out, so the step is five multiplies and no division. */
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = (-2.0f * c) / a0;
    f->a2 = (1.0f - alpha) / a0;
}

float filt_step(Filt *f, float x)
{
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    if (y > FILTER_STATE_MAX) y = FILTER_STATE_MAX;          /* the swept-resonator runaway guard */
    else if (y < -FILTER_STATE_MAX) y = -FILTER_STATE_MAX;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}
