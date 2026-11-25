# What CHOPPAGE reuses

| From | File(s) | Status in CHOPPAGE |
|---|---|---|
| [Twisted-Breaks](https://github.com/TwistedGenetics/Twisted-Breaks) | `Engine/SampleBuffer.h`, `Engine/SampleLoader.*` | Copied as is (namespace `tb`) |
| Twisted-Breaks | `Engine/TransientDetector.*` | Copied as is |
| Twisted-Breaks | `Engine/SliceTable.h` | Copied; limit raised from 16 to 64 slices |
| Twisted-Breaks | `CMakeLists.txt` layout, headless `TestMain` / `HostTest` pattern | Followed |
| [Twisted-Timestretch](https://github.com/TwistedGenetics/Twisted-Timestretch) | `Engine/Akaizer.*` (the exact Akaizer v1.2 port) | Copied as is; kept for the offline AKAIZE RENDER (next phase) |
| Twisted-Timestretch | CI workflow, `HostTest.cpp`, Linux minimal-deps build | Adapted |
| [Twisted-MPC-Breaks](https://github.com/TwistedGenetics/Twisted-MPC-Breaks) | MPC host shim, skin, Docker toolchains | To follow for the MPC edition (not in this phase) |

## New in CHOPPAGE

| File | What |
|---|---|
| `Engine/Model.h` | Slots, badges, slice settings, patterns (fixed-size POD for the audio thread) |
| `Engine/Analysis.*` | Hit classifier (K / S / G / H / O), BPM + bars from length and file name, grid slicing |
| `Engine/Generator.*` | PATTERN dice (8 styles, AMOUNT, locks) and FX dice |
| `Engine/SliceVoice.*` | One slice: rate, reverse, stutter, cyclic (Akai) stretch, transient shaping, gate |
| `Engine/PerfFx.*` | The 8 hold pads on a 4 s output ring |
| `Engine/SoundChain.*` | Machine character (S950, SP-1200, MPC60, S1000, LO-FI, CLEAN), SAMPLE RATE fader, filter, distortion, crush, echo, reverb |
| `Engine/Engine.*` | Mono-break playback, host-locked sequencer, MIDI note map |
| `Engine/DemoBreak.h` | Synthesised demo break so it makes a sound on first open |
| `UI/*`, `PluginEditor.*` | The S950 panel in BR3AK's arrangement |
