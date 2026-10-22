/* MPC Synthdrums -- the plugin engine (wrapper/engine.h): parameters, MIDI and audio for one instance.
 * The twelve voices are drum synths (src/voice.c) and render() sums them into the master bus. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "synthdrums.h"

/* The master bus's gain: one init voice at velocity 127 peaks at about -6 dBFS (the soft clip below keeps
 * the sum of twelve voices inside the int16 range). */
#define MASTER_GAIN 1.0f

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void *create(const char *data_dir)
{
    Synth *s = calloc(1, sizeof *s);
    int i;
    (void)data_dir;
    if (!s) return NULL;
    patch_init_kit(s);
    s->base_note = (int)PDEF[P_BASE_NOTE].def;   /* device-level: a fresh kit starts at C1 (36) */
    s->bpm = 120.0f;                             /* the LFOs' sync tempo until the host says otherwise */
    for (i = 0; i < 12; i++) {   /* the voices know their own number: their key is base_note + index */
        s->v[i].index = i;
        s->v[i].base_note = s->base_note;
    }
    return s;
}

static void destroy(void *p) { free(p); }

/* The choke groups: a trigger of voice i in group g (g > 0) silences the other sounding voices of the
 * same group -- the hi-hat pairs. The fade lives in the voice itself (src/voice.c), so the choking voice's
 * own hit is untouched: a fade on the master bus would take that down as well. Group 0 is "off": such a
 * voice never chokes and is never choked. */
static void choke_group(Synth *s, int idx)
{
    const int g = (int)(s->v[idx].p[P2E[P_CHOKE]] + 0.5f);
    int j;
    if (g <= 0) return;
    for (j = 0; j < 12; j++) {
        Voice *o = &s->v[j];
        if (j == idx || !o->sounding) continue;
        if ((int)(o->p[P2E[P_CHOKE]] + 0.5f) != g) continue;
        o->choke_fading = 1;   /* the fade starts at full level and reaches silence in 5 ms */
        o->choke_t = 0.0f;
        o->choke_gain = 1.0f;
    }
}

/* A note-on folds into the kit: voice ((note - base_note) mod 12), so every pad bank plays all twelve voices.
 * Note-off is ignored: a drum voice is a one-shot, not a gate. */
static void midi(void *p, const uint8_t *m, int len)
{
    Synth *s = p;
    if (len < 3) return;
    if ((m[0] & 0xF0) == 0x90 && m[2]) {
        const int idx = ((m[1] - s->base_note) % 12 + 12) % 12;
        if (idx != s->sel) { s->sel = idx; s->rev++; }   /* a pad hit selects its voice for editing */
        Voice *v = &s->v[idx];
        v->note = m[1];                       /* the played note: pitch_track's reference */
        sd_voice_note(v, (float)m[2] / 127.0f);
        choke_group(s, idx);
    }
}

static int key_index(const char *k)
{
    int i;
    for (i = 0; i < P_COUNT; i++) if (!strcmp(k, PDEF[i].key)) return i;
    return -1;
}

/* A frequency as a readout: "940 Hz" below 1 kHz, "12.5 kHz" at and above it (the wrapper's dynamic
 * display strings -- get_param("<key>_display")). */
void hz_str(float hz, char *buf, int n)
{
    if (hz < 1000.0f) snprintf(buf, (size_t)n, "%d Hz", (int)(hz + 0.5f));
    else snprintf(buf, (size_t)n, "%.1f kHz", hz * 0.001f);
}

/* ---------------------------------------------------------------- the state: "MSDR 1" + sel/base + twelve voice lines */
#define STATE_CAP 8192   /* the whole state is under 2.5 KB; the wrapper's chunk buffer holds 8 KB */

static void add(char *buf, size_t cap, size_t *n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (r < 0) return;
    if ((size_t)r >= cap - *n) { *n = cap - 1; return; }   /* truncated: stay NUL-terminated (cannot happen at this size) */
    *n += (size_t)r;
}

static char *serialize(const Synth *s, int *len)
{
    char *buf = malloc(STATE_CAP);
    size_t n = 0;
    int i, k;
    if (!buf) return NULL;
    add(buf, STATE_CAP, &n, "MSDR 1\nsel=%d\nbase=%d\n", s->sel, s->base_note);
    for (i = 0; i < 12; i++) {
        add(buf, STATE_CAP, &n, "v%d=", i + 1);   /* 1-based line number, 0-based parameter values */
        for (k = 0; k < EDIT_PARAMS; k++) add(buf, STATE_CAP, &n, k ? ",%g" : "%g", (double)s->v[i].p[k]);
        add(buf, STATE_CAP, &n, "\n");
    }
    *len = (int)n;
    return buf;
}

/* Parse by index, clamp to PDEF's range, tolerate missing/extra lines: a chunk from another version still loads. */
static void deserialize(Synth *s, const char *text)
{
    const char *p = text;
    if (strncmp(text, "MSDR ", 5)) return;
    patch_init_kit(s);
    while ((p = strchr(p, '\n'))) {
        p++;
        if (!strncmp(p, "sel=", 4)) {
            s->sel = (int)clampf((float)atoi(p + 4), (float)PDEF[P_VOICE].min, (float)PDEF[P_VOICE].max);
            continue;
        }
        if (!strncmp(p, "base=", 5)) {
            s->base_note = (int)clampf((float)atoi(p + 5), PDEF[P_BASE_NOTE].min, PDEF[P_BASE_NOTE].max);
            continue;
        }
        if (p[0] == 'v' && p[1] >= '0' && p[1] <= '9') {
            const int idx = atoi(p + 1);
            const char *q = strchr(p, '=');
            int k;
            if (idx < 1 || idx > 12 || !q) continue;
            q++;
            for (k = 0; k < EDIT_PARAMS; k++) {
                char *end;
                const double d = strtod(q, &end);
                if (end == q) break;   /* short line: the rest stays at the init values */
                s->v[idx - 1].p[k] = clampf((float)d, PDEF[E2P[k]].min, PDEF[E2P[k]].max);
                q = end;
                if (*q != ',') break;   /* extra values are ignored */
                q++;
            }
        }
    }
    s->rev++;   /* a state load changes what the screen shows */
}

/* Every PK_PATCH parameter edits the selected voice. The selector and base note are device-level; the trigs act. */
static void set_param(void *p, const char *k, const char *val)
{
    Synth *s = p;
    int i;
    if (!strcmp(k, "state")) { deserialize(s, val); return; }
    if (!strcmp(k, "lfo_bpm")) {   /* the host's tempo for the LFOs' sync (vst.json: HAS_LFO_BPM) */
        s->bpm = (float)atof(val);
        if (s->bpm < 20.0f) s->bpm = 20.0f;
        if (s->bpm > 300.0f) s->bpm = 300.0f;
        return;
    }
    i = key_index(k);
    if (i < 0) return;
    {
        const ParamDef *d = &PDEF[i];
        const float f = clampf((float)atof(val), d->min, d->max);
        switch (d->kind) {
        case PK_PATCH:
            s->v[s->sel].p[P2E[i]] = f;
            break;
        case PK_UI:
            if (i == P_VOICE) {
                const int idx = (int)(f + 0.5f);   /* an option's index, 0-based */
                if (idx != s->sel) { s->sel = idx; s->rev++; }
            } else if (i == P_BASE_NOTE) {
                int k;
                s->base_note = (int)(f + 0.5f);
                for (k = 0; k < 12; k++) s->v[k].base_note = s->base_note;   /* the voices' own key moves with it */
            }
            break;
        case PK_TRIG:
            if (i == P_TRIG) {
                /* no edge memory: the wrapper springs the key back, so every press is a fresh 1 -> 0 */
                if (f > 0.5f) {
                    Voice *v = &s->v[s->sel];
                    v->note = s->base_note + s->sel;   /* the trigger plays the voice's own key */
                    sd_voice_note(v, 1.0f);             /* the trigger's velocity is full */
                    choke_group(s, s->sel);              /* the choke groups: the same group goes silent */
                }
            } else if (i == P_KIT_INIT) {
                if (f > 0.5f) { patch_init_kit(s); s->rev++; }
            } else if (i == P_VOICE_COPY) {
                if (f > 0.5f) { memcpy(s->clip, s->v[s->sel].p, sizeof s->clip); s->have_clip = 1; }
            } else if (i == P_VOICE_PASTE) {
                if (f > 0.5f && s->have_clip) { memcpy(s->v[s->sel].p, s->clip, sizeof s->clip); s->rev++; }
            } else if (i == P_VOICE_INIT) {
                if (f > 0.5f) { patch_init_voice(&s->v[s->sel]); s->rev++; }
            }
            /* voice_prev/voice_next never reach the engine: the wrapper steps "voice" itself (step_of) */
            break;
        default: break;   /* PK_SHOW is read-only */
        }
    }
}

static int put(char *buf, int n, const char *s)
{
    if (n <= 0) return 0;
    snprintf(buf, (size_t)n, "%s", s);
    return (int)strlen(s) > 0 ? (int)strlen(s) : 1;
}

static int get_param(void *p, const char *k, char *buf, int n)
{
    Synth *s = p;
    char t[64];
    int i;
    if (!strcmp(k, "state")) {
        int len = 0, r = 0;
        char *str = serialize(s, &len);
        if (str) {
            if (len < n) memcpy(buf, str, (size_t)len + 1);
            r = len + 1;
            free(str);
        }
        return r;
    }
    if (!strcmp(k, "display_rev")) {
        snprintf(t, sizeof t, "%u", s->rev);
        return put(buf, n, t);
    }
    if (!strcmp(k, "lfo_bpm")) {
        snprintf(t, sizeof t, "%g", (double)s->bpm);
        return put(buf, n, t);
    }
    /* "<key>_display": the engine composes a value string (dynamic_display params; see tools/gen_params.py) */
    {
        const int dlen = (int)strlen(k);
        if (dlen > 8 && !strcmp(k + dlen - 8, "_display")) {
            char base[64];
            int j, n2 = dlen - 8;
            if (n2 >= (int)sizeof base) return 0;
            memcpy(base, k, (size_t)n2); base[n2] = 0;
            j = key_index(base);
            if (j < 0 || PDEF[j].kind != PK_PATCH) return 0;
            {
                const float v = s->v[s->sel].p[P2E[j]];
                if (j == P_FILTER_CUTOFF) {
                    hz_str(30.0f * powf(16000.0f / 30.0f, v * (1.0f / 127.0f)), t, sizeof t);
                    return put(buf, n, t);
                }
                if (j == P_NOISE_FILTER) {
                    hz_str(20.0f * powf(900.0f, v * (1.0f / 127.0f)), t, sizeof t);
                    return put(buf, n, t);
                }
                if (j == P_EQ_FREQ || j == P_EQ2_FREQ || j == P_EQ3_FREQ) {
                    hz_str(40.0f * powf(300.0f, v * (1.0f / 127.0f)), t, sizeof t);
                    return put(buf, n, t);
                }
                if (j == P_RING_MOD) {
                    hz_str(powf(8000.0f, v * (1.0f / 127.0f)), t, sizeof t);   /* the carrier, 1 Hz .. 8 kHz */
                    return put(buf, n, t);
                }
                if (j == P_COMB) {
                    snprintf(t, sizeof t, "%.1f ms", 0.5f * powf(60.0f, v * (1.0f / 127.0f)));
                    return put(buf, n, t);
                }
                if (j == P_DELAY_L || j == P_DELAY_R) {
                    /* the delay times: 1..1000 ms, the same log knob mapping the voice uses */
                    snprintf(t, sizeof t, "%.0f ms", powf(1000.0f, v * (1.0f / 127.0f)));
                    return put(buf, n, t);
                }
                if (j == P_DELAY_DETUNE) {
                    /* the vendor's quadratic taper: a signed speed percent, +-2.5% at the ends */
                    const float td = v * (2.0f / 127.0f) - 1.0f;
                    snprintf(t, sizeof t, "%+.1f%%", (double)(td * fabsf(td) * 2.5f));
                    return put(buf, n, t);
                }
                if (j == P_LFO1_RATE || j == P_LFO2_RATE || j == P_LFO3_RATE) {
                    /* synced: the division governs the rate (the knob reads SYNC); free-run: the
                     * knob's 0.02..20 Hz exponential sweep, the same mapping the voice uses */
                    const int sk = j == P_LFO1_RATE ? P_LFO1_SYNC
                                  : j == P_LFO2_RATE ? P_LFO2_SYNC : P_LFO3_SYNC;
                    if (s->v[s->sel].p[P2E[sk]] != 0.0f) return put(buf, n, "SYNC");
                    hz_str(0.02f * powf(1000.0f, v * (1.0f / 127.0f)), t, sizeof t);
                    return put(buf, n, t);
                }
                return 0;   /* unknown dynamic display: the wrapper's default numeric path */
            }
        }
    }
    i = key_index(k);
    if (i < 0) return 0;
    if (PDEF[i].kind == PK_TRIG) return put(buf, n, "0");   /* a momentary key always reads back released */
    if (PDEF[i].kind == PK_SHOW) return put(buf, n, VOICE_NAMES[s->sel]);
    if (i == P_VOICE) {
        snprintf(t, sizeof t, "%d", s->sel);   /* an option's index, 0-based */
        return put(buf, n, t);
    }
    if (i == P_BASE_NOTE) {
        snprintf(t, sizeof t, "%g", (float)s->base_note);
        return put(buf, n, t);
    }
    if (PDEF[i].kind == PK_PATCH) {
        const float v = s->v[s->sel].p[P2E[i]];
        if (PDEF[i].nopts) snprintf(t, sizeof t, "%d", (int)v);
        else snprintf(t, sizeof t, "%g", v);
        return put(buf, n, t);
    }
    return 0;
}

/* Denormals: the audio runs with flush-to-zero on, the host's setting put back afterwards */
#if defined(__arm__)
static inline unsigned fz_on(void)
{
    unsigned old;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(old));
    __asm__ volatile("vmsr fpscr, %0" : : "r"(old | (1u << 24)));
    return old;
}
static inline void fz_off(unsigned old) { __asm__ volatile("vmsr fpscr, %0" : : "r"(old)); }
#elif defined(__aarch64__)
static inline unsigned fz_on(void)
{
    unsigned long old;
    __asm__ volatile("mrs %0, fpcr" : "=r"(old));
    __asm__ volatile("msr fpcr, %0" : : "r"(old | (1ul << 24)));
    return (unsigned)old;
}
static inline void fz_off(unsigned old)
{
    unsigned long v = old;
    __asm__ volatile("msr fpcr, %0" : : "r"(v));
}
#elif defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
static inline unsigned fz_on(void) { const unsigned old = _mm_getcsr(); _mm_setcsr(old | 0x8040u); return old; }
static inline void fz_off(unsigned old) { _mm_setcsr(old); }
#else
static inline unsigned fz_on(void) { return 0; }
static inline void fz_off(unsigned old) { (void)old; }
#endif

/* SAMPLE_ACCURATE: any frame count from 1 to 128 (wrapper/engine.h). The twelve voices sum into the master
 * bus (stereo: each voice carries its own pan), the soft clip x/(1+|x|) keeps them inside the int16 range
 * and the gain sets the level. */
static void render(void *p, int16_t *out, int frames)
{
    const unsigned fpu = fz_on();
    Synth *s = p;
    float acc[256];
    int i, n;
    if (frames > 128) frames = 128;
    if (frames <= 0) { fz_off(fpu); return; }
    for (i = 0; i < 2 * frames; i++) acc[i] = 0.0f;
    for (n = 0; n < 12; n++) {
        Voice *v = &s->v[n];
        if (!v->sounding) continue;   /* the one-shot is over: the voice costs nothing */
        sd_voice_render(v, acc, frames, s->bpm);
    }
    for (i = 0; i < frames; i++) {
        float xl = acc[2 * i] * MASTER_GAIN;
        float xr = acc[2 * i + 1] * MASTER_GAIN;
        int16_t yl, yr;
        if (!isfinite(xl)) { xl = 0.0f; s->nan_count++; }   /* a NaN would be undefined behaviour here */
        if (!isfinite(xr)) { xr = 0.0f; s->nan_count++; }
        xl = xl / (1.0f + fabsf(xl));                        /* the master soft clip */
        xr = xr / (1.0f + fabsf(xr));
        if (xl > 1.0f) xl = 1.0f; else if (xl < -1.0f) xl = -1.0f;
        if (xr > 1.0f) xr = 1.0f; else if (xr < -1.0f) xr = -1.0f;
        yl = (int16_t)lrintf(xl * 32767.0f);
        yr = (int16_t)lrintf(xr * 32767.0f);
        out[2 * i] = yl;
        out[2 * i + 1] = yr;
    }
    fz_off(fpu);
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
