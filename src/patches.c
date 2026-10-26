/* MPC Synthdrums -- the init patch and kit. INIT_PATCH itself is generated from tools/gen_params.py's
 * defaults into src/params_gen.h (p[] edit order), included via synthdrums.h. */
#include <string.h>
#include "synthdrums.h"

void patch_init_voice(Voice *v)
{
    int k;
    for (k = 0; k < EDIT_PARAMS; k++) v->p[k] = INIT_PATCH[k];
    v->sounding = 0;
}

/* The whole kit back to INIT: every voice, the selector and the master EQ (1.1.0 design §3: KIT INIT
 * resets the master EQ to flat -- the author's call; create() and deserialize() run this too, so an
 * old chunk without the meq= line lands on defaults). base_note is a device-level setting and
 * survives. */
void patch_init_kit(Synth *s)
{
    int i;
    for (i = 0; i < 12; i++) patch_init_voice(&s->v[i]);
    s->sel = 0;
    for (i = 0; i < 9; i++) s->meq[i] = PDEF[P_MEQ_HI_FREQ + i].def;
    memset(s->meq_hs, 0, sizeof s->meq_hs);   /* fresh band states: no stale ring */
    memset(s->meq_pk, 0, sizeof s->meq_pk);
    memset(s->meq_ls, 0, sizeof s->meq_ls);
}
