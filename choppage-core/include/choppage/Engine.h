#pragma once
/*
    Engine — the audio-thread half of CHOPPAGE.

    One break plays at a time. The message thread publishes each slot's audio
    (SlotContent, immutable) and its program (slice settings + pattern, POD)
    under a spin lock; the audio thread picks them up with a try-lock at the
    top of a block, so it never waits and never allocates.

    Notes (C1 = 36):  C1-G1 break slots · G#1-D#2 FX pads (held) ·
                      E2-G3 slices 1-16 of the playing break · G#3-B4 pattern memories

    SYNC:  the pattern's steps are clocked from the host's song position
           (or an internal clock when the host is stopped and a break is latched).
    MIDI:  no sequencing; slice notes play slices, break notes pick the break.

    Work is done in 64-sample chunks: MIDI and step triggers are sample-exact
    inside a chunk; FX pads switch at chunk edges (1.5 ms at 44.1 kHz).
*/
#include "choppage/Model.h"
#include <atomic>
#include <limits>
#include "choppage/SliceVoice.h"
#include "choppage/PerfFx.h"
#include "choppage/SoundChain.h"

namespace bc
{
    /** A MIDI message at a sample offset in the block. */
    struct MidiMsg
    {
        int offset = 0;
        uint8_t data[3] { 0, 0, 0 };
        int status() const noexcept { return data[0] & 0xF0; }
        bool isNoteOn() const noexcept  { return status() == 0x90 && data[2] > 0; }
        bool isNoteOff() const noexcept { return status() == 0x80 || (status() == 0x90 && data[2] == 0); }
        bool isAllOff() const noexcept  { return status() == 0xB0 && (data[1] == 120 || data[1] == 123); }
        static MidiMsg noteOn (int note, int vel, int offset = 0) { MidiMsg m; m.offset = offset; m.data[0] = 0x90; m.data[1] = (uint8_t) note; m.data[2] = (uint8_t) vel; return m; }
        static MidiMsg noteOff (int note, int offset = 0) { MidiMsg m; m.offset = offset; m.data[0] = 0x80; m.data[1] = (uint8_t) note; return m; }
    };

    /** Tiny spin lock: the message side spins, the audio side only ever try-locks. */
    class SpinLock
    {
    public:
        void lock() noexcept { while (flag.test_and_set (std::memory_order_acquire)) {} }
        bool tryLock() noexcept { return ! flag.test_and_set (std::memory_order_acquire); }
        void unlock() noexcept { flag.clear (std::memory_order_release); }
    private:
        std::atomic_flag flag = ATOMIC_FLAG_INIT;
    };

    class Engine
    {
    public:
        struct Host
        {
            bool   playing = false;
            bool   hasInfo = false;
            double bpm = 120.0;
            double ppq = 0.0;     // at the first sample of the block
        };

        struct Settings
        {
            bool   midiTrigger = false;     // TRIGGER: SYNC (false) / MIDI
            int    breakMode = 1;           // 0 RESTART, 1 BAR SYNC, 2 QUANT
            bool   gateHold = false;        // GATE: LATCH / HOLD
            bool   velToSlice = false;
            double speed = 1.0;
            bool   fit = true;
            bool   keepPitch = false;       // FIT: REPITCH / KEEP
            double tempoSourceMul = 1.0;    // AUTO 1, /2 0.5, X2 2
            float  swing = 0.5f;
            double stretchCycleMs = 9.5;
            PerfFx::Settings pads;          // samplesPerBeat filled in here
            SoundChain::Settings sound;
        };

        void prepare (double sampleRate, int maxBlock);
        void reset();

        // ---- message thread --------------------------------------------------------
        void publishContent (int slot, ContentPtr);
        void publishProgram (int slot, const SlotProgram&);

        // ---- audio thread --------------------------------------------------------------
        /** Renders n samples into left / right (overwrites). MIDI sorted by offset. */
        void process (float* left, float* right, int n, const MidiMsg* midi, int numMidi, const Host&, const Settings&) noexcept;

        // ---- shared with the UI (atomics) ------------------------------------------------
        std::atomic<int>  selectedSlot { 0 };     // UI / last break note
        std::atomic<int>  playingSlot  { -1 };    // -1 = silent
        std::atomic<int>  currentStep  { -1 };
        std::atomic<int>  currentSlice { -1 };
        std::atomic<bool> uiPlay { false };       // the ▶ button (internal clock when the host is stopped)
        std::array<std::atomic<bool>, kFxPads> uiPad {};   // UI / automation pad holds
        std::array<std::atomic<bool>, kFxPads> padLit {};  // pad audible (for the lamps)
        std::atomic<int>  previewSlice { -1 };    // UI click on a slice
        std::atomic<int>  memoryRequest { -1 };   // slot * 16 + memory, for the message thread
        std::atomic<int>  lastNote { -1 };
        float getPeak() noexcept { return chain.getPeak(); }

    private:
        static constexpr int kChunk = 64;

        void pullPublished() noexcept;
        void handleMidi (const MidiMsg&, const Settings&) noexcept;
        void triggerStep (int step, int atSample, const Settings&, double samplesPerStep) noexcept;
        void playSlice (int slice, int atSample, int gateSamples, const Settings&) noexcept;
        void startStopFromHost (const Host&) noexcept;
        void renderVoices (int from, int to) noexcept;

        double sr = 44100.0;
        int maxBlock = 512;

        // published by the message thread
        SpinLock lock;
        std::array<ContentPtr, kSlots> pendingContent;
        std::array<SlotProgram, kSlots> pendingProgram {};
        std::atomic<uint32_t> pendingVersion { 0 };
        uint32_t seenVersion = 0;

        // audio thread's copies
        std::array<ContentPtr, kSlots> content;
        std::array<SlotProgram, kSlots> program {};

        // sequencer
        int    activeSlot = 0;
        int    pendingSlot = -1;          // QUANT waits for the next beat
        bool   latched = false;           // a break note turned it on (host stopped / HOLD off)
        int    heldBreakNote = -1;
        double stepPos = 0.0;             // in 16ths since the pattern's step 0
        double syncOffset = 0.0;          // stepPos - host position in 16ths (0 when on the bar grid)
        int64_t lastStepGlobal = std::numeric_limits<int64_t>::min();
        double internalPpq = 0.0;
        bool   wasRunning = false, wasHostPlaying = false, halfTimeWas = false;
        bool   restartNow = false, restartFromNote = false;
        double lastHostBpm = 120.0;
        int    renderedUpTo = 0;
        int    pendingSliceNote = -1;
        int    startStepOffset = 0;

        // voices: the new slice fades in while the old one fades out
        std::array<SliceVoice, 3> voices;
        int nextVoice = 0;
        float mix[2][kChunk] {};

        // FX
        std::array<bool, kFxPads> midiPad {};
        std::array<int, kFxPads> autoPadLeft {};   // FX lane events, samples left
        int autoStutterDiv = 16;
        PerfFx perf;
        SoundChain chain;
    };
}
