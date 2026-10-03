#include <stdio.h>
#include <stdint.h>
#include "slice_select.h"

typedef struct { uint32_t state; } rng_t;
static float next_random(void *ctx) {
    rng_t *r = ctx;
    uint32_t x = r->state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->state = x;
    return (float)(x & 0xffffff) / 16777216.0f;
}

int main(void) {
    rng_t rng = {12345};
    slice_inputs_t in = {.complexity = 1.0f};
    for (int p = 0; p < 8; p++) {
        in.beat_position = p;
        in.anchors[p] = 1.0f;
        for (int n = 0; n < 1000; n++)
            if (slice_select_next(&in, next_random, &rng) != p) return 1;
        in.anchors[p] = 0.0f;
        for (int n = 0; n < 1000; n++) {
            int got = slice_select_next(&in, next_random, &rng);
            if (got == p || got < 0 || got > 7) return 2;
        }
    }
    in.complexity = 0.0f;
    for (int p = 0; p < 8; p++) {
        in.beat_position = p;
        for (int n = 0; n < 100; n++)
            if (slice_select_next(&in, next_random, &rng) != p) return 3;
    }
    in.complexity = 0.8f;
    in.beat_position = 3;
    in.anchors[3] = 0.5f;
    int natural = 0;
    for (int n = 0; n < 10000; n++)
        natural += slice_select_next(&in, next_random, &rng) == 3;
    if (natural < 5700 || natural > 6300) return 4;
    puts("slice selection passed");
    return 0;
}
