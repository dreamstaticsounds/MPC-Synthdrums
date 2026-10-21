/* MPC Synthdrums -- offline engine test (x86): defaults, the voice selector's mirroring, the trigger, the state
 * round trip and display_rev; then the voice DSP: the oscillator, the noise, the envelopes, the x-mod, the
 * velocity routings and the master bus. */
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
typedef struct { double left, right; } StatLR;

/* The engine counts the samples it had to silence because they were not finite (Synth.nan_count): a NaN that
 * reached the output would be undefined behaviour, so the count is what "no NaN" means here. */
static unsigned nan_mark(void *x) { return ((Synth *)x)->nan_count; }
static int nan_since(void *x, unsigned mark) { return (int)(((Synth *)x)->nan_count - mark); }

static Stat run(void *x, int blocks)
{
    Stat s = {0, 0, 0};
    double acc = 0;
    long cnt = 0;
    const unsigned n0 = nan_mark(x);
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
    s.rms = cnt ? sqrt(acc / (double)cnt) / 32768.0 : 0.0;
    s.nan = nan_since(x, n0);
    return s;
}

/* the roster's helpers: the same measurements, sliced differently. The ones Task 3's block does not call yet
 * (Tasks 4 and 5 need them) are marked unused so the build stays warning-free. */
static Stat run_from(void *x, int skip, int blocks)
{
    Stat s = {0, 0, 0};
    double acc = 0;
    long cnt = 0;
    const unsigned n0 = nan_mark(x);
    int b, i;
    for (b = 0; b < skip; b++) E->render(x, buf, 128);
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 256; i++) {
            const int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > s.peak) s.peak = v;
            acc += (double)buf[i] * buf[i];
            cnt++;
        }
    }
    s.rms = cnt ? sqrt(acc / (double)cnt) / 32768.0 : 0.0;
    s.nan = nan_since(x, n0);
    return s;
}

static void __attribute__((unused)) run_windows(void *x, int nwin, int winblocks, float *rms_out)
{
    int w, b, i;
    for (w = 0; w < nwin; w++) {
        double acc = 0.0;
        long cnt = 0;
        for (b = 0; b < winblocks; b++) {
            E->render(x, buf, 128);
            for (i = 0; i < 256; i++) { acc += (double)buf[i] * buf[i]; cnt++; }
        }
        rms_out[w] = cnt ? (float)(sqrt(acc / (double)cnt) / 32768.0) : 0.0f;
    }
}

static StatLR __attribute__((unused)) run_lr(void *x, int blocks)
{
    StatLR r = {0.0, 0.0};
    double al = 0.0, ar = 0.0;
    long cnt = 0;
    int b, i;
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 128; i++) {
            al += (double)buf[2 * i] * buf[2 * i];
            ar += (double)buf[2 * i + 1] * buf[2 * i + 1];
            cnt++;
        }
    }
    r.left = cnt ? sqrt(al / (double)cnt) / 32768.0 : 0.0;
    r.right = cnt ? sqrt(ar / (double)cnt) / 32768.0 : 0.0;
    return r;
}

/* the zero crossings of the mono fold (both channels carry the same signal today) */
static int zc(void *x, int blocks)
{
    int b, i, sign = 0, n = 0;
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 256; i++) {
            const int v = buf[i];
            const int s = v > 0 ? 1 : v < 0 ? -1 : 0;
            if (!s) continue;   /* a zero sample is not a crossing */
            if (sign && s != sign) n++;
            sign = s;
        }
    }
    return n;
}

static int zc_after(void *x, int skip, int blocks)
{
    int b, i, sign = 0, n = 0;
    for (b = 0; b < skip; b++) E->render(x, buf, 128);
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 256; i++) {
            const int v = buf[i];
            const int s = v > 0 ? 1 : v < 0 ? -1 : 0;
            if (!s) continue;
            if (sign && s != sign) n++;
            sign = s;
        }
    }
    return n;
}

/* the raw mono samples of the next block, normalized to 0..1 */
static void cap(void *x, float *out, int nframes)
{
    int i;
    if (nframes > 128) nframes = 128;
    E->render(x, buf, nframes);
    for (i = 0; i < nframes; i++) out[i] = (float)((buf[2 * i] + buf[2 * i + 1]) * 0.5f) / 32768.0f;
}

static double capdiff(const float *a, const float *b, int n)
{
    double acc = 0.0;
    int i;
    for (i = 0; i < n; i++) { const double d = (double)a[i] - (double)b[i]; acc += d * d; }
    return n ? sqrt(acc / (double)n) : 0.0;
}

static float __attribute__((unused)) buf_rms(const float *a, int n)
{
    double acc = 0.0;
    int i;
    for (i = 0; i < n; i++) acc += (double)a[i] * a[i];
    return n ? (float)sqrt(acc / (double)n) : 0.0f;
}

/* the biggest jump between two neighbouring samples, in int16 units */
static float __attribute__((unused)) maxstep(void *x, int blocks)
{
    int b, i, prev = 0, first = 1;
    float m = 0.0f;
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 256; i++) {
            const int v = buf[i];
            if (!first) {
                const int d = v - prev;
                const int ad = d < 0 ? -d : d;
                if ((float)ad > m) m = (float)ad;
            }
            prev = v;
            first = 0;
        }
    }
    return m;
}

static float __attribute__((unused)) minf(const float *a, int n)
{
    float m = 0.0f;
    int i;
    for (i = 0; i < n; i++) if (!i || a[i] < m) m = a[i];
    return m;
}

static float __attribute__((unused)) maxf(const float *a, int n)
{
    float m = 0.0f;
    int i;
    for (i = 0; i < n; i++) if (!i || a[i] > m) m = a[i];
    return m;
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
    note(x, 36, 100); note(x, 48, 100); note(x, 41, 100);  /* all trigger without crash */
    { Stat s = run(x, 4); CHECK(s.peak > 0, "the folded notes sound (peak %d)", s.peak); }

    /* ---- the voice DSP (Task 3's block) ---- */
    sp(x, "base_note", 36); sp(x, "voice", 0);   /* prelude: one octave from C2; test on voice idx 0 */
    sp(x, "trig", 1); Stat s = run(x, 200);     /* ~0.6 s */
    CHECK(s.rms > 0.01 && s.nan == 0, "init voice sounds, no NaN (%.4f)", s.rms);
    CHECK(s.peak > 8000 && s.peak < 32700, "init voice level and headroom (%d)", s.peak);
    /* decays to silence (one-shot; measure the tail, not the attack) */
    sp(x, "osc_decay", 20); sp(x, "trig", 1); Stat tail = run_from(x, 100, 200);
    CHECK(tail.peak < 200, "one-shot decays to silence (%d)", tail.peak);
    /* velocity response with vel_to_vol at max: same voice for both hits, drained in between */
    sp(x, "osc_decay", 64);                                   /* ~90 ms body, drains quickly */
    note(x, 36, 127); Stat loud = run(x, 40); run(x, 300);    /* drain before the next hit */
    note(x, 36, 10);  Stat quiet = run(x, 40);
    CHECK(loud.rms > quiet.rms * 3, "velocity scales level (%.4f vs %.4f)", loud.rms, quiet.rms);
    sp(x, "vel_to_vol", 0);
    note(x, 36, 127); loud = run(x, 40); run(x, 300);
    note(x, 36, 10);  quiet = run(x, 40);
    CHECK(loud.rms > quiet.rms * 0.95 && loud.rms < quiet.rms * 1.05, "vel_to_vol=0 flattens velocity (%.4f vs %.4f)", loud.rms, quiet.rms);
    sp(x, "vel_to_vol", 127);
    /* pitch sweep: amount up → starts high, settles back down */
    sp(x, "src_mix", 0); sp(x, "penv_amount", 60); sp(x, "penv_decay", 100); sp(x, "osc_decay", 127);
    sp(x, "trig", 1); int early = zc(x, 10); int late = zc_after(x, 400, 10);
    CHECK(early > late * 2, "pitch env sweeps down (%d vs %d)", early, late);
    sp(x, "penv_amount", 0); sp(x, "penv_decay", 64);
    /* shape and x-mod change the waveform exactly (same params → bit-identical capture) */
    static float A[128*128], B[128*128];
    sp(x, "osc_shape", 0); sp(x, "osc_xmod", 0); sp(x, "trig", 1); cap(x, A, 100);
    sp(x, "osc_shape", 127); sp(x, "trig", 1); cap(x, B, 100);
    CHECK(capdiff(A, B, 128*100) > 1e-3, "osc_shape changes the wave");
    sp(x, "osc_shape", 0); sp(x, "osc_xmod", 127); sp(x, "trig", 1); cap(x, B, 100);
    CHECK(capdiff(A, B, 128*100) > 1e-3, "x-mod adds modulation");
    sp(x, "osc_xmod", 0);
    /* noise path */
    sp(x, "src_mix", 127); sp(x, "noise_decay", 100); sp(x, "trig", 1); s = run(x, 150);
    CHECK(s.rms > 0.005 && s.nan == 0, "noise source alone sounds");
    sp(x, "noise_colour", 0); sp(x, "trig", 1); int dark = zc(x, 60);
    sp(x, "noise_colour", 127); sp(x, "trig", 1); int bright = zc(x, 60);
    CHECK(bright > dark * 2, "colour tilts spectrum (%d vs %d)", dark, bright);
    /* vel_to_noise rides the same crossfade law as vel_to_vol (vol off isolates it) */
    sp(x, "vel_to_vol", 0); sp(x, "noise_decay", 127); sp(x, "vel_to_noise", 127);
    note(x, 36, 127); Stat n_loud = run(x, 40); run(x, 300);
    note(x, 36, 10);  Stat n_quiet = run(x, 40);
    CHECK(n_loud.rms > n_quiet.rms * 3, "vel_to_noise scales the noise (%.4f vs %.4f)", n_loud.rms, n_quiet.rms);
    sp(x, "vel_to_noise", 0);
    note(x, 36, 127); n_loud = run(x, 40); run(x, 300);
    note(x, 36, 10);  n_quiet = run(x, 40);
    CHECK(n_loud.rms > n_quiet.rms * 0.95 && n_loud.rms < n_quiet.rms * 1.05, "vel_to_noise=0 flattens");
    sp(x, "vel_to_vol", 127);
    /* MIDI: every key of the octave triggers; note-off does not stop a one-shot */
    sp(x, "src_mix", 0);
    for (int n = 36; n < 48; n++) { note(x, n, 110); s = run(x, 20); CHECK(s.rms > 1e-4, "key %d silent", n); note(x, n, 0); }
    sp(x, "osc_decay", 64); note(x, 36, 110); run(x, 5); note(x, 36, 0);
    Stat off = run(x, 30); CHECK(off.rms > 1e-3, "note-off ignored, one-shot keeps sounding");

    E->destroy(y);
    E->destroy(x);
    printf(fails ? "FAILED: %d\n" : "PASSED\n", fails);
    return fails != 0;
}
