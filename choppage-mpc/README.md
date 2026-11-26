# CHOPPAGE for MPC

![MAIN](../docs/mpc-main.png)

CHOPPAGE as a plugin on standalone Akai MPCs (MPC OS 3.10.1 or later). The folder carries both builds: 32-bit (Gen 1) and 64-bit (Gen 2); the MPC picks the right one.
It runs the same engine as the desktop AU / VST3, laid out as an Akai S950 front panel. **This edition is untested on hardware.**

## Install
1. Unzip and copy the `Twisted Genetics - VST - CHOPPAGE` folder into `VST2 Plugins` on an MPC drive (SD card, SSD or USB).
2. On the MPC, go to Preferences > Enhancements (Beta) and turn on **Enable Enhancements** and **External Plugin Support**, then restart.
3. Put your breaks (WAV, AIFF or REX .rx2 / .rex) in **`VST2 Plugins/Content/Breaks`**: a shared folder beside your plugin folders (CHOPPAGE makes it the first time it loads), so breaks don't live inside the plugin and survive reinstalls. Other plugins can use the same idea (`VST2 Plugins/Content/Wavetables` ...). Breaks left in the plugin's own `Content/Breaks` are still listed until the shared folder has some. A REX loop loads with ReCycle's slices and tempo. A tempo in the file name is read: `amen_170.wav` = 170 BPM.
4. Make a plugin program with CHOPPAGE (it's under External Plugins).

## Pads
| Pads | Notes (default) | What |
|---|---|---|
| 1–8 (bottom two rows) | C1–G1 (36–43) | The 8 breaks. **A break plays only when its key triggers it**, in time with the MPC's tempo. Pressing a key (or a sequenced note) also switches **every screen to that break**: the LCD, the waveform and slices, the SEQ grid and the slice settings |
| 9–16 (top two rows) | G#1–D#2 (44–51) | Hold FX: STUTTER, ROLL, REVERSE, TAPE STOP, HALF-TIME, AKAIZE, FILTER DIVE, S950 DROP. The FX plays while you hold the pad and stops when you let go |

The 8 FX pads on the touchscreen work the same way: they play while touched and stop when your finger lifts. Keys on the screen (DICE, UNDO, ADD and so on) light while touched and act once per press.

How long a break plays is set by **PLAY > GATE**:
- **LATCH** (the default): it plays until you press the same key again, play another break in its mute group, or stop the MPC.
- **HOLD**: it plays for as long as the key, or the note in the MPC's sequence, is held.
- **ONE SHOT**: one note plays the pattern through to its end, then it stops (the note's length doesn't matter).

**Mute groups (PLAY > MUTE GROUP, per break):** breaks in the same group cut each other. The new one carries on from the same point in the bar (BREAK SWITCH = BAR SYNC, the default), or starts from step 1 (RESTART), or waits for the next beat (QUANT). Breaks in different groups play together, in time with each other. All 8 start in group 1: one break at a time.

If the program's pads send other notes, set **SET > PAD NOTES** to 37 (the MPC's default) or 48. Slices 1–16 of the playing break are on E2–G3, and pattern memories are on G#3–B4. These are the same as the desktop edition.

## Screens (the function keys under the MPC's screen)
Double-tap a knob for a big knob overlay. The Q-Links follow each screen, in rows of four.

**MAIN** is the desktop's main screen in the BR3AK arrangement. The S950 LCD has:
- ▶ (CHOPPAGE's own clock while the MPC is stopped), ◀ BREAK NAME ▶, LOAD and the tempo.
- The 8 break keys.
- The break's **waveform with its slice markers** (the selected slice in a yellow box, the slice playing now lit up) and the hit letter (K S G H O X) of every slice. Tap the waveform to pick and hear a slice.
- The selected slice's settings and the status line.

Under the LCD:
- **PATTERN** and **FX** bars. The dice square rolls; tap the bar to open that section, and tap it again to come back.
- The 8 FX pads, with their settings.
- FILTER, DISTORTION, CRUSH and REVERB, with SAMPLE RATE and VOLUME either side.

The sections:
- **PATTERN** turns the LCD into the SEQ grid (below), with LENGTH, STEPS SHOWN, FOLLOW, ORIGINAL, CLEAR, the selected step's FX and RATE, UNDO and REDO underneath. STYLE, SWING and the memories are on the PATTERN screen.
- **FX** puts each pad's setting knob under it.
- **LOAD** opens the DISK section: the files in `VST2 Plugins/Content/Breaks` with their tempo (read again each time DISK opens, so new files show). Tap the list, then **turn the jog wheel**: each break plays as you land on it. **Push the jog wheel** to load it into the selected break. DEMO BREAK and AKAIZE TO MPC BPM are beside the list.

**SLICE / CHOP** (press SLICE again for CHOP): the waveform at the top, with ZOOM ALL or ZOOM SLICE, **+ ADD** and **- DEL** on the LCD.
- **ZOOM** on the MAIN screen's LCD switches the same waveform between the whole break and the selected slice (it is the same picture as on SLICE).
- **Chopping by touch**: the **SELECT / ADD / DEL** keys on the LCD say what a touch on the waveform does.
  - SELECT: the slice you touch is selected and plays.
  - ADD: a new slice starts where you touch. With **SNAP TO HITS** on (CHOP page), it lands on the nearest drum hit; the new slice is selected and plays.
  - DEL: the slice you touch goes (joined to the one before).
- **Slice pads** (E2–G3, pad bank B with PAD NOTES at 36) select the slice they play, so you can pick a slice by ear.
- **Jog wheel**: after touching the waveform, turning the jog wheel moves the selected slice's start; pushing it plays the slice.
- **Touch a slice** in the waveform to select it: everything under the LCD now edits that slice.
- **Drag with your finger** on the strip under the hit letters: the **START** handle sits on the selected slice's start marker and follows your finger. In ZOOM SLICE the view holds still while you drag, for fine moves; the MOVE knob or jog wheel fine-tune.
- **+ ADD** splits the selected slice in two; **- DEL** joins it to the one before.
- **SLICE** sets the selected slice's **START** and **END** (the waveform's markers move as you turn them) and its **HIT**: K, S, GS, H, O or P (perc). HIT overrides the automatic one.
- SLICE also has PITCH, GATE, STRETCH, TRANSIENT, REVERSE, STUTTER, XFADE and MUTE.
- **CHOP** has ADD (split the slice in two), DELETE (join it to the one before), NUDGE START < >, EDIT ALL SLICES, SLICING (TRANSIENTS / GRID), SENSITIVITY, GRID and RESLICE.
- Slices sit end to end, so a slice's END is the next slice's START. The last slice ends with the break.

**SEQ** is the BR3AK pattern editor: KICK, SNARE, GHOST, HAT, OPEN and PERC lanes over 16 steps. A hit shows the number of the slice it plays.
- **Tap** a cell to put a hit of that kind there, or take it off.
- With the cell selected (red frame), **turn the jog wheel** to step through that lane's hits only: kicks in KICK, snares in SNARE, ghosts in GHOST, hats in HAT, open hats in OPEN, percs in PERC. The MPC shows which one (for example "K 05").
- **FX on a step:** tap a step, or the FX row under the grid, to select it. Then pick its FX (STUTTER, REVERSE, STRETCH, TAPE STOP or OFF) and RATE (1/8 to 1/64). The FX row shows each step's FX.
- Above the grid: PATTERN / ORIGINAL, LENGTH, STEPS (1-16 to 49-64) and FOLLOW, which keeps the playing steps on screen.
- The 16 Q-Links set the slice of the 16 steps shown.

**PLAY / SET** (press again for SET):
- PLAY has GATE, MUTE GROUP, BREAK SWITCH, VELOCITY > SLICE, TRIGGER and PAD NOTES.
- SET has TEMPO SOURCE, SPEED, FIT, the LCD colour (BLUE, GREEN, AMBER, WHITE) and SLICING: TRANS / GRID, **SENSITIVITY**, GRID and RESLICE. The slicing settings are also used the next time a break loads into that slot.

**PATTERN** memories: pick one with MEMORY, then **SAVE PATTERN** or **SAVE ORIGINAL** (the break as recorded, no FX), and **RECALL** (or play its pad, G#3–B4). **PATTERN** also has **RESET TO ORIGINAL** (the break as recorded, FX lane emptied; UNDO brings your pattern back). RESET is also in the MAIN screen's PATTERN section.

**FX PAD QUANTISE** (FX PADS page): OFF, 1/32, 1/16, 1/8, 1/4, BAR. A pad hit off the beat waits for the next line of the grid; letting go ends it on the next line.

**PATTERN, FX PADS, SOUND / SPACE**: everything else, in rows of four. FX PADS has the AKAIZE pad's and slice STRETCH's CYCLE LENGTH / FREQ / MANUAL.

**AKAIZER** (press FX PADS again): the full Akaizer render. SYNC TO MPC BPM (from LOOP BPM, AUTO = the break's own tempo; TARGET when the MPC has no tempo) or STRETCH 25–2000 % with SYNC off, CYCLE LENGTH, CYCLE FREQ (linked to the length unless FREQ MANUAL), PITCH ±24. **AKAIZE THE BREAK** writes a 16-bit `<name>_akaized.wav` to `Content/Renders` and loads it back into the break.

**Hits are found automatically:** when a break loads, CHOPPAGE slices it and works out what each slice is (kick, snare, ghost, hat, open hat or perc), on the MPC just as on the desktop. The SEQ lanes and the dice use these. If one is wrong, set it with HIT on the SLICE screen.

## What the MPC can't do
- **Coarser waveform:** MPC plugin screens have no drawing area, so the waveform is made of 84 picture columns the plugin sets (the approach Twisted MPC Breaks uses). It's less detailed than the desktop's.
- **No pad colours:** plugins can't light the MPC's pads.
- **Content folder only:** the plugin can only read files in its own `Content` folder.

## Build
Builds run on GitHub Actions: `choppage-mpc/build.sh` uses the official [MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk) (submodule `external/mpc-plugin-builder-sdk`) and zig to cross-compile arm32 + arm64. Each build is checked with the SDK's host simulator.

| File | What |
|---|---|
| `src/ChoppageMpc.cpp` | The whole MPC-specific layer: `mpc::Plugin` adapter |
| `app/ChoppageMpcApp.cpp`, `include/` | Platform-independent app: parameter table, mirrors, readouts, worker (tested on desktop by `tests/test_mpc_app.cpp`) |
| `tools/make_skin.py` | Generates `skins/` (TUI.json, Q-Links.json, PNGs) from `tools/params.json` (`test_mpc_app --dump`), and checks the hardware rules (no label over a touch area, focus frame first) |
| `tools/preview_skin.py` | Draws the screens from the generated TUI.json with real values (`test_mpc_app --state`), to check a layout without an MPC |
| `tools/sync_core.sh` | One `src/core_*.cpp` wrapper per core source |
