#include "Engine/Params.h"

namespace bc::params
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });

        auto f = [&] (const char* id, const char* name, float lo, float hi, float def, AudioParameterFloatAttributes a = {})
        {
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def, a));
        };
        auto c = [&] (const char* id, const char* name, StringArray items, int def)
        {
            l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
        };
        auto b = [&] (const char* id, const char* name, bool def)
        {
            l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
        };

        // Sound
        {
            NormalisableRange<float> r (4000.0f, 44100.0f);
            r.setSkewForCentre (15000.0f);
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { sampleRate, 1 }, "Sample Rate", r, 44100.0f,
                AudioParameterFloatAttributes().withLabel ("Hz")
                    .withStringFromValueFunction ([] (float v, int) { return String (v / 1000.0f, 1) + "k"; })));
        }
        c (character, "Character", characterNames(), 1);   // S950
        {
            f (volume, "Volume", -60.0f, 6.0f, 0.0f, AudioParameterFloatAttributes().withLabel ("dB"));
        }

        // Post FX
        f (filter,   "Filter",     0.0f, 1.0f, 1.0f, pct);
        c (filterType, "Filter Type", { "LP", "HP" }, 0);
        f (filterRes, "Filter Res", 0.0f, 1.0f, 0.2f, pct);
        f (dist,     "Distortion", 0.0f, 1.0f, 0.0f, pct);
        f (distTone, "Dist Tone",  -1.0f, 1.0f, 0.0f);
        f (crush,    "Crush",      0.0f, 1.0f, 0.0f, pct);
        f (reverb,   "Reverb",     0.0f, 1.0f, 0.0f, pct);
        f (revDecay, "Reverb Decay", 0.0f, 1.0f, 0.33f, pct);
        f (revDamp,  "Reverb Damp",  0.0f, 1.0f, 0.5f, pct);
        f (revWidth, "Reverb Width", 0.0f, 1.0f, 0.8f, pct);
        f (revPre,   "Reverb Pre",   0.0f, 100.0f, 10.0f, AudioParameterFloatAttributes().withLabel ("ms"));
        f (echoMix,  "Echo Mix",     0.0f, 1.0f, 0.0f, pct);
        f (echoFb,   "Echo Feedback", 0.0f, 0.95f, 0.4f, pct);
        c (echoTime, "Echo Time", echoTimes(), 2);
        b (echoMono, "Echo Mono", true);

        // Generate
        f (patAmount, "Pattern Amount", 0.0f, 1.0f, 0.35f, pct);
        f (fxAmount,  "FX Amount",      0.0f, 1.0f, 0.2f, pct);
        c (style, "Style", styleNames(), 0);
        f (swing, "Swing", 0.5f, 0.75f, 0.5f, pct);
        b (layers, "Layers", true);

        // Tempo / trigger
        c (speed, "Speed", speeds(), 1);
        b (fit, "Fit", true);
        c (fitMode, "Fit Mode", { "REPITCH", "KEEP" }, 0);
        c (trigger, "Trigger", { "SYNC", "MIDI" }, 0);
        c (breakMode, "Break Switch", { "RESTART", "BAR SYNC", "QUANT" }, 1);
        c (gateMode, "Gate", { "LATCH", "HOLD" }, 0);
        b (velSlice, "Velocity To Start Slice", false);
        c (tempoSrc, "Tempo Source", { "AUTO", "/2", "X2" }, 0);

        // FX pads
        c (padQuant, "Pad Quantise", { "OFF", "1/16", "1/8" }, 1);
        c (stutRate, "Stutter Rate", noteRates(), 1);
        c (rollRate, "Roll Rate", noteRates(), 2);
        f (tapeMs,   "Tape Stop Time", 100.0f, 3000.0f, 700.0f, AudioParameterFloatAttributes().withLabel ("ms"));
        f (akCycle,  "Akaize Cycle", 2.0f, 60.0f, 9.5f, AudioParameterFloatAttributes().withLabel ("ms"));
        c (akRatio,  "Akaize Stretch", akRatios(), 1);
        f (diveRes,  "Filter Dive Res", 0.0f, 1.0f, 0.7f, pct);
        f (dropRate, "S950 Drop Rate", 4000.0f, 15600.0f, 7500.0f, AudioParameterFloatAttributes().withLabel ("Hz"));
        f (stretchCycle, "Slice Stretch Cycle", 2.0f, 60.0f, 9.5f, AudioParameterFloatAttributes().withLabel ("ms"));

        for (int i = 0; i < kFxPads; ++i)
            b (padParamId (i).toRawUTF8(), fxPadName (i), false);

        return l;
    }
}
