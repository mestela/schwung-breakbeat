# Schwung Breakbeat for desktop

An experimental JUCE port of Schwung Breakbeat. It builds VST3 on macOS and
Windows, plus AU and a standalone app on macOS. The Move release is unchanged.

## Build

```sh
cmake -S desktop -B desktop/build -DCMAKE_BUILD_TYPE=Release
cmake --build desktop/build --config Release
ctest --test-dir desktop/build -C Release --output-on-failure
```

The first configure downloads JUCE 9.0.3. The macOS VST3 is under
`desktop/build/SchwungBreakbeat_artefacts/Release/VST3/`. The downloadable
Mac disk image contains one `.vst3` bundle; copy that whole bundle to
`~/Library/Audio/Plug-Ins/VST3/` to try it in Live. A VST3 bundle contains
internal `Contents` folders, but those should stay inside the `.vst3` package.
Load a WAV,
AIFF, FLAC or MP3 into A or B, then start the host transport. MIDI notes
36–51 select the 16 pads directly.

This is a first desktop build. It has not yet been tested in Ableton Live or on
Windows. Sample files are referenced by path in the plug-in state, so moved
files need to be loaded again.
