#include "slice_select.h"

float slice_select_weight_at(int slice_idx, float anchor) {
    static const float locked[8] = {0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f};
    if (slice_idx < 0) slice_idx = 0;
    if (slice_idx > 7) slice_idx = 7;
    if (anchor < 0.0f) anchor = 0.0f;
    if (anchor > 1.0f) anchor = 1.0f;
    return 1.0f * (1.0f - anchor) + locked[slice_idx] * anchor;
}

void slice_select_apply_phrase(const slice_inputs_t *in,
                                float *out_complexity,
                                float *out_anchor,
                                float *out_roll) {
    *out_complexity = in->complexity;
    *out_anchor = in->anchor;
    *out_roll = in->roll;
}

int slice_select_next(const slice_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx) {
    (void)in; (void)rand_fn; (void)rand_ctx;
    return 0;
}
