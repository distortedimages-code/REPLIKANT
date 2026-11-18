// REPLIK8 — DRUM (0.4) in the window: the screen (the eight tracks on the page of steps shown, the playheads, the
// track chosen and its sound), the strip under it (the tracks, the sixteen step keys, the pages, PTN: the patterns and
// their chain), and the controls of the encoders (a track's setting, or its p-locks on the steps lifted).

#pragma once

#include <JuceHeader.h>
#include "Controls.h"
#include "Look.h"
#include "Text.h"
#include "../plugin/PluginProcessor.h"

namespace te
{
    // what the drum's part of the window is editing: shared by the screen, the strip and the encoders
    struct DrumEdit
    {
        int page = 0;                            // the 16 steps shown (0 .. 3)
        std::vector<int> lifted;                 // the steps lifted for p-locks (the encoders write on them)
        bool patterns = false;                   // PTN: the strip shows the patterns
        int bank = 0;                            // PTN: the bank shown (A .. H)
        bool chaining = false;                   // PTN: the patterns touched make the chain
        std::vector<int> chainDraft;
        int pending = 0;                         // PTN: 1 paste, 2 clear on the next pattern touched
        std::unique_ptr<drum::Pattern> clipboard;
        bool isLifted (int s) const              { return std::find (lifted.begin(), lifted.end(), s) != lifted.end(); }
    };

    juce::String patternName (int i);           // A01 .. H16
    juce::String speedName (int i);             // a track's speed: 1/8X .. 2X

    // a setting of the track shown (or its lock on the steps lifted)
    Ctl drumCtl (ReplikantProcessor&, DrumEdit&, int param, const char* label = nullptr, std::function<bool()> shown = nullptr);

    struct DrumScreen
    {
        const drum::Sound* cachedSound = nullptr;
        juce::Path wave;
        float cachedW = 0.0f, cachedH = 0.0f;
        void paint (juce::Graphics&, juce::Rectangle<float>, ReplikantProcessor&, DrumEdit&);
        // a click on the grid: a track's name chooses it, a step of a track turns its trig on or off
        bool click (juce::Point<float>, ReplikantProcessor&, DrumEdit&);
        juce::Rectangle<float> grid;             // (where the grid was drawn)
        float labelW = 0.0f;
    };

    class DrumStrip : public juce::Component
    {
    public:
        DrumStrip (ReplikantProcessor&, DrumEdit&, std::function<void (const juce::String&, bool)> message, std::function<void()> changed);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ReplikantProcessor& proc;
        DrumEdit& edit;
        std::function<void (const juce::String&, bool)> message;
        std::function<void()> changed;
        juce::Rectangle<float> tracks[drum::numTracks], steps[drum::pageSteps], pages[4], keys[5], hint;
        int downStep = -1, downTrack = -1, startRat = 1;
        float startY = 0.0f;
        bool dragged = false;
        double lastPaint = 0.0;
        void layout();
        drum::Track& track();
    };
}
