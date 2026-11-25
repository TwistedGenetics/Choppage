#pragma once
/*
    Params — every host-automatable control. IDs are stable: never rename one,
    projects store them.
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/Model.h"

namespace bc::params
{
    // Sound
    inline constexpr const char* sampleRate = "srate";
    inline constexpr const char* character  = "character";
    inline constexpr const char* volume     = "volume";
    // Post FX knobs + FX tab detail
    inline constexpr const char* filter     = "filter";
    inline constexpr const char* filterType = "filterType";
    inline constexpr const char* filterRes  = "filterRes";
    inline constexpr const char* dist       = "dist";
    inline constexpr const char* distTone   = "distTone";
    inline constexpr const char* crush      = "crush";
    inline constexpr const char* reverb     = "reverb";
    inline constexpr const char* revDecay   = "revDecay";
    inline constexpr const char* revDamp    = "revDamp";
    inline constexpr const char* revWidth   = "revWidth";
    inline constexpr const char* revPre     = "revPre";
    inline constexpr const char* echoMix    = "echoMix";
    inline constexpr const char* echoFb     = "echoFb";
    inline constexpr const char* echoTime   = "echoTime";
    inline constexpr const char* echoMono   = "echoMono";
    // Generate
    inline constexpr const char* patAmount  = "patAmount";
    inline constexpr const char* fxAmount   = "fxAmount";
    inline constexpr const char* style      = "style";
    inline constexpr const char* swing      = "swing";
    inline constexpr const char* layers     = "layers";
    // Tempo / trigger
    inline constexpr const char* speed      = "speed";
    inline constexpr const char* fit        = "fit";
    inline constexpr const char* fitMode    = "fitMode";
    inline constexpr const char* trigger    = "trigger";
    inline constexpr const char* breakMode  = "breakMode";
    inline constexpr const char* gateMode   = "gateMode";
    inline constexpr const char* velSlice   = "velSlice";
    inline constexpr const char* tempoSrc   = "tempoSrc";
    // FX pads
    inline constexpr const char* padQuant   = "padQuant";
    inline constexpr const char* stutRate   = "stutRate";
    inline constexpr const char* rollRate   = "rollRate";
    inline constexpr const char* tapeMs     = "tapeMs";
    inline constexpr const char* akCycle    = "akCycle";
    inline constexpr const char* akRatio    = "akRatio";
    inline constexpr const char* diveRes    = "diveRes";
    inline constexpr const char* dropRate   = "dropRate";
    inline constexpr const char* stretchCycle = "stretchCycle";

    inline juce::String padParamId (int pad) { return "pad" + juce::String (pad + 1); }

    // Choice lists
    inline juce::StringArray noteRates()   { return { "1/8", "1/16", "1/32", "1/64" }; }
    inline int noteRateDivisor (int idx)   { static const int d[] = { 8, 16, 32, 64 }; return d[juce::jlimit (0, 3, idx)]; }
    inline juce::StringArray echoTimes()   { return { "1/16", "1/8", "1/8.", "1/4", "1/4." }; }
    inline double echoBeats (int idx)      { static const double b[] = { 0.25, 0.5, 0.75, 1.0, 1.5 }; return b[juce::jlimit (0, 4, idx)]; }
    inline juce::StringArray speeds()      { return { "0.5X", "1X", "2X" }; }
    inline double speedFactor (int idx)    { static const double s[] = { 0.5, 1.0, 2.0 }; return s[juce::jlimit (0, 2, idx)]; }
    inline juce::StringArray akRatios()    { return { "1.5X", "2X", "3X", "4X" }; }
    inline double akRatioValue (int idx)   { static const double r[] = { 1.5, 2.0, 3.0, 4.0 }; return r[juce::jlimit (0, 3, idx)]; }
    inline juce::StringArray characterNames()
    {
        juce::StringArray a;
        for (int i = 0; i < kNumCharacters; ++i) a.add (bc::character (i).name);
        return a;
    }

    inline const char* fxPadName (int i)
    {
        static const char* n[kFxPads] = { "STUTTER", "ROLL", "REVERSE", "TAPE STOP", "HALF-TIME", "AKAIZE", "FILT DIVE", "S950 DROP" };
        return n[juce::jlimit (0, kFxPads - 1, i)];
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
