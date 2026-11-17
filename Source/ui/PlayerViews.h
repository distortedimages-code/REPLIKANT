// What the player's pages show on the right, and its strip at the bottom:
//
//   FilterView   the filter's response as it is now (its cutoff moved by the envelope, the scenes, the modulators),
//                the drive's curve beside it
//   ChainView    the chain, block after block (a click turns one on or off), and SPECTRE's oscillators
//   ModView      the four modulators over the last seconds (IMPAKT's lanes), the routes of the one chosen (their depth
//                dragged, x to take one away), its steps drawn with the mouse (PAS), its loop drawn again (HASARD)
//   SceneView    the sixteen scenes (click: arm it to lock settings in it; right click: A, B, empty, copy), what A
//                and B hold, setting by setting
//   PlayerStrip  the bottom bar's: M1..M4 (click: arm), the crossfader between A and B, the clock

#pragma once

#include <JuceHeader.h>
#include "../plugin/PluginProcessor.h"
#include "Widgets.h"

#include <array>
#include <deque>

namespace ui
{
    // a parameter of the sound, set as the host should hear it (one gesture)
    inline void setSound (ReplikantProcessor& p, int id, float value)
    {
        if (auto* param = p.params.getParameter (snd::def (id).id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (snd::legal (id, value)));
            param->endChangeGesture();
        }
    }

    inline juce::String soundName (int id)                { return upper (juce::String::fromUTF8 (snd::longName (id).c_str())); }
    inline juce::String soundText (int id, float v)       { return juce::String::fromUTF8 (snd::text (id, v).c_str()); }

    //==========================================================================
    class FilterView : public juce::Component
    {
    public:
        explicit FilterView (ReplikantProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void refresh()                                    { repaint(); }
    private:
        ReplikantProcessor& proc;
    };

    //==========================================================================
    class ChainView : public juce::Component
    {
    public:
        explicit ChainView (ReplikantProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void refresh()                                    { repaint(); }
        bool showSpectrum = true;
    private:
        ReplikantProcessor& proc;
        std::array<juce::Rectangle<int>, 6> blocks;
    };

    //==========================================================================
    class ModView : public juce::Component
    {
    public:
        ModView (ReplikantProcessor& p, Arming& a) : proc (p), arm (a) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void refresh();                                   // 30 times a second: the lanes move on
        int selected = 0;
        std::function<void (int)> onSelect;
    private:
        void layout();
        ReplikantProcessor& proc;
        Arming& arm;
        std::array<std::deque<float>, snd::numMods> history;
        juce::Rectangle<int> lanes, routesArea, stepsArea, redraw;
        struct RouteRow { int dest; juce::Rectangle<int> bar, cross; };
        std::vector<RouteRow> rows;
        int dragRoute = -1;
        float dragStart = 0.0f;
    };

    //==========================================================================
    class SceneView : public juce::Component
    {
    public:
        SceneView (ReplikantProcessor& p, Arming& a) : proc (p), arm (a) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void refresh()                                    { repaint(); }
    private:
        juce::Rectangle<int> padArea() const;
        juce::Rectangle<int> padRect (int i) const;
        ReplikantProcessor& proc;
        Arming& arm;
        juce::var clipboard;
    };

    //==========================================================================
    class PlayerStrip : public juce::Component
    {
    public:
        PlayerStrip (ReplikantProcessor& p, Arming& a) : proc (p), arm (a) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void resized() override;
        bool clockOnly = false;                   // only the clock (beside the meter, in both modes)
    private:
        ReplikantProcessor& proc;
        Arming& arm;
        std::array<juce::Rectangle<int>, snd::numMods> modButtons;
        juce::Rectangle<int> fader, sceneA, sceneB, clockButton, clockText;
        bool draggingFader = false;
    };
}
