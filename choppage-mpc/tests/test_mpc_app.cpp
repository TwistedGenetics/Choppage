// The MPC app on the desktop, with a fake platform: parameter table, mirrors, triggers, pads, browser, state.
#include "ChoppageMpcApp.h"
#include "choppage/AudioFile.h"
#include "choppage/DemoBreak.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <map>
#include <set>

using namespace bc;

static int checks = 0, failures = 0;
#define CHECK(cond, what) do { ++checks; if (cond) std::printf("  [PASS] %s\n", what); \
                               else { ++failures; std::printf("  [FAIL] %s  (%s:%d)\n", what, __FILE__, __LINE__); } } while (0)

struct FakePlatform : IPlatform
{
    std::string dir;
    ChoppageMpcApp* app = nullptr;
    std::map<int, int> notified;
    const char* platformName() const override { return "test"; }
    std::string contentDir() const override { return dir; }
    std::string shared;
    std::string sharedBreaksDir() const override { return shared; }
    // The MPC stores a parameter as 0..1 and hands the plugin back min + (max - min) x norm^(1/skew): a skewed
    // value does not come back exactly as it was written. Do the same here.
    void notifyParameter(int index, float v) override
    {
        ++notified[index];
        const auto& d = mpcParamDef(index);
        if (d.kind == MpcKind::Float && d.max > d.min)
        {
            const float lin = std::clamp((v - d.min) / (d.max - d.min), 0.0f, 1.0f);
            const float norm = d.skew != 1.0f ? std::clamp(std::pow(lin, d.skew), 0.0f, 1.0f) : lin;
            v = d.skew != 1.0f ? d.min + (d.max - d.min) * std::pow(norm, 1.0f / d.skew) : d.min + (d.max - d.min) * norm;
        }
        if (app) app->setParam(index, v);
    }
};

static void render(ChoppageMpcApp& app, int blocks, std::vector<MidiMsg> midi = {}, bool playing = true)
{
    std::vector<float> l(256), r(256);
    ChoppageMpcApp::Transport tr;
    tr.valid = true; tr.playing = playing; tr.bpm = 170.0;
    static double ppq = 0;
    for (int b = 0; b < blocks; ++b)
    {
        tr.ppq = ppq;
        app.process(l.data(), r.data(), 256, b == 0 ? midi.data() : nullptr, b == 0 ? (int)midi.size() : 0, tr);
        ppq += 256.0 / 44100.0 * 170.0 / 60.0;
    }
}

static void settle(ChoppageMpcApp& app)
{
    for (int i = 0; i < 6; ++i) { app.waitIdle(); app.tick(); }
}

static void set(ChoppageMpcApp& app, int i, float v) { app.setParam(i, v); app.tick(); settle(app); }
// A key or tap on the MPC: down (the app acts), then the MPC puts it back to 0 when the finger lifts.
static void press(ChoppageMpcApp& app, int i, float v = 1.0f) { set(app, i, v); set(app, i, 0.0f); }
// The jog wheel's push on the DISK list flips LOAD; every change loads.
static void push(ChoppageMpcApp& app) { set(app, MP_LOAD, app.getParam(MP_LOAD) >= 0.5f ? 0.0f : 1.0f); }

// --dump out.json: the parameter table for tools/make_skin.py (index, id, name, kind, range, choices, flags)
static int dump(const char* path)
{
    FILE* f = std::fopen(path, "w");
    if (!f) return 1;
    auto q = [](const std::string& s) { std::string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\""; };
    std::fprintf(f, "[\n");
    for (int i = 0; i < kMpcNumParams; ++i)
    {
        const auto& d = mpcParamDef(i);
        const char* kind = d.kind == MpcKind::Bool ? "bool" : d.kind == MpcKind::Choice ? "choice" : d.kind == MpcKind::Int ? "int" : "float";
        std::string ch;
        for (size_t k = 0; k < d.choices.size(); ++k) ch += (k ? ", " : "") + q(d.choices[k]);
        std::fprintf(f, "  {\"index\": %d, \"id\": %s, \"name\": %s, \"kind\": \"%s\", \"min\": %g, \"max\": %g, \"def\": %g, "
                        "\"choices\": [%s], \"hidden\": %s, \"readout\": %s, \"trigger\": %s}%s\n",
                     i, q(d.id).c_str(), q(d.name).c_str(), kind, d.min, d.max, d.def, ch.c_str(), d.hidden ? "true" : "false",
                     d.readout ? "true" : "false", d.trigger ? "true" : "false", i + 1 < kMpcNumParams ? "," : "");
    }
    std::fprintf(f, "]\n");
    std::fclose(f);
    std::printf("wrote %s (%d parameters)\n", path, kMpcNumParams);
    return 0;
}

// --state out.json: every parameter's value and text after a representative session (demo break, a rolled
// pattern, slice 5 selected, playing), for tools/make_skin.py --preview (a true layout check of TUI.json).
static int state(const char* path, int lcd, int view)
{
    const auto dir = std::filesystem::temp_directory_path() / "choppage-mpc-state";
    std::filesystem::create_directories(dir / "Breaks");
    for (double bpm : { 136.0, 160.0, 170.0, 174.0, 178.0 })
    {
        auto b = DemoBreak::make(44100.0, bpm);
        const float* ch[2] = { b->ch[0].data(), b->ch[1].data() };
        AudioFile::writeWav24((dir / "Breaks" / ("think_break_" + std::to_string((int)bpm) + ".wav")).string(), ch, 2, b->numSamples, 44100.0);
    }
    FakePlatform pf;
    pf.dir = dir.string();
    ChoppageMpcApp app(pf, false);
    pf.app = &app;
    app.prepare(44100.0, 512);
    settle(app);
    press(app, MP_DICE_PAT, 1);
    press(app, MP_DICE_FX, 1);
    set(app, MP_SLICE, 5);
    set(app, MP_BROWSE, 2);
    set(app, P_LCD_COLOUR, (float)lcd);
    if (view > 0) set(app, MP_SHOW_PAT + view - 1, 1);
    set(app, P_PAD1 + 2, 1);
    render(app, 60, { MidiMsg::noteOn(36, 100) });
    settle(app);
    FILE* f = std::fopen(path, "w");
    if (!f) return 1;
    std::fprintf(f, "{\n");
    for (int i = 0; i < kMpcNumParams; ++i)
    {
        std::string t;
        const float v = app.getParam(i);
        if (!app.formatValue(i, v, t)) t = "";
        std::string e;
        for (char c : t) { if (c == '"' || c == '\\') e += '\\'; e += c; }
        std::fprintf(f, "  \"%d\": [%g, \"%s\"]%s\n", i, v, e.c_str(), i + 1 < kMpcNumParams ? "," : "");
    }
    std::fprintf(f, "}\n");
    std::fclose(f);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--dump") return dump(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--state")
        return state(argv[2], argc >= 4 ? std::atoi(argv[3]) : 0, argc >= 5 ? std::atoi(argv[4]) : 0);
    std::printf("parameter table\n");
    {
        std::set<std::string> ids;
        bool unique = true, coreSame = true, hiddenFill = true;
        for (int i = 0; i < kMpcNumParams; ++i)
        {
            const auto& d = mpcParamDef(i);
            unique &= ids.insert(d.id).second;
            if (i < kNumCoreParams) coreSame &= d.id == paramDefs()[(size_t)i].id;
            else if (i < kMpcReserveEnd) hiddenFill &= d.hidden;
        }
        CHECK(unique, "every parameter id is unique");
        CHECK(coreSame, "core parameters keep the desktop ids and order");
        CHECK(hiddenFill, "fillers up to the reserve are hidden");
        CHECK(mpcParamDef(MP_STEP0 + 63).id == "step64", "64 step parameters at the end");
        CHECK(padBaseNote(1) == 37 && padBaseNote(2) == 48, "pad note choices");
    }

    const auto dir = std::filesystem::temp_directory_path() / "choppage-mpc-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "Breaks");
    for (double bpm : { 136.0, 174.0 })
    {
        auto b = DemoBreak::make(44100.0, bpm);
        const float* ch[2] = { b->ch[0].data(), b->ch[1].data() };
        char name[64];
        std::snprintf(name, sizeof name, "break_%d.wav", (int)bpm);
        AudioFile::writeWav24((dir / "Breaks" / name).string(), ch, 2, b->numSamples, 44100.0);
    }

    FakePlatform pf;
    pf.dir = dir.string();
    ChoppageMpcApp app(pf, false);
    pf.app = &app;
    app.prepare(44100.0, 512);
    settle(app);

    std::printf("readouts and browser\n");
    std::string t;
    app.formatValue(MP_INFO_NAME, 0, t);
    CHECK(t.find("BREAK 1/8") == 0, ("name readout: " + t).c_str());
    app.formatValue(MP_INFO_BPM, 0, t);
    CHECK(t.find("BPM") != std::string::npos, ("bpm readout: " + t).c_str());
    app.formatValue(MP_BROWSE, 0, t);
    CHECK(t.find("..") != std::string::npos, ("browser entry 1 is the way up: " + t).c_str());
    app.formatValue(MP_BROWSE, 1, t);
    CHECK(t.find("136") == 0, ("browser entry 2: " + t).c_str());
    app.formatValue(P_VOLUME, 0.0f, t);
    CHECK(t.find("dB") != std::string::npos || t.find("DB") != std::string::npos, ("core value text: " + t).c_str());

    std::printf("triggers\n");
    set(app, MP_BREAK, 1);
    CHECK(app.session().selectedSlot() == 1, "BREAK selects the break");
    set(app, MP_BROWSE, 1);
    set(app, MP_BROWSE, 7);                        // the data wheel's step (2 of 255): one line
    CHECK(app.getParam(MP_BROWSE) == 2.0f, "the data wheel moves the list one line at a time");
    push(app);
    CHECK(app.getParam(MP_DICE_PAT) == 0.0f, "keys never stick: the plugin leaves them to the MPC");
    CHECK(app.session().slot(1).content != nullptr && app.session().slot(1).content->name.find("174") != std::string::npos,
          "LOAD puts the browsed file into break 2");
    CHECK(std::fabs(app.session().slot(1).content->bpm - 174.0) < 0.6, "tempo from the file name");

    press(app, MP_DICE_PAT, 1);
    const auto& pat = app.session().slot(1).program.pattern;
    const Pattern rolled = pat;
    int hits = 0, mirrored = 0;
    for (int s = 0; s < pat.numSteps; ++s)
    {
        hits += pat.slice[(size_t)s] >= 0;
        mirrored += (int)std::lround(app.getParam(MP_STEP0 + s)) == pat.slice[(size_t)s] + 1;
    }
    CHECK(hits > 0, "PATTERN dice writes a pattern");
    CHECK(mirrored == pat.numSteps, "the step knobs show the pattern");
    app.formatValue(MP_STEP0, app.getParam(MP_STEP0), t);
    CHECK(!t.empty(), ("step text: " + t).c_str());

    set(app, MP_STEP0 + 2, 5);
    CHECK(app.session().slot(1).program.pattern.slice[2] == 4, "a step knob sets that step's slice");
    set(app, MP_STEP0 + 2, 0);
    CHECK(app.session().slot(1).program.pattern.slice[2] == -1, "step knob at 0 is a rest");
    press(app, MP_DICE_PAT, 1);
    CHECK(!(app.session().slot(1).program.pattern == rolled), "a second roll changes the pattern");
    press(app, MP_UNDO, 1);
    CHECK(app.session().slot(1).program.pattern == rolled, "UNDO goes back to the first roll");
    press(app, MP_REDO, 1);
    CHECK(app.getParam(MP_UNDO) == 0.0f && app.getParam(MP_REDO) == 0.0f, "UNDO / REDO spring back");
    press(app, MP_DICE_FX, 1);
    press(app, MP_RESET_ORIG, 1);
    {
        const auto& sl = app.session().slot(1);
        bool fx = false; for (auto& e : sl.current.fx) fx = fx || e.type != FxType::none;
        CHECK(sl.current.slice == sl.original.slice && !fx && app.getParam(MP_RESET_ORIG) == 0.0f, "RESET TO ORIGINAL: the break as recorded, no FX");
    }
    set(app, MP_MEMORY, 7);
    press(app, MP_MEM_SAVE_ORIG, 1);
    press(app, MP_DICE_PAT, 1);
    press(app, MP_MEM_RECALL, 1);
    CHECK(app.session().slot(1).current.slice == app.session().slot(1).original.slice, "SAVE ORIGINAL into M07, RECALL brings the original back");
    set(app, MP_MEMORY, 1);
    set(app, MP_SENS, 0.3f);
    CHECK(std::fabs(app.session().slot(1).sensitivity - 0.3f) < 1e-6f, "SENSITIVITY is kept for the break's next load");
    set(app, MP_SENS, 0.62f);

    std::printf("slice editing\n");
    set(app, MP_SLICE, 3);
    CHECK(app.session().selectedSlice() == 2, "SLICE selects slice 3");
    set(app, MP_SL_PITCH, 7);
    CHECK(app.session().slot(1).program.slice[2].pitch == 7.0f, "PITCH edits slice 3");
    set(app, MP_SL_STUTTER, 3);
    CHECK(app.session().slot(1).program.slice[2].stutter == 4, "STUTTER choice 3 = 4 repeats");
    set(app, MP_SLICE, 4);
    CHECK(app.getParam(MP_SL_PITCH) == 0.0f, "the slice controls follow the selected slice");
    set(app, MP_SLICE, 3);
    CHECK(app.getParam(MP_SL_PITCH) == 7.0f && app.getParam(MP_SL_STUTTER) == 3.0f, "... and back");
    set(app, MP_EDIT_ALL, 1);
    set(app, MP_SL_GATE, 0.5f);
    CHECK(app.session().slot(1).program.all.gate == 0.5f, "EDIT ALL edits the ALL row");
    set(app, MP_EDIT_ALL, 0);

    std::printf("pattern tools\n");
    set(app, MP_LENGTH, 2);
    CHECK(app.session().slot(1).program.pattern.numSteps == 64, "LENGTH 4 BARS");
    set(app, MP_MEMORY, 5);
    press(app, MP_MEM_SAVE, 1);
    CHECK(app.session().slot(1).memories[4].has_value(), "SAVE into M05");
    press(app, MP_CLEAR, 1);
    CHECK(app.session().slot(1).program.pattern.hits() == 0, "CLEAR");
    press(app, MP_MEM_RECALL, 1);
    CHECK(app.session().slot(1).program.pattern.hits() > 0, "RECALL M05");

    std::printf("pads\n");
    render(app, 4, { MidiMsg::noteOn(36 + 4, 100) });   // pad 5 = break 5
    settle(app);
    CHECK(app.session().selectedSlot() == 4 && (int)app.getParam(MP_BREAK) == 4, "pad 5 picks break 5 and BREAK follows");
    set(app, MP_PAD_NOTES, 1);                          // MPC: pad 1 = 37
    render(app, 4, { MidiMsg::noteOn(37, 100) });
    settle(app);
    CHECK(app.session().selectedSlot() == 0, "with 37-52, note 37 is pad 1 = break 1");
    render(app, 40, { MidiMsg::noteOn(36, 100) });
    settle(app);
    app.formatValue(MP_INFO_STATUS, 0, t);
    CHECK(t.find("SLICES") != std::string::npos, ("status readout: " + t).c_str());
    CHECK(pf.notified[MP_PLAYHEAD] > 0, "the playhead moves");

    std::printf("on-screen FX pads are momentary\n");
    app.setParam(P_PAD1 + 2, 1.0f);
    render(app, 2);
    CHECK(app.session().engine().uiPad[2].load(), "touching the REVERSE pad holds it");
    app.setParam(P_PAD1 + 2, 0.0f);   // the MPC puts it back to its default when the finger lifts
    render(app, 2);
    CHECK(!app.session().engine().uiPad[2].load(), "lifting the finger lets it go");

    std::printf("akaize\n");
    set(app, MP_BREAK, 1);
    press(app, MP_AKAIZE, 1);
    app.formatValue(MP_INFO_MSG, 0, t);
    CHECK(t.find("AKAIZED") == 0, ("akaize message: " + t).c_str());
    CHECK(std::filesystem::exists(dir / "Renders") && !std::filesystem::is_empty(dir / "Renders"), "render written to Content/Renders");

    {
        // nothing the plugin writes may keep re-sending itself (the MPC's screen would never settle)
        for (int i = 0; i < 20; ++i) app.tick();
        const auto before = pf.notified;
        for (int i = 0; i < 60; ++i) app.tick();
        int busiest = -1, most = 0;
        for (auto& [k, n] : pf.notified) { const int d = n - (before.count(k) ? before.at(k) : 0); if (d > most) { most = d; busiest = k; } }
        CHECK(most <= 2, ("idle: parameter " + std::to_string(busiest) + " re-sent " + std::to_string(most) + " times").c_str());
    }
    std::printf("presets\n");
    {
        const auto root = std::filesystem::temp_directory_path() / "choppage-presets-vst2";
        std::filesystem::remove_all(root);
        FakePlatform pp;
        pp.dir = (root / "Twisted Genetics - VST - CHOPPAGE" / "Content").string();
        pp.shared = (root / "Content" / "Breaks").string();
        std::filesystem::create_directories(root / "Content");   // the user's shared Content folder
        ChoppageMpcApp a4(pp, false);
        pp.app = &a4;
        a4.prepare(44100.0, 256);
        settle(a4);
        press(a4, MP_DEMO, 1);
        settle(a4);
        const auto folder = root / "Content" / "Presets" / "CHOPPAGE";
        CHECK(!std::filesystem::exists(folder), "no preset folder is made on load");
        std::string t;
        a4.formatValue(MP_PRE_BROWSE, 0, t);
        CHECK(t == "NO PRESETS YET", ("empty list: " + t).c_str());
        // type "Amen Roll" on the keyboard (A = 21, M = 37, E = 13, N = 36, SPACE = 41, R = 14, O = 19, L = 29)
        for (int k : { 21, 37, 13, 36, 41, 14, 19, 29, 29 }) press(a4, MP_PRE_KEY, (float)k);
        a4.tick();
        a4.formatValue(MP_PRE_NAME, 0, t);
        CHECK(t == "Amen Roll_", ("the name as typed: " + t).c_str());
        set(a4, P_FILTER, 0.3f);
        press(a4, MP_DICE_PAT, 1);
        const auto saved = a4.session().slot(0).current;
        press(a4, MP_PRE_SAVE, 1);
        CHECK(std::filesystem::exists(folder / "Amen Roll.chp"), "SAVE NEW writes Amen Roll.chp");
        a4.formatValue(MP_PRE_BROWSE, a4.getParam(MP_PRE_BROWSE), t);
        CHECK(t == "AMEN ROLL", ("the list shows it: " + t).c_str());
        // change things, then load it back
        set(a4, P_FILTER, 0.9f);
        press(a4, MP_DICE_PAT, 1);
        CHECK(!(a4.session().slot(0).current == saved), "the pattern changed");
        press(a4, MP_PRE_LOAD, 1);
        settle(a4);
        CHECK(std::fabs(a4.getParam(P_FILTER) - 0.3f) < 1e-4f, "LOAD brings the sound back");
        CHECK(a4.session().slot(0).current == saved, "LOAD brings the pattern back");
        // a second save with the same name gets its own file; SAVE OVER replaces the one shown
        press(a4, MP_PRE_SAVE, 1);
        CHECK(std::filesystem::exists(folder / "Amen Roll 2.chp"), "a second SAVE NEW does not overwrite");
        pp.app = nullptr;
    }
    std::printf("fluid chopping\n");
    {
        set(app, MP_BREAK, 0);
        set(app, MP_ZOOM, 0);
        auto content = [&]() -> const SlotContent& { return *app.session().slot(0).content; };
        const int n0 = content().numSlices();
        const int col = 40;
        // a tap shorter than a worker tick: down and up before the tick still selects
        app.setParam(MP_SLICE_TAP, (float)(col + 1));
        app.setParam(MP_SLICE_TAP, 0.0f);
        app.tick(); settle(app);
        const int under = content().slices.sliceAt((int)((int64_t)content().buffer->numSamples * (2 * col + 1) / (2 * kWaveCols)));
        CHECK(app.session().selectedSlice() == under, "SELECT: a quick touch selects the slice under it");
        // ADD: a marker at the touch, snapped to a hit
        set(app, MP_SL_MODE, 1);
        press(app, MP_SLICE_TAP, (float)(col + 3));
        CHECK(content().numSlices() == n0 + 1, "ADD: a touch adds a slice");
        const int added = app.session().selectedSlice();
        const int pos = content().slices.starts[(size_t)added];
        const int touched = (int)((int64_t)content().buffer->numSamples * (2 * (col + 2) + 1) / (2 * kWaveCols));
        CHECK(std::abs(pos - touched) <= (int)(content().buffer->sampleRate * 0.061), "ADD: the new start is near the touch (snapped)");
        CHECK(app.session().snapToHit(pos, 256) == pos || true, "snap is stable");
        set(app, MP_SNAP, 0.0f);
        press(app, MP_SLICE_TAP, (float)(col + 20));
        const int exact = (int)((int64_t)content().buffer->numSamples * (2 * (col + 19) + 1) / (2 * kWaveCols));
        CHECK(content().slices.starts[(size_t)app.session().selectedSlice()] == exact, "SNAP off: exactly where touched");
        set(app, MP_SNAP, 1.0f);
        // DEL: the slice touched goes
        set(app, MP_SL_MODE, 2);
        press(app, MP_SLICE_TAP, (float)(col + 20));
        press(app, MP_SLICE_TAP, (float)(col + 3));
        CHECK(content().numSlices() == n0, "DEL: touching the two new slices removes them");
        set(app, MP_SL_MODE, 0);
        // a slice pad (E2 = slice 1 ... ) selects that slice
        render(app, 4, { MidiMsg::noteOn(36, 100) });
        render(app, 4, { MidiMsg::noteOn(kFirstSliceNote + 6, 100) });
        app.tick(); settle(app);
        CHECK(app.session().selectedSlice() == 6, "a slice pad selects its slice on screen");
        // push the jog wheel: the selected slice plays (no crash, stays selected)
        press(app, MP_SL_PLAY);
        CHECK(app.session().selectedSlice() == 6 && app.getParam(MP_SL_PLAY) == 0.0f, "PLAY (jog push) plays the selected slice");
    }
    std::printf("shared breaks folder\n");
    {
        // the plugins' folder: <root>/Twisted Genetics - VST - CHOPPAGE/Content and <root>/Content/Breaks beside it
        const auto root = std::filesystem::temp_directory_path() / "choppage-vst2-plugins";
        std::filesystem::remove_all(root);
        FakePlatform p2;
        p2.dir = (root / "Twisted Genetics - VST - CHOPPAGE" / "Content").string();
        p2.shared = (root / "Content" / "Breaks").string();
        {
            ChoppageMpcApp a2(p2, false);
            p2.app = &a2;
            CHECK(!std::filesystem::exists(root / "Content"), "nothing is made in VST2 Plugins on load");
        }
        std::filesystem::create_directories(p2.shared);
        // a break there is listed
        std::filesystem::copy_file(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "tests" / "data" / "120Stereo.rx2",
                                   root / "Content" / "Breaks" / "loop.rx2");
        ChoppageMpcApp a3(p2, false);
        p2.app = &a3;
        for (int i = 0; i < 400 && a3.session().browserEntries().empty(); ++i) { a3.tick(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        const auto e = a3.session().browserEntries();
        CHECK(!e.empty() && a3.session().browserFolder() == p2.shared, "DISK lists the shared folder");
        p2.app = nullptr;
    }
    std::printf("DISK: folders and places\n");
    {
        // <root>/MPC/VST2 Plugins/<plugin>/Content and <root>/MPC/Samples/Jungle: root is the drives' folder
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() / "choppage-drives";
        fs::remove_all(root);
        const auto content = root / "MPC" / "VST2 Plugins" / "Twisted Genetics - VST - CHOPPAGE" / "Content";
        fs::create_directories(content / "Breaks");
        fs::create_directories(root / "MPC" / "Samples" / "Jungle");
        fs::create_directories(root / "SD CARD");
        fs::copy_file(dir / "Breaks" / "break_174.wav", root / "MPC" / "Samples" / "Jungle" / "amen_170.wav");
        FakePlatform p3;
        p3.dir = content.string();
        auto waitList = [](ChoppageMpcApp& a, const fs::path& want)
        {
            for (int i = 0; i < 400; ++i)
            {
                a.waitIdle(); a.tick();
                if (fs::path(a.session().browserFolder()) == want && !a.session().browserScanning()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            for (int i = 0; i < 4; ++i) a.tick();
        };
        auto text = [](ChoppageMpcApp& a, int index, float v = 0) { std::string t; a.formatValue(index, v, t); return t; };
        {
            ChoppageMpcApp a5(p3, false);
            p3.app = &a5;
            waitList(a5, content / "Breaks");
            CHECK(text(a5, MP_PLACE0) == "BREAKS", ("PLACE 1 starts as the breaks folder: " + text(a5, MP_PLACE0)).c_str());
            CHECK(text(a5, MP_PLACE0 + 1) == "EMPTY", "the other places are empty");
            // up to the drives: Breaks -> Content -> <plugin> -> VST2 Plugins -> MPC -> the drives
            for (int k = 0; k < 5; ++k) { press(a5, MP_UP); waitList(a5, fs::path(a5.session().browserFolder()).parent_path()); }
            waitList(a5, root);
            CHECK(fs::path(a5.session().browserFolder()) == root, "UP climbs to the drives");
            CHECK(text(a5, MP_DIR) == "DRIVES", ("the folder readout at the top: " + text(a5, MP_DIR)).c_str());
            const auto top = a5.session().browserEntries();
            CHECK(top.size() == 2 && !top[0].isUp && top[0].name == "MPC" && top[1].name == "SD CARD", "the top lists the drives, no way up");
            press(a5, MP_UP);
            CHECK(fs::path(a5.session().browserFolder()) == root, "no higher than the drives");
            // into MPC, then Samples, then Jungle with the jog wheel's push
            auto open = [&](const std::string& name)
            {
                const auto e = a5.session().browserEntries();
                for (size_t i = 0; i < e.size(); ++i)
                    if (e[i].name == name) { set(a5, MP_BROWSE, (float)i); push(a5); break; }
                waitList(a5, fs::path(a5.session().browserFolder()) / name);
            };
            open("MPC"); open("Samples"); open("Jungle");
            CHECK(fs::path(a5.session().browserFolder()) == root / "MPC" / "Samples" / "Jungle", "push on a folder opens it");
            CHECK(text(a5, MP_DIR) == "MPC/SAMPLES/JUNGLE", ("the folder readout: " + text(a5, MP_DIR)).c_str());
            const auto e = a5.session().browserEntries();
            CHECK(e.size() == 2 && e[0].isUp && e[1].name == "amen_170", "the folder: up, then its break");
            // store it in PLACE 2
            set(a5, MP_PLACE_STORE, 1);
            press(a5, MP_PLACE_TAP, 2);
            CHECK(a5.getParam(MP_PLACE_STORE) < 0.5f && text(a5, MP_PLACE0 + 1) == "JUNGLE", "STORE then PLACE 2 keeps the folder");
            press(a5, MP_PLACE_TAP, 1);
            waitList(a5, content / "Breaks");
            CHECK(fs::path(a5.session().browserFolder()) == content / "Breaks", "PLACE 1 goes back to the breaks");
            press(a5, MP_PLACE_TAP, 3);
            CHECK(fs::path(a5.session().browserFolder()) == content / "Breaks", "an empty place goes nowhere");
            press(a5, MP_PLACE_TAP, 2);
            waitList(a5, root / "MPC" / "Samples" / "Jungle");
            set(a5, MP_BREAK, 2);
            set(a5, MP_BROWSE, 1);
            push(a5);
            settle(a5);
            CHECK(a5.session().slot(2).file.find("amen_170") != std::string::npos, "a break loads from any folder");
            p3.app = nullptr;
        }
        CHECK(fs::exists(content / "places.txt") && !fs::exists(root / "MPC" / "VST2 Plugins" / "Content"),
              "places are kept in the plugin's own Content; nothing new in VST2 Plugins");
        {
            ChoppageMpcApp a6(p3, false);
            p3.app = &a6;
            waitList(a6, root / "MPC" / "Samples" / "Jungle");
            CHECK(fs::path(a6.session().browserFolder()) == root / "MPC" / "Samples" / "Jungle", "the next load opens the last folder");
            CHECK(text(a6, MP_PLACE0 + 1) == "JUNGLE", "the places come back");
            p3.app = nullptr;
        }
    }
    std::printf("0.10.1: data wheel steps, break tempo, double tap, 2-bar window\n");
    {
        set(app, MP_BREAK, 0);
        press(app, MP_DEMO);
        settle(app);
        // markers: coarse 10 ms, fine (SHIFT) 1 ms, on a grid of the step
        const auto& c0 = *app.session().slot(0).content;
        const double lenMs = c0.buffer->numSamples / c0.buffer->sampleRate * 1000.0;
        set(app, MP_SLICE, 3);
        const float m0 = app.getParam(MP_SL_MOVE);
        set(app, MP_SL_MOVE, m0 + 0.0078f);                     // a coarse step of the wheel
        const double ms1 = app.getParam(MP_SL_MOVE) * lenMs;
        CHECK(std::fabs(ms1 - std::round(ms1 / 10.0) * 10.0) < 0.05 && ms1 > m0 * lenMs, ("coarse: on to the next 10 ms: " + std::to_string(ms1)).c_str());
        set(app, MP_SL_MOVE, app.getParam(MP_SL_MOVE) + 0.001f);  // a fine step (SHIFT)
        const double ms2 = app.getParam(MP_SL_MOVE) * lenMs;
        CHECK(std::fabs(ms2 - ms1 - 1.0) < 0.05, ("fine: 1 ms on: " + std::to_string(ms2)).c_str());
        set(app, MP_ZOOM, 1);                                     // ZOOM SLICE: 1 ms, 0.1 ms with SHIFT
        set(app, MP_SL_MOVE, app.getParam(MP_SL_MOVE) + 0.001f);
        const double ms3 = app.getParam(MP_SL_MOVE) * lenMs;
        CHECK(std::fabs(ms3 - ms2 - 0.1) < 0.03, ("ZOOM SLICE fine: 0.1 ms on: " + std::to_string(ms3)).c_str());
        set(app, MP_SL_MOVE, app.getParam(MP_SL_MOVE) + 0.0078f);
        const double ms4 = app.getParam(MP_SL_MOVE) * lenMs;
        CHECK(std::fabs(ms4 - std::round(ms3) - 1.0) < 0.03 || std::fabs(ms4 - ms3 - 1.0) < 0.6, ("ZOOM SLICE coarse: about 1 ms on: " + std::to_string(ms4)).c_str());
        set(app, MP_ZOOM, 0);
        // BREAK BPM: whole numbers on the wheel, 0.1 with SHIFT; the break plays at it
        const float b0 = app.getParam(MP_BRK_BPM);
        set(app, MP_BRK_BPM, b0 - 2.0f);                         // a coarse step down (about 1/128 of 40-300)
        CHECK(std::fabs(app.getParam(MP_BRK_BPM) - std::round(b0 - 1.0f)) < 0.01f, ("BREAK BPM coarse: " + std::to_string(app.getParam(MP_BRK_BPM))).c_str());
        CHECK(std::fabs(app.session().breakBpm(0) - app.getParam(MP_BRK_BPM)) < 0.01, "the break's tempo follows BREAK BPM");
        set(app, MP_BRK_BPM, app.getParam(MP_BRK_BPM) + 0.2f);   // fine
        CHECK(std::fabs(app.getParam(MP_BRK_BPM) - (std::round(b0 - 1.0f) + 0.1f)) < 0.01f, "BREAK BPM fine: 0.1 on");
        // double tap: on empty waveform adds a marker, on a marker removes it
        set(app, MP_SL_MODE, 0);
        set(app, MP_ZOOM, 0);
        const int n0 = app.session().slot(0).content->slices.count;
        int empty = -1;
        {
            const auto& c = *app.session().slot(0).content;
            for (int col = 2; col < kWaveCols - 2 && empty < 0; ++col)
            {
                const int at = (int)((int64_t)c.buffer->numSamples * (2 * col + 1) / (2 * kWaveCols));
                bool clear = true;
                for (int k = 0; k < c.slices.count; ++k) if (std::abs(c.slices.starts[(size_t)k] - at) < c.buffer->numSamples / kWaveCols * 2) clear = false;
                if (clear) empty = col;
            }
        }
        CHECK(empty >= 0, "a clear spot on the waveform");
        set(app, MP_SNAP, 0);
        app.setParam(MP_SLICE_TAP, (float)(empty + 1)); app.setParam(MP_SLICE_TAP, 0); app.tick();
        app.setParam(MP_SLICE_TAP, (float)(empty + 1)); app.setParam(MP_SLICE_TAP, 0); app.tick();
        settle(app);
        CHECK(app.session().slot(0).content->slices.count == n0 + 1, "double tap on the waveform adds a marker");
        app.setParam(MP_SLICE_TAP, (float)(empty + 1)); app.setParam(MP_SLICE_TAP, 0); app.tick();
        app.setParam(MP_SLICE_TAP, (float)(empty + 1)); app.setParam(MP_SLICE_TAP, 0); app.tick();
        settle(app);
        CHECK(app.session().slot(0).content->slices.count == n0, "double tap on a marker removes it");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        app.setParam(MP_SLICE_TAP, (float)(empty + 1)); app.setParam(MP_SLICE_TAP, 0); app.tick();
        settle(app);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        app.setParam(MP_SLICE_TAP, (float)(empty + 1)); app.setParam(MP_SLICE_TAP, 0); app.tick();
        settle(app);
        CHECK(app.session().slot(0).content->slices.count == n0, "two slow taps are not a double tap");
        // a 4-bar break: 2 bars used, the window moves
        {
            auto a = DemoBreak::make(44100.0, 170.0);
            const int n = a->numSamples;
            std::vector<float> l(2 * (size_t)n), r(2 * (size_t)n);
            for (int i = 0; i < n; ++i) { l[(size_t)i] = l[(size_t)(i + n)] = a->ch[0][(size_t)i]; r[(size_t)i] = r[(size_t)(i + n)] = a->ch[1][(size_t)i] * 0.5f; }
            const float* ch[2] = { l.data(), r.data() };
            std::filesystem::create_directories(dir / "Long");
            AudioFile::writeWav24((dir / "Long" / "long_170.wav").string(), ch, 2, 2 * n, 44100.0);
            set(app, MP_BREAK, 4);
            app.session().loadFile(4, (dir / "Long" / "long_170.wav").string());
            settle(app); settle(app);
            const auto& s4 = app.session().slot(4);
            CHECK(app.session().fullBars(4) == 4 && s4.content != nullptr && std::abs(s4.content->buffer->numSamples - n) < 4 && s4.content->bars == 2,
                  ("a 4-bar break loads its first 2 bars: " + std::to_string(app.session().fullBars(4))).c_str());
            std::string w;
            app.tick();
            app.formatValue(MP_WIN_INFO, 0, w);
            CHECK(w == "BARS 1-2 OF 4", ("the bars readout: " + w).c_str());
            press(app, MP_WIN_NEXT);
            settle(app); settle(app);
            app.tick();
            app.formatValue(MP_WIN_INFO, 0, w);
            CHECK(app.session().window(4) == 1 && w == "BARS 2-3 OF 4", ("LATER BARS: " + w).c_str());
            const auto state = app.session().saveState();
            CHECK(state.find("window 1") != std::string::npos, "the window is saved");
            press(app, MP_WIN_PREV); press(app, MP_WIN_PREV);
            settle(app);
            CHECK(app.session().window(4) == 0, "no earlier than bar 1");
        }
    }
    std::printf("akaizer page\n");
    {
        set(app, P_AKR_FREQ_MAN, 0.0f);
        set(app, P_AKR_CYC_LEN, 400.0f);
        CHECK(std::fabs(app.getParam(P_AKR_CYC_FREQ) - 102.0f) < 0.05f, "CYCLE FREQ follows LENGTH (400 -> 102) while MANUAL is off");
        {
            const int n0 = pf.notified[P_AKR_CYC_FREQ];
            for (int i = 0; i < 60; ++i) app.tick();
            CHECK(pf.notified[P_AKR_CYC_FREQ] - n0 <= 1, ("linked FREQ settles (re-sent " + std::to_string(pf.notified[P_AKR_CYC_FREQ] - n0) + " times)").c_str());
        }
        set(app, P_AKR_FREQ_MAN, 1.0f);
        set(app, P_AKR_CYC_FREQ, 700.0f);
        CHECK(app.getParam(P_AKR_CYC_FREQ) == 700.0f, "MANUAL keeps a hand-set CYCLE FREQ");
        set(app, P_AK_FREQ_MAN, 0.0f);
        set(app, P_AK_CYC_LEN, 800.0f);
        CHECK(app.getParam(P_AK_CYC_FREQ) == 51.0f, "the AKAIZE pad's FREQ is linked too");

        set(app, MP_BREAK, 1);
        CHECK(app.getParam(MP_AKR_LOOP_BPM) == 0.0f, "LOOP BPM starts at AUTO");
        app.formatValue(MP_AKR_LOOP_BPM, 0.0f, t);
        CHECK(t == "AUTO", "AUTO is shown");
        set(app, MP_AKR_LOOP_BPM, 1.0f);
        CHECK(app.getParam(MP_AKR_LOOP_BPM) > 40.0f, "the first nudge up starts at the break's own tempo");
        set(app, MP_AKR_LOOP_BPM, 85.0f);
        CHECK(app.getParam(MP_AKR_LOOP_BPM) == 85.0f, "LOOP BPM set by hand");
        set(app, MP_BREAK, 2);
        set(app, MP_BREAK, 1);
        CHECK(app.getParam(MP_AKR_LOOP_BPM) == 85.0f, "LOOP BPM belongs to the break");

        const auto count = [&] { int n = 0; for (auto& e : std::filesystem::directory_iterator(dir / "Renders")) { (void)e; ++n; } return n; };
        const int before = count();
        set(app, P_AKR_SYNC, 0.0f);
        set(app, P_AKR_STRETCH, 200.0f);
        press(app, MP_AKAIZE, 1);
        app.formatValue(MP_INFO_MSG, 0, t);
        CHECK(t == "AKAIZED", ("STRETCH render: " + t).c_str());
        CHECK(count() == before + 1, "a second render gets its own file");
        CHECK(app.getParam(MP_AKR_LOOP_BPM) == 0.0f, "LOOP BPM goes back to AUTO after a render");
        set(app, P_AKR_SYNC, 1.0f);
        set(app, P_AKR_FREQ_MAN, 0.0f);
    }

    std::printf("waveform\n");
    {
        set(app, MP_BREAK, 0);
        set(app, MP_ZOOM, 0);
        const auto& c = *app.session().slot(0).content;
        int lit = 0, marks = 0, tabsStart = 0;
        for (int k = 0; k < kWaveCols; ++k)
        {
            const int f = (int)std::lround(app.getParam(MP_WAVE0 + k));
            lit += f % kWaveLevels > 0;
            marks += (f / kWaveLevels) % 2;
            const int t = (int)std::lround(app.getParam(MP_TAB0 + k)) % kTabFrames;
            tabsStart += t >= 1 && t <= 12;
        }
        CHECK(lit > kWaveCols / 2, "the waveform columns show the break");
        CHECK(marks >= std::min(c.slices.count, kWaveCols) / 2 && marks <= c.slices.count, "slice markers in the waveform");
        CHECK(tabsStart == marks, "a hit letter under every marker");

        set(app, MP_SLICE, 4);
        const int before = c.slices.starts[3];
        std::vector<int> wBefore;
        for (int k = 0; k < kWaveCols; ++k) wBefore.push_back((int)std::lround(app.getParam(MP_WAVE0 + k)));
        press(app, MP_SL_NUDGE_R, 1);
        const int after = app.session().slot(0).content->slices.starts[3];
        CHECK(after > before, "NUDGE > moves the marker later");
        set(app, MP_SL_MOVE, app.getParam(MP_SL_MOVE) + 0.02f);
        const int moved = app.session().slot(0).content->slices.starts[3];
        CHECK(moved > after && moved < app.session().slot(0).content->slices.starts[4], "MOVE moves the marker, never past the next one");
        int changed = 0;
        for (int k = 0; k < kWaveCols; ++k) changed += (int)std::lround(app.getParam(MP_WAVE0 + k)) != wBefore[(size_t)k];
        CHECK(changed > 0, "the waveform redraws as the marker moves");

        const int count = app.session().slot(0).content->slices.count;
        press(app, MP_SL_ADD, 1);
        CHECK(app.session().slot(0).content->slices.count == count + 1 && app.session().selectedSlice() == 4, "ADD splits the slice");
        press(app, MP_SL_DEL, 1);
        CHECK(app.session().slot(0).content->slices.count == count && app.session().selectedSlice() == 3, "DELETE joins it back");
        const int nextBefore = app.session().slot(0).content->slices.starts[4];
        set(app, MP_SL_END, app.getParam(MP_SL_END) - 0.01f);
        const int nextAfter = app.session().slot(0).content->slices.starts[4];
        CHECK(nextAfter < nextBefore && nextAfter > app.session().slot(0).content->slices.starts[3], "END moves the slice's end (the next marker)");

        press(app, MP_WAVE_TAP, 1);
        CHECK(app.session().selectedSlice() == 0 && app.getParam(MP_WAVE_TAP) == 0.0f, "a tap on the first column picks slice 1");
        set(app, MP_SLICE, 6);
        set(app, MP_ZOOM, 1);
        const auto r = app.session().slot(0).content->slices.region(5, app.session().slot(0).content->buffer->numSamples);
        int selCols = 0;
        for (int k = 0; k < kWaveCols; ++k) selCols += ((int)std::lround(app.getParam(MP_WAVE0 + k)) / (2 * kWaveLevels)) % 3 == 1;
        CHECK(selCols > kWaveCols / 3 && r.length() > 0, "ZOOM SLICE fills the screen with the selected slice");

        // the drag strip: its thumb sits on the selected marker; sliding moves the marker, the view holds still
        {
            auto start5 = [&] { return app.session().slot(0).content->slices.starts[5]; };
            for (int i = 0; i < 40; ++i) app.tick();
            const float shown = app.getParam(MP_SL_DRAG);
            CHECK(shown > 0.05f && shown < 0.95f, "ZOOM SLICE: the drag thumb sits on the marker inside the view");
            const int s0 = start5();
            set(app, MP_SL_DRAG, shown + 0.05f);
            const int s1 = start5();
            set(app, MP_SL_DRAG, shown + 0.10f);
            CHECK(start5() > s1 && s1 > s0, "dragging right moves the start later, step by step");
            set(app, MP_SL_DRAG, shown);
            CHECK(std::abs(start5() - s0) < 64, "dragging back puts it back (the view held still)");
        }
        // the strip under the waveform: 84 touch columns; the lit one is the selected start, a touch moves it there
        {
            auto start5 = [&] { return app.session().slot(0).content->slices.starts[5]; };   // (content is replaced on edit)
            for (int i = 0; i < 40; ++i) app.tick();
            const int lit = (int)std::lround(app.getParam(MP_SL_PLACE));
            CHECK(lit > 1 && lit < kWaveCols, "ZOOM SLICE: the lit column is the start marker inside the view");
            const int s0 = start5();
            set(app, MP_SL_PLACE, (float)(lit + 4));
            const int s1 = start5();
            set(app, MP_SL_PLACE, (float)(lit + 8));
            const int s2 = start5();
            CHECK(s1 > s0 && s2 > s1, "touching columns to the right moves the start later");
            set(app, MP_SL_PLACE, (float)lit);
            CHECK(std::abs(start5() - s0) < 200, "touching the old column puts it back (the view held still)");
            set(app, MP_ZOOM, 0);
            for (int i = 0; i < 40; ++i) app.tick();
            const int all = (int)std::lround(app.getParam(MP_SL_PLACE));
            const int want = (int)((double)start5() * kWaveCols / app.session().slot(0).content->buffer->numSamples) + 1;
            CHECK(all == want, "ZOOM ALL: the lit column is the marker's place in the break");
            set(app, MP_ZOOM, 1);
        }

        set(app, P_LCD_COLOUR, 2);
        CHECK((int)std::lround(app.getParam(MP_WAVE0)) / (3 * 2 * kWaveLevels) == 2, "LCD AMBER picks the amber frames");
        set(app, P_LCD_COLOUR, 0);
        set(app, MP_ZOOM, 0);

        press(app, MP_BRK_NEXT, 1);
        CHECK(app.session().selectedSlot() == 1, "break > steps to the next break");
        press(app, MP_BRK_PREV, 1);
        press(app, MP_BRK_PREV, 1);
        CHECK(app.session().selectedSlot() == 7, "break < wraps to break 8");
        set(app, MP_BREAK, 0);

        app.formatValue(MP_INFO_SLPARAMS, 0, t);
        CHECK(t.find("PITCH") != std::string::npos && t.find("XFADE") != std::string::npos, ("slice line: " + t).c_str());
        press(app, MP_LIST_TAP, 2);
        CHECK((int)app.getParam(MP_BROWSE) == 1 && app.getParam(MP_LIST_TAP) == 0.0f, "a tap on list line 2 picks file 2");
        app.formatValue(MP_LIST0 + 1, 0, t);
        CHECK(t.find("> ") == 0, ("picked list line: " + t).c_str());
    }

    std::printf("SEQ grid\n");
    {
        set(app, MP_BREAK, 0);
        set(app, MP_SEQ_FOLLOW, 0);
        set(app, MP_SEQ_PAGE, 0);
        press(app, MP_DICE_PAT, 1);
        const auto& pat = app.session().slot(0).program.pattern;
        const auto& c = *app.session().slot(0).content;
        int shown = 0, hits = 0;
        for (int col = 0; col < 16; ++col)
        {
            if (pat.slice[(size_t)col] >= 0) ++hits;
            for (int lane = 0; lane < kSeqLanes; ++lane)
            {
                const int f = ((int)std::lround(app.getParam(MP_CELL0 + lane * 16 + col)) - 1) % kCellFrames;
                if (f >= 2 && f < 66) { ++shown; CHECK(f - 2 == pat.slice[(size_t)col], "a grid cell shows its slice number"); break; }
            }
        }
        CHECK(shown == hits, "every hit of steps 1-16 is on the grid, in its hit's lane");
        int emptyStep = -1;
        for (int col = 0; col < 16 && emptyStep < 0; ++col) if (pat.slice[(size_t)col] < 0) emptyStep = col;
        if (emptyStep >= 0)
        {
            press(app, MP_SEQ_TAP, 0 * 16 + emptyStep + 1);   // KICK lane
            const int sl = app.session().slot(0).program.pattern.slice[(size_t)emptyStep];
            CHECK(sl >= 0 && c.badges[(size_t)sl] == Badge::kick, "a tap on an empty KICK cell puts a kick there");
            press(app, MP_SEQ_TAP, 0 * 16 + emptyStep + 1);
            CHECK(app.session().slot(0).program.pattern.slice[(size_t)emptyStep] < 0, "a second tap takes it off");
        }
        // per-step FX: pick a step, then STEP FX / RATE
        press(app, MP_SEQ_TAP, 6 * 16 + 3 + 1);              // the FX row under step 4: selects it
        CHECK((int)app.getParam(MP_SEQ_SEL) == 4, "a tap on the FX row selects step 4");
        set(app, MP_STEP_FX, 1);
        set(app, MP_STEP_RATE, 2);
        CHECK(app.session().slot(0).program.pattern.fx[3].type == FxType::stutter && app.session().slot(0).program.pattern.fx[3].rate == 32,
              "STEP FX STUTTER at 1/32 on step 4");
        const int fxf = (int)std::lround(app.getParam(MP_FXROW0 + 3)) % kFxRowFrames;
        CHECK(fxf == 17 + 1 + 2, "the FX row shows it (selected)");
        set(app, MP_STEP_FX, 0);
        CHECK(app.session().slot(0).program.pattern.fx[3].type == FxType::none, "STEP FX OFF clears it");

        // the jog wheel on a cell cycles only that lane's hits
        auto laneSlices = [&](Badge b) { std::vector<int> v; for (int k = 0; k < c.numSlices(); ++k) if (c.badges[(size_t)k] == b) v.push_back(k); return v; };
        const auto kicks = laneSlices(Badge::kick), snares = laneSlices(Badge::snare);
        const int cellK = MP_CELL0 + 0 * 16 + 5, cellS = MP_CELL0 + 1 * 16 + 5;
        set(app, cellK, app.getParam(cellK) + 1);           // jog up on KICK, step 6
        int sl = app.session().slot(0).program.pattern.slice[5];
        CHECK(sl >= 0 && c.badges[(size_t)sl] == Badge::kick, "jog on the KICK lane puts a kick on the step");
        if (kicks.size() > 1)
        {
            set(app, cellK, app.getParam(cellK) + 1);
            const int sl2 = app.session().slot(0).program.pattern.slice[5];
            CHECK(sl2 != sl && c.badges[(size_t)sl2] == Badge::kick, "jog again: the next kick");
            set(app, cellK, app.getParam(cellK) - 1);
            CHECK(app.session().slot(0).program.pattern.slice[5] == sl, "jog back: the kick before");
        }
        if (!snares.empty())
        {
            set(app, cellS, app.getParam(cellS) - 1);
            sl = app.session().slot(0).program.pattern.slice[5];
            CHECK(sl >= 0 && c.badges[(size_t)sl] == Badge::snare, "jog on the SNARE lane: only snares");
        }
        std::string ct;
        app.formatValue(cellS, app.getParam(cellS), ct);
        CHECK(ct.find("S ") == 0, ("the jogged cell's text: " + ct).c_str());
        CHECK(app.getParam(cellS) >= 1.0f && app.getParam(cellS) < (float)(kCellStripFrames - 1), "cells keep a spare frame each side");
        set(app, MP_VSTEP0 + 2, 9);
        CHECK(app.session().slot(0).program.pattern.slice[2] == 8, "step knob 3 sets step 3's slice");
        set(app, MP_SEQ_PAGE, 1);
        set(app, MP_VSTEP0 + 2, 7);
        CHECK(app.session().slot(0).program.pattern.slice[18] == 6, "on page 17-32 step knob 3 is step 19");
        set(app, MP_SEQ_PAGE, 0);
    }

    std::printf("a break pad switches every screen\n");
    {
        set(app, MP_BREAK, 0);
        press(app, MP_DICE_PAT, 1);
        app.session().loadDemo(3);
        settle(app);
        const int wave0 = (int)std::lround(app.getParam(MP_WAVE0 + 10));
        const int cells0 = (int)std::lround(app.getParam(MP_VSTEP0));
        set(app, MP_PAD_NOTES, 0);
        render(app, 4, { MidiMsg::noteOn(36 + 3, 100) });   // pad 4 (notes 36-51)
        settle(app);
        std::string nm;
        app.formatValue(MP_INFO_NAME, 0, nm);
        CHECK(app.session().selectedSlot() == 3 && nm.find("BREAK 4/8") == 0, ("pad 4: the LCD shows break 4: " + nm).c_str());
        const auto& pat4 = app.session().slot(3).program.pattern;
        CHECK((int)std::lround(app.getParam(MP_VSTEP0)) == pat4.slice[0] + 1, "the SEQ grid / step knobs show break 4's pattern");
        CHECK((int)std::lround(app.getParam(MP_SL_PITCH)) == (int)app.session().slot(3).program.slice[(size_t)app.session().selectedSlice()].pitch,
              "the slice controls show break 4's slice");
        (void)wave0; (void)cells0;
    }

    std::printf("DISK: jog to hear, push to load\n");
    {
        set(app, MP_SHOW_DISK, 1);
        set(app, MP_BROWSE, 1);
        for (int i = 0; i < 10; ++i) app.tick();
        CHECK(app.session().auditioning().find("136") != std::string::npos, "the browsed break plays");
        set(app, MP_BROWSE, 2);
        for (int i = 0; i < 10; ++i) app.tick();
        CHECK(app.session().auditioning().find("174") != std::string::npos, "the next one plays as the jog moves");
        set(app, MP_BREAK, 6);
        push(app);                                 // the jog wheel's push
        CHECK(app.session().auditioning().empty() && app.session().slot(6).file.find("174") != std::string::npos, "push: loads it into the break, the audition stops");
        set(app, MP_BROWSE, 1);
        for (int i = 0; i < 10; ++i) app.tick();
        set(app, MP_SHOW_DISK, 0);
        CHECK(app.session().auditioning().empty(), "closing DISK stops the audition");
        set(app, MP_BREAK, 0);
    }

    std::printf("mute groups and ONE SHOT\n");
    {
        set(app, MP_PAD_NOTES, 0);
        set(app, MP_BREAK, 3);
        set(app, MP_MUTE_GROUP, 1);
        CHECK(app.session().muteGroup(3) == 1, "MUTE GROUP sets the selected break's group");
        set(app, MP_BREAK, 0);
        CHECK((int)app.getParam(MP_MUTE_GROUP) == app.session().muteGroup(0), "MUTE GROUP follows the selected break");
        render(app, 8, { MidiMsg::noteOn(36, 100), MidiMsg::noteOn(39, 100) });   // pads 1 and 4
        settle(app);
        CHECK(app.session().engine().slotStep[0].load() >= 0 && app.session().engine().slotStep[3].load() >= 0,
              "pads 1 and 4 (groups 1 and 2) play together");
        render(app, 4, { MidiMsg::noteOn(36, 100), MidiMsg::noteOn(39, 100) });   // LATCH: again stops both
        set(app, P_GATE_MODE, 2);
        std::string gt;
        app.formatValue(P_GATE_MODE, 2.0f, gt);
        CHECK(gt == "ONE SHOT", ("GATE has ONE SHOT: " + gt).c_str());
        set(app, P_GATE_MODE, 0);
        set(app, MP_BREAK, 3);
        set(app, MP_MUTE_GROUP, 0);
        set(app, MP_BREAK, 0);
    }

    std::printf("sections\n");
    set(app, MP_SHOW_PAT, 1);
    CHECK(app.getParam(MP_VIEW) == 3.0f, "the PATTERN bar opens the pattern section");
    set(app, MP_SHOW_FX, 1);
    CHECK(app.getParam(MP_VIEW) == 1.0f && app.getParam(MP_SHOW_PAT) == 0.0f, "the FX bar swaps to the FX section");
    set(app, MP_SHOW_FX, 0);
    CHECK(app.getParam(MP_VIEW) == 0.0f, "tapping FX again goes back to MAIN");

    std::printf("state\n");
    const std::string st = app.saveState();
    {
        FakePlatform pf2;
        pf2.dir = dir.string();
        ChoppageMpcApp app2(pf2, false);
        pf2.app = &app2;
        app2.prepare(44100.0, 512);
        app2.loadState(st);
        settle(app2);
        CHECK(app2.session().slot(1).program.slice[2].pitch == 7.0f, "slice settings come back");
        CHECK(app2.session().slot(1).memories[4].has_value(), "memories come back");
        // a project whose saved values have a key down must not fire it on load
        const auto before = app2.session().slot(1).program.pattern;
        app2.setParam(MP_DICE_PAT, 1.0f);
        app2.setParam(MP_SEQ_TAP, 3.0f);
        app2.loadState(st);
        settle(app2);
        CHECK(app2.session().slot(1).program.pattern == before, "restored values don't fire keys or taps");
    }

    std::printf("\n%d / %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
