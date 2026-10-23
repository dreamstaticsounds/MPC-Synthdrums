/* MPC Synthdrums -- golden-hash harness (1.0.3 Task 1).
 *
 * Pins the 1.0.2 FX sound across the 1.0.3 engine changes: three scenarios rendered from a fresh
 * engine and hashed FNV-1a-64 over the raw int16 stereo stream. The fixture test/goldens/
 * fx-sound.txt is captured from the PRISTINE 1.0.2 engine (972c519) before any 1.0.3 change:
 * every scenario uses only 1.0.2 params, so the 1.0.3 knobs at their defaults (both delay fbs 64,
 * the verb length 1, the three mixes pure wet, dist_tone still applied) must reproduce every
 * captured hash -- the design's bit-identity promises, checked at once.
 * Compiled like engine_test plus -ffp-contract=off (the goldens compare build-to-build, so the
 * contraction policy is pinned); x86-only (arm_sim.sh runs engine_test alone).
 *   build/host/golden_test test/goldens/fx-sound.txt            verify (exit 1 on mismatch)
 *   build/host/golden_test test/goldens/fx-sound.txt --capture  (re)write the fixture
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "engine.h"

static const mpc_engine_t *E;
static int16_t buf[256];
static int fails;

#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

/* FNV-1a-64 over the raw interleaved int16 samples of the rendered blocks */
static uint64_t hash_run(void *x, int blocks)
{
    uint64_t h = 1469598103934665603ULL;   /* the offset basis */
    int b, i;
    for (b = 0; b < blocks; b++) {
        E->render(x, buf, 128);
        for (i = 0; i < 256; i++) { h ^= (uint64_t)(uint16_t)buf[i]; h *= 1099511628211ULL; }
    }
    return h;
}

/* the scenarios: (name, blocks, {key, value} pairs, 0-terminated) */
typedef struct { const char *name; int blocks; const char *set[12][2]; } scenario;
static const scenario SCEN[] = {
    {"verb-delay", 320, {{"gverb", "64"}, {"delay_wet", "127"}, {"delay_l", "64"}, {"delay_r", "64"},
                         {"delay_detune", "64"}, {"delay_fb", "64"}, {"dist_drive", "96"}, {0, 0}}},
    {"dark-tone", 128, {{"dist_drive", "96"}, {"dist_tone", "40"}, {0, 0}}},
    {"fx-mixes", 128, {{"ring_mod", "64"}, {"fm_index", "64"}, {"comb", "32"}, {0, 0}}},
};

int main(int argc, char **argv)
{
    const char *fixture = argc > 1 ? argv[1] : "test/goldens/fx-sound.txt";
    const int capture = argc > 2 && !strcmp(argv[2], "--capture");
    uint64_t got[sizeof SCEN / sizeof *SCEN];
    int s, i;

    E = mpc_engine();
    CHECK(E != NULL, "mpc_engine");
    if (!E) return 1;
    for (s = 0; s < (int)(sizeof SCEN / sizeof *SCEN); s++) {
        void *x = E->create(NULL);
        uint8_t m[3] = {0x90, 36, 127};   /* one hit, full velocity */
        CHECK(x != NULL, "%s: create", SCEN[s].name);
        if (!x) return 1;
        for (i = 0; SCEN[s].set[i][0]; i++) E->set_param(x, SCEN[s].set[i][0], SCEN[s].set[i][1]);
        E->midi(x, m, 3);
        got[s] = hash_run(x, SCEN[s].blocks);
        E->destroy(x);
    }
    if (capture) {
        FILE *f = fopen(fixture, "w");
        if (!f) { fprintf(stderr, "golden_test: cannot write %s\n", fixture); return 1; }
        for (s = 0; s < (int)(sizeof SCEN / sizeof *SCEN); s++)
            fprintf(f, "%s %016llx\n", SCEN[s].name, (unsigned long long)got[s]);
        fclose(f);
        printf("golden_test: captured %d scenarios into %s\n", s, fixture);
        return 0;
    }
    {   /* verify: every scenario's name + hash must match the fixture */
        char line[128];
        FILE *f = fopen(fixture, "r");
        if (!f) { fprintf(stderr, "golden_test: missing fixture %s (capture it at the pristine engine first)\n",
                          fixture); return 1; }
        for (s = 0; s < (int)(sizeof SCEN / sizeof *SCEN); s++) {
            unsigned long long want = 0; char name[64] = ""; int ok = 0;
            rewind(f);
            while (fgets(line, sizeof line, f))
                if (sscanf(line, "%63s %llx", name, &want) == 2 && !strcmp(name, SCEN[s].name)) { ok = 1; break; }
            CHECK(ok && want == (unsigned long long)got[s], "golden %s: want %016llx got %016llx",
                  SCEN[s].name, ok ? want : 0ULL, (unsigned long long)got[s]);
        }
        fclose(f);
    }
    printf(fails ? "golden_test: FAILED (%d)\n" : "golden_test: PASSED\n", fails);
    return fails != 0;
}
