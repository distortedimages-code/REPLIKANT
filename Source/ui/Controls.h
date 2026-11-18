// The controls on the eight encoders: a setting of the sound (its parameter, its modulations, its scene locks) or
// anything else (a setting of the capture, an option), and the encoder that turns it.
//
// The mods and scenes keys arm editing: an armed modulator makes the encoders set its routes (the band of ticks in
// its colour: how far it moves the setting); an armed scene makes them set what the scene holds.

#pragma once

#include <JuceHeader.h>
#include "Look.h"
#include "Text.h"
#include "../plugin/PluginProcessor.h"

namespace te
{
    struct Arming
    {
        int mod = -1, scene = -1;
        Player* player = nullptr;
        std::function<void()> changed;
        bool any() const                 { return mod >= 0 || scene >= 0; }
    };

    struct Ctl
    {
        juce::String name;
        int param = -1;                                  // a setting of the sound (snd), or -1
        std::function<float()> getNorm;                  // where it is, 0..1
        std::function<void (float)> setNorm;
        std::function<juce::String()> text;
        std::function<bool()> shown;                     // left out when false
        std::function<bool()> enabled;                   // greyed out when false
        float defaultNorm = 0.0f;
        int steps = 0;                                   // > 1: that many values (a choice, a whole number)
        bool bipolar = false;
        std::function<void()> begin, end;                // the gesture (a host records automation between them)
        std::function<void()> changed;                   // after it was set
        juce::String tip;

        bool isShown() const             { return ! shown || shown(); }
        bool isEnabled() const           { return ! enabled || enabled(); }
    };

    // a setting of the sound
    Ctl soundCtl (ReplikantProcessor&, int id, const char* label = nullptr, std::function<bool()> shown = nullptr);
    // a choice of anything
    Ctl choiceCtl (const juce::String& name, juce::StringArray choices, std::function<int()> get, std::function<void (int)> set);
    // a number of anything (shown by `fmt`), `step` > 0 makes it stepped
    Ctl numberCtl (const juce::String& name, double lo, double hi, double step, std::function<double()> get,
                   std::function<void (double)> set, std::function<juce::String (double)> fmt, bool logarithmic = false);

    class Encoder : public juce::Component,
                    public juce::SettableTooltipClient
    {
    public:
        Encoder (int index, Arming&);
        void setCtl (const Ctl*);
        const Ctl* getCtl() const        { return ctl; }
        bool focused = false;
        std::function<void (const Ctl&)> onTouch;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void refreshIfChanged();

        // what it shows for its control (the screen's big readout too)
        static juce::String valueText (const Ctl&, const Arming&);

    private:
        int armedMod() const;
        int armedScene() const;
        float shownNorm() const;
        void nudge (float deltaNorm, bool fine);
        void setFrom (float norm);

        const int index;
        Arming& arm;
        const Ctl* ctl = nullptr;
        float dragStart = 0.0f, stepAccum = 0.0f;
        bool dragged = false, inGesture = false;
        juce::String lastSig;
    };
}
