// CHOPPAGE on a hardware host: the core Session, the MPC-only controls and the worker that keeps the screen's
// mirror parameters and readouts in step with the session. Platform-independent (talks to the MPC through
// IPlatform), so it is tested on the desktop (tests/test_mpc_app.cpp).
//
// Threads: process() is the audio thread. setParam() / formatValue() can come from any host thread and only touch
// atomics or a short mutex. Everything else (loading, dice, editing, readouts) happens on the app's worker, which
// is the session's "message thread".
#pragma once
#include "IPlatform.h"
#include "MpcParams.h"
#include "choppage/Session.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bc {

class ChoppageMpcApp
{
public:
    struct Transport
    {
        bool valid = false, playing = false;
        double bpm = 120.0, ppq = 0.0;
    };

    explicit ChoppageMpcApp(IPlatform& platform, bool startWorker = true);
    ~ChoppageMpcApp();

    // ---- audio thread
    void prepare(double sampleRate, int maxBlock);
    void process(float* l, float* r, int frames, const MidiMsg* midi, int numMidi, const Transport& tr) noexcept;

    // ---- parameters (any thread)
    void setParam(int index, float plain);
    float getParam(int index) const;
    // Value text for every parameter whose text the app owns (core units, readouts, browser, steps).
    bool formatValue(int index, float plain, std::string& out) const;

    // ---- state (non-real-time)
    std::string saveState() const;
    void loadState(const std::string& text);

    // One round of the worker (the thread runs it ~30 times a second; tests call it directly).
    void tick();
    void waitIdle() { session_.waitUntilIdle(); }
    Session& session() { return session_; }

private:
    void workerLoop();
    void handle(int index, float v, float prev);
    void write(int index, float v);           // the app changes a parameter: remembered as seen, host told
    void syncMirrors(bool force);
    void syncReadouts(bool force);
    void syncWave(bool force);
    void syncList(bool force);
    void syncSeq(bool force);
    void syncAudition();
    void moveMarkerOf(int marker, int position);
    struct View { int start = 0, length = 1; };
    View viewOf(const SlotContent& c, int sel);
    void moveSelectedMarker(int position);
    void setText(int index, const std::string& s);
    std::string browserText(int k) const;
    std::string stepText(int step, int value) const;

    IPlatform& platform_;
    Session session_;
    std::atomic<float> values_[kMpcNumParams];
    float seen_[kMpcNumParams];

    mutable std::mutex textMutex_;
    std::vector<std::string> text_;                 // readout texts, by parameter index
    std::vector<Session::BrowserEntry> browser_;             // copy for formatValue
    std::vector<std::string> badgeLetters_;         // per slice of the selected break (step text)
    std::string breaksDir_;                         // where DISK lists breaks from (see the constructor)
    std::string breaksLabel_ = "CONTENT/BREAKS";    // the same, for the screen
    void chooseBreaksFolder();
    int dragTicks_ = 0;                             // a drag on the strip is under way: hold the view, don't mirror
    std::string moveText_ = "-";                    // MOVE knob text: the marker's time
    std::string endText_ = "-";                     // END knob text
    std::string cellText_[kSeqLanes * 16];          // what the MPC shows while a grid cell is jogged ("K 05")
    float cellShown_[kSeqLanes * 16] = {};          // the frame each cell shows (jog direction = value vs this)

    // shared with the audio thread
    std::atomic<double> fallbackBpm_{170.0};        // the selected break's tempo, when the MPC sends none
    std::atomic<double> hostBpm_{0.0};
    std::atomic<bool> hostPlaying_{false};
    double sampleRate_ = 44100.0;
    std::vector<MidiMsg> midiScratch_;
    bool padDown_[kFxPads] = {};                    // audio thread: last on-screen pad state

    // worker-side change detection
    uint32_t lastVersion_ = ~0u;
    int lastSlot_ = -1, lastSlice_ = -1, lastBrowserCount_ = -1, lastStepShown_ = -2;
    bool lastEditAll_ = false;
    // waveform: the shown span is frozen while a marker is edited inside it
    View frozen_;
    int viewSlot_ = -1, viewSel_ = -1, viewZoom_ = -1, viewN_ = -1;
    uint64_t waveKey_ = ~0ull;
    int listTop_ = 0;
    uint64_t listKey_ = ~0ull;
    uint64_t seqKey_ = ~0ull;
    int auditionWant_ = -1, auditionWait_ = 0;
    std::string message_;
    int messageTicks_ = 0;

    std::atomic<bool> quit_{false};
    std::atomic<bool> resync_{false};
    std::thread worker_;
};

} // namespace bc
