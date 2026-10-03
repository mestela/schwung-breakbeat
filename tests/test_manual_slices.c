#include <stdint.h>
#include <stdio.h>
#include "manual_slices.h"

int main(void) {
    uint32_t positions[8], starts[8], lengths[8];
    for (int i = 0; i < 8; i++) positions[i] = (uint32_t)i * BB_SLICE_UNITS / 8u;
    bb_place_slices(80000, 0, positions, starts, lengths);
    for (int i = 0; i < 8; i++)
        if (starts[i] != (uint32_t)i * 10000u || lengths[i] != 10000u) return 1;
    positions[2] = 260000;
    bb_place_slices(80000, 1, positions, starts, lengths);
    if (starts[2] != 20800 || lengths[1] != 10800 || lengths[2] != 9200) return 2;
    for (int i = 0; i < 7; i++)
        if (starts[i] + lengths[i] != starts[i + 1]) return 3;
    if (starts[7] + lengths[7] != 80000) return 4;
    const uint32_t bounced[] = {10, 11, 12, 13, 14, 13, 12, 11, 10, 11};
    for (int i = 0; i < 10; i++)
        if (bb_reflected_frame(10, 5, (float)(10 + i)) != bounced[i]) return 5;
    if (bb_reflected_frame(10, 1, 100.0f) != 10) return 6;
    puts("manual slicing passed");
    return 0;
}
