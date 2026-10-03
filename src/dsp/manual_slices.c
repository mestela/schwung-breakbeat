#include "manual_slices.h"
#include <math.h>

void bb_place_slices(uint32_t frames, int manual, const uint32_t positions[8],
                     uint32_t starts[8], uint32_t lengths[8]) {
    if (!frames) {
        for (int i = 0; i < 8; i++) starts[i] = lengths[i] = 0;
        return;
    }
    for (int i = 0; i < 8; i++) {
        uint32_t grid = (uint32_t)((uint64_t)frames * (uint32_t)i / 8u);
        uint32_t start = manual && positions
                       ? (uint32_t)((uint64_t)frames * positions[i] / BB_SLICE_UNITS)
                       : grid;
        uint32_t minimum = i ? starts[i - 1] + 1u : 0u;
        uint32_t maximum = frames > (uint32_t)(8 - i)
                         ? frames - (uint32_t)(8 - i) : frames - 1u;
        if (start < minimum) start = minimum;
        if (start > maximum) start = maximum;
        starts[i] = start;
    }
    for (int i = 0; i < 7; i++) lengths[i] = starts[i + 1] - starts[i];
    lengths[7] = frames - starts[7];
}

uint32_t bb_reflected_frame(uint32_t start, uint32_t length, float position) {
    if (length <= 1u) return start;
    float distance = position - (float)start;
    float edge = (float)(length - 1u);
    if (distance >= 0.0f && distance <= edge)
        return start + (uint32_t)distance;
    float period = edge * 2.0f;
    float travel = fmodf(distance, period);
    if (travel < 0.0f) travel += period;
    float offset = travel <= edge ? travel : period - travel;
    return start + (uint32_t)offset;
}
