#include "Engine/Engine.h"
#include <cmath>

namespace bc
{
    void Engine::prepare (double sampleRate, int block)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlock = juce::jmax (kChunk, block);
        mix.setSize (2, kChunk);
        for (auto& v : voices) v.prepare (sr);
        perf.prepare (sr, kChunk);
        chain.prepare (sr, kChunk);
        reset();
    }

    void Engine::reset()
    {
        for (auto& v : voices) v.kill();
        perf.reset();
        chain.reset();
        lastStepGlobal = std::numeric_limits<int64_t>::min();
        stepPos = internalPpq = 0.0;
        wasRunning = wasHostPlaying = false;
        autoPadLeft.fill (0);
        midiPad.fill (false);
        currentStep = -1;
        playingSlot = -1;
    }

    void Engine::publishContent (int slot, SlotContent::Ptr c)
    {
        if (slot < 0 || slot >= kSlots) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        pendingContent[(size_t) slot] = std::move (c);
        pendingVersion.fetch_add (1);
    }

    void Engine::publishProgram (int slot, const SlotProgram& p)
    {
        if (slot < 0 || slot >= kSlots) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        pendingProgram[(size_t) slot] = p;
        pendingVersion.fetch_add (1);
    }

    void Engine::pullPublished() noexcept
    {
        const auto v = pendingVersion.load();
        if (v == seenVersion) return;
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked()) return;   // next block
        // Ptr copies only bump counts; the message thread keeps every content alive,
        // so dropping the old one here never frees memory on the audio thread.
        content = pendingContent;
        program = pendingProgram;
        seenVersion = v;
    }

    // ------------------------------------------------------------------------------------
    void Engine::playSlice (int slice, int atSample, int gateSamples, const Settings& s) noexcept
    {
        const auto& c = content[(size_t) activeSlot];
        if (c == nullptr || c->buffer == nullptr) return;
        const auto& prog = program[(size_t) activeSlot];
        slice = juce::jlimit (0, c->numSlices() - 1, slice);
        const auto& sp = prog.slice[(size_t) slice];
        const auto& all = prog.all;
        if (sp.mute) return;

        SliceVoice::Start st;
        st.content = c;
        st.region = c->region (slice);

        double rate = c->buffer->sourceSampleRate / sr;
        if (s.fit && ! s.keepPitch && ! s.midiTrigger)
        {
            const double hostBpm = lastHostBpm;
            rate *= hostBpm / juce::jmax (20.0, c->bpm * s.tempoSourceMul);
        }
        rate *= std::pow (2.0, (sp.pitch + all.pitch) / 12.0);
        st.rate = rate;
        st.reverse = sp.reverse != all.reverse;
        st.stutter = sp.stutter > 1 ? sp.stutter : all.stutter;
        st.stretch = juce::jlimit (0.25, 8.0, (double) sp.stretch * (double) all.stretch);
        st.cycleSamples = juce::jmax (16, (int) (s.stretchCycleMs * 0.001 * sr));
        st.trans = juce::jlimit (0.0f, 1.0f, sp.trans + (all.trans - 0.5f));
        const float gate = juce::jlimit (0.02f, 1.0f, sp.gate * all.gate);
        st.gateSamples = gateSamples > 0 ? juce::jmax (8, (int) (gateSamples * gate)) : (1 << 30);
        const float xf = juce::jmax (sp.xfadeMs, all.xfadeMs);
        st.fadeSamples = juce::jmax (8, (int) (xf * 0.001f * (float) sr));
        st.level = sp.level;

        // Mono: whatever was playing fades out over this slice's crossfade.
        renderVoices (renderedUpTo, atSample);
        for (auto& v : voices) v.release (st.fadeSamples);
        auto& v = voices[(size_t) nextVoice];
        nextVoice = (nextVoice + 1) % (int) voices.size();
        v.start (st);
        currentSlice = slice;
    }

    void Engine::triggerStep (int step, int atSample, const Settings& s, double samplesPerStep) noexcept
    {
        const auto& prog = program[(size_t) activeSlot];
        const auto& pat = prog.pattern;
        const int n = juce::jlimit (1, kMaxSteps, pat.numSteps);
        step = ((step % n) + n) % n;
        currentStep = step;

        // FX lane: an event holds its effect for one step (tape stop: a beat)
        const auto& e = pat.fx[(size_t) step];
        if (e.type != FxType::none)
        {
            const int len = (int) (samplesPerStep * (e.type == FxType::tapeStop ? 4.0 : 1.0));
            switch (e.type)
            {
                case FxType::stutter:  autoPadLeft[PerfFx::stutter] = len; autoStutterDiv = e.rate; break;
                case FxType::reverse:  autoPadLeft[PerfFx::reverse] = len; break;
                case FxType::stretch:  autoPadLeft[PerfFx::akaize] = len; break;
                case FxType::tapeStop: autoPadLeft[PerfFx::tapeStop] = len; break;
                case FxType::none: break;
            }
        }

        const int slice = pat.slice[(size_t) step];
        if (slice < 0) return;
        // Gate: until the next hit in the pattern
        int gapSteps = 1;
        while (gapSteps < n && pat.slice[(size_t) ((step + gapSteps) % n)] < 0) ++gapSteps;
        playSlice (slice, atSample, (int) (gapSteps * samplesPerStep), s);
    }

    void Engine::renderVoices (int from, int to) noexcept
    {
        if (to <= from) return;
        float* out[2] = { mix.getWritePointer (0), mix.getWritePointer (1) };
        for (auto& v : voices)
            if (v.isActive()) v.render (out, 2, from, to - from);
        renderedUpTo = to;
    }

    void Engine::handleMidi (const juce::MidiMessage& m, const Settings& s, double ppqNow) noexcept
    {
        const int note = m.getNoteNumber();
        if (m.isNoteOn())
        {
            lastNote = note;
            if (note >= kFirstBreakNote && note < kFirstBreakNote + kSlots)
            {
                const int slot = note - kFirstBreakNote;
                const bool sameAgain = slot == activeSlot && latched && heldBreakNote < 0;
                selectedSlot = slot;
                heldBreakNote = note;
                if (! s.gateHold && sameAgain) { latched = false; return; }   // LATCH: same key again stops
                latched = true;
                if (s.breakMode == 2 && wasRunning && slot != activeSlot) { pendingSlot = slot; return; }
                activeSlot = slot;
                startStepOffset = 0;
                if (s.velToSlice)
                {
                    const int n = program[(size_t) slot].pattern.numSteps;
                    startStepOffset = (m.getVelocity() * 16 / 128) * n / 16;
                }
                if (s.breakMode == 0 || s.velToSlice) { restartNow = true; restartFromNote = true; }
                else if (! wasRunning) restartNow = true;
                juce::ignoreUnused (ppqNow);
            }
            else if (note >= kFirstFxNote && note < kFirstFxNote + kFxPads)
                midiPad[(size_t) (note - kFirstFxNote)] = true;
            else if (note >= kFirstSliceNote && note < kFirstSliceNote + 16)
                pendingSliceNote = note - kFirstSliceNote;
            else if (note >= kFirstMemoryNote && note < kFirstMemoryNote + kMemories)
                memoryRequest = activeSlot * kMemories + (note - kFirstMemoryNote);
        }
        else if (m.isNoteOff())
        {
            if (note >= kFirstFxNote && note < kFirstFxNote + kFxPads)
                midiPad[(size_t) (note - kFirstFxNote)] = false;
            else if (note == heldBreakNote)
            {
                heldBreakNote = -1;
                if (s.gateHold) latched = false;
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            midiPad.fill (false);
            heldBreakNote = -1;
            if (s.gateHold) latched = false;
        }
    }

    // ------------------------------------------------------------------------------------
    void Engine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Host& host,
                          const Settings& sIn) noexcept
    {
        pullPublished();
        const int total = out.getNumSamples();
        out.clear();

        Settings s = sIn;
        const auto& sel = content[(size_t) juce::jlimit (0, kSlots - 1, activeSlot)];
        const double bpm = host.hasInfo && host.bpm > 1.0 ? host.bpm
                         : (sel != nullptr ? sel->bpm * s.tempoSourceMul : 120.0);
        lastHostBpm = bpm;
        const double ppqPerSample = bpm / 60.0 / sr;
        s.pads.samplesPerBeat = sr * 60.0 / bpm;

        // The UI picked a different break: follow it (as a break note would, without starting it).
        {
            const int ui = selectedSlot.load();
            if (ui != activeSlot && ui >= 0 && ui < kSlots && pendingSlot < 0 && heldBreakNote < 0)
            {
                activeSlot = ui;
                restartNow = s.breakMode == 0;
            }
        }

        if (! host.playing && wasHostPlaying) latched = false;   // host stop ends a latch
        if (host.playing && ! wasHostPlaying) { restartNow = true; internalPpq = host.ppq; }
        wasHostPlaying = host.playing;

        auto midiIt = midi.begin();
        for (int c0 = 0; c0 < total; c0 += kChunk)
        {
            const int n = juce::jmin (kChunk, total - c0);
            mix.clear();
            renderedUpTo = 0;

            for (int i = 0; i < n; ++i)
            {
                const int abs = c0 + i;
                while (midiIt != midi.end() && (*midiIt).samplePosition <= abs)
                {
                    handleMidi ((*midiIt).getMessage(), s, 0.0);
                    ++midiIt;
                }

                const double ppqNow = host.playing ? host.ppq + abs * ppqPerSample : internalPpq;
                const bool halfTime = midiPad[PerfFx::halfTime] || uiPad[PerfFx::halfTime].load();
                const double speed = s.speed * (halfTime ? 0.5 : 1.0);

                const bool hostRuns = host.playing && ! s.gateHold;
                const bool running = ! s.midiTrigger && content[(size_t) activeSlot] != nullptr
                                     && (hostRuns || latched || uiPlay.load());

                // QUANT switch on the next beat line
                if (pendingSlot >= 0)
                {
                    const double beatPos = ppqNow - std::floor (ppqNow);
                    if (! running || beatPos < ppqPerSample) { activeSlot = pendingSlot; pendingSlot = -1; }
                }

                if (running)
                {
                    const auto& pat = program[(size_t) activeSlot].pattern;
                    const int steps = juce::jlimit (1, kMaxSteps, pat.numSteps);
                    const bool followHost = host.playing && ! halfTime;
                    if (! wasRunning || restartNow)
                    {
                        // BAR SYNC / QUANT: lock to the host grid; RESTART (or a note that asks): step 0 now
                        const bool grid = s.breakMode != 0 && host.playing && ! restartFromNote;
                        restartFromNote = false;
                        stepPos = grid ? ppqNow * 4.0 * speed : (double) startStepOffset;
                        syncOffset = stepPos - ppqNow * 4.0 * speed;
                        lastStepGlobal = std::numeric_limits<int64_t>::min();
                        restartNow = false;
                    }
                    else if (followHost)
                    {
                        // Follow the song position every sample: loop braces and relocates stay locked.
                        // (After HALF-TIME this also snaps back to where the grid is.)
                        stepPos = ppqNow * 4.0 * speed + syncOffset;
                    }
                    halfTimeWas = halfTime;

                    // which step (swung) are we in?
                    const double whole = std::floor (stepPos);
                    const double frac = stepPos - whole;
                    auto g = (int64_t) whole;
                    const double swingAt = (double) (s.swing - 0.5f) * 2.0;   // 0 .. 0.5 of a step
                    if ((g & 1) != 0 && frac < swingAt) --g;
                    if (g != lastStepGlobal)
                    {
                        lastStepGlobal = g;
                        const double samplesPerStep = sr * 60.0 / bpm / 4.0 / speed;
                        triggerStep ((int) (g % steps), i, s, samplesPerStep);
                    }
                    if (! followHost) stepPos += ppqPerSample * 4.0 * speed;
                    playingSlot = activeSlot;
                }
                else if (wasRunning)
                {
                    renderVoices (renderedUpTo, i);
                    for (auto& v : voices) v.release ((int) (0.005 * sr));
                    currentStep = -1;
                    playingSlot = -1;
                }
                wasRunning = running;

                // Slice notes / preview: one-shots over the top
                int one = pendingSliceNote;
                pendingSliceNote = -1;
                if (one < 0) one = previewSlice.exchange (-1);
                if (one >= 0 && content[(size_t) activeSlot] != nullptr)
                    playSlice (one, i, 0, s);

                if (! host.playing) internalPpq += ppqPerSample;
            }
            renderVoices (renderedUpTo, n);

            // FX pads: MIDI, UI and the FX lane
            std::array<bool, kFxPads> held {};
            for (int p = 0; p < kFxPads; ++p)
            {
                const bool lane = autoPadLeft[(size_t) p] > 0;
                held[(size_t) p] = midiPad[(size_t) p] || uiPad[(size_t) p].load() || lane;
                if (lane) autoPadLeft[(size_t) p] -= n;
            }
            PerfFx::Settings ps = s.pads;
            if (autoPadLeft[PerfFx::stutter] > 0 && ! midiPad[PerfFx::stutter] && ! uiPad[PerfFx::stutter].load())
                ps.stutterDiv = autoStutterDiv;
            const double samplesPerStep = sr * 60.0 / bpm / 4.0;
            const double ppqHere = host.playing ? host.ppq + c0 * ppqPerSample : internalPpq;
            ps.samplesSinceGrid = (ppqHere * 4.0 - std::floor (ppqHere * 4.0)) * samplesPerStep;
            perf.process (mix, n, held, ps);

            SoundChain::Settings cs = s.sound;
            cs.dropHeld = held[PerfFx::s950Drop];
            chain.process (mix, n, cs);

            for (int p = 0; p < kFxPads; ++p)
                padLit[(size_t) p] = held[(size_t) p] || perf.isAudible (p);

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.copyFrom (ch, c0, mix, juce::jmin (ch, 1), 0, n);
        }
    }
}
