/* MPC Synthdrums -- the plugin's state: twelve drum voices, the selected voice, the keyboard base note and the
 * display revision. src/synth.c exposes it to the host as mpc_engine() (wrapper/engine.h). */
#pragma once

#include "params_gen.h"

/* The edit panel is one contiguous PK_PATCH block (P_VEL_TO_VOL .. P_CHOKE): every parameter indexed by
 * (param - P_VEL_TO_VOL) lives in the selected voice's p[]. */
_Static_assert(P_CHOKE - P_VEL_TO_VOL + 1 == EDIT_PARAMS, "the edit panel must be one contiguous PK_PATCH block");

typedef struct {
    float p[EDIT_PARAMS];   /* the voice's sound: P_VEL_TO_VOL .. P_CHOKE, in parameter order */
    int sounding;           /* a trigger (a pad, TRIG) hit this voice and its one-shot has not finished yet */
    /* Task 3 adds the voice's DSP state here (its oscillators, envelopes, filter). */
} Voice;

typedef struct {
    Voice v[12];
    int sel;                /* the selected voice, 0-based (the UI shows sel + 1) */
    int base_note;          /* MIDI note that plays v[0]; the other voices fold by 12 (device-level: survives KIT INIT) */
    unsigned rev;           /* display_rev: bumped when the engine changes values by itself */
} Synth;

extern const float INIT_PATCH[EDIT_PARAMS];   /* PDEF's defaults in P_ order */
void patch_init_voice(Voice *v);
void patch_init_kit(Synth *s);
