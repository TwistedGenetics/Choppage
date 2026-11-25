# What CHOPPAGE reuses

CHOPPAGE follows the Twisted-Helix layout: a JUCE-free core shared by a JUCE desktop wrapper and an MPC wrapper built with the official inMusic MPC Plugin SDK.

| From | What | Status in CHOPPAGE |
|---|---|---|
| [Twisted-Breaks](https://github.com/TwistedGenetics/Twisted-Breaks) | Transient detector, slice table, sample buffer | Ported into `choppage-core` without JUCE; slice limit raised from 16 to 64 |
| Twisted-Breaks | Headless `TestMain` / `HostTest` pattern | Followed in `choppage-desktop/Tests` |
| [Twisted-Timestretch](https://github.com/TwistedGenetics/Twisted-Timestretch) | The Akaizer v1.2 cyclic stretch | `choppage-core/src/Akaizer.cpp`; drives the AKAIZE pad, the slice STRETCH and the AKAIZE TO HOST BPM render |
| Twisted-Timestretch | CI workflow, Linux minimal-deps build | Adapted |
| [Twisted-Helix](https://github.com/TwistedGenetics/Twisted-Helix) | core / desktop / mpc split, append-only shared parameter table, generated MPC skin, `build.sh`, MPC CI + release | Followed |
| [Twisted-MPC-Breaks](https://github.com/TwistedGenetics/Twisted-MPC-Breaks) | Pad layout ideas for the MPC | Ideas only; its community-toolkit host shim is replaced by the official SDK |

## New in CHOPPAGE (`choppage-core`)

| File | What |
|---|---|
| `Model.h` | Slots, badges, slice settings, patterns (fixed-size data for the audio thread) |
| `Dsp.h` | Filters, smoothing, RNG, FFT, reverb: the small DSP kit that replaces JUCE's |
| `AudioFile.*` | WAV / AIFF reader and 24-bit WAV writer |
| `Analysis.*` | Hit classifier (K / S / G / H / O), BPM + bars from length and file name, grid slicing |
| `Generator.*` | PATTERN dice (8 styles, AMOUNT, locks) and FX dice |
| `SliceVoice.*` | One slice: rate, reverse, stutter, cyclic (Akai) stretch, transient shaping, gate |
| `PerfFx.*` | The 8 hold pads on a 4 s output ring |
| `SoundChain.*` | Machine character, SAMPLE RATE, filter, distortion, crush, echo, reverb |
| `Engine.*` | Mono-break playback, host-locked sequencer, MIDI note map |
| `Params.*` | The parameter table shared by the desktop and MPC editions |
| `Session.*` | Break slots, edit history, memories, folder browser, Akaize render, bounce, saved state; worker thread |
| `DemoBreak.h` | Synthesised demo break, so it makes a sound on first open |
