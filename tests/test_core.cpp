/*
    test_core — choppage-core on its own (no JUCE, no host): the same engine the
    desktop and MPC plugins run.

      analysis · generators · playback (BAR SYNC, LATCH / HOLD, MIDI mode, swing, FIT)
      FX pads + character · editing, history, memories · state round trip
      WAV / AIFF in and out · break browser · Akaize render · bounce
*/
#include <thread>
#include "choppage/Analysis.h"
#include "choppage/AudioFile.h"
#include "choppage/DemoBreak.h"
#include "choppage/Generator.h"
#include "choppage/Params.h"
#include "choppage/Session.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace bc;
namespace fs = std::filesystem;

static int failures = 0, checks = 0;
static void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) ++failures;
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << std::endl;
}

struct Audio { std::vector<float> l, r; };

struct Rig
{
    Session s;
    std::vector<float> plain;
    Engine::Host host;
    double sr = 44100.0;
    int block = 512;
    bool autoKey = true;   // a break plays only once its key is pressed: the first run presses the selected break's key

    Rig()
    {
        for (int i = 0; i < kNumCoreParams; ++i) plain.push_back (paramDefs()[(size_t) i].def);
        host.playing = true; host.hasInfo = true; host.bpm = 170.0; host.ppq = 0.0;
        s.prepare (sr, block);
    }

    void set (int p, float v) { plain[(size_t) p] = v; }

    Audio run (double seconds, std::vector<MidiMsg> events = {})
    {
        const int total = (int) (seconds * sr);
        Audio a; a.l.assign ((size_t) total, 0.0f); a.r.assign ((size_t) total, 0.0f);
        auto settings = settingsFromParams (plain.data());
        if (autoKey)
        {
            autoKey = false;
            events.insert (events.begin(), MidiMsg::noteOn (kFirstBreakNote + s.selectedSlot(), 100));
        }
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            std::vector<MidiMsg> m;
            for (auto e : events) if (e.offset >= pos && e.offset < pos + n) { e.offset -= pos; m.push_back (e); }
            settings.sound.echoSamples = echoBeats ((int) plain[P_ECHO_TIME]) * sr * 60.0 / host.bpm;
            for (int p = 0; p < kFxPads; ++p) if (plain[(size_t) (P_PAD1 + p)] > 0.5f) s.setPadHeld (p, true);
            s.process (a.l.data() + pos, a.r.data() + pos, n, m.data(), (int) m.size(), host, settings);
            if (host.playing) host.ppq += n * host.bpm / 60.0 / sr;
            s.poll();
        }
        return a;
    }
};

static float rms (const Audio& a, int from = 0, int len = -1)
{
    if (len < 0) len = (int) a.l.size() - from;
    if (len <= 0) return 0.0f;
    double s = 0;
    for (int i = from; i < from + len; ++i) s += (double) a.l[(size_t) i] * a.l[(size_t) i];
    return (float) std::sqrt (s / len);
}
static bool finite (const Audio& a)
{
    for (size_t i = 0; i < a.l.size(); ++i)
        if (! std::isfinite (a.l[i]) || ! std::isfinite (a.r[i]) || std::abs (a.l[i]) > 8.0f) return false;
    return true;
}
static float difference (const Audio& a, const Audio& b)
{
    const size_t n = std::min (a.l.size(), b.l.size());
    double d = 0;
    for (size_t i = 0; i < n; ++i) d += std::abs (a.l[i] - b.l[i]);
    return (float) (d / std::max<size_t> (1, n));
}
static std::string fmt (double v, int dec = 1) { char b[32]; std::snprintf (b, sizeof b, "%.*f", dec, v); return b; }
static fs::path tmp() { auto p = fs::temp_directory_path() / "choppage-core-test"; fs::create_directories (p); return p; }

static void writeDemo (const std::string& path, double bpm)
{
    auto b = DemoBreak::make (44100.0, bpm);
    const float* ch[2] = { b->ch[0].data(), b->ch[1].data() };
    AudioFile::writeWav24 (path, ch, 2, b->numSamples, 44100.0);
}

// =====================================================================================
static void analysisTests()
{
    std::cout << "\n== Analysis ==" << std::endl;
    auto t = Analysis::estimateTempo (60.0 / 170.0 * 8.0, "amen");
    check (std::abs (t.bpm - 170.0) < 0.5 && t.bars == 2, "2 bars at 170 BPM from its length (" + fmt (t.bpm) + ")");
    auto h = Analysis::estimateTempo (60.0 / 136.0 * 4.0, "think_break_136");
    check (std::abs (h.bpm - 136.0) < 0.5 && h.bars == 1, "BPM hint in the file name (think_break_136 -> 136, 1 bar)");
    auto h2 = Analysis::estimateTempo (60.0 / 85.0 * 8.0, "funky 170bpm");
    check (std::abs (h2.bpm - 170.0) < 0.5, "name says 170: 85-BPM-length loop read as 4 bars at 170");

    auto buf = DemoBreak::make (44100.0, 170.0);
    SlotContent c;
    c.buffer = buf; c.bars = 2; c.bpm = 170.0;
    c.slices = Analysis::gridSlices (buf->numSamples, 2, 16);
    c.badges = Analysis::classify (*buf, c.slices);
    const char* lay = DemoBreak::layout();
    int right = 0, hits = 0;
    std::string got, want;
    for (int k = 0; k < 32; ++k)
    {
        if (lay[k] == '.') continue;
        ++hits;
        const char g = badgeLetter (c.badges[(size_t) k])[0];
        got += g; want += lay[k];
        if (g == lay[k]) ++right;
    }
    check (right == hits, "classifier badges every demo hit (" + std::to_string (right) + "/" + std::to_string (hits) + ": " + got + ")");
    auto orig = Analysis::originalPattern (c);
    check (orig.numSteps == 32 && orig.slice[0] == 0 && orig.slice[31] == 31, "original pattern = slices in order on their 16ths");
}

static void generatorTests()
{
    std::cout << "\n== Generators ==" << std::endl;
    auto buf = DemoBreak::make();
    auto c = std::make_shared<SlotContent>();
    c->buffer = buf; c->bars = 2; c->bpm = 170.0;
    c->slices = Analysis::gridSlices (buf->numSamples, 2, 16);
    c->badges = Analysis::classify (*buf, c->slices);

    Generator::Input in;
    in.content = c.get();
    in.original = Analysis::originalPattern (*c);
    in.current = in.original;
    in.amount = 0.0f; in.layers = false; in.seed = 42;
    check (Generator::pattern (in) == in.original, "AMOUNT 0 gives back the original order");
    in.amount = 1.0f;
    auto a = Generator::pattern (in);
    check (! (a == in.original), "AMOUNT 100% rewrites the pattern");
    check (a == Generator::pattern (in), "same seed, same pattern");
    in.current.slice[5] = 3; in.current.locked[5] = true;
    check (Generator::pattern (in).slice[5] == 3, "a locked step survives the dice");
    // every style, many rolls: a kick on the one, and the grooves' backbeat - most bars have a snare on 2 or 4 (step 5 / 13)
    for (int st = 0; st < kNumStyles; ++st)
    {
        int kickOnOne = 0, backbeat = 0, rolls = 40;
        for (int seed = 0; seed < rolls; ++seed)
        {
            Generator::Input g = in;
            g.current = g.original; g.style = (Style) st; g.seed = seed * 31 + 7; g.amount = 1.0f;
            const auto p = Generator::pattern (g);
            auto badgeAt = [&] (int k) { return p.slice[(size_t) k] >= 0 ? c->badges[(size_t) p.slice[(size_t) k]] : Badge::other; };
            kickOnOne += badgeAt (0) == Badge::kick;
            backbeat += badgeAt (4) == Badge::snare || badgeAt (12) == Badge::snare || badgeAt (8) == Badge::snare;
        }
        check (kickOnOne == rolls && backbeat >= rolls * 3 / 4, std::string (styleNames()[(size_t) st]) + ": a kick on the one every roll, a snare backbeat in "
                                                                + std::to_string (backbeat) + " / " + std::to_string (rolls));
    }
    auto fx = Generator::fx (in.original, 1.0f, 7);
    int ev = 0; for (int i = 0; i < fx.numSteps; ++i) ev += fx.fx[(size_t) i].type != FxType::none;
    check (ev > 2 && fx.fx[0].type == FxType::none, "FX dice scatters events and leaves the one clean (" + std::to_string (ev) + ")");
    const int nxt = Generator::nextWithSameBadge (*c, 4);
    check (nxt != 4 && c->badges[(size_t) nxt] == c->badges[4], "swap finds another slice with the same badge");
}

static void playbackTests()
{
    std::cout << "\n== Playback ==" << std::endl;
    {
        Rig quiet;
        quiet.autoKey = false;
        check (rms (quiet.run (0.5)) < 0.0001f, "host playing, no break key pressed: silent (a break plays only when its key triggers it)");
        Rig r;
        check (r.s.slot (0).status == Session::Status::ready, "demo break ready in slot 1");
        auto a = r.run (1.0);
        check (rms (a) > 0.01f && finite (a), "C1 pressed, host playing: the break plays in time (rms " + fmt (rms (a), 3) + ")");
        r.host.ppq = 4.0;
        r.run (0.01);
        check (r.s.engine().currentStep.load() == 16, "BAR SYNC: bar 2 starts on step 17 (got " + std::to_string (r.s.engine().currentStep.load() + 1) + ")");
        r.host.ppq = 2.0;    // relocate backwards (loop brace)
        r.run (0.01);
        check (r.s.engine().currentStep.load() == 8, "BAR SYNC follows a relocate (step 9)");
        r.s.loadDemo (1);
        r.run (0.2, { MidiMsg::noteOn (kFirstBreakNote + 1, 100) });
        check (r.s.engine().playingSlot.load() == 1, "C#1 switches to break 2");
        r.host.playing = false;
        auto stopped = r.run (0.5);
        check (rms (stopped, (int) (0.2 * r.sr)) < 0.001f, "host stop: silence");
    }
    {
        Rig r;
        r.autoKey = false;
        r.host.playing = false;
        check (rms (r.run (0.3)) < 0.0001f, "host stopped, nothing latched: silent");
        auto on = r.run (0.6, { MidiMsg::noteOn (kFirstBreakNote, 100), MidiMsg::noteOff (kFirstBreakNote, 1000) });
        check (rms (on) > 0.01f, "LATCH: a tap on C1 runs the break on the internal clock");
        r.run (0.1, { MidiMsg::noteOn (kFirstBreakNote, 100) });
        check (rms (r.run (0.4), (int) (0.1 * r.sr)) < 0.001f, "LATCH: the same key again stops it");
        r.set (P_GATE_MODE, 1);
        auto held = r.run (0.5, { MidiMsg::noteOn (kFirstBreakNote, 100), MidiMsg::noteOff (kFirstBreakNote, (int) (0.25 * r.sr)) });
        check (rms (held, 0, (int) (0.2 * r.sr)) > 0.01f && rms (held, (int) (0.33 * r.sr)) < 0.001f, "HOLD: plays while held, stops on release");
    }
    {
        // ONE SHOT: one note plays the pattern to its end, then stops (the note-off doesn't stop it)
        Rig r;
        r.autoKey = false;
        r.set (P_GATE_MODE, 2);
        const int steps = r.s.slot (0).program.pattern.numSteps;
        const double stepSec = 60.0 / r.host.bpm / 4.0;
        auto a = r.run (stepSec * steps * 0.5, { MidiMsg::noteOn (kFirstBreakNote, 100), MidiMsg::noteOff (kFirstBreakNote, 200) });
        check (rms (a) > 0.01f && r.s.engine().slotStep[0].load() >= 0, "ONE SHOT: a note starts the pattern; its note-off doesn't stop it");
        r.run (stepSec * steps * 0.5 + 0.05);
        auto after = r.run (0.4);
        check (r.s.engine().slotStep[0].load() < 0 && rms (after, (int) (0.05 * r.sr)) < 0.001f,
               "ONE SHOT: stops at the end of the pattern (" + std::to_string (steps) + " steps)");
        r.run (0.3, { MidiMsg::noteOn (kFirstBreakNote, 100) });
        check (r.s.engine().slotStep[0].load() >= 0, "ONE SHOT: the next note plays it again");
    }
    {
        // Mute groups: same group hands over (carrying on from the same point in the bar), different groups layer
        Rig r;
        r.autoKey = false;
        r.s.loadDemo (1);
        r.s.loadDemo (2);
        r.s.setMuteGroup (2, 1);
        check (r.s.muteGroup (2) == 1, "break 3 in mute group 2");
        r.run (0.3, { MidiMsg::noteOn (kFirstBreakNote, 100) });
        const int before = r.s.engine().slotStep[0].load();
        auto hand = r.run (0.01, { MidiMsg::noteOn (kFirstBreakNote + 1, 100) });
        const int after = r.s.engine().slotStep[1].load();
        check (r.s.engine().slotStep[0].load() < 0 && after >= 0, "same group: break 2 takes over from break 1");
        check (after >= before && after <= before + 1, "BAR SYNC: break 2 carries on from the same step (" + std::to_string (before + 1) + " -> "
                                                       + std::to_string (after + 1) + ")");
        r.run (0.2, { MidiMsg::noteOn (kFirstBreakNote + 2, 100) });
        check (r.s.engine().slotStep[1].load() >= 0 && r.s.engine().slotStep[2].load() >= 0, "different groups: break 2 and break 3 play together");
        check (r.s.engine().slotStep[1].load() % 16 == r.s.engine().slotStep[2].load() % 16, "... in step with each other");
        auto st = r.s.saveState();
        Session t;
        t.prepare (44100.0, 512);
        t.loadState (st);
        t.waitUntilIdle();
        t.poll();
        check (t.muteGroup (2) == 1 && t.muteGroup (1) == 0, "mute groups are saved with the session");
        r.s.loadDemo (2);
        r.s.waitUntilIdle();
        r.s.poll();
        check (r.s.muteGroup (2) == 1, "loading another break into a slot keeps its mute group");
        r.host.playing = false;
        r.run (0.1);
        check (r.s.engine().slotStep[1].load() < 0 && r.s.engine().slotStep[2].load() < 0, "MPC / host stop ends both");
    }
    {
        Rig r;
        r.host.playing = false;
        r.set (P_TRIGGER, 1);
        auto a = r.run (0.4, { MidiMsg::noteOn (kFirstSliceNote, 100, 100) });
        check (rms (a, 0, 90) < 0.0001f && rms (a, 100) > 0.005f, "MIDI mode: E2 plays slice 1 on the sample it arrives");
    }
    {
        Rig a, b; b.set (P_SWING, 0.7f);
        check (difference (a.run (2.0), b.run (2.0)) > 0.002f, "SWING moves the off-beat 16ths");
        Rig k, q; k.set (P_FIT_MODE, 1); k.host.bpm = 120; q.host.bpm = 120;
        auto ka = k.run (1.0);
        check (difference (ka, q.run (1.0)) > 0.002f && finite (ka), "FIT: REPITCH and KEEP differ at 120 BPM");
    }
}

static void fxTests()
{
    std::cout << "\n== FX pads and character ==" << std::endl;
    Rig base;
    auto dry = base.run (1.5);
    for (int pad = 0; pad < kFxPads; ++pad)
    {
        Rig r;
        auto wet = r.run (1.5, { MidiMsg::noteOn (kFirstFxNote + pad, 110, (int) (0.4 * r.sr)), MidiMsg::noteOff (kFirstFxNote + pad, (int) (1.2 * r.sr)) });
        const float d = difference (dry, wet);
        check (d > 0.001f && finite (wet), std::string ("pad ") + fxPadName (pad) + " changes the sound (" + fmt (d, 4) + ")");
    }
    {
        // the AKAIZE pad held from the screen (not MIDI), the host playing and stopped (CHOPPAGE's own clock)
        for (bool hostPlays : { true, false })
            for (bool inTime : { false, true })
            {
                auto runIt = [&] (bool held) {
                    Rig r;
                    r.host.playing = hostPlays;
                    r.set (P_AK_MODE, inTime ? 1.0f : 0.0f);
                    if (! hostPlays) { r.autoKey = false; r.s.setPlaying (true); }
                    Audio a = r.run (0.4);
                    if (held) r.s.setPadHeld (PerfFx::akaize, true);
                    Audio b = r.run (0.8);
                    return b;
                };
                const float d = difference (runIt (false), runIt (true));
                check (d > 0.01f, std::string ("AKAIZE held on screen, host ") + (hostPlays ? "playing" : "stopped") + (inTime ? ", IN TIME" : ", STRETCH")
                                    + " changes the sound (" + fmt (d, 4) + ")");
            }
    }
    {
        // the Akaizer's settings, live: CYCLE LENGTH / FREQ on the AKAIZE pad and on per-slice STRETCH
        check (akaizeFreq (800, 300, false) == 51 && akaizeFreq (400, 300, false) == 102 && akaizeFreq (800, 300, true) == 300,
               "CYCLE FREQ: linked 41000 / length like the original (800 -> 51), or MANUAL");
        auto padRun = [] (float len, float freq, bool manual) {
            Rig r;
            r.set (P_AK_CYC_LEN, len); r.set (P_AK_CYC_FREQ, freq); r.set (P_AK_FREQ_MAN, manual ? 1.0f : 0.0f);
            return r.run (1.5, { MidiMsg::noteOn (kFirstFxNote + PerfFx::akaize, 110, (int) (0.4 * r.sr)),
                                 MidiMsg::noteOff (kFirstFxNote + PerfFx::akaize, (int) (1.2 * r.sr)) });
        };
        auto a = padRun (800, 51, false), b = padRun (200, 51, false), c = padRun (800, 400, true);
        check (difference (a, b) > 0.001f && difference (a, c) > 0.001f && finite (b) && finite (c),
               "AKAIZE pad: CYCLE LENGTH and a MANUAL FREQ change it");
        check (rms (a, (int) (0.5 * 44100), (int) (0.6 * 44100)) > 0.3f * rms (dry, (int) (0.5 * 44100), (int) (0.6 * 44100)),
               "AKAIZE pad keeps its level (overlapping grains scaled)");
        auto sliceRun = [] (float len) {
            Rig r;
            r.set (P_SL_CYC_LEN, len);
            r.s.setSliceValue (Session::SliceField::stretch, 2.0f, true);
            return r.run (1.0);
        };
        auto s1 = sliceRun (800), s2 = sliceRun (100);
        check (difference (s1, s2) > 0.001f && finite (s2) && rms (s1) > 0.01f, "slice STRETCH x2: CYCLE LENGTH changes it");
        // AKAIZE MODE IN TIME: the grains (and the metallic sound) but the beat kept
        auto modeRun = [] (bool inTime, float pitch) {
            Rig r;
            r.set (P_AK_MODE, inTime ? 1.0f : 0.0f); r.set (P_AK_PITCH, pitch);
            return r.run (1.5, { MidiMsg::noteOn (kFirstFxNote + PerfFx::akaize, 110, (int) (0.4 * r.sr)),
                                 MidiMsg::noteOff (kFirstFxNote + PerfFx::akaize, (int) (1.2 * r.sr)) });
        };
        auto st = modeRun (false, 0), it = modeRun (true, 0), itUp = modeRun (true, 12);
        // ... and IN TIME at pitch 0 is really heard: not the dry break (the grains repeat, then jump to the beat)
        auto dry = [] { Rig r; return r.run (1.5); }();
        check (difference (dry, it) > 0.01f, "AKAIZE MODE IN TIME at pitch 0 changes the sound (not the dry break): " + fmt (difference (dry, it), 4));
        check (difference (st, it) > 0.001f && difference (it, itUp) > 0.001f && finite (it) && finite (itUp)
                 && rms (it, (int) (0.5 * 44100), (int) (0.6 * 44100)) > 0.01f,
               "AKAIZE MODE IN TIME on the pad differs from STRETCH, IN TIME PITCH changes it");
        auto sliceMode = [] (bool inTime) {
            Rig r;
            r.set (P_AK_MODE, inTime ? 1.0f : 0.0f);
            r.s.setSliceValue (Session::SliceField::stretch, 2.0f, true);
            return r.run (1.0);
        };
        auto sSt = sliceMode (false), sIt = sliceMode (true);
        auto sDry = [] { Rig r; return r.run (1.0); }();
        check (difference (sDry, sIt) > 0.01f, "slice STRETCH x2 IN TIME changes the sound: " + fmt (difference (sDry, sIt), 4));
        check (difference (sSt, sIt) > 0.001f && finite (sIt) && rms (sIt) > 0.01f, "slice STRETCH x2 IN TIME plays the grains at normal speed");
    }
    {
        Rig clean, s950, low;
        clean.set (P_CHARACTER, 0);
        low.set (P_SRATE, 4000.0f);
        auto a = clean.run (1.0), b = s950.run (1.0), c = low.run (1.0);
        check (difference (a, b) > 0.001f, "S950 character differs from CLEAN");
        check (difference (b, c) > 0.001f && finite (c), "SAMPLE RATE fader at 4 kHz crunches it");
    }
    {
        Rig r;
        r.set (P_DIST, 0.8f); r.set (P_CRUSH, 0.7f); r.set (P_REVERB, 0.6f); r.set (P_ECHO_MIX, 0.5f); r.set (P_FILTER, 0.3f);
        auto a = r.run (2.0);
        check (finite (a) && rms (a) > 0.005f, "all post FX at once: stable");
        r.set (P_FILTER_TYPE, 1); r.set (P_FILTER, 0.9f);
        check (finite (r.run (0.5)), "HP filter: stable");
    }
    {
        Rig r;
        for (int st = 1; st < 32; st += 2) r.s.setFxStep (st, FxType::stutter, 32);
        auto a = r.run (1.5);
        check (difference (dry, a) > 0.001f && finite (a), "FX lane events change the sound");
    }
}

static void editTests()
{
    std::cout << "\n== Editing, history, state ==" << std::endl;
    Rig r;
    const auto& s = r.s.slot (0);
    r.s.rollPattern (Style::jungle, 0.8f, true);
    r.s.rollPattern (Style::jungle, 0.8f, true);
    const auto after2 = s.current;
    r.s.historyBack();
    check (! (s.current == after2), "history back goes back a roll");
    r.s.historyForward();
    check (s.current == after2, "history forward comes back");
    r.s.setShowOriginal (true);
    check (s.program.pattern == s.original, "ORIGINAL plays the break as recorded");
    r.s.setShowOriginal (false);

    r.s.selectSlice (4, false);
    r.s.setSliceValue (Session::SliceField::pitch, 5.0f, false);
    check (std::abs (s.program.slice[4].pitch - 5.0f) < 1e-6f && std::abs (s.program.slice[3].pitch) < 1e-6f, "SLICE edits only the selected slice");
    r.s.setSliceValue (Session::SliceField::gate, 0.5f, true);
    check (std::abs (s.program.all.gate - 0.5f) < 1e-6f, "ALL edits the whole break");
    const int before = s.content->numSlices();
    r.s.addMarker ((s.content->slices.starts[2] + s.content->slices.starts[3]) / 2);
    check (s.content->numSlices() == before + 1, "add a marker");
    r.s.deleteMarker (3);
    check (s.content->numSlices() == before, "delete it");
    r.s.setBadge (6, Badge::open);
    r.s.toggleMute (7);
    check (s.content->badges[6] == Badge::open && s.program.slice[7].mute, "badge override + mute");
    r.s.setStepSlice (2, 9);
    check (s.current.slice[2] == 9, "set a step's slice (MPC step knob)");
    r.s.saveMemory (3);
    r.s.clearPattern();
    check (s.current.hits() == 0, "clear pattern");
    r.s.recallMemory (3);
    check (s.current.hits() > 0, "memory 4 recalled");
    r.s.setFxStep (9, FxType::reverse, 16);

    const auto state = r.s.saveState();
    Rig q;
    q.s.loadState (state);
    q.s.waitUntilIdle();
    const auto& t = q.s.slot (0);
    check (t.status == Session::Status::ready && t.current == s.current, "restore: pattern + FX lane");
    check (t.program.slice[7].mute && std::abs (t.program.all.gate - 0.5f) < 1e-6f && t.content->badges[6] == Badge::open, "restore: mutes, ALL, badges");
    check (t.memories[3].has_value(), "restore: memories");
    check (finite (q.run (0.5)), "restored session plays");

    // memory notes from the keyboard / pads
    r.run (0.05, { MidiMsg::noteOn (kFirstMemoryNote + 3, 100) });
    r.s.clearPattern();
    r.run (0.05, { MidiMsg::noteOn (kFirstMemoryNote + 3, 100) });
    check (s.current.hits() > 0, "G#3+3 (memory 4) recalls on the next poll");
}

static void fileTests()
{
    std::cout << "\n== Files, browser, renders ==" << std::endl;
    const auto dir = tmp();
    const auto wav = (dir / "think_break_136.wav").string();
    writeDemo (wav, 136.0);
    Rig r;
    r.s.loadFile (2, wav);
    r.s.waitUntilIdle();
    const auto& w = r.s.slot (2);
    check (w.status == Session::Status::ready && w.content && std::abs (w.content->bpm - 136.0) < 0.5, "WAV loads and reads 136 BPM from its name");
    check (w.content && w.content->numSlices() >= 8, "transient slicer finds the hits (" + std::to_string (w.content ? w.content->numSlices() : 0) + ")");
    {
        Rig a;
        a.autoKey = false;
        a.host.playing = false;
        check (a.s.audition (wav) && a.s.auditioning() == wav, "audition decodes the browsed file");
        check (rms (a.run (0.5)) > 0.01f, "audition plays with no break running");
        check (! a.s.audition ("/no/such/break.wav") && a.s.auditioning().empty(), "an unreadable file stops the audition");
        a.s.audition (wav);
        a.run (0.1);
        a.s.auditionStop();
        a.run (0.05);
        check (rms (a.run (0.3)) < 0.0001f, "auditionStop: silence");
    }
    r.s.loadFile (3, "/no/such/break.wav");
    r.s.waitUntilIdle();
    check (r.s.slot (3).status == Session::Status::missing, "a missing file says so");

    // AIFF (big-endian 16-bit) written by hand
    {
        const auto aif = (dir / "aiff_break_170.aif").string();
        auto b = DemoBreak::make (44100.0, 170.0);
        std::vector<uint8_t> d;
        auto be32 = [&d] (uint32_t v) { for (int i = 3; i >= 0; --i) d.push_back ((uint8_t) (v >> (8 * i))); };
        auto be16 = [&d] (uint16_t v) { d.push_back ((uint8_t) (v >> 8)); d.push_back ((uint8_t) v); };
        const uint32_t frames = (uint32_t) b->numSamples, ssnd = frames * 4 + 8;
        d.insert (d.end(), { 'F','O','R','M' }); be32 (4 + 26 + 8 + ssnd + 8); d.insert (d.end(), { 'A','I','F','F' });
        d.insert (d.end(), { 'C','O','M','M' }); be32 (18); be16 (2); be32 (frames); be16 (16);
        const uint8_t rate[10] = { 0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0 }; d.insert (d.end(), rate, rate + 10);
        d.insert (d.end(), { 'S','S','N','D' }); be32 (ssnd); be32 (0); be32 (0);
        for (uint32_t i = 0; i < frames; ++i) for (int c = 0; c < 2; ++c) be16 ((uint16_t) (int16_t) std::lround (b->ch[c][i] * 32767.0f));
        FILE* f = std::fopen (aif.c_str(), "wb"); std::fwrite (d.data(), 1, d.size(), f); std::fclose (f);
        AudioFile::Status st;
        auto a = AudioFile::load (aif, st);
        check (st == AudioFile::Status::ok && a && a->numSamples == b->numSamples && std::abs (a->sampleRate - 44100.0) < 1.0
               && std::abs (a->ch[0][5000] - b->ch[0][5000]) < 0.001f, "AIFF reads back sample-exact (16-bit)");
    }

    // browser: sorted by BPM
    writeDemo ((dir / "fast_175.wav").string(), 175.0);
    writeDemo ((dir / "slow_94.wav").string(), 94.0);
    r.s.scanFolder (dir.string());
    r.s.waitUntilIdle();
    auto e = r.s.browserEntries();
    bool sorted = e.size() >= 4;
    for (size_t i = 1; i < e.size(); ++i) sorted = sorted && e[i - 1].bpm <= e[i].bpm;
    check (sorted && std::abs (e.front().bpm - 94.0) < 0.5, "browser lists " + std::to_string (e.size()) + " breaks, slowest first (" + (e.empty() ? std::string ("-") : e.front().name) + ")");

    // Akaize render: 136 -> 170 BPM
    r.s.setDataFolder (dir.string());
    r.s.selectSlot (2);
    const int before = r.s.slot (2).content->buffer->numSamples;
    check (r.s.akaizeSlot (2, 170.0, 0.0, 800), "AKAIZE RENDER runs");
    const auto& k = r.s.slot (2);
    const double ratio = (double) k.content->buffer->numSamples / before;
    check (std::abs (ratio - 136.0 / 170.0) < 0.01 && std::abs (k.content->bpm - 170.0) < 0.01, "renders to 170 BPM (length x" + fmt (ratio, 3) + ")");
    check (fs::exists (k.file) && k.file.find ("Renders") != std::string::npos, "render saved in Renders/ so the project finds it");
    {
        // the file is what Akaizer writes: 16-bit, <name>_akaized.wav
        std::ifstream f (k.file, std::ios::binary);
        char h[36] {};
        f.read (h, 36);
        const int bits = (unsigned char) h[34] | ((unsigned char) h[35] << 8);
        check (bits == 16 && k.file.find ("_akaized") != std::string::npos, "render is a 16-bit _akaized.wav, like the original (" + fs::path (k.file).filename().string() + ")");
    }
    {
        // the full settings: STRETCH by hand, SYNC with a hand-set LOOP BPM, PITCH, MANUAL FREQ; never over an earlier file
        r.s.loadDemo (3);
        r.s.waitUntilIdle(); r.s.poll();
        const int n0 = r.s.slot (3).content->buffer->numSamples;
        Session::AkaizeRender set;
        set.sync = false; set.stretchPercent = 250.0; set.cycleLength = 400; set.cycleFreq = 103;
        check (r.s.akaizeSlot (3, set), "render: STRETCH 250% by hand");
        const double rr = (double) r.s.slot (3).content->buffer->numSamples / n0;
        check (std::abs (rr - 2.5) < 0.01, "... 2.5 x as long (x" + fmt (rr, 3) + ")");
        const auto first = r.s.slot (3).file;
        r.s.loadDemo (3);
        r.s.waitUntilIdle(); r.s.poll();
        r.s.setLoopBpm (3, 85.0);
        check (r.s.loopBpmSet (3) && std::abs (r.s.loopBpm (3) - 85.0) < 0.01, "LOOP BPM set by hand");
        set.sync = true; set.targetBpm = 170.0; set.pitch = 12.0; set.cycleFreq = 700;
        check (r.s.akaizeSlot (3, set), "render: SYNC 85 -> 170 BPM, PITCH +12, FREQ 700 cps");
        const double rs = (double) r.s.slot (3).content->buffer->numSamples / n0;
        check (std::abs (rs - 0.5) < 0.01 && std::abs (r.s.slot (3).content->bpm - 170.0) < 0.01 && ! r.s.loopBpmSet (3),
               "... half the length, now 170 BPM (x" + fmt (rs, 3) + ")");
        check (r.s.slot (3).file != first && fs::exists (first), "a second render gets its own file");
        r.s.loadDemo (3);
        r.s.waitUntilIdle(); r.s.poll();
        set.inTime = true; set.pitch = 7.0;
        check (r.s.akaizeSlot (3, set), "render: AKAIZE MODE IN TIME, PITCH +7");
        const double ri = (double) r.s.slot (3).content->buffer->numSamples / n0;
        check (std::abs (ri - 1.0) < 0.01, "... IN TIME keeps the length, the beat (x" + fmt (ri, 3) + ")");
    }

    // bounce
    r.s.selectSlot (0);
    auto settings = settingsFromParams (r.plain.data());
    auto b = r.s.bounce (settings, 170.0, 44100.0);
    const int expect = (int) std::ceil (32 / 4.0 * 60.0 / 170.0 * 44100.0);
    check (b && b->numSamples == expect, "bounce: one pattern pass (" + std::to_string (b ? b->numSamples : 0) + " samples)");
    fs::remove_all (dir);
}

static void paramTests()
{
    std::cout << "\n== Params ==" << std::endl;
    check ((int) paramDefs().size() == kNumCoreParams, "table matches the index enum (" + std::to_string (paramDefs().size()) + ")");
    check (paramIndex ("srate") == P_SRATE && paramIndex ("pad8") == P_PAD8 && paramIndex ("lcdColour") == P_LCD_COLOUR, "ids sit at their indices");
    const float n = paramToNorm (P_SRATE, 15000.0f);
    check (std::abs (n - 0.5f) < 0.01f && std::abs (paramFromNorm (P_SRATE, n) - 15000.0f) < 1.0f, "SAMPLE RATE skew: 15 kHz mid-travel");
    check (formatParam (P_SRATE, 10000.0f) == "10.0 kHz" && formatParam (P_PAT_AMOUNT, 0.35f) == "35%" && formatParam (P_STYLE, 2) == "ROLLERS",
           "value text: " + formatParam (P_SRATE, 10000.0f) + " / " + formatParam (P_PAT_AMOUNT, 0.35f) + " / " + formatParam (P_STYLE, 2));
}

// WAV files as real apps write them (24-bit 44.1 kHz stereo): the reader must load every one.
static void wavVariantTests()
{
    std::cout << "WAV variants" << std::endl;
    const int frames = 1000;
    std::vector<uint8_t> pcm;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const int v = (int) (0.5 * 8388607 * std::sin (i * 0.05));
            pcm.push_back ((uint8_t) (v & 0xff)); pcm.push_back ((uint8_t) ((v >> 8) & 0xff)); pcm.push_back ((uint8_t) ((v >> 16) & 0xff));
        }
    auto u32 = [] (std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back ((uint8_t) (v >> (8 * i))); };
    auto u16 = [] (std::vector<uint8_t>& o, uint16_t v) { o.push_back ((uint8_t) v); o.push_back ((uint8_t) (v >> 8)); };
    auto chunk = [&] (const char* id, const std::vector<uint8_t>& body, bool pad, int64_t size) {
        std::vector<uint8_t> o (id, id + 4);
        u32 (o, size >= 0 ? (uint32_t) size : (uint32_t) body.size());
        o.insert (o.end(), body.begin(), body.end());
        if (pad && (body.size() & 1)) o.push_back (0);
        return o;
    };
    auto fmt = [&] (bool ext) {
        std::vector<uint8_t> b;
        u16 (b, ext ? 0xFFFE : 1); u16 (b, 2); u32 (b, 44100); u32 (b, 44100 * 6); u16 (b, 6); u16 (b, 24);
        if (ext)
        {
            u16 (b, 22); u16 (b, 24); u32 (b, 3); u16 (b, 1);
            const uint8_t g[14] = { 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 };
            b.insert (b.end(), g, g + 14);
        }
        return chunk ("fmt ", b, true, -1);
    };
    auto riff = [&] (std::vector<std::vector<uint8_t>> parts, int64_t size) {
        std::vector<uint8_t> body = { 'W', 'A', 'V', 'E' };
        for (auto& p : parts) body.insert (body.end(), p.begin(), p.end());
        std::vector<uint8_t> o = { 'R', 'I', 'F', 'F' };
        u32 (o, size >= 0 ? (uint32_t) size : (uint32_t) body.size());
        o.insert (o.end(), body.begin(), body.end());
        return o;
    };
    const std::vector<uint8_t> odd = { 'I', 'N', 'F', 'O', 'I', 'S', 'F', 'T', 5, 0, 0, 0, 'L', 'o', 'g', 'i', 'c' };
    const std::vector<uint8_t> atem = { '{', '"', 'x', '"', ':', '1', '}' };
    struct V { const char* name; std::vector<uint8_t> bytes; };
    std::vector<V> vs = {
        { "plain", riff ({ fmt (false), chunk ("data", pcm, true, -1) }, -1) },
        { "WAVE_FORMAT_EXTENSIBLE", riff ({ fmt (true), chunk ("data", pcm, true, -1) }, -1) },
        { "odd LIST chunk without its pad byte", riff ({ fmt (false), chunk ("LIST", odd, false, -1), chunk ("data", pcm, true, -1) }, -1) },
        { "odd atem chunk without its pad byte (MPC)", riff ({ fmt (false), chunk ("atem", atem, false, -1), chunk ("data", pcm, true, -1) }, -1) },
        { "data size 0xFFFFFFFF", riff ({ fmt (false), chunk ("data", pcm, true, 0xFFFFFFFFll) }, 0xFFFFFFFFll) },
        { "data size 0", riff ({ fmt (false), chunk ("data", pcm, true, 0) }, 0) },
        { "data before fmt", riff ({ chunk ("data", pcm, true, -1), fmt (false) }, -1) },
        { "JUNK + bext + smpl", riff ({ chunk ("JUNK", std::vector<uint8_t> (28), true, -1), chunk ("bext", std::vector<uint8_t> (602), true, -1),
                                        fmt (false), chunk ("smpl", std::vector<uint8_t> (36), true, -1), chunk ("data", pcm, true, -1) }, -1) },
    };
    const auto dir = fs::temp_directory_path() / "choppage-wav-variants";
    fs::create_directories (dir);
    for (auto& v : vs)
    {
        const auto path = (dir / (std::string (v.name).substr (0, 12) + ".wav")).string();
        FILE* f = std::fopen (path.c_str(), "wb");
        std::fwrite (v.bytes.data(), 1, v.bytes.size(), f);
        std::fclose (f);
        AudioFile::Status st;
        auto s = AudioFile::load (path, st);
        check (s != nullptr && s->numSamples == frames && s->channels == 2
                   && std::fabs (s->ch[0][20] - (float) (0.5 * std::sin (20 * 0.05))) < 1e-4f,
               std::string ("24-bit WAV loads: ") + v.name);
    }
}

static void quantiseTests()
{
    std::cout << "\n== Pad quantise, reset to original ==" << std::endl;
    for (int choice : { 2, 0, 5 })   // 1/8, OFF, 1 BAR
    {
        Rig r;
        r.block = 64;
        r.set (P_PAD_QUANT, (float) choice);
        r.run (0.3);
        const double q = choice == 2 ? 2.0 : choice == 5 ? 16.0 : 0.0;
        // press off the grid
        while (q > 0.0 && std::fmod (r.host.ppq * 4.0, q) < q * 0.4) r.run (64.0 / r.sr);
        const double pressAt = r.host.ppq * 4.0;
        r.s.setPadHeld (PerfFx::reverse, true);
        double startAt = -1;
        for (int i = 0; i < 4000 && startAt < 0; ++i) { r.run (64.0 / r.sr); if (r.s.engine().padActive[PerfFx::reverse]) startAt = r.host.ppq * 4.0; }
        const double chunk16 = 64.0 * r.host.bpm / 60.0 / r.sr * 4.0;
        if (q == 0.0)
            check (startAt >= 0 && startAt - pressAt <= chunk16 + 1e-9, "PAD QUANTISE OFF: the pad acts at once");
        else
        {
            const double line = std::ceil (pressAt / q) * q;
            check (startAt >= line && startAt - line <= 2.0 * chunk16 + 1e-9,
                   "PAD QUANTISE " + std::string (choice == 2 ? "1/8" : "1 BAR") + ": an early press waits for the next line (pressed "
                   + std::to_string (pressAt) + ", started " + std::to_string (startAt) + ", line " + std::to_string (line) + ")");
            // a quick tap (let go straight away) still plays to the following line
            r.s.setPadHeld (PerfFx::reverse, false);
            double stopAt = -1;
            for (int i = 0; i < 8000 && stopAt < 0; ++i) { r.run (64.0 / r.sr); if (! r.s.engine().padActive[PerfFx::reverse]) stopAt = r.host.ppq * 4.0; }
            check (stopAt >= line + q && stopAt - (line + q) <= 2.0 * chunk16 + 1e-9, "letting go ends it on the next line");
        }
    }
    {
        Rig r;
        r.s.loadDemo (0);
        r.s.waitUntilIdle();
        r.s.rollPattern (Style::jungle, 0.9f, true);
        r.s.rollFx (1.0f);
        const auto& sl = r.s.slot (0);
        bool anyFx = false; for (auto& f : sl.current.fx) anyFx = anyFx || f.type != FxType::none;
        check (! (sl.current.slice == sl.original.slice) && anyFx, "dice changed the pattern and the FX lane");
        r.s.resetToOriginal();
        bool fxLeft = false; for (auto& f : sl.current.fx) fxLeft = fxLeft || f.type != FxType::none;
        check (sl.current.slice == sl.original.slice && ! fxLeft && ! sl.showOriginal, "RESET: the break as recorded, FX lane empty");
        r.s.historyBack();
        check (! (r.s.slot (0).current.slice == r.s.slot (0).original.slice), "RESET can be undone");
    }
}

static void snapTests()
{
    std::cout << "\n== Snap to hits ==" << std::endl;
    // a made-up break: 8 hits on known samples, loud and quiet (decaying noise), over a quiet bed
    const double sr = 44100.0;
    const int n = (int) (sr * 2.0);
    std::vector<float> x ((size_t) n);
    uint32_t seed = 1;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f; };
    for (auto& v : x) v = 0.003f * rnd();
    const int hits[] = { 2000, 11500, 22050, 30000, 44100, 55125, 66150, 77000 };
    const float amp[] = { 0.9f, 0.25f, 0.8f, 0.15f, 0.9f, 0.3f, 0.7f, 0.2f };
    for (int h = 0; h < 8; ++h)
        for (int i = 0; i < 9000 && hits[h] + i < n; ++i)
            x[(size_t) (hits[h] + i)] += amp[h] * std::exp (-i / 1800.0f) * rnd();
    const auto path = (std::filesystem::temp_directory_path() / "choppage-snap.wav").string();
    const float* ch[] = { x.data() };
    AudioFile::writeWav24 (path, ch, 1, n, sr);
    Rig r;
    r.s.loadFile (0, path);
    r.s.waitUntilIdle();
    int good = 0;
    std::string misses;
    for (int h = 0; h < 8; ++h)
        for (int off : { -500, -150, 150, 500 })   // rough touches up to 11 ms either side
        {
            const int got = r.s.snapToHit (hits[h] + off, 1200);
            if (std::abs (got - hits[h]) <= 48) ++good; else misses += " " + std::to_string (got - hits[h]);
        }
    check (good >= 30, "rough touches land on the hit, loud or quiet (" + std::to_string (good) + " of 32" + misses + ")");
    check (r.s.snapToHit (16000, 1200) == 16000, "nothing near: the touch stays where it was");
}

static void rexTests()
{
    std::cout << "\n== REX loops ==" << std::endl;
    const auto data = std::filesystem::path (__FILE__).parent_path() / "data";
    for (auto [name, bpm] : { std::pair<const char*, double> { "120Stereo.rx2", 120.0 }, { "120RexTest.rex", 79.763 } })
    {
        const auto path = (data / name).string();
        AudioFile::Status st;
        auto d = AudioFile::load (path, st);
        const std::string n = name;
        check (d != nullptr && st == AudioFile::Status::ok, n + " decodes");
        if (d == nullptr) continue;
        float peak = 0; for (float v : d->ch[0]) peak = std::max (peak, std::abs (v));
        check (peak > 0.05f && peak <= 1.5f, n + ": audio in it (peak " + std::to_string (peak) + ")");
        check (d->fileMarkers.size() >= 2 && d->fileMarkers.front() == 0, n + ": ReCycle's slice points (" + std::to_string (d->fileMarkers.size()) + ")");
        check (std::abs (d->fileBpm - bpm) < 1.0 && d->fileBars == 1, n + ": its tempo, " + std::to_string (d->fileBars) + " bar(s) (" + std::to_string (d->fileBpm) + ")");
        check (AudioFile::isAudioFile (path) && AudioFile::readInfo (path).ok, n + ": listed by the browser");
        Rig r;
        r.s.loadFile (1, path);
        r.s.waitUntilIdle();
        const auto& sl = r.s.slot (1);
        check (sl.status == Session::Status::ready && sl.content && sl.content->numSlices() == std::min ((int) d->fileMarkers.size(), kMaxSlices)
               && std::abs (sl.content->bpm - bpm) < 1.0, n + ": loads with ReCycle's slices and tempo");
    }
}

// 0.17: UNDO / REDO for everything
static void undoTests()
{
    std::cout << "\n== undo / redo ==" << std::endl;
    Rig r;
    std::vector<float> params = r.plain;
    auto settle = [&] { for (int k = 0; k < 3; ++k) { r.s.undoCheckpoint (params); std::this_thread::sleep_for (std::chrono::milliseconds (300)); } r.s.undoCheckpoint (params); };
    settle();
    check (! r.s.canUndo(), "nothing to undo at the start");
    const auto before = r.s.slot (0).current;
    r.s.rollPattern (Style::jungle, 1.0f, true);
    settle();
    check (r.s.canUndo() && ! (r.s.slot (0).current == before), "a pattern roll is a step");
    params[P_FILTER] = 0.3f;
    settle();
    r.s.setStepCondition (2, false, cond::abBase);
    settle();
    auto p1 = r.s.undo();
    check (r.s.slot (0).current.noteCond[2] == 0 && p1.size() == params.size() && std::abs (p1[P_FILTER] - 0.3f) < 1e-6f, "undo: the condition goes, the filter stays");
    params = p1;
    auto p2 = r.s.undo();
    check (! p2.empty() && std::abs (p2[P_FILTER] - 1.0f) < 1e-6f, "undo: the filter knob goes back");
    params = p2;
    auto p3 = r.s.undo();
    check (r.s.slot (0).current == before, "undo: the pattern roll goes back");
    params = p3;
    settle();
    check (r.s.canRedo(), "after undo, redo is there (and a settled state doesn't clear it)");
    auto p4 = r.s.redo(); params = p4;
    check (! (r.s.slot (0).current == before), "redo: the roll comes back");
    // a load is a step too: undo puts the old break back (no reload)
    settle();
    const auto oldContent = r.s.slot (1).content;
    r.s.loadDemo (1); r.s.waitUntilIdle(); r.s.poll();
    r.s.setBadge (0, Badge::snare);
    settle();
    r.s.undo();
    check (r.s.slot (1).content == oldContent || r.s.slot (0).content->badges[0] != Badge::snare, "undo after a load / a badge change puts it back");
    // UNDO straight after an edit (before it settled) takes that edit back
    settle();
    const auto beforeQuick = r.s.slot (r.s.selectedSlot()).current;
    r.s.setStepCondition (5, true, cond::fill);
    r.s.undo (params);
    check (r.s.slot (r.s.selectedSlot()).current == beforeQuick, "undo straight after an edit takes it back");
    r.s.redo (params);
    check (r.s.slot (r.s.selectedSlot()).current.fxCond[5] == cond::fill, "and redo brings it back");
}

// 0.17: trig conditions (NOTE PROB / FX PROB), ROLL MODE, HALF-TIME MODE
static void conditionTests()
{
    std::cout << "\n== trig conditions, roll and half-time modes ==" << std::endl;
    check (cond::kCount == 65 && cond::name (0) == "ALWAYS" && cond::name (cond::fill) == "FILL" && cond::name (cond::percentBase + 10) == "50%"
             && cond::name (cond::abBase) == "1:2" && cond::name (cond::abBase + 1) == "2:2" && cond::name (cond::kCount - 1) == "8:8",
           "the conditions: ALWAYS, FILL ... NOT 1ST, 1% .. 99%, 1:2 .. 8:8 (65)");
    auto bars = [] (int condition, bool fill, int barsToRun) {
        Rig r;
        r.set (P_FILL, fill ? 1.0f : 0.0f);
        const int slot = r.s.selectedSlot();
        r.s.setPatternLength (1);
        for (int k = 0; k < 16; ++k) r.s.setStepCondition (k, false, condition);
        const double bar = 4.0 * 60.0 / r.host.bpm;
        auto a = r.run (bar * barsToRun);
        std::vector<float> out;
        for (int b = 0; b < barsToRun; ++b) out.push_back (rms (a, (int) ((b + 0.05) * bar * r.sr), (int) (0.9 * bar * r.sr)));
        (void) slot;
        return out;
    };
    auto always = bars (cond::always, false, 2);
    auto ab = bars (cond::abBase, false, 4);            // 1:2: the 1st and 3rd time round
    check (always[0] > 0.02f && always[1] > 0.02f, "ALWAYS plays every bar");
    check (ab[0] > 0.02f && ab[1] < 0.002f && ab[2] > 0.02f && ab[3] < 0.002f,
           "1:2 plays the 1st and 3rd time round only (" + fmt (ab[0], 3) + " " + fmt (ab[1], 4) + " " + fmt (ab[2], 3) + " " + fmt (ab[3], 4) + ")");
    auto first = bars (cond::first, false, 2), notFirst = bars (cond::notFirst, false, 2);
    check (first[0] > 0.02f && first[1] < 0.002f && notFirst[0] < 0.002f && notFirst[1] > 0.02f, "1ST / NOT 1ST");
    auto fillOff = bars (cond::fill, false, 1), fillOn = bars (cond::fill, true, 1), notFill = bars (cond::notFill, false, 1);
    check (fillOff[0] < 0.002f && fillOn[0] > 0.02f && notFill[0] > 0.02f, "FILL plays only with FILL on, NOT FILL without it");
    auto half = bars (cond::percentBase + 10, false, 1), always1 = bars (cond::always, false, 1), one = bars (cond::percentBase, false, 1);
    check (half[0] > 0.002f && half[0] < always1[0] * 0.95f && one[0] < half[0],
           "50% plays some hits (rms " + fmt (half[0], 3) + " of " + fmt (always1[0], 3) + "), 1% hardly any (" + fmt (one[0], 4) + ")");
    {
        // the FX row's own condition: a STUTTER on every step, FX PROB FILL (FILL off): as if there were no FX
        Rig a, b, c;
        for (Rig* r : { &a, &b }) { r->s.setPatternLength (1); for (int k = 0; k < 16; ++k) r->s.setFxStep (k, FxType::stutter, 32); }
        for (int k = 0; k < 16; ++k) b.s.setStepCondition (k, true, cond::fill);
        c.s.setPatternLength (1);
        auto wa = a.run (1.0), wb = b.run (1.0), wc = c.run (1.0);
        check (difference (wa, wc) > 0.01f && difference (wb, wc) < 0.001f, "FX PROB: FILL (off) keeps a step's FX out");
    }
    {
        // patterns keep their conditions through a save
        Rig r;
        r.s.setStepCondition (3, false, cond::abBase + 4);
        r.s.setStepCondition (5, true, cond::percentBase + 3);
        const auto st = r.s.saveState();
        Rig q;
        q.s.loadState (st);
        q.s.waitUntilIdle(); q.s.poll();
        const auto& p = q.s.slot (q.s.selectedSlot()).current;
        check (p.noteCond[3] == cond::abBase + 4 && p.fxCond[5] == cond::percentBase + 3 && p.noteCond[4] == 0, "... the conditions come back");
    }
    // ROLL MODE: each one sounds different; HALF-TIME MODE: each one sounds different
    {
        std::vector<Audio> rolls;
        for (int m = 0; m < 6; ++m)
        {
            Rig r;
            r.set (P_ROLL_MODE, (float) m);
            rolls.push_back (r.run (1.5, { MidiMsg::noteOn (kFirstFxNote + PerfFx::roll, 110, (int) (0.4 * r.sr)), MidiMsg::noteOff (kFirstFxNote + PerfFx::roll, (int) (1.4 * r.sr)) }));
        }
        bool allDiffer = true;
        for (int m = 1; m < 6; ++m) allDiffer = allDiffer && difference (rolls[0], rolls[(size_t) m]) > 0.002f && finite (rolls[(size_t) m]);
        const int at = (int) (0.45 * 44100), late = (int) (1.25 * 44100), w = (int) (0.1 * 44100);
        check (allDiffer, "ROLL MODE: PITCH UP, VEL UP, VEL UP +PITCH, VEL DOWN, VEL DOWN -PITCH, PITCH DOWN all differ");
        check (rms (rolls[1], at, w) < rms (rolls[1], late, w) && rms (rolls[3], at, w) > rms (rolls[3], late, w),
               "VEL UP gets louder, VEL DOWN quieter (" + fmt (rms (rolls[1], at, w), 3) + " -> " + fmt (rms (rolls[1], late, w), 3) + ", "
               + fmt (rms (rolls[3], at, w), 3) + " -> " + fmt (rms (rolls[3], late, w), 3) + ")");
    }
    {
        std::vector<Audio> halves;
        for (int m = 0; m < 5; ++m)
        {
            Rig r;
            r.set (P_HALF_MODE, (float) m);
            halves.push_back (r.run (2.0, { MidiMsg::noteOn (kFirstFxNote + PerfFx::halfTime, 110, (int) (0.3 * r.sr)), MidiMsg::noteOff (kFirstFxNote + PerfFx::halfTime, (int) (1.9 * r.sr)) }));
        }
        bool allDiffer = true;
        for (int m = 1; m < 5; ++m) allDiffer = allDiffer && difference (halves[0], halves[(size_t) m]) > 0.002f && finite (halves[(size_t) m]);
        check (allDiffer, "HALF-TIME MODE: 1/2, 3/4, 2/3, 1/4, 1/2 TAPE all differ");
    }
}

int main()
{
    wavVariantTests();
    analysisTests();
    generatorTests();
    playbackTests();
    fxTests();
    conditionTests();
    undoTests();
    editTests();
    fileTests();
    paramTests();
    rexTests();
    quantiseTests();
    snapTests();
    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
