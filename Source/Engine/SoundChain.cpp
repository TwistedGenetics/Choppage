#include "Engine/SoundChain.h"
#include <cmath>

namespace bc
{
    void SoundChain::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlock), 2 };
        charLp.prepare (spec);
        charLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        charLp.setResonance (0.6f);
        filt.prepare (spec);
        cutoff.reset (sr, 0.03);
        echo.setSize (2, (int) (sr * 3.0) + 1);
        pre.setSize (2, (int) (sr * 0.11) + 1);
        wetBuf.setSize (2, juce::jmax (1, maxBlock));
        verb.setSampleRate (sr);
        gain.reset (sr, 0.02);
        reset();
    }

    void SoundChain::reset()
    {
        holdPhase = crushPhase = 1.0;
        held[0] = held[1] = crushHeld[0] = crushHeld[1] = 0.0f;
        toneState[0] = toneState[1] = echoLp[0] = echoLp[1] = 0.0f;
        charLp.reset(); filt.reset(); verb.reset();
        echo.clear(); pre.clear(); echoW = preW = 0;
    }

    void SoundChain::process (juce::AudioBuffer<float>& buf, int n, const Settings& s) noexcept
    {
        const int chans = juce::jmin (2, buf.getNumChannels());
        float* d[2] = { buf.getWritePointer (0), buf.getWritePointer (chans > 1 ? 1 : 0) };

        // ---- CHARACTER -------------------------------------------------------------
        const auto& c = character (s.character);
        double rate = c.rate > 0.0 ? juce::jmin (c.rate, s.faderRate) : s.faderRate;
        int bits = c.bits;
        if (s.dropHeld) { rate = juce::jmin (rate, s.dropRate); bits = bits == 0 ? 8 : juce::jmin (bits, 8); }
        const bool decimate = rate < sr - 1.0;
        const double step = rate / sr;
        const float q = bits > 0 ? (float) (1 << (bits - 1)) : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (decimate)
            {
                holdPhase += step;
                if (holdPhase >= 1.0)
                {
                    holdPhase -= 1.0;
                    for (int ch = 0; ch < chans; ++ch) held[ch] = d[ch][i];
                }
            }
            for (int ch = 0; ch < chans; ++ch)
            {
                float v = decimate ? held[ch] : d[ch][i];
                if (q > 0.0f) v = std::round (juce::jlimit (-1.0f, 1.0f, v) * q) / q;
                d[ch][i] = v;
            }
        }
        if (c.lowpassHz > 0.0f && c.lowpassHz < sr * 0.45)
        {
            charLp.setCutoffFrequency (c.lowpassHz);
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < n; ++i) d[ch][i] = charLp.processSample (ch, d[ch][i]);
        }

        // ---- FILTER ------------------------------------------------------------------
        filt.setType (s.highPass ? juce::dsp::StateVariableTPTFilterType::highpass
                                 : juce::dsp::StateVariableTPTFilterType::lowpass);
        filt.setResonance (0.707f + s.filterRes * 7.0f);
        cutoff.setTargetValue (20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, s.filter)));
        const bool filterOpen = (! s.highPass && s.filter >= 0.999f) || (s.highPass && s.filter <= 0.001f);
        if (! filterOpen || cutoff.isSmoothing())
            for (int i = 0; i < n; ++i)
            {
                if ((i & 7) == 0) filt.setCutoffFrequency (juce::jlimit (20.0f, (float) (sr * 0.45), cutoff.getNextValue()));
                else cutoff.getNextValue();
                for (int ch = 0; ch < chans; ++ch) d[ch][i] = filt.processSample (ch, d[ch][i]);
            }

        // ---- DISTORTION --------------------------------------------------------------
        if (s.dist > 0.001f)
        {
            const float g = juce::Decibels::decibelsToGain (s.dist * 30.0f);
            const float norm = 1.0f / std::tanh (g);
            const float toneA = 0.15f + 0.8f * (s.distTone * 0.5f + 0.5f);   // one-pole: dark .. bright
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float y = std::tanh (d[ch][i] * g) * norm * 0.8f;
                    toneState[ch] += (y - toneState[ch]) * toneA;
                    d[ch][i] = toneState[ch];
                }
        }

        // ---- CRUSH --------------------------------------------------------------------
        if (s.crush > 0.001f)
        {
            const float cbits = 16.0f - s.crush * 12.0f;
            const float cq = std::pow (2.0f, cbits - 1.0f);
            const double cstep = 1.0 / (1.0 + s.crush * 7.0);
            for (int i = 0; i < n; ++i)
            {
                crushPhase += cstep;
                if (crushPhase >= 1.0)
                {
                    crushPhase -= 1.0;
                    for (int ch = 0; ch < chans; ++ch) crushHeld[ch] = std::round (d[ch][i] * cq) / cq;
                }
                for (int ch = 0; ch < chans; ++ch) d[ch][i] = crushHeld[ch];
            }
        }

        // ---- ECHO ---------------------------------------------------------------------
        if (s.echoMix > 0.001f)
        {
            const int len = echo.getNumSamples();
            const int delay = juce::jlimit (1, len - 1, (int) s.echoSamples);
            const float lpA = 0.35f;
            for (int i = 0; i < n; ++i)
            {
                const int r = (echoW - delay + len) % len;
                float wet[2] = { echo.getSample (0, r), echo.getSample (1, r) };
                const float in0 = d[0][i], in1 = d[chans > 1 ? 1 : 0][i];
                const float mono = 0.5f * (in0 + in1);
                for (int ch = 0; ch < 2; ++ch)
                {
                    echoLp[ch] += (wet[ch] - echoLp[ch]) * lpA;
                    const float in = s.echoMono ? mono : (ch == 0 ? in0 : in1);
                    echo.setSample (ch, echoW, in + echoLp[ch] * s.echoFb);
                }
                for (int ch = 0; ch < chans; ++ch) d[ch][i] += wet[ch] * s.echoMix;
                echoW = (echoW + 1) % len;
            }
        }

        // ---- REVERB ---------------------------------------------------------------------
        if (s.reverb > 0.001f)
        {
            juce::Reverb::Parameters p;
            p.roomSize = 0.3f + s.revDecay * 0.68f;
            p.damping  = s.revDamp;
            p.width    = s.revWidth;
            p.wetLevel = 1.0f;
            p.dryLevel = 0.0f;
            verb.setParameters (p);
            // The reverb hears the signal REV PRE ms late; the dry path is untouched.
            const int plen = pre.getNumSamples();
            const int pd = juce::jlimit (0, plen - 1, (int) (s.revPreMs * 0.001f * sr));
            const int m = juce::jmin (n, wetBuf.getNumSamples());
            float* w[2] = { wetBuf.getWritePointer (0), wetBuf.getWritePointer (1) };
            for (int i = 0; i < m; ++i)
            {
                const int r = (preW - pd + plen) % plen;
                for (int ch = 0; ch < 2; ++ch)
                {
                    pre.setSample (ch, preW, d[juce::jmin (ch, chans - 1)][i]);
                    w[ch][i] = pre.getSample (ch, r);
                }
                preW = (preW + 1) % plen;
            }
            verb.processStereo (w[0], w[1], m);
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < m; ++i) d[ch][i] = d[ch][i] * (1.0f - s.reverb * 0.4f) + w[ch][i] * s.reverb;
        }

        // ---- VOLUME + meter ----------------------------------------------------------------
        gain.setTargetValue (s.volumeDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (s.volumeDb));
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
            {
                d[ch][i] *= g;
                pk = juce::jmax (pk, std::abs (d[ch][i]));
            }
        }
        float prev = peak.load();
        while (pk > prev && ! peak.compare_exchange_weak (prev, pk)) {}
    }
}
