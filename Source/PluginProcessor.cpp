#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/Analysis.h"
#include "Engine/Generator.h"
#include "Engine/DemoBreak.h"
#include "Engine/SampleLoader.h"
#include "Engine/TransientDetector.h"

using namespace bc;

namespace
{
    SlotContent::Ptr analyse (SampleBuffer::Ptr buf, bool grid, float sens, int gridPerBar, const juce::ValueTree* restore)
    {
        SlotContent::Ptr c = new SlotContent();
        c->buffer = buf;
        c->name = buf->name;
        const auto tempo = Analysis::estimateTempo (buf->getLengthSeconds(), buf->name);
        c->bpm = tempo.bpm;
        c->bars = tempo.bars;

        if (restore != nullptr && restore->hasProperty ("slices"))
        {
            c->bpm  = (double) restore->getProperty ("bpm", c->bpm);
            c->bars = (int) restore->getProperty ("bars", c->bars);
            c->slices = SliceTable::fromString (restore->getProperty ("slices").toString());
            c->slices.normalise (buf->getNumSamples());
        }
        if (c->slices.isEmpty())
            c->slices = grid ? Analysis::gridSlices (buf->getNumSamples(), c->bars, gridPerBar)
                             : tb::TransientDetector::detect (*buf, sens, juce::jmin (kMaxSlices, c->bars * 16));
        if (c->slices.isEmpty()) c->slices = SliceTable::equal (buf->getNumSamples(), 16);

        c->badges = Analysis::classify (*buf, c->slices);
        if (restore != nullptr && restore->hasProperty ("badges"))
        {
            const auto s = restore->getProperty ("badges").toString();
            for (int k = 0; k < juce::jmin (s.length(), kMaxSlices); ++k)
                switch (s[k]) { case 'K': c->badges[(size_t) k] = Badge::kick; break; case 'S': c->badges[(size_t) k] = Badge::snare; break;
                                case 'G': c->badges[(size_t) k] = Badge::ghost; break; case 'H': c->badges[(size_t) k] = Badge::hat; break;
                                case 'O': c->badges[(size_t) k] = Badge::open; break; default: c->badges[(size_t) k] = Badge::other; break; }
        }
        return c;
    }

    juce::String patternToString (const Pattern& p)
    {
        juce::StringArray a, f, l;
        for (int i = 0; i < p.numSteps; ++i)
        {
            a.add (juce::String (p.slice[(size_t) i]));
            f.add (juce::String ((int) p.fx[(size_t) i].type) + ":" + juce::String ((int) p.fx[(size_t) i].rate));
            l.add (p.locked[(size_t) i] ? "1" : "0");
        }
        return juce::String (p.numSteps) + "|" + a.joinIntoString (",") + "|" + f.joinIntoString (",") + "|" + l.joinIntoString (",");
    }

    std::optional<Pattern> patternFromString (const juce::String& s)
    {
        auto parts = juce::StringArray::fromTokens (s, "|", "");
        if (parts.size() < 2) return std::nullopt;
        Pattern p;
        p.numSteps = juce::jlimit (1, kMaxSteps, parts[0].getIntValue());
        auto a = juce::StringArray::fromTokens (parts[1], ",", "");
        for (int i = 0; i < juce::jmin (p.numSteps, a.size()); ++i) p.slice[(size_t) i] = (int16_t) a[i].getIntValue();
        if (parts.size() > 2)
        {
            auto f = juce::StringArray::fromTokens (parts[2], ",", "");
            for (int i = 0; i < juce::jmin (p.numSteps, f.size()); ++i)
            {
                p.fx[(size_t) i].type = (FxType) juce::jlimit (0, kNumFxTypes - 1, f[i].upToFirstOccurrenceOf (":", false, false).getIntValue());
                p.fx[(size_t) i].rate = (uint8_t) juce::jlimit (8, 64, f[i].fromFirstOccurrenceOf (":", false, false).getIntValue());
            }
        }
        if (parts.size() > 3)
        {
            auto l = juce::StringArray::fromTokens (parts[3], ",", "");
            for (int i = 0; i < juce::jmin (p.numSteps, l.size()); ++i) p.locked[(size_t) i] = l[i] == "1";
        }
        return p;
    }

    juce::String sliceParamsToString (const SliceParams& s)
    {
        return juce::String (s.pitch) + "," + juce::String (s.gate) + "," + juce::String (s.stretch) + "," + (s.reverse ? "1" : "0")
             + "," + juce::String (s.stutter) + "," + juce::String (s.trans) + "," + juce::String (s.xfadeMs) + "," + (s.mute ? "1" : "0")
             + "," + juce::String (s.level);
    }

    SliceParams sliceParamsFromString (const juce::String& str)
    {
        SliceParams s;
        auto t = juce::StringArray::fromTokens (str, ",", "");
        if (t.size() >= 9)
        {
            s.pitch = t[0].getFloatValue(); s.gate = t[1].getFloatValue(); s.stretch = t[2].getFloatValue();
            s.reverse = t[3] == "1"; s.stutter = t[4].getIntValue(); s.trans = t[5].getFloatValue();
            s.xfadeMs = t[6].getFloatValue(); s.mute = t[7] == "1"; s.level = t[8].getFloatValue();
        }
        return s;
    }
}

// ======================================================================================
ChoppageProcessor::ChoppageProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "PARAMS", params::createLayout())
{
    formats.registerBasicFormats();
    loadDemo (0);
    startTimerHz (30);
}

ChoppageProcessor::~ChoppageProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 5000);
}

bool ChoppageProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChoppageProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
    for (int i = 0; i < kSlots; ++i)
    {
        engine.publishContent (i, slots[(size_t) i].content);
        publish (i);
    }
}

Engine::Settings ChoppageProcessor::readSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto i = [&] (const char* id) { return (int) std::round (v (id)); };
    Engine::Settings s;
    s.midiTrigger = i (params::trigger) == 1;
    s.breakMode   = i (params::breakMode);
    s.gateHold    = i (params::gateMode) == 1;
    s.velToSlice  = v (params::velSlice) > 0.5f;
    s.speed       = params::speedFactor (i (params::speed));
    s.fit         = v (params::fit) > 0.5f;
    s.keepPitch   = i (params::fitMode) == 1;
    s.tempoSourceMul = i (params::tempoSrc) == 1 ? 0.5 : i (params::tempoSrc) == 2 ? 2.0 : 1.0;
    s.swing       = v (params::swing);
    s.stretchCycleMs = v (params::stretchCycle);

    s.pads.stutterDiv = params::noteRateDivisor (i (params::stutRate));
    s.pads.rollDiv    = params::noteRateDivisor (i (params::rollRate));
    s.pads.tapeMs     = v (params::tapeMs);
    s.pads.akCycleMs  = v (params::akCycle);
    s.pads.akRatio    = params::akRatioValue (i (params::akRatio));
    s.pads.diveRes    = v (params::diveRes);
    s.pads.quantSteps = i (params::padQuant);

    auto& c = s.sound;
    c.character = i (params::character);
    c.faderRate = v (params::sampleRate);
    c.dropRate  = v (params::dropRate);
    c.filter = v (params::filter); c.highPass = i (params::filterType) == 1; c.filterRes = v (params::filterRes);
    c.dist = v (params::dist); c.distTone = v (params::distTone);
    c.crush = v (params::crush);
    c.reverb = v (params::reverb); c.revDecay = v (params::revDecay); c.revDamp = v (params::revDamp);
    c.revWidth = v (params::revWidth); c.revPreMs = v (params::revPre);
    c.echoMix = v (params::echoMix); c.echoFb = v (params::echoFb); c.echoMono = v (params::echoMono) > 0.5f;
    c.volumeDb = v (params::volume);
    return s;
}

void ChoppageProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    Engine::Host host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.hasInfo = pos->getBpm().hasValue();
            host.bpm = pos->getBpm().orFallback (120.0);
            host.playing = pos->getIsPlaying();
            host.ppq = pos->getPpqPosition().orFallback (0.0);
        }

    auto s = readSettings();
    // UI pad buttons can also be automated as host parameters
    for (int p = 0; p < kFxPads; ++p)
        if (apvts.getRawParameterValue (params::padParamId (p))->load() > 0.5f) engine.uiPad[(size_t) p] = true;

    const auto& sel = slots[(size_t) juce::jlimit (0, kSlots - 1, engine.selectedSlot.load())].content;
    const double bpm = host.hasInfo ? host.bpm : (sel != nullptr ? sel->bpm : 120.0);
    s.sound.echoSamples = params::echoBeats ((int) apvts.getRawParameterValue (params::echoTime)->load())
                          * currentSampleRate * 60.0 / juce::jmax (20.0, bpm);

    engine.process (buffer, midi, host, s);
    hostBpm = host.hasInfo ? host.bpm : 0.0;
    hostPlaying = host.playing;
    midi.clear();
}

// ======================================================================================
//  Slots
// ======================================================================================
juce::String ChoppageProcessor::statusText (Status s)
{
    switch (s)
    {
        case Status::empty:      return "EMPTY";
        case Status::loading:    return "LOADING";
        case Status::ready:      return "READY";
        case Status::missing:    return "FILE MISSING";
        case Status::unreadable: return "CAN'T READ FILE";
    }
    return {};
}

void ChoppageProcessor::selectSlot (int i)
{
    engine.selectedSlot = juce::jlimit (0, kSlots - 1, i);
    selectedSlice = 0;
    ++uiVersion;
}

void ChoppageProcessor::selectSlice (int k, bool audition)
{
    const auto& s = slots[(size_t) getSelectedSlot()];
    if (s.content == nullptr) return;
    selectedSlice = juce::jlimit (0, s.content->numSlices() - 1, k);
    if (audition) engine.previewSlice = selectedSlice;
    ++uiVersion;
}

void ChoppageProcessor::loadDemo (int slot)
{
    auto buf = DemoBreak::make (44100.0, 170.0);
    installContent (slot, analyse (buf, false, 0.62f, 16, nullptr), {}, true, nullptr);
}

void ChoppageProcessor::loadFile (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= kSlots) return;
    auto& s = slots[(size_t) slot];
    s.status = Status::loading;
    s.file = file;
    ++s.version; ++uiVersion;
    ++loading;
    const bool grid = s.gridSlicing; const float sens = s.sensitivity; const int gpb = s.gridPerBar;
    juce::WeakReference<ChoppageProcessor> weak (this);
    pool.addJob ([this, weak, slot, file, grid, sens, gpb]
    {
        auto r = tb::SampleLoader::loadFile (file, formats);
        SlotContent::Ptr c;
        if (r.status == tb::SampleLoader::Status::ok) c = analyse (r.buffer, grid, sens, gpb, nullptr);
        const bool missing = r.status == tb::SampleLoader::Status::missing;
        juce::MessageManager::callAsync ([weak, slot, file, c, missing]
        {
            if (auto* p = weak.get())
            {
                --p->loading;
                if (c != nullptr) p->installContent (slot, c, file, false, nullptr);
                else
                {
                    p->slots[(size_t) slot].status = missing ? Status::missing : Status::unreadable;
                    p->changed (slot);
                }
            }
        });
    });
}

void ChoppageProcessor::clearSlot (int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    slots[(size_t) slot] = Slot();
    engine.publishContent (slot, nullptr);
    publish (slot);
}

void ChoppageProcessor::reslice (int slot, bool grid, float sens, int gridPerBar)
{
    auto& s = slots[(size_t) slot];
    s.gridSlicing = grid; s.sensitivity = sens; s.gridPerBar = gridPerBar;
    if (s.content == nullptr || s.content->buffer == nullptr) { changed (slot); return; }
    auto c = analyse (s.content->buffer, grid, sens, gridPerBar, nullptr);
    c->bpm = s.content->bpm; c->bars = s.content->bars;
    installContent (slot, c, s.file, s.demo, nullptr);
}

void ChoppageProcessor::installContent (int slot, SlotContent::Ptr c, const juce::File& file, bool demo,
                                        const juce::ValueTree* restore)
{
    auto& s = slots[(size_t) slot];
    s.content = c;
    keepAlive.push_back (c);
    s.file = file;
    s.demo = demo;
    s.status = Status::ready;
    s.original = Analysis::originalPattern (*c);
    s.current = s.original;
    s.program = {};
    s.history.clear();
    s.memories = {};
    s.showOriginal = false;

    if (restore != nullptr)
    {
        if (auto p = patternFromString (restore->getProperty ("pattern").toString())) s.current = *p;
        s.showOriginal = (bool) restore->getProperty ("showOriginal", false);
        auto ps = juce::StringArray::fromTokens (restore->getProperty ("sliceParams").toString(), ";", "");
        for (int k = 0; k < juce::jmin (ps.size(), kMaxSlices); ++k) s.program.slice[(size_t) k] = sliceParamsFromString (ps[k]);
        const auto all = sliceParamsFromString (restore->getProperty ("all").toString());
        s.program.all = { all.pitch, all.gate, all.stretch, all.reverse, all.stutter, all.trans, all.xfadeMs };
        for (auto m : *restore)
            if (m.hasType ("MEMORY"))
            {
                const int idx = (int) m.getProperty ("index", -1);
                if (idx >= 0 && idx < kMemories)
                    if (auto p = patternFromString (m.getProperty ("pattern").toString())) s.memories[(size_t) idx] = *p;
            }
    }
    pushHistory (s, s.current);
    if (slot == getSelectedSlot()) selectedSlice = 0;
    engine.publishContent (slot, c);
    publish (slot);
}

void ChoppageProcessor::publish (int slot)
{
    auto& s = slots[(size_t) slot];
    s.program.pattern = s.showOriginal ? s.original : s.current;
    engine.publishProgram (slot, s.program);
    changed (slot);
}

void ChoppageProcessor::changed (int slot)
{
    ++slots[(size_t) slot].version;
    ++uiVersion;
}

void ChoppageProcessor::pushHistory (Slot& s, const Pattern& p)
{
    // Drop anything after the current position (like undo), then keep the last 32.
    if (s.historyPos >= 0 && s.historyPos < (int) s.history.size() - 1)
        s.history.resize ((size_t) s.historyPos + 1);
    s.history.push_back (p);
    if (s.history.size() > 32) s.history.erase (s.history.begin());
    s.historyPos = (int) s.history.size() - 1;
}

SlotContent::Ptr ChoppageProcessor::withBadges (const SlotContent& c, const std::array<Badge, kMaxSlices>& b) const
{
    SlotContent::Ptr n = new SlotContent();
    n->buffer = c.buffer; n->slices = c.slices; n->badges = b; n->bpm = c.bpm; n->bars = c.bars; n->name = c.name;
    return n;
}

SlotContent::Ptr ChoppageProcessor::withSlices (const SlotContent& c, const SliceTable& t) const
{
    SlotContent::Ptr n = new SlotContent();
    n->buffer = c.buffer; n->slices = t; n->bpm = c.bpm; n->bars = c.bars; n->name = c.name;
    n->slices.normalise (c.buffer->getNumSamples());
    n->badges = Analysis::classify (*c.buffer, n->slices);
    return n;
}

// ---- SAMPLE tab ------------------------------------------------------------------------
float ChoppageProcessor::getSliceValue (SliceField f, bool all) const
{
    const auto& s = slots[(size_t) getSelectedSlot()];
    if (all)
    {
        const auto& a = s.program.all;
        switch (f)
        {
            case SliceField::pitch: return a.pitch;   case SliceField::gate: return a.gate;
            case SliceField::stretch: return a.stretch; case SliceField::reverse: return a.reverse ? 1.0f : 0.0f;
            case SliceField::stutter: return (float) a.stutter; case SliceField::trans: return a.trans;
            case SliceField::xfade: return a.xfadeMs; case SliceField::level: return 1.0f;
        }
    }
    const auto& p = s.program.slice[(size_t) juce::jlimit (0, kMaxSlices - 1, selectedSlice)];
    switch (f)
    {
        case SliceField::pitch: return p.pitch;   case SliceField::gate: return p.gate;
        case SliceField::stretch: return p.stretch; case SliceField::reverse: return p.reverse ? 1.0f : 0.0f;
        case SliceField::stutter: return (float) p.stutter; case SliceField::trans: return p.trans;
        case SliceField::xfade: return p.xfadeMs; case SliceField::level: return p.level;
    }
    return 0.0f;
}

void ChoppageProcessor::setSliceValue (SliceField f, float v, bool all)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    auto apply = [&] (auto& p)
    {
        switch (f)
        {
            case SliceField::pitch:   p.pitch = juce::jlimit (-24.0f, 24.0f, v); break;
            case SliceField::gate:    p.gate = juce::jlimit (0.05f, 1.0f, v); break;
            case SliceField::stretch: p.stretch = juce::jlimit (0.5f, 4.0f, v); break;
            case SliceField::reverse: p.reverse = v > 0.5f; break;
            case SliceField::stutter: p.stutter = juce::jlimit (0, 8, (int) std::round (v)); break;
            case SliceField::trans:   p.trans = juce::jlimit (0.0f, 1.0f, v); break;
            case SliceField::xfade:   p.xfadeMs = juce::jlimit (0.5f, 20.0f, v); break;
            case SliceField::level:   break;
        }
    };
    if (all) apply (s.program.all);
    else
    {
        auto& p = s.program.slice[(size_t) juce::jlimit (0, kMaxSlices - 1, selectedSlice)];
        apply (p);
        if (f == SliceField::level) p.level = juce::jlimit (0.0f, 2.0f, v);
    }
    publish (slot);
}

void ChoppageProcessor::toggleMute (int k)
{
    const int slot = getSelectedSlot();
    auto& p = slots[(size_t) slot].program.slice[(size_t) juce::jlimit (0, kMaxSlices - 1, k)];
    p.mute = ! p.mute;
    publish (slot);
}

void ChoppageProcessor::setBadge (int k, Badge b)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr) return;
    auto badges = s.content->badges;
    badges[(size_t) juce::jlimit (0, kMaxSlices - 1, k)] = b;
    s.content = withBadges (*s.content, badges);
    keepAlive.push_back (s.content);
    engine.publishContent (slot, s.content);
    publish (slot);
}

namespace
{
    void remapPattern (Pattern& p, int from, int delta)
    {
        for (int i = 0; i < p.numSteps; ++i)
        {
            auto& v = p.slice[(size_t) i];
            if (v < 0) continue;
            if (delta < 0 && v == from) v = -1;
            else if (v >= from) v = (int16_t) juce::jmax (0, v + delta);
        }
    }
}

void ChoppageProcessor::moveMarker (int marker, int position)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr || marker < 0 || marker >= s.content->slices.count) return;
    auto t = s.content->slices;
    t.starts[(size_t) marker] = position;
    s.content = withSlices (*s.content, t);
    keepAlive.push_back (s.content);
    s.original = Analysis::originalPattern (*s.content);
    engine.publishContent (slot, s.content);
    publish (slot);
}

void ChoppageProcessor::addMarker (int position)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr || s.content->slices.count >= kMaxSlices) return;
    auto t = s.content->slices;
    t.starts[(size_t) t.count++] = position;
    auto c = withSlices (*s.content, t);
    const int newIndex = c->slices.sliceAt (position);
    s.content = c;
    keepAlive.push_back (c);
    remapPattern (s.current, newIndex, +1);
    // slice settings shift with their slices
    for (int k = kMaxSlices - 1; k > newIndex; --k) s.program.slice[(size_t) k] = s.program.slice[(size_t) k - 1];
    s.program.slice[(size_t) newIndex] = {};
    s.original = Analysis::originalPattern (*c);
    engine.publishContent (slot, c);
    publish (slot);
}

void ChoppageProcessor::deleteMarker (int marker)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr || marker <= 0 || marker >= s.content->slices.count) return;   // keep the first
    auto t = s.content->slices;
    for (int k = marker; k < t.count - 1; ++k) t.starts[(size_t) k] = t.starts[(size_t) k + 1];
    --t.count;
    s.content = withSlices (*s.content, t);
    keepAlive.push_back (s.content);
    remapPattern (s.current, marker, -1);
    for (int k = marker; k < kMaxSlices - 1; ++k) s.program.slice[(size_t) k] = s.program.slice[(size_t) k + 1];
    s.original = Analysis::originalPattern (*s.content);
    selectedSlice = juce::jmin (selectedSlice, s.content->numSlices() - 1);
    engine.publishContent (slot, s.content);
    publish (slot);
}

// ---- dice / history / SEQ ------------------------------------------------------------------
void ChoppageProcessor::rollPattern()
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr) return;
    Generator::Input in;
    in.content = s.content.get();
    in.original = s.original;
    in.current = s.current;
    in.style = (Style) (int) apvts.getRawParameterValue (params::style)->load();
    in.amount = apvts.getRawParameterValue (params::patAmount)->load();
    in.layers = apvts.getRawParameterValue (params::layers)->load() > 0.5f;
    in.seed = s.seed = juce::Random::getSystemRandom().nextInt64();
    auto p = Generator::pattern (in);
    p.fx = s.current.fx;   // the FX lane is the FX dice's business
    s.current = p;
    s.showOriginal = false;
    pushHistory (s, s.current);
    publish (slot);
}

void ChoppageProcessor::rollFx()
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr) return;
    s.current = Generator::fx (s.current, apvts.getRawParameterValue (params::fxAmount)->load(),
                               juce::Random::getSystemRandom().nextInt64());
    s.showOriginal = false;
    pushHistory (s, s.current);
    publish (slot);
}

void ChoppageProcessor::historyBack()
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.historyPos <= 0) return;
    s.current = s.history[(size_t) --s.historyPos];
    s.showOriginal = false;
    publish (slot);
}

void ChoppageProcessor::historyForward()
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.historyPos >= (int) s.history.size() - 1) return;
    s.current = s.history[(size_t) ++s.historyPos];
    s.showOriginal = false;
    publish (slot);
}

void ChoppageProcessor::setShowOriginal (bool b)
{
    const int slot = getSelectedSlot();
    slots[(size_t) slot].showOriginal = b;
    publish (slot);
}

void ChoppageProcessor::setPatternLength (int bars)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    const int steps = juce::jlimit (16, kMaxSteps, bars * 16);
    for (int i = s.current.numSteps; i < steps; ++i)
    {
        const int src = i % juce::jmax (1, s.current.numSteps);
        s.current.slice[(size_t) i] = s.current.slice[(size_t) src];
        s.current.fx[(size_t) i] = s.current.fx[(size_t) src];
        s.current.locked[(size_t) i] = false;
    }
    s.current.numSteps = steps;
    s.showOriginal = false;
    pushHistory (s, s.current);
    publish (slot);
}

void ChoppageProcessor::toggleStep (int step, Badge lane)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr || step < 0 || step >= s.current.numSteps) return;
    auto& v = s.current.slice[(size_t) step];
    if (v >= 0 && s.content->badges[(size_t) v] == lane) v = -1;
    else
    {
        juce::Random r ((int64_t) step * 7919 + (int64_t) lane);
        const int k = Generator::pickSlice (*s.content, lane, r);
        if (k >= 0) v = (int16_t) k;
    }
    s.showOriginal = false;
    publish (slot);
}

void ChoppageProcessor::swapStep (int step)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (s.content == nullptr || step < 0 || step >= s.current.numSteps) return;
    auto& v = s.current.slice[(size_t) step];
    if (v >= 0) v = (int16_t) Generator::nextWithSameBadge (*s.content, v);
    s.showOriginal = false;
    publish (slot);
}

void ChoppageProcessor::toggleLock (int step)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (step < 0 || step >= s.current.numSteps) return;
    s.current.locked[(size_t) step] = ! s.current.locked[(size_t) step];
    publish (slot);
}

void ChoppageProcessor::setFxStep (int step, FxType t, int rate)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (step < 0 || step >= s.current.numSteps) return;
    s.current.fx[(size_t) step] = { t, (uint8_t) juce::jlimit (8, 64, rate) };
    s.showOriginal = false;
    publish (slot);
}

void ChoppageProcessor::cycleFxRate (int step)
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (step < 0 || step >= s.current.numSteps) return;
    auto& e = s.current.fx[(size_t) step];
    if (e.type == FxType::none) return;
    e.rate = (uint8_t) (e.rate >= 64 ? 8 : e.rate * 2);
    publish (slot);
}

void ChoppageProcessor::saveMemory (int m)
{
    if (m < 0 || m >= kMemories) return;
    auto& s = slots[(size_t) getSelectedSlot()];
    s.memories[(size_t) m] = s.current;
    changed (getSelectedSlot());
}

void ChoppageProcessor::recallMemory (int m)
{
    if (m < 0 || m >= kMemories) return;
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    if (! s.memories[(size_t) m].has_value()) return;
    s.current = *s.memories[(size_t) m];
    s.showOriginal = false;
    pushHistory (s, s.current);
    publish (slot);
}

void ChoppageProcessor::clearPattern()
{
    const int slot = getSelectedSlot();
    auto& s = slots[(size_t) slot];
    const int n = s.current.numSteps;
    s.current = Pattern();
    s.current.numSteps = n;
    s.showOriginal = false;
    pushHistory (s, s.current);
    publish (slot);
}

void ChoppageProcessor::timerCallback()
{
    // Pattern memory notes (G#3-B4) arrive from the audio thread
    const int req = engine.memoryRequest.exchange (-1);
    if (req >= 0)
    {
        const int slot = req / kMemories, m = req % kMemories;
        if (slot == getSelectedSlot()) recallMemory (m);
        else if (slots[(size_t) slot].memories[(size_t) m].has_value())
        {
            auto& s = slots[(size_t) slot];
            s.current = *s.memories[(size_t) m];
            pushHistory (s, s.current);
            publish (slot);
        }
    }
    // A break note changed the slot: the UI follows (and the selected slice resets)
    static_cast<void> (0);

    // Free contents nothing uses any more (never on the audio thread)
    keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(),
                                     [] (const SlotContent::Ptr& p) { return p->getReferenceCount() <= 1; }),
                     keepAlive.end());
}

// ======================================================================================
//  State
// ======================================================================================
void ChoppageProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("CHOPPAGE");
    root.setProperty ("version", JucePlugin_VersionString, nullptr);
    root.setProperty ("selectedSlot", getSelectedSlot(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    juce::ValueTree slotsTree ("SLOTS");
    for (int i = 0; i < kSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        if (s.status == Status::empty) continue;
        juce::ValueTree t ("SLOT");
        t.setProperty ("index", i, nullptr);
        t.setProperty ("demo", s.demo, nullptr);
        t.setProperty ("path", s.file.getFullPathName(), nullptr);
        t.setProperty ("grid", s.gridSlicing, nullptr);
        t.setProperty ("sens", s.sensitivity, nullptr);
        t.setProperty ("gridPerBar", s.gridPerBar, nullptr);
        if (s.content != nullptr)
        {
            t.setProperty ("bpm", s.content->bpm, nullptr);
            t.setProperty ("bars", s.content->bars, nullptr);
            t.setProperty ("slices", s.content->slices.toString(), nullptr);
            juce::String b;
            for (int k = 0; k < s.content->numSlices(); ++k) b += badgeLetter (s.content->badges[(size_t) k]);
            t.setProperty ("badges", b, nullptr);
            juce::StringArray ps;
            for (int k = 0; k < s.content->numSlices(); ++k) ps.add (sliceParamsToString (s.program.slice[(size_t) k]));
            t.setProperty ("sliceParams", ps.joinIntoString (";"), nullptr);
        }
        const auto& a = s.program.all;
        SliceParams all; all.pitch = a.pitch; all.gate = a.gate; all.stretch = a.stretch; all.reverse = a.reverse;
        all.stutter = a.stutter; all.trans = a.trans; all.xfadeMs = a.xfadeMs;
        t.setProperty ("all", sliceParamsToString (all), nullptr);
        t.setProperty ("pattern", patternToString (s.current), nullptr);
        t.setProperty ("showOriginal", s.showOriginal, nullptr);
        for (int m = 0; m < kMemories; ++m)
            if (s.memories[(size_t) m].has_value())
            {
                juce::ValueTree mt ("MEMORY");
                mt.setProperty ("index", m, nullptr);
                mt.setProperty ("pattern", patternToString (*s.memories[(size_t) m]), nullptr);
                t.appendChild (mt, nullptr);
            }
        slotsTree.appendChild (t, nullptr);
    }
    root.appendChild (slotsTree, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void ChoppageProcessor::restoreSlot (int i, const juce::ValueTree& t)
{
    auto& s = slots[(size_t) i];
    s.gridSlicing = (bool) t.getProperty ("grid", false);
    s.sensitivity = (float) t.getProperty ("sens", 0.62f);
    s.gridPerBar = (int) t.getProperty ("gridPerBar", 16);
    if ((bool) t.getProperty ("demo", false))
    {
        auto c = analyse (DemoBreak::make (44100.0, 170.0), false, 0.62f, 16, &t);
        installContent (i, c, {}, true, &t);
        return;
    }
    const juce::File file (t.getProperty ("path").toString());
    s.status = Status::loading; s.file = file;
    ++loading;
    juce::WeakReference<ChoppageProcessor> weak (this);
    pool.addJob ([this, weak, i, file, t]
    {
        auto r = tb::SampleLoader::loadFile (file, formats);
        SlotContent::Ptr c;
        if (r.status == tb::SampleLoader::Status::ok) c = analyse (r.buffer, false, 0.62f, 16, &t);
        const bool missing = r.status == tb::SampleLoader::Status::missing;
        juce::MessageManager::callAsync ([weak, i, file, c, t, missing]
        {
            if (auto* p = weak.get())
            {
                --p->loading;
                if (c != nullptr) p->installContent (i, c, file, false, &t);
                else
                {
                    p->slots[(size_t) i].status = missing ? Status::missing : Status::unreadable;
                    p->changed (i);
                }
            }
        });
    });
}

void ChoppageProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("CHOPPAGE")) return;
    const auto p = root.getChildWithName (apvts.state.getType());
    if (p.isValid()) apvts.replaceState (p);

    for (int i = 0; i < kSlots; ++i) clearSlot (i);
    for (auto t : root.getChildWithName ("SLOTS"))
    {
        const int i = (int) t.getProperty ("index", -1);
        if (i >= 0 && i < kSlots) restoreSlot (i, t);
    }
    selectSlot ((int) root.getProperty ("selectedSlot", 0));
}

#if CHOP_TEST_BUILD
void ChoppageProcessor::waitUntilIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (isBusy() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}
#endif

juce::AudioProcessorEditor* ChoppageProcessor::createEditor() { return new ChoppageEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChoppageProcessor(); }
