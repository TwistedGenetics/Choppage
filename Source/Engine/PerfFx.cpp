#include "Engine/PerfFx.h"
#include <cmath>

namespace bc
{
    namespace { constexpr int kReadOrder[] = { PerfFx::tapeStop, PerfFx::reverse, PerfFx::roll, PerfFx::stutter, PerfFx::akaize }; }

    void PerfFx::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        int size = 1;
        while (size < (int) (sr * 4.0)) size <<= 1;
        ring.setSize (2, size, false, true, false);
        ringMask = size - 1;
        rampStep = 1.0f / (float) juce::jmax (1.0, 0.003 * sr);
        dive.prepare ({ sr, (juce::uint32) juce::jmax (1, maxBlock), 2 });
        dive.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        reset();
    }

    void PerfFx::reset() noexcept
    {
        ring.clear();
        written = 0;
        for (auto& f : fx) f = {};
        wasHeld.fill (false);
        dive.reset();
        diveMix = 0.0f;
        diveCutoff = 18000.0f;
    }

    float PerfFx::ringAt (int ch, double absPos) const noexcept
    {
        if (absPos < 0.0 || absPos > (double) written - 1.0) return 0.0f;
        if ((double) written - absPos >= (double) ringMask) return 0.0f;   // older than the ring holds
        const auto i = (int64_t) std::floor (absPos);
        const float f = (float) (absPos - (double) i);
        const float* d = ring.getReadPointer (ch);
        const float a = d[(int) (i & ringMask)];
        const float b = d[(int) ((i + 1) & ringMask)];
        return a + (b - a) * f;
    }

    void PerfFx::engage (int pad, const Settings& s) noexcept
    {
        auto& r = fx[(size_t) pad];
        r.age = 0;
        const double now = (double) written - 1.0;   // the sample just recorded
        switch (pad)
        {
            case stutter:
            {
                r.loopLen = s.samplesPerBeat * 4.0 / (double) s.stutterDiv;
                double back = 0.0;
                if (s.quantSteps > 0)
                {
                    const double grid = s.samplesPerBeat / 4.0 * (double) s.quantSteps;
                    back = std::fmod (s.samplesSinceGrid, grid);
                }
                r.loopStart = now - back;
                r.phase = back;            // carries on live until the loop wraps
                break;
            }
            case roll:
                r.loopLen = s.samplesPerBeat * 4.0 / (double) s.rollDiv;
                r.loopStart = now - r.loopLen;
                r.phase = 0.0;
                r.speed = 1.0;
                break;
            case reverse:
            case tapeStop:
                r.pos = now;
                r.speed = 1.0;
                break;
            case akaize:
                r.pos = now;
                r.grainPos = r.prevGrainPos = now;
                r.grainAge = 0;
                break;
            case filterDive:
                diveAge = 0;
                break;
            default: break;
        }
    }

    float PerfFx::readEffect (int pad, int ch, const Settings& s) const noexcept
    {
        const auto& r = fx[(size_t) pad];
        switch (pad)
        {
            case stutter:
            case roll:     return ringAt (ch, r.loopStart + r.phase);
            case reverse:
            case tapeStop: return ringAt (ch, r.pos);
            case akaize:
            {
                const int cycle = juce::jmax (8, (int) (s.akCycleMs * 0.001 * sr));
                const int splice = juce::jmin (cycle / 2, juce::jmax (4, (int) (0.0005 * sr)));
                float v = ringAt (ch, r.grainPos);
                if (r.grainAge < splice)
                {
                    const float a = (float) r.grainAge / (float) splice;
                    v = v * a + ringAt (ch, r.prevGrainPos) * (1.0f - a);
                }
                juce::ignoreUnused (cycle);
                return v;
            }
            default: return 0.0f;
        }
    }

    void PerfFx::advance (int pad, const Settings& s) noexcept
    {
        auto& r = fx[(size_t) pad];
        ++r.age;
        switch (pad)
        {
            case stutter:
                r.phase += 1.0;
                if (r.phase >= r.loopLen) r.phase -= r.loopLen;
                break;
            case roll:
                // up an octave over two beats
                r.speed = std::pow (2.0, juce::jmin (1.0, (double) r.age / (s.samplesPerBeat * 2.0)));
                r.phase += r.speed;
                while (r.phase >= r.loopLen) r.phase -= r.loopLen;
                break;
            case reverse:
                r.pos -= 1.0;
                break;
            case tapeStop:
            {
                const double t = (double) r.age / juce::jmax (1.0, s.tapeMs * 0.001 * sr);
                r.speed = juce::jmax (0.0, 1.0 - t);
                r.speed *= r.speed;          // a heavy platter slows fast, then drags
                r.pos += r.speed;
                break;
            }
            case akaize:
            {
                const int cycle = juce::jmax (8, (int) (s.akCycleMs * 0.001 * sr));
                r.pos += 1.0 / juce::jmax (1.0, s.akRatio);
                r.grainPos += 1.0;
                r.prevGrainPos += 1.0;
                if (++r.grainAge >= cycle)
                {
                    r.prevGrainPos = r.grainPos;
                    r.grainPos = r.pos;
                    r.grainAge = 0;
                }
                break;
            }
            default: break;
        }
    }

    void PerfFx::process (juce::AudioBuffer<float>& buf, int numSamples, const std::array<bool, kFxPads>& held,
                          const Settings& s) noexcept
    {
        const int chans = juce::jmin (2, buf.getNumChannels());
        float* data[2] = { buf.getWritePointer (0), buf.getWritePointer (chans > 1 ? 1 : 0) };

        // Which read effect wins (highest priority held)
        int winner = -1;
        for (int p : kReadOrder) if (held[(size_t) p]) { winner = p; break; }

        for (int i = 0; i < numSamples; ++i)
        {
            // Record the dry input
            const int w = (int) (written & ringMask);
            for (int ch = 0; ch < 2; ++ch)
                ring.getWritePointer (ch)[w] = data[juce::jmin (ch, chans - 1)][i];
            ++written;

            // Edges: (re)start an effect the moment its pad goes down
            if (i == 0)
                for (int p = 0; p < kFxPads; ++p)
                {
                    if (held[(size_t) p] && ! wasHeld[(size_t) p]) engage (p, s);
                    wasHeld[(size_t) p] = held[(size_t) p];
                }

            float sum[2] = { 0.0f, 0.0f };
            float total = 0.0f;
            for (int p : kReadOrder)
            {
                auto& r = fx[(size_t) p];
                const float target = p == winner ? 1.0f : 0.0f;
                if (r.gain < target)      r.gain = juce::jmin (target, r.gain + rampStep);
                else if (r.gain > target) r.gain = juce::jmax (target, r.gain - rampStep);
                if (r.gain <= 0.0f) continue;
                for (int ch = 0; ch < 2; ++ch) sum[ch] += readEffect (p, ch, s) * r.gain;
                total += r.gain;
                advance (p, s);
            }
            total = juce::jmin (1.0f, total);
            for (int ch = 0; ch < chans; ++ch)
                data[ch][i] = data[ch][i] * (1.0f - total) + sum[ch];
        }

        // FILTER DIVE: closes from 18 kHz to 150 Hz over a beat while held, opens in 50 ms after.
        const bool diving = held[(size_t) filterDive];
        if (diving || diveMix > 0.0f)
        {
            dive.setResonance (0.7f + s.diveRes * 6.0f);
            for (int i = 0; i < numSamples; ++i)
            {
                if (diving)
                {
                    const float t = juce::jmin (1.0f, (float) diveAge++ / (float) s.samplesPerBeat);
                    diveCutoff = 18000.0f * std::pow (150.0f / 18000.0f, t);
                    diveMix = juce::jmin (1.0f, diveMix + rampStep);
                }
                else
                {
                    diveCutoff = juce::jmin (18000.0f, diveCutoff * (1.0f + 4.0f / (float) (0.05 * sr)) + 1.0f);
                    if (diveCutoff >= 17999.0f) diveMix = juce::jmax (0.0f, diveMix - rampStep);
                }
                if ((i & 15) == 0) dive.setCutoffFrequency (juce::jlimit (40.0f, (float) (sr * 0.45), diveCutoff));
                for (int ch = 0; ch < chans; ++ch)
                {
                    const float x = data[ch][i];
                    const float y = dive.processSample (ch, x);
                    data[ch][i] = x + (y - x) * diveMix;
                }
            }
        }
    }
}
