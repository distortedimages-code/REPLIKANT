#include "Controls.h"

namespace te
{
Ctl soundCtl (ReplikantProcessor& p, int id, const char* label, std::function<bool()> shown)
{
    const auto& d = snd::def (id);
    auto* param = p.params.getParameter (d.id);
    jassert (param != nullptr);
    Ctl c;
    c.name = juce::String::fromUTF8 (label != nullptr ? label : d.name);
    c.param = id;
    c.getNorm = [param] { return param->getValue(); };
    c.setNorm = [param] (float t) { param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, t)); };
    Player* pl = &p.player;
    c.text = [pl, id] { return juce::String::fromUTF8 (snd::text (id, pl->get (id)).c_str()); };
    c.shown = std::move (shown);
    c.defaultNorm = param->getDefaultValue();
    if (d.kind == snd::Kind::choice || d.kind == snd::Kind::toggle || d.kind == snd::Kind::integer)
        c.steps = (int) std::lround (d.hi - d.lo) + 1;
    c.bipolar = d.kind != snd::Kind::choice && d.lo < 0.0f && d.hi > 0.0f;
    c.toggle = d.kind == snd::Kind::toggle;
    c.begin = [param] { param->beginChangeGesture(); };
    c.end = [param] { param->endChangeGesture(); };
    return c;
}

Ctl choiceCtl (const juce::String& name, juce::StringArray choices, std::function<int()> get, std::function<void (int)> set)
{
    Ctl c;
    c.name = name;
    const int n = juce::jmax (1, choices.size());
    c.steps = n;
    c.getNorm = [get, n] { return n > 1 ? (float) juce::jlimit (0, n - 1, get()) / (float) (n - 1) : 0.0f; };
    c.setNorm = [set, n] (float t) { set (juce::jlimit (0, n - 1, (int) std::lround (t * (float) (n - 1)))); };
    c.text = [get, choices] { return choices[juce::jlimit (0, choices.size() - 1, get())]; };
    c.defaultNorm = c.getNorm();
    return c;
}

Ctl numberCtl (const juce::String& name, double lo, double hi, double step, std::function<double()> get,
               std::function<void (double)> set, std::function<juce::String (double)> fmt, bool logarithmic)
{
    Ctl c;
    c.name = name;
    auto toNorm = [lo, hi, logarithmic] (double v)
    {
        v = juce::jlimit (lo, hi, v);
        if (logarithmic && lo > 0.0)
            return (float) (std::log (v / lo) / std::log (hi / lo));
        return (float) ((v - lo) / (hi - lo));
    };
    auto fromNorm = [lo, hi, step, logarithmic] (float t)
    {
        double v = logarithmic && lo > 0.0 ? lo * std::pow (hi / lo, (double) t) : lo + (hi - lo) * (double) t;
        if (step > 0.0)
            v = lo + std::round ((v - lo) / step) * step;
        return juce::jlimit (lo, hi, v);
    };
    c.getNorm = [get, toNorm] { return toNorm (get()); };
    c.setNorm = [set, fromNorm] (float t) { set (fromNorm (juce::jlimit (0.0f, 1.0f, t))); };
    c.text = [get, fmt] { return fmt (get()); };
    if (step >= 1.0 && (hi - lo) / step <= 16.0)
        c.steps = (int) std::lround ((hi - lo) / step) + 1;
    c.bipolar = lo < 0.0 && hi > 0.0;
    c.defaultNorm = c.getNorm();
    return c;
}

//==============================================================================
Encoder::Encoder (int i, Arming& a) : index (i), arm (a)
{
    setRepaintsOnMouseActivity (false);
}

void Encoder::setCtl (const Ctl* c)
{
    if (c == ctl)
        return;
    ctl = c;
    setTooltip (c != nullptr ? c->tip : juce::String());
    lastSig = {};
    repaint();
}

int Encoder::armedMod() const
{
    return ctl != nullptr && arm.mod >= 0 && ctl->param >= 0 && arm.player != nullptr && snd::def (ctl->param).modulatable ? arm.mod : -1;
}

int Encoder::armedScene() const
{
    return ctl != nullptr && arm.scene >= 0 && ctl->param >= 0 && arm.player != nullptr && snd::sceneLockable (ctl->param) ? arm.scene : -1;
}

float Encoder::shownNorm() const
{
    if (ctl == nullptr)
        return 0.0f;
    if (const int sc = armedScene(); sc >= 0)
    {
        const float l = arm.player->sceneLock (sc, ctl->param);
        if (! std::isnan (l))
            return snd::toNorm (ctl->param, l);
    }
    return ctl->getNorm ? ctl->getNorm() : 0.0f;
}

juce::String Encoder::valueText (const Ctl& c, const Arming& arm)
{
    if (c.param >= 0 && arm.player != nullptr)
    {
        if (arm.mod >= 0 && snd::def (c.param).modulatable)
        {
            const float a = arm.player->routeAmount (arm.mod, c.param);
            return "m" + juce::String (arm.mod + 1) + " " + (a > 0 ? "+" : a < 0 ? "-" : "") + juce::String (juce::roundToInt (std::abs (a) * 100.0f)) + " %";
        }
        if (arm.scene >= 0 && snd::sceneLockable (c.param))
        {
            const float l = arm.player->sceneLock (arm.scene, c.param);
            if (! std::isnan (l))
                return juce::String::fromUTF8 (snd::text (c.param, l).c_str());
            return juce::String ("—"_fr);
        }
    }
    return c.text ? c.text() : juce::String();
}

void Encoder::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (ctl == nullptr)
        return;
    if (focused)
    {
        g.setColour (col::deck == col::body ? col::face : col::deck.brighter (0.14f));
        g.fillRoundedRectangle (r.reduced (2.0f), 12.0f);
    }
    const bool on = ctl->isEnabled();
    text (g, ctl->name, r.removeFromTop (24.0f).withTrimmedTop (6.0f), mono (12.0f, 500), on ? col::deckInk2 : col::deckInk2.withAlpha (0.45f), juce::Justification::centred);
    const float radius = juce::jmin (46.0f, r.getWidth() * 0.32f);
    auto knob = r.removeFromTop (radius * 2.0f + 4.0f);
    const float value = shownNorm();
    float modFrom = value, modTo = value;
    juce::Colour modColour = col::ink;
    if (const int m = armedMod(); m >= 0)
    {
        modTo = juce::jlimit (0.0f, 1.0f, value + arm.player->routeAmount (m, ctl->param));
        modColour = col::mod (m);
    }
    else if (ctl->param >= 0 && arm.player != nullptr)
    {
        const float now = arm.player->modulatedNorm (ctl->param);
        if (now >= 0.0f)
        {
            modTo = now;
            for (int k = 0; k < snd::numMods; ++k)
                if (std::abs (arm.player->routeAmount (k, ctl->param)) > 1.0e-4f)
                {
                    modColour = col::mod (k);
                    break;
                }
            if (modColour == col::ink)
                modColour = col::red;              // the scenes' fader or WTF?! moves it
        }
    }
    const bool sceneEdit = armedScene() >= 0;
    if (ctl->toggle)
    {
        // a switch: the slot, its lever to the right when on
        const bool isOn = value > 0.5f;
        auto slot = juce::Rectangle<float> (radius * 1.7f, radius * 0.9f).withCentre (knob.getCentre());
        const float rr = slot.getHeight() * 0.5f;
        g.setColour (col::keyShadow.withMultipliedAlpha (on ? 0.6f : 0.3f));
        g.fillRoundedRectangle (slot.translated (0.0f, 2.0f), rr);
        g.setColour (sceneEdit ? col::violet : (isOn ? col::keyOn : col::well));
        g.fillRoundedRectangle (slot, rr);
        g.setColour (col::keyLine);
        g.drawRoundedRectangle (slot.reduced (0.5f), rr, 1.0f);
        const auto lever = juce::Rectangle<float> (slot.getHeight() - 6.0f, slot.getHeight() - 6.0f)
                               .withCentre ({ isOn ? slot.getRight() - rr : slot.getX() + rr, slot.getCentreY() });
        g.setColour (on ? col::ink : col::ink3);
        g.fillEllipse (lever);
        g.setColour (col::keyShadow.withAlpha (0.5f));
        g.drawEllipse (lever, 1.0f);
    }
    else
        encoder (g, knob.getCentre(), radius, value, ctl->bipolar, sceneEdit ? col::violet : col::cap (index),
                 sceneEdit ? juce::Colours::white : col::notch (index), modFrom, modTo, modColour, ! on);
    // its value, then what moves it: the mods' colours, the scenes holding it
    juce::String v = valueText (*ctl, arm);
    text (g, v, r.removeFromTop (22.0f), mono (15.0f, 500), on ? col::deckInk : col::deckInk.withAlpha (0.45f), juce::Justification::centred);
    if (ctl->param >= 0 && arm.player != nullptr)
    {
        auto tag = r.removeFromTop (16.0f);
        juce::Array<juce::Colour> marks;
        juce::String held;
        for (int m = 0; m < snd::numMods; ++m)
            if (std::abs (arm.player->routeAmount (m, ctl->param)) > 1.0e-4f)
                marks.add (col::mod (m));
        for (int side = 0; side < 2; ++side)
        {
            const int sc = (int) arm.player->get (side == 0 ? snd::sceneA : snd::sceneB) - 1;
            if (! std::isnan (arm.player->sceneLock (sc, ctl->param)))
                held << (side == 0 ? "a" : "b");
        }
        const float w = (float) marks.size() * 11.0f + (held.isNotEmpty() ? 18.0f : 0.0f);
        float x = tag.getCentreX() - w * 0.5f;
        for (auto c : marks)
        {
            g.setColour (c);
            g.fillRoundedRectangle (x, tag.getCentreY() - 4.0f, 8.0f, 8.0f, 2.0f);
            x += 11.0f;
        }
        if (held.isNotEmpty())
            text (g, held, { x, tag.getY(), 18.0f, tag.getHeight() }, mono (11.0f, 600), col::violet);
    }
}

void Encoder::setFrom (float norm)
{
    norm = juce::jlimit (0.0f, 1.0f, norm);
    if (const int m = armedMod(); m >= 0)
    {
        arm.player->setRoute (m, ctl->param, juce::jlimit (-1.0f, 1.0f, norm));
        if (arm.changed) arm.changed();
    }
    else if (const int sc = armedScene(); sc >= 0)
    {
        arm.player->setSceneLock (sc, ctl->param, snd::fromNorm (ctl->param, norm));
        if (arm.changed) arm.changed();
    }
    else if (ctl->setNorm)
        ctl->setNorm (norm);
    if (ctl->changed)
        ctl->changed();
    repaint();
}

void Encoder::nudge (float delta, bool fine)
{
    if (ctl == nullptr || ! ctl->isEnabled())
        return;
    if (const int m = armedMod(); m >= 0)
    {
        const float a = arm.player->routeAmount (m, ctl->param);
        setFrom (a + delta * (fine ? 0.15f : 1.0f));
        return;
    }
    setFrom (shownNorm() + delta * (fine ? 0.15f : 1.0f));
}

void Encoder::mouseDown (const juce::MouseEvent& e)
{
    if (ctl == nullptr || ! ctl->isEnabled())
        return;
    dragged = false;
    stepAccum = 0.0f;
    dragStart = armedMod() >= 0 ? arm.player->routeAmount (armedMod(), ctl->param) : shownNorm();
    juce::ignoreUnused (e);
    if (ctl->begin && ! arm.any())
    {
        ctl->begin();
        inGesture = true;
    }
    if (onTouch)
        onTouch (*ctl);
}

void Encoder::mouseDrag (const juce::MouseEvent& e)
{
    if (ctl == nullptr || ! ctl->isEnabled())
        return;
    const float dy = (float) -e.getDistanceFromDragStartY();
    if (std::abs (dy) > 2.0f)
        dragged = true;
    const bool fine = e.mods.isShiftDown();
    if (ctl->steps > 1 && ctl->steps <= 16 && armedMod() < 0)
    {
        // a few values: one per 14 pixels
        const float perStep = fine ? 30.0f : 14.0f;
        const int k = (int) std::floor (dy / perStep + 0.5f);
        setFrom (dragStart + (float) k / (float) (ctl->steps - 1));
        return;
    }
    const float range = armedMod() >= 0 ? 2.0f : 1.0f;      // a route: -100 % .. +100 %
    setFrom (dragStart + dy / 240.0f * range * (fine ? 0.15f : 1.0f));
}

void Encoder::mouseUp (const juce::MouseEvent&)
{
    if (ctl == nullptr)
        return;
    if (! dragged && ctl->steps > 1 && ctl->steps <= 16 && ctl->isEnabled() && armedMod() < 0)
    {
        // a click: the next value (round again after the last)
        const float now = shownNorm();
        const int at = juce::roundToInt (now * (float) (ctl->steps - 1));
        setFrom ((float) ((at + 1) % ctl->steps) / (float) (ctl->steps - 1));
    }
    if (inGesture && ctl->end)
        ctl->end();
    inGesture = false;
}

void Encoder::mouseDoubleClick (const juce::MouseEvent&)
{
    if (ctl == nullptr || ! ctl->isEnabled())
        return;
    if (const int m = armedMod(); m >= 0)
    {
        arm.player->setRoute (m, ctl->param, 0.0f);       // the route gone
        if (arm.changed) arm.changed();
    }
    else if (const int sc = armedScene(); sc >= 0)
    {
        arm.player->setSceneLock (sc, ctl->param, std::numeric_limits<float>::quiet_NaN());
        if (arm.changed) arm.changed();
    }
    else if (ctl->steps <= 1 || ctl->steps > 16)
    {
        if (ctl->begin) ctl->begin();
        if (ctl->setNorm) ctl->setNorm (ctl->defaultNorm);
        if (ctl->end) ctl->end();
        if (ctl->changed) ctl->changed();
    }
    repaint();
}

void Encoder::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (ctl == nullptr)
        return;
    const float d = (std::abs (w.deltaY) > 0.0f ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    if (ctl->steps > 1 && ctl->steps <= 16)
        nudge (d > 0.0f ? 1.0f / (float) (ctl->steps - 1) : -1.0f / (float) (ctl->steps - 1), false);
    else
        nudge (d * 0.08f, e.mods.isShiftDown());
    if (onTouch)
        onTouch (*ctl);
}

void Encoder::refreshIfChanged()
{
    if (ctl == nullptr)
        return;
    juce::String sig;
    sig << shownNorm() << "|" << valueText (*ctl, arm) << "|" << (int) ctl->isEnabled() << "|" << (int) focused;
    if (ctl->param >= 0 && arm.player != nullptr)
    {
        sig << "|" << arm.player->modulatedNorm (ctl->param);
        for (int m = 0; m < snd::numMods; ++m)
            sig << "|" << arm.player->routeAmount (m, ctl->param);
        sig << "|" << arm.mod << arm.scene << (int) arm.player->get (snd::sceneA) << (int) arm.player->get (snd::sceneB);
    }
    if (sig != lastSig)
    {
        lastSig = sig;
        repaint();
    }
}
}
