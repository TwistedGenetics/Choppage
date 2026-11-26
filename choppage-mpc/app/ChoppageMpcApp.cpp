#include "ChoppageMpcApp.h"
#include "choppage/AudioFile.h"
#include "choppage/Preset.h"
#include "choppage/Params.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace bc {

// ===================================================================================== parameter table
namespace {

std::vector<int> buildQList(const std::vector<MpcParamDef>& v);

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

    C("break", "Break", { "1 C0", "2 D0", "3 E0", "4 F0", "5 G0", "6 A0", "7 B0", "8 C1" }, 0);
    I("browse", "Browse", 0, 255, 0);
    { auto* p = B("load", "Load", false); p->uiOnly = true; }   // the jog wheel's push flips it: every change loads
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
    C("slBadge", "Hit", { "K", "S", "GS", "H", "O", "P" }, 0);
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
    { auto* p = I("seqTap", "Grid Tap", 0, kSeqLanes * 16 + 16, 0); p->uiOnly = true; }
    { auto* p = I("seqHead", "Step Now", 0, 4 * 17 - 1, 0); p->readout = true; }
    for (int c = 0; c < kSeqLanes * 16; ++c)
    { auto* p = I(("cell" + std::to_string(c + 1)).c_str(), ("Cell " + std::to_string(c + 1)).c_str(), 0, kCellStripFrames - 1, 1); p->uiOnly = true; }
    for (int k = 0; k < 16; ++k)
        I(("vstep" + std::to_string(k + 1)).c_str(), ("Step " + std::to_string(k + 1)).c_str(), 0, 64, 0);
    R("infoBrk", "Break Now");
    F("slEnd", "End", 0, 1, 1);
    { auto* p = I("seqSel", "Step", 1, 64, 1); p->uiOnly = true; }
    C("stepFx", "Step FX", { "OFF", "STUTTER", "REVERSE", "STRETCH", "TAPE STOP" }, 0);
    C("stepRate", "Step FX Rate", { "1/8", "1/16", "1/32", "1/64" }, 1);
    for (int k = 0; k < 16; ++k)
    { auto* p = I(("fxrow" + std::to_string(k + 1)).c_str(), ("FX Step " + std::to_string(k + 1)).c_str(), 0, 4 * kFxRowFrames - 1, 0); p->readout = true; }
    C("muteGroup", "Mute Group", { "1", "2", "3", "4" }, 0);
    F("akrLoopBpm", "Loop BPM", 0.0f, 300.0f, 0.0f);
    { auto* p = F("slDrag", "Drag Start", 0.0f, 1.0f, 0.0f); p->hidden = true; }   // (the drag strip of 0.6.1 and 0.8.0 - 0.9.1 left the MPC screen blank)
    T("resetOrig", "Reset");
    T("memSaveOrig", "Save Original");
    { auto* p = I("slPlace", "Place Start", 0, kWaveCols, 0); p->uiOnly = true; }   // the touch columns under the waveform
    { auto* p = C("slMode", "Touch", { "SELECT", "ADD", "DEL" }, 0); p->uiOnly = true; }
    { auto* p = I("sliceTap", "Slice Tap", 0, kWaveCols, 0); p->uiOnly = true; }
    { auto* p = B("slPlay", "Play Slice", false); p->uiOnly = true; }   // the jog wheel's push flips it: every change plays
    B("snap", "Snap To Hits", true);
    { auto* p = I("preBrowse", "Preset", 0, 999, 0); p->uiOnly = true; }
    T("preLoad", "Load Preset");
    T("preSave", "Save New");
    T("preOver", "Save Over");
    R("preName", "Name");
    { auto* p = I("preKey", "Key", 0, 44, 0); p->uiOnly = true; }
    R("dir", "Folder");
    T("up", "Up");
    T("refresh", "Refresh");
    { auto* p = I("placeTap", "Place Tap", 0, kPlaces, 0); p->uiOnly = true; }
    { auto* p = B("placeStore", "Store Place", false); p->uiOnly = true; }
    for (int k = 0; k < kPlaces; ++k) R(("place" + std::to_string(k + 1)).c_str(), ("Place " + std::to_string(k + 1)).c_str());
    F("brkBpm", "Break BPM", 40.0f, 300.0f, 170.0f);
    T("winPrev", "Bars Earlier");
    T("winNext", "Bars Later");
    R("winInfo", "Bars Used");
    C("noteMap", "Note Map", { "C MAJOR", "CHROMATIC" }, 0);
    for (int k = 0; k < kQLinks; ++k) F(("q" + std::to_string(k + 1)).c_str(), ("Q-Link " + std::to_string(k + 1)).c_str(), 0.0f, 1.0f, 0.0f);
    for (int k = 0; k < kQLinks; ++k)
    { auto* p = I(("qt" + std::to_string(k + 1)).c_str(), ("Q-Link " + std::to_string(k + 1) + " Target").c_str(), 0, 255, 0); p->uiOnly = true; }
    // the knobs start on the sound and the dice (what Program mode had before 0.11)
    const auto list = buildQList(v);
    const char* start[kQLinks] = { "filter", "filterRes", "dist", "crush", "srate", "reverb", "echoMix", "volume",
                                   "patAmount", "fxAmount", "swing", "style", "stutRate", "rollRate", "tapeMs", "akCycLen" };
    for (int k = 0; k < kQLinks; ++k)
        for (size_t e = 1; e < list.size(); ++e)
            if (v[(size_t)list[e]].id == start[k]) v[(size_t)(MP_QT0 + k)].def = (float)e;
    return v;
}

// What a Q-Link can be set to turn: every knob, switch and choice a page has (not readouts, taps, keys, the step
// grid or the knobs themselves). Entry 0 is "none".
std::vector<int> buildQList(const std::vector<MpcParamDef>& v)
{
    std::vector<int> list { -1 };
    for (int i = 0; i < (int)v.size() && (int)list.size() < 256; ++i)
    {
        const auto& d = v[(size_t)i];
        if (d.hidden || d.readout || d.trigger || d.uiOnly) continue;
        if (i >= MP_QK0 && i < MP_QT_END) continue;
        if (isStepParam(i) || isPictureParam(i)) continue;
        if (i >= MP_CELL0 && i < MP_CELL0 + kSeqLanes * 16) continue;
        if (i >= MP_VSTEP0 && i < MP_VSTEP0 + 16) continue;
        list.push_back(i);
    }
    return list;
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
// a parameter's value as the MPC's 0-1 (with its skew) and back
static float toNorm(int index, float plain)
{
    const auto& d = mpcParamDef(index);
    const float lin = d.max > d.min ? std::clamp((plain - d.min) / (d.max - d.min), 0.0f, 1.0f) : 0.0f;
    return d.skew != 1.0f ? std::pow(lin, d.skew) : lin;
}
static float toPlain(int index, float norm)
{
    const auto& d = mpcParamDef(index);
    const float lin = d.skew != 1.0f ? std::pow(std::clamp(norm, 0.0f, 1.0f), 1.0f / d.skew) : std::clamp(norm, 0.0f, 1.0f);
    float p = d.min + lin * (d.max - d.min);
    if (d.kind != MpcKind::Float) p = std::round(p);
    return p;
}
int roundi(float v) { return (int)std::lround(v); }

const int kStutterValues[] = { 0, 2, 3, 4, 6, 8 };
int stutterChoice(int n) { for (int i = 0; i < 6; ++i) if (kStutterValues[i] == n) return i; return 0; }
const int kGridValues[] = { 8, 16, 32 };
const Badge kLaneBadge[kSeqLanes] = { Badge::kick, Badge::snare, Badge::ghost, Badge::hat, Badge::open, Badge::other };

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

std::vector<int> qLinkTargets(const std::vector<MpcParamDef>& table) { return buildQList(table); }

const std::vector<int>& qLinkList()
{
    static const std::vector<int> list = qLinkTargets(defs());
    return list;
}

const MpcParamDef& mpcParamDef(int index)
{
    const auto& d = defs();
    return d[(size_t)std::clamp(index, 0, (int)d.size() - 1)];
}

bool isStepParam(int index) { return index >= MP_STEP0 && index < MP_STEP0 + 64; }
bool isPictureParam(int index)
{
    return index == MP_PLAYHEAD || (index >= MP_WAVE0 && index < MP_TAB0 + kWaveCols) || index == MP_SEQ_HEAD
        || (index >= MP_FXROW0 && index < MP_FXROW0 + 16);
}

int padBaseNote(int choice)
{
    static const int base[] = { 36, 37, 48 };
    return base[std::clamp(choice, 0, 2)];
}

// ===================================================================================== app
ChoppageMpcApp::ChoppageMpcApp(IPlatform& platform, bool startWorker) : platform_(platform)
{
    static_assert(kMpcNumParams == MP_QT0 + kQLinks, "parameter table");
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
    setupFolders();
    choosePresetsFolder();
    refreshPresets();
    if (startWorker) worker_ = std::thread([this] { workerLoop(); });
}

// DISK lists the shared folder beside the plugins (VST2 Plugins/Content/Breaks), made on first use, so every
// plugin can share one set of breaks. Breaks still in the plugin's own Content/Breaks are listed while the
// shared folder has none.
static bool hasAudio(const std::string& dir)
{
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && AudioFile::isAudioFile(it->path().string())) return true;
    return false;
}

void ChoppageMpcApp::chooseBreaksFolder()
{
    const std::string own = platform_.contentDir() + "/Breaks";
    const std::string shared = platform_.sharedBreaksDir();
    breaksDir_ = own;
    breaksLabel_ = "CONTENT/BREAKS";
    if (shared.empty()) return;
    std::error_code ec;
    // never make folders in VST2 Plugins: only use the shared folder if it is already there
    if (!std::filesystem::is_directory(shared, ec)) return;
    if (!hasAudio(shared) && hasAudio(own)) return;     // not moved over yet: the old place
    breaksDir_ = shared;
    const auto parent = std::filesystem::path(shared).parent_path().parent_path().filename().string();
    breaksLabel_ = (parent.empty() ? std::string() : parent + "/") + "CONTENT/BREAKS";
    for (auto& c : breaksLabel_) c = (char)std::toupper((unsigned char)c);
}

// ===================================================================================== DISK: folders and places
// The DISK list shows one folder: ".." (up), its folders, then its breaks. It can go anywhere on the MPC's drives:
// the top is the folder that holds the drives (<top>/<drive>/VST2 Plugins/<this plugin>), where the list is the
// drives themselves. Four PLACE keys keep favourite folders. The places and the last folder are kept in
// <plugin>/Content/places.txt (never in VST2 Plugins itself).
static std::string cleanDir(std::string d)
{
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    return d;
}

void ChoppageMpcApp::setupFolders()
{
    namespace fs = std::filesystem;
    chooseBreaksFolder();                                   // the default: VST2 Plugins/Content/Breaks, else our own
    const fs::path pluginDir = fs::path(cleanDir(platform_.contentDir())).parent_path();
    const fs::path drive = pluginDir.parent_path().parent_path();
    topDir_ = drive.empty() ? std::string("/") : drive.has_parent_path() && drive.parent_path() != drive ? drive.parent_path().string() : drive.string();
    placesFile_ = platform_.contentDir() + "/places.txt";
    loadPlaces();
    std::error_code ec;
    if (browseDir_.empty() || !fs::is_directory(browseDir_, ec)) browseDir_ = breaksDir_;
    {
        std::lock_guard<std::mutex> l(textMutex_);
        if (places_[0].empty()) places_[0] = breaksDir_;    // PLACE 1 starts as the breaks folder
    }
    session_.listFolder(browseDir_, topDir_);
    syncPlaces();
}

void ChoppageMpcApp::loadPlaces()
{
    std::ifstream in(placesFile_);
    std::string line;
    std::lock_guard<std::mutex> l(textMutex_);
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("last ", 0) == 0) browseDir_ = line.substr(5);
        for (int k = 0; k < kPlaces; ++k)
        {
            const std::string key = "place" + std::to_string(k + 1) + " ";
            if (line.rfind(key, 0) == 0) places_[k] = line.substr(key.size());
        }
    }
}

void ChoppageMpcApp::savePlaces()
{
    std::string text = "last " + browseDir_ + "\n";
    {
        std::lock_guard<std::mutex> l(textMutex_);
        for (int k = 0; k < kPlaces; ++k)
            if (!places_[k].empty()) text += "place" + std::to_string(k + 1) + " " + places_[k] + "\n";
    }
    std::ofstream out(placesFile_, std::ios::binary | std::ios::trunc);
    out << text;
}

void ChoppageMpcApp::goFolder(const std::string& dir, bool remember)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) { message_ = "CAN'T OPEN THAT FOLDER"; messageTicks_ = 90; return; }
    browseDir_ = cleanDir(dir);
    session_.auditionStop();
    session_.listFolder(browseDir_, topDir_);
    write(MP_BROWSE, 0.0f);
    listKey_ = ~0ull;
    if (remember) savePlaces();
    syncPlaces();
}

void ChoppageMpcApp::placeTap(int k)
{
    if (k < 0 || k >= kPlaces) return;
    const std::string n = std::to_string(k + 1);
    if (values_[MP_PLACE_STORE].load() >= 0.5f)
    {
        {
            std::lock_guard<std::mutex> l(textMutex_);
            places_[k] = browseDir_;
        }
        write(MP_PLACE_STORE, 0.0f);
        savePlaces();
        syncPlaces();
        message_ = "PLACE " + n + " IS NOW " + upper(std::filesystem::path(browseDir_).filename().string());
        messageTicks_ = 120;
        return;
    }
    std::string dir;
    {
        std::lock_guard<std::mutex> l(textMutex_);
        dir = places_[k];
    }
    if (dir.empty()) { message_ = "PLACE " + n + " IS EMPTY: PRESS STORE, THEN THIS KEY"; messageTicks_ = 120; return; }
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) { message_ = "PLACE " + n + "'S FOLDER IS GONE (DRIVE NOT IN?)"; messageTicks_ = 120; return; }
    goFolder(dir);
}

void ChoppageMpcApp::syncPlaces()
{
    // the folder shown: its path below the drives ("MPC DRIVE/SAMPLES/BREAKS"), the end kept if it is long
    std::string shown = cleanDir(browseDir_);
    const std::string top = cleanDir(topDir_);
    if (shown == top) shown = "DRIVES";
    else if (shown.rfind(top + "/", 0) == 0) shown = shown.substr(top.size() + 1);
    else if (top == "/" && !shown.empty() && shown[0] == '/') shown = shown.substr(1);
    shown = upper(shown);
    if (shown.size() > 44) shown = "..." + shown.substr(shown.size() - 41);
    setText(MP_DIR, shown);
    std::string names[kPlaces];
    {
        std::lock_guard<std::mutex> l(textMutex_);
        for (int k = 0; k < kPlaces; ++k) names[k] = places_[k];
    }
    for (int k = 0; k < kPlaces; ++k)
    {
        std::string t = names[k].empty() ? std::string("EMPTY") : upper(std::filesystem::path(cleanDir(names[k])).filename().string());
        if (t.empty()) t = "/";
        if (t.size() > 14) t = t.substr(0, 13) + ".";
        setText(MP_PLACE0 + k, t);
    }
}

// A touch on the SLICE pages' waveform, by the TOUCH mode on the LCD:
//   SELECT  the slice under the finger (and it plays)
//   ADD     a new marker under the finger (SNAP: on the nearest drum hit); the new slice is selected and plays
//   DEL     the slice under the finger goes (joined to the one before; the first joins the second)
void ChoppageMpcApp::sliceTap(int column, bool doubleTap)
{
    auto say = [this](const std::string& m) { message_ = m; messageTicks_ = 90; };
    const auto& s = session_.slot(session_.selectedSlot());
    if (s.content == nullptr || s.content->slices.isEmpty()) return;
    const View vw = viewOf(*s.content, session_.selectedSlice());
    const int at = vw.start + (int)((int64_t)vw.length * (2 * column + 1) / (2 * kWaveCols));
    const int under = s.content->slices.sliceAt(at);
    // 0.11: a touch only selects (ADD / DELETE act on the selected slice); the old touch modes and double taps are off
    doubleTap = false;
    int mode = 0;
    if (doubleTap && mode == 0)
    {
        // a double tap in SELECT: on a marker (within about a column) it goes; anywhere else a marker is added there
        const double sr = s.content->buffer->sampleRate;
        const int near = std::max((int)(sr * 0.015), vw.length / kWaveCols + 1);
        int best = -1, bestD = near + 1;
        for (int k = 1; k < s.content->slices.count; ++k)
        {
            const int d = std::abs(s.content->slices.starts[(size_t)k] - at);
            if (d < bestD) { bestD = d; best = k; }
        }
        if (best > 0)
        {
            session_.deleteMarker(best);
            session_.selectSlice(best - 1, true);
            say("REMOVED THE MARKER - NOW SLICE " + std::to_string(best));
            return;
        }
        mode = 1;
    }
    switch (mode)
    {
        case 1:
        {
            if (s.content->slices.count >= kMaxSlices) { say("64 SLICES IS THE MOST"); return; }
            int pos = at;
            if (values_[MP_SNAP].load() >= 0.5f)
            {
                // look about a column either side (at least 10 ms, at most 60 ms)
                const double sr = s.content->buffer->sampleRate;
                const int radius = std::clamp(vw.length / kWaveCols, (int)(sr * 0.010), (int)(sr * 0.060));
                pos = session_.snapToHit(at, radius);
            }
            auto taken = [&](int p) {
                for (int k = 0; k < s.content->slices.count; ++k)
                    if (std::abs(s.content->slices.starts[(size_t)k] - p) < SliceTable::kMinGap) return true;
                return false;
            };
            if (taken(pos)) pos = at;          // the nearest hit already has a marker: put it where touched
            if (taken(pos)) { say("A MARKER IS ALREADY THERE"); return; }
            session_.addMarker(pos);
            const auto& c = *session_.slot(session_.selectedSlot()).content;
            const int added = c.slices.sliceAt(pos);
            session_.selectSlice(added, true);
            say("ADDED SLICE " + std::to_string(added + 1));
            return;
        }
        case 2:
        {
            if (s.content->slices.count <= 1) { say("THE LAST SLICE STAYS"); return; }
            const int marker = under > 0 ? under : 1;     // slice 1 has no marker of its own: it takes slice 2's
            session_.deleteMarker(marker);
            const int keep = std::max(0, marker - 1);
            session_.selectSlice(keep, true);
            say("DELETED - NOW SLICE " + std::to_string(keep + 1));
            return;
        }
        default:
            session_.selectSlice(under, true);
            return;
    }
}

// ===================================================================================== presets
// Presets live beside the plugins, VST2 Plugins/Content/Presets/CHOPPAGE (the same idea as the shared Breaks
// folder), or in the plugin's own Content/Presets where there is no shared place.
void ChoppageMpcApp::choosePresetsFolder()
{
    const std::string shared = platform_.sharedBreaksDir();
    presetsDir_ = shared.empty() ? platform_.contentDir() + "/Presets"
                                 : (std::filesystem::path(shared).parent_path() / "Presets" / "CHOPPAGE").string();
    // the folder is made by the first save, not here (nothing is created in VST2 Plugins on load)
    std::error_code ec;
    if (!shared.empty() && !std::filesystem::is_directory(std::filesystem::path(shared).parent_path(), ec))
        presetsDir_ = platform_.contentDir() + "/Presets";
}

void ChoppageMpcApp::refreshPresets()
{
    auto v = Preset::list(presetsDir_);
    std::lock_guard<std::mutex> l(textMutex_);
    presets_ = std::move(v);
}

void ChoppageMpcApp::loadPreset(int index)
{
    std::string path, name;
    {
        std::lock_guard<std::mutex> l(textMutex_);
        if (index < 0 || index >= (int)presets_.size()) { message_ = "NO PRESET TO LOAD"; messageTicks_ = 90; return; }
        path = presets_[(size_t)index].second;
        name = presets_[(size_t)index].first;
    }
    loadPresetFile(path, name);
}

// A preset file (from the PRESET list or the DISK list): the sound and settings, then the breaks and patterns.
void ChoppageMpcApp::loadPresetFile(const std::string& path, const std::string& name)
{
    std::string text;
    Preset::Parsed p;
    if (!Preset::readFile(path, text) || !Preset::parse(text, p)) { message_ = "CAN'T READ " + upper(name); messageTicks_ = 90; return; }
    // the sound and settings first (as the MPC's own knobs), then the breaks, slices and patterns
    const auto& defs = paramDefs();
    for (int i = 0; i < kNumCoreParams && i < (int)defs.size(); ++i)
    {
        const auto it = p.params.find(defs[(size_t)i].id);
        if (it != p.params.end() && values_[i].load() != it->second) write(i, it->second);
    }
    session_.loadState(p.session);
    resync_ = true;
    { std::lock_guard<std::mutex> l(textMutex_); presetName_ = p.name.empty() ? name : p.name; }
    message_ = "LOADED " + upper(p.name.empty() ? name : p.name);
    messageTicks_ = 90;
}

void ChoppageMpcApp::savePreset(int overIndex)
{
    std::string name, path;
    {
        std::lock_guard<std::mutex> l(textMutex_);
        name = presetName_;
        if (overIndex >= 0)
        {
            if (overIndex >= (int)presets_.size()) { message_ = "NO PRESET TO SAVE OVER"; messageTicks_ = 90; return; }
            path = presets_[(size_t)overIndex].second;
            if (name.empty()) name = presets_[(size_t)overIndex].first;
        }
    }
    if (name.empty())
    {
        // no name typed: the selected break's name
        const auto& s = session_.slot(session_.selectedSlot());
        name = s.content != nullptr ? upper(s.content->name) : std::string("CHOPPAGE");
    }
    if (path.empty()) path = Preset::freePath(presetsDir_, name);
    float p[kNumCoreParams];
    for (int i = 0; i < kNumCoreParams; ++i) p[i] = values_[i].load();
    if (!Preset::writeFile(path, Preset::make(name, p, session_.saveState()))) { message_ = "CAN'T SAVE (CARD FULL OR LOCKED?)"; messageTicks_ = 120; return; }
    refreshPresets();
    int at = 0;
    {
        std::lock_guard<std::mutex> l(textMutex_);
        for (int k = 0; k < (int)presets_.size(); ++k) if (presets_[(size_t)k].second == path) at = k;
    }
    write(MP_PRE_BROWSE, (float)at + (seen_[MP_PRE_BROWSE] == (float)at ? 0.001f : 0.0f));   // (moved a hair: the name redraws)
    // where it went (the DISK list shows presets too)
    std::string where = std::filesystem::path(presetsDir_).parent_path().filename().string() + "/" + std::filesystem::path(presetsDir_).filename().string();
    message_ = "SAVED " + upper(name) + " IN " + upper(where);
    messageTicks_ = 150;
}

// The name keyboard: words start with a capital (CAPS: all capitals).
void ChoppageMpcApp::typeKey(int key)
{
    static const char* rows = "1234567890QWERTYUIOPASDFGHJKL&ZXCVBNM-.'";
    std::lock_guard<std::mutex> l(textMutex_);
    auto& n = presetName_;
    if (key == 42) { if (!n.empty()) n.pop_back(); return; }
    if (key == 43) { n.clear(); return; }
    if (key == 44) { caps_ = !caps_; return; }
    if ((int)n.size() >= kPresetNameMax) return;
    if (key == 41) { if (!n.empty() && n.back() != ' ') n += ' '; return; }
    if (key < 1 || key > 40) return;
    char c = rows[key - 1];
    if (c >= 'A' && c <= 'Z' && !caps_ && !(n.empty() || n.back() == ' ' || n.back() == '-')) c = (char)(c - 'A' + 'a');
    n += c;
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
    const bool cMajor = roundi(values_[MP_NOTE_MAP].load(std::memory_order_relaxed)) == 0;
    s.noteMap = cMajor ? 1 : 0;
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
    // (CHROMATIC only: in C MAJOR the notes are taken as they come)
    const int base = cMajor ? 36 : padBaseNote(roundi(values_[MP_PAD_NOTES].load(std::memory_order_relaxed)));
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
    if (index < 0 || index >= kMpcNumParams) return;
    const float before = values_[index].exchange(plain, std::memory_order_relaxed);
    // a key (ADD, DELETE, UP ...): the press is queued, so a tap shorter than the worker's tick (33 ms) still acts
    if (mpcParamDef(index).trigger && plain >= 0.5f && before < 0.5f)
    {
        std::lock_guard<std::mutex> l(tapMutex_);
        if (taps_.size() < 32) taps_.push_back({ index, 1.0f, std::chrono::steady_clock::now() });
        return;
    }
    if ((index == MP_SLICE_TAP || index == MP_WAVE_TAP || index == MP_PRE_KEY || index == MP_PLACE_TAP || index == MP_LIST_TAP || index == MP_SEQ_TAP) && plain >= 0.5f)
    {
        // a touch: kept until the worker's next tick, so a tap shorter than a tick still counts
        std::lock_guard<std::mutex> l(tapMutex_);
        if (taps_.size() < 32) taps_.push_back({ index, plain, std::chrono::steady_clock::now() });
    }
}

float ChoppageMpcApp::getParam(int index) const
{
    return index >= 0 && index < kMpcNumParams ? values_[index].load(std::memory_order_relaxed) : 0.0f;
}

bool ChoppageMpcApp::formatValue(int index, float plain, std::string& out) const
{
    if (index < 0 || index >= kMpcNumParams) return false;
    if (index < kNumCoreParams) { out = formatParam(index, plain); return true; }
    if (index >= MP_QT0 && index < MP_QT0 + kQLinks)
    {
        const auto& list = qLinkList();
        const int e = roundi(plain);
        out = e > 0 && e < (int)list.size() ? upper(mpcParamDef(list[(size_t)e]).name) : std::string("NONE");
        return true;
    }
    if (index >= MP_QK0 && index < MP_QK0 + kQLinks)
    {
        const int t = qTarget(index - MP_QK0);
        if (t < 0) { out = "-"; return true; }
        std::string val;
        formatValue(t, values_[t].load(std::memory_order_relaxed), val);
        out = upper(mpcParamDef(t).name) + " " + val;
        if (out.size() > 31) out = out.substr(0, 31);
        return true;
    }
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
        case MP_SL_MOVE: case MP_SL_DRAG: { std::lock_guard<std::mutex> l(textMutex_); out = moveText_; return true; }
        case MP_SL_END: { std::lock_guard<std::mutex> l(textMutex_); out = endText_; return true; }
        case MP_SEQ_SEL: std::snprintf(buf, sizeof buf, "STEP %d", roundi(plain)); out = buf; return true;
        case MP_MEMORY: std::snprintf(buf, sizeof buf, "M%02d", roundi(plain)); out = buf; return true;
        case MP_PRE_BROWSE:
        {
            std::lock_guard<std::mutex> l(textMutex_);
            const int k = roundi(plain);
            out = presets_.empty() ? std::string("NO PRESETS YET") : k < (int)presets_.size() ? upper(presets_[(size_t)k].first) : std::string("-");
            return true;
        }
        case MP_PRE_KEY:
        {
            const int k = roundi(plain);
            static const char* rows = "1234567890QWERTYUIOPASDFGHJKL&ZXCVBNM-.'";
            out = k >= 1 && k <= 40 ? std::string(1, rows[k - 1]) : k == 41 ? "SPACE" : k == 42 ? "DEL" : k == 43 ? "CLEAR" : k == 44 ? "CAPS" : "-";
            return true;
        }
        case MP_BRK_BPM: std::snprintf(buf, sizeof buf, "%.1f BPM", plain); out = buf; return true;
        case MP_AKR_LOOP_BPM:
            if (plain < 0.05f) out = "AUTO"; else { std::snprintf(buf, sizeof buf, "%.1f", plain); out = buf; }
            return true;
        default: break;
    }
    if (index >= MP_CELL0 && index < MP_CELL0 + kSeqLanes * 16)
    {
        std::lock_guard<std::mutex> l(textMutex_);
        out = cellText_[(size_t)(index - MP_CELL0)];
        return true;
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
    if (browser_.empty()) return "EMPTY FOLDER";
    if (k < 0 || k >= (int)browser_.size()) return "-";
    const auto& e = browser_[(size_t)k];
    if (e.isUp) return "[ .. ]  UP A FOLDER";
    if (e.isDir) return "[ " + upper(e.name) + " ]";
    if (e.isPreset) return "PRESET  " + upper(e.name);
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
    resync_ = true;       // the worker takes the restored values as they are and resyncs every mirror
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
    if (resync_.exchange(false))
    {
        // a saved state came back: take the host's values as they are (nothing fires on project load), then
        // put every mirror and picture right from the session
        for (int i = kMpcReserveEnd; i < kMpcNumParams; ++i) seen_[i] = values_[i].load(std::memory_order_relaxed);
        lastVersion_ = ~0u; seqKey_ = ~0ull; waveKey_ = ~0ull; listKey_ = ~0ull;
    }
    {
        std::vector<Tap> taps;
        { std::lock_guard<std::mutex> l(tapMutex_); taps.swap(taps_); }
        for (const auto& t : taps)
        {
            if (t.index == MP_SLICE_TAP && roundi(t.value) >= 1)
            {
                // two taps close together in time and place: a double tap
                const int col = roundi(t.value) - 1;
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.when - lastTapWhen_).count();
                const bool dbl = haveLastTap_ && ms >= 0 && ms < 450 && std::abs(col - lastTapCol_) <= 2;
                haveLastTap_ = !dbl;
                lastTapWhen_ = t.when;
                lastTapCol_ = col;
                sliceTap(col, dbl);
                continue;
            }
            handle(t.index, t.value, 0.0f);
        }
        if (!taps.empty())
            for (int i : { (int)MP_SLICE_TAP, (int)MP_WAVE_TAP, (int)MP_PRE_KEY, (int)MP_PLACE_TAP, (int)MP_LIST_TAP, (int)MP_SEQ_TAP }) seen_[i] = values_[i].load(std::memory_order_relaxed);
    }
    // a slice pad was played: the screen goes to that slice (of that break)
    if (const int hit = session_.engine().sliceHit.exchange(-1, std::memory_order_relaxed); hit >= 0)
    {
        if (hit / kMaxSlices != session_.selectedSlot()) session_.selectSlot(hit / kMaxSlices);
        session_.selectSlice(hit % kMaxSlices, false);
    }
    for (int i = kMpcReserveEnd; i < kMpcNumParams; ++i)
    {
        if (i == MP_SLICE_TAP || i == MP_WAVE_TAP || i == MP_PRE_KEY || i == MP_PLACE_TAP || i == MP_LIST_TAP || i == MP_SEQ_TAP
            || mpcParamDef(i).trigger) { seen_[i] = values_[i].load(std::memory_order_relaxed); continue; }   // (the queue above)
        const float v = values_[i].load(std::memory_order_relaxed);
        if (v == seen_[i]) continue;
        const float prev = seen_[i];
        seen_[i] = v;
        if (!mpcParamDef(i).readout) handle(i, v, prev);
    }

    // CYCLE FREQ follows CYCLE LENGTH unless MANUAL is on (Akaizer's linked frequency)
    for (auto [len, freq, man] : { std::array<int, 3>{ P_AK_CYC_LEN, P_AK_CYC_FREQ, P_AK_FREQ_MAN },
                                   std::array<int, 3>{ P_SL_CYC_LEN, P_SL_CYC_FREQ, P_SL_FREQ_MAN },
                                   std::array<int, 3>{ P_AKR_CYC_LEN, P_AKR_CYC_FREQ, P_AKR_FREQ_MAN } })
    {
        if (values_[man].load() > 0.5f) continue;
        const float linked = (float)akaizeFreq(values_[len].load(), values_[freq].load(), false);
        // (the MPC hands a skewed value back a hair off what was written: compare loosely, or this re-sends every tick)
        if (std::fabs(values_[freq].load() - linked) > 0.25f) write(freq, linked);
    }

    if (dragTicks_ > 0 && --dragTicks_ == 0)
    {
        // the drag ended: with SNAP, the marker jumps to the drum hit nearest where it was let go
        if (dragMoved_ && values_[MP_SNAP].load() >= 0.5f)
        {
            const auto& s = session_.slot(session_.selectedSlot());
            const int k = session_.selectedSlice();
            if (s.content != nullptr && k > 0 && k < s.content->slices.count)
            {
                const int at = s.content->slices.starts[(size_t)k];
                const int snapped = session_.snapToHit(at, (int)(s.content->buffer->sampleRate * 0.012));
                if (snapped != at) moveMarkerOf(k, snapped);
            }
        }
        dragMoved_ = false;
        lastVersion_ = ~0u;   // the strip goes back onto the marker
    }

    // 2. the session moved: mirrors and readouts follow
    syncMirrors(false);
    syncReadouts(false);
    syncWave(false);
    syncList(false);
    syncSeq(false);
    syncAudition();
    syncQLinks();
    if (messageTicks_ > 0 && --messageTicks_ == 0) message_.clear();
}

// The data wheel moves a parameter by a fixed fraction of its range (coarse; fine with SHIFT), so a list of 255
// moves 2 at a time and times and tempos land between whole numbers. For these parameters each move becomes an
// exact step instead: coarse or fine (told apart by the size of the MPC's move), on a grid of that step.
// A big jump (a touch, the overlay knob) is taken as it is, only put on the fine grid.
float ChoppageMpcApp::jogged(int index, float v, float prev) const
{
    double fine = 0, coarse = 0, scale = 1;              // in the parameter's own units (scale: units per 1.0)
    if (index >= MP_QT0 && index < MP_QT0 + kQLinks) { fine = coarse = 1; }
    else switch (index)
    {
        case MP_BROWSE: case MP_PRE_BROWSE: fine = coarse = 1; break;
        case MP_AKR_LOOP_BPM: case MP_BRK_BPM: fine = 0.1; coarse = 1; break;
        case MP_SL_MOVE: case MP_SL_END:
        {
            const auto& s = session_.slot(session_.selectedSlot());
            if (s.content == nullptr || s.content->buffer == nullptr || s.content->buffer->sampleRate <= 0) return v;
            scale = (double)s.content->buffer->numSamples / s.content->buffer->sampleRate * 1000.0;   // ms across the break
            // ZOOM ALL: 10 ms a click, 1 ms with SHIFT; ZOOM SLICE (getting onto a transient): 1 ms, 0.1 ms with SHIFT
            if (roundi(values_[MP_ZOOM].load()) == 1) { fine = 0.1; coarse = 1; }
            else { fine = 1; coarse = 10; }
            break;
        }
        default: return v;
    }
    const auto& d = mpcParamDef(index);
    const double range = std::max(1e-9, (double)(d.max - d.min));
    const double dn = ((double)v - prev) / range;
    if (std::fabs(dn) < 1e-7) return v;
    const double was = prev * scale;
    double now;
    if (std::fabs(dn) > 0.04) now = std::round(v * scale / fine) * fine;
    else
    {
        const double step = std::fabs(dn) < 0.003 ? fine : coarse;
        now = std::round((was + (dn > 0 ? step : -step)) / step) * step;
        if (std::fabs(now - was) < 1e-9) now = was + (dn > 0 ? step : -step);
    }
    now /= scale;
    return (float)std::clamp(now, (double)d.min, (double)d.max);
}

// ===================================================================================== Q-Links
// The MPC's 16 Q-Link knobs turn MP_QK0.. on every page; each goes on to the parameter picked for it (Q-LINKS page).
int ChoppageMpcApp::qTarget(int k) const
{
    const auto& list = qLinkList();
    const int e = roundi(values_[MP_QT0 + k].load(std::memory_order_relaxed));
    return e > 0 && e < (int)list.size() ? list[(size_t)e] : -1;
}

void ChoppageMpcApp::qLinkTurned(int k, float v, float prev)
{
    const int t = qTarget(k);
    if (t < 0) return;
    const auto& d = mpcParamDef(t);
    float plain;
    if (d.kind == MpcKind::Float) plain = toPlain(t, v);
    else
    {
        // a switch or a list: one step a move, whichever way the knob went (the knob then shows where it is)
        const float now = values_[t].load(std::memory_order_relaxed);
        if (std::fabs(v - prev) < 1e-6f) return;
        plain = std::clamp(std::round(now) + (v > prev ? 1.0f : -1.0f), d.min, d.max);
    }
    values_[t].store(plain, std::memory_order_relaxed);
    platform_.notifyParameter(t, plain);
    qMoved_[k] = 3;                                     // the knob leads for a few ticks: no echo back onto it
}

void ChoppageMpcApp::syncQLinks()
{
    for (int k = 0; k < kQLinks; ++k)
    {
        if (qMoved_[k] > 0) { --qMoved_[k]; if (mpcParamDef(qTarget(k) < 0 ? 0 : qTarget(k)).kind == MpcKind::Float) continue; }
        const int t = qTarget(k);
        if (t < 0) continue;
        const float n = toNorm(t, values_[t].load(std::memory_order_relaxed));
        if (std::fabs(n - seen_[MP_QK0 + k]) > 0.002f) write(MP_QK0 + k, n);
    }
}

void ChoppageMpcApp::handle(int index, float v, float prev)
{
    if (const float j = jogged(index, v, prev); j != v) { write(index, j); v = j; }
    if (index >= MP_QK0 && index < MP_QK0 + kQLinks) { qLinkTurned(index - MP_QK0, v, prev); return; }
    if (index >= MP_QT0 && index < MP_QT0 + kQLinks)
    {
        const int k = index - MP_QT0, t = qTarget(k);
        message_ = "Q-LINK " + std::to_string(k + 1) + (t < 0 ? std::string(" DOES NOTHING") : " TURNS " + upper(mpcParamDef(t).name));
        messageTicks_ = 90;
        qMoved_[k] = 0;
        return;
    }
    auto say = [this](const std::string& m) { message_ = m; messageTicks_ = 90; };
    const auto& d = mpcParamDef(index);
    const bool on = v >= 0.5f;
    auto field = [this, v](Session::SliceField f) { session_.setSliceValue(f, v, session_.editingAll()); };
    const int slot = session_.selectedSlot();

    // Keys and taps are momentary on the screen: the MPC puts them back to their default (0) when the finger
    // lifts (ResetOnGestureEnd). The app acts on the press and never writes them itself, so a saved state always
    // matches what the host holds.
    if (d.trigger && !(on && prev < 0.5f)) return;
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
        const int lane = k / 16, step = roundi(seen_[MP_SEQ_PAGE]) * 16 + k % 16;
        const auto& s = session_.slot(slot);
        if (s.content == nullptr || step >= s.program.pattern.numSteps) return;
        write(MP_SEQ_SEL, (float)(step + 1));                 // the step STEP FX / RATE edit
        if (lane < kSeqLanes) session_.toggleStep(step, kLaneBadge[lane]);   // the FX row only selects
        return;
    }
    if (index >= MP_CELL0 && index < MP_CELL0 + kSeqLanes * 16)
    {
        // the jog wheel on a focused cell: through the slices of that lane's hit only (K in KICK, P in PERC ...)
        const int dir = v > cellShown_[index - MP_CELL0] ? 1 : -1;
        const int lane = (index - MP_CELL0) / 16, step = roundi(seen_[MP_SEQ_PAGE]) * 16 + (index - MP_CELL0) % 16;
        const auto& s = session_.slot(slot);
        if (s.content == nullptr || step >= s.program.pattern.numSteps) { seqKey_ = ~0ull; return; }
        std::vector<int> pool;
        for (int k2 = 0; k2 < s.content->numSlices(); ++k2) if (s.content->badges[(size_t)k2] == kLaneBadge[lane]) pool.push_back(k2);
        write(MP_SEQ_SEL, (float)(step + 1));
        if (pool.empty()) { say(std::string("NO ") + badgeName(kLaneBadge[lane]) + " SLICES IN THIS BREAK"); seqKey_ = ~0ull; return; }
        const int cur = s.program.pattern.slice[(size_t)step];
        int pos = -1;
        for (size_t i = 0; i < pool.size(); ++i) if (pool[i] == cur) pos = (int)i;
        const int n = (int)pool.size();
        const int next = pos < 0 ? (dir > 0 ? 0 : n - 1) : (pos + dir + n) % n;
        session_.setStepSlice(step, pool[(size_t)next]);
        seqKey_ = ~0ull;   // put the right frame back even if nothing changed
        return;
    }
    if (index == MP_PRE_KEY)
    {
        typeKey(roundi(v));
        return;
    }
    if (index == MP_PLACE_TAP)
    {
        if (roundi(v) >= 1) placeTap(roundi(v) - 1);
        return;
    }
    if (index == MP_SLICE_TAP)
    {
        if (roundi(v) >= 1) sliceTap(roundi(v) - 1);
        return;
    }
    if (index == MP_WAVE_TAP || index == MP_LIST_TAP)
    {
        const int k = roundi(v) - 1;
        if (k < 0) return;
        if (index == MP_LIST_TAP)
        {
            const int n = (int)session_.browserEntries().size();
            const int pick = listTop_ + k;
            // a folder opens at a touch; a break plays at the first touch and loads at the second
            const auto entries = session_.browserEntries();
            if (pick < (int)entries.size() && entries[(size_t)pick].isDir) { goFolder(entries[(size_t)pick].path); return; }
            if (pick < (int)entries.size() && pick == roundi(seen_[MP_BROWSE]) && entries[(size_t)pick].isPreset)
            {
                loadPresetFile(entries[(size_t)pick].path, entries[(size_t)pick].name);
                return;
            }
            if (pick < (int)entries.size() && pick == roundi(seen_[MP_BROWSE]))
            {
                session_.auditionStop();
                session_.loadFile(slot, entries[(size_t)pick].path);
                message_ = "LOADING " + upper(entries[(size_t)pick].name) + " INTO BREAK " + std::to_string(slot + 1);
                messageTicks_ = 90;
                return;
            }
            if (pick < n) { write(MP_BROWSE, (float)pick); browsePickSeen_ = -1; }   // (a touch plays it, on any page)
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
        case MP_ZOOM: lastVersion_ = ~0u; break;   // syncWave follows (and the drag strip)
        case MP_MUTE_GROUP: session_.setMuteGroup(slot, roundi(v)); break;
        case MP_WIN_PREV: case MP_WIN_NEXT:
        {
            const int n = session_.fullBars(slot);
            if (n <= Session::kWindowBars) { say(n > 0 ? "THIS BREAK IS " + std::to_string(n) + " BAR" + (n > 1 ? "S" : "") + ": ALL OF IT IS USED" : "NO BREAK HERE"); break; }
            const int w = std::clamp(session_.window(slot) + (index == MP_WIN_NEXT ? 1 : -1), 0, n - Session::kWindowBars);
            if (w == session_.window(slot)) { say(index == MP_WIN_NEXT ? "THESE ARE THE LAST BARS" : "THESE ARE THE FIRST BARS"); break; }
            session_.setWindow(slot, w);
            say("USING BARS " + std::to_string(w + 1) + "-" + std::to_string(w + Session::kWindowBars) + " OF " + std::to_string(n) + " (SLICED AGAIN)");
            break;
        }
        case MP_BRK_BPM:
            if (session_.slot(slot).content == nullptr) { say("NO BREAK HERE"); lastVersion_ = ~0u; break; }
            session_.setBreakBpm(slot, v);
            say("BREAK " + std::to_string(slot + 1) + " PLAYS AS " + (std::fabs(v - std::round(v)) < 0.05f ? std::to_string((int)std::lround(v)) : std::to_string((int)v) + "." + std::to_string((int)std::lround(std::fabs(v - (int)v) * 10.0f) % 10)) + " BPM");
            break;
        case MP_AKR_LOOP_BPM:
            // AUTO at the bottom; the first nudge up from AUTO starts at the break's own tempo
            if (v < 0.05f) session_.setLoopBpm(slot, 0.0);
            else if (!session_.loopBpmSet(slot) && v < 40.0f) session_.setLoopBpm(slot, std::round(session_.loopBpm(slot) * 10.0) / 10.0);
            else session_.setLoopBpm(slot, v);
            break;
        case MP_SEQ_PAGE: case MP_SEQ_FOLLOW: case MP_SEQ_SEL: seqKey_ = ~0ull; lastVersion_ = ~0u; break;   // syncs follow
        case MP_STEP_FX: case MP_STEP_RATE:
        {
            const auto& s = session_.slot(slot);
            const int step = roundi(seen_[MP_SEQ_SEL]) - 1;
            if (s.content == nullptr || step < 0 || step >= s.program.pattern.numSteps) break;
            static const int rates[] = { 8, 16, 32, 64 };
            session_.setFxStep(step, (FxType)std::clamp(roundi(seen_[MP_STEP_FX]), 0, kNumFxTypes - 1),
                               rates[std::clamp(roundi(seen_[MP_STEP_RATE]), 0, 3)]);
            break;
        }
        case MP_SL_END:
        {
            const auto& s = session_.slot(slot);
            if (s.content == nullptr) break;
            const int k = session_.selectedSlice();
            if (k + 1 >= s.content->slices.count) { say("THE LAST SLICE ENDS WITH THE BREAK"); lastVersion_ = ~0u; break; }
            moveMarkerOf(k + 1, (int)std::lround(v * (double)s.content->buffer->numSamples));
            break;
        }
        case MP_SHOW_PAT: case MP_SHOW_FX: case MP_SHOW_DISK:
        {
            // MAIN 0, FX 1, DISK 2, PATTERN 3 (the waveform's tap area shows for 0-2: one RangedEnabling)
            const int mine = index == MP_SHOW_PAT ? 3 : index == MP_SHOW_FX ? 1 : 2;
            if (on)
            {
                for (int i = MP_SHOW_PAT; i <= MP_SHOW_DISK; ++i) if (i != index && seen_[i] != 0.0f) write(i, 0.0f);
                write(MP_VIEW, (float)mine);
                if (mine == 2) session_.listFolder(browseDir_, topDir_);   // DISK: new files show
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
        case MP_SL_PLACE:
        {
            // a touch on the strip under the waveform: the selected slice's start goes to that column of the
            // waveform as shown (the view holds still for a moment, so touches in a row land where they look)
            const int col = roundi(v) - 1;
            const auto& s = session_.slot(slot);
            if (col < 0 || s.content == nullptr || s.content->slices.isEmpty()) break;
            const View vw = viewOf(*s.content, session_.selectedSlice());
            dragTicks_ = 30;
            moveSelectedMarker(vw.start + (int)std::lround(((double)col + 0.5) * (double)vw.length / (double)kWaveCols));
            break;
        }
        case MP_SL_DRAG:
        {
            // the finger on the strip under the waveform: the selected slice's start goes where it is, in the
            // waveform as shown (ALL or the zoomed slice; the view holds still while the finger moves)
            const auto& s = session_.slot(slot);
            if (s.content == nullptr || s.content->slices.isEmpty()) break;
            const View vw = viewOf(*s.content, session_.selectedSlice());
            dragTicks_ = 20;
            dragMoved_ = true;
            moveSelectedMarker(vw.start + (int)std::lround(std::clamp(v, 0.0f, 1.0f) * (double)vw.length));
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
        case MP_SL_MODE:
            say(roundi(v) == 1 ? "TOUCH THE WAVE TO ADD A SLICE" : roundi(v) == 2 ? "TOUCH A SLICE TO DELETE IT" : "TOUCH A SLICE TO SELECT IT");
            break;
        case MP_SL_PLAY:
            if (session_.slot(slot).content != nullptr) session_.selectSlice(session_.selectedSlice(), true);
            break;
        case MP_SNAP: break;
        case MP_PRE_BROWSE:
        {
            // shown by formatValue; LOAD / SAVE OVER act on it. Kept inside the list (read again: files copied in show).
            refreshPresets();
            int n;
            { std::lock_guard<std::mutex> l(textMutex_); n = (int)presets_.size(); }
            if (roundi(v) > std::max(0, n - 1)) write(MP_PRE_BROWSE, (float)std::max(0, n - 1));
            break;
        }
        case MP_PRE_LOAD: loadPreset(roundi(values_[MP_PRE_BROWSE].load())); break;
        case MP_PRE_SAVE: savePreset(-1); break;
        case MP_PRE_OVER: savePreset(roundi(values_[MP_PRE_BROWSE].load())); break;
        case MP_SL_ADD:
        {
            const auto& s = session_.slot(slot);
            if (s.content == nullptr) break;
            // the selected slice (a touch or its pad) is split at the strongest hit inside it (not right at its edges)
            const int sel = session_.selectedSlice();
            const auto r = s.content->slices.region(sel, s.content->buffer->numSamples);
            const int edge = std::max(SliceTable::kMinGap, (int)(s.content->buffer->sampleRate * 0.015));
            if (r.length() < 2 * edge + 64) { say("SLICE " + std::to_string(sel + 1) + " IS TOO SHORT TO SPLIT"); break; }
            bool found = false;
            const int mid = r.start + r.length() / 2;
            int at = session_.snapToHit(mid, r.length() / 2 - edge, &found);
            if (!found || at < r.start + edge || at > r.end - edge) { at = mid; found = false; }
            session_.addMarker(at);
            session_.selectSlice(sel, false);
            say(found ? "SPLIT SLICE " + std::to_string(sel + 1) + " AT ITS HIT" : "NO HIT IN SLICE " + std::to_string(sel + 1) + ": SPLIT IN THE MIDDLE");
            break;
        }
        case MP_SL_DEL:
        {
            // the selected slice goes: it joins the one before (slice 1 takes in slice 2)
            const auto& s = session_.slot(slot);
            if (s.content == nullptr || s.content->slices.count < 2) { say("ONE SLICE LEFT: IT STAYS"); break; }
            const int sel = session_.selectedSlice();
            if (sel <= 0)
            {
                session_.deleteMarker(1);
                session_.selectSlice(0, false);
                say("DELETED SLICE 2 INTO SLICE 1");
                break;
            }
            session_.deleteMarker(sel);
            session_.selectSlice(sel - 1, false);
            say("DELETED SLICE " + std::to_string(sel + 1) + " (JOINED TO " + std::to_string(sel) + ")");
            break;
        }
        case MP_BROWSE: break;   // shown by formatValue; LOAD acts on it
        case MP_LOAD:
        {
            const auto entries = session_.browserEntries();
            const int k = roundi(values_[MP_BROWSE].load());
            if (k >= 0 && k < (int)entries.size() && entries[(size_t)k].isDir) { goFolder(entries[(size_t)k].path); break; }
            if (k >= 0 && k < (int)entries.size() && entries[(size_t)k].isPreset) { loadPresetFile(entries[(size_t)k].path, entries[(size_t)k].name); break; }
            if (k >= 0 && k < (int)entries.size())
            {
                session_.auditionStop();
                session_.loadFile(slot, entries[(size_t)k].path);
                say("LOADING " + upper(entries[(size_t)k].name));
            }
            else say("NO BREAKS IN THIS FOLDER");
            break;
        }
        case MP_DEMO: session_.loadDemo(slot); say("DEMO BREAK"); break;
        case MP_UP:
        {
            auto clean = [](std::string d) { while (d.size() > 1 && d.back() == '/') d.pop_back(); return d; };
            const std::string here = clean(browseDir_);
            const std::string parent = std::filesystem::path(here).parent_path().string();
            if (here == clean(topDir_) || parent.empty() || parent == here) { say("THESE ARE THE DRIVES"); break; }
            goFolder(parent);
            break;
        }
        case MP_REFRESH: session_.listFolder(browseDir_, topDir_); say("READING THE FOLDER AGAIN"); break;
        case MP_PLACE_STORE: if (v >= 0.5f) say("NOW TAP A PLACE KEY TO STORE THIS FOLDER THERE"); break;
        case MP_AKAIZE:
        {
            const auto& s = session_.slot(slot);
            if (s.content == nullptr) { say("NO BREAK TO AKAIZE"); break; }
            const double host = hostBpm_.load();
            Session::AkaizeRender r;
            r.sync = values_[P_AKR_SYNC].load() > 0.5f;
            r.targetBpm = host > 1.0 ? host : (double)values_[P_AKR_TARGET].load();
            r.stretchPercent = values_[P_AKR_STRETCH].load();
            r.cycleLength = roundi(values_[P_AKR_CYC_LEN].load());
            r.cycleFreq = akaizeFreq(values_[P_AKR_CYC_LEN].load(), values_[P_AKR_CYC_FREQ].load(), values_[P_AKR_FREQ_MAN].load() > 0.5f);
            r.pitch = values_[P_AKR_PITCH].load();
            say("AKAIZING...");
            syncReadouts(true);
            const bool ok = session_.akaizeSlot(slot, r);
            char buf[64];
            if (ok && r.sync) std::snprintf(buf, sizeof buf, "AKAIZED TO %.1f BPM", r.targetBpm);
            else std::snprintf(buf, sizeof buf, ok ? "AKAIZED" : "AKAIZE FAILED");
            say(buf);
            break;
        }
        case MP_DICE_PAT:
            session_.rollPattern((Style)roundi(values_[P_STYLE].load()), values_[P_PAT_AMOUNT].load(), values_[P_LAYERS].load() > 0.5f);
            break;
        case MP_DICE_FX: session_.rollFx(values_[P_FX_AMOUNT].load()); break;
        case MP_RESET_ORIG: session_.resetToOriginal(); say("PATTERN + FX BACK TO THE ORIGINAL"); break;
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
        case MP_SLICING: case MP_SENS: case MP_GRID:
            // kept for this break's next load; RESLICE applies them to the break loaded now
            session_.setSlicing(slot, roundi(values_[MP_SLICING].load()) == 1, values_[MP_SENS].load(),
                                kGridValues[std::clamp(roundi(values_[MP_GRID].load()), 0, 2)]);
            break;
        case MP_RESLICE:
            session_.reslice(slot, roundi(values_[MP_SLICING].load()) == 1, values_[MP_SENS].load(),
                             kGridValues[std::clamp(roundi(values_[MP_GRID].load()), 0, 2)]);
            say("RESLICED");
            break;
        case MP_LENGTH: session_.setPatternLength(1 << std::clamp(roundi(v), 0, 2)); break;
        case MP_CLEAR: session_.clearPattern(); break;
        case MP_MEMORY: break;
        case MP_MEM_SAVE_ORIG:
        {
            const int m = roundi(values_[MP_MEMORY].load());
            session_.saveOriginalToMemory(m - 1);
            char b[48]; std::snprintf(b, sizeof b, "ORIGINAL SAVED IN M%02d", m); say(b);
            break;
        }
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
    put(MP_MUTE_GROUP, (float)session_.muteGroup(slot));
    put(MP_AKR_LOOP_BPM, session_.loopBpmSet(slot) ? (float)session_.loopBpm(slot) : 0.0f);
    if (s.content != nullptr && std::fabs(seen_[MP_BRK_BPM] - (float)session_.breakBpm(slot)) > 0.01f) write(MP_BRK_BPM, (float)session_.breakBpm(slot));
    put(MP_SENS, s.sensitivity);
    put(MP_GRID, s.gridPerBar <= 8 ? 0.0f : s.gridPerBar <= 16 ? 1.0f : 2.0f);
    const int steps = s.program.pattern.numSteps;
    put(MP_LENGTH, steps <= 16 ? 0.0f : steps <= 32 ? 1.0f : 2.0f);

    // step text uses the hit letters of this break
    {
        std::lock_guard<std::mutex> l(textMutex_);
        badgeLetters_.clear();
        if (s.content != nullptr)
            for (int k = 0; k < s.content->numSlices(); ++k) badgeLetters_.push_back(badgeLabel(s.content->badges[(size_t)k]));
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
            std::snprintf(t, sizeof t, "%.4f S", at / s.content->buffer->sampleRate);
            moveText_ = t;
        }
        put(MP_SL_MOVE, (float)at / (float)n);
        if (dragTicks_ == 0 && !s.content->slices.isEmpty())
        {
            const View vw = viewOf(*s.content, slice);
            put(MP_SL_DRAG, std::clamp((float)(at - vw.start) / (float)std::max(1, vw.length), 0.0f, 1.0f));
            const int col = (int)std::floor((double)(at - vw.start) * kWaveCols / (double)std::max(1, vw.length));
            put(MP_SL_PLACE, (float)(col >= 0 && col < kWaveCols ? col + 1 : 0));
        }
        const int last = s.content->slices.count - 1;
        const int endAt = slice < last ? s.content->slices.starts[(size_t)slice + 1] : n;
        {
            std::lock_guard<std::mutex> l(textMutex_);
            char t[40];
            if (slice < last) std::snprintf(t, sizeof t, "%.4f S", endAt / s.content->buffer->sampleRate);
            else std::snprintf(t, sizeof t, "END OF BREAK");
            endText_ = t;
        }
        put(MP_SL_END, (float)endAt / (float)n);
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
                line += std::string(" ") + badgeLabel(s.content->badges[(size_t)slice]);
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

    // the 2-bar window of a longer break
    {
        const int n = session_.fullBars(slot), w = session_.window(slot);
        setText(MP_WIN_INFO, s.content == nullptr ? std::string("-")
                             : n > Session::kWindowBars ? "BARS " + std::to_string(w + 1) + "-" + std::to_string(w + Session::kWindowBars) + " OF " + std::to_string(n)
                             : "ALL " + std::to_string(std::max(1, s.content->bars)) + (s.content->bars == 1 ? " BAR" : " BARS"));
    }
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
    bool othersPlaying = false;
    for (int k = 0; k < kSlots; ++k) if (k != slot && eng.slotStep[(size_t)k].load() >= 0) othersPlaying = true;
    const bool thisPlaying = eng.slotStep[(size_t)slot].load() >= 0;
    if (s.content != nullptr)
    {
        char hostTxt[32];
        if (host > 1.0) std::snprintf(hostTxt, sizeof hostTxt, "MPC %.1f", host);
        else std::snprintf(hostTxt, sizeof hostTxt, "MPC --");
        std::snprintf(buf, sizeof buf, "%d SLICES  %d BARS  GRP %d  %s  %s%s", s.content->numSlices(), s.content->bars,
                      session_.muteGroup(slot) + 1, hostTxt,
                      thisPlaying ? "PLAYING" : othersPlaying ? "OTHER PLAYING" : "STOPPED",
                      s.showOriginal ? "  ORIGINAL" : "");
    }
    else std::snprintf(buf, sizeof buf, "PAD 1-8 PICKS A BREAK  LOAD FROM CONTENT/BREAKS");
    setText(MP_INFO_STATUS, buf);

    // the slice playing now
    const int now = eng.slotSlice[(size_t)slot].load();
    if (now >= 0 && s.content != nullptr && now < s.content->numSlices())
        std::snprintf(buf, sizeof buf, "S%02d %s", now + 1, badgeName(s.content->badges[(size_t)now]));
    else std::snprintf(buf, sizeof buf, "--");
    setText(MP_INFO_SLICE, buf);
    setText(MP_INFO_MSG, message_.empty() ? std::string("CHOPPAGE") : message_);
    {
        std::string n;
        { std::lock_guard<std::mutex> l(textMutex_); n = presetName_; }
        setText(MP_PRE_NAME, (n.empty() ? std::string("TYPE A NAME") : n) + ((int)n.size() < kPresetNameMax ? "_" : "") + (caps_ ? "  CAPS" : ""));
    }

    // playhead through the pattern (only while this break plays)
    const int step = eng.slotStep[(size_t)slot].load();
    if (force || step != lastStepShown_)
    {
        lastStepShown_ = step;
        const int n = std::max(1, s.program.pattern.numSteps);
        const float pos = step < 0 ? 0.0f : clampf((step + 0.5f) / (float)n, 0.0f, 1.0f);
        if (seen_[MP_PLAYHEAD] != pos) write(MP_PLAYHEAD, pos);
    }

    // browser (BROWSE knob text)
    const auto entries = session_.browserEntries();
    const std::string folder = session_.browserFolder();
    if (force || (int)entries.size() != lastBrowserCount_ || folder != lastBrowserDir_)
    {
        lastBrowserDir_ = folder;
        listKey_ = ~0ull;
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
    if (same && dragTicks_ > 0 && frozen_.start + frozen_.length <= n) return frozen_;   // a drag holds the view
    const int in0 = frozen_.start + frozen_.length / 10, in1 = frozen_.start + frozen_.length * 9 / 10;
    if (!same || frozen_.start + frozen_.length > n || marker < in0 || marker > in1)
    {
        frozen_ = ideal;
        viewSlot_ = slot; viewSel_ = sel; viewZoom_ = zoom; viewN_ = n;
    }
    return frozen_;
}

void ChoppageMpcApp::moveSelectedMarker(int position) { moveMarkerOf(session_.selectedSlice(), position); }

void ChoppageMpcApp::moveMarkerOf(int marker, int position)
{
    const auto& s = session_.slot(session_.selectedSlot());
    if (s.content == nullptr || s.content->slices.isEmpty()) return;
    const auto& t = s.content->slices;
    const int k = std::clamp(marker, 0, t.count - 1);
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
    const int playing = eng.slotSlice[(size_t)slot].load();
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
        if (count == 0 && k == 0) line = session_.browserScanning() ? "READING..." : "NO BREAKS OR FOLDERS HERE";
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
    const int now = eng.slotStep[(size_t)slot].load();
    const int pages = std::max(1, (pat.numSteps + 15) / 16);
    int page = std::clamp(roundi(seen_[MP_SEQ_PAGE]), 0, 3);
    if (seen_[MP_SEQ_FOLLOW] >= 0.5f && now >= 0 && now / 16 != page) page = now / 16;
    if (page >= pages) page = pages - 1;
    if ((float)page != seen_[MP_SEQ_PAGE]) write(MP_SEQ_PAGE, (float)page);

    const int head = now >= page * 16 && now < page * 16 + 16 ? now - page * 16 + 1 : 0;
    const float headF = (float)(theme * 17 + head);
    if (force || seen_[MP_SEQ_HEAD] != headF) write(MP_SEQ_HEAD, headF);

    const int sel = std::clamp(roundi(seen_[MP_SEQ_SEL]), 1, 64) - 1;
    const uint64_t key = ((uint64_t)session_.version() << 24) ^ ((uint64_t)slot << 16) ^ ((uint64_t)page << 8) ^ (uint64_t)theme
                       ^ ((uint64_t)sel << 40);
    if (!force && key == seqKey_) return;
    seqKey_ = key;
    auto laneOf = [](Badge b) { for (int i = 0; i < kSeqLanes; ++i) if (kLaneBadge[i] == b) return i; return kSeqLanes - 1; };
    for (int lane = 0; lane < kSeqLanes; ++lane)
        for (int col = 0; col < 16; ++col)
        {
            const int step = page * 16 + col;
            int c = col % 4 == 0 ? 1 : 0;
            std::string txt = std::string(badgeName(kLaneBadge[lane])) + " --";
            if (step < pat.numSteps && s.content != nullptr)
            {
                const int sl = pat.slice[(size_t)step];
                if (sl >= 0 && sl < s.content->numSlices() && laneOf(s.content->badges[(size_t)sl]) == lane)
                {
                    c = 2 + std::min(sl, 63);
                    char b[24];
                    std::snprintf(b, sizeof b, "%s %02d", badgeLabel(s.content->badges[(size_t)sl]), sl + 1);
                    txt = b;
                }
            }
            const int cellIdx = lane * 16 + col;
            {
                std::lock_guard<std::mutex> l(textMutex_);
                cellText_[(size_t)cellIdx] = txt;
            }
            const float f = (float)cellFrame(theme, c);
            cellShown_[cellIdx] = f;
            const int idx = MP_CELL0 + cellIdx;
            if (force || seen_[idx] != f || values_[idx].load() != f) write(idx, f);
        }
    for (int col = 0; col < 16; ++col)
    {
        const int step = page * 16 + col;
        int f = 0;
        if (step < pat.numSteps && pat.fx[(size_t)step].type != FxType::none)
        {
            const int r = pat.fx[(size_t)step].rate;
            const int ri = r <= 8 ? 0 : r <= 16 ? 1 : r <= 32 ? 2 : 3;
            f = 1 + ((int)pat.fx[(size_t)step].type - 1) * 4 + ri;
        }
        const float fr = (float)fxRowFrame(theme, step == sel, f);
        if (force || seen_[MP_FXROW0 + col] != fr) write(MP_FXROW0 + col, fr);
    }
    for (int k = 0; k < 16; ++k)
    {
        const int step = page * 16 + k;
        const float v = step < pat.numSteps ? (float)(pat.slice[(size_t)step] + 1) : 0.0f;
        if (force || seen_[MP_VSTEP0 + k] != v) write(MP_VSTEP0 + k, v);
    }
    // the selected step's FX
    if (sel < pat.numSteps)
    {
        const auto& fx = pat.fx[(size_t)sel];
        const float t = (float)(int)fx.type;
        const float r = fx.rate <= 8 ? 0.0f : fx.rate <= 16 ? 1.0f : fx.rate <= 32 ? 2.0f : 3.0f;
        if (seen_[MP_STEP_FX] != t) write(MP_STEP_FX, t);
        if (fx.type != FxType::none && seen_[MP_STEP_RATE] != r) write(MP_STEP_RATE, r);
    }
}

// ===================================================================================== DISK audition
void ChoppageMpcApp::syncAudition()
{
    // The DISK section open: the file under the jog wheel plays after a short pause (so scrolling doesn't stutter);
    // closed: quiet.
    // On the DISK page (MAIN's DISK section not open) a break plays while the jog wheel is being turned, and goes
    // quiet about 10 seconds after the last turn.
    const bool section = roundi(seen_[MP_VIEW]) == 2;
    const int now = roundi(seen_[MP_BROWSE]);
    if (now != browsePickSeen_) { browsePickSeen_ = now; browseIdle_ = 0; }
    else if (browseIdle_ < 100000) ++browseIdle_;
    if (sectionWas_ && !section) browseIdle_ = 100000;      // the DISK section closed: quiet now
    sectionWas_ = section;
    const bool disk = section || browseIdle_ < 300;
    if (!disk)
    {
        if (!session_.auditioning().empty()) session_.auditionStop();
        auditionWant_ = -1;
        return;
    }
    const int pick = roundi(seen_[MP_BROWSE]);
    if (pick != auditionWant_) { auditionWant_ = pick; auditionWait_ = 6; return; }
    if (auditionWait_ > 0 && --auditionWait_ == 0)
    {
        const auto entries = session_.browserEntries();
        if (pick >= 0 && pick < (int)entries.size() && !entries[(size_t)pick].isDir && !entries[(size_t)pick].isPreset && session_.auditioning() != entries[(size_t)pick].path)
            if (!session_.audition(entries[(size_t)pick].path)) { message_ = "CAN'T READ " + upper(entries[(size_t)pick].name); messageTicks_ = 90; }
    }
}

} // namespace bc
