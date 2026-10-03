#ifndef SLICE_SELECT_H
#define SLICE_SELECT_H

typedef struct {
    int beat_position;       /* 0..7: current grid position */
    float complexity;        /* 0..1: chance of replacing the natural slice */
    float anchors[8];        /* 0..1: chance of keeping the natural slice */
} slice_inputs_t;

typedef float (*slice_rand_fn)(void *ctx);
int slice_select_next(const slice_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx);

#endif
