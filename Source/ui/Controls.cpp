#include "Controls.h"

namespace te
{
namespace
{
    // a value written short, for a scale: 1K, 2.5K, 0.5, 120
    juce::String shortNumber (double v)
    {
        const double a = std::abs (v);
        juce::String sign = v < 0.0 ? "-" : "";
        if (a >= 1000.0)
        {
            const double k = a / 1000.0;
            return sign + (std::abs (k - std::round (k)) < 1.0e-6 ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + "K";
        }
        if (std::abs (a - std::round (a)) < 1.0e-6)
            return sign + juce::String ((int) std::round (a));
        if (std::abs (a - 0.25) < 1.0e-6) return sign + juce::String ("1/4");
        if (std::abs (a - 0.5) < 1.0e-6)  return sign + juce::String ("1/2");
        return sign + juce::String (a, a < 1.0 ? 2 : 1);
    }

    // the scale of a setting of the sound: values a hardware panel would print round its knob
    void scaleOf (Ctl& c, int id)
    {
        const auto& d = snd::def (id);
        const std::string u = d.unit;
        auto at = [id] (double v) { return snd::toNorm (id, (float) v); };
        auto add = [&c] (float t, const juce::String& l) { c.marks.push_back ({ juce::jlimit (0.0f, 1.0f, t), l }); };
        auto ends = [&] (const juce::String& lo, const juce::String& mid, const juce::String& hi)
        {
            add (0.0f, lo);
            if (mid.isNotEmpty()) add (0.5f, mid);
            add (1.0f, hi);
        };
        // values along a log or power scale: decades, the ends written when nothing sits near them
        auto decades = [&] (std::initializer_list<double> vs, const char* suffixOver1000)
        {
            for (double v : vs)
                if (v >= d.lo - 1.0e-6 && v <= d.hi + 1.0e-6)
                {
                    juce::String l = suffixOver1000 != nullptr && v >= 1000.0 ? juce::String ((int) (v / 1000.0)) + suffixOver1000 : shortNumber (v);
                    add (at (v), l);
                }
            bool nearLo = false, nearHi = false;
            for (auto& m : c.marks)
            {
                nearLo = nearLo || m.at < 0.12f;
                nearHi = nearHi || m.at > 0.88f;
            }
            if (! nearLo) add (0.0f, shortNumber (d.lo));
            if (! nearHi) add (1.0f, shortNumber (d.hi));
        };
        c.ticks = 11;
        if (d.kind == snd::Kind::choice)
        {
            const int n = (int) std::lround (d.hi - d.lo) + 1;
            c.ticks = n;
            const auto o = snd::choicesOf (id);
            if (! o.empty())
            {
                add (0.0f, juce::String::fromUTF8 (o.front().c_str()));
                add (0.5f, juce::String::fromUTF8 (o[(size_t) (n - 1) / 2].c_str()));
                add (1.0f, juce::String::fromUTF8 (o.back().c_str()));
            }
            return;
        }
        if (d.kind == snd::Kind::integer)
        {
            const int n = (int) std::lround (d.hi - d.lo) + 1;
            c.ticks = juce::jmin (n, 25);
            auto label = [&] (int v) { return (v > 0 && d.lo < 0.0f ? "+" : "") + juce::String (v); };
            if (n <= 7)
                for (int k = 0; k < n; ++k)
                    add ((float) k / (float) (n - 1), label ((int) d.lo + k));
            else
            {
                add (0.0f, label ((int) d.lo));
                if (d.lo < 0.0f && d.hi > 0.0f)
                    add (at (0.0), "0");
                else
                    add (0.5f, label ((int) std::lround ((d.lo + d.hi) * 0.5f)));
                add (1.0f, label ((int) d.hi));
            }
            return;
        }
        if (u == "Hz")                 decades ({ 0.1, 1.0, 10.0, 100.0, 1000.0, 10000.0 }, "K");
        else if (u == "ms")            { decades ({ 1.0, 10.0, 100.0, 1000.0, 10000.0 }, "S"); }
        else if (u == "dB")
        {
            const bool across = d.lo < 0.0f && d.hi > 0.0f;
            ends (shortNumber (d.lo), across ? juce::String() : shortNumber ((d.lo + d.hi) * 0.5), (d.hi > 0.0f ? "+" : "") + shortNumber (d.hi));
            if (across)
                add (at (0.0), "0");
        }
        else if (u == "x" && d.kind == snd::Kind::log) decades ({ 0.25, 1.0, 4.0, 16.0 }, nullptr);
        else if (u == "x")             { ends (shortNumber (d.lo), "", "+" + shortNumber (d.hi)); add (at (0.0), "0"); add (at (1.0), "1"); }
        else if (u == "smp")           decades ({ 0.0, 16.0, 64.0, 256.0 }, nullptr);
        else if (u == "bit")           { ends ("1", "", shortNumber (d.hi)); add (at (8.0), "8"); }
        else if (u == "bpm")           { add (0.0f, "40"); add (at (120.0), "120"); add (at (200.0), "200"); add (1.0f, "300"); c.ticks = 14; }
        else if (u == "orb")           decades ({ 0.0, 1.0, 4.0, 12.0 }, nullptr);
        else if (u == "tun")
        {
            // (the sampler's dial: +-96; the radio's tunes: +-24)
            const int hi = (int) d.hi;
            add (0.0f, "-" + juce::String (hi)); add (0.25f, "-" + juce::String (hi / 2)); add (0.5f, "0");
            add (0.75f, "+" + juce::String (hi / 2)); add (1.0f, "+" + juce::String (hi));
            c.ticks = hi > 24 ? 17 : 9;
        }
        else if (u == "rsq")           { add (0.0f, "OFF"); add (at (0.5), "-38"); add (1.0f, "-10"); c.ticks = 9; }
        else if (u == "rgb")           { add (0.0f, "KEEP"); add (1.0f, "100"); c.ticks = 9; }
        else if (u == "rbw")           { add (0.0f, "FULL"); add (1.0f, "250"); c.ticks = 9; }
        else if (u == "rdc")           { add (0.0f, "20MS"); add (1.0f, "HOLD"); c.ticks = 9; }
        else if (u == "s")             decades ({ 1.0, 5.0, 20.0 }, nullptr);
        else if (u == "shp")           { add (0.0f, "TRI"); add (0.5f, "SAW"); add (1.0f, "SQR"); c.ticks = 9; }
        else if (u == "deg")           { add (0.0f, "0"); add (0.5f, "180"); add (1.0f, "360"); c.ticks = 9; }
        else if (u == "ct" || u == "oct" || u == "sym" || (d.lo < 0.0f && d.hi > 0.0f))
                                       ends ("-", "0", "+");
        else if (u == "%" && d.lo >= 0.0f && d.kind == snd::Kind::log)
                                       decades ({ 0.01, 0.1, 1.0 }, nullptr);
        else                           ends ("0", "5", "10");
    }
}

void dress (Ctl& c)
{
    if (c.toggle)
    {
        c.look = Ctl::Look::toggle;
        if (c.options.isEmpty())
            c.options = { "off", "on" };
        return;
    }
    const int n = c.options.size();
    if (n == 2)
    {
        c.look = Ctl::Look::twoWay;
        return;
    }
    if (n >= 3)
    {
        int longest = 0;
        for (auto& o : c.options)
            longest = juce::jmax (longest, o.length());
        if (n <= 6 && longest <= 5)
            c.look = Ctl::Look::buttons;
        else if (n <= 5 && longest <= 10)
            c.look = Ctl::Look::ledList;
        else if (n > 8)
            c.look = Ctl::Look::knob;              // many short steps (the divisions): a graduated knob
        else
            c.look = Ctl::Look::display;
        return;
    }
    c.look = Ctl::Look::knob;
}

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
    if (d.kind == snd::Kind::choice)
        for (auto& o : snd::choicesOf (id))
            c.options.add (juce::String::fromUTF8 (o.c_str()));
    // the tempo by whole BPM
    if (id == snd::tempo)
    {
        c.quantum = 1.0f;
        c.setNorm = [param, id] (float t)
        {
            const float v = std::round (snd::fromNorm (id, juce::jlimit (0.0f, 1.0f, t)));
            param->setValueNotifyingHost (snd::toNorm (id, v));
        };
    }
    // the tuning: 192 semitones, so a long drag (a tenth of a semitone a pixel; shift: a cent and a half)
    if (id == snd::tune)
        c.dragPixels = 1900.0f;
    scaleOf (c, id);
    dress (c);
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
    c.options = choices;
    dress (c);
    if (c.look == Ctl::Look::knob)
        c.look = Ctl::Look::display;               // (a long list of names: the display and its menu)
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
    // its scale: the two ends (as it writes them), the middle when it is zero
    c.marks.push_back ({ 0.0f, fmt (lo) });
    if (c.bipolar)
        c.marks.push_back ({ c.getNorm ? (float) ((0.0 - lo) / (hi - lo)) : 0.5f, fmt (0.0) });
    c.marks.push_back ({ 1.0f, fmt (hi) });
    c.ticks = c.steps > 1 && c.steps <= 25 ? c.steps : 11;
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

// the printed scale round a knob: its graduations, a few values written outside them
void Encoder::paintScale (juce::Graphics& g, juce::Point<float> c, float radius, bool on) const
{
    const float pi = juce::MathConstants<float>::pi;
    auto angle = [pi] (float t) { return (-135.0f + 270.0f * juce::jlimit (0.0f, 1.0f, t)) * pi / 180.0f; };
    auto at = [c] (float a, float r) { return juce::Point<float> (c.x + std::sin (a) * r, c.y - std::cos (a) * r); };
    const auto ink = col::deckInk2.withMultipliedAlpha (on ? 0.85f : 0.35f);
    g.setColour (ink);
    if (ctl->ticks > 1)
        for (int i = 0; i < ctl->ticks; ++i)
        {
            const float a = angle ((float) i / (float) (ctl->ticks - 1));
            g.drawLine ({ at (a, radius * 1.1f), at (a, radius * 1.2f) }, 1.0f);
        }
    for (auto& m : ctl->marks)
    {
        const float a = angle (m.at);
        g.drawLine ({ at (a, radius * 1.08f), at (a, radius * 1.3f) }, 1.6f);
        const auto p = at (a, radius * 1.62f);
        const float w = 40.0f;
        auto box = juce::Rectangle<float> (w, 12.0f).withCentre (p);
        auto just = juce::Justification::centred;
        if (std::sin (a) < -0.35f)      { box = box.withX (p.x - w + 8.0f); just = juce::Justification::centredRight; }
        else if (std::sin (a) > 0.35f)  { box = box.withX (p.x - 8.0f); just = juce::Justification::centredLeft; }
        text (g, m.label, box, mono (9.5f, 500), ink, just);
    }
}

void Encoder::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    hits.clear();
    prevHit = nextHit = menuHit = {};
    if (ctl == nullptr)
        return;
    if (focused)
    {
        g.setColour (col::deck == col::body ? col::face : col::deck.brighter (0.14f));
        g.fillRoundedRectangle (r.reduced (2.0f), 12.0f);
    }
    const bool on = ctl->isEnabled();
    const auto inkOn = on ? col::deckInk : col::deckInk.withAlpha (0.45f);
    const auto ink2 = on ? col::deckInk2 : col::deckInk2.withAlpha (0.45f);
    text (g, ctl->name, r.removeFromTop (22.0f).withTrimmedTop (6.0f), mono (12.0f, 500), ink2, juce::Justification::centred);
    auto tags = r.removeFromBottom (16.0f);
    const bool showValue = ctl->look == Ctl::Look::knob;
    auto valueBox = showValue ? r.removeFromBottom (24.0f) : juce::Rectangle<float>();
    auto area = r;                                              // the control itself
    const float value = shownNorm();
    const int n = juce::jmax (1, ctl->options.size());
    const int chosen = n > 1 ? juce::jlimit (0, n - 1, juce::roundToInt (value * (float) (n - 1))) : 0;
    const bool sceneEdit = armedScene() >= 0;

    // a lamp: a lens in a dark bezel, lit (its glow round it) or dark, a glint on top
    auto led = [&g, on, sceneEdit] (juce::Point<float> c, bool lit, float rad = 4.0f)
    {
        const auto hue = sceneEdit ? col::violet : col::red;
        if (lit && on)
        {
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.45f), c.x, c.y, hue.withAlpha (0.0f), c.x + rad * 3.2f, c.y, true));
            g.fillEllipse (juce::Rectangle<float> (rad * 6.4f, rad * 6.4f).withCentre (c));
        }
        auto bezel = juce::Rectangle<float> (rad * 2.0f + 3.0f, rad * 2.0f + 3.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2a), c.x, bezel.getY(), juce::Colour (0xff6b6b6b), c.x, bezel.getBottom(), false));
        g.fillEllipse (bezel);
        auto lens = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);
        if (lit)
        {
            const auto core = on ? hue.brighter (0.9f) : hue.withAlpha (0.5f);
            g.setGradientFill (juce::ColourGradient (core, c.x, c.y - rad * 0.2f, on ? hue.darker (0.3f) : hue.darker (1.0f), c.x, c.y + rad, true));
        }
        else
            g.setGradientFill (juce::ColourGradient (hue.darker (2.2f), c.x, c.y - rad * 0.3f, juce::Colour (0xff120606), c.x, c.y + rad, true));
        g.fillEllipse (lens);
        g.setColour (juce::Colours::white.withAlpha (lit && on ? 0.55f : 0.18f));
        g.fillEllipse (c.x - rad * 0.45f, c.y - rad * 0.7f, rad * 0.6f, rad * 0.45f);
    };

    // a bat-handle switch: its chrome nut, the dark slot, the lever up or down; a lamp and a name at each end
    auto batSwitch = [&] (int topIndex)
    {
        const int bottomIndex = 1 - topIndex;
        const bool up = chosen == topIndex;
        const auto c = area.getCentre();
        auto plate = juce::Rectangle<float> (34.0f, 56.0f).withCentre (c);
        // the plate: brushed chrome, its edge
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (plate.translated (0.0f, 2.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9e6df), plate.getX(), plate.getY(), juce::Colour (0xff8d8a83), plate.getRight(), plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 5.0f);
        g.setColour (juce::Colour (0xff4a4844));
        g.drawRoundedRectangle (plate.reduced (0.5f), 5.0f, 1.0f);
        // the slot, and the lever in it: its lit face on the side it points to
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawLine (plate.getX() + 4.0f, plate.getY() + 1.2f, plate.getRight() - 4.0f, plate.getY() + 1.2f, 1.0f);     // its lit edge
        auto slot = plate.reduced (7.0f, 8.0f);
        g.setColour (juce::Colour (0xff0d0d0d));
        g.fillRoundedRectangle (slot, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (slot.expanded (0.8f).translated (0.0f, 0.6f), 3.5f, 0.8f);
        auto lever = slot.reduced (2.0f);
        auto face = up ? lever.removeFromTop (lever.getHeight() * 0.55f) : lever.removeFromBottom (lever.getHeight() * 0.55f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), lever.getX(), lever.getY(), juce::Colour (0xff1a1a1a), lever.getX(), lever.getBottom(), false));
        g.fillRoundedRectangle (lever, 2.0f);
        const auto light = sceneEdit ? col::violet.brighter (0.6f) : (on ? juce::Colour (0xfff4f1ea) : juce::Colour (0xff9a978f));
        g.setGradientFill (juce::ColourGradient (light, face.getX(), up ? face.getY() : face.getBottom(), light.darker (0.45f), face.getX(), up ? face.getBottom() : face.getY(), false));
        g.fillRoundedRectangle (face, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (face, 2.0f, 0.8f);
        // its two ends: a name, a lamp
        const float gap = 6.0f;
        auto topName = juce::Rectangle<float> (area.getWidth(), 12.0f).withCentre ({ c.x, plate.getY() - gap - 6.0f });
        auto bottomName = juce::Rectangle<float> (area.getWidth(), 12.0f).withCentre ({ c.x, plate.getBottom() + gap + 6.0f });
        text (g, ctl->options[topIndex], topName, mono (10.5f, up ? 700 : 500), up ? inkOn : ink2.withMultipliedAlpha (0.55f), juce::Justification::centred);
        text (g, ctl->options[bottomIndex], bottomName, mono (10.5f, up ? 500 : 700), up ? ink2.withMultipliedAlpha (0.55f) : inkOn, juce::Justification::centred);
        led ({ c.x, topName.getY() - 9.0f }, up);
        led ({ c.x, bottomName.getBottom() + 9.0f }, ! up);
    };

    switch (ctl->look)
    {
        case Ctl::Look::toggle:
            batSwitch (1);                                              // ON above
            break;
        case Ctl::Look::twoWay:
            batSwitch (0);                                              // its first position above
            break;
        case Ctl::Look::buttons:
        {
            // a push button per position, a lamp above the one chosen, its name under it (two rows past four)
            const int perRow = n <= 4 ? n : (n + 1) / 2;
            const int rows = (n + perRow - 1) / perRow;
            const float cellW = juce::jmin (38.0f, (area.getWidth() - 8.0f) / (float) perRow), rowH = 54.0f;
            auto block = juce::Rectangle<float> (cellW * (float) perRow, rowH * (float) rows).withCentre (area.getCentre());
            for (int k = 0; k < n; ++k)
            {
                const int row = k / perRow, col_ = k % perRow;
                const int inRow = juce::jmin (perRow, n - row * perRow);
                const float x0 = block.getCentreX() - cellW * (float) inRow * 0.5f + cellW * (float) col_;
                auto cell = juce::Rectangle<float> (x0, block.getY() + rowH * (float) row, cellW, rowH);
                const bool sel = k == chosen;
                const auto cc = juce::Point<float> (cell.getCentreX(), cell.getY() + 27.0f);
                led ({ cc.x, cell.getY() + 6.0f }, sel, 3.4f);
                // the button: a bezel ring, its round black cap (lower and darker when held in)
                const float br = 12.5f;
                g.setColour (juce::Colours::black.withAlpha (0.3f));
                g.fillEllipse (juce::Rectangle<float> (br * 2.0f + 4.0f, br * 2.0f + 4.0f).withCentre (cc.translated (0.0f, 2.0f)));
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5c5a55), cc.x, cc.y - br, juce::Colour (0xff1c1c1b), cc.x, cc.y + br, false));
                g.fillEllipse (juce::Rectangle<float> (br * 2.0f + 4.0f, br * 2.0f + 4.0f).withCentre (cc));
                const auto capC = cc.translated (0.0f, sel ? 1.0f : 0.0f);
                const float cr = sel ? br - 1.0f : br;
                const auto base = sceneEdit ? col::violet.darker (0.4f) : juce::Colour (0xff161616);
                g.setGradientFill (juce::ColourGradient (base.brighter (sel ? 0.15f : 0.45f), capC.x, capC.y - cr, base.darker (0.5f), capC.x, capC.y + cr, false));
                g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (capC));
                g.setColour (juce::Colours::white.withAlpha (sel ? 0.06f : 0.16f));
                g.drawEllipse (juce::Rectangle<float> (cr * 2.0f - 3.0f, cr * 2.0f - 3.0f).withCentre (capC), 1.0f);
                text (g, ctl->options[k], { cell.getX() - 6.0f, cc.y + br + 3.0f, cell.getWidth() + 12.0f, 12.0f }, mono (9.5f, sel ? 700 : 500),
                      sel ? inkOn : ink2.withMultipliedAlpha (0.6f), juce::Justification::centred);
                hits.push_back (cell);
            }
            break;
        }
        case Ctl::Look::ledList:
        {
            // the positions one under the other, a lamp by each, the chosen one lit
            const float rowH = juce::jmin (18.0f, area.getHeight() / (float) n);
            auto list = juce::Rectangle<float> (juce::jmin (area.getWidth() - 16.0f, 120.0f), rowH * (float) n).withCentre (area.getCentre());
            for (int k = 0; k < n; ++k)
            {
                auto row = list.removeFromTop (rowH);
                led ({ row.getX() + 6.0f, row.getCentreY() }, k == chosen, 3.6f);
                text (g, ctl->options[k], row.withTrimmedLeft (18.0f), mono (11.0f, k == chosen ? 700 : 500), k == chosen ? inkOn : ink2.withMultipliedAlpha (0.6f),
                      juce::Justification::centredLeft);
                hits.push_back (row);
            }
            break;
        }
        case Ctl::Look::display:
        {
            // an orange digital display (the old machines' kind) behind glass, in a dark bezel, an arrow each side
            auto bezel = juce::Rectangle<float> (area.getWidth() - 12.0f, 44.0f).withCentre (area.getCentre());
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (bezel.translated (0.0f, 2.0f), 7.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3935), bezel.getX(), bezel.getY(), juce::Colour (0xff111110), bezel.getX(), bezel.getBottom(), false));
            g.fillRoundedRectangle (bezel, 7.0f);
            auto box = bezel.reduced (3.0f);
            g.setColour (juce::Colour (0xff140a04));
            g.fillRoundedRectangle (box, 5.0f);
            const auto orange = on ? juce::Colour (0xffff8a24) : juce::Colour (0xff7a4416);
            auto arrows = box.reduced (5.0f, 0.0f);
            prevHit = arrows.removeFromLeft (16.0f);
            nextHit = arrows.removeFromRight (16.0f);
            menuHit = arrows;
            auto tri = [&g] (juce::Rectangle<float> b, bool left, juce::Colour c)
            {
                juce::Path t;
                const auto m = b.getCentre();
                if (left) t.addTriangle (m.x - 4.0f, m.y, m.x + 3.0f, m.y - 5.0f, m.x + 3.0f, m.y + 5.0f);
                else      t.addTriangle (m.x + 4.0f, m.y, m.x - 3.0f, m.y - 5.0f, m.x - 3.0f, m.y + 5.0f);
                g.setColour (c);
                g.fillPath (t);
            };
            tri (prevHit, true, orange.withAlpha (chosen > 0 ? 0.9f : 0.25f));
            tri (nextHit, false, orange.withAlpha (chosen < n - 1 ? 0.9f : 0.25f));
            const auto shown = ctl->options.isEmpty() ? valueText (*ctl, arm) : ctl->options[chosen];
            float size = 17.0f;
            while (size > 10.0f && mono (size, 700).getStringWidthFloat (shown.toUpperCase()) > menuHit.getWidth() - 4.0f)
                size -= 1.0f;
            // the unlit segments behind, the glow, the figures
            text (g, juce::String::repeatedString ("8", juce::jmax (1, shown.length())), menuHit, mono (size, 700), orange.withAlpha (0.07f), juce::Justification::centred);
            text (g, shown, menuHit.translated (0.0f, 0.6f), mono (size, 700), orange.withAlpha (0.3f), juce::Justification::centred);
            text (g, shown, menuHit, mono (size, 700), orange, juce::Justification::centred);
            // the glass: a sheen across its top half
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f), box.getX(), box.getY(), juce::Colours::white.withAlpha (0.0f), box.getX(), box.getCentreY(), false));
            g.fillRoundedRectangle (box.withHeight (box.getHeight() * 0.5f), 5.0f);
            text (g, juce::String (chosen + 1) + " / " + juce::String (n), bezel.withY (bezel.getBottom() + 4.0f).withHeight (13.0f), mono (9.5f, 500), ink2,
                  juce::Justification::centred);
            break;
        }
        case Ctl::Look::knob:
        default:
        {
            const float radius = juce::jmin (32.0f, area.getWidth() * 0.21f, area.getHeight() * 0.33f);
            const auto c = area.getCentre().translated (0.0f, 4.0f);
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
            paintScale (g, c, radius, on);
            encoder (g, c, radius, value, ctl->bipolar, sceneEdit ? col::violet : col::cap (index),
                     sceneEdit ? juce::Colours::white : col::notch (index), modFrom, modTo, modColour, ! on);
            break;
        }
    }
    // its value (a knob's), in a small LCD window; then what moves it: the mods' colours, the scenes holding it
    if (showValue)
    {
        auto lcd = valueBox.withSizeKeepingCentre (juce::jmin (valueBox.getWidth() - 20.0f, 104.0f), 19.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (lcd.translated (0.0f, 1.0f), 4.0f, 1.0f);
        g.setColour (juce::Colour (0xff15171a));
        g.fillRoundedRectangle (lcd, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (lcd, 4.0f, 1.0f);
        text (g, valueText (*ctl, arm), lcd, mono (12.0f, 500), on ? juce::Colour (0xffd9e2e6) : juce::Colour (0xff6a7276), juce::Justification::centred);
    }
    if (ctl->param >= 0 && arm.player != nullptr)
    {
        auto tag = tags;
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

// a position chosen by a click (a choice's)
void Encoder::pick (int option)
{
    const int n = ctl->options.size();
    if (n < 2)
        return;
    setFrom ((float) juce::jlimit (0, n - 1, option) / (float) (n - 1));
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
    setFrom (dragStart + dy / ctl->dragPixels * range * (fine ? 0.15f : 1.0f));
}

void Encoder::mouseUp (const juce::MouseEvent& e)
{
    if (ctl == nullptr)
        return;
    const auto pos = e.position;
    const int n = ctl->options.size();
    const int at = n > 1 ? juce::roundToInt (shownNorm() * (float) (n - 1)) : 0;
    if (! dragged && ctl->isEnabled() && armedMod() < 0)
    {
        bool done = false;
        // a position clicked; the display's arrows; its middle: the whole list
        for (size_t k = 0; k < hits.size() && ! done; ++k)
            if (hits[k].contains (pos))
            {
                pick ((int) k);
                done = true;
            }
        if (! done && ctl->look == Ctl::Look::display && n > 1)
        {
            if (prevHit.contains (pos))      { pick (at - 1); done = true; }
            else if (nextHit.contains (pos)) { pick (at + 1); done = true; }
            else if (menuHit.contains (pos))
            {
                juce::PopupMenu menu;
                for (int k = 0; k < n; ++k)
                    menu.addItem (k + 1, ctl->options[k], true, k == at);
                juce::Component::SafePointer<Encoder> safe (this);
                const Ctl* which = ctl;
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, which] (int r)
                {
                    if (safe != nullptr && safe->ctl == which && r > 0)
                        safe->pick (r - 1);
                });
                done = true;
            }
        }
        if (! done && ctl->steps > 1 && ctl->steps <= 16 && ctl->look != Ctl::Look::display && ctl->look != Ctl::Look::ledList
            && ctl->look != Ctl::Look::buttons)
        {
            // a click elsewhere on a switch, a two-way button or a stepped knob: the next value (round again)
            const float now = shownNorm();
            const int k = juce::roundToInt (now * (float) (ctl->steps - 1));
            setFrom ((float) ((k + 1) % ctl->steps) / (float) (ctl->steps - 1));
        }
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
