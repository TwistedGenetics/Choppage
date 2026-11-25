# CHOPPAGE for MPC

CHOPPAGE as a plugin on standalone Akai MPCs (MPC OS 3.10.1 or later). The folder carries both builds: 32-bit (Gen 1) and 64-bit (Gen 2); the MPC picks the right one.
It runs the same engine as the desktop AU / VST3, laid out as an Akai S950 front panel. **This edition is untested on hardware.**

## Install
1. Unzip and copy the `Twisted Genetics - VST - CHOPPAGE` folder into `VST2 Plugins` on an MPC drive (SD card, SSD or USB).
2. On the MPC, go to Preferences > Enhancements (Beta) and turn on **Enable Enhancements** and **External Plugin Support**, then restart.
3. Put your breaks (WAV or AIFF) in the plugin's `Content/Breaks` folder. A tempo in the file name is read: `amen_170.wav` = 170 BPM.
4. Make a plugin program with CHOPPAGE (it's under External Plugins).

## Pads
| Pads | Notes (default) | What |
|---|---|---|
| 1–8 (bottom two rows) | C1–G1 (36–43) | The 8 breaks. Hit one to switch to it; it plays locked to the MPC's tempo |
| 9–16 (top two rows) | G#1–D#2 (44–51) | Hold FX: STUTTER, ROLL, REVERSE, TAPE STOP, HALF-TIME, AKAIZE, FILTER DIVE, S950 DROP |

If the program's pads send other notes, set **SET > PAD NOTES** to 37 (the MPC's default) or 48. Slices 1–16 of the playing break are on E2–G3, and pattern memories are on G#3–B4. These are the same as the desktop edition.

## Pages (the function keys under the screen)
The S950 LCD across the top shows the break, its tempo, slices and bars, the MPC tempo, the slice playing now and a playhead bar. Turn a knob on screen, or use the Q-Links: each row of four controls is one bank of four Q-Links. Double-tap a knob for a big knob overlay.

| Page | Controls |
|---|---|
| SAMPLE | BREAK, BROWSE (files in Content/Breaks) + LOAD, PATTERN dice + AMOUNT, FX dice + AMOUNT, UNDO / REDO, ORIGINAL, PLAY (CHOPPAGE's own clock while the MPC is stopped), AKAIZE TO MPC BPM, DEMO BREAK |
| SLICE | SLICE, PITCH, GATE, STRETCH, REVERSE, STUTTER, TRANSIENT, XFADE, EDIT ALL SLICES, MUTE, HIT (K S G H O X), SLICING (TRANSIENTS / GRID), SENSITIVITY, GRID, RESLICE |
| SEQ (press again for 17-32, 33-48, 49-64) | 16 step knobs: each step's slice (-- = rest). Q-Links 1-16 are steps 1-16 |
| PATTERN | LENGTH, STYLE, LAYERS, SWING, CLEAR, MEMORY + SAVE / RECALL |
| FX PADS | the pads' rates and times, PAD QUANTISE |
| SOUND (press again for SPACE) | CHARACTER, SAMPLE RATE, VOLUME, FILTER, RESONANCE, DISTORTION, CRUSH, REVERB, ECHO |
| SET | TEMPO SOURCE, SPEED, FIT, TRIGGER, BREAK SWITCH, GATE, VELOCITY > SLICE, PAD NOTES, LCD colour |

**AKAIZE TO MPC BPM** renders the break time-stretched to the MPC's tempo the Akaizer way, writes it to `Content/Renders`, and loads it back into the same break.

## What the MPC can't do
- **No waveform:** MPC plugin screens are built from the SDK's widgets, so there's no drawing area for a waveform. The LCD shows readouts and a playhead instead. The desktop edition draws the waveform.
- **No pad colours:** plugins can't light the MPC's pads.
- **Content folder only:** the plugin can only read files in its own `Content` folder.

## Build
Builds run on GitHub Actions: `choppage-mpc/build.sh` uses the official [MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk) (submodule `external/mpc-plugin-builder-sdk`) and zig to cross-compile arm32 + arm64. Each build is checked with the SDK's host simulator.

| File | What |
|---|---|
| `src/ChoppageMpc.cpp` | The whole MPC-specific layer: `mpc::Plugin` adapter |
| `app/ChoppageMpcApp.cpp`, `include/` | Platform-independent app: parameter table, mirrors, readouts, worker (tested on desktop by `tests/test_mpc_app.cpp`) |
| `tools/make_skin.py` | Generates `skins/` (TUI.json, Q-Links.json, PNGs) from `tools/params.json` (`test_mpc_app --dump`) |
| `tools/sync_core.sh` | One `src/core_*.cpp` wrapper per core source |
