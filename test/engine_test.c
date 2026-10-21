/* MPC Synthdrums -- offline engine test (x86): defaults, the voice selector's mirroring, the trigger, the state
 * round trip and display_rev. The voices have no DSP yet (Task 3), so render is expected to be silent. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "synthdrums.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static const mpc_engine_t *E;
static int16_t buf[256];

static void sp(void *x, const char *k, double v) { char b[32]; snprintf(b, sizeof b, "%g", v); E->set_param(x, k, b); }
static void note(void *x, int n, int vel) { uint8_t m[3] = {(uint8_t)(vel ? 0x90 : 0x80), (uint8_t)n, (uint8_t)vel}; E->midi(x, m, 3); }

typedef struct { double rms; int peak; int nan; } Stat;
static Stat run(void *x, int blocks)
{
    Stat s = {0, 0, 0};
    double acc = 0;
    long cnt = 0;
    int b, i;
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 256; i++) {
            const int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > s.peak) s.peak = v;
            acc += (double)buf[i] * buf[i];
            cnt++;
        }
    }
    s.rms = sqrt(acc / (double)cnt) / 32768.0;
    return s;
}

int main(void)
{
    E = mpc_engine();
    CHECK(E != NULL, "mpc_engine");
    if (!E) return 1;
    /* defaults */
    void *x = E->create(NULL); char b[64];
    CHECK(x != NULL, "create");
    CHECK(E->get_param(x, "osc_decay", b, sizeof b) > 0 && !strcmp(b, "64"), "init osc_decay");
    /* voice is an option param: the engine speaks the 0-based index ("0" = displayed "1") */
    CHECK(E->get_param(x, "voice", b, sizeof b) > 0 && !strcmp(b, "0"), "init voice index");
    CHECK(E->get_param(x, "base_note", b, sizeof b) > 0 && !strcmp(b, "36"), "init base_note");
    /* mirroring: edits land on the selected voice only (indices, not display numbers) */
    sp(x, "voice", 3); sp(x, "osc_decay", 42);
    sp(x, "voice", 1);
    E->get_param(x, "osc_decay", b, sizeof b); CHECK(!strcmp(b, "64"), "voice idx 1 untouched by idx 3 edit");
    sp(x, "voice", 3);
    E->get_param(x, "osc_decay", b, sizeof b); CHECK(!strcmp(b, "42"), "voice idx 3 kept its edit");
    /* trig records a hit on the selected voice; trigs always read back "0" */
    sp(x, "trig", 1);
    CHECK(E->get_param(x, "trig", b, sizeof b) > 0 && !strcmp(b, "0"), "trig reads back 0");
    CHECK(((Synth *)x)->v[3].sounding == 1, "trig hits the selected voice");
    /* state round trip: 12 distinct voices (voice values 0..11) */
    for (int v = 0; v < 12; v++) { sp(x, "voice", v); sp(x, "osc_decay", 10 + v); sp(x, "pitch", v - 6); }
    sp(x, "base_note", 40);
    char a[8192], c[8192];
    CHECK(E->get_param(x, "state", a, sizeof a) > 0, "state serializes");
    CHECK(strlen(a) < 8000, "state length %d < 8000", (int)strlen(a));
    void *y = E->create(NULL);
    E->set_param(y, "state", a);
    E->get_param(y, "state", c, sizeof c);
    CHECK(!strcmp(a, c), "state round trip");
    for (int v = 0; v < 12; v++) { char k[32]; snprintf(k, sizeof k, "%d", 10 + v); sp(y, "voice", v);
      E->get_param(y, "osc_decay", b, sizeof b); CHECK(!strcmp(b, k), "voice idx %d survived the chunk", v); }
    /* display_rev bumps on voice switch, not on plain edits */
    E->get_param(x, "display_rev", a, sizeof a); sp(x, "voice", 5);
    E->get_param(x, "display_rev", c, sizeof c); CHECK(strcmp(a, c), "display_rev bumps on voice switch");
    snprintf(a, sizeof a, "%s", c); sp(x, "osc_decay", 77);
    E->get_param(x, "display_rev", c, sizeof c); CHECK(!strcmp(a, c), "display_rev still on plain edit");
    /* kit_init resets every voice (base_note is a device-level setting and survives) */
    sp(x, "kit_init", 1);
    E->get_param(x, "osc_decay", b, sizeof b); CHECK(!strcmp(b, "64"), "kit_init reset");
    sp(x, "base_note", 36);   /* restore for later sections */
    /* MIDI folding */
    note(x, 36, 100); note(x, 48, 100); note(x, 41, 100);  /* all trigger without crash; silence for now */
    { Stat s = run(x, 4); CHECK(s.peak == 0, "render is silent before the DSP (peak %d)", s.peak); }
    E->destroy(y);
    E->destroy(x);
    printf(fails ? "FAILED: %d\n" : "PASSED\n", fails);
    return fails != 0;
}
