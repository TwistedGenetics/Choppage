/*
    ChoppageTests — headless checks, no DAW needed.

      * analysis: tempo from length / name, the demo break's badges
      * playback: one break at a time, BAR SYNC on the host grid, LATCH / HOLD,
        host stopped, MIDI trigger mode, slice notes
      * generators: AMOUNT 0 = original, locks survive, FX lane, history
      * FX pads + character change the sound and never produce NaNs
      * markers, badges, state save / restore

    Also renders the editor to PNGs:  ChoppageTests --snapshot out.png [tab 0-3]
*/
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/Analysis.h"
#include "Engine/Generator.h"
#include "Engine/DemoBreak.h"
#include <iostream>

using namespace bc;

static int failures = 0, checks = 0;
static void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) ++failures;
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << std::endl;
}
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// ---- A fake host ---------------------------------------------------------------------------
struct FakeHost final : juce::AudioPlayHead
{
    bool playing = true;
    double bpm = 170.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Rig
{
    ChoppageProcessor p;
    FakeHost host;
    double sr = 44100.0;
    int block = 512;

    Rig() { p.setPlayHead (&host); p.prepareToPlay (sr, block); pump (30); }

    void setParam (const char* id, float plain)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    }

    /** Renders `seconds`; `events` are (sample offset from now, message). Returns the audio. */
    juce::AudioBuffer<float> run (double seconds, std::vector<std::pair<int, juce::MidiMessage>> events = {})
    {
        const int total = (int) (seconds * sr);
        juce::AudioBuffer<float> all (2, total);
        all.clear();
        juce::AudioBuffer<float> buf (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            buf.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            for (auto& e : events)
                if (e.first >= pos && e.first < pos + n) midi.addEvent (e.second, e.first - pos);
            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, buf, ch, 0, n);
            if (host.playing) host.ppq += n * host.bpm / 60.0 / sr;
        }
        return all;
    }
};

static float rms (const juce::AudioBuffer<float>& b, int from = 0, int len = -1)
{
    if (len < 0) len = b.getNumSamples() - from;
    if (len <= 0) return 0.0f;
    return b.getRMSLevel (0, from, len);
}

static bool finite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)) || std::abs (b.getSample (ch, i)) > 8.0f) return false;
    return true;
}

static float difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    double d = 0.0;
    for (int i = 0; i < n; ++i) d += std::abs (a.getSample (0, i) - b.getSample (0, i));
    return (float) (d / juce::jmax (1, n));
}

// ======================================================================================
static void analysisTests()
{
    std::cout << "\n== Analysis ==" << std::endl;
    auto t = Analysis::estimateTempo (60.0 / 170.0 * 8.0, "amen");
    check (std::abs (t.bpm - 170.0) < 0.5 && t.bars == 2, "2 bars at 170 BPM from its length (got " + juce::String (t.bpm, 1) + " / " + juce::String (t.bars) + ")");
    auto h = Analysis::estimateTempo (60.0 / 136.0 * 4.0, "think_break_136");
    check (std::abs (h.bpm - 136.0) < 0.5 && h.bars == 1, "BPM hint in the file name (think_break_136 -> 136, 1 bar)");
    auto h2 = Analysis::estimateTempo (60.0 / 85.0 * 8.0, "funky 170bpm");
    check (std::abs (h2.bpm - 170.0) < 0.5, "name says 170: 85-BPM-length loop read as 4 bars at 170");

    auto buf = DemoBreak::make (44100.0, 170.0);
    SlotContent c;
    c.buffer = buf;
    c.bars = 2; c.bpm = 170.0;
    c.slices = Analysis::gridSlices (buf->getNumSamples(), 2, 16);
    c.badges = Analysis::classify (*buf, c.slices);
    const char* lay = DemoBreak::layout();
    int right = 0, hits = 0;
    juce::String got, want;
    for (int k = 0; k < 32; ++k)
    {
        if (lay[k] == '.') continue;
        ++hits;
        const char g = badgeLetter (c.badges[(size_t) k])[0];
        got += g; want += lay[k];
        if (g == lay[k] || (lay[k] == 'O' && g == 'H')) ++right;
    }
    check (right >= hits * 8 / 10, "classifier badges the demo break (" + juce::String (right) + "/" + juce::String (hits) + ": " + got + " vs " + want + ")");

    auto orig = Analysis::originalPattern (c);
    check (orig.numSteps == 32 && orig.slice[0] == 0 && orig.slice[31] == 31, "original pattern = slices in order on their 16ths");
}

static void generatorTests()
{
    std::cout << "\n== Generators ==" << std::endl;
    auto buf = DemoBreak::make();
    SlotContent::Ptr c = new SlotContent();
    c->buffer = buf; c->bars = 2; c->bpm = 170.0;
    c->slices = Analysis::gridSlices (buf->getNumSamples(), 2, 16);
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
    check (a.slice[0] >= 0, "the downbeat is never empty");

    in.current.slice[5] = 3; in.current.locked[5] = true;
    auto l = Generator::pattern (in);
    check (l.slice[5] == 3, "a locked step survives the dice");

    int kicksOnKickRole = 0;
    for (int st : { 0, 16 }) if (a.slice[(size_t) st] >= 0 && c->badges[(size_t) a.slice[(size_t) st]] == Badge::kick) ++kicksOnKickRole;
    check (kicksOnKickRole == 2, "JUNGLE puts kicks on the ones");

    auto fx = Generator::fx (in.original, 1.0f, 7);
    int events = 0; for (int i = 0; i < fx.numSteps; ++i) events += fx.fx[(size_t) i].type != FxType::none;
    check (events > 2 && fx.fx[0].type == FxType::none, "FX dice scatters events and leaves the one clean (" + juce::String (events) + ")");
    auto none = Generator::fx (in.original, 0.0f, 7);
    int e0 = 0; for (int i = 0; i < none.numSteps; ++i) e0 += none.fx[(size_t) i].type != FxType::none;
    check (e0 == 0, "FX AMOUNT 0 clears the FX lane");

    const int s = 4;   // first snare
    const int next = Generator::nextWithSameBadge (*c, s);
    check (next != s && c->badges[(size_t) next] == c->badges[(size_t) s], "right-click swap finds another slice with the same badge");
}

static void playbackTests()
{
    std::cout << "\n== Playback ==" << std::endl;
    {
        Rig r;
        check (r.p.getSlot (0).status == ChoppageProcessor::Status::ready, "demo break ready in slot 1 on first open");
        auto a = r.run (1.0);
        check (rms (a) > 0.01f, "host playing: the break plays (rms " + juce::String (rms (a), 3) + ")");
        check (finite (a), "output finite and in range");
        check (r.p.getPlayingSlot() == 0 && r.p.getCurrentStep() >= 0, "slot 1 playing, step " + juce::String (r.p.getCurrentStep()));

        // BAR SYNC: at ppq 4 (bar 2) the step is 16
        r.host.ppq = 4.0;
        r.run (0.01);
        check (r.p.getCurrentStep() == 16, "BAR SYNC: bar 2 starts on step 17 (got " + juce::String (r.p.getCurrentStep() + 1) + ")");

        // A break note on an empty slot changes nothing audible; on a loaded one it switches
        r.p.loadDemo (1);
        r.run (0.2, { { 0, juce::MidiMessage::noteOn (1, kFirstBreakNote + 1, (juce::uint8) 100) } });
        check (r.p.getPlayingSlot() == 1, "C#1 switches to break 2 (mono: one break at a time)");
        r.host.playing = false;
        auto stopped = r.run (0.5);
        check (rms (stopped, (int) (0.2 * r.sr)) < 0.001f, "host stop: silence");
    }
    {
        Rig r;
        r.host.playing = false;
        auto quiet = r.run (0.3);
        check (rms (quiet) < 0.0001f, "host stopped, nothing latched: silent");
        auto on = r.run (0.6, { { 0, juce::MidiMessage::noteOn (1, kFirstBreakNote, (juce::uint8) 100) },
                                { 1000, juce::MidiMessage::noteOff (1, kFirstBreakNote) } });
        check (rms (on) > 0.01f, "LATCH: a tap on C1 runs the break on the internal clock");
        r.run (0.1, { { 0, juce::MidiMessage::noteOn (1, kFirstBreakNote, (juce::uint8) 100) } });
        auto off = r.run (0.4);
        check (rms (off, (int) (0.1 * r.sr)) < 0.001f, "LATCH: the same key again stops it");

        r.setParam (params::gateMode, 1.0f);   // HOLD
        auto held = r.run (0.5, { { 0, juce::MidiMessage::noteOn (1, kFirstBreakNote, (juce::uint8) 100) },
                                  { (int) (0.25 * r.sr), juce::MidiMessage::noteOff (1, kFirstBreakNote) } });
        check (rms (held, 0, (int) (0.2 * r.sr)) > 0.01f && rms (held, (int) (0.33 * r.sr)) < 0.001f,
               "HOLD: plays while the key is down, stops on release");
    }
    {
        Rig r;
        r.host.playing = false;
        r.setParam (params::trigger, 1.0f);   // MIDI
        auto a = r.run (0.4, { { 100, juce::MidiMessage::noteOn (1, kFirstSliceNote, (juce::uint8) 100) } });
        check (rms (a, 0, 90) < 0.0001f && rms (a, 100) > 0.005f, "MIDI mode: E2 plays slice 1, on the sample it arrives");
    }
    {
        Rig r;
        auto plain = r.run (2.0);
        Rig s;
        s.setParam (params::swing, 0.7f);
        auto swung = s.run (2.0);
        check (difference (plain, swung) > 0.002f, "SWING moves the off-beat 16ths");
    }
    {
        Rig r;
        r.setParam (params::fitMode, 1.0f);   // KEEP
        r.host.bpm = 120.0;
        auto keep = r.run (1.0);
        Rig q;
        q.host.bpm = 120.0;
        auto rep = q.run (1.0);
        check (difference (keep, rep) > 0.002f && finite (keep), "FIT: REPITCH and KEEP sound different at 120 BPM");
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
        const int note = kFirstFxNote + pad;
        auto wet = r.run (1.5, { { (int) (0.4 * r.sr), juce::MidiMessage::noteOn (1, note, (juce::uint8) 110) },
                                 { (int) (1.2 * r.sr), juce::MidiMessage::noteOff (1, note) } });
        const float d = difference (dry, wet);
        check (d > 0.001f && finite (wet), juce::String ("pad ") + params::fxPadName (pad) + " changes the sound (" + juce::String (d, 4) + "), no NaNs");
    }
    {
        Rig r;
        r.p.setPadHeld (0, true);
        r.run (0.05);
        check (r.p.isPadLit (0), "a UI pad lights while held");
        r.p.setPadHeld (0, false);
        r.run (0.05);
        check (! r.p.isPadLit (0), "and goes out after release");
    }
    {
        Rig clean;
        clean.setParam (params::character, 0.0f);
        auto a = clean.run (1.0);
        Rig s950;
        auto b = s950.run (1.0);
        check (difference (a, b) > 0.001f, "S950 character differs from CLEAN");
        Rig low;
        low.setParam (params::sampleRate, 4000.0f);
        auto c = low.run (1.0);
        check (difference (b, c) > 0.001f && finite (c), "SAMPLE RATE fader at 4 kHz crunches it");
    }
    {
        Rig r;
        r.setParam (params::dist, 0.8f); r.setParam (params::crush, 0.7f); r.setParam (params::reverb, 0.6f);
        r.setParam (params::echoMix, 0.5f); r.setParam (params::filter, 0.3f);
        auto a = r.run (2.0);
        check (finite (a) && rms (a) > 0.005f, "all post FX at once: stable");
        r.setParam (params::filterType, 1.0f); r.setParam (params::filter, 0.9f);
        check (finite (r.run (0.5)), "HP filter: stable");
    }
    {
        // FX lane events play without a pad
        Rig r;
        auto& s = r.p.getSlot (0);
        juce::ignoreUnused (s);
        for (int st = 1; st < 32; st += 2) r.p.setFxStep (st, FxType::stutter, 32);
        auto a = r.run (1.5);
        check (difference (dry, a) > 0.001f && finite (a), "FX lane events change the sound");
    }
}

static void editTests()
{
    std::cout << "\n== Editing, history, state ==" << std::endl;
    Rig r;
    const auto& s = r.p.getSlot (0);
    const int slices = s.content->numSlices();
    r.p.rollPattern();
    r.p.rollPattern();
    const auto after2 = s.current;
    r.p.historyBack();
    check (! (s.current == after2), "history ◀ goes back a roll");
    r.p.historyForward();
    check (s.current == after2, "history ▶ comes forward again");
    r.p.setShowOriginal (true);
    check (s.program.pattern == s.original, "ORIGINAL plays the break as recorded");
    r.p.setShowOriginal (false);

    r.p.selectSlice (4, false);
    r.p.setSliceValue (ChoppageProcessor::SliceField::pitch, 5.0f, false);
    check (s.program.slice[4].pitch == 5.0f && s.program.slice[3].pitch == 0.0f, "SLICE edits only the selected slice");
    r.p.setSliceValue (ChoppageProcessor::SliceField::gate, 0.5f, true);
    check (s.program.all.gate == 0.5f, "ALL edits the whole break");

    const int before = s.content->numSlices();
    const int pos = (s.content->slices.starts[2] + s.content->slices.starts[3]) / 2;
    r.p.addMarker (pos);
    check (s.content->numSlices() == before + 1, "add a marker: one more slice");
    r.p.deleteMarker (3);
    check (s.content->numSlices() == before, "delete it again");
    juce::ignoreUnused (slices);

    r.p.setBadge (6, Badge::open);
    check (s.content->badges[6] == Badge::open, "badge override sticks");
    r.p.toggleMute (7);
    check (s.program.slice[7].mute, "right-click mute");

    r.p.saveMemory (3);
    r.p.clearPattern();
    check (s.current.hits() == 0, "clear pattern");
    r.p.recallMemory (3);
    check (s.current.hits() > 0, "memory 4 recalled");

    // state round trip
    r.p.setFxStep (9, FxType::reverse, 16);
    juce::MemoryBlock mb;
    r.p.getStateInformation (mb);
    Rig q;
    q.p.setStateInformation (mb.getData(), (int) mb.getSize());
    q.p.waitUntilIdle();
    const auto& t = q.p.getSlot (0);
    check (t.status == ChoppageProcessor::Status::ready, "restore: slot 1 ready");
    check (t.current == s.current, "restore: pattern + FX lane come back");
    check (t.program.slice[7].mute && t.program.all.gate == 0.5f && t.content->badges[6] == Badge::open,
           "restore: mutes, ALL settings and badge overrides come back");
    check (t.memories[3].has_value(), "restore: pattern memories come back");
    check (finite (q.run (0.5)), "restored instance plays");

    // a WAV file round trip through the loader
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("choppage_test_break_136.wav");
    {
        auto buf = DemoBreak::make (44100.0, 136.0);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100.0, 2, 16, {}, 0));
        w->writeFromAudioSampleBuffer (buf->audio, 0, buf->getNumSamples());
    }
    r.p.loadFile (2, f);
    r.p.waitUntilIdle();
    const auto& w = r.p.getSlot (2);
    check (w.status == ChoppageProcessor::Status::ready && w.content != nullptr && std::abs (w.content->bpm - 136.0) < 0.5,
           "WAV loads on slot 3 and reads 136 BPM from its name");
    check (w.content != nullptr && w.content->numSlices() >= 8, "transient slicer finds the hits (" + juce::String (w.content != nullptr ? w.content->numSlices() : 0) + ")");
    r.p.loadFile (3, juce::File ("/no/such/break.wav"));
    r.p.waitUntilIdle();
    check (r.p.getSlot (3).status == ChoppageProcessor::Status::missing, "a missing file says so");
    f.deleteFile();
}

// ======================================================================================
static int snapshot (const juce::String& path, int tab, int width)
{
    Rig r;
    r.p.loadDemo (1);
    r.run (0.7);
    r.p.selectSlice (4, false);
    r.p.rollFx();
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
    if (auto* ce = dynamic_cast<ChoppageEditor*> (ed.get())) ce->showTab (tab);
    ed->setSize (width, width * ChoppageEditor::designH / ChoppageEditor::designW);
    pump (300);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream (img, os);
    std::cout << (ok ? "wrote " : "could not write ") << f.getFullPathName() << std::endl;
    return ok ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2], argc >= 4 ? juce::String (argv[3]).getIntValue() : 0,
                         argc >= 5 ? juce::String (argv[4]).getIntValue() : ChoppageEditor::designW);

    analysisTests();
    generatorTests();
    playbackTests();
    fxTests();
    editTests();
    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
