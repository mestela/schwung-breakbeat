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
    /* Tests will be added in later phases. */

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
