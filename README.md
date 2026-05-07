# Breakbeat Generator for Schwung

A musically-anchored breakbeat slicer for Ableton Move (via Schwung).

Loads a WAV breakbeat, slices it into 8 equal parts, and recombines them per
trigger using biased-random selection. Designed for jungle, DnB, and breakbeat-
driven dance music: classic breaks (Amen, Funky Drummer, Think, Apache, etc.)
keep their kick-snare backbone via the **Anchor** knob, while **Roll**, **Fill**,
and **Phrase** add motion, fills, and multi-bar phrasing.

## Controls

### Performance knobs (turn while playing)

| Knob | Range | What it does |
|---|---|---|
| **Complexity** | 0–100 | Probability that any given trigger picks a *random* slice instead of advancing in order. At 0, slices follow either Anchor (if engaged) or sequential advance. At 100, every non-stay trigger rolls a fresh random slice. |
| **Anchor** | 0–100 | Locks slice index to *beat position* in the bar. At 0, behavior matches the original module (sequential advance with random swaps). At 100, the no-swap fallback forces `slice = beat_position` — kick lands on every beat 1, snare lands on every beat 3. The locked weight curve also makes lock-zone slices unswappable: at high Anchor, beats 1 and 3 *never* get swapped out by Complexity. |
| **Roll** | 0–100 | Temporal stickiness. At 0, every trigger is independent. At 100, most triggers either repeat the current slice, walk to the ±1 neighbor, or take a 5% escape-hatch jump. Produces the rolling jungle "1 2 3 1 2 3 4 5" feel and held-slice stutters. |
| **Fill** | 0–100 | Intensity of the *fill bar* modulation. Only meaningful when **Phrase** is non-Off. Modulates Complexity ↑, Roll ↓, Anchor ↓ on the last bar of every phrase. At 0, fill bars play normally. At 100, the fill bar throws out the groove rules entirely. |
| **Retrigger** | 0–100 | Probability that any given trigger sub-divides the slice for a stutter repeat. |

### Settings (configure once, leave alone)

| Setting | Values | What it does |
|---|---|---|
| **A Sample** | filepath | Selects which WAV to slice. Opens file browser. |
| **A Length** | enum | Trigger interval for A loop (1/4 bar to 8 bars). |
| **B Sample** | filepath | Selects the loop used for phrase fills. |
| **B Length** | enum | Trigger interval for B loop (1/4 bar to 8 bars). |
| **B Chance** | 0–100 | Probability of swapping to B Loop on the last bar of a phrase. |
| **Phrase** | enum | Multi-bar phrase length (Off, 2, 4, 8, 16 bars). |
| **Retrig Rate** | enum | Sub-divisions per retrigger event (2x, 3x, 4x, 8x, Rand). |

### Meta

| Knob | Values | What it does |
|---|---|---|
| **Preset** | enum | Selects a dynamic preset from `presets/` folder. |
| **Save Preset** | toggle | Saves current settings as a new JSON file in `presets/`. |
| **Status** | read-only | Displays current playing loop, slice, and retrig status (e.g., `A_3_1x`). |

## Dynamic Presets & Custom Samples

Presets are no longer hardcoded in C. They are stored as `.json` files in the `presets/` directory. The module scans this directory on startup and when saving a new preset.

To add custom presets, place a JSON file in the `presets/` folder on the device and restart or save a preset to rescan.

Custom samples can be loaded via the file browser for **A Sample** and **B Sample**. The browser opens in `/data/UserData/breakbeat-samples`, with symlinks to built-in samples and your User Library.

## How the algorithm picks slices

Each trigger tick:

1. **Phrase modulation** — if Phrase is set and we're on the fill bar, Complexity is pushed up, Roll and Anchor are pushed down (proportional to Fill).
2. **Roll the dice** — random number `r ∈ [0,1)`:
   - If `r < (1 - Roll)` → **Move** branch (independent decision)
   - Otherwise → **Stay** branch (correlated with previous slice)
3. **Move branch:**
   - Compute swap probability: `p_swap = Complexity * weight_at(beat_position, Anchor)`
   - If we swap → uniform random slice 0..7
   - Else → with probability **Anchor**, snap to `beat_position`; otherwise sequential advance from current slice
4. **Stay branch:**
   - 5% escape hatch: jump 2..4 forward
   - Otherwise: with probability `(1 - weight_at(current_slice, Anchor))` repeat current; else walk ±1

The **anchor weight curve** at Anchor=100 is `[0.0, 0.5, 1.0, 0.7, 0.0, 0.5, 1.0, 1.2]`:
- Slices 0 and 4 (beats 1 and 3) → weight 0 → never swap (kick/snare locked)
- Slice 7 (last 16th) → weight 1.2 → *more* likely to swap than baseline (fill territory)

Reseed happens automatically on transport start, so each play produces a fresh stochastic realization.

## Knob interactions worth knowing

- **Anchor=0, Roll=0, Phrase=Off** → original module behavior: sequential advance with Complexity-driven random swaps.
- **Anchor=100, Roll=0, Complexity=0** → straight playback of the break in beat order. At Length=0.5, this means slices 0, 2, 4, 6 (the structural beats only); at Length=0.25, slices 0..7 in order.
- **Anchor=100, Roll=100, Complexity=50** → camped on slice 0 with occasional ±1 walks and rare 5% escape jumps. Heavy stutter feel.
- **Anchor=80, Roll=70, Phrase=4, Fill=70** → bars 1–3 groove, bar 4 audibly opens up into a fill, bar 1 of the next phrase resets.
- **Phrase=2, Fill=100** → every other bar feels wild.

## Built-in Presets

Loaded dynamically from `src/presets/`:
- **1_calm**
- **2_mid**
- **3_frantic**

## UI Architecture

This module returns `ui_hierarchy` dynamically from C code as a compact JSON
string to enable the stock parameter list view in the Synth view of the host.
Do not use `ui_hierarchy` in `module.json` for instruments if you want this
behavior.

## Building

```bash
./scripts/build.sh    # cross-compiles dsp.so for aarch64 via Docker
./scripts/install.sh  # scp's to ableton@move.local
```

After install, restart Schwung on the device to load the module.

## Testing

Pure slice-selection logic is host-testable:

```bash
./tests/run_tests.sh  # compiles tests/test_slice_select.c and runs assertions
```
