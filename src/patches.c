/* MPC Synthdrums -- the init patch and kit. INIT_PATCH itself is generated from tools/gen_params.py's
 * defaults into src/params_gen.h (p[] edit order), included via synthdrums.h. */
#include "synthdrums.h"

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
