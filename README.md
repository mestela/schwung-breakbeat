# Breakbeat Generator for Schwung

A musically-anchored breakbeat slicer for Ableton Move (via Schwung).

Loads a WAV breakbeat, slices it into 8 equal parts, and recombines them per
trigger using biased-random selection. Designed for jungle, DnB, and breakbeat-
driven dance music: classic breaks (Amen, Funky Drummer, Think, Apache, etc.)
keep their kick-snare backbone via the **Anchor** knob, while **Roll**, **Fill**,
and **Phrase** add motion, fills, and multi-bar phrasing.

Demo: https://www.youtube.com/watch?v=vYf1vPt3pMc

## Before you start... fix midi out
This uses midi out to get timing from the Move side of things. I tried (and failed) to have a more elegant solution, so this is the workflow for now:

1. From the regular Move UI, get into Settings (shift-step 2, the little gear icon at the bottom)
2. Find Midi Sync (it will be in one of 3 modes, Off, In, Out)
3. Select it, set it to Out


## Controls

### Main page

| Knob | Range | What it does |
|---|---|---|
| **Complexity** | 0–100 | Probability that any given trigger picks a *random* slice instead of advancing in order. At 0, slices follow beat position (or Anchor if engaged). At 100, every non-stay trigger rolls a fresh random slice. |

Main also shows the current sample, slice, and playback mode. For example,
`A 3 1X` means A slice 3 is playing normally, `A 3 2R` means it is retriggering
twice, and `B 2 3S` means B slice 2 is stretched 3×. Custom Preset and Save Preset controls
have been removed from the page; Schwung saves the parameters with the Set.

Main also contains the sample and phrase settings:

| Setting | Values | What it does |
|---|---|---|
| **A Sample** | filepath | Selects which WAV to slice. Opens file browser rooted at `/data/UserData/breakbeat-samples`, which contains a `Built-in/` folder and a `User Library/` symlink to your device's sample library. |
| **A Length** | enum | Trigger interval for A loop (1/4 bar to 8 bars). |
| **B Sample** | filepath | Selects the loop used for phrase fills. |
| **B Length** | enum | Trigger interval for B loop (1/4 bar to 8 bars). |
| **B Chance** | 0–100 | Probability of swapping to B Loop on the last bar of a phrase. |
| **Phrase** | enum | Multi-bar phrase length (Off, 2, 4, 8, 16 bars). |

### Anchors page

| Knob | Range | What it does |
|---|---|---|
| **Anchor** | 0–100 | Locks slice index to *beat position* in the bar. At 0, behavior is sequential advance with Complexity-driven random swaps. At 100, beats 1 and 3 (kick/snare) are protected from swaps and the no-swap fallback snaps to `beat_position`. |
| **Roll** | 0–100 | Temporal stickiness. At 0, every trigger is independent. At 100, most triggers either repeat the current slice, walk to the ±1 neighbor, or take a 5% escape-hatch jump. Produces the rolling jungle "1 2 3 1 2 3 4 5" feel and held-slice stutters. |
| **Fill** | 0–100 | Intensity of the *fill bar* modulation. Only meaningful when **Phrase** is non-Off. Modulates Complexity ↑, Roll ↓, Anchor ↓ on the last bar of every phrase. At 100, the fill bar throws out the groove rules entirely. |

### Retrig page

| Control | Range | What it does |
|---|---|---|
| **Retrig 2x** | 0–100 | Per-bar probability of a 2x (half-slice) stutter on any given beat. |
| **Retrig 3x** | 0–100 | Per-bar probability of a 3x stutter. |
| **Retrig 4x** | 0–100 | Per-bar probability of a 4x stutter. |
| **Retrig 8x** | 0–100 | Per-bar probability of an 8x (16th-note micro-stutter) on any given beat. |

The four retrigger knobs are independent. If several win on one slice, one rate
is chosen at random.

### Stretch page

| Control | Range | What it does |
|---|---|---|
| **Chance** | 0–100 | Probability that each automatic slice is stretched. 0 disables random stretching; 100 stretches every eligible slice. |
| **Ln Min / Ln Max** | 0–100 | Lowest and highest stretch multipliers. 0 maps to 2× and 100 maps to 16×; every integer multiplier in between is available. |
| **Sl Min / Sl Max** | 0–100 | Lowest and highest grid durations. 0 maps to one slice and 100 maps to eight slices. |
| **PtchMin / PtchMax** | 0–100 | Lowest and highest random pitch shifts. 0 maps to −12 semitones, 50 to zero, and 100 to +12 semitones. |
| **Grn FX** | 0–100 | Adds short repeated grains for a coarse early-sampler texture. |

### Stretch FX page

| Control | Range | What it does |
|---|---|---|
| **GrnCyc** | 10–120 ms | Length of each grain. Shorter cycles sound more buzzy; longer cycles make repeats clearer. |
| **PiLck** | Off / On | Keeps normal slices near their original pitch as tempo changes. A randomly stretched slice always uses pitch-preserving grains and the chosen pitch shift. |

Stretched slices remain on the original grid. The stretch multiplier controls
the audio rate, while the slice range controls how long it plays. Automatic
selection resumes after the chosen number of slots. A played or programmed pad takes over at its next
slice boundary; an A/B phrase change takes over at the bar boundary. Random
stretching and retrigger subdivision are mutually exclusive for each slice.

### Playing slices from pads or a clip

In drum-pad layout, pads 1–8 (notes 36–43) select A slices 0–7, and pads
9–16 (notes 44–51) select B slices 0–7. A pad press auditions one slice while
Play is stopped. With Play running, a note waits for the next slice boundary
unless that boundary has not rendered yet. The selected slice
then has its turn, and the following boundary returns to the automatic pattern.
Each pad slice plays at its own loop's A or B length, even when the automatic
pattern uses the other loop's grid. Notes recorded on later steps can each
select their own slice.

## Saved Settings & Custom Samples

Schwung saves Breakbeat's parameters with the Set. Older `.json` files in the
module's `presets/` directory can still be loaded for compatibility, but the
module's Preset and Save Preset controls are no longer shown.

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
   - Else → play `beat_position` (the natural slice for this beat)
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

## SSH setup (Mac → Move)

The install script uses `scp`/`ssh` to push files to the device. If you get `Permission denied (publickey)`:

```bash
# Generate a key on your build machine
ssh-keygen -t ed25519 -f ~/.ssh/move -N ""

# Add it to the Move (run from a machine that already has access)
echo "$(cat ~/.ssh/move.pub)" | ssh ableton@move.local \
  "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

# Add a host entry so the key is picked up automatically
cat >> ~/.ssh/config << 'EOF'
Host move.local
    User ableton
    IdentityFile ~/.ssh/move
    StrictHostKeyChecking no
EOF
```

If the Move's host key has changed (after a firmware update or reset), clear the stale entry first:
```bash
ssh-keygen -R move.local
```

## Changelog

### Unreleased
- **Complete status readings.** Status now shows the sample, slice, and effect multiplier (`1X`, `2R`, `3S`) as one consistent readout.
- **Tighter live-pad timing.** A pad note received after a slice has started sounding waits for the next clocked slice boundary.
- **Independent stretch controls.** Pitch Lock preserves pitch across tempo changes; Grain FX adds repeated-grain texture, with a separate Grain Cycle length. All default off except the cycle length, so existing presets keep their sound.
- **Independent random stretch ranges.** Stretch can choose any integer multiplier from 2× to 16× and occupy one to eight grid slices, with separate pitch endpoints from −12 to +12 semitones. Pads and phrase changes still interrupt on the grid.
- **Clearer pages and status.** Main includes the status readout, Anchors holds Anchor/Roll/Fill, and the eight Stretch knobs show full names when touched. Schwung's Set saving replaces the module's custom Preset controls.

### v0.4.20
- **Two banks of playable slices.** Pads 1–8 play A and pads 9–16 play B, including audition while stopped and notes programmed in a clip.
- **Quantized handoff.** Early pad hits land on the next slice boundary, then the automatic break resumes at the following boundary. Notes just after a boundary play immediately.
- **Independent pad speed.** Manual A and B slices play at their own loop lengths while the automatic pattern keeps its original clock grid. A shorter pad slice repeats within its turn rather than playing at half speed.
- **Tempo refresh.** Stopped pad presses recheck the host tempo when Move's published set tempo is temporarily unavailable.

### v0.4.14
- **Native Move tempo.** Breakbeat reads the live Set BPM published by Schwung 1.6 instead of relying on the legacy inferred project-tempo callback.
- **Correct first downbeat.** MIDI Start now arms playback; slice zero begins on the first clock pulse, which is Move's actual downbeat.
- **Tempo changes while stopped.** The current Move tempo is refreshed directly on Start, preventing the previous BPM from swallowing the opening slice after a stopped tempo edit.
- **Live tempo changes.** Changes made during playback remain sample-accurate to Move's clock while the sample rate follows the newly reported BPM.

### v0.4.11
- **Deterministic transport and phrasing.** MIDI clock owns bar boundaries directly: A plays the non-fill bars and B is selected only on the final phrase bar, with no deferred one-bar-ahead switch state.
- **Stable live editing.** A/B sample choices load on a low-priority worker without blocking Move's shared audio callback. Replacing a sample preserves the current slice instead of shifting the sequence.
- **Immediate length changes.** A Length and B Length update playback rate immediately and restart only their slice grid; the bar and phrase clocks remain untouched.
- **Reliable tempo response.** Playback starts at the stored Set tempo and follows live tempo changes without a Stop/Start cycle.
- **Transport correctness.** Breakbeat remains silent while stopped, begins on A slice zero, and returns to A on the exact next phrase downbeat after its one-bar B fill.

### v0.4.7 (test build)
- **Dynamic tempo changes.** Stored Set BPM supplies the correct playback rate immediately on Start; after Schwung has a clean live-clock measurement, that BPM controls the sample cursor so tempo changes alter pitch/speed without Stop/Start.

### v0.4.6 (test build)
- **Actual Set tempo reaches the module.** Schwung now reads the active Set's stored BPM on its worker thread and publishes it through the real-time-safe plugin callback. Previously the callback existed but remained zero, causing Breakbeat to fall back to 120 BPM (including in the 91 BPM test Set).
- **Safe startup fallback.** If the Set snapshot is briefly unavailable during loading, Breakbeat takes one valid host BPM and retains the last known tempo instead of repeatedly resetting to 120 BPM.

### v0.4.5 (test build)
- **Clock-authoritative scheduling.** MIDI Start/Stop and the incoming 24-PPQN clock are now the sole authority for slice and bar boundaries. The interpolated host playhead is no longer used for audio scheduling.
- **Original phrase structure restored.** In a four-bar phrase, A occupies bars one through three and B occupies only bar four, regardless of B's declared source length.
- **Stored project tempo.** Breakbeat reads Schwung's explicit Set/project BPM when instantiated and whenever the user changes it. Live MIDI-clock measurement is never used for playback speed; clock pulses control boundaries only.

### v0.4.4 (test build)
- **Continuous audio cursor.** Absolute song position still schedules slices and A/B boundaries, but audio now runs continuously between them at the Set tempo. This removes the clicks and distortion caused by tiny per-block cursor corrections in v0.4.3.

### v0.4.3 (test build)
- **Song-position timing.** Slice choice and playback phase now come from Schwung's absolute song beat, so playback starts on the correct slice without a preroll and is re-anchored every audio block instead of accumulating drift.
- **Exact B-loop placement.** A selected B loop plays once and ends on the phrase boundary. For example, a two-bar B loop in a four-bar phrase occupies bars three and four, then returns to A exactly once.
- **Independent B sequence.** A/B changes reset the authoritative trigger sequence, preventing B from inheriting A's slice number or firing duplicate transitions.

### v0.4.1 (test build)
- **Transport is explicit.** MIDI Start begins playback at slice zero, MIDI Stop silences the next block, and clock ticks received while stopped cannot start audio.
- **Immediate tempo lock.** Playback rate comes from Schwung's current Set tempo on the first block; MIDI clock supplies musical phase rather than a loop-dependent warm-up measurement.
- **Silent Set restore.** Programmatic preset/state restoration no longer starts sample preview while the Move is stopped.
- **Real-time-safe phrase swaps.** A and B are mapped before playback and bar-boundary changes swap resident sample metadata without file I/O, logging, or allocation in the audio callback.
- **State fixes.** Preset ID and both A/B loop lengths now round-trip consistently, with compatibility for v0.4 state.

### v0.4.0
- **Multi-rate retrigger.** Replaced the single Retrigger + Retrig Rate pair with four independent per-bar probability knobs (Retrig 2x / 3x / 4x / 8x). Any combination can be active simultaneously; if multiple rates fire on the same beat one is chosen at random. Probabilities are normalised correctly using the inverse binomial formula so 100% guarantees the rate fires on every beat and 5% means roughly 5% of bars. Old presets migrate automatically.
- **Sample preview.** Changing A Sample or Preset while transport is stopped now plays one full loop of the selected break immediately, using the current tempo knob value for rate. Lets you audition samples from the file browser without starting the transport.
- **User sample library access.** The A/B Sample file browser now exposes a `User Library/` folder alongside `Built-in/`, linked to `/data/UserData/UserLibrary/Samples`.
- **Tick-based timing rewrite.** Slice triggers now fire directly from MIDI 0xF8 clock ticks rather than a BPM phase accumulator. Playback rate is measured from the actual sample-counter distance between ticks — no BPM math, no drift. Falls back to the Move tempo-knob BPM when MIDI clock is unavailable.
- **Phrase-2 pre-scheduling fix.** For 2-bar phrases, B was never scheduled because the `bar_in_phrase == 0` boundary is never reached during normal playback. Fixed by pre-scheduling at transport reset.
- **Slice drift fix.** The no-swap branch now returns `beat_position` rather than `(current_slice+1) & 7`, preventing a random swap from permanently drifting all subsequent beats off-grid.
- **Retrigger bleed fix.** A mid-flight retrigger on the last beat of a bar no longer carries into the first beat of the incoming sample when a phrase swap occurs.

### v0.3.3
- Added diagnostic logging; renamed module display name to Breakbeat.

### v0.3.2
- **Module now loads the `1_calm` preset immediately on startup** instead of a missing hardcoded path.
- **Preset list sorted alphabetically** so index 0 is always `1_calm`.
- **State restore applies sample immediately** when transport is stopped.
- **Added `madvise(MADV_WILLNEED)`** after every `mmap` to pre-fault WAV pages before the audio thread touches them, preventing render-watchdog kills on sample swap.
