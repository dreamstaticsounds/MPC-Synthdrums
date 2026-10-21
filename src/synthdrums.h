/* MPC Synthdrums -- the plugin's state: twelve drum voices, the selected voice, the keyboard base note and the
 * display revision. src/synth.c exposes it to the host as mpc_engine() (wrapper/engine.h). */
#pragma once

#include <stdint.h>
#include "params_gen.h"

/* The edit panel is one contiguous PK_PATCH block (P_VEL_TO_VOL .. P_CHOKE): every parameter indexed by
 * (param - P_VEL_TO_VOL) lives in the selected voice's p[]. */
_Static_assert(P_CHOKE - P_VEL_TO_VOL + 1 == EDIT_PARAMS, "the edit panel must be one contiguous PK_PATCH block");

typedef struct {
    float p[EDIT_PARAMS];   /* the voice's sound: P_VEL_TO_VOL .. P_CHOKE, in parameter order */
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
void sd_voice_render(Voice *v, float *mono, int frames);    /* add this voice's signal to the mono bus */
