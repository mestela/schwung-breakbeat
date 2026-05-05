#include "slice_select.h"

float slice_select_weight_at(int slice_idx, float anchor) {
    (void)slice_idx; (void)anchor;
    return 1.0f;
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
