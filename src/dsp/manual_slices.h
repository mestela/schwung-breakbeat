#ifndef BB_MANUAL_SLICES_H
#define BB_MANUAL_SLICES_H
#include <stdint.h>

#define BB_SLICE_UNITS 1000000u
void bb_place_slices(uint32_t frames, int manual, const uint32_t positions[8],
                     uint32_t starts[8], uint32_t lengths[8]);
uint32_t bb_reflected_frame(uint32_t start, uint32_t length, float position);
#endif
