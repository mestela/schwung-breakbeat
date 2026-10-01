#include <math.h>
#include <stdio.h>
#include "grain_stretch.h"

static int passed, failed;
#define CHECK(ok, label) do { if (ok) passed++; else { failed++; fprintf(stderr, "FAIL: %s\n", label); } } while (0)

int main(void) {
    bb_grain_t grain;
    bb_grain_reset(&grain);
    bb_grain_frame_t frame = {0};
    for (int i = 0; i < 64; i++)
        frame = bb_grain_next(&grain, (float)(i * 2), 2.0f, 64, 1, 0);
    CHECK(fabsf(frame.current_pos - 63.0f) < 0.01f,
          "pitch lock reads the first grain at its original pitch");
    frame = bb_grain_next(&grain, 128.0f, 2.0f, 64, 1, 0);
    CHECK(fabsf(frame.current_pos - 128.0f) < 0.01f,
          "next grain catches up to the song tempo");
    CHECK(frame.previous_gain == 1.0f && frame.current_gain == 0.0f,
          "grain boundary starts with the previous voice for a smooth join");

    bb_grain_reset(&grain);
    for (int i = 0; i < 64; i++)
        bb_grain_next(&grain, (float)(i * 2), 2.0f, 64, 0, 0);
    frame = bb_grain_next(&grain, 128.0f, 2.0f, 64, 0, 0);
    for (int i = 1; i < 9; i++)
        frame = bb_grain_next(&grain, 128.0f + (float)(i * 2), 2.0f, 64, 0, 0);
    CHECK(fabsf(frame.current_pos - 144.0f) < 0.01f,
          "grain effect without pitch lock follows the original repitch rate");

    bb_grain_reset(&grain);
    for (int i = 0; i < 64; i++)
        bb_grain_next(&grain, (float)i, 1.0f, 64, 0, 100);
    frame = bb_grain_next(&grain, 64.0f, 1.0f, 64, 0, 100);
    CHECK(fabsf(frame.current_pos) < 0.01f,
          "full grain effect repeats the first grain");
    for (int i = 1; i < 64; i++)
        bb_grain_next(&grain, 64.0f + (float)i, 1.0f, 64, 0, 100);
    frame = bb_grain_next(&grain, 128.0f, 1.0f, 64, 0, 100);
    CHECK(fabsf(frame.current_pos - 128.0f) < 0.01f,
          "grain effect catches up after the repeat");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
