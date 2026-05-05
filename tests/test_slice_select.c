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

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
