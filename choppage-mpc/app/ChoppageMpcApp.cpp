#include "ChoppageMpcApp.h"
#include "choppage/AudioFile.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace bc {

// ===================================================================================== parameter table
namespace {

std::vector<MpcParamDef> buildDefs()
{
    std::vector<MpcParamDef> v;
    // core: same ids, names and ranges as the desktop edition
    for (const auto& d : paramDefs())
    {
        MpcParamDef p;
        p.id = d.id; p.name = d.name; p.min = d.min; p.max = d.max; p.def = d.def;
        p.kind = d.kind == PKind::Bool ? MpcKind::Bool : d.kind == PKind::Choice ? MpcKind::Choice : MpcKind::Float;
        p.choices = d.choices;
        if (d.centre > 0.0f)
        {
            const float c = (d.centre - d.min) / (d.max - d.min);
            p.skew = std::log(0.5f) / std::log(c);    // core: norm = lin^k with c^k = 0.5; SDK: norm = lin^skew
        }
        v.push_back(p);
    }
    while ((int)v.size() < kMpcReserveEnd)
    {
        MpcParamDef p;
        p.id = "reserved_" + std::to_string(v.size());
        p.name = "-";
        p.kind = MpcKind::Bool;
        p.hidden = true;
        v.push_back(p);
    }
    auto F = [&](const char* id, const char* name, float lo, float hi, float def) {
        MpcParamDef p; p.id = id; p.name = name; p.min = lo; p.max = hi; p.def = def; v.push_back(p); return &v.back(); };
    auto I = [&](const char* id, const char* name, int lo, int hi, int def) {
        MpcParamDef p; p.id = id; p.name = name; p.kind = MpcKind::Int; p.min = (float)lo; p.max = (float)hi; p.def = (float)def;
        v.push_back(p); return &v.back(); };
    auto B = [&](const char* id, const char* name, bool def) {
        MpcParamDef p; p.id = id; p.name = name; p.kind = MpcKind::Bool; p.max = 1; p.def = def ? 1.0f : 0.0f; v.push_back(p);
        return &v.back(); };
    auto T = [&](const char* id, const char* name) { auto* p = B(id, name, false); p->trigger = true; return p; };
    auto C = [&](const char* id, const char* name, std::vector<std::string> ch, int def) {
        MpcParamDef p; p.id = id; p.name = name; p.kind = MpcKind::Choice; p.choices = std::move(ch);
        p.max = (float)(p.choices.size() - 1); p.def = (float)def; v.push_back(p); return &v.back(); };
    auto R = [&](const char* id, const char* name) { auto* p = I(id, name, 0, 1, 0); p->readout = true; return p; };

    C("break", "Break", { "1 C1", "2 C#1", "3 D1", "4 D#1", "5 E1", "6 F1", "7 F#1", "8 G1" }, 0);
    I("browse", "Browse", 0, 255, 0);
    T("load", "Load");
    T("demo", "Demo");
    T("akaize", "Akaize");
    T("dicePattern", "Pattern");
    T("diceFx", "FX Dice");
    T("undo", "Undo");
    T("redo", "Redo");
    B("original", "Original", false);
    B("play", "Play", false);
    I("slice", "Slice", 1, 64, 1);
    B("editAll", "Edit All", false);
    F("slPitch", "Pitch", -24, 24, 0);
    F("slGate", "Gate", 0.05f, 1, 1);
    F("slStretch", "Stretch", 0.5f, 4, 1);
    B("slReverse", "Reverse", false);
    C("slStutter", "Stutter", { "OFF", "2", "3", "4", "6", "8" }, 0);
    F("slTrans", "Trans", 0, 1, 0.5f);
    F("slXfade", "Xfade", 0.5f, 20, 2);
    B("slMute", "Mute", false);
    C("slBadge", "Hit", { "K", "S", "G", "H", "O", "X" }, 0);
    C("slicing", "Slicing", { "TRANSIENTS", "GRID" }, 0);
    F("sens", "Sensitivity", 0, 1, 0.62f);
    C("grid", "Grid", { "1/8", "1/16", "1/32" }, 1);
    T("reslice", "Reslice");
    C("length", "Length", { "1 BAR", "2 BARS", "4 BARS" }, 1);
    T("clear", "Clear");
    I("memory", "Memory", 1, 16, 1);
    T("memSave", "Save");
    T("memRecall", "Recall");
    C("padNotes", "Pad Notes", { "36-51 C1", "37-52 MPC", "48-63 C2" }, 0);
    R("infoName", "Break");
    R("infoBpm", "BPM");
    R("infoStatus", "Status");
    R("infoSlice", "Now");
    R("infoMsg", "Message");
    { auto* p = F("playhead", "Playhead", 0, 1, 0); p->readout = true; }
    for (int s = 0; s < 64; ++s)
        I(("step" + std::to_string(s + 1)).c_str(), ("Step " + std::to_string(s + 1)).c_str(), 0, 64, 0);
    // 0.3: BR3AK main screen + waveform
    { auto* p = C("view", "View", { "MAIN", "FX", "DISK", "PATTERN" }, 0); p->uiOnly = true; p->readout = true; }
    { auto* p = B("showPattern", "Pattern Section", false); p->uiOnly = true; }
    { auto* p = B("showFx", "FX Section", false); p->uiOnly = true; }
    { auto* p = B("showDisk", "Disk Section", false); p->uiOnly = true; }
    T("brkPrev", "Break <");
    T("brkNext", "Break >");
    F("slMove", "Move", 0, 1, 0);
    T("slNudgeL", "Nudge <");
    T("slNudgeR", "Nudge >");
    T("slAdd", "Add");
    T("slDel", "Delete");
    { auto* p = C("zoom", "Zoom", { "ALL", "SLICE" }, 0); p->uiOnly = true; }
    { auto* p = I("waveTap", "Wave Tap", 0, kWaveCols, 0); p->uiOnly = true; }
    { auto* p = I("listTap", "List Tap", 0, kListLines, 0); p->uiOnly = true; }
    R("infoSlParams", "Slice Settings");
    for (int k = 0; k < kListLines; ++k) R(("list" + std::to_string(k + 1)).c_str(), ("List " + std::to_string(k + 1)).c_str());
    for (int c = 0; c < kWaveCols; ++c)
    { auto* p = I(("wave" + std::to_string(c + 1)).c_str(), ("Wave " + std::to_string(c + 1)).c_str(), 0, kWaveFrames - 1, 0); p->readout = true; }
    for (int c = 0; c < kWaveCols; ++c)
    { auto* p = I(("tab" + std::to_string(c + 1)).c_str(), ("Tab " + std::to_string(c + 1)).c_str(), 0, tabFrame(3, kTabFrames - 1), 0); p->readout = true; }
    { auto* p = C("seqPage", "Steps", { "1-16", "17-32", "33-48", "49-64" }, 0); p->uiOnly = true; }
    { auto* p = B("seqFollow", "Follow", true); p->uiOnly = true; }
    { auto* p = I("seqTap", "Grid Tap", 0, kSeqLanes * 16, 0); p->uiOnly = true; }
    { auto* p = I("seqHead", "Step Now", 0, 4 * 17 - 1, 0); p->readout = true; }
    for (int c = 0; c < kSeqLanes * 16; ++c)
    { auto* p = I(("cell" + std::to_string(c + 1)).c_str(), ("Cell " + std::to_string(c + 1)).c_str(), 0, 4 * kCellFrames - 1, 0); p->readout = true; }
    for (int k = 0; k < 16; ++k)
        I(("vstep" + std::to_string(k + 1)).c_str(), ("Step " + std::to_string(k + 1)).c_str(), 0, 64, 0);
    R("infoBrk", "Break Now");
    return v;
}

const std::vector<MpcParamDef>& defs()
{
    static const std::vector<MpcParamDef> d = [] {
        auto v = buildDefs();
        if ((int)v.size() != kMpcNumParams) { std::fprintf(stderr, "CHOPPAGE: parameter table has %d entries, expected %d\n", (int)v.size(), kMpcNumParams); std::abort(); }
        return v;
    }();
    return d;
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
int roundi(float v) { return (int)std::lround(v); }

const int kStutterValues[] = { 0, 2, 3, 4, 6, 8 };
int stutterChoice(int n) { for (int i = 0; i < 6; ++i) if (kStutterValues[i] == n) return i; return 0; }
const int kGridValues[] = { 8, 16, 32 };

// Denormals cost a lot of CPU on the MPC's ARM cores: flush them to zero for the whole block (FZ bit).
struct ScopedFlushDenormals
{
#if defined(__aarch64__)
    uint64_t old = 0;
    ScopedFlushDenormals() { asm volatile("mrs %0, fpcr" : "=r"(old)); asm volatile("msr fpcr, %0" :: "r"(old | (1ull << 24))); }
    ~ScopedFlushDenormals() { asm volatile("msr fpcr, %0" :: "r"(old)); }
#elif defined(__arm__) && defined(__ARM_FP)
    uint32_t old = 0;
    ScopedFlushDenormals() { asm volatile("vmrs %0, fpscr" : "=r"(old)); asm volatile("vmsr fpscr, %0" :: "r"(old | (1u << 24))); }
    ~ScopedFlushDenormals() { asm volatile("vmsr fpscr, %0" :: "r"(old)); }
#elif defined(__SSE__) || defined(__x86_64__)
    unsigned old = 0;
    ScopedFlushDenormals() { old = __builtin_ia32_stmxcsr(); __builtin_ia32_ldmxcsr(old | 0x8040); }
    ~ScopedFlushDenormals() { __builtin_ia32_ldmxcsr(old); }
#endif
};

// Soft ceiling: untouched below 0.9, rounds off into +-1 above (the MPC's bus takes floats, but its meters and
// the sampler's resampling do not like overs).
inline float ceilingClip(float x)
{
    const float a = std::fabs(x);
    if (a < 1e-20f) return 0.0f;     // decaying tails: no near-zero dust in the output
    if (a <= 0.9f) return x;
    const float over = (a - 0.9f) / 0.1f;
    const float y = 0.9f + 0.1f * (over / (1.0f + over));
    return x < 0 ? -y : y;
}

std::string upper(std::string s)
{
    for (auto& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32); else if (c == '_') c = ' ';
    return s;
}

} // namespace

const MpcParamDef& mpcParamDef(int index)
{
    const auto& d = defs();
    return d[(size_t)std::clamp(index, 0, (int)d.size() - 1)];
}

bool isStepParam(int index) { return index >= MP_STEP0 && index < MP_STEP0 + 64; }
bool isPictureParam(int index)
{
    return index == MP_PLAYHEAD || (index >= MP_WAVE0 && index < MP_TAB0 + kWaveCols) || index == MP_SEQ_HEAD
        || (index >= MP_CELL0 && index < MP_CELL0 + kSeqLanes * 16);
}

int padBaseNote(int choice)
{
    static const int base[] = { 36, 37, 48 };
    return base[std::clamp(choice, 0, 2)];
}

// ===================================================================================== app
ChoppageMpcApp::ChoppageMpcApp(IPlatform& platform, bool startWorker) : platform_(platform)
{
    static_assert(kMpcNumParams == MP_INFO_BRK + 1, "parameter table");
    for (int i = 0; i < kMpcNumParams; ++i)
    {
        values_[i] = mpcParamDef(i).def;
        seen_[i] = mpcParamDef(i).def;
    }
    text_.resize((size_t)kMpcNumParams);
    midiScratch_.reserve(1024);
    const std::string content = platform_.contentDir();
    std::error_code ec;
    std::filesystem::create_directories(content + "/Breaks", ec);
    session_.setDataFolder(content);
    session_.scanFolder(content + "/Breaks");
    if (startWorker) worker_ = std::thread([this] { workerLoop(); });
}

ChoppageMpcApp::~ChoppageMpcApp()
{
    quit_ = true;
    if (worker_.joinable()) worker_.join();
}

void ChoppageMpcApp::prepare(double sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate;
    session_.prepare(sampleRate, maxBlock);
}

void ChoppageMpcApp::process(float* l, float* r, int frames, const MidiMsg* midi, int numMidi, const Transport& tr) noexcept
{
    ScopedFlushDenormals ftz;
    float v[kNumCoreParams];
    for (int i = 0; i < kNumCoreParams; ++i) v[i] = values_[i].load(std::memory_order_relaxed);
    Engine::Settings s = settingsFromParams(v);
    // on-screen pads: follow the parameter both ways (press and release), only when it changes
    for (int p = 0; p < kFxPads; ++p)
    {
        const bool down = v[P_PAD1 + p] > 0.5f;
        if (down != padDown_[p]) { padDown_[p] = down; session_.setPadHeld(p, down); }
    }

    Engine::Host host;
    host.hasInfo = tr.valid;
    host.playing = tr.playing;
    host.bpm = tr.bpm;
    host.ppq = tr.ppq;
    const double bpm = tr.valid ? tr.bpm : fallbackBpm_.load();
    s.sound.echoSamples = echoBeats(roundi(v[P_ECHO_TIME])) * sampleRate_ * 60.0 / std::max(20.0, bpm);
    hostBpm_ = tr.valid ? tr.bpm : 0.0;
    hostPlaying_ = tr.playing;

    // The 16 pads: whatever notes the MPC program sends, pad 1 becomes C1 (36): pads 1-8 the breaks, 9-16 the FX.
    const int base = padBaseNote(roundi(values_[MP_PAD_NOTES].load(std::memory_order_relaxed)));
    midiScratch_.clear();
    for (int i = 0; i < numMidi && (int)midiScratch_.size() < (int)midiScratch_.capacity(); ++i)
    {
        MidiMsg m = midi[i];
        const int st = m.data[0] & 0xF0;
        if ((st == 0x80 || st == 0x90 || st == 0xA0) && base != 36 && m.data[1] >= base && m.data[1] < base + 16)
            m.data[1] = (uint8_t)(36 + m.data[1] - base);
        midiScratch_.push_back(m);
    }
    session_.process(l, r, frames, midiScratch_.data(), (int)midiScratch_.size(), host, s);
    for (int i = 0; i < frames; ++i)
    {
        l[i] = ceilingClip(l[i]);
        if (r != l) r[i] = ceilingClip(r[i]);
    }
}

void ChoppageMpcApp::setParam(int index, float plain)
{
    if (index >= 0 && index < kMpcNumParams) values_[index].store(plain, std::memory_order_relaxed);
}

float ChoppageMpcApp::getParam(int index) const
{
    return index >= 0 && index < kMpcNumParams ? values_[index].load(std::memory_order_relaxed) : 0.0f;
}

bool ChoppageMpcApp::formatValue(int index, float plain, std::string& out) const
{
    if (index < 0 || index >= kMpcNumParams) return false;
    if (index < kNumCoreParams) { out = formatParam(index, plain); return true; }
    const auto& d = mpcParamDef(index);
    if (d.hidden) { out = ""; return true; }
    if (isPictureParam(index) && index != MP_PLAYHEAD) { out = ""; return true; }
    if (d.readout && index != MP_PLAYHEAD)
    {
        std::lock_guard<std::mutex> l(textMutex_);
        out = text_[(size_t)index];
        return true;
    }
    char buf[64];
    switch (index)
    {
        case MP_BROWSE: { std::lock_guard<std::mutex> l(textMutex_); out = browserText(roundi(plain)); return true; }
        case MP_SL_PITCH: std::snprintf(buf, sizeof buf, "%+.1f ST", plain); if (std::fabs(plain) < 0.05f) std::snprintf(buf, sizeof buf, "0 ST"); out = buf; return true;
        case MP_SL_GATE: case MP_SL_TRANS: case MP_SENS: case MP_PLAYHEAD:
            std::snprintf(buf, sizeof buf, "%d%%", roundi(plain * 100.0f)); out = buf; return true;
        case MP_SL_STRETCH: std::snprintf(buf, sizeof buf, "%.2fX", plain); out = buf; return true;
        case MP_SL_XFADE: std::snprintf(buf, sizeof buf, "%.1f MS", plain); out = buf; return true;
        case MP_SLICE: std::snprintf(buf, sizeof buf, "S%02d", roundi(plain)); out = buf; return true;
        case MP_SL_MOVE: { std::lock_guard<std::mutex> l(textMutex_); out = moveText_; return true; }
        case MP_MEMORY: std::snprintf(buf, sizeof buf, "M%02d", roundi(plain)); out = buf; return true;
        default: break;
    }
    if (isStepParam(index) || (index >= MP_VSTEP0 && index < MP_VSTEP0 + 16))
    {
        std::lock_guard<std::mutex> l(textMutex_);
        out = stepText(0, roundi(plain));
        return true;
    }
    if (d.kind == MpcKind::Bool) { out = plain >= 0.5f ? (d.trigger ? "GO" : "ON") : (d.trigger ? "-" : "OFF"); return true; }
    if (d.kind == MpcKind::Choice)
    {
        out = d.choices[(size_t)std::clamp(roundi(plain), 0, (int)d.choices.size() - 1)];
        return true;
    }
    return false;
}

std::string ChoppageMpcApp::browserText(int k) const
{
    if (browser_.empty()) return "NO BREAKS IN CONTENT/BREAKS";
    if (k < 0 || k >= (int)browser_.size()) return "-";
    const auto& e = browser_[(size_t)k];
    char buf[160];
    if (e.bpm > 0) std::snprintf(buf, sizeof buf, "%.0f  %s", e.bpm, upper(e.name).c_str());
    else std::snprintf(buf, sizeof buf, "%s", upper(e.name).c_str());
    return buf;
}

std::string ChoppageMpcApp::stepText(int step, int value) const
{
    (void)step;
    if (value <= 0) return "--";
    char buf[16];
    const char* b = value - 1 < (int)badgeLetters_.size() ? badgeLetters_[(size_t)(value - 1)].c_str() : "";
    std::snprintf(buf, sizeof buf, "%02d %s", value, b);
    return buf;
}

// ===================================================================================== state
std::string ChoppageMpcApp::saveState() const { return session_.saveState(); }

void ChoppageMpcApp::loadState(const std::string& text)
{
    session_.loadState(text);
    lastVersion_ = ~0u;   // the worker resyncs every mirror
}

// ===================================================================================== worker
void ChoppageMpcApp::workerLoop()
{
    while (!quit_)
    {
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

void ChoppageMpcApp::write(int index, float v)
{
    seen_[index] = v;
    values_[index].store(v, std::memory_order_relaxed);
    platform_.notifyParameter(index, v);
}

void ChoppageMpcApp::setText(int index, const std::string& s)
{
    {
        std::lock_guard<std::mutex> l(textMutex_);
        if (text_[(size_t)index] == s) return;
        text_[(size_t)index] = s;
    }
    write(index, seen_[index] >= 0.5f ? 0.0f : 1.0f);   // flip so the screen asks for the new text
}

void ChoppageMpcApp::tick()
{
    session_.poll();

    // 1. the user moved something: act on it
    for (int i = kMpcReserveEnd; i < kMpcNumParams; ++i)
    {
        const float v = values_[i].load(std::memory_order_relaxed);
        if (v == seen_[i]) continue;
        seen_[i] = v;
        if (!mpcParamDef(i).readout) handle(i, v);
    }

    // 2. the session moved: mirrors and readouts follow
    syncMirrors(false);
    syncReadouts(false);
    syncWave(false);
    syncList(false);
    syncSeq(false);
    if (messageTicks_ > 0 && --messageTicks_ == 0) message_.clear();
}

void ChoppageMpcApp::handle(int index, float v)
{
    const auto& d = mpcParamDef(index);
    const bool on = v >= 0.5f;
    auto say = [this](const std::string& m) { message_ = m; messageTicks_ = 90; };
    auto field = [this, v](Session::SliceField f) { session_.setSliceValue(f, v, session_.editingAll()); };
    const int slot = session_.selectedSlot();

    if (d.trigger)
    {
        if (!on) return;
        write(index, 0.0f);   // momentary
    }
    if (isStepParam(index))
    {
        session_.setStepSlice(index - MP_STEP0, roundi(v) - 1);
        return;
    }
    if (index >= MP_VSTEP0 && index < MP_VSTEP0 + 16)
    {
        session_.setStepSlice(roundi(seen_[MP_SEQ_PAGE]) * 16 + index - MP_VSTEP0, roundi(v) - 1);
        return;
    }
    if (index == MP_SEQ_TAP)
    {
        const int k = roundi(v) - 1;
        if (k < 0) return;
        write(index, 0.0f);
        const int lane = k / 16, step = roundi(seen_[MP_SEQ_PAGE]) * 16 + k % 16;
        const auto& s = session_.slot(slot);
        if (s.content == nullptr || step >= s.program.pattern.numSteps) return;
        if (lane < 5)
        {
            static const Badge laneBadge[] = { Badge::kick, Badge::snare, Badge::ghost, Badge::hat, Badge::open };
            session_.toggleStep(step, laneBadge[lane]);
        }
        else
        {
            // FX lane: each tap moves on: off > STUTTER > REVERSE > STRETCH > TAPE STOP > off
            const auto t = (int)s.program.pattern.fx[(size_t)step].type;
            session_.setFxStep(step, (FxType)((t + 1) % kNumFxTypes), 16);
        }
        return;
    }
    if (index == MP_WAVE_TAP || index == MP_LIST_TAP)
    {
        const int k = roundi(v) - 1;
        if (k < 0) return;
        write(index, 0.0f);   // springs back, so the same place can be tapped again
        if (index == MP_LIST_TAP)
        {
            const int n = (int)session_.browserEntries().size();
            const int pick = listTop_ + k;
            if (pick < n) write(MP_BROWSE, (float)pick);
            syncList(true);
            return;
        }
        const auto& s = session_.slot(slot);
        if (s.content == nullptr) return;
        const View vw = viewOf(*s.content, session_.selectedSlice());
        const int at = vw.start + (int)((int64_t)vw.length * (2 * k + 1) / (2 * kWaveCols));
        session_.selectSlice(s.content->slices.sliceAt(at), true);
        return;
    }
    switch (index)
    {
        case MP_BREAK: session_.selectSlot(roundi(v)); break;
        case MP_BRK_PREV: session_.selectSlot((slot + kSlots - 1) % kSlots); break;
        case MP_BRK_NEXT: session_.selectSlot((slot + 1) % kSlots); break;
        case MP_ZOOM: break;   // syncWave follows
        case MP_SEQ_PAGE: case MP_SEQ_FOLLOW: break;   // syncSeq follows
        case MP_SHOW_PAT: case MP_SHOW_FX: case MP_SHOW_DISK:
        {
            // MAIN 0, FX 1, DISK 2, PATTERN 3 (the waveform's tap area shows for 0-2: one RangedEnabling)
            const int mine = index == MP_SHOW_PAT ? 3 : index == MP_SHOW_FX ? 1 : 2;
            if (on)
            {
                for (int i = MP_SHOW_PAT; i <= MP_SHOW_DISK; ++i) if (i != index && seen_[i] != 0.0f) write(i, 0.0f);
                write(MP_VIEW, (float)mine);
            }
            else if (roundi(seen_[MP_VIEW]) == mine) write(MP_VIEW, 0.0f);
            break;
        }
        case MP_SL_MOVE:
        {
            const auto& s = session_.slot(slot);
            if (s.content != nullptr) moveSelectedMarker((int)std::lround(v * (double)s.content->buffer->numSamples));
            break;
        }
        case MP_SL_NUDGE_L: case MP_SL_NUDGE_R:
        {
            const auto& s = session_.slot(slot);
            if (s.content == nullptr || s.content->slices.isEmpty()) break;
            const View vw = viewOf(*s.content, session_.selectedSlice());
            const int step = std::max(1, vw.length / (kWaveCols * 4));    // a quarter of a column of what is shown
            const int at = s.content->slices.starts[(size_t)session_.selectedSlice()];
            moveSelectedMarker(at + (index == MP_SL_NUDGE_L ? -step : step));
            break;
        }
        case MP_SL_ADD:
        {
            const auto& s = session_.slot(slot);
            if (s.content == nullptr) break;
            const int sel = session_.selectedSlice();
            const auto r = s.content->slices.region(sel, s.content->buffer->numSamples);
            if (r.length() < 2 * SliceTable::kMinGap) { say("SLICE TOO SHORT TO SPLIT"); break; }
            session_.addMarker(r.start + r.length() / 2);
            session_.selectSlice(sel + 1, false);
            say("SPLIT SLICE " + std::to_string(sel + 1));
            break;
        }
        case MP_SL_DEL:
        {
            const int sel = session_.selectedSlice();
            if (sel <= 0) { say("THE FIRST MARKER STAYS"); break; }
            session_.deleteMarker(sel);
            session_.selectSlice(sel - 1, false);
            say("JOINED SLICES " + std::to_string(sel) + " + " + std::to_string(sel + 1));
            break;
        }
        case MP_BROWSE: break;   // shown by formatValue; LOAD acts on it
        case MP_LOAD:
        {
            const auto entries = session_.browserEntries();
            const int k = roundi(values_[MP_BROWSE].load());
            if (k >= 0 && k < (int)entries.size())
            {
                session_.loadFile(slot, entries[(size_t)k].path);
                say("LOADING " + upper(entries[(size_t)k].name));
            }
            else say("PUT WAV / AIFF BREAKS IN CONTENT/BREAKS");
            break;
        }
        case MP_DEMO: session_.loadDemo(slot); say("DEMO BREAK"); break;
        case MP_AKAIZE:
        {
            const auto& s = session_.slot(slot);
            if (s.content == nullptr) { say("NO BREAK TO AKAIZE"); break; }
            const int cycle = std::clamp(roundi(values_[P_AK_CYCLE].load() * 0.001f * (float)s.content->buffer->sampleRate), 20, 2000);
            const double host = hostBpm_.load();
            say("AKAIZING...");
            syncReadouts(true);
            const bool ok = host > 1.0 ? session_.akaizeSlot(slot, host, 0.0, cycle)
                                       : session_.akaizeSlot(slot, 0.0, akRatioValue(roundi(values_[P_AK_RATIO].load())) * 100.0, cycle);
            char buf[64];
            if (ok && host > 1.0) std::snprintf(buf, sizeof buf, "AKAIZED TO %.1f BPM", host);
            else std::snprintf(buf, sizeof buf, ok ? "AKAIZED" : "AKAIZE FAILED");
            say(buf);
            break;
        }
        case MP_DICE_PAT:
            session_.rollPattern((Style)roundi(values_[P_STYLE].load()), values_[P_PAT_AMOUNT].load(), values_[P_LAYERS].load() > 0.5f);
            break;
        case MP_DICE_FX: session_.rollFx(values_[P_FX_AMOUNT].load()); break;
        case MP_UNDO: session_.historyBack(); break;
        case MP_REDO: session_.historyForward(); break;
        case MP_ORIGINAL: session_.setShowOriginal(on); break;
        case MP_PLAY: session_.setPlaying(on); break;
        case MP_SLICE: session_.selectSlice(roundi(v) - 1, true); break;
        case MP_EDIT_ALL: session_.setEditingAll(on); break;
        case MP_SL_PITCH: field(Session::SliceField::pitch); break;
        case MP_SL_GATE: field(Session::SliceField::gate); break;
        case MP_SL_STRETCH: field(Session::SliceField::stretch); break;
        case MP_SL_REVERSE: field(Session::SliceField::reverse); break;
        case MP_SL_STUTTER:
            session_.setSliceValue(Session::SliceField::stutter, (float)kStutterValues[std::clamp(roundi(v), 0, 5)], session_.editingAll());
            break;
        case MP_SL_TRANS: field(Session::SliceField::trans); break;
        case MP_SL_XFADE: field(Session::SliceField::xfade); break;
        case MP_SL_MUTE:
        {
            const auto& s = session_.slot(slot);
            const int k = session_.selectedSlice();
            if (s.program.slice[(size_t)k].mute != on) session_.toggleMute(k);
            break;
        }
        case MP_SL_BADGE: session_.setBadge(session_.selectedSlice(), (Badge)std::clamp(roundi(v), 0, kNumBadges - 1)); break;
        case MP_SLICING: case MP_SENS: case MP_GRID: break;   // RESLICE applies them
        case MP_RESLICE:
            session_.reslice(slot, roundi(values_[MP_SLICING].load()) == 1, values_[MP_SENS].load(),
                             kGridValues[std::clamp(roundi(values_[MP_GRID].load()), 0, 2)]);
            say("RESLICED");
            break;
        case MP_LENGTH: session_.setPatternLength(1 << std::clamp(roundi(v), 0, 2)); break;
        case MP_CLEAR: session_.clearPattern(); break;
        case MP_MEMORY: break;
        case MP_MEM_SAVE:
            session_.saveMemory(roundi(values_[MP_MEMORY].load()) - 1);
            say("SAVED TO M" + std::to_string(roundi(values_[MP_MEMORY].load())));
            break;
        case MP_MEM_RECALL:
        {
            const int m = roundi(values_[MP_MEMORY].load()) - 1;
            if (session_.slot(slot).memories[(size_t)std::clamp(m, 0, kMemories - 1)].has_value())
            {
                session_.recallMemory(m);
                say("RECALLED M" + std::to_string(m + 1));
            }
            else say("M" + std::to_string(m + 1) + " IS EMPTY");
            break;
        }
        default: break;
    }
}

void ChoppageMpcApp::syncMirrors(bool force)
{
    const uint32_t ver = session_.version();
    const int slot = session_.selectedSlot();
    const int slice = session_.selectedSlice();
    const bool all = session_.editingAll();
    if (!force && ver == lastVersion_ && slot == lastSlot_ && slice == lastSlice_ && all == lastEditAll_) return;
    lastVersion_ = ver; lastSlot_ = slot; lastSlice_ = slice; lastEditAll_ = all;

    const auto& s = session_.slot(slot);
    auto put = [this](int i, float v) { if (seen_[i] != v) write(i, v); };
    put(MP_BREAK, (float)slot);
    put(MP_SLICE, (float)(slice + 1));
    put(MP_EDIT_ALL, all ? 1.0f : 0.0f);
    put(MP_ORIGINAL, s.showOriginal ? 1.0f : 0.0f);
    put(MP_SL_PITCH, session_.getSliceValue(Session::SliceField::pitch, all));
    put(MP_SL_GATE, session_.getSliceValue(Session::SliceField::gate, all));
    put(MP_SL_STRETCH, session_.getSliceValue(Session::SliceField::stretch, all));
    put(MP_SL_REVERSE, session_.getSliceValue(Session::SliceField::reverse, all));
    put(MP_SL_STUTTER, (float)stutterChoice(roundi(session_.getSliceValue(Session::SliceField::stutter, all))));
    put(MP_SL_TRANS, session_.getSliceValue(Session::SliceField::trans, all));
    put(MP_SL_XFADE, session_.getSliceValue(Session::SliceField::xfade, all));
    put(MP_SL_MUTE, s.program.slice[(size_t)std::clamp(slice, 0, kMaxSlices - 1)].mute ? 1.0f : 0.0f);
    put(MP_SLICING, s.gridSlicing ? 1.0f : 0.0f);
    put(MP_SENS, s.sensitivity);
    put(MP_GRID, s.gridPerBar <= 8 ? 0.0f : s.gridPerBar <= 16 ? 1.0f : 2.0f);
    const int steps = s.program.pattern.numSteps;
    put(MP_LENGTH, steps <= 16 ? 0.0f : steps <= 32 ? 1.0f : 2.0f);

    // step text uses the hit letters of this break
    {
        std::lock_guard<std::mutex> l(textMutex_);
        badgeLetters_.clear();
        if (s.content != nullptr)
            for (int k = 0; k < s.content->numSlices(); ++k) badgeLetters_.push_back(badgeLetter(s.content->badges[(size_t)k]));
    }
    if (s.content != nullptr)
    {
        put(MP_SL_BADGE, (float)(int)s.content->badges[(size_t)std::clamp(slice, 0, kMaxSlices - 1)]);
        fallbackBpm_ = s.content->bpm;
        const int n = std::max(1, s.content->buffer->numSamples);
        const int at = s.content->slices.isEmpty() ? 0 : s.content->slices.starts[(size_t)std::clamp(slice, 0, s.content->slices.count - 1)];
        {
            std::lock_guard<std::mutex> l(textMutex_);
            char t[32];
            std::snprintf(t, sizeof t, "%.3f S", at / s.content->buffer->sampleRate);
            moveText_ = t;
        }
        put(MP_SL_MOVE, (float)at / (float)n);
    }
    {
        // one line of the LCD: the slice being edited (or ALL) and its settings, like the desktop's slice rows
        std::string line;
        char b[64];
        if (all) line = "ALL SLICES";
        else
        {
            std::snprintf(b, sizeof b, "SLICE %02d", slice + 1);
            line = b;
            if (s.content != nullptr && slice < s.content->numSlices())
                line += std::string(" ") + badgeLetter(s.content->badges[(size_t)slice]);
        }
        auto add = [&](const char* name, int idx) {
            std::string t;
            formatValue(idx, seen_[idx], t);
            line += std::string("   ") + name + " " + t;
        };
        add("PITCH", MP_SL_PITCH);
        add("GATE", MP_SL_GATE);
        add("STRETCH", MP_SL_STRETCH);
        add("REV", MP_SL_REVERSE);
        add("STUT", MP_SL_STUTTER);
        add("TRANS", MP_SL_TRANS);
        add("XFADE", MP_SL_XFADE);
        if (s.program.slice[(size_t)std::clamp(slice, 0, kMaxSlices - 1)].mute && !all) line += "   MUTED";
        setText(MP_INFO_SLPARAMS, line);
    }
    for (int st = 0; st < 64; ++st)
    {
        const int v = st < steps ? s.program.pattern.slice[(size_t)st] + 1 : 0;
        if (seen_[MP_STEP0 + st] != (float)v) write(MP_STEP0 + st, (float)v);
        else if (force) platform_.notifyParameter(MP_STEP0 + st, (float)v);   // letters may have changed
    }
    syncReadouts(true);
}

void ChoppageMpcApp::syncReadouts(bool force)
{
    const int slot = session_.selectedSlot();
    const auto& s = session_.slot(slot);
    auto& eng = session_.engine();
    char buf[200];

    // break name + BPM
    std::string name;
    switch (s.status)
    {
        case Session::Status::empty: name = "EMPTY"; break;
        case Session::Status::loading: name = "LOADING..."; break;
        case Session::Status::missing: name = "MISSING FILE"; break;
        case Session::Status::unreadable: name = "UNREADABLE FILE"; break;
        case Session::Status::ready: name = s.content != nullptr ? upper(s.content->name) : "EMPTY"; break;
    }
    std::snprintf(buf, sizeof buf, "BREAK %d/8  %s", slot + 1, name.c_str());
    setText(MP_INFO_NAME, buf);
    setText(MP_INFO_BRK, "BRK " + std::to_string(slot + 1));
    if (s.content != nullptr) std::snprintf(buf, sizeof buf, "%.1f BPM", s.content->bpm);
    else std::snprintf(buf, sizeof buf, "--- BPM");
    setText(MP_INFO_BPM, buf);

    // status line
    const double host = hostBpm_.load();
    const bool playing = eng.playingSlot.load() >= 0;
    if (s.content != nullptr)
    {
        char hostTxt[32];
        if (host > 1.0) std::snprintf(hostTxt, sizeof hostTxt, "MPC %.1f", host);
        else std::snprintf(hostTxt, sizeof hostTxt, "MPC --");
        std::snprintf(buf, sizeof buf, "%d SLICES  %d BARS  %s  %s%s", s.content->numSlices(), s.content->bars, hostTxt,
                      playing ? (eng.playingSlot.load() == slot ? "PLAYING" : "PLAYING OTHER") : "STOPPED",
                      s.showOriginal ? "  ORIGINAL" : "");
    }
    else std::snprintf(buf, sizeof buf, "PAD 1-8 PICKS A BREAK  LOAD FROM CONTENT/BREAKS");
    setText(MP_INFO_STATUS, buf);

    // the slice playing now
    const int now = eng.currentSlice.load();
    if (now >= 0 && s.content != nullptr && now < s.content->numSlices())
        std::snprintf(buf, sizeof buf, "S%02d %s", now + 1, badgeName(s.content->badges[(size_t)now]));
    else std::snprintf(buf, sizeof buf, "--");
    setText(MP_INFO_SLICE, buf);
    setText(MP_INFO_MSG, message_.empty() ? std::string("CHOPPAGE") : message_);

    // playhead through the pattern (only while this break plays)
    const int step = eng.playingSlot.load() == slot ? eng.currentStep.load() : -1;
    if (force || step != lastStepShown_)
    {
        lastStepShown_ = step;
        const int n = std::max(1, s.program.pattern.numSteps);
        const float pos = step < 0 ? 0.0f : clampf((step + 0.5f) / (float)n, 0.0f, 1.0f);
        if (seen_[MP_PLAYHEAD] != pos) write(MP_PLAYHEAD, pos);
    }

    // browser (BROWSE knob text)
    const auto entries = session_.browserEntries();
    if (force || (int)entries.size() != lastBrowserCount_)
    {
        {
            std::lock_guard<std::mutex> l(textMutex_);
            browser_ = entries;
        }
        lastBrowserCount_ = (int)entries.size();
        platform_.notifyParameter(MP_BROWSE, seen_[MP_BROWSE]);
    }
}

// ===================================================================================== waveform
ChoppageMpcApp::View ChoppageMpcApp::viewOf(const SlotContent& c, int sel)
{
    const int n = std::max(1, c.buffer->numSamples);
    const int zoom = roundi(values_[MP_ZOOM].load());
    const auto& t = c.slices;
    if (zoom == 0 || t.isEmpty()) return { 0, n };
    const auto r = t.region(sel, n);
    const int margin = std::max(256, r.length() / 4);
    View ideal { std::max(0, r.start - margin), 0 };
    ideal.length = std::max(kWaveCols, std::min(n, r.end + margin) - ideal.start);
    // keep the span still while the marker is edited inside it, so MOVE and NUDGE keep meaning the same places
    const int slot = session_.selectedSlot();
    const int marker = t.starts[(size_t)std::clamp(sel, 0, t.count - 1)];
    const bool same = viewSlot_ == slot && viewSel_ == sel && viewZoom_ == zoom && viewN_ == n;
    const int in0 = frozen_.start + frozen_.length / 10, in1 = frozen_.start + frozen_.length * 9 / 10;
    if (!same || frozen_.start + frozen_.length > n || marker < in0 || marker > in1)
    {
        frozen_ = ideal;
        viewSlot_ = slot; viewSel_ = sel; viewZoom_ = zoom; viewN_ = n;
    }
    return frozen_;
}

void ChoppageMpcApp::moveSelectedMarker(int position)
{
    const auto& s = session_.slot(session_.selectedSlot());
    if (s.content == nullptr || s.content->slices.isEmpty()) return;
    const auto& t = s.content->slices;
    const int k = std::clamp(session_.selectedSlice(), 0, t.count - 1);
    const int n = s.content->buffer->numSamples;
    const int lo = k > 0 ? t.starts[(size_t)k - 1] + SliceTable::kMinGap : 0;
    const int hi = (k + 1 < t.count ? t.starts[(size_t)k + 1] : n) - SliceTable::kMinGap;
    if (hi <= lo) return;
    const int pos = std::clamp(position, lo, hi);
    if (pos != t.starts[(size_t)k]) session_.moveMarker(k, pos);
}

void ChoppageMpcApp::syncWave(bool force)
{
    const int slot = session_.selectedSlot();
    const auto& s = session_.slot(slot);
    const int sel = session_.selectedSlice();
    const int theme = std::clamp(roundi(values_[P_LCD_COLOUR].load()), 0, 3);
    auto& eng = session_.engine();
    const int playing = eng.playingSlot.load() == slot ? eng.currentSlice.load() : -1;
    const uint64_t key = ((uint64_t)session_.version() << 32) ^ ((uint64_t)slot << 24) ^ ((uint64_t)(sel & 0xff) << 16)
                       ^ ((uint64_t)(playing & 0xff) << 8) ^ (uint64_t)(theme << 2) ^ (uint64_t)roundi(values_[MP_ZOOM].load())
                       ^ ((uint64_t)(s.content != nullptr ? s.content->slices.count : 0) << 40)
                       ^ ((uint64_t)(s.content != nullptr && !s.content->slices.isEmpty() ? s.content->slices.starts[(size_t)std::clamp(sel, 0, 63)] : 0) << 44);
    if (!force && key == waveKey_) return;
    waveKey_ = key;

    int wave[kWaveCols], tab[kWaveCols];
    for (int c = 0; c < kWaveCols; ++c) { wave[c] = waveFrame(theme, 0, 0, 0); tab[c] = tabFrame(theme, 0); }
    if (s.content != nullptr && s.content->buffer != nullptr && s.content->buffer->numSamples > 0)
    {
        const auto& b = *s.content->buffer;
        const auto& t = s.content->slices;
        const int n = b.numSamples;
        const View vw = viewOf(*s.content, sel);
        const auto whole = b.peakRange(0, n);
        const float maxPeak = std::max(1e-6f, std::max(std::fabs(whole.first), std::fabs(whole.second)));
        for (int c = 0; c < kWaveCols; ++c)
        {
            const int c0 = vw.start + (int)((int64_t)vw.length * c / kWaveCols);
            const int c1 = std::max(c0 + 1, vw.start + (int)((int64_t)vw.length * (c + 1) / kWaveCols));
            const auto r = b.peakRange(c0, std::min(n, c1));
            const float rel = std::max(std::fabs(r.first), std::fabs(r.second)) / maxPeak;
            const int level = rel < 0.01f ? 0 : std::clamp(1 + (int)(std::sqrt(rel) * (kWaveLevels - 1.01f)), 1, kWaveLevels - 1);
            const int mid = (c0 + c1) / 2;
            const int here = t.isEmpty() ? -1 : t.sliceAt(mid);
            const int look = here >= 0 && here == playing ? 2 : here >= 0 && here == sel ? 1 : 0;
            int mark = 0, startK = -1;
            for (int k = 0; k < t.count; ++k)
                if (t.starts[(size_t)k] >= c0 && t.starts[(size_t)k] < c1 && (startK < 0 || k == sel)) { mark = 1; startK = k; }
            wave[c] = waveFrame(theme, look, mark, level);
            if (startK >= 0)
                tab[c] = tabFrame(theme, 1 + (int)s.content->badges[(size_t)startK] + (startK == sel ? 6 : 0));
            else if (here >= 0)
                tab[c] = tabFrame(theme, here == sel ? 14 : 13);
        }
    }
    for (int c = 0; c < kWaveCols; ++c)
    {
        if (seen_[MP_WAVE0 + c] != (float)wave[c] || force) write(MP_WAVE0 + c, (float)wave[c]);
        if (seen_[MP_TAB0 + c] != (float)tab[c] || force) write(MP_TAB0 + c, (float)tab[c]);
    }
}

void ChoppageMpcApp::syncList(bool force)
{
    const auto entries = session_.browserEntries();
    const int count = (int)entries.size();
    const int pick = std::clamp(roundi(values_[MP_BROWSE].load()), 0, std::max(0, count - 1));
    // keep the picked file on screen, near the middle
    listTop_ = std::clamp(pick - kListLines / 2, 0, std::max(0, count - kListLines));
    const uint64_t key = ((uint64_t)count << 32) ^ ((uint64_t)pick << 16) ^ (uint64_t)listTop_;
    if (!force && key == listKey_) return;
    listKey_ = key;
    for (int k = 0; k < kListLines; ++k)
    {
        const int i = listTop_ + k;
        std::string line;
        if (count == 0 && k == 0) line = "PUT WAV / AIFF BREAKS IN CONTENT/BREAKS";
        else if (i < count)
        {
            std::string t;
            {
                std::lock_guard<std::mutex> l(textMutex_);
                t = browserText(i);
            }
            line = (i == pick ? "> " : "   ") + t;
        }
        setText(MP_LIST0 + k, line);
    }
}

// ===================================================================================== SEQ grid
void ChoppageMpcApp::syncSeq(bool force)
{
    const int slot = session_.selectedSlot();
    const auto& s = session_.slot(slot);
    const auto& pat = s.program.pattern;
    const int theme = std::clamp(roundi(values_[P_LCD_COLOUR].load()), 0, 3);
    auto& eng = session_.engine();
    const int now = eng.playingSlot.load() == slot ? eng.currentStep.load() : -1;
    const int pages = std::max(1, (pat.numSteps + 15) / 16);
    int page = std::clamp(roundi(seen_[MP_SEQ_PAGE]), 0, 3);
    if (seen_[MP_SEQ_FOLLOW] >= 0.5f && now >= 0 && now / 16 != page) page = now / 16;
    if (page >= pages) page = pages - 1;
    if ((float)page != seen_[MP_SEQ_PAGE]) write(MP_SEQ_PAGE, (float)page);

    const int head = now >= page * 16 && now < page * 16 + 16 ? now - page * 16 + 1 : 0;
    const float headF = (float)(theme * 17 + head);
    if (force || seen_[MP_SEQ_HEAD] != headF) write(MP_SEQ_HEAD, headF);

    const uint64_t key = ((uint64_t)session_.version() << 24) ^ ((uint64_t)slot << 16) ^ ((uint64_t)page << 8) ^ (uint64_t)theme;
    if (!force && key == seqKey_) return;
    seqKey_ = key;
    static const Badge laneBadge[] = { Badge::kick, Badge::snare, Badge::ghost, Badge::hat, Badge::open };
    auto laneOf = [](Badge b) { for (int i = 0; i < 5; ++i) if (laneBadge[i] == b) return i; return 1; };   // OTHER shows on SNARE
    for (int lane = 0; lane < kSeqLanes; ++lane)
        for (int col = 0; col < 16; ++col)
        {
            const int step = page * 16 + col;
            int c = col % 4 == 0 ? 1 : 0;
            if (step < pat.numSteps && s.content != nullptr)
            {
                if (lane < 5)
                {
                    const int sl = pat.slice[(size_t)step];
                    if (sl >= 0 && sl < s.content->numSlices() && laneOf(s.content->badges[(size_t)sl]) == lane) c = 2 + std::min(sl, 63);
                }
                else if (pat.fx[(size_t)step].type != FxType::none)
                {
                    const int r = pat.fx[(size_t)step].rate;
                    const int ri = r <= 8 ? 0 : r <= 16 ? 1 : r <= 32 ? 2 : 3;
                    c = 66 + ((int)pat.fx[(size_t)step].type - 1) * 4 + ri;
                }
            }
            const float f = (float)cellFrame(theme, c);
            const int idx = MP_CELL0 + lane * 16 + col;
            if (force || seen_[idx] != f) write(idx, f);
        }
    for (int k = 0; k < 16; ++k)
    {
        const int step = page * 16 + k;
        const float v = step < pat.numSteps ? (float)(pat.slice[(size_t)step] + 1) : 0.0f;
        if (force || seen_[MP_VSTEP0 + k] != v) write(MP_VSTEP0 + k, v);
    }
}

} // namespace bc
