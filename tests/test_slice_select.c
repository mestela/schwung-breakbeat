#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../src/dsp/slice_select.h"

/* Deterministic seeded PRNG for tests. xorshift32. */
typedef struct { uint32_t state; } test_rng_t;

static float test_rand(void *ctx) {
    test_rng_t *r = (test_rng_t *)ctx;
    uint32_t x = r->state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->state = x;
    return (float)(x & 0x00FFFFFF) / (float)0x01000000;
}

static int g_pass = 0, g_fail = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (cond) { g_pass++; } else { \
        g_fail++; printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps, msg) do { \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) <= (eps)) { g_pass++; } else { \
        g_fail++; printf("FAIL: %s — got %f, expected %f (%s:%d)\n", msg, _a, _b, __FILE__, __LINE__); \
    } \
} while (0)

int main(void) {
    /* === weight_at: Anchor=0 → uniform 1.0 === */
    for (int i = 0; i < 8; i++) {
        float w = slice_select_weight_at(i, 0.0f);
        ASSERT_NEAR(w, 1.0f, 1e-5f, "weight_at(i, 0) should be 1.0");
    }

    /* === weight_at: Anchor=1 → locked curve === */
    {
        static const float expected[8] = {0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f};
        for (int i = 0; i < 8; i++) {
            float w = slice_select_weight_at(i, 1.0f);
            ASSERT_NEAR(w, expected[i], 1e-5f, "weight_at(i, 1) should match locked curve");
        }
    }

    /* === weight_at: Anchor=0.5 → halfway between 1.0 and locked === */
    {
        static const float locked[8] = {0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f};
        for (int i = 0; i < 8; i++) {
            float w = slice_select_weight_at(i, 0.5f);
            float expected = 0.5f * 1.0f + 0.5f * locked[i];
            ASSERT_NEAR(w, expected, 1e-5f, "weight_at(i, 0.5) interpolation");
        }
    }

    /* === apply_phrase: phrase_bars=0 → no modulation === */
    {
        slice_inputs_t in = {0};
        in.complexity = 0.5f; in.anchor = 0.7f; in.roll = 0.3f;
        in.phrase_bars = 0; in.fill = 1.0f; in.bar_in_phrase = 0;

        float c, a, r;
        slice_select_apply_phrase(&in, &c, &a, &r);
        ASSERT_NEAR(c, 0.5f, 1e-5f, "phrase off: complexity unchanged");
        ASSERT_NEAR(a, 0.7f, 1e-5f, "phrase off: anchor unchanged");
        ASSERT_NEAR(r, 0.3f, 1e-5f, "phrase off: roll unchanged");
    }

    /* === apply_phrase: bar 0 of 4-bar phrase → no modulation === */
    {
        slice_inputs_t in = {0};
        in.complexity = 0.5f; in.anchor = 0.7f; in.roll = 0.3f;
        in.phrase_bars = 4; in.fill = 1.0f; in.bar_in_phrase = 0;

        float c, a, r;
        slice_select_apply_phrase(&in, &c, &a, &r);
        ASSERT_NEAR(c, 0.5f, 1e-5f, "non-fill bar: complexity unchanged");
        ASSERT_NEAR(a, 0.7f, 1e-5f, "non-fill bar: anchor unchanged");
        ASSERT_NEAR(r, 0.3f, 1e-5f, "non-fill bar: roll unchanged");
    }

    /* === apply_phrase: fill bar (bar 3 of 4) with Fill=1 === */
    {
        slice_inputs_t in = {0};
        in.complexity = 0.5f; in.anchor = 0.7f; in.roll = 0.3f;
        in.phrase_bars = 4; in.fill = 1.0f; in.bar_in_phrase = 3;

        float c, a, r;
        slice_select_apply_phrase(&in, &c, &a, &r);
        ASSERT_NEAR(c, 1.0f, 1e-5f, "fill=1: complexity → 1.0");
        ASSERT_NEAR(a, 0.0f, 1e-5f, "fill=1: anchor → 0");
        ASSERT_NEAR(r, 0.0f, 1e-5f, "fill=1: roll → 0");
    }

    /* === apply_phrase: fill bar with Fill=0.5 → halfway === */
    {
        slice_inputs_t in = {0};
        in.complexity = 0.5f; in.anchor = 0.8f; in.roll = 0.6f;
        in.phrase_bars = 4; in.fill = 0.5f; in.bar_in_phrase = 3;

        float c, a, r;
        slice_select_apply_phrase(&in, &c, &a, &r);
        ASSERT_NEAR(c, 0.75f, 1e-5f, "fill=0.5: complexity halfway to 1.0");
        ASSERT_NEAR(a, 0.4f,  1e-5f, "fill=0.5: anchor halved");
        ASSERT_NEAR(r, 0.3f,  1e-5f, "fill=0.5: roll halved");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
