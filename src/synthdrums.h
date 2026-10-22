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

/* The edit panel is the PK_PATCH params (P_PITCH .. P_CHOKE first): P2E maps VST enum order to the
 * selected voice's p[] index; the generated static assert in params_gen.h pins the 1.0.0 block. */

/* src/filter.c: the voice's biquad filter (RBJ's cookbook: 12 dB/oct, low pass / band pass / high pass with
 * a resonant peak). The coefficients are normalized (a0 = 1), the delay line is Direct Form I and the env
 * field holds the filter envelope's current cutoff offset, in Hz (the voice decays it and asks for a new
 * coefficient set once a block as it sweeps). */
typedef struct {
    float b0, b1, b2, a1, a2;   /* the normalized coefficients */
    float x1, x2, y1, y2;       /* the delay line */
    float env;                  /* the filter env's cutoff offset, in Hz */
} Filt;

void filt_set(Filt *f, float cutoff_hz, float q, int mode /*0=LP,1=BP,2=HP*/);
float filt_step(Filt *f, float x);

/* src/fx.c -- the voice's effects (the chain: sources -> filters -> stutter -> dist -> EQ x3 -> ring ->
 * FM -> comb -> gated reverb -> vol/pan -> the stereo delay -> the choke fade -> the bus).
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

/* The FX row (spec §8): ring mod, operator FM, comb and gated reverb. Each function returns x EXACTLY
 * when its depth/mix argument is 0, so a knob at zero is a true bypass (no rounding, no state change).
 * All four states are per-hit: sd_voice_reset() clears them, so two hits with the same settings render
 * bit-identical audio. */

/* Ring mod: y = x*((1-g) + g*sin(2*pi*ph)), the carrier ph advancing at hz. g ramps to its target over
 * 1 ms (a step in the ring depth would click). */
typedef struct { float ph, g; } RingSt;
float fx_ring(RingSt *r, float x, float hz, float g);

/* Operator FM: y = x + g*sin(2*pi*ph_c + idx*x) — a 150 Hz operator on the voice itself. */
typedef struct { float ph; } FmSt;
float fx_fm(FmSt *f, float x, float idx, float g);

/* Comb: a feedback delay line (fb 0.75), out = the read head, delay = the knob's ms. The buffer covers
 * the longest delay (30 ms = 1323 samples) plus linear-interp margin; w is the write cursor. */
#define COMB_BUF 1408
typedef struct { float buf[COMB_BUF]; int w; } CombSt;
float fx_comb(CombSt *cb, float x, float ms, float fb, float g);

/* Gated reverb: a Schroeder bank (4 combs + 2 allpass) behind a per-hit gate — the gate is open for
 * GATE_S from the hit, then fades out over 5 ms, so the tail stops dead (the classic gated look). The
 * gate's age t_hit comes from the voice's own per-hit timer. */
#define GVERB_GATE_S 0.2f
typedef struct { float c[4][2048], a[2][224]; int ci[4], ai[2]; } GVerbSt;   /* combs < 2048; allpass: 5.0 ms = 221 < 224 */
float fx_gverb(GVerbSt *gv, float x, float m, float t_hit);

/* The stereo delay: Airwindows Doublelay, vendored and adapted (MIT License:
 * third_party/airwindows-doublelay/LICENSE; the source's header names the upstream file). One shared
 * delay line per channel with an integer read head, then 8 splicing pitch-shift heads per channel (the
 * vendor's doubler: the "detune" pulls the two channels' heads apart in speed by up to +-2.5%), and the
 * golden-ratio direct+cross feedback (the caller caps it at 0.95). Adapted to this engine's contract --
 * float, fixed 44.1 kHz, one stereo sample per call, per-hit determinism: the sample-rate scaling
 * (cycleEnd/lastRef), the VST plumbing and the output dither are gone; the knob->time mapping lives in
 * the caller (this port takes milliseconds); the doubler's position accumulators stay double (they run
 * forever and are only wrapped by subtraction); and dly_reset() initializes every field (the vendor's
 * constructor leaves tracking[] unset; this port zeroes it) so two hits render bit-identical audio. */
#define DL_LINE 44100      /* the main lines' span: 1 s at 44.1 kHz (vendor: 48005 at 48 kHz) */
#define DL_WIDTH 2300      /* the doubler buffer's width (the vendor's fixed constant) */
#define DL_ADJUST 1100     /* the splice detection's offset (the vendor's fixed constant) */
typedef struct {
    float p[2 * DL_WIDTH + 1];   /* the doubler's read buffer, double-written at [b] and [b + width] */
    double pos[8], lastpos[8];   /* the 8 heads' positions (double: they accumulate, then wrap) */
    float tracking[8];           /* the splice timers: 1.0 = splicing, 0 = this head is stable */
    int gcount, lastcount;       /* the write cursors */
    int active, bestsplice;      /* the active head and the candidate to splice to */
    float bestyet;
} DlyChan;
typedef struct {
    float l[DL_LINE + 1], r[DL_LINE + 1];   /* the main delay lines: 1 s at 44.1 kHz + 1 */
    DlyChan lc, rc;
    float feedbackL, feedbackR; /* the previous doubled outputs (the direct+cross feedback) */
    float airPrevL, airEvenL, airOddL, airPrevR, airEvenR, airOddR;  /* the "air" treble compensator */
    int flip;                   /* the air's even/odd alternator */
    int dcount;                 /* the main lines' descending write cursor */
} DlySt;
void dly_reset(DlySt *d);
void dly_step(DlySt *d, float inl, float inr, float tl_ms, float tr_ms, float detune01, float fb,
              float wet, float *ol, float *or_);

/* The choke fade: 5 ms, from full level to silence. */
#define CHOKE_FADE_S 0.005f

/* The LFOs (Task 9, spec §6): three per voice, assignable, free-run or tempo-synced, retriggered with
 * the hit by default. This is continuous per-voice state, NOT per-hit state: retrig OFF runs on across
 * hits, so sd_voice_reset() leaves it alone (it only seeds rng: an all-zero LFSR never advances) and
 * sd_voice_note() applies the retrig knob. ph is the cycle position 0..1, hold the s&h value (and the
 * rnd slew's start), tgt the rnd's next target, rng the random source (1.0.0's noise LFSR). */
typedef struct {
    float ph, hold, tgt;
    uint32_t rng;
} LFO;

typedef struct {
    float p[EDIT_PARAMS];   /* the voice's sound: P_PITCH .. P_CHOKE, in parameter order */
    int sounding;           /* a trigger (a pad, TRIG) hit this voice and its one-shot has not finished yet */
    /* The voice's DSP state. sd_voice_note() resets all of it at every trigger, so two hits with the same
     * settings render bit-identical audio (the engine's per-hit determinism guarantee). The LFOs are the
     * exception: their phase and random source are continuous per-voice state (retrig OFF runs on). */
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
    float t_amp;            /* the osc amp env's age: its hold/release law needs the clock, not just a coef */
    float brown;            /* the noise colour's leaky integrator */
    uint32_t lfsr;          /* the noise LFSR (re-seeded at every trigger) */
    Filt filt;              /* the oscillator branch's filter: coefficients, delay line, filter env */
    float fenv_t;           /* the osc filter env's age, in seconds */
    Filt nfil;              /* the noise branch's filter (same biquad, its own envelope) */
    float nfenv_t;          /* the noise filter env's age, in seconds */
    Stutter stut;           /* the stutter gate's phase */
    PeakEQ eq[3];           /* the three peaking EQ bands' coefficients and delay lines, in series */
    RingSt ring;            /* the FX row's states, in chain order after the EQ */
    FmSt fm;
    CombSt comb;
    GVerbSt gv;
    float gverb_t;          /* the gated reverb's per-hit gate age, in seconds */
    DlySt dly;              /* the stereo delay, last in the chain -- after the pan, so the wet tails
                             * are independent of the voice's pan (spec §8) */
    float dly_quiet_t;      /* the delay's tail-liveness clock: quiet seconds since the last energy */
    LFO lfo[3];             /* the three LFOs (Task 9): continuous per-voice state, retrig OFF runs on */
    float choke_gain;       /* the choke fade's multiplier: 1 = free, 0 = silenced */
    float choke_t;          /* the choke fade's age, in seconds */
    int choke_fading;       /* the choke fade is running: the voice is on its way to silence */
} Voice;

typedef struct {
    Voice v[12];
    int sel;                /* the selected voice, 0-based (the UI shows sel + 1) */
    int base_note;          /* MIDI note that plays v[0]; the other voices fold by 12 (device-level: survives KIT INIT) */
    float bpm;              /* the host's tempo for the LFOs' sync (set_param "lfo_bpm"; the default 120) */
    unsigned rev;           /* display_rev: bumped when the engine changes values by itself */
    unsigned nan_count;     /* the samples the master bus had to silence (not finite): the test watches this */
    float clip[EDIT_PARAMS];/* voice_copy's snapshot of the selected voice (session-only: never serialized) */
    int have_clip;          /* the clipboard holds a voice */
} Synth;

void patch_init_voice(Voice *v);   /* INIT_PATCH (generated, p[] order) fills the voice */
void hz_str(float hz, char *buf, int n);   /* "940 Hz" / "12.5 kHz": the dynamic frequency readouts */
void patch_init_kit(Synth *s);

/* The tempo-division grid shared by the stutter and the delay syncs (1.0.2): the beat multiplier is
 * beats per cycle (1/4 note = 1.0), so rate = (bpm/60)/mult and time = mult*60/bpm seconds. Defined in
 * src/voice.c; the display branches in src/synth.c read the words, and the knob quantize expressions
 * are duplicated there (keep them in sync). */
#define SD_DIV_COUNT 18
extern const float SD_DIV_BEATS[SD_DIV_COUNT];
extern const char *const SD_DIV_WORDS[SD_DIV_COUNT];

/* src/voice.c: one voice's DSP -- the oscillator, the noise, the pitch and amp envelopes, the x-mod and the
 * velocity routings. Nothing here allocates and nothing here touches the other voices. */
void sd_voice_reset(Voice *v);                              /* zero the DSP state */
void sd_voice_note(Voice *v, float vel);                    /* (re)trigger: restart everything, keep the velocity scalars */
void sd_voice_render(Voice *v, float *bus, int frames, float bpm);   /* add the voice's signal to the stereo bus (interleaved: 2*frames floats); bpm feeds the LFOs' sync */
