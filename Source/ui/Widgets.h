// The small pieces of the window: cards with a title band, and the parameter rows that replace knobs
// (a name, a value, a little pixel gauge; drag up/down, scroll, click through choices, double-click to type).

#pragma once

#include <JuceHeader.h>
#include "Pixel.h"
#include "Text.h"

namespace ui
{
    // laid out once at this size, on a 4-pixel grid, and zoomed as a whole
    constexpr int designWidth = 1360, designHeight = 848;
    constexpr float zoomSteps[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

    inline void styleBig (juce::TextButton& b, bool primary = false)
    {
        b.getProperties().set ("primary", primary);
        if (primary)
            b.setColour (juce::TextButton::textColourOffId, px::black);
        else
            b.removeColour (juce::TextButton::textColourOffId);
        b.repaint();
    }

    // a card: dark panel, grey frame, its title in black capitals on a white band
    inline juce::Rectangle<int> drawCard (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title,
                                          juce::Colour band = px::white)
    {
        g.setColour (px::panel);
        g.fillRect (r);
        px::frameRect (g, r, px::frame, 2);
        if (title.isNotEmpty())
        {
            auto b = r.removeFromTop (24).reduced (2, 2);
            g.setColour (band);
            g.fillRect (b);
            px::drawText (g, upper (title), b.reduced (10, 0), juce::Justification::centredLeft, px::label (11.0f, true), px::black);
        }
        return r.reduced (10, 6);
    }

    //==========================================================================
    class ParamRow : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        // a number
        ParamRow (juce::String name, double minimum, double maximum, double stepSize,
                  std::function<double()> getter, std::function<void (double)> setter,
                  std::function<juce::String (double)> format, bool logarithmic = false)
            : label (std::move (name)), lo (minimum), hi (maximum), step (stepSize), log (logarithmic),
              get (std::move (getter)), set (std::move (setter)), fmt (std::move (format)) {}

        // a choice
        ParamRow (juce::String name, juce::StringArray options, std::function<int()> getter, std::function<void (int)> setter)
            : label (std::move (name)), choices (std::move (options)), getChoice (std::move (getter)), setChoice (std::move (setter)) {}

        std::function<bool()> enabled;            // greyed out when false
        std::function<bool()> locked;             // greyed out when true, whatever `enabled` says (a session runs)
        std::function<void()> onChange;
        juce::Colour accent = px::white;

        bool isChoice() const                     { return ! choices.isEmpty(); }
        void setChoices (const juce::StringArray& c)   { choices = c; repaint(); }
        bool isOn() const                         { return (locked == nullptr || ! locked()) && (enabled == nullptr || enabled()); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            const bool on = isOn();
            const bool hot = on && (isMouseOverOrDragging());
            const auto ink = on ? px::white : px::faint;
            auto valueBox = r.removeFromRight (juce::jmin (176, r.getWidth() / 2 + 20));
            px::drawText (g, upper (label), r.withTrimmedLeft (2), juce::Justification::centredLeft, px::label (10.0f), on ? px::grey : px::faint);
            // dotted leader
            g.setColour (px::faint);
            const int textW = juce::GlyphArrangement::getStringWidthInt (px::label (10.0f), upper (label)) + 10;
            for (int x = r.getX() + textW; x < r.getRight() - 6; x += 6)
                g.fillRect (x, r.getCentreY() + 4, 2, 2);

            g.setColour (px::cell);
            g.fillRect (valueBox);
            px::frameRect (g, valueBox, hot ? px::grey : px::frame, 2);
            auto inner = valueBox.reduced (8, 2);
            if (isChoice())
            {
                const int c = juce::jlimit (0, choices.size() - 1, getChoice());
                // small arrows each side: click left part = previous, right part = next
                g.setColour (on ? px::dim : px::faint);
                arrow (g, { inner.getX() + 2, inner.getCentreY() }, -1);
                arrow (g, { inner.getRight() - 3, inner.getCentreY() }, 1);
                px::drawText (g, choices[c], inner.reduced (12, 0), juce::Justification::centred, px::text (22.0f), on ? accent : px::faint);
            }
            else
            {
                const double v = get();
                const auto t = fmt ? fmt (v) : juce::String (v);
                auto barArea = inner.removeFromBottom (5).reduced (2, 0);
                px::drawText (g, t, inner, juce::Justification::centred, px::text (22.0f), on ? accent : px::faint);
                const float frac = (float) toNorm (v);
                px::bar (g, barArea, 16, juce::roundToInt (frac * 16.0f), on ? (hot ? px::white : px::dim) : px::faint);
            }
            juce::ignoreUnused (ink);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! isOn())
                return;
            if (isChoice())
            {
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu m;
                    for (int i = 0; i < choices.size(); ++i)
                        m.addItem (i + 1, choices[i], true, i == getChoice());
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
                    {
                        if (r > 0) apply (r - 1);
                    });
                    return;
                }
                const auto box = valueArea();
                const int dir = (e.x < box.getCentreX() - box.getWidth() / 4 || e.mods.isShiftDown()) ? -1 : 1;
                const int n = choices.size();
                apply ((getChoice() + dir + n) % n);
                return;
            }
            startValue = toNorm (get());
            dragged = false;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! isOn() || isChoice())
                return;
            const int d = -e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() / 2;
            if (std::abs (d) < 2 && ! dragged)
                return;
            dragged = true;
            const double sens = e.mods.isShiftDown() ? 1200.0 : 240.0;
            applyValue (fromNorm (juce::jlimit (0.0, 1.0, startValue + d / sens)));
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            if (! isOn())
                return;
            const float dy = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
            if (std::abs (dy) < 1.0e-4f)
                return;
            if (isChoice())
            {
                const int n = choices.size();
                apply ((getChoice() + (dy > 0 ? 1 : -1) + n) % n);
                return;
            }
            // one step per notch (at least)
            const double v = get();
            const double s = step > 0 ? step : (hi - lo) / 100.0;
            const double next = log ? fromNorm (juce::jlimit (0.0, 1.0, toNorm (v) + (dy > 0 ? 0.01 : -0.01)))
                                    : v + (dy > 0 ? s : -s);
            applyValue (juce::jlimit (lo, hi, next));
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (! isOn() || isChoice())
                return;
            auto* ed = new juce::TextEditor();
            ed->setFont (px::text (22.0f));
            ed->setJustification (juce::Justification::centred);
            ed->setText (juce::String (get()), false);
            ed->selectAll();
            ed->setBounds (valueArea());
            addAndMakeVisible (ed);
            ed->grabKeyboardFocus();
            auto done = [this, ed] (bool keep)
            {
                if (keep)
                {
                    const auto t = ed->getText().retainCharacters ("0123456789.,-").replaceCharacter (',', '.');
                    if (t.isNotEmpty())
                        applyValue (juce::jlimit (lo, hi, t.getDoubleValue()));
                }
                juce::MessageManager::callAsync ([ed] { delete ed; });
            };
            ed->onReturnKey = [done] { done (true); };
            ed->onEscapeKey = [done] { done (false); };
            ed->onFocusLost = [done] { done (true); };
        }

        void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { repaint(); }

    private:
        juce::Rectangle<int> valueArea() const
        {
            auto r = getLocalBounds();
            return r.removeFromRight (juce::jmin (176, r.getWidth() / 2 + 20));
        }

        static void arrow (juce::Graphics& g, juce::Point<int> p, int dir)
        {
            for (int k = 0; k < 3; ++k)
                g.fillRect (p.x - dir * k * 2 - (dir > 0 ? 2 : 0), p.y - k * 2, 2, 2 + k * 4);
        }

        double toNorm (double v) const
        {
            if (hi <= lo) return 0.0;
            if (log && lo > 0) return std::log (v / lo) / std::log (hi / lo);
            return (v - lo) / (hi - lo);
        }

        double fromNorm (double t) const
        {
            double v = (log && lo > 0) ? lo * std::pow (hi / lo, t) : lo + t * (hi - lo);
            if (step > 0)
                v = lo + std::round ((v - lo) / step) * step;
            return juce::jlimit (lo, hi, v);
        }

        void applyValue (double v)
        {
            if (std::abs (v - get()) < 1.0e-12)
                return;
            set (v);
            repaint();
            if (onChange) onChange();
        }

        void apply (int c)
        {
            if (c == getChoice())
                return;
            setChoice (c);
            repaint();
            if (onChange) onChange();
        }

        juce::String label;
        double lo = 0, hi = 1, step = 0;
        bool log = false;
        std::function<double()> get;
        std::function<void (double)> set;
        std::function<juce::String (double)> fmt;
        juce::StringArray choices;
        std::function<int()> getChoice;
        std::function<void (int)> setChoice;
        double startValue = 0.0;
        bool dragged = false;
    };

    // helpers for row texts
    inline juce::String ms (double seconds)
    {
        if (seconds < 1.0)
            return juce::String (juce::roundToInt (seconds * 1000.0)) + " MS";
        return juce::String (seconds, seconds < 10.0 ? 2 : 1) + " S";
    }

    inline juce::String duration (double seconds)
    {
        const int s = juce::roundToInt (seconds);
        if (s < 60)
            return juce::String (s) + " S";
        if (s < 3600)
            return juce::String (s / 60) + " MIN " + juce::String (s % 60).paddedLeft ('0', 2);
        return juce::String (s / 3600) + " H " + juce::String ((s / 60) % 60).paddedLeft ('0', 2);
    }
}
