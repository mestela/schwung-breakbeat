#include "slice_select.h"

int slice_select_next(const slice_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx) {
    int position = in->beat_position & 7;
    float complexity = in->complexity;
    float anchor = in->anchors[position];
    if (complexity < 0.0f) complexity = 0.0f;
    if (complexity > 1.0f) complexity = 1.0f;
    if (anchor < 0.0f) anchor = 0.0f;
    if (anchor > 1.0f) anchor = 1.0f;

    /* Complexity proposes a different slice. This position's anchor rejects
     * that proposal with its own probability. */
    if (rand_fn(rand_ctx) >= complexity || rand_fn(rand_ctx) < anchor)
        return position;
    int other = (int)(rand_fn(rand_ctx) * 7.0f);
    if (other < 0) other = 0;
    if (other > 6) other = 6;
    return other >= position ? other + 1 : other;
}
