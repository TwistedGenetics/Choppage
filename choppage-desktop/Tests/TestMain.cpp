/*
    ChoppageTests — the desktop wrapper (the engine itself is tested in tests/test_core.cpp):
    JUCE parameters from the core table, MIDI through processBlock with a fake host,
    JUCE decoding, state through the host's XML, and the editor rendered to PNGs.

      ChoppageTests                         run the checks
      ChoppageTests --snapshot out.png [tab 0-4] [width] [lcd 0-3]
*/
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "choppage/DemoBreak.h"
#include <iostream>

using namespace bc;

static int failures = 0, checks = 0;
static void check (bool ok, const juce::String& what)
{
    ++checks; if (! ok) ++failures;
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << std::endl;
}
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

struct FakeHost final : juce::AudioPlayHead
{
    bool playing = true; double bpm = 170.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setIsPlaying (playing); p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Rig
{
    ChoppageProcessor p;
    FakeHost host;
    Rig() { p.setPlayHead (&host); p.prepareToPlay (44100.0, 512); pump (30); }
    void setParam (const char* id, float plain) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); }
    float run (double seconds, juce::MidiBuffer first = {})
    {
        const int total = (int) (seconds * 44100.0);
        double sum = 0; int n = 0;
        for (int pos = 0; pos < total; pos += 512)
        {
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m = pos == 0 ? first : juce::MidiBuffer();
            p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { sum += (double) b.getSample (0, i) * b.getSample (0, i); ++n; }
            if (host.playing) host.ppq += 512 * host.bpm / 60.0 / 44100.0;
        }
        return (float) std::sqrt (sum / std::max (1, n));
    }
};

static void tests()
{
    std::cout << "\n== Desktop wrapper ==" << std::endl;
    {
        Rig r;
        check (r.p.getParameters().size() == kNumCoreParams, "one JUCE parameter per core parameter (" + juce::String (r.p.getParameters().size()) + ")");
        auto* sr = r.p.apvts.getParameter ("srate");
        check (sr->getCurrentValueAsText() == "44.1 kHz" && std::abs (sr->convertFrom0to1 (0.5f) - 15000.0f) < 50.0f,
               "core value text and skew reach the host (" + sr->getCurrentValueAsText() + ")");
        check (r.run (1.0) > 0.01f, "host playing: the demo break plays");
        r.host.playing = false;
        r.run (0.3);
        check (r.run (0.3) < 0.001f, "host stopped: silent");
        juce::MidiBuffer c1; c1.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        check (r.run (0.5, c1) > 0.01f, "C1 through processBlock latches the break");
        r.setParam ("volume", -60.0f);
        r.run (0.2);
        check (r.run (0.3) < 0.0001f, "Volume at -INF silences it");
    }
    {
        // JUCE decodes the file (here a WAV it writes itself, 24-bit)
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("choppage_juce_136.wav");
        f.deleteFile();
        auto b = DemoBreak::make (44100.0, 136.0);
        {
            juce::AudioBuffer<float> a (2, b->numSamples);
            for (int c = 0; c < 2; ++c) a.copyFrom (c, 0, b->ch[c].data(), b->numSamples);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (24));
            w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples());
        }
        Rig r;
        r.p.loadFile (2, f);
        r.p.waitUntilIdle();
        const auto& s = r.p.getSlot (2);
        check (s.status == ChoppageProcessor::Status::ready && s.content && std::abs (s.content->bpm - 136.0) < 0.5, "JUCE decoder feeds the core (136 BPM)");

        r.p.selectSlot (2);
        r.p.rollPattern();
        r.setParam ("patAmount", 0.8f);
        juce::MemoryBlock mb;
        r.p.getStateInformation (mb);
        Rig q;
        q.p.setStateInformation (mb.getData(), (int) mb.getSize());
        q.p.waitUntilIdle();
        check (q.p.getSlot (2).status == ChoppageProcessor::Status::ready && q.p.getSlot (2).current == s.current
               && q.p.getSelectedSlot() == 2, "state through the host: slot 3, its pattern and the selection come back");
        check (std::abs (q.p.apvts.getParameter ("patAmount")->getValue() - r.p.apvts.getParameter ("patAmount")->getValue()) < 1e-5f,
               "parameters come back");
        auto dragged = r.p.bounceLoop();
        check (dragged.existsAsFile() && dragged.getSize() > 100000, "DRAG bounce writes a WAV (" + juce::String (dragged.getSize() / 1024) + " KB)");
        dragged.deleteFile();
        f.deleteFile();
    }
}

static int snapshot (const juce::String& path, int tab, int width, int lcd)
{
    Rig r;
    if (auto* prm = r.p.apvts.getParameter ("lcdColour")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) lcd));
    r.p.loadDemo (1);
    r.run (0.7);
    r.p.selectSlice (4, false);
    r.p.rollFx();
    if (tab == 4)
    {
        // a few breaks for the DISK page
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("choppage-snap-breaks");
        dir.deleteRecursively(); dir.createDirectory();
        for (auto [name, bpm] : { std::pair<const char*, double> { "amen_brother_170", 170.0 }, { "think_break_136", 136.0 }, { "apache_112", 112.0 },
                                  { "hot_pants_100", 100.0 }, { "funky_drummer_98", 98.0 }, { "soul_pride_174", 174.0 } })
        {
            auto b = DemoBreak::make (44100.0, bpm);
            const float* ch[2] = { b->ch[0].data(), b->ch[1].data() };
            AudioFile::writeWav24 (dir.getChildFile (juce::String (name) + ".wav").getFullPathName().toStdString(), ch, 2, b->numSamples, 44100.0);
        }
        r.p.scanBrowser (dir);
        r.p.waitUntilIdle();
    }
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
                         argc >= 5 ? juce::String (argv[4]).getIntValue() : ChoppageEditor::designW,
                         argc >= 6 ? juce::String (argv[5]).getIntValue() : 0);
    tests();
    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
