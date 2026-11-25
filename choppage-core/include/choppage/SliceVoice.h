#pragma once
/*
    SliceVoice — plays one slice of a break, the way the SLICE controls say.

      * rate       speed through the source (tempo fit x pitch), source/output rate included
      * reverse    reads the region backwards
      * stutter    the first 1/n of the slice, n times
      * stretch    Akai-style cyclic stretch: the play head moves at rate/stretch,
                   each CYCLE of output restarts a grain at the play head (0.5 ms splice)
      * trans      0..0.5 shortens the decay, 0.5..1 lifts the tail
      * gate       stops (with a fade) after this many output samples
      * xfade      fade in / fade out length
    No allocation; holds the content reference so the buffer stays alive while it plays.
*/
#include "choppage/Model.h"

namespace bc
{
    class SliceVoice
    {
    public:
        struct Start
        {
            ContentPtr content;
            Range region;
            double rate = 1.0;         // source samples per output sample
            bool   reverse = false;
            int    stutter = 0;
            double stretch = 1.0;
            int    cycleSamples = 420; // output samples per stretch grain
            float  trans = 0.5f;
            int    gateSamples = 1 << 30;
            int    fadeSamples = 88;
            float  level = 1.0f;
        };

        void prepare (double sr) noexcept { sampleRate = sr; }
        void start (const Start&) noexcept;
        void release (int fadeSamples) noexcept;
        void kill() noexcept { active = false; content = nullptr; }
        bool isActive() const noexcept { return active; }

        /** Adds into out[0..1]. */
        void render (float* const* out, int numChannels, int startSample, int numSamples) noexcept;

    private:
        float read (int ch, double pos) const noexcept;

        double sampleRate = 44100.0;
        ContentPtr content;
        bool active = false;
        Start s;

        // Position inside the stutter window, in source samples from the region start (or end, reversed).
        double head = 0.0;         // play head (stretch moves it at rate/stretch)
        double grain = 0.0;        // grain read position
        double prevGrain = 0.0;    // the grain fading out at a splice
        int    grainAge = 0;
        int    spliceFade = 22;
        double windowLen = 0.0;    // source samples in one stutter repeat
        int    repeat = 0, repeats = 1;
        int    age = 0;            // output samples since start
        int    releaseLeft = -1, releaseLen = 1;
        float  envDecay = 1.0f, envCoef = 1.0f, tailLift = 0.0f;
    };
}
