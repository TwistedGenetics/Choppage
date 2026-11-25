#include "Engine/Analysis.h"
#include <cmath>

namespace bc::Analysis
{
    namespace
    {
        struct Bands { double low = 0, mid = 0, high = 0; float peak = 0; };

        // Band energies of [s0, s0 + len). The filters run from `warm` samples earlier so they are settled.
        Bands bands (const SampleBuffer& buf, int s0, int len, int warm)
        {
            Bands out;
            const double sr = buf.sourceSampleRate;
            juce::IIRFilter lowF, highF, midHp, midLp;
            lowF.setCoefficients  (juce::IIRCoefficients::makeLowPass  (sr, 150.0));
            highF.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 5000.0));
            midHp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 300.0));
            midLp.setCoefficients (juce::IIRCoefficients::makeLowPass  (sr, 4000.0));
            const int n = buf.getNumSamples(), chans = buf.getNumChannels();
            const int from = juce::jmax (0, s0 - warm);
            const int to = juce::jmin (n, s0 + len);
            for (int i = from; i < to; ++i)
            {
                float x = 0.0f;
                for (int c = 0; c < chans; ++c) x += buf.audio.getSample (c, i);
                x /= (float) chans;
                const float l = lowF.processSingleSampleRaw (x);
                const float h = highF.processSingleSampleRaw (x);
                const float m = midLp.processSingleSampleRaw (midHp.processSingleSampleRaw (x));
                if (i < s0) continue;
                out.low += (double) l * l; out.high += (double) h * h; out.mid += (double) m * m;
                out.peak = juce::jmax (out.peak, std::abs (x));
            }
            return out;
        }
    }

    SliceFeatures measure (const SampleBuffer& buf, juce::Range<int> region)
    {
        SliceFeatures f;
        const double sr = buf.sourceSampleRate;
        const int s0 = region.getStart();
        const int win = juce::jmin (region.getLength(), (int) (0.04 * sr));   // the attack: first 40 ms
        if (win < 32) return f;

        // What is NEW at this slice: its first 40 ms minus what was already ringing just
        // before it (the previous hit's tail, e.g. a kick under a ghost note).
        const auto now  = bands (buf, s0, win, (int) (0.01 * sr));
        const int pre = juce::jmin (s0, (int) (0.02 * sr));
        Bands before;
        if (pre > 16) before = bands (buf, s0 - pre, pre, (int) (0.01 * sr));
        const double scale = (double) win / (double) juce::jmax (1, pre);
        const double low  = juce::jmax (0.0, now.low  - before.low  * scale);
        const double mid  = juce::jmax (0.0, now.mid  - before.mid  * scale);
        const double high = juce::jmax (0.0, now.high - before.high * scale);
        const double total = low + mid + high + 1e-12;
        f.low = (float) (low / total); f.mid = (float) (mid / total); f.high = (float) (high / total);
        f.peak = juce::jmax (0.0f, now.peak - before.peak * 0.5f);

        // Decay: envelope from the peak to -20 dB, over the whole slice (capped at 400 ms)
        const int len = juce::jmin (region.getLength(), (int) (0.4 * sr));
        const float rel = std::exp (-1.0f / (float) (0.004 * sr));
        float env = 0.0f, top = 0.0f; int topAt = 0, endAt = len;
        std::vector<float> e ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < buf.getNumChannels(); ++c) x += std::abs (buf.audio.getSample (c, s0 + i));
            env = x > env ? x : env * rel + x * (1.0f - rel);
            e[(size_t) i] = env;
            if (env > top) { top = env; topAt = i; }
        }
        for (int i = topAt; i < len; ++i) if (e[(size_t) i] < top * 0.1f) { endAt = i; break; }
        f.decayMs = (float) ((endAt - topAt) * 1000.0 / sr);
        return f;
    }

    std::array<Badge, kMaxSlices> classify (const SampleBuffer& buf, const SliceTable& slices)
    {
        std::array<Badge, kMaxSlices> out;
        out.fill (Badge::other);
        const int n = buf.getNumSamples();
        const int count = juce::jmax (1, slices.count);

        std::array<SliceFeatures, kMaxSlices> f {};
        float loudest = 1e-6f;
        for (int k = 0; k < count; ++k)
        {
            f[(size_t) k] = measure (buf, slices.region (k, n));
            loudest = juce::jmax (loudest, f[(size_t) k].peak);
        }

        for (int k = 0; k < count; ++k)
        {
            const auto& s = f[(size_t) k];
            const float rel = s.peak / loudest;
            Badge b;
            if (rel < 0.04f)                                   b = Badge::other;   // near silence
            else if (s.high > 0.6f && s.low < 0.1f)
            {
                // open = still ringing at the end of its slice (or past 110 ms)
                const float sliceMs = (float) (slices.region (k, n).getLength() * 1000.0 / buf.sourceSampleRate);
                b = s.decayMs > juce::jmin (110.0f, sliceMs * 0.85f) ? Badge::open : Badge::hat;
            }
            else if (s.low > 0.65f && s.high < 0.12f)          b = Badge::kick;
            else if (s.mid + s.high > 0.35f)                   b = rel < 0.45f ? Badge::ghost : Badge::snare;
            else if (s.low > s.high)                           b = Badge::kick;
            else                                               b = Badge::other;
            out[(size_t) k] = b;
        }
        return out;
    }

    Tempo estimateTempo (double lengthSeconds, const juce::String& fileName)
    {
        Tempo t;
        if (lengthSeconds <= 0.0) return t;

        // A number in the name between 60 and 200 is taken as the BPM ("think_136", "amen 170bpm").
        juce::String digits;
        double hinted = 0.0;
        for (auto c : fileName + " ")
        {
            if (juce::CharacterFunctions::isDigit (c)) digits += c;
            else
            {
                if (digits.isNotEmpty())
                {
                    const double v = digits.getDoubleValue();
                    if (v >= 60.0 && v <= 200.0) hinted = v;
                }
                digits.clear();
            }
        }

        const int barChoices[] = { 1, 2, 4, 8 };
        double best = 1e9;
        for (int bars : barChoices)
        {
            const double bpm = bars * 4.0 * 60.0 / lengthSeconds;
            if (bpm < 70.0 || bpm > 190.0) continue;
            const double score = hinted > 0.0 ? std::abs (bpm - hinted) : std::abs (bpm - 140.0);
            if (score < best) { best = score; t.bpm = bpm; t.bars = bars; }
        }
        if (best > 1e8)   // odd length: keep the nearest sensible bar count
        {
            t.bars = juce::jmax (1, (int) std::round (lengthSeconds * 140.0 / 240.0));
            t.bpm  = t.bars * 240.0 / lengthSeconds;
        }
        return t;
    }

    SliceTable gridSlices (int numSamples, int bars, int stepsPerBar)
    {
        return SliceTable::equal (numSamples, juce::jlimit (1, kMaxSlices, bars * stepsPerBar));
    }

    Pattern originalPattern (const SlotContent& c)
    {
        Pattern p;
        const int n = c.buffer != nullptr ? c.buffer->getNumSamples() : 0;
        p.numSteps = juce::jlimit (16, kMaxSteps, c.bars * 16);
        if (n <= 0) return p;
        const double perStep = (double) n / (double) p.numSteps;
        for (int k = 0; k < c.numSlices(); ++k)
        {
            const int start = c.slices.count > 0 ? c.slices.starts[(size_t) k] : 0;
            const int step = juce::jlimit (0, p.numSteps - 1, (int) std::round (start / perStep));
            if (p.slice[(size_t) step] < 0) p.slice[(size_t) step] = (int16_t) k;
        }
        return p;
    }
}
