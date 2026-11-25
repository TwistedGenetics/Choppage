#pragma once
/*
    The four LCD tabs:
      SAMPLE  waveform + slice badges, then the SLICE / ALL table
      SEQ     PATTERN / ORIGINAL, bars, pages, the KICK..OPEN + FX lanes, memories
      FX      post-FX detail and every pad's setting
      SET     tempo, trigger, sound character, slicing, generate, MIDI
    Each is transparent and drawn on the LCD glass by the editor.
*/
#include "UI/Widgets.h"
#include "PluginProcessor.h"
#include "UI/ParamIds.h"

namespace ui
{
    // ---- A page of LCD rows bound to parameters ----------------------------------------------
    class ParamPage : public juce::Component
    {
    public:
        explicit ParamPage (ChoppageProcessor&);

        void addRow (const juce::String& group, const juce::String& title);
        void addText (const juce::String& t, int width = 0);
        void addValue (const char* paramId, const juce::String& caption, int width = 150, float fineStep = 0.01f);
        void addChoice (const char* paramId, juce::StringArray labels = {});
        void addBool (const char* paramId, const juce::String& on, const juce::String& off);
        void addAction (const juce::String& text, std::function<void()> fn, std::function<bool()> isOn = nullptr, int width = 0);
        void addRule();    // a divider line above the next row

        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh();

        static constexpr float kFont = 25.0f;
        int rowHeight = 38;
        int titleW = 170;    // 0: no title column

    protected:
        ChoppageProcessor& proc;
        Param param (const char* id) const { return { proc.apvts.getParameter (id) }; }

    private:
        struct Item { std::unique_ptr<LcdCell> cell; int width; };
        struct Row { juce::String group, title; std::vector<Item> items; bool ruleAbove = false; };
        std::vector<Row> rows;
        bool nextRule = false;
        static int textWidth (const juce::String& t) { return (int) (t.length() * kFont * 0.62f) + 18; }
    };

    // ---- SAMPLE -------------------------------------------------------------------------------------
    class WaveView final : public juce::Component
    {
    public:
        explicit WaveView (ChoppageProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        static constexpr int badgeRowH = 34;
    private:
        int xForSample (int s) const;
        int sampleForX (int x) const;
        int sliceAtX (int x) const;
        int markerNear (int x) const;
        void popup (int slice, int marker);
        ChoppageProcessor& proc;
        int dragMarker = -1, dragX = -1;
    };

    class SampleTab final : public juce::Component
    {
    public:
        explicit SampleTab (ChoppageProcessor&);
        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh();
    private:
        ChoppageProcessor& proc;
        WaveView wave;
        LcdCell sliceRow, allRow;
        std::vector<std::unique_ptr<LcdCell>> headers, cells;
    };

    // ---- SEQ ------------------------------------------------------------------------------------------
    class SeqGrid final : public juce::Component
    {
    public:
        explicit SeqGrid (ChoppageProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        int page = 0;
        static constexpr int laneW = 112;
        static constexpr int kLanes = 6;   // KICK SNARE GHOST HAT OPEN FX
    private:
        int visibleSteps() const;
        juce::Rectangle<int> cell (int lane, int column) const;
        ChoppageProcessor& proc;
    };

    class SeqTab final : public juce::Component
    {
    public:
        explicit SeqTab (ChoppageProcessor&);
        void resized() override;
        void refresh();
    private:
        ChoppageProcessor& proc;
        SeqGrid grid;
        std::vector<std::unique_ptr<LcdCell>> tools, memories;
        LcdCell memLabel, help;
    };

    // ---- DISK: the break browser, sorted by BPM -------------------------------------------------------
    class DiskTab final : public juce::Component
    {
    public:
        explicit DiskTab (ChoppageProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void refresh();
        std::function<void()> onChooseFolder;
    private:
        int visibleRows() const;
        juce::Rectangle<int> listArea() const;
        ChoppageProcessor& proc;
        std::vector<bc::Session::BrowserEntry> entries;
        uint32_t seen = 0;
        int scroll = 0;
        static constexpr int rowH = 34;
        LcdCell folderCell, rescanCell, upCell, downCell, pathCell;
    };

    // ---- FX / SET -----------------------------------------------------------------------------------
    class FxTab final : public ParamPage { public: explicit FxTab (ChoppageProcessor&); };
    class SetTab final : public ParamPage { public: explicit SetTab (ChoppageProcessor&); };
}
