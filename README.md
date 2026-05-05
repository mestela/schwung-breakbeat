# Breakbeat Generator for Schwung

A stochastic breakbeat slicer and shuffler module for Ableton Move (via Schwung).
It loads a WAV file, slices it into 8 equal parts, and shuffles them based on a complexity parameter.
It also supports structured retriggering (sub-slicing) and automatic BPM sync.

## Parameters
-   **Preset**: Select between "Calm", "Mid", and "Frantic" baked presets.
-   **Loop**: Select the WAV file to play (from 23 available samples). Shows text names.
-   **Length**: Select the playback length in bars/beats (options: 0.25, 0.5, 1, 2, 4, 8). Shows text names.
-   **Complexity**: Percentage chance to jump to a random slice on each trigger (pure randomness).
-   **Retrigger**: Percentage chance to trigger a sub-slice repeat.
-   **Retrig Rate**: Number of divisions for the retrigger (2x, 3x, 4x).

## UI Architecture
This module returns `ui_hierarchy` dynamically from C code as a compact JSON string to enable the stock parameter list view in the Synth view of the host. Do not use `ui_hierarchy` in `module.json` for instruments if you want this behavior.

## Installation
Copy the folder to `/data/UserData/schwung/modules/sound_generators/breakbeat/` on your Move.
