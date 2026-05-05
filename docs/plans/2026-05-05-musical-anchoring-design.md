# Breakbeat: Musical Anchoring Design

Date: 2026-05-05

## Goal

Move the breakbeat module from "every trigger is an independent uniform-random dice roll" to a system that produces musically coherent breaks suitable for jungle / DnB / dance music, while preserving the current stochastic mode (set the new knobs to 0).

The musical target is the feel of weighted, sticky traversal with phrased fills — held repeats, neighbor walks, occasional excursions, and a "fill bar" that breaks the groove every N bars. Inspiration: Strudel/Tidal patterns like `< 4@3 4@5 4@3 4@1 3@2 6@2 >*8` and rolling motifs like `1 2 3 1 2 3 4 5`. The mechanism is *biased random with phrase-modulated parameters*, not stored sequences.

## Four new parameters

### Anchor (0–100)

Per-position swap-probability weighting. Biases *what* slice is picked when a new slice is chosen.

Internally, Anchor selects an interpolation point on a per-slice weight curve:

- At **Anchor = 0**: every slice position has multiplier `1.0`. Behavior is identical to today — Complexity rolls a uniform random slice.
- At **Anchor = 100**: beat-1 (slice 0) and beat-3 (slice 4) positions have multiplier `0.0` (never swap); off-beats and the last 16th have multipliers `>= 1.0`.
- Intermediate Anchor values linearly interpolate between the flat curve and the locked curve.

Reference curve at Anchor = 100:

```
slice index: 0    1    2    3    4    5    6    7
multiplier:  0.0  0.5  1.0  0.7  0.0  0.5  1.0  1.2
```

The multiplier scales `Complexity` per slice position.

### Roll (0–100)

Temporal stickiness. Biases *whether* the next pick is correlated with the current one.

- At **Roll = 0**: every trigger is an independent roll (today's behavior, modulated by Anchor).
- At **Roll = 100**: most triggers either repeat the current slice or walk by ±1; full random jumps are rare.

Roll produces the rolling jungle feel and Tidal-style stutter-then-shift. Anchor and Roll are orthogonal: Anchor says *what slices are home base*, Roll says *how successive picks correlate*.

### Phrase (enum: Off, 2, 4, 8, 16 bars)

Defines the multi-bar phrasing length. The *last bar* of every phrase is the "fill bar," where the parameters are modulated by Fill (below).

At `Phrase = Off`, no phrase modulation occurs — every bar is the same.

### Fill (0–100)

How hard the fill bar departs from the groove. Modulates three parameters during the fill bar:

- **Complexity ↑**: `effective_complexity = complexity + (1.0 - complexity) * fill`. At Fill=100, complexity is forced to 100% on the fill bar.
- **Roll ↓**: `effective_roll = roll * (1.0 - fill)`. At Fill=100, Roll drops to 0 on the fill bar — pure jumps, no walks.
- **Anchor ↓**: `effective_anchor = anchor * (1.0 - fill)`. At Fill=100, the kick/snare lock fully releases — kick can land anywhere on the fill bar.

When `Phrase = Off`, Fill has no effect.

## Selection logic per trigger tick

```c
static const float locked_curve[8] = {0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f};

float weight_at(int slice_idx, float anchor) {
    return 1.0f * (1.0f - anchor) + locked_curve[slice_idx] * anchor;
}

float frand(void) { return (float)rand() / (float)RAND_MAX; }

// Per trigger: compute effective parameters with phrase modulation
float eff_complexity = bb->complexity;
float eff_roll       = bb->roll;
float eff_anchor     = bb->anchor;

if (bb->phrase_bars > 0) {
    int bar_in_phrase = bb->bar_counter % bb->phrase_bars;
    int is_fill_bar = (bar_in_phrase == bb->phrase_bars - 1);
    if (is_fill_bar) {
        eff_complexity = bb->complexity + (1.0f - bb->complexity) * bb->fill;
        eff_roll       = bb->roll       * (1.0f - bb->fill);
        eff_anchor     = bb->anchor     * (1.0f - bb->fill);
    }
}

int next_slice;

if (frand() < (1.0f - eff_roll)) {
    // MOVE branch: anchored weighted random
    float p_swap = eff_complexity * weight_at(beat_position, eff_anchor);
    if (frand() < p_swap) {
        next_slice = rand() % 8;
    } else {
        next_slice = (bb->current_slice + 1) % 8;
    }
} else {
    // STAY branch: repeat, walk by ±1, or escape — tilted by anchor
    float w_here = weight_at(bb->current_slice, eff_anchor);

    // Escape hatch: small fixed probability of a longer jump even when "staying"
    const float ESCAPE_P = 0.05f;
    if (frand() < ESCAPE_P) {
        int jump = 2 + (rand() % 3);   // 2, 3, or 4
        next_slice = (bb->current_slice + jump) % 8;
    } else {
        float p_repeat = 1.0f - w_here;  // lock-zone slices repeat; fill-zone slices walk
        if (frand() < p_repeat) {
            next_slice = bb->current_slice;
        } else {
            next_slice = (bb->current_slice + ((rand() & 1) ? 1 : 7)) % 8;
        }
    }
}

bb->current_slice = next_slice;
```

Key behaviors that fall out:

- Roll = 0, Anchor = 0, Phrase = Off → exactly today's behavior.
- Roll = 0, Anchor = 100 → today's stochasticity but kick/snare locked on beats 1/3.
- Roll = 100, Anchor = 100 → camped on slice 0 mostly, with occasional ±1 walks and rare escape jumps.
- Roll = 100, Anchor = 0 → drum-machine local-walk feel across the whole loop.
- Phrase = 4, Fill = 70 → bars 1–3 hold groove, bar 4 breaks: more random, less sticky, kick unmoored.

## Behaviors that change

- **Remove the existing `clock_counter % 384` hard-reset to slice 0.** Anchor handles slice-0 grounding via the weight curve. At low Anchor, the user is opting into chaos; no hidden override.
- **Reseed on transport start.** Whenever the host clock transitions to running, call `srand()` with a fresh seed (e.g. `time(NULL) ^ sample_counter`). Today the module relies on libc's default seed.
- **Track bar_counter** for phrase modulation. Increments every 96 MIDI clocks (1 bar at 24 ppq). Resets to 0 on transport start.

## Behaviors that stay the same

- WAV slicing into 8 equal parts.
- MIDI clock-driven trigger interval, derived from `Length`.
- Pad-trigger note range (36–43 → slices 0–7).
- Sample-rate adjustment to fit `total_frames` into `16 * length` beats at host BPM.
- Auto-play / auto-stop on transport.
- Single sample at a time (the `Loop` knob). No loop rotation.
- Retriggers stay live and stochastic in all modes.

## UI parameter order and roles

Parameters split into three roles based on how they're used:

**Performance knobs** (turn during play):
- Complexity
- **Anchor** *(new)*
- **Roll** *(new)*
- **Fill** *(new)*
- Retrigger

**Settings** (configure once, leave alone):
- Loop *(sample selection)*
- Length *(trigger interval)*
- **Phrase** *(2/4/8/16 bars)* — new
- Retrig Rate

**Meta:**
- Preset
- Save Preset

Five performance knobs (3 new), four settings (1 new), 2 meta. The host's `ui_hierarchy` JSON may or may not support a visual settings/knobs distinction — at minimum the order in the `knobs` array should group performance knobs together. If the host supports a sub-mode for settings, performance knobs go in the main view and settings go in the sub-mode.

Suggested order in `knobs` array:

```
Preset, Loop, Length, Phrase, Complexity, Anchor, Roll, Fill, Retrigger, Retrig Rate, Save Preset
```

(Settings clustered after Preset, performance knobs in the middle, retrig pair and save at the end.)

All four new params are added to:

- `ui_hierarchy` JSON in `bb_get_param`
- `chain_params` JSON in `bb_get_param`
- `set_param` / `get_param` switch chains
- `state` serialization in `bb_get_param` and `bb_set_param`
- `preset_t` struct, `g_presets` defaults, and `save_preset` log line

## Updated preset defaults

| Name    | Loop | Length | Cplx | Anchor | Roll | Phrase | Fill | Retrig | Rate |
|---------|------|--------|------|--------|------|--------|------|--------|------|
| Calm    | 0    | 0.5    | 18   | 80     | 70   | 4      | 50   | 16     | 2    |
| Mid     | 1    | 0.5    | 56   | 60     | 50   | 4      | 60   | 8      | 4    |
| Frantic | 18   | 0.25   | 80   | 30     | 20   | 2      | 80   | 11     | 4    |

Calm and Mid use 4-bar phrases. Frantic uses 2-bar phrases with hard fills — feels constantly "about to break."

## Data model changes

```c
typedef struct {
    // ...existing fields...

    float anchor;            // 0.0..1.0
    float roll;              // 0.0..1.0
    int   phrase_bars;       // 0=Off, 2, 4, 8, 16
    float fill;              // 0.0..1.0
    int   bar_counter;       // increments every 96 MIDI clocks; reset on transport start
    int   reseed_pending;    // set when transport starts; cleared after srand()
} breakbeat_t;
```

The `clock_counter % 384 == 0 → slice 0` line is deleted.

## Beat-position mapping

The selection logic uses `beat_position` (the slice index in a 4/4 bar this tick falls on) to weight the move-branch.

General form: `beat_position = (int)(trigger_count_in_bar * (8.0 / triggers_per_bar)) % 8`.

`trigger_count_in_bar` resets every 96 MIDI clocks (1 bar at 24 ppq), at the same point `bar_counter` increments.

## Risks / non-goals

- **Non-aligned WAVs**: a few of the included samples may not have slice 0 = kick. Anchor will still produce structure, just not always a *kick* on beat 1.
- **Time signatures other than 4/4**: not supported.
- **No fixed sequences**: by design.
- **No loop rotation**: by design.
- **No serialization of seed**: pattern regenerates every transport start.
- **Phrase boundaries are bar-aligned**: a phrase always starts on a bar; mid-bar phrase resets are not supported.

## Testing approach

- Build and load on Move.
- Anchor=0, Roll=0, Phrase=Off: identical to current main.
- Anchor=100, Roll=0, Complexity=100: slice 0 lands on beat 1 every bar despite max chaos elsewhere.
- Anchor=100, Roll=100, Phrase=Off: heavy slice-0 stuttering with occasional walks and rare escape jumps. Verify it doesn't get *stuck* on slice 0 for absurdly long stretches (escape hatch should fire).
- Anchor=0, Roll=100, Phrase=Off: drum-machine local-walk feel.
- Anchor=80, Roll=70, Phrase=4, Fill=70 (the Calm preset region): verify bars 1–3 feel groovy, bar 4 audibly breaks, then bar 1 resets.
- Phrase=2, Fill=100 (extreme): every other bar should be wild.
- Transport start/stop cycle: verify reseed produces audibly different runs.
- Retrigger=100 + Roll=100: retriggers fire stochastically on top of the sticky walk.
