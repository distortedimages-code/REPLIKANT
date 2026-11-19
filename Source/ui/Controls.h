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
        float origin = 0.5f;                             // (bipolar) where its zero is (0..1): its lit arc starts there
        bool toggle = false;                             // on or off: drawn as a switch, not a knob
        // how it looks on the desk (chosen from what it is by dress(); a page can choose another):
        //   knob     a knob with its printed scale (ticks, a few values written round it), its value in a small LCD
        //   toggle   a bat-handle switch, ON above and OFF below, a lamp by the side it's on
        //   twoWay   the same switch for two named positions (the first above)
        //   buttons  a push button per position (a few, short names), a lamp above the one chosen
        //   ledList  the positions written one under the other, a lamp by the one chosen
        //   display  an orange digital display, arrows each side (a click in the middle: the list)
        //   tuning   (0.4.12) a big tuning knob: a fluted skirt, a coloured cap, its dial printed round it (0..9, a full
        //            turn), its value in the LCD; `spin` says how far round it is (in turns)
        enum class Look { knob, toggle, twoWay, buttons, ledList, display, tuning };
        Look look = Look::knob;
        juce::StringArray options;                       // its positions' names (a choice)
        struct Mark { float at; juce::String label; };   // a value printed round a knob, where it sits (0..1)
        std::vector<Mark> marks;
        int ticks = 0;                                   // the knob's graduations, evenly spread (0: none)
        // a choice's positions printed round its knob as pictures (a wave's shape...) instead of their names
        std::function<void (juce::Graphics&, int option, juce::Rectangle<float>, juce::Colour)> drawOption;
        // (0.4.7) where a mod armed goes when this is turned, if not to `param` (a DRUM track's setting)
        std::function<int()> modDest;
        int routeDest() const                            { return modDest ? modDest() : param; }
        bool sepBefore = false;                          // a line before it on the desk (a new group)
        float quantum = 0.0f;                            // > 0: kept on whole multiples of it (in its own units)
        float dragPixels = 240.0f;                       // the drag that sweeps its whole range
        std::function<float()> spin;                     // (tuning) how far round the knob is, in turns
        // (0.4.14, tuning) its value in its own units, read and set: the knob then turns by the pixel, slowly (tuneRate
        // units a pixel), faster the faster it's dragged (up to 400 times), and keeps what's below the setter's rounding
        std::function<double()> tuneGet;
        std::function<void (double)> tuneSet;
        double tuneRate = 0.025;
        std::function<void()> begin, end;                // the gesture (a host records automation between them)
        std::function<void()> changed;                   // after it was set
        juce::String tip;

        bool isShown() const             { return ! shown || shown(); }
        bool isEnabled() const           { return ! enabled || enabled(); }
    };

    // its look, its positions, its scale, from what it is
    void dress (Ctl&);
    // a look chosen by the page; a line before it
    inline Ctl as (Ctl c, Ctl::Look l)           { c.look = l; return c; }
    inline Ctl sep (Ctl c)                         { c.sepBefore = true; return c; }

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

        // where its positions were drawn (a click picks one); the display's arrows and middle
        std::vector<juce::Rectangle<float>> hits;
        juce::Rectangle<float> prevHit, nextHit, menuHit;
        void pick (int option);
        void paintScale (juce::Graphics&, juce::Point<float> c, float radius, bool on) const;

        const int index;
        Arming& arm;
        const Ctl* ctl = nullptr;
        float dragStart = 0.0f, stepAccum = 0.0f;
        bool dragged = false, inGesture = false;
        // (tuning) the value as dragged (finer than the setter keeps), the last pixel, when, how fast
        double tuneValue = 0.0, lastDragTime = 0.0;
        float lastDragY = 0.0f, dragSpeed = 0.0f;
        bool tuning() const;
        juce::String lastSig;
    };
}
