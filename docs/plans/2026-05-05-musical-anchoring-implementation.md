# Musical Anchoring Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace breakbeat's pure-uniform random slice selection with anchored, sticky, phrase-modulated stochastic generation. Adds Anchor, Roll, Phrase, Fill knobs to a single-shared-library aarch64 plugin (`dsp.so`) deployed to Ableton Move.

**Architecture:** All slice-selection logic moves into pure C functions in `src/dsp/slice_select.c` / `slice_select.h` with no dependencies on host APIs or globals. These functions are unit-tested on host (macOS/linux native) via a small test harness in `tests/test_slice_select.c` that compiles with `gcc` and runs as a normal binary. The aarch64 plugin (`breakbeat.c`) calls into the same slice_select code. Plugin glue (param parsing, MIDI clock handling, JSON state) is updated in `breakbeat.c` directly without TDD because it's mechanical wiring.

**Tech Stack:**
- C99 plugin code, GCC cross-compiled for aarch64-linux-gnu
- Docker-based build (model: `schwung-gate/scripts/build.sh`)
- Vendored `plugin_api_v1.h` in `src/dsp/` (copy from `schwung-keydetect`)
- Native test binary on macOS via `clang` or `gcc` for slice_select tests

**Reference design doc:** `docs/plans/2026-05-05-musical-anchoring-design.md` — read this first.

---

## Phase 0: Build Infrastructure

The breakbeat repo currently has *no build script*. Add one before changing any code so we can verify the existing module still builds.

### Task 0.1: Vendor plugin_api_v1.h

**Files:**
- Create: `src/dsp/plugin_api_v1.h` (copy from `/Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-keydetect/src/dsp/plugin_api_v1.h`)

**Step 1: Copy the header**

```bash
cp /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-keydetect/src/dsp/plugin_api_v1.h \
   /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-breakbeat/src/dsp/plugin_api_v1.h
```

**Step 2: Update the include in breakbeat.c**

Change line 8 in `src/dsp/breakbeat.c`:

```c
// Before:
#include "host/plugin_api_v1.h"
// After:
#include "plugin_api_v1.h"
```

**Step 3: Commit**

```bash
git add src/dsp/plugin_api_v1.h src/dsp/breakbeat.c
git commit -m "build: vendor plugin_api_v1.h alongside breakbeat.c"
```

### Task 0.2: Add Dockerfile

**Files:**
- Create: `scripts/Dockerfile`

**Step 1: Write Dockerfile**

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    make \
    && rm -rf /var/lib/apt/lists/*

ENV CROSS_PREFIX=aarch64-linux-gnu-
WORKDIR /build
```

**Step 2: Commit**

```bash
git add scripts/Dockerfile
git commit -m "build: add Dockerfile for aarch64 cross-compile"
```

### Task 0.3: Add build.sh

**Files:**
- Create: `scripts/build.sh`

**Step 1: Write build.sh** (model: `schwung-gate/scripts/build.sh`)

```bash
#!/usr/bin/env bash
# Build breakbeat module for Move (aarch64).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

IMAGE_NAME="schwung-breakbeat-builder"
MODULE_ID="breakbeat"
DIST_DIR="dist/${MODULE_ID}"
TARBALL="dist/${MODULE_ID}-module.tar.gz"

if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== breakbeat build (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
CC="${CC:-${CROSS_PREFIX}gcc}"

rm -rf dist build
mkdir -p "$DIST_DIR" build

echo "Compiling DSP..."
"$CC" -Ofast -shared -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -fomit-frame-pointer -fno-stack-protector \
    -DNDEBUG \
    -Isrc/dsp \
    src/dsp/breakbeat.c \
    src/dsp/slice_select.c \
    -o build/dsp.so \
    -lm

echo "Packaging..."
cat build/dsp.so > "$DIST_DIR/dsp.so"
chmod 0755 "$DIST_DIR/dsp.so"
cat src/module.json > "$DIST_DIR/module.json"
cat src/ui.js > "$DIST_DIR/ui.js"

# Bundle samples too (the module loads them at runtime from its install dir)
mkdir -p "$DIST_DIR/samples"
for f in samples/*.wav; do
    cat "$f" > "$DIST_DIR/samples/$(basename "$f")"
done

(cd dist && tar -czf "${MODULE_ID}-module.tar.gz" "${MODULE_ID}/")

echo "Built: $TARBALL"
ls -lh "$TARBALL"
file "$DIST_DIR/dsp.so"
```

**Step 2: Make executable**

```bash
chmod +x scripts/build.sh
```

**Step 3: Note** — `slice_select.c` doesn't exist yet. The build will fail at this stage. That's fine — we're committing the script now and adding the file in Task 1.1.

**Step 4: Commit**

```bash
git add scripts/build.sh
git commit -m "build: add build.sh (slice_select.c stub follows)"
```

### Task 0.4: Add install.sh

**Files:**
- Create: `scripts/install.sh`

**Step 1: Write install.sh**

```bash
#!/usr/bin/env bash
# Install breakbeat module to Move.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

MODULE_ID="breakbeat"
TARGET="/data/UserData/schwung/modules/sound_generators/${MODULE_ID}"

if [ ! -d "dist/${MODULE_ID}" ]; then
    echo "Error: dist/${MODULE_ID} not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing breakbeat module ==="
ssh ableton@move.local "mkdir -p ${TARGET}"
scp -r "dist/${MODULE_ID}"/* "ableton@move.local:${TARGET}/"
ssh ableton@move.local "chmod -R a+rw ${TARGET}"

echo ""
echo "Installed to: ${TARGET}"
echo "Restart Schwung to load the new module."
```

**Step 2: Make executable**

```bash
chmod +x scripts/install.sh
```

**Step 3: Commit**

```bash
git add scripts/install.sh
git commit -m "build: add install.sh"
```

---

## Phase 1: Extract slice_select as a testable module

The slice-selection algorithm is the heart of this work. Move it into a pure C file with no host dependencies, then unit-test it on host before wiring it into the plugin.

### Task 1.1: Create slice_select.h with public API

**Files:**
- Create: `src/dsp/slice_select.h`

**Step 1: Write the header**

```c
#ifndef SLICE_SELECT_H
#define SLICE_SELECT_H

#include <stdint.h>

/* Inputs to a single trigger-tick slice decision. */
typedef struct {
    int   current_slice;    /* 0..7 */
    int   beat_position;    /* 0..7, where this tick lands in a 4/4 bar */
    float complexity;       /* 0..1 */
    float anchor;           /* 0..1 */
    float roll;             /* 0..1 */
    int   phrase_bars;      /* 0=Off, 2, 4, 8, 16 */
    float fill;             /* 0..1; only used when phrase_bars > 0 */
    int   bar_in_phrase;    /* 0..(phrase_bars-1); ignored if phrase_bars == 0 */
} slice_inputs_t;

/* Random source: returns float in [0, 1). Implementation provided by caller
 * (production = libc rand(); tests = deterministic seeded PRNG). */
typedef float (*slice_rand_fn)(void *ctx);

/* Picks the next slice index (0..7) given inputs and a random source. */
int slice_select_next(const slice_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx);

/* Compute per-slice swap-probability multiplier. Exposed for testing. */
float slice_select_weight_at(int slice_idx, float anchor);

/* Apply phrase modulation to params. Exposed for testing. */
void slice_select_apply_phrase(const slice_inputs_t *in,
                                float *out_complexity,
                                float *out_anchor,
                                float *out_roll);

/* Escape-hatch probability constant (5%). Exposed so tests can reason about it. */
#define SLICE_SELECT_ESCAPE_P 0.05f

#endif
```

**Step 2: Commit**

```bash
git add src/dsp/slice_select.h
git commit -m "feat: add slice_select.h public API"
```

### Task 1.2: Create slice_select.c stub that compiles

**Files:**
- Create: `src/dsp/slice_select.c`

**Step 1: Write minimal stub**

```c
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
```

**Step 2: Verify build**

```bash
./scripts/build.sh
```

Expected: `Built: dist/breakbeat-module.tar.gz`. The plugin still works (plugin uses old code path; `slice_select_*` is unused by `breakbeat.c` yet).

**Step 3: Commit**

```bash
git add src/dsp/slice_select.c
git commit -m "feat: add slice_select.c stub (compiles but unused)"
```

### Task 1.3: Add test harness scaffolding

**Files:**
- Create: `tests/test_slice_select.c`
- Create: `tests/run_tests.sh`

**Step 1: Write test_slice_select.c with empty harness**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/dsp/slice_select.h"

/* Deterministic seeded PRNG for tests. xorshift32. */
typedef struct { uint32_t state; } test_rng_t;

static float test_rand(void *ctx) {
    test_rng_t *r = (test_rng_t *)ctx;
    uint32_t x = r->state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->state = x;
    return (float)(x & 0x00FFFFFF) / (float)0x01000000;
}

static int g_pass = 0, g_fail = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (cond) { g_pass++; } else { \
        g_fail++; printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps, msg) do { \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) <= (eps)) { g_pass++; } else { \
        g_fail++; printf("FAIL: %s — got %f, expected %f (%s:%d)\n", msg, _a, _b, __FILE__, __LINE__); \
    } \
} while (0)

#include <math.h>

int main(void) {
    /* Tests will be added below. */

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
```

**Step 2: Write run_tests.sh**

```bash
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

cc -Wall -Wextra -O0 -g -std=c99 \
    -Isrc/dsp \
    tests/test_slice_select.c src/dsp/slice_select.c \
    -lm \
    -o tests/test_slice_select

./tests/test_slice_select
```

**Step 3: Make runnable & verify it runs**

```bash
chmod +x tests/run_tests.sh
./tests/run_tests.sh
```

Expected: `0 passed, 0 failed` (no tests yet).

**Step 4: Commit**

```bash
git add tests/test_slice_select.c tests/run_tests.sh
git commit -m "test: add slice_select test harness scaffolding"
```

---

## Phase 2: Weight curve (Anchor)

### Task 2.1: TDD — weight at Anchor=0 is uniform 1.0

**Files:**
- Modify: `tests/test_slice_select.c` (add test in main())

**Step 1: Write the failing test** — insert before the print line in main():

```c
/* === weight_at: Anchor=0 → uniform 1.0 === */
for (int i = 0; i < 8; i++) {
    float w = slice_select_weight_at(i, 0.0f);
    ASSERT_NEAR(w, 1.0f, 1e-5f, "weight_at(i, 0) should be 1.0");
}
```

**Step 2: Run** → passes (stub already returns 1.0).

```bash
./tests/run_tests.sh
```

Expected: `8 passed, 0 failed`.

**Step 3: No commit yet** — test is trivial; bundle with the next ones.

### Task 2.2: TDD — weight at Anchor=1 follows the locked curve

**Files:**
- Modify: `tests/test_slice_select.c`

**Step 1: Write the failing test**

```c
/* === weight_at: Anchor=1 → locked curve === */
{
    static const float expected[8] = {0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f};
    for (int i = 0; i < 8; i++) {
        float w = slice_select_weight_at(i, 1.0f);
        ASSERT_NEAR(w, expected[i], 1e-5f, "weight_at(i, 1) should match locked curve");
    }
}
```

**Step 2: Run** → fails (stub returns 1.0 for everything).

**Step 3: Implement** — replace `slice_select_weight_at` in `src/dsp/slice_select.c`:

```c
float slice_select_weight_at(int slice_idx, float anchor) {
    static const float locked[8] = {0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f};
    if (slice_idx < 0) slice_idx = 0;
    if (slice_idx > 7) slice_idx = 7;
    if (anchor < 0.0f) anchor = 0.0f;
    if (anchor > 1.0f) anchor = 1.0f;
    return 1.0f * (1.0f - anchor) + locked[slice_idx] * anchor;
}
```

**Step 4: Run** → expect all weight tests passing.

**Step 5: TDD — interpolation midpoint**

Add:

```c
/* === weight_at: Anchor=0.5 → halfway between 1.0 and locked === */
{
    static const float locked[8] = {0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f};
    for (int i = 0; i < 8; i++) {
        float w = slice_select_weight_at(i, 0.5f);
        float expected = 0.5f * 1.0f + 0.5f * locked[i];
        ASSERT_NEAR(w, expected, 1e-5f, "weight_at(i, 0.5) interpolation");
    }
}
```

Run → all pass.

**Step 6: Commit**

```bash
git add tests/test_slice_select.c src/dsp/slice_select.c
git commit -m "feat: implement slice_select_weight_at with anchor curve"
```

---

## Phase 3: Phrase modulation

### Task 3.1: TDD — phrase off, no modulation

**Files:**
- Modify: `tests/test_slice_select.c`

**Step 1: Write test**

```c
/* === apply_phrase: phrase_bars=0 → no modulation === */
{
    slice_inputs_t in = {0};
    in.complexity = 0.5f; in.anchor = 0.7f; in.roll = 0.3f;
    in.phrase_bars = 0; in.fill = 1.0f; in.bar_in_phrase = 0;

    float c, a, r;
    slice_select_apply_phrase(&in, &c, &a, &r);
    ASSERT_NEAR(c, 0.5f, 1e-5f, "phrase off: complexity unchanged");
    ASSERT_NEAR(a, 0.7f, 1e-5f, "phrase off: anchor unchanged");
    ASSERT_NEAR(r, 0.3f, 1e-5f, "phrase off: roll unchanged");
}
```

**Step 2: Run** → passes (stub passes through).

### Task 3.2: TDD — non-fill bar of phrase, no modulation

**Step 1: Add test**

```c
/* === apply_phrase: bar 0 of 4-bar phrase → no modulation === */
{
    slice_inputs_t in = {0};
    in.complexity = 0.5f; in.anchor = 0.7f; in.roll = 0.3f;
    in.phrase_bars = 4; in.fill = 1.0f; in.bar_in_phrase = 0;

    float c, a, r;
    slice_select_apply_phrase(&in, &c, &a, &r);
    ASSERT_NEAR(c, 0.5f, 1e-5f, "non-fill bar: complexity unchanged");
    ASSERT_NEAR(a, 0.7f, 1e-5f, "non-fill bar: anchor unchanged");
    ASSERT_NEAR(r, 0.3f, 1e-5f, "non-fill bar: roll unchanged");
}
```

**Step 2: Run** → passes.

### Task 3.3: TDD — fill bar with Fill=1 zeroes Roll and Anchor; max Complexity

**Step 1: Add test**

```c
/* === apply_phrase: fill bar (bar 3 of 4) with Fill=1 === */
{
    slice_inputs_t in = {0};
    in.complexity = 0.5f; in.anchor = 0.7f; in.roll = 0.3f;
    in.phrase_bars = 4; in.fill = 1.0f; in.bar_in_phrase = 3;

    float c, a, r;
    slice_select_apply_phrase(&in, &c, &a, &r);
    ASSERT_NEAR(c, 1.0f, 1e-5f, "fill=1: complexity → 1.0");
    ASSERT_NEAR(a, 0.0f, 1e-5f, "fill=1: anchor → 0");
    ASSERT_NEAR(r, 0.0f, 1e-5f, "fill=1: roll → 0");
}
```

**Step 2: Run** → fails (stub passes through).

**Step 3: Implement** — replace `slice_select_apply_phrase`:

```c
void slice_select_apply_phrase(const slice_inputs_t *in,
                                float *out_complexity,
                                float *out_anchor,
                                float *out_roll) {
    *out_complexity = in->complexity;
    *out_anchor = in->anchor;
    *out_roll = in->roll;

    if (in->phrase_bars <= 0) return;
    int is_fill = (in->bar_in_phrase == in->phrase_bars - 1);
    if (!is_fill) return;

    float f = in->fill;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;

    *out_complexity = in->complexity + (1.0f - in->complexity) * f;
    *out_anchor     = in->anchor * (1.0f - f);
    *out_roll       = in->roll   * (1.0f - f);
}
```

**Step 4: Run** → all phrase tests pass.

### Task 3.4: TDD — fill bar with Fill=0.5 (partial modulation)

**Step 1: Add test**

```c
/* === apply_phrase: fill bar with Fill=0.5 → halfway === */
{
    slice_inputs_t in = {0};
    in.complexity = 0.5f; in.anchor = 0.8f; in.roll = 0.6f;
    in.phrase_bars = 4; in.fill = 0.5f; in.bar_in_phrase = 3;

    float c, a, r;
    slice_select_apply_phrase(&in, &c, &a, &r);
    ASSERT_NEAR(c, 0.75f, 1e-5f, "fill=0.5: complexity halfway to 1.0");
    ASSERT_NEAR(a, 0.4f,  1e-5f, "fill=0.5: anchor halved");
    ASSERT_NEAR(r, 0.3f,  1e-5f, "fill=0.5: roll halved");
}
```

**Step 2: Run** → passes.

**Step 3: Commit**

```bash
git add tests/test_slice_select.c src/dsp/slice_select.c
git commit -m "feat: implement slice_select_apply_phrase"
```

---

## Phase 4: Slice selection (move & stay branches, escape hatch)

### Task 4.1: TDD — Roll=0, Anchor=0, Complexity=0 → sequential advance

**Step 1: Add test**

```c
/* === select_next: Roll=0, Anchor=0, Complexity=0 → sequential ===
 * With Complexity=0, move-branch always falls through to (current+1)%8.
 * No randomness should affect outcome. */
{
    test_rng_t rng = { .state = 12345 };
    slice_inputs_t in = {0};
    in.current_slice = 3;
    in.beat_position = 3;
    in.complexity = 0.0f; in.anchor = 0.0f; in.roll = 0.0f;
    in.phrase_bars = 0;

    int next = slice_select_next(&in, test_rand, &rng);
    ASSERT_TRUE(next == 4, "sequential advance from 3 → 4");

    in.current_slice = 7;
    next = slice_select_next(&in, test_rand, &rng);
    ASSERT_TRUE(next == 0, "sequential advance wraps 7 → 0");
}
```

**Step 2: Run** → fails (stub returns 0 always).

**Step 3: Implement minimal `slice_select_next` covering the move-only path**

In `src/dsp/slice_select.c`, replace the function:

```c
int slice_select_next(const slice_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx) {
    float eff_complexity, eff_anchor, eff_roll;
    slice_select_apply_phrase(in, &eff_complexity, &eff_anchor, &eff_roll);

    float r = rand_fn(rand_ctx);
    if (r < (1.0f - eff_roll)) {
        /* MOVE branch */
        float p_swap = eff_complexity * slice_select_weight_at(in->beat_position, eff_anchor);
        if (rand_fn(rand_ctx) < p_swap) {
            /* random jump (1..7 since rand returns [0,1) and *8 floors) */
            int j = (int)(rand_fn(rand_ctx) * 8.0f);
            if (j < 0) j = 0; if (j > 7) j = 7;
            return j;
        }
        return (in->current_slice + 1) & 7;
    }
    /* STAY branch — implement in next task */
    return in->current_slice;
}
```

**Step 4: Run** → sequential test passes.

### Task 4.2: TDD — Roll=1, Anchor=1, current_slice=0 → repeat (kick anchor)

**Step 1: Add test**

```c
/* === select_next: Roll=1, Anchor=1, current=0 → must stay on 0 ===
 * Roll=1 forces stay branch. Anchor=1 + slice 0 → weight=0 → p_repeat=1.
 * Escape hatch fires only 5% of the time, but the *test* uses many iterations
 * and asserts that ≥80% of outcomes are repeat-0. */
{
    test_rng_t rng = { .state = 0xdeadbeef };
    slice_inputs_t in = {0};
    in.current_slice = 0;
    in.beat_position = 0;
    in.complexity = 0.5f; in.anchor = 1.0f; in.roll = 1.0f;
    in.phrase_bars = 0;

    int repeat_count = 0;
    const int N = 2000;
    for (int i = 0; i < N; i++) {
        int next = slice_select_next(&in, test_rand, &rng);
        if (next == 0) repeat_count++;
    }
    /* Expect: ~95% of stay-branch decisions repeat slice 0
     * (5% escape hatch + ~50% of the non-escape walks land on slice 0 by symmetry — actually no,
     *  walk goes ±1, never lands on 0 from slice 0 since both ±1 of 0 are ≠ 0. So ~95%.) */
    ASSERT_TRUE(repeat_count >= (int)(N * 0.85), "Roll=1 Anchor=1 slice=0: should repeat heavily");
}
```

**Step 2: Run** → likely fails (stay branch is `return in->current_slice` for now, which actually DOES repeat — but it doesn't model walk/escape, so this test passes trivially. We need a test that breaks the trivial implementation.)

Actually re-reading: stub returns `in->current_slice` for stay branch, so this test passes. We need a *different* test that the trivial-repeat impl fails. Add this instead:

```c
/* === select_next: Roll=1, Anchor=0, current=4 → walks should occur ===
 * Anchor=0 means weight=1 for all slices, so p_repeat = 1 - 1 = 0.
 * Escape hatch takes 5%; remaining 95% should be ±1 walks (slice 3 or 5).
 * Slice 4 should NOT dominate — repeat probability is only escape-hatch driven. */
{
    test_rng_t rng = { .state = 0x1234abcd };
    slice_inputs_t in = {0};
    in.current_slice = 4;
    in.beat_position = 4;
    in.complexity = 0.5f; in.anchor = 0.0f; in.roll = 1.0f;
    in.phrase_bars = 0;

    int repeat_count = 0, walk_neighbor = 0;
    const int N = 2000;
    for (int i = 0; i < N; i++) {
        int next = slice_select_next(&in, test_rand, &rng);
        if (next == 4) repeat_count++;
        if (next == 3 || next == 5) walk_neighbor++;
    }
    /* Repeat should be small (escape hatch can land on 4? No — escape jumps by 2..4.
     * From 4: +2→6, +3→7, +4→0. None are 4. So repeats should be 0 or near-zero. */
    ASSERT_TRUE(repeat_count < (int)(N * 0.05), "Roll=1 Anchor=0: walk dominates, repeat near zero");
    ASSERT_TRUE(walk_neighbor > (int)(N * 0.80), "Roll=1 Anchor=0: ≥80% are ±1 walks");
}
```

**Step 3: Run** → fails (current stay branch returns current_slice always).

**Step 4: Implement stay branch with escape hatch and walk** — replace stay branch in `slice_select_next`:

```c
    /* STAY branch */
    if (rand_fn(rand_ctx) < SLICE_SELECT_ESCAPE_P) {
        /* Escape: jump 2..4 forward */
        int j = 2 + (int)(rand_fn(rand_ctx) * 3.0f);
        if (j < 2) j = 2; if (j > 4) j = 4;
        return (in->current_slice + j) & 7;
    }
    {
        float w_here = slice_select_weight_at(in->current_slice, eff_anchor);
        float p_repeat = 1.0f - w_here;
        if (rand_fn(rand_ctx) < p_repeat) {
            return in->current_slice;
        }
        /* walk ±1 */
        int dir = (rand_fn(rand_ctx) < 0.5f) ? 1 : 7;  /* 7 == -1 mod 8 */
        return (in->current_slice + dir) & 7;
    }
```

**Step 5: Run** → both selection tests pass.

### Task 4.3: TDD — Roll=1, Anchor=1, slice=0 — repeat dominates after fix

**Step 1: Add the earlier-deferred test**

```c
/* === select_next: Roll=1, Anchor=1, current=0 → repeat dominates === */
{
    test_rng_t rng = { .state = 0xfacefeed };
    slice_inputs_t in = {0};
    in.current_slice = 0;
    in.beat_position = 0;
    in.complexity = 0.5f; in.anchor = 1.0f; in.roll = 1.0f;
    in.phrase_bars = 0;

    int repeat_count = 0;
    const int N = 2000;
    for (int i = 0; i < N; i++) {
        int next = slice_select_next(&in, test_rand, &rng);
        if (next == 0) repeat_count++;
    }
    ASSERT_TRUE(repeat_count >= (int)(N * 0.80), "Roll=1 Anchor=1 slice=0: repeats heavily");
}
```

**Step 2: Run** → expect pass.

### Task 4.4: TDD — Roll=0, Anchor=0, Complexity=1 → uniform random across all 8 slices

**Step 1: Add test**

```c
/* === select_next: Roll=0, Anchor=0, Complexity=1 → uniform random ===
 * All slices visited; no slice >>> any other. */
{
    test_rng_t rng = { .state = 0x55aa55aa };
    slice_inputs_t in = {0};
    in.current_slice = 0;
    in.beat_position = 0;
    in.complexity = 1.0f; in.anchor = 0.0f; in.roll = 0.0f;
    in.phrase_bars = 0;

    int counts[8] = {0};
    const int N = 8000;
    for (int i = 0; i < N; i++) {
        int next = slice_select_next(&in, test_rand, &rng);
        ASSERT_TRUE(next >= 0 && next < 8, "slice in 0..7");
        counts[next]++;
    }
    /* Each bucket should land near N/8 = 1000. Allow ±25%. */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(counts[i] >= 750 && counts[i] <= 1250, "uniform distribution per slice");
    }
}
```

**Step 2: Run** → expect pass.

### Task 4.5: TDD — Phrase=4 fill bar releases anchor

**Step 1: Add test**

```c
/* === select_next: Phrase=4, bar_in_phrase=3, Fill=1 → ignores Anchor ===
 * Even with Anchor=1 + Roll=1 + slice=0, fill bar should produce non-zero
 * variation because effective_anchor and effective_roll both go to 0. */
{
    test_rng_t rng = { .state = 0xc0ffee };
    slice_inputs_t in = {0};
    in.current_slice = 0;
    in.beat_position = 0;
    in.complexity = 1.0f; in.anchor = 1.0f; in.roll = 1.0f;
    in.phrase_bars = 4; in.fill = 1.0f; in.bar_in_phrase = 3;

    int repeat_count = 0;
    const int N = 2000;
    for (int i = 0; i < N; i++) {
        int next = slice_select_next(&in, test_rand, &rng);
        if (next == 0) repeat_count++;
    }
    /* On fill bar, Roll=0 so move-branch dominates. With Complexity=1 and Anchor=0,
     * move branch always rolls uniform 0..7. Slice 0 lands ~12.5% of the time. */
    ASSERT_TRUE(repeat_count <= (int)(N * 0.20), "fill bar: anchor lock released");
}
```

**Step 2: Run** → expect pass (logic already correct because apply_phrase is called inside select_next).

**Step 3: Commit**

```bash
git add tests/test_slice_select.c src/dsp/slice_select.c
git commit -m "feat: implement slice_select_next with move/stay branches and escape hatch"
```

---

## Phase 5: Wire slice_select into breakbeat.c

### Task 5.1: Add new fields to breakbeat_t

**Files:**
- Modify: `src/dsp/breakbeat.c:14-48`

**Step 1: Update the struct**

In the `breakbeat_t` struct definition, add after `complexity`:

```c
    float anchor;
    float roll;
    int   phrase_bars;
    float fill;
    int   bar_counter;       /* increments per bar; resets on transport start */
    int   reseed_pending;
```

**Step 2: Initialize in `bb_create_instance`**

After `bb->complexity = 0.5f;`, add:

```c
    bb->anchor = 0.0f;
    bb->roll = 0.0f;
    bb->phrase_bars = 0;
    bb->fill = 0.0f;
    bb->bar_counter = 0;
    bb->reseed_pending = 0;
```

**Step 3: Build (don't install yet)**

```bash
./scripts/build.sh
```

Expected: builds clean.

**Step 4: Commit**

```bash
git add src/dsp/breakbeat.c
git commit -m "feat: add anchor/roll/phrase_bars/fill fields to breakbeat_t"
```

### Task 5.2: Replace slice-selection block with slice_select_next

**Files:**
- Modify: `src/dsp/breakbeat.c` — top of file (add include) and `bb_on_midi` (replace selection)

**Step 1: Add include**

Near the top, after the existing `#include "plugin_api_v1.h"`:

```c
#include "slice_select.h"
#include <time.h>
```

**Step 2: Add a libc-rand wrapper**

After `static const char *g_loop_names[]` (around line 70), add:

```c
static float bb_rand(void *ctx) {
    (void)ctx;
    return (float)rand() / (float)RAND_MAX;
}
```

**Step 3: Replace selection block in `bb_on_midi`**

Find the block `if (bb->clock_counter % trigger_clocks == 0) { ... }` (around line 297). Replace its body with:

```c
        if (bb->clock_counter % trigger_clocks == 0) {
            /* Compute beat_position: which 1/8 of the bar this trigger lands on. */
            int trigger_in_bar = (bb->clock_counter % 96) / trigger_clocks;
            int triggers_per_bar = 96 / trigger_clocks;
            if (triggers_per_bar < 1) triggers_per_bar = 1;
            int beat_position = (int)((float)trigger_in_bar * (8.0f / (float)triggers_per_bar)) & 7;

            slice_inputs_t in = {
                .current_slice = bb->current_slice,
                .beat_position = beat_position,
                .complexity    = bb->complexity,
                .anchor        = bb->anchor,
                .roll          = bb->roll,
                .phrase_bars   = bb->phrase_bars,
                .fill          = bb->fill,
                .bar_in_phrase = bb->phrase_bars > 0 ? bb->bar_counter % bb->phrase_bars : 0,
            };
            bb->current_slice = slice_select_next(&in, bb_rand, NULL);

            /* Roll for retrigger (sub-slice) — unchanged */
            bb->sub_slice_counter = 0;
            if ((float)rand() / (float)RAND_MAX < bb->retrigger_prob) {
                bb->sub_slice_active = 1;
            } else {
                bb->sub_slice_active = 0;
            }

            bb->play_pos = bb->slice_starts[bb->current_slice];
        }
```

Also DELETE the existing line `if (bb->clock_counter % 384 == 0) { bb->current_slice = 0; }` block (around line 302) and the `else if (r < comp)` / `else` randomization block.

**Step 4: Add bar_counter increment**

Inside the same MIDI clock handling block, after `bb->clock_counter++;` (around line 273), add:

```c
        if (bb->clock_counter % 96 == 0 && bb->clock_counter > 0) {
            bb->bar_counter++;
        }
```

**Step 5: Build**

```bash
./scripts/build.sh
```

Expected: builds clean.

**Step 6: Commit**

```bash
git add src/dsp/breakbeat.c
git commit -m "feat: wire breakbeat.c selection into slice_select_next"
```

### Task 5.3: Add transport-start reseed

**Files:**
- Modify: `src/dsp/breakbeat.c` — in `bb_render_block`

**Step 1: Add reseed when transport transitions to running**

In `bb_render_block`, find the transport-status block (around line 571):

```c
if (g_host && g_host->get_clock_status) {
    if (g_host->get_clock_status() == 2) {
        bb->playing = 1;
    } else {
        bb->playing = 0;
        bb->play_pos = 0;
        bb->clock_counter = 0;
        bb->current_slice = 0;
    }
}
```

Replace with:

```c
if (g_host && g_host->get_clock_status) {
    int running = (g_host->get_clock_status() == 2);
    if (running && !bb->playing) {
        /* Transport just started → reseed */
        srand((unsigned int)(time(NULL) ^ bb->sample_counter));
        bb->bar_counter = 0;
    }
    if (running) {
        bb->playing = 1;
    } else {
        bb->playing = 0;
        bb->play_pos = 0;
        bb->clock_counter = 0;
        bb->current_slice = 0;
        bb->bar_counter = 0;
    }
}
```

**Step 2: Build**

```bash
./scripts/build.sh
```

Expected: builds clean.

**Step 3: Commit**

```bash
git add src/dsp/breakbeat.c
git commit -m "feat: reseed RNG on transport start"
```

---

## Phase 6: Param plumbing for new knobs

### Task 6.1: Add anchor/roll/phrase/fill to set_param

**Files:**
- Modify: `src/dsp/breakbeat.c` — `bb_set_param` (around lines 348-488)

**Step 1: Add handlers** — after the `complexity` handler (before `retrigger`), add:

```c
    else if (strcmp(key, "anchor") == 0) {
        bb->anchor = atof(val) / 100.0f;
        if (bb->anchor < 0.0f) bb->anchor = 0.0f;
        if (bb->anchor > 1.0f) bb->anchor = 1.0f;
    }
    else if (strcmp(key, "roll") == 0) {
        bb->roll = atof(val) / 100.0f;
        if (bb->roll < 0.0f) bb->roll = 0.0f;
        if (bb->roll > 1.0f) bb->roll = 1.0f;
    }
    else if (strcmp(key, "phrase") == 0) {
        /* enum: 0=Off, 1=2bars, 2=4bars, 3=8bars, 4=16bars */
        int idx = atoi(val);
        static const int phrase_values[] = {0, 2, 4, 8, 16};
        if (idx < 0) idx = 0;
        if (idx > 4) idx = 4;
        bb->phrase_bars = phrase_values[idx];
    }
    else if (strcmp(key, "fill") == 0) {
        bb->fill = atof(val) / 100.0f;
        if (bb->fill < 0.0f) bb->fill = 0.0f;
        if (bb->fill > 1.0f) bb->fill = 1.0f;
    }
```

**Step 2: Build**

```bash
./scripts/build.sh
```

**Step 3: Commit**

```bash
git add src/dsp/breakbeat.c
git commit -m "feat: handle anchor/roll/phrase/fill in set_param"
```

### Task 6.2: Add to get_param

**Files:**
- Modify: `src/dsp/breakbeat.c` — `bb_get_param`

**Step 1: Add cases** — after the `complexity` get-case:

```c
    else if (strcmp(key, "anchor") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->anchor * 100.0f));
    }
    else if (strcmp(key, "roll") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->roll * 100.0f));
    }
    else if (strcmp(key, "phrase") == 0) {
        int idx = 0;
        if (bb->phrase_bars == 2) idx = 1;
        else if (bb->phrase_bars == 4) idx = 2;
        else if (bb->phrase_bars == 8) idx = 3;
        else if (bb->phrase_bars == 16) idx = 4;
        return snprintf(buf, buf_len, "%d", idx);
    }
    else if (strcmp(key, "fill") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->fill * 100.0f));
    }
```

**Step 2: Build**

**Step 3: Commit**

```bash
git add src/dsp/breakbeat.c
git commit -m "feat: handle anchor/roll/phrase/fill in get_param"
```

### Task 6.3: Update ui_hierarchy and chain_params JSON in get_param

**Files:**
- Modify: `src/dsp/breakbeat.c` — the `ui_hierarchy` and `chain_params` strings

**Step 1: Replace the `ui_hierarchy` string** in `bb_get_param`:

```c
    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hierarchy = "{\"modes\":null,\"levels\":{\"root\":{\"list_param\":\"preset\",\"count_param\":\"preset_count\",\"name_param\":\"preset_name\",\"knobs\":[\"preset\",\"loop\",\"length\",\"phrase\",\"complexity\",\"anchor\",\"roll\",\"fill\",\"retrigger\",\"retrigger_rate\",\"save_preset\"],\"params\":[{\"key\":\"preset\",\"label\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":10},{\"key\":\"loop\",\"label\":\"Loop\",\"type\":\"enum\",\"options\":[\"amen01\",\"amen09\",\"amen18\",\"amen19\",\"amen20\",\"apache\",\"do\",\"eeloil\",\"fireeater\",\"funkydrummer\",\"groove\",\"hungup_0\",\"king\",\"kool\",\"mechanicalman\",\"neworleans\",\"riffin\",\"ripple\",\"sesame\",\"sport\",\"squib\",\"think\",\"useme\"]},{\"key\":\"length\",\"label\":\"Length\",\"type\":\"enum\",\"options\":[\"0.25\",\"0.5\",\"1\",\"2\",\"4\",\"8\"]},{\"key\":\"phrase\",\"label\":\"Phrase\",\"type\":\"enum\",\"options\":[\"Off\",\"2\",\"4\",\"8\",\"16\"]},{\"key\":\"complexity\",\"label\":\"Complexity\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"anchor\",\"label\":\"Anchor\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"roll\",\"label\":\"Roll\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"fill\",\"label\":\"Fill\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"retrigger\",\"label\":\"Retrigger\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"retrigger_rate\",\"label\":\"Retrig Rate\",\"type\":\"int\",\"min\":2,\"max\":4},{\"key\":\"save_preset\",\"label\":\"Save to Log\",\"type\":\"int\",\"min\":0,\"max\":1}]}}}";
        strncpy(buf, hierarchy, buf_len);
        return strlen(hierarchy);
    }
```

**Step 2: Replace the `chain_params` string**:

```c
    if (strcmp(key, "chain_params") == 0) {
        const char *json = "["
            "{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":10},"
            "{\"key\":\"loop\",\"name\":\"Loop\",\"type\":\"enum\",\"options\":[\"amen01\",\"amen09\",\"amen18\",\"amen19\",\"amen20\",\"apache\",\"do\",\"eeloil\",\"fireeater\",\"funkydrummer\",\"groove\",\"hungup_0\",\"king\",\"kool\",\"mechanicalman\",\"neworleans\",\"riffin\",\"ripple\",\"sesame\",\"sport\",\"squib\",\"think\",\"useme\"]},"
            "{\"key\":\"length\",\"name\":\"Length\",\"type\":\"enum\",\"options\":[\"0.25\",\"0.5\",\"1\",\"2\",\"4\",\"8\"]},"
            "{\"key\":\"phrase\",\"name\":\"Phrase\",\"type\":\"enum\",\"options\":[\"Off\",\"2\",\"4\",\"8\",\"16\"]},"
            "{\"key\":\"complexity\",\"name\":\"Complexity\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"anchor\",\"name\":\"Anchor\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"roll\",\"name\":\"Roll\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"fill\",\"name\":\"Fill\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"retrigger\",\"name\":\"Retrigger\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"retrigger_rate\",\"name\":\"Retrig Rate\",\"type\":\"int\",\"min\":2,\"max\":4},"
            "{\"key\":\"save_preset\",\"name\":\"Save to Log\",\"type\":\"int\",\"min\":0,\"max\":1}"
        "]";
        strncpy(buf, json, buf_len);
        return strlen(json);
    }
```

**Step 3: Build**

**Step 4: Commit**

```bash
git add src/dsp/breakbeat.c
git commit -m "feat: expose anchor/roll/phrase/fill in ui_hierarchy and chain_params"
```

### Task 6.4: Update module.json with new params

**Files:**
- Modify: `src/module.json`

**Step 1: Add new param entries** — after the `complexity` entry in `params` array:

```json
{
  "key": "anchor",
  "label": "Anchor",
  "type": "int",
  "min": 0,
  "max": 100,
  "default": 0
},
{
  "key": "roll",
  "label": "Roll",
  "type": "int",
  "min": 0,
  "max": 100,
  "default": 0
},
{
  "key": "phrase",
  "label": "Phrase",
  "type": "enum",
  "options": ["Off", "2", "4", "8", "16"],
  "default": 0
},
{
  "key": "fill",
  "label": "Fill",
  "type": "int",
  "min": 0,
  "max": 100,
  "default": 0
}
```

**Step 2: Update the `knobs` array**:

```json
"knobs": [
    "preset",
    "loop",
    "length",
    "phrase",
    "complexity",
    "anchor",
    "roll",
    "fill",
    "retrigger",
    "retrigger_rate"
]
```

**Step 3: Bump version**

Change `"version": "0.1.2"` → `"version": "0.2.0"`.

**Step 4: Build**

**Step 5: Commit**

```bash
git add src/module.json
git commit -m "feat: declare anchor/roll/phrase/fill in module.json (v0.2.0)"
```

### Task 6.5: Update preset_t and presets

**Files:**
- Modify: `src/dsp/breakbeat.c` — `preset_t` struct and `g_presets` array

**Step 1: Update `preset_t`**:

```c
typedef struct {
    char name[64];
    int loop_idx;
    float length;
    float complexity;
    float retrigger_prob;
    int retrigger_divisions;
    float anchor;
    float roll;
    int phrase_bars;
    float fill;
} preset_t;
```

**Step 2: Update `g_presets`**:

```c
static preset_t g_presets[] = {
    /* name, loop, length, cplx, retrig, ratediv, anchor, roll, phrase, fill */
    {"Calm",    0,  0.5f, 0.18f, 0.16f, 2,  0.80f, 0.70f, 4, 0.50f},
    {"Mid",     1,  0.5f, 0.56f, 0.08f, 4,  0.60f, 0.50f, 4, 0.60f},
    {"Frantic", 18, 0.25f, 0.80f, 0.11f, 4, 0.30f, 0.20f, 2, 0.80f}
};
```

**Step 3: Update preset application in `bb_set_param`** — find the `if (strcmp(key, "preset") == 0...)` block and add after the existing `bb->retrigger_divisions = p->retrigger_divisions;`:

```c
        bb->anchor = p->anchor;
        bb->roll = p->roll;
        bb->phrase_bars = p->phrase_bars;
        bb->fill = p->fill;
```

**Step 4: Update `save_preset` log line** to include new params:

```c
            int phrase_idx = 0;
            if (bb->phrase_bars == 2) phrase_idx = 1;
            else if (bb->phrase_bars == 4) phrase_idx = 2;
            else if (bb->phrase_bars == 8) phrase_idx = 3;
            else if (bb->phrase_bars == 16) phrase_idx = 4;

            snprintf(buf, sizeof(buf), "PRESET_DATA: {\"name\":\"%s\",\"loop\":%d,\"length\":%d,\"complexity\":%d,\"anchor\":%d,\"roll\":%d,\"phrase\":%d,\"fill\":%d,\"retrigger\":%d,\"retrigger_rate\":%d}",
                g_presets[bb->preset_idx].name, bb->loop_idx, len_idx,
                (int)(bb->complexity * 100.0f),
                (int)(bb->anchor * 100.0f),
                (int)(bb->roll * 100.0f),
                phrase_idx,
                (int)(bb->fill * 100.0f),
                (int)(bb->retrigger_prob * 100.0f),
                bb->retrigger_divisions);
```

**Step 5: Update `state` get/set in `bb_get_param` and `bb_set_param`** — add the four new fields to the JSON. In `bb_get_param`'s `state` case:

```c
        return snprintf(buf, buf_len, "{\"preset\":%d,\"loop\":%d,\"length\":%d,\"complexity\":%d,\"anchor\":%d,\"roll\":%d,\"phrase\":%d,\"fill\":%d,\"retrigger\":%d,\"retrigger_rate\":%d}",
            bb->preset_idx, bb->loop_idx, len_idx,
            (int)(bb->complexity * 100.0f),
            (int)(bb->anchor * 100.0f),
            (int)(bb->roll * 100.0f),
            phrase_idx,  /* compute as above */
            (int)(bb->fill * 100.0f),
            (int)(bb->retrigger_prob * 100.0f),
            bb->retrigger_divisions);
```

(Compute `phrase_idx` similarly to `len_idx` before the snprintf.)

In `bb_set_param`'s `state` case, after the existing `complexity` parsing, add:

```c
        if (json_get_string(val, "anchor", float_str, sizeof(float_str))) {
            bb->anchor = atof(float_str) / 100.0f;
        }
        if (json_get_string(val, "roll", float_str, sizeof(float_str))) {
            bb->roll = atof(float_str) / 100.0f;
        }
        if (json_get_string(val, "phrase", float_str, sizeof(float_str))) {
            int idx = atoi(float_str);
            static const int phrase_values[] = {0, 2, 4, 8, 16};
            if (idx < 0) idx = 0; if (idx > 4) idx = 4;
            bb->phrase_bars = phrase_values[idx];
        }
        if (json_get_string(val, "fill", float_str, sizeof(float_str))) {
            bb->fill = atof(float_str) / 100.0f;
        }
```

**Step 6: Build**

```bash
./scripts/build.sh
```

**Step 7: Commit**

```bash
git add src/dsp/breakbeat.c
git commit -m "feat: extend presets and state serialization with new params"
```

---

## Phase 7: Final verification

### Task 7.1: Confirm tests still pass

```bash
./tests/run_tests.sh
```

Expected: all assertions pass.

### Task 7.2: Build clean

```bash
rm -rf build dist
./scripts/build.sh
```

Expected: `Built: dist/breakbeat-module.tar.gz`. Sanity-check `file dist/breakbeat/dsp.so` reports `aarch64`.

### Task 7.3: Install on Move

```bash
./scripts/install.sh
```

Expected: scp succeeds. SSH out and restart Schwung manually on the device.

### Task 7.4: Manual test plan (on Move)

Per the design doc's "Testing approach" section. Run through these by ear:

- [ ] Anchor=0, Roll=0, Phrase=Off, Complexity=50, Loop=amen01: should sound like the old module — random slice every trigger, sometimes sequential.
- [ ] Anchor=100, Roll=0, Complexity=100, Phrase=Off: kick stays on beat 1 every bar despite max chaos elsewhere. Subjective: still grooves?
- [ ] Anchor=100, Roll=100, Complexity=50: heavy slice-0 stuttering. Periodically breaks out via escape hatch (every ~20 triggers on average). Not stuck.
- [ ] Anchor=80, Roll=70, Phrase=4, Fill=70: bars 1–3 groove, bar 4 audibly opens up, bar 1 of next phrase resets.
- [ ] Phrase=2, Fill=100: every other bar should feel wild.
- [ ] Transport stop → start: pattern is audibly different each run (reseed working).
- [ ] Cycle through all 23 loops at the Mid preset: each loop should still sound musical.

If anything sounds bad: take notes, the design doc's "Risks" section calls out which knob to turn down first.

### Task 7.5: Final commit + push

If everything sounds good:

```bash
git status
git push -u origin musical-anchoring
```

Open a PR or merge to `main` per project conventions.

---

## Notes for the implementer

- **The plan assumes the module's existing semantics.** If `clock_counter` is a 1/24-clock counter (24 ppq), then 96 = one bar. Verify by reading `bb_on_midi`. If different, adjust `bar_counter` increment.
- **Escape hatch sanity** — the 5% probability is tuned so that a sticky walk (Roll=1, Anchor=1) breaks out roughly every 20 triggers. At Length=0.5 (8th-note triggers, 8/bar), that's ~2.5 bars between escapes. If too rare or too frequent on Move, tune `SLICE_SELECT_ESCAPE_P` in `slice_select.h`.
- **Test isolation** — never call `rand()` from inside `slice_select.c`; always go through `rand_fn`. The plugin glue code (`breakbeat.c`) is the only place that touches libc `rand()`.
- **Don't TDD the JSON plumbing** — it's mechanical and obvious from the design doc. Verify by inspecting the running module's response to host queries on Move.
- **DRY** — if you find yourself duplicating the `phrase_bars → idx` mapping more than twice, extract a helper.
