// The MPC app on the desktop, with a fake platform: parameter table, mirrors, triggers, pads, browser, state.
#include "ChoppageMpcApp.h"
#include "choppage/AudioFile.h"
#include "choppage/DemoBreak.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
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
    void notifyParameter(int index, float v) override { ++notified[index]; if (app) app->setParam(index, v); }
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

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--dump") return dump(argv[2]);
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
    CHECK(t.find("136") == 0, ("browser entry 1: " + t).c_str());
    app.formatValue(P_VOLUME, 0.0f, t);
    CHECK(t.find("dB") != std::string::npos || t.find("DB") != std::string::npos, ("core value text: " + t).c_str());

    std::printf("triggers\n");
    set(app, MP_BREAK, 1);
    CHECK(app.session().selectedSlot() == 1, "BREAK selects the break");
    set(app, MP_BROWSE, 1);
    set(app, MP_LOAD, 1);
    CHECK(app.getParam(MP_LOAD) == 0.0f, "LOAD springs back to 0");
    CHECK(app.session().slot(1).content != nullptr && app.session().slot(1).content->name.find("174") != std::string::npos,
          "LOAD puts the browsed file into break 2");
    CHECK(std::fabs(app.session().slot(1).content->bpm - 174.0) < 0.6, "tempo from the file name");

    set(app, MP_DICE_PAT, 1);
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
    set(app, MP_DICE_PAT, 1);
    CHECK(!(app.session().slot(1).program.pattern == rolled), "a second roll changes the pattern");
    set(app, MP_UNDO, 1);
    CHECK(app.session().slot(1).program.pattern == rolled, "UNDO goes back to the first roll");
    set(app, MP_REDO, 1);
    CHECK(app.getParam(MP_UNDO) == 0.0f && app.getParam(MP_REDO) == 0.0f, "UNDO / REDO spring back");

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
    set(app, MP_MEM_SAVE, 1);
    CHECK(app.session().slot(1).memories[4].has_value(), "SAVE into M05");
    set(app, MP_CLEAR, 1);
    CHECK(app.session().slot(1).program.pattern.hits() == 0, "CLEAR");
    set(app, MP_MEM_RECALL, 1);
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

    std::printf("akaize\n");
    set(app, MP_BREAK, 1);
    set(app, MP_AKAIZE, 1);
    app.formatValue(MP_INFO_MSG, 0, t);
    CHECK(t.find("AKAIZED") == 0, ("akaize message: " + t).c_str());
    CHECK(std::filesystem::exists(dir / "Renders") && !std::filesystem::is_empty(dir / "Renders"), "render written to Content/Renders");

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
    }

    std::printf("\n%d / %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
