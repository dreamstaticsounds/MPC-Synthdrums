/* MPC Synthdrums -- the init patch and kit. Keep INIT_PATCH in step with PDEF's defaults in src/params_gen.h
 * (both are generated from tools/gen_params.py's single source of truth). */
#include "synthdrums.h"

const float INIT_PATCH[EDIT_PARAMS] = {
    [P_VEL_TO_VOL] = 127,
    [P_VEL_TO_CUT] = 0,
    [P_VEL_TO_NOISE] = 0,
    [P_VEL_TO_OSC] = 0,
    [P_VEL_TO_PENV] = 0,
    [P_SRC_MIX] = 0,
    [P_OSC_DECAY] = 64,
    [P_OSC_SHAPE] = 0,
    [P_OSC_XMOD] = 0,
    [P_PITCH] = 0,
    [P_PITCH_TRACK] = 0,
    [P_PENV_ATTACK] = 0,
    [P_PENV_DECAY] = 0,
    [P_PENV_SHAPE] = 64,
    [P_PENV_AMOUNT] = 0,
    [P_NOISE_COLOUR] = 64,
    [P_NOISE_ATTACK] = 0,
    [P_NOISE_DECAY] = 64,
    [P_NOISE_SHAPE] = 64,
    [P_NOISE_FILTER] = 127,
    [P_STUTTER_LENGTH] = 64,
    [P_STUTTER_STRENGTH] = 0,
    [P_FILTER_MODE] = 0,
    [P_FILTER_RES] = 0,
    [P_FILTER_CUTOFF] = 127,
    [P_FENV_AMOUNT] = 0,
    [P_FENV_DECAY] = 64,
    [P_DIST_DRIVE] = 0,
    [P_EQ_FREQ] = 64,
    [P_EQ_GAIN] = 0,
    [P_EQ_Q] = 64,
    [P_OUT_PAN] = 0,
    [P_OUT_VOL] = 100,
    [P_OUT_SEL] = 0,
    [P_CHOKE] = 0,
};

void patch_init_voice(Voice *v)
{
    int k;
    for (k = 0; k < EDIT_PARAMS; k++) v->p[k] = INIT_PATCH[k];
    v->sounding = 0;
}

/* The whole kit back to INIT: every voice and the selector. base_note is a device-level setting and survives. */
void patch_init_kit(Synth *s)
{
    int i;
    for (i = 0; i < 12; i++) patch_init_voice(&s->v[i]);
    s->sel = 0;
}
