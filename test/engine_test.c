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

typedef struct { double rms; int peak; int nan; int peak_l; int peak_r; } Stat;
typedef struct { double left, right; } StatLR;

/* The engine counts the samples it had to silence because they were not finite (Synth.nan_count): a NaN that
 * reached the output would be undefined behaviour, so the count is what "no NaN" means here. */
static unsigned nan_mark(void *x) { return ((Synth *)x)->nan_count; }
static int nan_since(void *x, unsigned mark) { return (int)(((Synth *)x)->nan_count - mark); }

static Stat run(void *x, int blocks)
{
    Stat s = {0, 0, 0, 0, 0};
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
        for (i = 0; i < 128; i++) {   /* the two channels' peaks on their own */
            const int l = buf[2 * i] < 0 ? -buf[2 * i] : buf[2 * i];
            const int r = buf[2 * i + 1] < 0 ? -buf[2 * i + 1] : buf[2 * i + 1];
            if (l > s.peak_l) s.peak_l = l;
            if (r > s.peak_r) s.peak_r = r;
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
    Stat s = {0, 0, 0, 0, 0};
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

static void run_windows(void *x, int nwin, int winblocks, float *rms_out)
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

static StatLR run_lr(void *x, int blocks)
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

/* the biggest jump between two neighbouring samples of one channel, in int16 units.
 * The engine is stereo now, so the measurement is per channel: the jump from the left sample to the right
 * sample of an interleaved buffer is not a click, it is the stereo image (a hard-panned pair makes it
 * ~16000 units). The plan's Task-5 text asks for "the choked channel's max sample-to-sample step", so
 * the biggest jump inside either channel is what this measures. */
static float maxstep(void *x, int blocks)
{
    int b, i, prevl = 0, prevr = 0, first = 1;
    float m = 0.0f;
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 128; i++) {
            const int l = buf[2 * i], r = buf[2 * i + 1];
            if (!first) {
                const int dl = l - prevl, dr = r - prevr;
                const int al = dl < 0 ? -dl : dl, ar = dr < 0 ? -dr : dr;
                if ((float)al > m) m = (float)al;
                if ((float)ar > m) m = (float)ar;
            }
            prevl = l;
            prevr = r;
            first = 0;
        }
    }
    return m;
}

static float minf(const float *a, int n)
{
    float m = 0.0f;
    int i;
    for (i = 0; i < n; i++) if (!i || a[i] < m) m = a[i];
    return m;
}

static float maxf(const float *a, int n)
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
    /* the saved base note (40) must reach the voices' own keys, not just the device level: folding keys
     * off s->base_note while the oscillator keys off v->base_note would tune every voice off. */
    { int ok = 1; for (int v = 0; v < 12; v++) if (((Synth *)y)->v[v].base_note != 40) ok = 0;
      CHECK(ok, "loaded base note propagated to all 12 voices (%d)", ((Synth *)y)->v[5].base_note); }
    /* Task 1: the edit-order mapping -- the first 35 params keep their 1.0.0 positions */
    {
        int k, ok = 1;
        for (k = 0; k < 35; k++) if (E2P[k] != P_PITCH + k || P2E[P_PITCH + k] != k) ok = 0;
        CHECK(ok, "E2P/P2E identity for the 1.0.0 edit panel");
        CHECK(P2E[P_VOICE] == -1 && P2E[P_KIT_INIT] == -1, "non-patch params are not in p[]");
    }
    /* Task 1: a 1.0.0-format chunk (35 values, ends at choke) still loads */
    {
        static const char *old = "MSDR 1\nsel=0\nbase=36\n"
            "v1=0,127,0,0,0,0,0,64,0,0,0,0,0,64,0,64,0,64,0,127,64,0,0,0,127,0,64,0,64,0,64,0,100,0,4\n";
        E->set_param(x, "state", old);
        E->get_param(x, "osc_decay", b, sizeof b); CHECK(!strcmp(b, "64"), "old chunk: osc_decay");
        E->get_param(x, "choke", b, sizeof b);     CHECK(!strcmp(b, "4"), "old chunk: last old param (choke)");
        E->set_param(x, "state", a);               /* put the captured state back for the tests below */
    }
    /* Task 2: a pad hit selects that voice (base_note = 36: note 41 -> voice 5) */
    {
        sp(x, "base_note", 36);
        E->get_param(x, "display_rev", a, sizeof a);
        note(x, 41, 100);
        E->get_param(x, "voice", b, sizeof b);   CHECK(!strcmp(b, "5"), "pad hit selects voice 5 (%s)", b);
        E->get_param(x, "display_rev", c, sizeof c); CHECK(strcmp(a, c), "selection change bumps display_rev");
        note(x, 41, 100);
        E->get_param(x, "display_rev", a, sizeof a); note(x, 41, 100);
        E->get_param(x, "display_rev", c, sizeof c); CHECK(!strcmp(a, c), "same-voice hit does not bump display_rev");
        CHECK(run(x, 2).rms > 1e-4, "the folded hit sounds");
        E->set_param(x, "voice", "0");           /* selection back for the tests below */
    }
    /* Task 3: copy/paste/init the selected voice */
    {
        char st[8192];
        E->get_param(x, "state", st, sizeof st);
        E->set_param(x, "voice", "0"); sp(x, "osc_decay", 42); sp(x, "voice_copy", 1);
        E->set_param(x, "voice", "1"); sp(x, "osc_decay", 99); sp(x, "voice_paste", 1);
        E->get_param(x, "osc_decay", b, sizeof b); CHECK(!strcmp(b, "42"), "paste restores the copied voice");
        E->set_param(x, "voice", "0"); E->get_param(x, "osc_decay", b, sizeof b);
        CHECK(!strcmp(b, "42"), "copy touched no other voice");
        sp(x, "voice_init", 1);
        E->get_param(x, "osc_decay", b, sizeof b); CHECK(!strcmp(b, "64"), "init voice resets to INIT_PATCH");
        E->set_param(x, "voice", "1"); E->get_param(x, "osc_decay", b, sizeof b);
        CHECK(!strcmp(b, "42"), "init voice leaves the others alone");
        /* the clipboard is not in the chunk */
        E->set_param(x, "voice", "0"); sp(x, "voice_copy", 1);
        { char s1[8192], s2[8192];
          sp(x, "osc_decay", 77); E->get_param(x, "state", s1, sizeof s1);
          sp(x, "voice_copy", 1); E->get_param(x, "state", s2, sizeof s2);
          CHECK(!strcmp(s1, s2), "copy does not change the state chunk"); }
        E->set_param(x, "state", st);   /* restore */
    }
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
    /* pitch sweep: amount up → starts high, settles back down (shape 0: decay from the first sample;
     * the default shape now holds the pitch for ~3.6 tau, which this window-based test doesn't want) */
    sp(x, "src_mix", 0); sp(x, "penv_amount", 60); sp(x, "penv_decay", 100); sp(x, "osc_decay", 127);
    sp(x, "penv_shape", 0); sp(x, "trig", 1); int early = zc(x, 10); int late = zc_after(x, 400, 10);
    CHECK(early > late * 2, "pitch env sweeps down (%d vs %d)", early, late);
    sp(x, "penv_amount", 0); sp(x, "penv_decay", 64); sp(x, "penv_shape", 64);
    /* shape and x-mod change the sound exactly (same params → bit-identical capture). osc_shape shapes the
     * amp env now, so a short decay makes the hold visible inside the first block's capture. */
    static float A[128*128], B[128*128];
    sp(x, "osc_decay", 30);
    sp(x, "osc_shape", 0); sp(x, "osc_xmod", 0); sp(x, "trig", 1); cap(x, A, 100);
    sp(x, "osc_shape", 127); sp(x, "trig", 1); cap(x, B, 100);
    CHECK(capdiff(A, B, 128*100) > 1e-3, "osc_shape changes the voice");
    sp(x, "osc_shape", 0); sp(x, "osc_xmod", 127); sp(x, "trig", 1); cap(x, B, 100);
    CHECK(capdiff(A, B, 128*100) > 1e-3, "x-mod adds modulation");
    sp(x, "osc_xmod", 0); sp(x, "osc_decay", 127);
    /* ---- Task 5: the shape knobs hold the level at full, then release exponentially to zero ---- */
    sp(x, "src_mix", 0); sp(x, "penv_amount", 0);
    sp(x, "osc_decay", 30);                       /* tau ~12 ms: T ~108 ms, hold(127) ~87 ms, tail ~2.4 ms */
    sp(x, "osc_shape", 0); note(x, 36, 127);
    float w0[12], w1[12];
    run_windows(x, 12, 4, w0);                    /* 12 windows of 4 blocks: ~11.6 ms each, ~139 ms total */
    CHECK(w0[3] < 0.5f * w0[0], "shape 0 decays immediately (%.4f vs %.4f)", w0[3], w0[0]);
    sp(x, "osc_shape", 127); note(x, 36, 127);
    run_windows(x, 12, 4, w1);
    CHECK(w1[3] > 0.9f * w1[0], "shape 127 holds the level (%.4f vs %.4f)", w1[3], w1[0]);
    CHECK(w1[9] < w1[5] * 0.3f, "then the tail falls off (%.4f vs %.4f)", w1[9], w1[5]);
    Stat sh_tail = run_from(x, 6, 54);            /* past t = T: the envelope reaches exact zero */
    CHECK(sh_tail.peak < 300, "the envelope reaches zero (%d)", sh_tail.peak);
    /* the pitch env uses the same law: at shape 127 the sweep holds at full depth, then settles */
    sp(x, "osc_decay", 127); sp(x, "osc_shape", 0);
    sp(x, "penv_amount", 60); sp(x, "penv_decay", 64); sp(x, "penv_shape", 127);
    sp(x, "trig", 1); int ph_early = zc(x, 3);    /* in the hold: pitch still way up */
    int ph_late = zc_after(x, 90, 3);             /* past T ~0.5 s: the pitch has settled */
    CHECK(ph_early > ph_late * 2, "pitch env holds at full depth (%d vs %d)", ph_early, ph_late);
    /* all three shape knobs still change the sound (the pitch env via its zc hold above) */
    sp(x, "penv_amount", 0); sp(x, "penv_shape", 64);
    sp(x, "src_mix", 127); sp(x, "noise_decay", 64);
    float wn0[8], wn1[8];
    sp(x, "noise_shape", 0); sp(x, "trig", 1); run_windows(x, 8, 20, wn0);   /* 58 ms windows, ~464 ms */
    sp(x, "noise_shape", 127); sp(x, "trig", 1); run_windows(x, 8, 20, wn1);
    CHECK(wn1[6] > wn0[6] * 3, "noise_shape changes the decay (%.4f vs %.4f)", wn1[6], wn0[6]);
    sp(x, "noise_shape", 64); sp(x, "src_mix", 0);
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

    /* ---- the filter (Task 4's block): the osc branch's own filter (the noise branch has its own below) ---- */
    sp(x, "voice", 0);   /* keep MIDI note(36) and trig on the same voice for this section */
    sp(x, "src_mix", 0); sp(x, "osc_decay", 127); sp(x, "pitch", 24);   /* a sustained mid sine for the LP to bite */
    sp(x, "filter_cutoff", 20); sp(x, "filter_res", 0); sp(x, "trig", 1); Stat lp_dark = run(x, 80);
    sp(x, "filter_cutoff", 127); sp(x, "trig", 1); Stat lp_open = run(x, 80);
    CHECK(lp_open.rms > lp_dark.rms * 2, "LP cutoff attenuates (%.4f vs %.4f)", lp_dark.rms, lp_open.rms);
    /* self-oscillation at max res stays bounded, no NaN */
    sp(x, "filter_cutoff", 30); sp(x, "filter_res", 127); sp(x, "trig", 1); s = run(x, 120);
    CHECK(s.nan == 0 && s.peak < 32767 && s.rms < 0.7, "self-osc bounded (%d)", s.peak);
    /* BP and HP differ from LP */
    sp(x, "filter_mode", 1); sp(x, "trig", 1); Stat bp = run(x, 60);
    sp(x, "filter_mode", 2); sp(x, "trig", 1); Stat hp = run(x, 60);
    sp(x, "filter_mode", 0); sp(x, "trig", 1); Stat lp = run(x, 60);
    CHECK(fabsf(bp.rms - lp.rms) > 1e-4 && fabsf(hp.rms - lp.rms) > 1e-4 && hp.nan == 0, "modes differ");
    /* filter env: open transient then settle. res 0 (the resonance would ring the 261 Hz test tone),
     * decay 64 (~70 ms tau) so the env is fully settled well inside the late window */
    sp(x, "filter_res", 0); sp(x, "filter_cutoff", 30);
    sp(x, "fenv_amount", 63); sp(x, "fenv_decay", 64); sp(x, "trig", 1);
    Stat fe = run(x, 10); Stat fs = run_from(x, 300, 100);
    CHECK(fe.rms > fs.rms * 1.3, "filter env opens then settles (%.4f vs %.4f)", fe.rms, fs.rms);
    /* vel→cut: both hits via MIDI so they land on the same voice; vol flattened to isolate cut */
    sp(x, "fenv_amount", 0); sp(x, "vel_to_cut", 127); sp(x, "vel_to_vol", 0); sp(x, "filter_cutoff", 64);
    note(x, 36, 127); Stat vc_loud = run(x, 40); note(x, 36, 5); Stat vc_quiet = run(x, 40);
    CHECK(vc_loud.rms > vc_quiet.rms * 1.05, "vel_to_cut brightens with velocity (%.4f vs %.4f)", vc_loud.rms, vc_quiet.rms);
    sp(x, "vel_to_cut", 0); sp(x, "vel_to_vol", 127); sp(x, "pitch", 0);   /* restore for Task 5 */

    /* ---- Task 4: the noise branch's own filter + dynamic Hz displays ---- */
    sp(x, "voice", 0); sp(x, "base_note", 36);
    sp(x, "src_mix", 127);                 /* noise only */
    sp(x, "noise_colour", 127); sp(x, "noise_attack", 0); sp(x, "noise_decay", 100);
    sp(x, "filter_res", 0); sp(x, "filter_mode", 0); sp(x, "fenv_amount", 0);
    sp(x, "nfilt_res", 0); sp(x, "nfilt_mode", 0);
    sp(x, "nfilt_env_amount", 0); sp(x, "nfilt_vel", 0); sp(x, "filter_cutoff", 0);
    sp(x, "noise_filter", 127); note(x, 36, 127);
    int zc_open = zc(x, 40);
    sp(x, "noise_filter", 0); note(x, 36, 127);
    int zc_dark = zc(x, 40);
    CHECK(zc_dark < zc_open / 2, "noise cutoff darkens the noise (%d vs %d)", zc_dark, zc_open);
    /* the osc filter (closed!) does not touch the noise branch */
    sp(x, "noise_filter", 127); note(x, 36, 127);
    int zc_oscclosed = zc(x, 40);
    CHECK(zc_oscclosed > zc_open * 3 / 4, "closed osc filter leaves noise bright (%d vs %d)", zc_oscclosed, zc_open);
    /* the noise filter env opens the cutoff on the hit then settles */
    sp(x, "filter_cutoff", 127); sp(x, "noise_filter", 0);
    sp(x, "nfilt_env_amount", 63); sp(x, "nfilt_env_decay", 64);
    note(x, 36, 127);
    int nfe = zc_after(x, 0, 6), nfl = zc_after(x, 60, 20);
    CHECK(nfe > nfl * 3 / 2, "noise filter env sweeps then settles (%d vs %d)", nfe, nfl);
    sp(x, "src_mix", 0); sp(x, "nfilt_env_amount", 0); sp(x, "filter_cutoff", 127);
    sp(x, "noise_filter", 127);
    /* dynamic Hz displays */
    sp(x, "filter_cutoff", 127);
    E->get_param(x, "filter_cutoff_display", b, sizeof b); CHECK(!strcmp(b, "16.0 kHz"), "cutoff display (%s)", b);
    sp(x, "filter_cutoff", 0);
    E->get_param(x, "filter_cutoff_display", b, sizeof b); CHECK(!strcmp(b, "30 Hz"), "cutoff display low (%s)", b);
    sp(x, "noise_filter", 127);
    E->get_param(x, "noise_filter_display", b, sizeof b); CHECK(!strcmp(b, "18.0 kHz"), "noise cutoff display (%s)", b);
    sp(x, "filter_cutoff", 127);

    /* ---- Task 6: three peaking EQ bands (40 Hz..12 kHz, ±18 dB, Q 0.5..6), Hz/kHz readouts ---- */
    sp(x, "src_mix", 0); sp(x, "osc_decay", 110); sp(x, "pitch", 24);   /* a 261.6 Hz steady tone */
    sp(x, "eq_freq", 64); sp(x, "eq_gain", 0); sp(x, "eq_q", 64);
    sp(x, "eq2_freq", 42); sp(x, "eq2_q", 64);                          /* centre 40*300^(42/127) ~ 264 Hz */
    sp(x, "eq3_freq", 64); sp(x, "eq3_gain", 0); sp(x, "eq3_q", 64);
    sp(x, "eq2_gain", 0); note(x, 36, 127); Stat eq_base = run(x, 6);
    sp(x, "eq2_gain", 63); note(x, 36, 127); Stat eq_boost = run(x, 6);
    sp(x, "eq2_gain", -63); note(x, 36, 127); Stat eq_cut = run(x, 6);
    CHECK(eq_boost.rms > eq_cut.rms * 1.3, "EQ2 boost vs cut (%.4f vs %.4f)", eq_boost.rms, eq_cut.rms);
    CHECK(eq_boost.rms > eq_base.rms * 1.5 && eq_cut.rms < eq_base.rms * 0.7,
          "EQ2 moves the level both ways (%.4f/%.4f vs %.4f)", eq_boost.rms, eq_cut.rms, eq_base.rms);
    CHECK(eq_base.nan == 0 && eq_boost.nan == 0 && eq_cut.nan == 0, "EQ2 no NaN");
    sp(x, "eq2_gain", 0); sp(x, "eq3_freq", 42);
    sp(x, "eq3_gain", 63); note(x, 36, 127); Stat eq3_boost = run(x, 6);
    sp(x, "eq3_gain", -63); note(x, 36, 127); Stat eq3_cut = run(x, 6);
    CHECK(eq3_boost.rms > eq3_cut.rms * 1.3, "EQ3 boost vs cut (%.4f vs %.4f)", eq3_boost.rms, eq3_cut.rms);
    /* gain 0 is transparent whatever the band's freq/Q (H(z) = 1; float rounding only) */
    sp(x, "eq3_gain", 0); sp(x, "eq2_freq", 42); note(x, 36, 127); cap(x, A, 128);
    sp(x, "eq2_freq", 90); note(x, 36, 127); cap(x, B, 128);
    CHECK(capdiff(A, B, 128) < 1e-4, "gain-0 bands are transparent (%.6f)", capdiff(A, B, 128));
    /* the Hz/kHz displays, on all three bands */
    sp(x, "eq_freq", 0);  E->get_param(x, "eq_freq_display", b, sizeof b);  CHECK(!strcmp(b, "40 Hz"), "eq display lo (%s)", b);
    sp(x, "eq_freq", 127); E->get_param(x, "eq_freq_display", b, sizeof b); CHECK(!strcmp(b, "12.0 kHz"), "eq display hi (%s)", b);
    sp(x, "eq2_freq", 0); E->get_param(x, "eq2_freq_display", b, sizeof b); CHECK(!strcmp(b, "40 Hz"), "eq2 display lo (%s)", b);
    sp(x, "eq3_freq", 127); E->get_param(x, "eq3_freq_display", b, sizeof b); CHECK(!strcmp(b, "12.0 kHz"), "eq3 display hi (%s)", b);
    sp(x, "pitch", 0);   /* restore: the choke click test's step limit assumes the 65 Hz base tone */

    /* ---- Task 7: ring mod, operator FM, comb, gated reverb ---- */
    /* every module is exact bypass at 0 (direct unit calls on fresh state) */
    {
        RingSt r; FmSt f; CombSt cb; GVerbSt gv;
        float xc = 0.5f;
        memset(&r, 0, sizeof r); memset(&f, 0, sizeof f); memset(&cb, 0, sizeof cb); memset(&gv, 0, sizeof gv);
        CHECK(fx_ring(&r, xc, 8000.0f, 0.0f) == xc, "ring bypass at 0");
        CHECK(fx_fm(&f, xc, 8.0f, 0.0f) == xc, "fm bypass at 0");
        CHECK(fx_comb(&cb, xc, 30.0f, 0.75f, 0.0f) == xc, "comb bypass at 0");
        CHECK(fx_gverb(&gv, xc, 0.0f, 0.0f) == xc, "gverb bypass at 0");
    }
    /* engaged: each knob changes the voice (cap: one block, 128 frames) */
    sp(x, "src_mix", 0); sp(x, "osc_decay", 100);
    note(x, 36, 127); cap(x, A, 128);
    sp(x, "ring_mod", 127); note(x, 36, 127); cap(x, B, 128);
    CHECK(capdiff(A, B, 128) > 1e-3, "ring engages");
    /* the knob is frequency only (design §8 "fully wet when engaged"): even the lowest engaged value
     * multiplies by the carrier outright. A 1 Hz carrier barely rises inside one block, so a full-wet
     * ring nearly nulls it (rms ~0.34x dry: a 1 ms engage ramp off dry, then |sin|~0.02); the old
     * blend law g=k/127 would have left it ~0.99x dry. The 0.6 threshold sits ~3x from both. */
    sp(x, "ring_mod", 1); note(x, 36, 127); cap(x, B, 128);
    CHECK(buf_rms(B, 128) < buf_rms(A, 128) * 0.6f, "ring fully wet at 1 (%.4f vs dry %.4f)",
          buf_rms(B, 128), buf_rms(A, 128));
    sp(x, "ring_mod", 0); sp(x, "fm_index", 127); note(x, 36, 127); cap(x, B, 128);
    CHECK(capdiff(A, B, 128) > 1e-3, "fm engages");
    sp(x, "fm_index", 0); sp(x, "comb", 127); note(x, 36, 127); cap(x, B, 128);
    CHECK(capdiff(A, B, 128) > 1e-3, "comb engages");
    /* the gate closes the tail: full wet + a sustained input — the tail must die when the gate shuts
     * (m = 1 removes the dry entirely, so what remains after the gate is nothing but the gated wet) */
    sp(x, "comb", 0); sp(x, "osc_decay", 127); sp(x, "gverb", 127);
    note(x, 36, 127); Stat gv_mid = run_from(x, 12, 10);   /* the combs' first reflections (29.7 ms) arrived */
    CHECK(gv_mid.nan == 0 && gv_mid.peak < 32767 && gv_mid.rms > 0.01, "gverb wet audible (%.4f)", gv_mid.rms);
    Stat gv_late = run_from(x, 120, 20);                   /* ~410 ms: the gate has shut */
    CHECK(gv_late.rms < gv_mid.rms * 0.05, "gated reverb tail is gated (%.4f vs %.4f)", gv_late.rms, gv_mid.rms);
    sp(x, "gverb", 0);
    /* the displays */
    sp(x, "ring_mod", 127); E->get_param(x, "ring_mod_display", b, sizeof b); CHECK(!strcmp(b, "8.0 kHz"), "ring display (%s)", b);
    sp(x, "comb", 127); E->get_param(x, "comb_display", b, sizeof b); CHECK(!strcmp(b, "30.0 ms"), "comb display (%s)", b);
    sp(x, "ring_mod", 0); sp(x, "comb", 0);
    /* two hits bit-identical with all four engaged (comb at 32: ~1.4 ms, so the reflections land
     * inside the captured block and the comparison sees real signal, not pre-reflection silence) */
    sp(x, "ring_mod", 64); sp(x, "fm_index", 64); sp(x, "comb", 32); sp(x, "gverb", 64);
    note(x, 36, 127); cap(x, A, 128);
    note(x, 36, 127); cap(x, B, 128);
    CHECK(capdiff(A, B, 128) < 1e-9, "FX states reset per hit (bit-identical)");
    sp(x, "ring_mod", 0); sp(x, "fm_index", 0); sp(x, "comb", 0); sp(x, "gverb", 0);
    sp(x, "src_mix", 0); sp(x, "osc_decay", 127);

    /* ---- Task 8: the stereo delay (vendored Airwindows Doublelay, last in the chain) ---- */
    /* full wet: nothing reaches the output before the delay time, then the hit arrives */
    sp(x, "voice", 0);
    sp(x, "src_mix", 0); sp(x, "osc_decay", 20); sp(x, "osc_shape", 0);   /* the dry body dies by ~108 ms */
    sp(x, "delay_wet", 127); sp(x, "delay_l", 102); sp(x, "delay_r", 102);  /* 1000^(102/127) ~ 257 ms */
    sp(x, "delay_fb", 64); sp(x, "delay_detune", 64);
    note(x, 36, 127);
    Stat dl_pre = run_from(x, 0, 60);                        /* 0..174 ms: still inside the delay line */
    CHECK(dl_pre.nan == 0 && dl_pre.rms < 1e-4, "full wet: silent before the delay time (%.6f)", dl_pre.rms);
    Stat dl_echo = run_from(x, 30, 30);                      /* 261..348 ms: the echo window */
    CHECK(dl_echo.nan == 0 && dl_echo.rms > 1e-4, "the delay echoes after the dry died (%.6f)", dl_echo.rms);
    /* wet 0 is exact bypass: the hit passes through immediately, the tail window stays silent */
    sp(x, "delay_wet", 0); note(x, 36, 127);
    Stat dl_thru = run_from(x, 0, 10);
    CHECK(dl_thru.rms > 1e-3, "wet 0 passes the dry hit through (%.4f)", dl_thru.rms);
    Stat dl_off = run_from(x, 80, 15);
    CHECK(dl_off.rms < 1e-5, "wet 0 is silent in the tail window (%.6f)", dl_off.rms);
    /* feedback at the knob max cannot run away (fb_eff caps at 0.95) */
    sp(x, "delay_wet", 127); sp(x, "delay_fb", 127); note(x, 36, 127);
    Stat dl_s = run(x, 200);
    CHECK(dl_s.nan == 0 && dl_s.peak < 32767, "fb 127 bounded (%d)", dl_s.peak);
    /* without feedback the single echo ends, and the tail window is silent */
    sp(x, "delay_fb", 0); note(x, 36, 127);
    run_from(x, 120, 40);                                    /* past the echo (~257 + 60 ms) */
    Stat dl_far = run(x, 40);
    CHECK(dl_far.nan == 0 && dl_far.peak < 300, "tail ends and the voice goes silent (%d)", dl_far.peak);
    /* the choke cuts the delay tail: voice 0 (panned left) rings in group 1, voice 2 (also group 1,
     * panned right, body short) triggers and must silence the left channel completely */
    sp(x, "voice", 0); sp(x, "osc_decay", 127); sp(x, "out_pan", -63); sp(x, "choke", 1);
    sp(x, "delay_fb", 64);
    sp(x, "voice", 2); sp(x, "src_mix", 0); sp(x, "osc_decay", 30); sp(x, "out_pan", 63); sp(x, "choke", 1);
    sp(x, "voice", 0); note(x, 36, 127);
    run(x, 90);                                              /* ~261 ms: the dry is long dead, the tail rings */
    StatLR dl_ring = run_lr(x, 5);
    CHECK(dl_ring.left > 1e-3, "the delay tail rings before the choke (%.6f)", dl_ring.left);
    sp(x, "voice", 2); sp(x, "trig", 1);                     /* chokes voice 0 mid-tail */
    run(x, 3);                                               /* the 5 ms choke fade runs out here */
    Stat dl_ch = run(x, 5);
    CHECK(dl_ch.peak_l < 400, "choke silences the delay tail (%d)", dl_ch.peak_l);
    sp(x, "voice", 2); sp(x, "choke", 0); sp(x, "out_pan", 0);
    /* two hits bit-identical with the delay engaged (delay ~2.3 ms: the echo lands inside the
     * 128-frame capture, so the comparison sees real delayed signal) */
    sp(x, "voice", 0); sp(x, "choke", 0); sp(x, "out_pan", 0); sp(x, "osc_decay", 64);
    sp(x, "delay_fb", 64); sp(x, "delay_wet", 127); sp(x, "delay_l", 15); sp(x, "delay_r", 15);
    run(x, 45);    /* flush: voice 2's body must be gone -- the caps compare the whole master bus */
    note(x, 36, 127); cap(x, A, 128);
    note(x, 36, 127); cap(x, B, 128);
    CHECK(capdiff(A, B, 128) < 1e-9, "delay state resets per hit (bit-identical)");
    /* the displays */
    sp(x, "delay_l", 127); E->get_param(x, "delay_l_display", b, sizeof b);
    CHECK(!strcmp(b, "1000 ms"), "delay display (%s)", b);
    sp(x, "delay_r", 0); E->get_param(x, "delay_r_display", b, sizeof b);
    CHECK(!strcmp(b, "1 ms"), "delay display lo (%s)", b);
    sp(x, "delay_detune", 127); E->get_param(x, "delay_detune_display", b, sizeof b);
    CHECK(!strcmp(b, "+2.5%"), "detune display (%s)", b);
    sp(x, "delay_detune", 0); E->get_param(x, "delay_detune_display", b, sizeof b);
    CHECK(!strcmp(b, "-2.5%"), "detune display lo (%s)", b);
    /* leave the delay off (exact bypass) for every later block */
    sp(x, "delay_detune", 64); sp(x, "delay_l", 64); sp(x, "delay_r", 64); sp(x, "delay_fb", 64);
    sp(x, "delay_wet", 0); sp(x, "voice", 0); sp(x, "osc_decay", 127);

    /* ---- Task 9: the three LFOs (modulation, retrig, sync) ---- */
    {
        int di = 0;
        while (strcmp(DEST_KEYS[di], "out_vol")) di++;   /* the destination list's "VOL" entry */
        E->set_param(x, "lfo_bpm", "120");
        /* prelude: pin the state earlier blocks left dirty (the FX prelude below restores it anyway) --
         * especially the stutter gate, which would swing the sync test's windowed rms on its own */
        sp(x, "src_mix", 0); sp(x, "osc_decay", 127); sp(x, "out_vol", 100);
        sp(x, "vel_to_cut", 0); sp(x, "filter_cutoff", 127); sp(x, "filter_res", 0); sp(x, "fenv_amount", 0);
        sp(x, "stutter_strength", 0);
        sp(x, "lfo1_wave", 0); sp(x, "lfo1_sync", 0); sp(x, "lfo1_retrig", 1);
        sp(x, "lfo1_rate", 100);                          /* ~4.6 Hz free-run (0.02*1000^(100/127)) */
        sp(x, "lfo1_dest", di);
        /* the wave is evaluated at the block's start phase: right after a retrig'd hit the sin is at
         * ph = 0, so render a few blocks before capturing or the modulation reads as zero */
        sp(x, "lfo1_depth", 0); note(x, 36, 127); run(x, 5); cap(x, A, 128);
        sp(x, "lfo1_depth", 63); note(x, 36, 127); run(x, 5); cap(x, B, 128);
        CHECK(capdiff(A, B, 128) > 1e-3, "LFO depth modulates the destination");
        sp(x, "lfo1_depth", 0); note(x, 36, 127); run(x, 5); cap(x, B, 128);
        CHECK(capdiff(A, B, 128) < 1e-6, "depth 0 = no modulation");
        /* retrig ON: two hits bit-identical even on a random waveform (s&h) */
        sp(x, "lfo1_wave", 5); sp(x, "lfo1_depth", 63); sp(x, "lfo1_retrig", 1);
        note(x, 36, 127); cap(x, A, 128);
        note(x, 36, 127); cap(x, B, 128);
        CHECK(capdiff(A, B, 128) < 1e-9, "retrig resets the LFO (bit-identical)");
        /* retrig OFF: the phase and the rng run on across hits (s&h steps at the cycle wrap, so run
         * past a full cycle: one cycle at ~4.6 Hz is ~75 blocks) */
        sp(x, "lfo1_retrig", 0);
        note(x, 36, 127); cap(x, A, 128); run(x, 100);
        note(x, 36, 127); cap(x, B, 128);
        CHECK(capdiff(A, B, 128) > 1e-4, "retrig OFF keeps the phase");
        /* sync: 120 bpm, 1/4 = 2 Hz; the windowed rms must sweep measurably. The base VOL is 40 so the
         * swing up (+-63.5 knob units at depth 63) does not clip against VOL's 127 ceiling. The voice
         * is noise: a 65 Hz sine beat-notes against the 2.9 ms window grid (one window covers 0.19 of
         * a cycle), which swings the windowed rms 2.5x all by itself. */
        sp(x, "lfo1_wave", 0); sp(x, "lfo1_retrig", 1); sp(x, "out_vol", 40);
        sp(x, "src_mix", 127); sp(x, "noise_decay", 127);
        sp(x, "lfo1_sync", 1); sp(x, "lfo1_div", 6);      /* 1/4 */
        note(x, 36, 127);
        float w[50]; run_windows(x, 50, 1, w);            /* 50 x 2.9 ms ~ 145 ms ~ 0.29 of a 2 Hz cycle */
        float mn = minf(w, 50), mx = maxf(w, 50);
        CHECK(mx > mn * 1.3f && mn > 0, "synced LFO sweeps the dest (%.4f..%.4f)", mn, mx);
        E->get_param(x, "lfo1_rate_display", b, sizeof b); CHECK(!strcmp(b, "SYNC"), "rate shows SYNC (%s)", b);
        sp(x, "lfo1_sync", 0);
        E->get_param(x, "lfo1_rate_display", b, sizeof b); CHECK(strstr(b, "Hz"), "rate shows Hz (%s)", b);
        /* leave the LFOs off (depth 0) for the blocks below */
        sp(x, "lfo1_depth", 0); sp(x, "lfo1_dest", 0); sp(x, "lfo1_retrig", 1); sp(x, "out_vol", 100);
        sp(x, "src_mix", 0);
    }

    /* ---- the FX and the output (Task 5's block) ---- */
/* prelude: one clean loud voice; everything Task 4 left dirty is restored/open */
sp(x, "voice", 0); sp(x, "base_note", 36);
sp(x, "src_mix", 0); sp(x, "osc_decay", 127); sp(x, "vel_to_vol", 127); sp(x, "vel_to_cut", 0);
sp(x, "filter_cutoff", 127); sp(x, "filter_res", 0); sp(x, "filter_mode", 0); sp(x, "fenv_amount", 0);
/* stutter gates the amplitude */
sp(x, "stutter_length", 90); sp(x, "stutter_strength", 127); sp(x, "trig", 1);
float w[12]; run_windows(x, 12, 10, w);
CHECK(maxf(w, 12) > 0.01 && minf(w, 12) < maxf(w, 12) * 0.3, "stutter gates (%.4f..%.4f)", minf(w, 12), maxf(w, 12));
sp(x, "stutter_strength", 0); sp(x, "trig", 1); run_windows(x, 12, 10, w);
CHECK(minf(w, 12) > maxf(w, 12) * 0.75, "strength 0 = no gate, decay only (%.4f..%.4f)", minf(w, 12), maxf(w, 12));
/* dist: changes the wave (harmonics) exactly, stays bounded — scale-independent via capdiff */
sp(x, "osc_decay", 100); sp(x, "dist_drive", 0); sp(x, "trig", 1); cap(x, A, 60);
sp(x, "dist_drive", 127); sp(x, "trig", 1); cap(x, B, 60);
CHECK(capdiff(A, B, 128*60) > 1e-3, "dist changes the wave");
sp(x, "trig", 1); Stat dr = run(x, 60);   /* deterministic: same render as B */
CHECK(dr.nan == 0 && dr.peak < 32767 && dr.rms > 0.01, "dist bounded and audible (%.4f)", dr.rms);
/* EQ on broadband noise (an osc tone sits far below eq_freq and would pass boost and cut alike) */
sp(x, "dist_drive", 0); sp(x, "src_mix", 127); sp(x, "noise_decay", 127); sp(x, "noise_colour", 127);
sp(x, "filter_cutoff", 127); sp(x, "filter_res", 0);
sp(x, "eq_freq", 90); sp(x, "eq_q", 40);
sp(x, "eq_gain", 63); sp(x, "trig", 1); float boost = run(x, 60).rms;
sp(x, "eq_gain", -63); sp(x, "trig", 1); float cut = run(x, 60).rms;
CHECK(boost > cut * 1.3, "EQ boost vs cut (%.4f vs %.4f)", boost, cut);
sp(x, "eq_gain", 0); sp(x, "src_mix", 0);
/* pan */
sp(x, "out_pan", -63); sp(x, "trig", 1); StatLR l = run_lr(x, 60);
sp(x, "out_pan", 63); sp(x, "trig", 1); StatLR r = run_lr(x, 60);
CHECK(l.left > l.right * 8 && r.right > r.left * 8, "pan hard left/right");
sp(x, "out_pan", 0);
/* out_sel: inaudible by design (buses sum to main) — deterministic → bit-identical rms */
sp(x, "out_sel", 0); sp(x, "trig", 1); float b1 = run(x, 40).rms;
sp(x, "out_sel", 2); sp(x, "trig", 1); float b2 = run(x, 40).rms;
CHECK(fabsf(b1 - b2) < 1e-6, "out_sel sums to main (%.6f vs %.6f)", b1, b2);
sp(x, "out_sel", 0);
/* choke: voices idx 0 and idx 2 share group 1, hard-panned apart so the choke is visible per channel;
   choker has a short body of its own so the post-choke window holds only what must be silent */
sp(x, "voice", 0); sp(x, "osc_decay", 127); sp(x, "out_pan", -63); sp(x, "choke", 1);
sp(x, "voice", 2); sp(x, "src_mix", 0); sp(x, "osc_decay", 30); sp(x, "out_pan", 63); sp(x, "choke", 1);
sp(x, "voice", 0); sp(x, "trig", 1); run(x, 10);        /* voice 0 sustaining on the left */
sp(x, "voice", 2); sp(x, "trig", 1);                    /* voice 2 hits right, chokes voice 0 */
float step = maxstep(x, 20);                            /* both channels, covers the 5 ms choke fade */
CHECK(step < 1000, "choke fade, no click (max step %.0f)", step);
Stat after = run(x, 100);
/* the choked voice is the one panned left, so the check watches that channel (the plan: "and that the
   channel goes silent after"). The mix's own peak is the choker's residual tail on the right: its
   osc_decay 30 is a 12 ms tau, so 58 ms after the trigger it is still at 0.8% of full scale (288 int16). */
CHECK(after.peak_l < 200, "choked voice silent (%d)", after.peak_l);
sp(x, "voice", 0); sp(x, "choke", 0); sp(x, "out_pan", 0);
sp(x, "voice", 2); sp(x, "choke", 0); sp(x, "out_pan", 0);
sp(x, "voice", 0);
/* 12 drums at once: soft-clip holds the ceiling and they stay loud */
for (int v = 0; v < 12; v++) { sp(x, "voice", v); sp(x, "osc_decay", 127); sp(x, "out_vol", 127);
  sp(x, "out_pan", 0); sp(x, "choke", 0); sp(x, "trig", 1); }
s = run(x, 100);
CHECK(s.nan == 0 && s.peak <= 32767 && s.peak > 24000, "12-voice sum loud and under ceiling (%d)", s.peak);

    E->destroy(y);
    E->destroy(x);
    printf(fails ? "FAILED: %d\n" : "PASSED\n", fails);
    return fails != 0;
}
