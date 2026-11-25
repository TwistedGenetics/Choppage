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
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/Model.h"
#include "Engine/SliceVoice.h"
#include "Engine/PerfFx.h"
#include "Engine/SoundChain.h"

namespace bc
{
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
        void publishContent (int slot, SlotContent::Ptr);
        void publishProgram (int slot, const SlotProgram&);

        // ---- audio thread --------------------------------------------------------------
        void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Host&, const Settings&) noexcept;

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
        void handleMidi (const juce::MidiMessage&, const Settings&, double ppqNow) noexcept;
        void triggerStep (int step, int atSample, const Settings&, double samplesPerStep) noexcept;
        void playSlice (int slice, int atSample, int gateSamples, const Settings&) noexcept;
        void startStopFromHost (const Host&) noexcept;
        void renderVoices (int from, int to) noexcept;

        double sr = 44100.0;
        int maxBlock = 512;

        // published by the message thread
        juce::SpinLock lock;
        std::array<SlotContent::Ptr, kSlots> pendingContent;
        std::array<SlotProgram, kSlots> pendingProgram {};
        std::atomic<uint32_t> pendingVersion { 0 };
        uint32_t seenVersion = 0;

        // audio thread's copies
        std::array<SlotContent::Ptr, kSlots> content;
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
        juce::AudioBuffer<float> mix;

        // FX
        std::array<bool, kFxPads> midiPad {};
        std::array<int, kFxPads> autoPadLeft {};   // FX lane events, samples left
        int autoStutterDiv = 16;
        PerfFx perf;
        SoundChain chain;
    };
}
