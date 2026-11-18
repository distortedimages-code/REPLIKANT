// The small pieces of the window: cards with a title band, and the parameter rows that replace knobs
// (a name, a value, a little pixel gauge; drag up/down, scroll, click through choices, double-click to type).

#pragma once

#include <JuceHeader.h>
#include "Pixel.h"
#include "Text.h"
#include "../engine/Player.h"

#include <limits>

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

    // a card: dark panel, its frame, a title strip (its title in light capitals after a small mark of its colour)
    inline juce::Rectangle<int> drawCard (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title,
                                          juce::Colour mark = px::accent)
    {
        g.setColour (px::panel);
        g.fillRect (r);
        px::frameRect (g, r, px::frame, 2);
        if (title.isNotEmpty())
        {
            auto b = r.removeFromTop (24).reduced (2, 2);
            g.setColour (px::cell);
            g.fillRect (b);
            g.setColour (mark);
            g.fillRect (b.getX() + 8, b.getCentreY() - 3, 6, 6);
            px::drawText (g, upper (title), b.withTrimmedLeft (22).withTrimmedRight (8), juce::Justification::centredLeft,
                          px::label (10.0f, true), px::white);
        }
        return r.reduced (10, 6);
    }

    //==========================================================================
    // What the window is arming: a modulator (its routes are set by turning the rows, as on IMPAKT and the Torso),
    // or a scene (its locks are set the same way); one at a time
    struct Arming
    {
        Player* player = nullptr;
        int mod = -1, scene = -1;
        std::function<void()> changed;
        bool any() const                     { return mod >= 0 || scene >= 0; }
    };

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
        std::function<bool()> shown;              // left out of the layout when false
        std::function<void()> onChange;
        std::function<void()> onGestureStart, onGestureEnd;   // (the host records automation between them)
        // the curve of the row's travel (else linear, or log): 0..1 to the value and back
        std::function<double (double)> toNormFn, fromNormFn;
        juce::Colour accent = px::white;

        // a setting of the player's sound: its modulations and its scene locks drawn on it, and armed editing
        int param = -1;
        Arming* arm = nullptr;

        bool isChoice() const                     { return ! choices.isEmpty(); }
        void setChoices (const juce::StringArray& c)   { choices = c; repaint(); }
        bool isOn() const                         { return (locked == nullptr || ! locked()) && (enabled == nullptr || enabled()); }
        bool isShown() const                      { return shown == nullptr || shown(); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            const bool on = isOn();
            const bool hot = on && (isMouseOverOrDragging());
            const int armedMod = armedModHere(), armedScene = armedSceneHere();
            juce::ignoreUnused (armedScene);
            auto valueBox = r.removeFromRight (juce::jmin (176, r.getWidth() / 2 + 20));
            px::drawText (g, upper (label), r.withTrimmedLeft (2), juce::Justification::centredLeft, px::label (10.0f), on ? px::grey : px::faint);
            const int textW = juce::GlyphArrangement::getStringWidthInt (px::label (10.0f), upper (label)) + 10;
            // the modulators and scenes that move it: their signs after the name
            int signsW = 0;
            if (param >= 0 && arm != nullptr && arm->player != nullptr)
            {
                int x = r.getRight() - 8;
                for (int m = snd::numMods - 1; m >= 0; --m)
                    if (std::abs (arm->player->routeAmount (m, param)) > 1.0e-4f)
                    {
                        px::modSign (g, m, { x, r.getCentreY() + 1 }, 8, px::mod[m]);
                        x -= 11;
                        signsW += 11;
                    }
                if (sceneHeld (0) || sceneHeld (1))
                {
                    const juce::String ab = juce::String (sceneHeld (0) ? "A" : "") + (sceneHeld (1) ? "B" : "");
                    px::drawText (g, ab, { x - 18, r.getY(), 22, r.getHeight() }, juce::Justification::centredRight, px::label (9.0f, true), px::violet);
                    signsW += 22;
                }
            }
            // dotted leader
            g.setColour (px::faint);
            for (int x = r.getX() + textW; x < r.getRight() - 6 - signsW; x += 6)
                g.fillRect (x, r.getCentreY() + 4, 2, 2);

            g.setColour (px::cell);
            g.fillRect (valueBox);
            const auto frameColour = armedMod >= 0 ? px::mod[armedMod] : armedScene >= 0 ? px::violet : (hot ? px::grey : px::frame);
            px::frameRect (g, valueBox, frameColour, 2);
            auto inner = valueBox.reduced (8, 2);
            if (isChoice())
            {
                const int c = juce::jlimit (0, choices.size() - 1, shownChoice());
                g.setColour (on ? px::dim : px::faint);
                arrow (g, { inner.getX() + 2, inner.getCentreY() }, -1);
                arrow (g, { inner.getRight() - 3, inner.getCentreY() }, 1);
                px::drawText (g, choices[c], inner.reduced (12, 0), juce::Justification::centred, px::text (22.0f),
                              ! on ? px::faint : armedSceneHolds() ? px::violet : accent);
            }
            else
            {
                const double v = shownValue();
                auto t = fmt ? fmt (v) : juce::String (v);
                auto ink = ! on ? px::faint : armedSceneHolds() ? px::violet : accent;
                if (armedMod >= 0 && arm->player != nullptr)
                {
                    const float a = arm->player->routeAmount (armedMod, param);
                    t = juce::String (a > 0 ? "+" : a < 0 ? "-" : "") + juce::String (juce::roundToInt (std::abs (a) * 100.0f)) + " %";
                    ink = px::mod[armedMod];
                }
                auto barArea = inner.removeFromBottom (5).reduced (2, 0);
                px::drawText (g, t, inner, juce::Justification::centred, px::text (22.0f), ink);
                const float frac = (float) toNorm (v);
                px::bar (g, barArea, 16, juce::roundToInt (frac * 16.0f), on ? (hot ? px::white : px::dim) : px::faint);
                drawModulation (g, barArea, frac);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! isOn())
                return;
            dragged = false;
            if (armedModHere() >= 0)
            {
                startValue = arm->player->routeAmount (arm->mod, param);
                return;
            }
            if (armedSceneHere() >= 0)
            {
                if (isChoice())
                {
                    const int n = choices.size();
                    const int dir = e.mods.isShiftDown() ? -1 : 1;
                    lockScene ((float) (((int) std::lround (sceneValueNorm() * (n - 1)) + dir + n) % n) / (float) juce::jmax (1, n - 1));
                    return;
                }
                startValue = sceneValueNorm();
                return;
            }
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
            if (onGestureStart) onGestureStart();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! isOn())
                return;
            const int d = -e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() / 2;
            if (std::abs (d) < 2 && ! dragged)
                return;
            const double sens = e.mods.isShiftDown() ? 1200.0 : 240.0;
            if (armedModHere() >= 0)
            {
                // the distance turned is the depth (either way)
                dragged = true;
                arm->player->setRoute (arm->mod, param, (float) juce::jlimit (-1.0, 1.0, startValue + d / sens));
                changedArm();
                return;
            }
            if (armedSceneHere() >= 0)
            {
                if (isChoice())
                    return;
                dragged = true;
                lockScene ((float) juce::jlimit (0.0, 1.0, startValue + d / sens));
                return;
            }
            if (isChoice())
                return;
            dragged = true;
            applyValue (fromNorm (juce::jlimit (0.0, 1.0, startValue + d / sens)));
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (armedModHere() >= 0)
            {
                // a touch without turning turns the route over (IMPAKT); a double click takes it away
                if (! dragged && e.getNumberOfClicks() < 2)
                {
                    const float a = arm->player->routeAmount (arm->mod, param);
                    if (std::abs (a) > 1.0e-4f)
                        arm->player->setRoute (arm->mod, param, -a);
                    changedArm();
                }
                return;
            }
            if (armedSceneHere() >= 0 || isChoice())
                return;
            if (onGestureEnd) onGestureEnd();
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            if (! isOn() || (arm != nullptr && arm->any()))
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
            const double next = (log || toNormFn) ? fromNorm (juce::jlimit (0.0, 1.0, toNorm (v) + (dy > 0 ? 0.01 : -0.01)))
                                                  : v + (dy > 0 ? s : -s);
            if (onGestureStart) onGestureStart();
            applyValue (juce::jlimit (lo, hi, next));
            if (onGestureEnd) onGestureEnd();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (! isOn())
                return;
            if (armedModHere() >= 0)
            {
                arm->player->setRoute (arm->mod, param, 0.0f);
                changedArm();
                return;
            }
            if (armedSceneHere() >= 0)
            {
                arm->player->setSceneLock (arm->scene, param, std::numeric_limits<float>::quiet_NaN());
                changedArm();
                return;
            }
            if (isChoice())
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
                    {
                        if (onGestureStart) onGestureStart();
                        applyValue (juce::jlimit (lo, hi, t.getDoubleValue()));
                        if (onGestureEnd) onGestureEnd();
                    }
                }
                juce::MessageManager::callAsync ([ed] { delete ed; });
            };
            ed->onReturnKey = [done] { done (true); };
            ed->onEscapeKey = [done] { done (false); };
            ed->onFocusLost = [done] { done (true); };
        }

        void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { repaint(); }

        // the editor's timer asks: repainted only when what it shows has changed (its value, what moves it, whether
        // it is on), not thirty times a second for nothing
        void refreshIfChanged()
        {
            double sig = isChoice() ? (double) shownChoice() : (get != nullptr || armedSceneHolds() ? shownValue() : 0.0);
            float moved = -1.0f;
            if (param >= 0 && arm != nullptr && arm->player != nullptr)
            {
                moved = arm->player->modulatedNorm (param);
                for (int m = 0; m < snd::numMods; ++m)
                    sig += 1.0e-6 * (m + 1) * (double) arm->player->routeAmount (m, param);
                sig += (sceneHeld (0) ? 1.0e-7 : 0.0) + (sceneHeld (1) ? 2.0e-7 : 0.0);
            }
            const bool on = isOn();
            if (std::abs (sig - lastSig) > 1.0e-12 || std::abs (moved - lastMoved) > 1.0e-4f || on != lastOn)
            {
                lastSig = sig;
                lastMoved = moved;
                lastOn = on;
                repaint();
            }
        }

    private:
        double lastSig = -1.0e300;
        float lastMoved = -2.0f;
        bool lastOn = true;

        int armedModHere() const
        {
            return arm != nullptr && arm->mod >= 0 && param >= 0 && arm->player != nullptr && snd::def (param).modulatable
                       ? arm->mod : -1;
        }
        int armedSceneHere() const
        {
            return arm != nullptr && arm->scene >= 0 && param >= 0 && arm->player != nullptr && snd::sceneLockable (param) ? arm->scene : -1;
        }
        // scene A (0) or B (1) holding this setting
        bool sceneHeld (int side) const
        {
            if (side < 0 || arm == nullptr || arm->player == nullptr || param < 0)
                return false;
            const int sc = (int) arm->player->get (side == 0 ? snd::sceneA : snd::sceneB) - 1;
            return ! std::isnan (arm->player->sceneLock (sc, param));
        }
        bool armedSceneHolds() const
        {
            return armedSceneHere() >= 0 && ! std::isnan (arm->player->sceneLock (arm->scene, param));
        }
        // the value the row shows: the armed scene's lock, or its own
        double shownValue() const
        {
            if (armedSceneHolds())
                return (double) arm->player->sceneLock (arm->scene, param);
            return get();
        }
        int shownChoice() const
        {
            if (armedSceneHolds())
                return (int) std::lround (arm->player->sceneLock (arm->scene, param) - snd::def (param).lo);
            return getChoice();
        }
        float sceneValueNorm() const
        {
            const float l = arm->player->sceneLock (arm->scene, param);
            return snd::toNorm (param, std::isnan (l) ? arm->player->get (param) : l);
        }
        void lockScene (float norm)
        {
            arm->player->setSceneLock (arm->scene, param, snd::fromNorm (param, norm));
            changedArm();
        }
        void changedArm()
        {
            repaint();
            if (arm != nullptr && arm->changed)
                arm->changed();
        }

        // the routes drawn under the gauge (each from the value as far as it goes, in its modulator's colour), and a
        // dot where the scenes and the modulators take it now
        void drawModulation (juce::Graphics& g, juce::Rectangle<int> bar, float frac)
        {
            if (param < 0 || arm == nullptr || arm->player == nullptr)
                return;
            int line = 0;
            for (int m = 0; m < snd::numMods; ++m)
            {
                const float a = arm->player->routeAmount (m, param);
                if (std::abs (a) < 1.0e-4f)
                    continue;
                const float depth = arm->player->get (snd::modParam (m, snd::mAmt));
                const float from = juce::jlimit (0.0f, 1.0f, frac - std::abs (a) * depth), to = juce::jlimit (0.0f, 1.0f, frac + std::abs (a) * depth);
                const int x0 = bar.getX() + juce::roundToInt (from * (float) bar.getWidth());
                const int x1 = bar.getX() + juce::roundToInt (to * (float) bar.getWidth());
                g.setColour (px::mod[m]);
                g.fillRect (x0, bar.getBottom() + 1 + line * 2, juce::jmax (2, x1 - x0), 2);
                ++line;
            }
            const float now = arm->player->modulatedNorm (param);
            if (now >= 0.0f)
            {
                const int x = bar.getX() + juce::roundToInt (now * (float) (bar.getWidth() - 4));
                g.setColour (px::white);
                g.fillRect (x, bar.getY() - 1, 4, bar.getHeight() + 2);
            }
        }

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
            if (toNormFn) return juce::jlimit (0.0, 1.0, toNormFn (v));
            if (hi <= lo) return 0.0;
            if (log && lo > 0) return std::log (v / lo) / std::log (hi / lo);
            return (v - lo) / (hi - lo);
        }

        double fromNorm (double t) const
        {
            if (fromNormFn) return fromNormFn (t);
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
            if (onGestureStart) onGestureStart();
            setChoice (c);
            if (onGestureEnd) onGestureEnd();
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
