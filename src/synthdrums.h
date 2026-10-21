/* MPC Synthdrums -- the plugin's state: twelve drum voices, the selected voice, the keyboard base note and the
 * display revision. src/synth.c exposes it to the host as mpc_engine() (wrapper/engine.h). */
#pragma once

#include <stdint.h>
#include "params_gen.h"

/* The engine runs at 44100 Hz (the wrapper's contract). */
#define SD_SR 44100.0f
#define TWO_PI 6.28318530717958648f

/* The filter's range: the cutoff knob's top (16 kHz) plus the filter env's reach (4 kHz) is clamped here. */
#define FILT_MIN_HZ 20.0f
#define FILT_MAX_HZ 18000.0f

/* The edit panel is one contiguous PK_PATCH block (P_PITCH .. P_CHOKE): every parameter indexed by
 * (param - P_EDIT_FIRST) lives in the selected voice's p[]. */
_Static_assert(P_CHOKE - P_EDIT_FIRST + 1 == EDIT_PARAMS, "the edit panel must be one contiguous PK_PATCH block");

/* src/filter.c: the voice's biquad filter (RBJ's cookbook: 12 dB/oct, low pass / band pass / high pass with
 * a resonant peak). The coefficients are normalized (a0 = 1), the delay line is Direct Form I and the env
 * field holds the filter envelope's current cutoff offset, in Hz (the voice decays it and asks for a new
 * coefficient set as it sweeps). */
typedef struct {
    float b0, b1, b2, a1, a2;   /* the normalized coefficients */
    float x1, x2, y1, y2;       /* the delay line */
    float env;                  /* the filter env's cutoff offset, in Hz */
} Filt;

void filt_set(Filt *f, float cutoff_hz, float q, int mode /*0=LP,1=BP,2=HP*/);
float filt_step(Filt *f, float x);

/* src/fx.c -- the voice's effects (the chain: sources -> filter -> stutter -> dist -> EQ -> pan/vol).
 *
 * The stutter: a square gate whose phase runs at the "length" knob's rate. Only the phase is state:
 * the gate's shape (the 5 ms raised-cosine edges) follows from the rate, so the struct stays this small.
 * The phase is reset at every trigger, so two hits with the same settings are bit-identical. */
typedef struct {
    float phase;            /* the gate's phase, 0..1 */
} Stutter;
float fx_stutter_step(Stutter *s, float x, float rate_hz, float depth);

/* The waveshaper: a tanh-style curve with a pre-gain and a makeup. drive01 = 0 is exactly unity. */
float fx_dist(float x, float drive01);

/* The peaking EQ: an RBJ bell. The brief's four state fields come first, in the brief's order: x1 and bx1
 * are the input's delays (n-1 and n-2), y1 and by1 the output's (n-1 and n-2). The five RBJ coefficients
 * follow: they cannot live in the four state fields, so the struct carries them too (eq_set fills them,
 * eq_step reads them). */
typedef struct {
    float x1, y1;       /* the input's delay (n-1) and the output's delay (n-1) */
    float bx1, by1;     /* the input's delay (n-2) and the output's delay (n-2) */
    float b0, b1, b2, a1, a2;   /* the RBJ coefficients, normalized (a0 = 1) */
} PeakEQ;
void eq_set(PeakEQ *e, float hz, float gain_db, float q);
float eq_step(PeakEQ *e, float x);

/* The EQ's range: the "freq" knob's log sweep, 40 Hz .. 12 kHz. */
#define EQ_MIN_HZ 40.0f
#define EQ_MAX_HZ 12000.0f

/* The choke fade: 5 ms, from full level to silence. */
#define CHOKE_FADE_S 0.005f

typedef struct {
    float p[EDIT_PARAMS];   /* the voice's sound: P_PITCH .. P_CHOKE, in parameter order */
    int sounding;           /* a trigger (a pad, TRIG) hit this voice and its one-shot has not finished yet */
    /* The voice's DSP state. sd_voice_note() resets all of it at every trigger, so two hits with the same
     * settings render bit-identical audio (the engine's per-hit determinism guarantee). */
    int index;              /* the voice's number 0..11: its own key is base_note + index */
    int base_note;          /* the device-level base note (the caller keeps it current) */
    int note;               /* the note that triggered the voice (pitch_track's reference) */
    float vel;              /* the trigger's velocity, 0..1 */
    float vel_vol;          /* the velocity crossfade scalars: 1 - d + d*vel, with d = depth/127 */
    float vel_osc;
    float vel_noise;
    float vel_penv;
    float phase;            /* the oscillator's phase, 0..1 */
    float amp;              /* the osc amp envelope's level */
    float penv;             /* the pitch envelope's depth, 0..1 */
    float nenv;             /* the noise amp envelope's level */
    float t_penv;           /* the envelopes' ages, in seconds */
    float t_nenv;
    float brown;            /* the noise colour's leaky integrator */
    float nfilt;            /* the noise tone filter's state */
    uint32_t lfsr;          /* the noise LFSR (re-seeded at every trigger) */
    Filt filt;              /* the voice's filter: the coefficients, the delay line and the filter env */
    float fenv_t;           /* the filter env's age, in seconds */
    Stutter stut;           /* the stutter gate's phase */
    PeakEQ eq;              /* the peaking EQ's coefficients and delay line */
    float choke_gain;       /* the choke fade's multiplier: 1 = free, 0 = silenced */
    float choke_t;          /* the choke fade's age, in seconds */
    int choke_fading;       /* the choke fade is running: the voice is on its way to silence */
} Voice;

typedef struct {
    Voice v[12];
    int sel;                /* the selected voice, 0-based (the UI shows sel + 1) */
    int base_note;          /* MIDI note that plays v[0]; the other voices fold by 12 (device-level: survives KIT INIT) */
    unsigned rev;           /* display_rev: bumped when the engine changes values by itself */
    unsigned nan_count;     /* the samples the master bus had to silence (not finite): the test watches this */
} Synth;

extern const float INIT_PATCH[EDIT_PARAMS];   /* PDEF's defaults in P_ order */
void patch_init_voice(Voice *v);
void patch_init_kit(Synth *s);

/* src/voice.c: one voice's DSP -- the oscillator, the noise, the pitch and amp envelopes, the x-mod and the
 * velocity routings. Nothing here allocates and nothing here touches the other voices. */
void sd_voice_reset(Voice *v);                              /* zero the DSP state */
void sd_voice_note(Voice *v, float vel);                    /* (re)trigger: restart everything, keep the velocity scalars */
void sd_voice_render(Voice *v, float *bus, int frames);   /* add the voice's signal to the stereo bus (interleaved: 2*frames floats) */
