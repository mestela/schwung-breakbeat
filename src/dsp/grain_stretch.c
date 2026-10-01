#include "grain_stretch.h"

void bb_grain_reset(bb_grain_t *grain) {
    grain->current_start = 0.0f;
    grain->previous_start = 0.0f;
    grain->phase = 0;
    grain->grain_index = 0;
    grain->have_previous = 0;
}

bb_grain_frame_t bb_grain_next(bb_grain_t *grain, float source_pos,
                               float source_rate, int cycle_frames,
                               int pitch_lock, int repeat_amount) {
    if (cycle_frames < 32) cycle_frames = 32;
    if (repeat_amount < 0) repeat_amount = 0;
    if (repeat_amount > 100) repeat_amount = 100;

    if (!grain->have_previous) {
        grain->current_start = source_pos;
        grain->have_previous = 1;
    } else if (grain->phase >= cycle_frames) {
        grain->previous_start = grain->current_start;
        grain->current_start = source_pos;
        grain->phase = 0;
        grain->grain_index++;

        /* Alternating grains can replay their predecessor. The next grain
         * catches up to the transport position, preserving slice duration. */
        unsigned int pattern = grain->grain_index * 37u + 17u;
        if ((grain->grain_index & 1u) && (int)(pattern % 100u) < repeat_amount)
            grain->current_start = grain->previous_start;
    }

    float read_rate = pitch_lock ? 1.0f : source_rate;
    bb_grain_frame_t frame;
    frame.current_pos = grain->current_start + grain->phase * read_rate;
    frame.previous_pos = grain->previous_start +
                         (cycle_frames + grain->phase) * read_rate;
    frame.current_gain = 1.0f;
    frame.previous_gain = 0.0f;

    int fade_frames = cycle_frames / 8;
    if (fade_frames > 220) fade_frames = 220;
    if (fade_frames < 1) fade_frames = 1;
    if (grain->grain_index > 0 && grain->phase < fade_frames) {
        frame.current_gain = (float)grain->phase / (float)fade_frames;
        frame.previous_gain = 1.0f - frame.current_gain;
    }
    grain->phase++;
    return frame;
}
