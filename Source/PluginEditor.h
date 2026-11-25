#pragma once
#include "PluginProcessor.h"
class ChoppageEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int designW = 1400, designH = 780;
    explicit ChoppageEditor (ChoppageProcessor& p) : juce::AudioProcessorEditor (p) { setSize (designW, designH); }
    void showTab (int) {}
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
};
