#ifndef BB_GRAIN_STRETCH_H
#define BB_GRAIN_STRETCH_H

/* A small, allocation-free grain cursor. The caller owns the sample reader. */
typedef struct {
    float current_start;
    float previous_start;
    int phase;
    unsigned int grain_index;
    int have_previous;
} bb_grain_t;

typedef struct {
    float current_pos;
    float previous_pos;
    float current_gain;
    float previous_gain;
} bb_grain_frame_t;

void bb_grain_reset(bb_grain_t *grain);
bb_grain_frame_t bb_grain_next(bb_grain_t *grain, float source_pos,
                               float source_rate, int cycle_frames,
                               int pitch_lock, float pitch_ratio,
                               int repeat_amount);

#endif
