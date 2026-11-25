#pragma once
/*
    ChoppageProcessor — owns the parameters, the 8 slots (message-thread model)
    and the audio Engine.

    Message thread: loads and analyses breaks on a background pool, keeps every
    slot's slices / badges / slice settings / pattern / history / memories, and
    publishes what the audio thread needs to the Engine. UI edits go through
    the methods below, each of which republishes.
*/
#include <JuceHeader.h>
#include "Engine/Engine.h"
#include "Engine/Params.h"

class ChoppageProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    ChoppageProcessor();
    ~ChoppageProcessor() override;

    // ---- AudioProcessor ------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CHOPPAGE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- Model -----------------------------------------------------------------------
    enum class Status { empty, loading, ready, missing, unreadable };

    struct Slot
    {
        Status status = Status::empty;
        juce::File file;                 // empty for the built-in demo
        bool demo = false;
        bc::SlotContent::Ptr content;
        bc::SlotProgram program;          // what plays (pattern = current or original)
        bc::Pattern current, original;
        std::vector<bc::Pattern> history; // dice results, newest last
        int historyPos = -1;
        bool showOriginal = false;
        std::array<std::optional<bc::Pattern>, bc::kMemories> memories;
        bool gridSlicing = false;        // SLICING: TRANSIENTS / GRID
        float sensitivity = 0.62f;
        int gridPerBar = 16;
        int64_t seed = 1;
        uint32_t version = 0;            // bumps on every change, for the UI
    };

    const Slot& getSlot (int i) const { return slots[(size_t) juce::jlimit (0, bc::kSlots - 1, i)]; }
    int  getSelectedSlot() const noexcept { return engine.selectedSlot.load(); }
    void selectSlot (int);
    int  getSelectedSlice() const noexcept { return selectedSlice; }
    void selectSlice (int, bool audition = true);
    bool isEditingAll() const noexcept { return editAll; }
    void setEditingAll (bool b) { editAll = b; ++uiVersion; }

    // loading
    void loadFile (int slot, const juce::File&);
    void loadDemo (int slot);
    void clearSlot (int slot);
    void reslice (int slot, bool grid, float sensitivity, int gridPerBar);
    bool isBusy() const { return pool.getNumJobs() > 0 || loading > 0; }

    // SAMPLE tab
    enum class SliceField { pitch, gate, stretch, reverse, stutter, trans, xfade, level };
    float getSliceValue (SliceField, bool all) const;
    void  setSliceValue (SliceField, float, bool all);
    void  toggleMute (int slice);
    void  setBadge (int slice, bc::Badge);
    void  moveMarker (int marker, int position);
    void  addMarker (int position);
    void  deleteMarker (int marker);

    // dice / history / SEQ tab
    void rollPattern();
    void rollFx();
    void historyBack();
    void historyForward();
    void setShowOriginal (bool);
    void setPatternLength (int bars);
    void toggleStep (int step, bc::Badge lane);
    void swapStep (int step);
    void toggleLock (int step);
    void setFxStep (int step, bc::FxType, int rate);
    void cycleFxRate (int step);
    void saveMemory (int memory);
    void recallMemory (int memory);
    void clearPattern();

    /** Renders one pass of the selected break's pattern (with the current sound) to a
        24-bit WAV in the temp folder, for DRAG to the DAW. Message thread. */
    juce::File bounceLoop();

    // transport (UI play button) and FX pads (UI)
    void setPlaying (bool b) { engine.uiPlay = b; }
    bool isPlaying() const { return engine.playingSlot.load() >= 0; }
    void setPadHeld (int pad, bool held) { if (pad >= 0 && pad < bc::kFxPads) engine.uiPad[(size_t) pad] = held; }

    // read-outs
    int   getCurrentStep() const  { return engine.currentStep.load(); }
    int   getCurrentSlice() const { return engine.currentSlice.load(); }
    int   getPlayingSlot() const  { return engine.playingSlot.load(); }
    bool  isPadLit (int p) const  { return p >= 0 && p < bc::kFxPads && engine.padLit[(size_t) p].load(); }
    float getPeak()               { return engine.getPeak(); }
    double getHostBpm() const     { return hostBpm.load(); }
    bool  isHostPlaying() const   { return hostPlaying.load(); }
    uint32_t getUiVersion() const { return uiVersion.load(); }
    static juce::String statusText (Status);

    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undo;

   #if CHOP_TEST_BUILD
    bc::Engine& getEngineForTests() { return engine; }
    void waitUntilIdle (int timeoutMs = 10000);
   #endif

private:
    void timerCallback() override;
    void installContent (int slot, bc::SlotContent::Ptr, const juce::File&, bool demo, const juce::ValueTree* restore);
    void publish (int slot);
    void pushHistory (Slot&, const bc::Pattern&);
    void changed (int slot);
    bc::SlotContent::Ptr withBadges (const bc::SlotContent&, const std::array<bc::Badge, bc::kMaxSlices>&) const;
    bc::SlotContent::Ptr withSlices (const bc::SlotContent&, const bc::SliceTable&) const;
    bc::Engine::Settings readSettings() const;
    void restoreSlot (int i, const juce::ValueTree&);

    bc::Engine engine;
    std::array<Slot, bc::kSlots> slots;
    std::vector<bc::SlotContent::Ptr> keepAlive;
    juce::ThreadPool pool { 1 };
    juce::AudioFormatManager formats;
    std::atomic<int> loading { 0 };
    int selectedSlice = 0;
    bool editAll = false;
    std::atomic<uint32_t> uiVersion { 1 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ChoppageProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoppageProcessor)
};
