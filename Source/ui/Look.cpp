#include "Look.h"

#include <BinaryData.h>

namespace te
{
namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    juce::Typeface::Ptr monoFace (int weight)
    {
        static auto regular = load (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
        static auto medium = load (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
        static auto semi = load (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize);
        return weight >= 600 ? semi : weight >= 500 ? medium : regular;
    }
    juce::Typeface::Ptr groteskFace (int weight)
    {
        static auto light = load (BinaryData::SpaceGroteskLight_ttf, BinaryData::SpaceGroteskLight_ttfSize);
        static auto medium = load (BinaryData::SpaceGroteskMedium_ttf, BinaryData::SpaceGroteskMedium_ttfSize);
        static auto bold = load (BinaryData::SpaceGroteskBold_ttf, BinaryData::SpaceGroteskBold_ttfSize);
        return weight >= 700 ? bold : weight >= 500 ? medium : light;
    }
}

namespace col
{
    namespace
    {
        juce::Colour modsNow[4], modsScreenNow[4], capsNow[4], notchesNow[4];
        Theme current = Theme::noirEtOr;
    }
    juce::Colour mod (int m)            { return modsNow[juce::jlimit (0, 3, m)]; }
    juce::Colour modOnScreen (int m)    { return modsScreenNow[juce::jlimit (0, 3, m)]; }
    juce::Colour cap (int i)            { return capsNow[((i % 4) + 4) % 4]; }
    juce::Colour notch (int i)          { return notchesNow[((i % 4) + 4) % 4]; }
}

Theme theme()   { return col::current; }

void setTheme (Theme t)
{
    using namespace col;
    col::current = t;
    auto c = [] (juce::uint32 argb) { return juce::Colour (argb); };
    if (t == Theme::clair)
    {
        body = c (0xffdddcd7); face = c (0xffe9e8e3); col::key = c (0xfff1f0ec); keyLine = c (0xffb9b8b3); keyShadow = c (0xffa9a8a3); well = c (0xff161616);
        ink = c (0xff161616); ink2 = c (0xff55554f); ink3 = c (0xff8a8984);
        keyOn = c (0xff161616); keyOnInk = c (0xfff2f2ee); keyOnEdge = c (0xff000000);
        accent = c (0xffff5a1f); accentInk = c (0xff161616); accentDark = c (0xffc8410f);
        tickOff = c (0xffb4b3ae); tickOn = c (0xff161616); red = c (0xffe8291c); violet = c (0xff8b6fe0); white = c (0xfff7f6f2);
        deck = body; deckEdge = body; deckInk = ink; deckInk2 = ink2;
        col::screen = c (0xff0c0c0c); screenEdge = c (0xff1d1d1b); screenLine = c (0xff2a2a28); screenGrid = c (0xff1c1c1a);
        screenText = c (0xfff2f2ee); screenDim = c (0xff8c8c88); screenGhost = c (0xff3b3b38);
        sBlue = c (0xff4d8dff); sGreen = c (0xff2ecc71); sOrange = c (0xffff6a2b); sYellow = c (0xffffc21a); sRed = c (0xffff3b2f);
        sPink = c (0xffff6fa3); sViolet = c (0xffa995ff);
        const juce::Colour m[4] = { c (0xff1f6fe5), c (0xff18a058), c (0xfff0b800), c (0xffef4f8b) };
        const juce::Colour ms[4] = { sBlue, sGreen, sYellow, sPink };
        const juce::Colour caps[4] = { c (0xff1f6fe5), c (0xff18a058), white, accent };
        const juce::Colour n[4] = { juce::Colours::white, juce::Colours::white, ink, ink };
        std::copy (m, m + 4, modsNow); std::copy (ms, ms + 4, modsScreenNow); std::copy (caps, caps + 4, capsNow); std::copy (n, n + 4, notchesNow);
    }
    else
    {
        // black (#121312), gold (#a67d43), red (#ad0013), a cream for what must be read
        body = c (0xff121312); face = c (0xff1a1b1a); col::key = c (0xff232422); keyLine = c (0xff30312e); keyShadow = c (0xff050505); well = c (0xff050505);
        ink = c (0xffece4d4); ink2 = c (0xffa67d43); ink3 = c (0xff5f5647);
        keyOn = c (0xffa67d43); keyOnInk = c (0xff121312); keyOnEdge = c (0xff7a5a2e);
        accent = c (0xffad0013); accentInk = c (0xfff3e9da); accentDark = c (0xff660009);
        tickOff = c (0xff3a3a36); tickOn = c (0xfff3ead9); red = c (0xffd2102a); violet = c (0xff9a84e0); white = c (0xffece4d4);
        deck = c (0xff8c857a); deckEdge = c (0xff6f695f); deckInk = c (0xff121312); deckInk2 = c (0xff2b2722);
        col::screen = c (0xff070707); screenEdge = c (0xff2b241a); screenLine = c (0xff2a2925); screenGrid = c (0xff161614);
        screenText = c (0xffefe6d6); screenDim = c (0xff938470); screenGhost = c (0xff3b352b);
        sBlue = c (0xffd4a35a); sGreen = c (0xffa8c9b4); sOrange = c (0xffe0303f); sYellow = c (0xffc9995a); sRed = c (0xffff3346);
        sPink = c (0xffe07a8a); sViolet = c (0xffb9a2e0);
        const juce::Colour m[4] = { c (0xffe0303f), c (0xffd4a35a), c (0xff7fb59c), c (0xff9fb3c8) };
        const juce::Colour caps[4] = { c (0xffad0013), c (0xffa67d43), c (0xffece4d4), c (0xff0b0b0b) };
        const juce::Colour n[4] = { c (0xffece4d4), c (0xff121312), c (0xff121312), c (0xffa67d43) };
        std::copy (m, m + 4, modsNow); std::copy (m, m + 4, modsScreenNow); std::copy (caps, caps + 4, capsNow); std::copy (n, n + 4, notchesNow);
    }
}

namespace
{
    struct DefaultTheme { DefaultTheme() { setTheme (Theme::noirEtOr); } } defaultTheme;
}

juce::Font mono (float size, int weight)
{
    if (theme() == Theme::noirEtOr)
        return juce::Font (juce::FontOptions (groteskFace (weight >= 600 ? 700 : 500)).withHeight (size * 1.24f)).withExtraKerningFactor (0.04f);
    return juce::Font (juce::FontOptions (monoFace (weight)).withHeight (size * 1.3f));
}

juce::Font grotesk (float size, int weight)
{
    return juce::Font (juce::FontOptions (groteskFace (weight)).withHeight (size * 1.28f));
}

juce::String themed (const juce::String& s)
{
    if (theme() == Theme::noirEtOr)
    {
        static const juce::String small = juce::String::fromUTF8 ("\xc3\xa0\xc3\xa2\xc3\xa4\xc3\xa7\xc3\xa8\xc3\xa9\xc3\xaa\xc3\xab\xc3\xae\xc3\xaf\xc3\xb4\xc3\xb6\xc3\xb9\xc3\xbb\xc3\xbc\xc5\x93");
        static const juce::String capital = juce::String::fromUTF8 ("\xc3\x80\xc3\x82\xc3\x84\xc3\x87\xc3\x88\xc3\x89\xc3\x8a\xc3\x8b\xc3\x8e\xc3\x8f\xc3\x94\xc3\x96\xc3\x99\xc3\x9b\xc3\x9c\xc5\x92");
        return s.replaceCharacters (small, capital).toUpperCase();
    }
    return lower (s);
}

juce::String lower (const juce::String& s)
{
    static const juce::String capital = juce::String::fromUTF8 ("\xc3\x80\xc3\x82\xc3\x84\xc3\x87\xc3\x88\xc3\x89\xc3\x8a\xc3\x8b\xc3\x8e\xc3\x8f\xc3\x94\xc3\x96\xc3\x99\xc3\x9b\xc3\x9c\xc5\x92");
    static const juce::String small = juce::String::fromUTF8 ("\xc3\xa0\xc3\xa2\xc3\xa4\xc3\xa7\xc3\xa8\xc3\xa9\xc3\xaa\xc3\xab\xc3\xae\xc3\xaf\xc3\xb4\xc3\xb6\xc3\xb9\xc3\xbb\xc3\xbc\xc5\x93");
    return s.replaceCharacters (capital, small).toLowerCase();
}

//==============================================================================
Look::Look()
{
    applyTheme();
}

void Look::applyTheme()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::body);
    setColour (juce::DocumentWindow::backgroundColourId, col::body);
    setColour (juce::TextButton::buttonColourId, col::key);
    setColour (juce::TextButton::buttonOnColourId, col::keyOn);
    setColour (juce::TextButton::textColourOffId, col::ink);
    setColour (juce::TextButton::textColourOnId, col::keyOnInk);
    setColour (juce::TextEditor::backgroundColourId, col::screen);
    setColour (juce::TextEditor::textColourId, col::screenText);
    setColour (juce::TextEditor::highlightColourId, col::sOrange.withAlpha (0.4f));
    setColour (juce::TextEditor::highlightedTextColourId, col::screenText);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId, col::sOrange);
    setColour (juce::Label::textColourId, col::ink);
    setColour (juce::PopupMenu::backgroundColourId, col::key);
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::headerTextColourId, col::ink2);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::keyOn);
    setColour (juce::PopupMenu::highlightedTextColourId, col::keyOnInk);
    setColour (juce::TooltipWindow::backgroundColourId, col::screen);
    setColour (juce::TooltipWindow::textColourId, col::screenText);
    setColour (juce::TooltipWindow::outlineColourId, col::screen);
    setColour (juce::AlertWindow::backgroundColourId, col::key);
    setColour (juce::AlertWindow::textColourId, col::ink);
    setColour (juce::ComboBox::backgroundColourId, col::key);
    setColour (juce::ComboBox::textColourId, col::ink);
    setColour (juce::ListBox::backgroundColourId, col::key);
    setColour (juce::ListBox::textColourId, col::ink);
    setColour (juce::ScrollBar::thumbColourId, col::ink3);
    setColour (juce::ToggleButton::textColourId, col::ink);
    setColour (juce::ToggleButton::tickColourId, col::ink);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, col::keyOn);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, col::ink);
}

juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f)
{
    // (never f.getTypefacePtr() here: a font that names no face of its own came back here through the typeface
    // cache and went round until the stack ran out; Live crashed on the first tooltip in 0.1)
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
        || f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return monoFace (f.isBold() ? 600 : 400);
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::key);
    g.setColour (col::keyLine);
    g.drawRect (0, 0, w, h, 1);
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h)
{
    g.fillAll (col::screen);
    juce::AttributedString s;
    s.append (t, mono (13.0f), col::screenText);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, (float) w - 20.0f);
    tl.draw (g, { 10.0f, 8.0f, (float) w - 20.0f, (float) h - 16.0f });
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& t, juce::Point<int> at, juce::Rectangle<int> parent)
{
    juce::AttributedString s;
    s.append (t, mono (13.0f), col::screenText);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 340.0f);
    const int w = (int) std::ceil (tl.getWidth()) + 22, h = (int) std::ceil (tl.getHeight()) + 18;
    return juce::Rectangle<int> (at.x > parent.getCentreX() ? at.x - (w + 12) : at.x + 24,
                                 at.y > parent.getCentreY() ? at.y - (h + 6) : at.y + 6, w, h).constrainedWithin (parent);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool, bool down)
{
    key (g, b.getLocalBounds().toFloat(), b.getToggleState() ? KeyState::on : (b.isEnabled() ? KeyState::normal : KeyState::off), down);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto st = b.getToggleState() ? KeyState::on : (b.isEnabled() ? KeyState::normal : KeyState::off);
    auto r = b.getLocalBounds().toFloat();
    if (down)
        r.translate (0.0f, 1.0f);
    text (g, b.getButtonText(), r.withTrimmedBottom (2.0f), mono (13.0f, 500), keyInk (st), juce::Justification::centred);
}

void Look::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::screen);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 7.0f);
}

//==============================================================================
juce::Colour keyInk (KeyState s)
{
    switch (s)
    {
        case KeyState::on:     return col::keyOnInk;
        case KeyState::accent: return col::accentInk;
        case KeyState::off:    return col::ink3;
        case KeyState::normal:
        default:               return col::ink;
    }
}

void key (juce::Graphics& g, juce::Rectangle<float> r, KeyState s, bool pressed, float radius)
{
    juce::Colour fill = col::key, edge = col::keyLine, depth = col::keyShadow;
    if (s == KeyState::on)        { fill = col::keyOn; edge = col::keyOnEdge; depth = col::keyOnEdge.darker (0.5f); }
    if (s == KeyState::accent)    { fill = col::accent; edge = col::accent; depth = col::accentDark; }
    if (s == KeyState::off)       { fill = col::key.withAlpha (0.55f); edge = col::keyLine.withAlpha (0.5f); depth = edge; }
    const float d = pressed ? 1.0f : 3.0f;
    g.setColour (depth);
    g.fillRoundedRectangle (r, radius);
    auto top = r.withTrimmedBottom (d);
    if (pressed)
        top = top.translated (0.0f, 2.0f);
    g.setColour (fill);
    g.fillRoundedRectangle (top, radius);
    g.setColour (edge);
    g.drawRoundedRectangle (top.reduced (0.5f), radius, 1.0f);
}

void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const juce::Font& f, juce::Colour c,
           juce::Justification j, bool asIs)
{
    g.setColour (c);
    g.setFont (f);
    g.drawFittedText (asIs ? s : themed (s), r.toNearestInt(), j, 1, 0.85f);
}

void screen (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (col::screen);
    g.fillRoundedRectangle (r, radius);
    if (theme() == Theme::noirEtOr)
    {
        // a fine grid, and the glass a touch lighter at its top
        g.saveState();
        g.reduceClipRegion (r.reduced (3.0f).toNearestInt());
        g.setColour (col::screenGrid);
        for (float x = r.getX() + 24.0f; x < r.getRight(); x += 24.0f)
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        for (float y = r.getY() + 24.0f; y < r.getBottom(); y += 24.0f)
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), r.getX(), r.getY(),
                                                 juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.5f, false));
        g.fillRect (r);
        g.restoreState();
    }
    g.setColour (col::screenEdge);
    g.drawRoundedRectangle (r.reduced (2.0f), radius - 1.0f, 4.0f);
}

void ledMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, const juce::String& name)
{
    g.setColour (col::screen);
    g.fillRoundedRectangle (r, 6.0f);
    auto in = r.reduced (5.0f, 8.0f);
    text (g, name, in.removeFromBottom (14.0f), mono (9.0f, 600), col::screenDim, juce::Justification::centred);
    in.removeFromBottom (6.0f);
    const float dbv = level > 1.0e-5f ? 20.0f * std::log10 (level) : -100.0f;     // (no audio module in the UI tests)
    const int dots = juce::jmax (4, (int) (in.getHeight() / 6.0f));
    const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (dbv + 60.0f) / 60.0f) * (float) dots);
    for (int i = 0; i < dots; ++i)
    {
        const float y = in.getBottom() - (float) (i + 1) * 6.0f + 1.0f;
        const bool on = i < lit;
        const bool hot = i >= dots - 2;
        g.setColour (on ? (hot ? col::sRed : col::screenText) : col::screenLine);
        g.fillRoundedRectangle (in.getCentreX() - 3.0f, y, 6.0f, 3.5f, 1.0f);
    }
}

static void knobInRelief (juce::Graphics& g, juce::Point<float> c, float radius, float value, bool bipolar, juce::Colour cap,
                          float modFrom, float modTo, juce::Colour modColour, bool dim, juce::Colour lit)
{
    const float pi = juce::MathConstants<float>::pi;
    auto angle = [pi] (float t) { return (-135.0f + 270.0f * juce::jlimit (0.0f, 1.0f, t)) * pi / 180.0f; };   // 0 at the top
    const float ra = radius * 0.9f;
    // the track, the value, the band the modulation moves it by
    juce::Path track;
    track.addCentredArc (c.x, c.y, ra, ra, 0.0f, angle (0.0f), angle (1.0f), true);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.strokePath (track, juce::PathStrokeType (radius * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bipolar ? 0.5f : 0.0f;
    if (std::abs (value - from) > 1.0e-3f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ra, ra, 0.0f, angle (juce::jmin (from, value)), angle (juce::jmax (from, value)), true);
        const auto glow = lit == juce::Colour() ? col::tickOn : lit;
        g.setColour (glow.withAlpha (dim ? 0.35f : 0.25f));
        g.strokePath (arc, juce::PathStrokeType (radius * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (dim ? glow.withAlpha (0.5f) : glow);
        g.strokePath (arc, juce::PathStrokeType (radius * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (std::abs (modTo - modFrom) > 1.0e-3f)
    {
        juce::Path band;
        const float rb = radius * 1.02f;
        band.addCentredArc (c.x, c.y, rb, rb, 0.0f, angle (juce::jmin (modFrom, modTo)), angle (juce::jmax (modFrom, modTo)), true);
        g.setColour (modColour);
        g.strokePath (band, juce::PathStrokeType (radius * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // the knob: its shadow on the desk, its body (light from above), its rim, the line
    const float rk = radius * 0.68f;
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillEllipse (c.x - rk - (float) i, c.y - rk + (float) i * 1.6f, (rk + (float) i) * 2.0f, (rk + (float) i) * 2.0f);
    }
    const auto base = cap == col::violet ? col::violet.darker (0.25f) : juce::Colour (0xff141414);     // (violet: a scene being set)
    g.setGradientFill (juce::ColourGradient (base.brighter (0.35f), c.x, c.y - rk, base.darker (0.6f), c.x, c.y + rk, false));
    g.fillEllipse (c.x - rk, c.y - rk, rk * 2.0f, rk * 2.0f);
    g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), c.x - rk * 0.3f, c.y - rk * 0.4f, base.darker (0.2f), c.x + rk * 0.5f, c.y + rk * 0.6f, true));
    g.fillEllipse (c.x - rk * 0.82f, c.y - rk * 0.82f, rk * 1.64f, rk * 1.64f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (c.x - rk + 0.5f, c.y - rk + 0.5f, rk * 2.0f - 1.0f, rk * 2.0f - 1.0f, 1.0f);
    const float a = angle (value);
    const auto line = base.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff121312) : juce::Colour (0xfff3ead9);
    g.setColour (dim ? line.withAlpha (0.4f) : line);
    g.drawLine (c.x + rk * 0.45f * std::sin (a), c.y - rk * 0.45f * std::cos (a), c.x + rk * 0.88f * std::sin (a), c.y - rk * 0.88f * std::cos (a), radius * 0.075f);
}

void encoder (juce::Graphics& g, juce::Point<float> c, float radius, float value, bool bipolar, juce::Colour cap,
              juce::Colour notch, float modFrom, float modTo, juce::Colour modColour, bool dim, juce::Colour lit)
{
    if (theme() == Theme::noirEtOr)
    {
        knobInRelief (g, c, radius, value, bipolar, cap, modFrom, modTo, modColour, dim, lit);
        return;
    }
    constexpr int ticks = 25;
    const float r1 = radius * 0.80f, r2 = radius * 0.94f;
    const float pi = juce::MathConstants<float>::pi;
    auto angle = [pi] (float t) { return (135.0f + 270.0f * t) * pi / 180.0f; };
    value = juce::jlimit (0.0f, 1.0f, value);
    const int litTo = juce::roundToInt (value * (ticks - 1));
    const int mid = (ticks - 1) / 2;
    const int mLo = juce::roundToInt (juce::jmin (modFrom, modTo) * (ticks - 1)), mHi = juce::roundToInt (juce::jmax (modFrom, modTo) * (ticks - 1));
    const bool modded = std::abs (modTo - modFrom) > 1.0e-3f;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = angle ((float) i / (float) (ticks - 1));
        const bool on = bipolar ? (i >= juce::jmin (mid, litTo) && i <= juce::jmax (mid, litTo)) : (value > 0.0f && i <= litTo);
        juce::Colour t = on ? (lit == juce::Colour() ? col::tickOn : lit) : col::tickOff;
        if (modded && i >= mLo && i <= mHi && i != litTo)
            t = modColour;
        if (dim)
            t = t.withMultipliedAlpha (0.4f);
        g.setColour (t);
        g.drawLine (c.x + r1 * std::cos (a), c.y + r1 * std::sin (a), c.x + r2 * std::cos (a), c.y + r2 * std::sin (a), radius * 0.065f);
    }
    // the cap: its shadow, its face, the notch
    const float rc = radius * 0.6f;
    g.setColour (col::keyShadow.withMultipliedAlpha (dim ? 0.5f : 1.0f));
    g.fillEllipse (c.x - rc, c.y - rc + radius * 0.035f, rc * 2.0f, rc * 2.0f);
    g.setColour (dim ? cap.withMultipliedSaturation (0.3f).brighter (0.3f) : cap);
    g.fillEllipse (c.x - rc, c.y - rc - radius * 0.02f, rc * 2.0f, rc * 2.0f);
    if (cap.getPerceivedBrightness() > 0.85f || cap.getPerceivedBrightness() < 0.12f)
    {
        g.setColour (cap.getPerceivedBrightness() > 0.5f ? col::keyShadow : col::ink2);
        g.drawEllipse (c.x - rc, c.y - rc - radius * 0.02f, rc * 2.0f, rc * 2.0f, 1.0f);
    }
    const float a = angle (value);
    g.setColour (notch);
    g.drawLine (c.x + rc * 0.42f * std::cos (a), c.y - radius * 0.02f + rc * 0.42f * std::sin (a),
                c.x + rc * 0.88f * std::cos (a), c.y - radius * 0.02f + rc * 0.88f * std::sin (a), radius * 0.085f);
}

void eye (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour outline, juce::Colour iris, float open)
{
    const float cx = r.getCentreX(), cy = r.getCentreY(), w = r.getWidth() * 0.5f, h = r.getHeight() * 0.5f * juce::jlimit (0.05f, 1.0f, open);
    juce::Path lid;
    lid.startNewSubPath (cx - w, cy);
    lid.quadraticTo (cx, cy - h * 1.7f, cx + w, cy);
    lid.quadraticTo (cx, cy + h * 1.7f, cx - w, cy);
    lid.closeSubPath();
    g.saveState();
    g.reduceClipRegion (lid);
    const float ri = r.getHeight() * 0.3f;
    g.setColour (iris);
    g.fillEllipse (cx - ri, cy - ri, ri * 2.0f, ri * 2.0f);
    g.setColour (col::ink);
    g.fillEllipse (cx - ri * 0.43f, cy - ri * 0.43f, ri * 0.86f, ri * 0.86f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (cx + ri * 0.2f, cy - ri * 0.55f, ri * 0.36f, ri * 0.36f);
    g.restoreState();
    g.setColour (outline);
    g.strokePath (lid, juce::PathStrokeType (r.getHeight() * 0.09f));
}
}
