#include "Assistant.h"
#include "Text.h"

namespace te
{
namespace
{
    using Host = ReplikantProcessor::Host;
    using Src = ReplikantProcessor::Source;
    constexpr double slide = 0.32;          // a step's entrance (s)

    float clamp01 (float x)                 { return juce::jlimit (0.0f, 1.0f, x); }
    float easeOut (float x)                 { x = clamp01 (x); return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); }
    float easeBack (float x)
    {
        x = clamp01 (x);
        const float c1 = 1.70158f, c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow (x - 1.0f, 3.0f) + c1 * std::pow (x - 1.0f, 2.0f);
    }

    void write (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const juce::Font& f, juce::Colour c,
                juce::Justification j = juce::Justification::centredLeft, int maxLines = 1)
    {
        g.setColour (c);
        g.setFont (f);
        g.drawFittedText (s, r.toNearestInt(), j, maxLines, 0.9f);
    }

    // text with [[parts]] in gold (what is to be set), wrapped at words; the height it took
    struct Rich { juce::String text; float size; juce::Colour base, gold; };
    Rich rich (const juce::String& s, float size, juce::Colour base, juce::Colour gold)   { return { s, size, base, gold }; }

    float drawRich (juce::Graphics& g, const Rich& rt, juce::Rectangle<float> r)
    {
        const auto plain = grotesk (rt.size, 400), bold = grotesk (rt.size, 600);
        const float lineH = rt.size * 1.42f;
        float x = r.getX(), y = r.getY();
        auto put = [&] (const juce::String& run, bool gold)
        {
            const auto& f = gold ? bold : plain;
            int i = 0;
            while (i < run.length())
            {
                // a word, or one space
                int j = i;
                if (run[i] == ' ')
                    j = i + 1;
                else
                    while (j < run.length() && run[j] != ' ')
                        ++j;
                const auto word = run.substring (i, j);
                const float w = juce::GlyphArrangement::getStringWidth (f, word);
                if (word == " ")
                {
                    if (x > r.getX())
                        x += w;
                }
                else
                {
                    if (x + w > r.getRight() + 0.5f && x > r.getX())
                    {
                        x = r.getX();
                        y += lineH;
                    }
                    g.setFont (f);
                    g.setColour (gold ? rt.gold : rt.base);
                    g.drawText (word, juce::Rectangle<float> (x, y, w + 4.0f, lineH), juce::Justification::centredLeft, false);
                    x += w;
                }
                i = j;
            }
        };
        auto rest = rt.text;
        while (rest.isNotEmpty())
        {
            const int open = rest.indexOf ("[[");
            if (open < 0) { put (rest, false); break; }
            put (rest.substring (0, open), false);
            const int close = rest.indexOf (open + 2, "]]");
            if (close < 0) { put (rest.substring (open + 2), true); break; }
            put (rest.substring (open + 2, close), true);
            rest = rest.substring (close + 2);
        }
        return y + lineH - r.getY();
    }

    void tickMark (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col, float thick = 2.0f)
    {
        juce::Path p;
        p.startNewSubPath (c.x - 0.42f * s, c.y + 0.02f * s);
        p.lineTo (c.x - 0.12f * s, c.y + 0.3f * s);
        p.lineTo (c.x + 0.45f * s, c.y - 0.34f * s);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void crossMark (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col, float thick = 2.0f)
    {
        g.setColour (col);
        g.drawLine (c.x - 0.32f * s, c.y - 0.32f * s, c.x + 0.32f * s, c.y + 0.32f * s, thick);
        g.drawLine (c.x + 0.32f * s, c.y - 0.32f * s, c.x - 0.32f * s, c.y + 0.32f * s, thick);
    }

    void caret (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col)
    {
        juce::Path p;
        p.addTriangle (c.x - s, c.y - s * 0.5f, c.x + s, c.y - s * 0.5f, c.x, c.y + s * 0.6f);
        g.setColour (col);
        g.fillPath (p);
    }

    // a pane on the screen, lit at its edge when chosen
    void pane (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool over, bool enabled = true, float radius = 10.0f)
    {
        const float a = enabled ? 1.0f : 0.45f;
        g.setColour ((on ? col::screenGhost.withAlpha (0.5f) : (over ? col::screenLine.withAlpha (0.85f) : col::screenGrid.brighter (0.15f))).withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, radius);
        if (on)
        {
            g.setColour (col::sRed.withAlpha (0.14f));
            g.drawRoundedRectangle (r.expanded (2.5f), radius + 2.5f, 3.0f);
        }
        g.setColour ((on ? col::sRed : (over ? col::screenDim : col::screenLine)).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, on ? 2.0f : 1.0f);
    }

    void chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, bool on, bool over, bool enabled = true)
    {
        const float a = enabled ? 1.0f : 0.4f;
        g.setColour ((on ? col::sRed.withAlpha (0.2f) : (over ? col::screenLine : col::screenGrid.brighter (0.2f))).withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour ((on ? col::sRed : (over ? col::screenDim : col::screenLine)).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, on ? 1.6f : 1.0f);
        write (g, s, r.reduced (10.0f, 0.0f), grotesk (13.0f, on ? 600 : 400), (on || over ? col::screenText : col::screenDim).withMultipliedAlpha (a),
               juce::Justification::centred);
    }

    float chipWidth (const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (grotesk (13.0f, 600), s, 0.0f, 0.0f);
        return juce::jmin (300.0f, ga.getBoundingBox (0, -1, true).getWidth() + 26.0f);
    }

    void bigKey (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, bool over, bool enabled, bool red = true)
    {
        auto fill = red ? col::red : col::screenLine;
        if (! enabled)
            fill = col::screenGrid.brighter (0.2f);
        else if (over)
            fill = fill.brighter (0.15f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 9.0f);
        if (! red || ! enabled)
        {
            g.setColour (enabled ? col::screenDim : col::screenLine);
            g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
        }
        write (g, s, r.reduced (12.0f, 0.0f), grotesk (14.0f, 600), enabled ? (red ? juce::Colours::white : col::screenText) : col::screenDim,
               juce::Justification::centred);
    }

    // the cables: MIDI in dashes running forward, audio as a wave running back
    void midiCable (juce::Graphics& g, const juce::Path& p, double t, bool lit)
    {
        const auto c = lit ? col::sRed : col::screenGhost;
        g.setColour (c.withAlpha (lit ? 0.35f : 0.6f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
        if (! lit)
            return;
        // the notes, small squares running along
        const float len = p.getLength();
        for (int i = 0; i < 4; ++i)
        {
            const float d = std::fmod ((float) (t * 70.0) + (float) i * len / 4.0f, len);
            const auto pt = p.getPointAlongPath (d);
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pt));
        }
    }

    void audioCable (juce::Graphics& g, const juce::Path& p, double t, bool lit, float level = 0.4f)
    {
        const auto c = lit ? col::sBlue : col::screenGhost;
        g.setColour (c.withAlpha (lit ? 0.3f : 0.6f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
        if (! lit)
            return;
        const float len = p.getLength();
        if (len < 4.0f)
            return;
        juce::Path wave;
        const float amp = 2.0f + 5.0f * clamp01 (level);
        bool started = false;
        for (float d = 0.0f; d <= len; d += 2.0f)
        {
            const auto a = p.getPointAlongPath (juce::jmax (0.0f, d - 1.0f));
            const auto b = p.getPointAlongPath (juce::jmin (len, d + 1.0f));
            auto dir = b - a;
            const float n = std::sqrt (dir.x * dir.x + dir.y * dir.y);
            if (n < 1.0e-3f)
                continue;
            const juce::Point<float> normal (-dir.y / n, dir.x / n);
            const float edge = juce::jmin (1.0f, juce::jmin (d, len - d) / 18.0f);
            const float y = amp * edge * std::sin (d * 0.32f + (float) (t * 9.0));
            const auto pt = p.getPointAlongPath (d) + normal * y;
            if (! started) { wave.startNewSubPath (pt); started = true; }
            else wave.lineTo (pt);
        }
        g.setColour (c);
        g.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // a pulsing frame round what is to be set
    void pulse (juce::Graphics& g, juce::Rectangle<float> r, double t, float radius = 6.0f)
    {
        const float k = 0.5f + 0.5f * std::sin ((float) (t * 5.0));
        g.setColour (col::sBlue.withAlpha (0.25f + 0.55f * k));
        g.drawRoundedRectangle (r.expanded (2.0f + 1.5f * k), radius + 2.0f, 1.6f);
    }

    //====================================================================== the drawings (home made)
    void laptop (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour line, float thick = 1.4f)
    {
        auto base = r.removeFromBottom (r.getHeight() * 0.1f);
        g.setColour (line);
        g.drawRoundedRectangle (r.reduced (r.getWidth() * 0.06f, 0.0f), 7.0f, thick);
        juce::Path b;
        b.startNewSubPath (base.getX(), base.getY() + 2.0f);
        b.lineTo (base.getRight(), base.getY() + 2.0f);
        b.lineTo (base.getRight() - base.getWidth() * 0.03f, base.getBottom());
        b.lineTo (base.getX() + base.getWidth() * 0.03f, base.getBottom());
        b.closeSubPath();
        g.strokePath (b, juce::PathStrokeType (thick));
        g.drawLine (base.getCentreX() - 18.0f, base.getY() + 2.0f, base.getCentreX() + 18.0f, base.getY() + 2.0f, thick + 1.0f);
    }

    // each host as a small drawing of what it looks like at work (no logo)
    void dawGlyph (juce::Graphics& g, int d, juce::Rectangle<float> r, juce::Colour c)
    {
        r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
        const float u = r.getWidth() / 24.0f;
        auto at = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (r.getX() + x * u, r.getY() + y * u, w * u, h * u); };
        g.setColour (c);
        switch (d)
        {
            case 0:     // clips in a grid
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                    {
                        auto b = at (2.0f + (float) i * 7.0f, 3.0f + (float) j * 6.5f, 5.5f, 4.5f);
                        if ((i + j) % 2 == 0) g.fillRoundedRectangle (b, u);
                        else g.drawRoundedRectangle (b, u, 1.2f);
                    }
                break;
            case 1:     // lanes and their regions
                for (int j = 0; j < 4; ++j)
                {
                    g.drawLine (r.getX() + 2.0f * u, r.getY() + (4.0f + (float) j * 5.5f) * u, r.getRight() - 2.0f * u, r.getY() + (4.0f + (float) j * 5.5f) * u, 0.8f);
                    g.fillRoundedRectangle (at (3.0f + (float) ((j * 5) % 9), 1.6f + (float) j * 5.5f, 7.0f + (float) (j % 2) * 4.0f, 3.2f), u);
                }
                break;
            case 2:     // devices in a chain
                for (int i = 0; i < 3; ++i)
                {
                    g.drawRoundedRectangle (at (1.5f + (float) i * 7.5f, 7.0f, 6.0f, 10.0f), u, 1.3f);
                    g.fillEllipse (at (3.4f + (float) i * 7.5f, 9.0f, 2.2f, 2.2f));
                    if (i < 2) g.drawLine (r.getX() + (7.5f + (float) i * 7.5f) * u, r.getY() + 12.0f * u, r.getX() + (9.0f + (float) i * 7.5f) * u, r.getY() + 12.0f * u, 1.3f);
                }
                break;
            case 3:     // a wave and its envelope
            {
                juce::Path w, e;
                for (int i = 0; i <= 40; ++i)
                {
                    const float x = r.getX() + 2.0f * u + (float) i * 0.5f * u;
                    const float env = std::sin ((float) i / 40.0f * juce::MathConstants<float>::pi);
                    const float y = r.getCentreY() + std::sin ((float) i * 1.3f) * env * 6.0f * u;
                    if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
                    if (i == 0) e.startNewSubPath (x, r.getCentreY() - env * 7.5f * u); else e.lineTo (x, r.getCentreY() - env * 7.5f * u);
                }
                g.strokePath (w, juce::PathStrokeType (1.1f));
                g.strokePath (e, juce::PathStrokeType (1.6f));
                break;
            }
            case 4:     // faders
                for (int i = 0; i < 4; ++i)
                {
                    const float x = r.getX() + (4.0f + (float) i * 5.5f) * u;
                    g.drawLine (x, r.getY() + 3.0f * u, x, r.getBottom() - 3.0f * u, 1.0f);
                    g.fillRoundedRectangle (juce::Rectangle<float> (4.0f * u, 2.6f * u).withCentre ({ x, r.getY() + (7.0f + (float) ((i * 7) % 11)) * u }), 0.6f * u);
                }
                break;
            case 5:     // blocks laid out in time
                g.fillRoundedRectangle (at (2.0f, 4.0f, 9.0f, 4.0f), u);
                g.drawRoundedRectangle (at (12.0f, 4.0f, 10.0f, 4.0f), u, 1.2f);
                g.drawRoundedRectangle (at (5.0f, 10.0f, 12.0f, 4.0f), u, 1.2f);
                g.fillRoundedRectangle (at (2.0f, 16.0f, 6.0f, 4.0f), u);
                g.fillRoundedRectangle (at (10.0f, 16.0f, 12.0f, 4.0f), u);
                break;
            default:    // another one: a window
                g.drawRoundedRectangle (at (2.0f, 4.0f, 20.0f, 16.0f), 1.5f * u, 1.3f);
                g.drawLine (r.getX() + 2.0f * u, r.getY() + 8.0f * u, r.getX() + 22.0f * u, r.getY() + 8.0f * u, 1.0f);
                write (g, "?", at (2.0f, 8.0f, 20.0f, 12.0f), grotesk (10.0f * u, 500), c, juce::Justification::centred);
                break;
        }
    }

    // a synth in a plugin window, a synth in a box, the test synth
    void pluginSynth (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, double t)
    {
        g.setColour (c);
        g.drawRoundedRectangle (r, 5.0f, 1.3f);
        auto bar = r.removeFromTop (juce::jmin (11.0f, r.getHeight() * 0.18f));
        g.drawLine (bar.getX(), bar.getBottom(), bar.getRight(), bar.getBottom(), 1.0f);
        for (int i = 0; i < 3; ++i)
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ bar.getX() + 7.0f + (float) i * 6.0f, bar.getCentreY() }));
        auto in = r.reduced (r.getWidth() * 0.1f, r.getHeight() * 0.14f);
        auto knobs = in.removeFromRight (in.getWidth() * 0.3f);
        juce::Path w;
        for (int i = 0; i <= 30; ++i)
        {
            const float x = in.getX() + in.getWidth() * (float) i / 30.0f;
            const float ph = (float) i / 30.0f * juce::MathConstants<float>::twoPi * 2.0f;
            const float y = in.getCentreY() - in.getHeight() * 0.36f * (std::sin (ph) + 0.3f * std::sin (ph * 3.0f + (float) t)) / 1.3f;
            if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
        }
        g.strokePath (w, juce::PathStrokeType (1.4f));
        for (int i = 0; i < 2; ++i)
        {
            auto k = knobs.removeFromTop (knobs.getHeight() * (i == 0 ? 0.5f : 1.0f)).reduced (3.0f);
            k = k.withSizeKeepingCentre (juce::jmin (k.getWidth(), k.getHeight()), juce::jmin (k.getWidth(), k.getHeight()));
            g.drawEllipse (k, 1.2f);
            const float a = -2.2f + (float) i * 1.7f + 0.3f * std::sin ((float) t * 0.8f + (float) i);
            g.drawLine (k.getCentreX(), k.getCentreY(), k.getCentreX() + std::sin (a) * k.getWidth() * 0.45f, k.getCentreY() - std::cos (a) * k.getWidth() * 0.45f, 1.2f);
        }
    }

    void hardwareSynth (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, double t)
    {
        g.setColour (c);
        g.drawRoundedRectangle (r, 5.0f, 1.3f);
        auto top = r.removeFromTop (r.getHeight() * 0.48f).reduced (r.getWidth() * 0.07f, r.getHeight() * 0.1f);
        for (int i = 0; i < 5; ++i)
        {
            const float d = juce::jmin (top.getHeight() * 0.8f, top.getWidth() / 6.5f);
            auto k = juce::Rectangle<float> (d, d).withCentre ({ top.getX() + top.getWidth() * ((float) i + 0.5f) / 5.0f, top.getCentreY() });
            g.drawEllipse (k, 1.1f);
            const float a = -2.4f + (float) ((i * 37) % 11) * 0.45f + (i == 2 ? 0.5f * std::sin ((float) t * 1.3f) : 0.0f);
            g.drawLine (k.getCentreX(), k.getCentreY(), k.getCentreX() + std::sin (a) * d * 0.45f, k.getCentreY() - std::cos (a) * d * 0.45f, 1.1f);
        }
        auto keys = r.reduced (r.getWidth() * 0.05f, r.getHeight() * 0.12f);
        g.drawRect (keys, 1.0f);
        const int n = 10;
        for (int i = 1; i < n; ++i)
            g.drawVerticalLine ((int) (keys.getX() + keys.getWidth() * (float) i / (float) n), keys.getY(), keys.getBottom());
        for (int i = 0; i < n - 1; ++i)
            if (i % 7 != 2 && i % 7 != 6)
                g.fillRect (juce::Rectangle<float> (keys.getWidth() / (float) n * 0.55f, keys.getHeight() * 0.58f)
                                .withCentre ({ keys.getX() + keys.getWidth() * (float) (i + 1) / (float) n, keys.getY() + keys.getHeight() * 0.29f }));
    }

    void testSynth (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, double t)
    {
        g.setColour (c);
        g.drawRoundedRectangle (r, 5.0f, 1.3f);
        auto in = r.reduced (r.getWidth() * 0.14f, r.getHeight() * 0.25f);
        juce::Path w;
        for (int i = 0; i <= 40; ++i)
        {
            const float x = in.getX() + in.getWidth() * (float) i / 40.0f;
            const float y = in.getCentreY() - in.getHeight() * 0.5f * std::sin ((float) i / 40.0f * juce::MathConstants<float>::twoPi * 1.5f + (float) t * 2.0f);
            if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
        }
        g.strokePath (w, juce::PathStrokeType (1.5f));
    }

    // REPLIK8 as a block: its name over a red band
    void replikBlock (juce::Graphics& g, juce::Rectangle<float> r, bool lit)
    {
        g.setColour (col::screenGrid.brighter (0.3f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (lit ? col::sRed : col::screenDim);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, lit ? 1.8f : 1.0f);
        auto band = r.removeFromBottom (juce::jmin (12.0f, r.getHeight() * 0.3f)).reduced (6.0f, 2.0f);
        g.setColour (col::red);
        g.fillRoundedRectangle (band, 2.0f);
        write (g, "SAMPLER", band, grotesk (7.5f, 600), juce::Colours::white, juce::Justification::centred);
        write (g, "REPLIK8", r, grotesk (juce::jmin (15.0f, r.getHeight() * 0.5f), 700), col::screenText, juce::Justification::centred);
    }

    void label (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c, juce::Justification j = juce::Justification::centred)
    {
        write (g, s, r, grotesk (11.0f, 500), c, j);
    }

    const char* const dawNames[] = { "Ableton Live", "Logic Pro", "Bitwig Studio", "Reaper", "Cubase", "Studio One", "autre" };

    int dawOf (Host h)
    {
        switch (h)
        {
            case Host::live:      return 0;
            case Host::logic:     return 1;
            case Host::bitwig:    return 2;
            case Host::reaper:    return 3;
            case Host::cubase:    return 4;
            case Host::studioOne: return 5;
            case Host::other:
            case Host::app:       return 6;
        }
        return 6;
    }
}

//==============================================================================
CaptureAssistant::CaptureAssistant (ReplikantProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setVisible (false);
}

CaptureAssistant::~CaptureAssistant()
{
    stopTimer();
    if (proc.probing())
        proc.stopProbe();
}

bool CaptureAssistant::automatic (ReplikantProcessor& p)          { return p.settingsFile().getBoolValue ("assistant", true); }
void CaptureAssistant::setAutomatic (ReplikantProcessor& p, bool on)
{
    p.settingsFile().setValue ("assistant", on);
    p.settingsFile().saveIfNeeded();
}

void CaptureAssistant::open (int k)
{
    kind = juce::jlimit (0, 1, k);
    host = proc.hostInfo();
    inApp = host.host == Host::app;
    auto& set = proc.settingsFile();
    dawDetected = ! inApp && host.host != Host::other;
    daw = inApp ? -1 : (dawDetected ? dawOf (host.host) : set.getIntValue ("asDaw", -1));
    // what REPLIK8 is set to says where it was left
    const auto& o = proc.options;
    target = (Target) juce::jlimit (0, 3, set.getIntValue ("asTarget", 0));
    if (target == Target::none)
    {
        if (o.source == Src::testSynth)                                                 target = Target::test;
        else if (o.midiPortName.containsIgnoreCase ("IAC"))                              target = Target::plugin;
        else if (o.midiPort.isNotEmpty() && o.midiPort != "host")                        target = Target::hardware;
    }
    if (inApp && target == Target::plugin)
        target = Target::none;
    hwPort = set.getValue ("asPort");
    if (hwPort.isEmpty() && o.midiPort.isNotEmpty() && o.midiPort != "host" && ! o.midiPortName.containsIgnoreCase ("IAC"))
        hwPort = o.midiPort;
    inDevice = set.getValue ("asInDevice");
    inFirst = juce::jmax (0, set.getIntValue ("asInFirst", 0));
    inStereo = set.getBoolValue ("asInStereo", true);
    iacFound = false;
    scan (target == Target::hardware);
    rebuildSteps();
    at = 0;
    test = Test::idle;
    ready = false;
    retried = false;
    fixNote.clear();
    applied.clear();
    notesResult = {};
    ccResult = {};
    level[0] = level[1] = hold[0] = hold[1] = 0.0f;
    now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    openedAt = now;
    movedAt = now;
    moveDir = 1;
    railShown = 0.0f;
    setVisible (true);
    toFront (true);
    if (isShowing())
        grabKeyboardFocus();
    if (! manualClock)
        startTimerHz (30);
    repaint();
}

void CaptureAssistant::close()
{
    stopTimer();
    if (proc.probing())
        proc.stopProbe();
    setVisible (false);
    if (onClose)
        onClose();
}

//==============================================================================
void CaptureAssistant::rebuildSteps()
{
    steps.clear();
    if (! inApp)
        steps.push_back (Step::daw);
    steps.push_back (Step::target);
    if (target == Target::plugin)
    {
        steps.push_back (Step::iac);
        steps.push_back (Step::track);
    }
    else if (target == Target::hardware)
    {
        steps.push_back (Step::wiring);
        if (! inApp)
            steps.push_back (Step::track);
    }
    steps.push_back (Step::test);
    at = juce::jlimit (0, (int) steps.size() - 1, at);
}

bool CaptureAssistant::canGoOn() const
{
    switch (stepNow())
    {
        case Step::daw:     return daw >= 0;
        case Step::target:  return target != Target::none;
        case Step::iac:     return iacFound;
        case Step::wiring:  return hwPort.isNotEmpty();
        case Step::track:   return true;
        case Step::test:    return ready;
    }
    return false;
}

void CaptureAssistant::go (int s)
{
    s = juce::jlimit (0, (int) steps.size() - 1, s);
    if (s == at)
        return;
    if (proc.probing())
        proc.stopProbe();
    moveDir = s > at ? 1 : -1;
    at = s;
    movedAt = now;
    switch (stepNow())
    {
        case Step::iac:     scan (false); applyPlugin(); break;
        case Step::wiring:  scan (true); applyHardware(); break;
        case Step::test:
            if (target == Target::test) applyTest();
            if (test != Test::notes && test != Test::cc) { test = Test::idle; ready = false; }
            break;
        case Step::daw:
        case Step::target:
        case Step::track:
            break;
    }
    auto& set = proc.settingsFile();
    if (daw >= 0 && ! dawDetected)
        set.setValue ("asDaw", daw);
    set.setValue ("asTarget", (int) target);
    set.saveIfNeeded();
    repaint();
}

//============================================================================== what the Mac has
void CaptureAssistant::scan (bool audioToo)
{
    if (fakeMac)
        return;
    midiOuts = juce::MidiOutput::getAvailableDevices();
    const bool had = iacFound;
    iacFound = false;
    for (auto& d : midiOuts)
        if (d.name.containsIgnoreCase ("IAC") && (! iacFound || (d.name.contains ("1") && ! iac.name.contains ("1"))))
        {
            iac = d;
            iacFound = true;
        }
    if (iacFound && ! had)
    {
        iacFoundAt = now;
        if (stepNow() == Step::iac && isVisible())
            applyPlugin();
    }
    if (audioToo)
    {
        ins = proc.audioInputs (inDevice);
        if (inDevice.isEmpty() || ! ins.devices.contains (inDevice))
            inDevice = ins.shown;
        if (ins.app && ins.shown == ins.current && ! ins.active.isZero())
        {
            inFirst = juce::jmax (0, ins.active.findNextSetBit (0));
            inStereo = ins.active.countNumberOfSetBits() > 1;
        }
        inFirst = juce::jlimit (0, juce::jmax (0, ins.channels.size() - 1), inFirst);
        if (inFirst + 1 >= ins.channels.size())
            inStereo = false;
    }
    lastScan = now;
}

juce::String CaptureAssistant::dawName (int d) const
{
    if (d == -2)
        d = daw;
    if (inApp)
        return "l'appli REPLIK8"_fr;
    if (d < 0 || d > 6)
        return "ton DAW";
    if (d == 6)
        return host.hostName.isNotEmpty() && host.host == Host::other ? host.hostName : juce::String ("ton DAW");
    return dawNames[d];
}

juce::String CaptureAssistant::iacName() const
{
    return iacFound ? iac.name : "Gestionnaire IAC Bus 1"_fr;
}

juce::String CaptureAssistant::portName() const
{
    for (auto& d : midiOuts)
        if (d.identifier == hwPort)
            return d.name;
    return proc.options.midiPortName.isNotEmpty() ? proc.options.midiPortName : juce::String ("?");
}

juce::String CaptureAssistant::inputName (bool forHost) const
{
    const int a = inFirst + 1, b = inFirst + 2;
    auto nameOf = [this] (int i) { return juce::isPositiveAndBelow (i, ins.channels.size()) && ins.channels[i].isNotEmpty() ? ins.channels[i] : "Input " + juce::String (i + 1); };
    if (! forHost)
        return inStereo ? nameOf (inFirst) + " + " + nameOf (inFirst + 1) : nameOf (inFirst);
    switch (daw)
    {
        case 0:  return inStereo ? juce::String (a) + "/" + juce::String (b) : juce::String (a);
        case 1:  return inStereo ? "Input " + juce::String (a) + "-" + juce::String (b) : "Input " + juce::String (a);
        case 3:  return inStereo ? "Stereo › "_fr + nameOf (inFirst) + " / " + nameOf (inFirst + 1) : "Mono › "_fr + nameOf (inFirst);
        default: return inStereo ? "In " + juce::String (a) + "-" + juce::String (b) : "In " + juce::String (a);
    }
}

//============================================================================== REPLIK8's settings
void CaptureAssistant::applyPlugin()
{
    auto& o = proc.options;
    o.source = Src::input;
    o.monitor = 1;
    if (iacFound && o.midiPort != iac.identifier)
        proc.setMidiPort (iac.identifier, iac.name);
    else
        proc.settingsChanged();
    if (iacFound)
    {
        applied = "SOURCE input · MIDI out "_fr + lower (iac.name) + " · canal "_fr + juce::String (o.channel) + " · MONITOR direct"_fr;
        appliedAt = now;
    }
    if (onSettingsChanged)
        onSettingsChanged();
}

void CaptureAssistant::applyHardware()
{
    auto& o = proc.options;
    o.source = Src::input;
    o.monitor = 1;
    bool set = false;
    for (auto& d : midiOuts)
        if (d.identifier == hwPort)
        {
            if (o.midiPort != d.identifier)
                proc.setMidiPort (d.identifier, d.name);
            set = true;
        }
    if (! set)
        proc.settingsChanged();
    if (set)
    {
        applied = "SOURCE input · MIDI out "_fr + lower (portName()) + " · canal "_fr + juce::String (o.channel) + " · MONITOR direct"_fr;
        appliedAt = now;
    }
    proc.settingsFile().setValue ("asPort", hwPort);
    if (onSettingsChanged)
        onSettingsChanged();
}

void CaptureAssistant::applyTest()
{
    auto& o = proc.options;
    o.source = Src::testSynth;
    o.monitor = 1;
    proc.settingsChanged();
    applied = "SOURCE test synth · MONITOR direct"_fr;
    appliedAt = now;
    if (onSettingsChanged)
        onSettingsChanged();
}

void CaptureAssistant::setChannel (int c)
{
    proc.options.channel = juce::jlimit (1, 16, c);
    proc.settingsChanged();
    if (onSettingsChanged)
        onSettingsChanged();
}

void CaptureAssistant::applyInput()
{
    auto& set = proc.settingsFile();
    set.setValue ("asInDevice", inDevice);
    set.setValue ("asInFirst", inFirst);
    set.setValue ("asInStereo", inStereo);
    set.saveIfNeeded();
    if (! ins.app)
        return;
    juce::String why;
    if (proc.useAudioInput (inDevice, inFirst, inStereo, why))
    {
        applied = "l'appli enregistre "_fr + inDevice + " · "_fr + inputName (false);
        appliedAt = now;
        ins = proc.audioInputs (inDevice);
    }
    else if (say)
        say (why, true);
}

//============================================================================== the test
bool CaptureAssistant::ccWanted() const
{
    return kind == 1 && proc.cycleOptions.vary == 0;
}

void CaptureAssistant::startTest()
{
    juce::String why;
    if (! proc.startProbe (false, why))
    {
        if (say)
            say (why, true);
        return;
    }
    test = Test::notes;
    testFrom = now;
    ready = false;
    notesResult = {};
    ccResult = {};
    echoSeen = false;
}

void CaptureAssistant::testTick()
{
    if (test != Test::notes && test != Test::cc)
        return;
    const auto p = proc.probe();
    if (test == Test::notes)
        notesResult = p;
    else
        ccResult = p;
    if (p.state == ReplikantProcessor::Probe::done)
    {
        if (test == Test::notes)
        {
            // nothing came back where REPLIK8 listens, but something came by the other door: that one taken
            auto& o = proc.options;
            if (p.heard == 0 && ! retried && o.source != Src::testSynth)
            {
                if ((p.heardOn & 2) != 0 && o.source == Src::input)
                {
                    o.source = Src::sidechain;
                    fixNote = "Le son arrivait par le sidechain : SOURCE réglée sur SIDECHAIN."_fr;
                }
                else if ((p.heardOn & 1) != 0 && o.source == Src::sidechain)
                {
                    o.source = Src::input;
                    fixNote = "Le son arrivait par l'entrée principale : SOURCE réglée sur INPUT."_fr;
                }
                if (fixNote.isNotEmpty())
                {
                    retried = true;
                    proc.settingsChanged();
                    if (onSettingsChanged)
                        onSettingsChanged();
                    startTest();
                    return;
                }
            }
            if (ccWanted() && p.heard > 0)
            {
                juce::String why;
                if (proc.startProbe (true, why))
                {
                    test = Test::cc;
                    testFrom = now;
                    return;
                }
            }
        }
        finishTest();
    }
    else if (now - testFrom > 12.0)
    {
        proc.stopProbe();
        test = Test::done;
        doneAt = now;
        ready = false;
        notesResult.state = ReplikantProcessor::Probe::idle;     // (timed out)
    }
}

void CaptureAssistant::finishTest()
{
    test = Test::done;
    doneAt = now;
    const auto& r = notesResult;
    const bool heard = r.heard >= 2;
    const bool ccOk = ! ccWanted() || std::abs (ccResult.ccDb) > 1.5f || std::abs (ccResult.ccBright) > 0.12f;
    ready = heard && ! r.echoed && ! r.clipped && ccOk;
    if (ready)
        readyAt = now;
    // the calibration: a steady latency, every note set on it; else each attack looked for
    if (kind == 0 && heard)
    {
        float lo = 1.0e9f, hi = 0.0f;
        for (int i = 0; i < r.heard; ++i)
        {
            lo = juce::jmin (lo, r.latencyMs[i]);
            hi = juce::jmax (hi, r.latencyMs[i]);
        }
        proc.session.align = hi - lo < 1.0f && ! r.drone ? rk::Align::fixedLatency : rk::Align::perNote;
        proc.settingsChanged();
        if (onSettingsChanged)
            onSettingsChanged();
    }
}

//==============================================================================
void CaptureAssistant::timerCallback()
{
    tick (juce::Time::getMillisecondCounterHiRes() * 0.001 - now);
}

void CaptureAssistant::tick (double dt)
{
    now += dt;
    const auto s = stepNow();
    if ((s == Step::iac || s == Step::wiring) && now - lastScan > 1.0)
        scan (false);
    testTick();
    // the meters: up at once, down at 24 dB/s, the peak held 1.5 s
    const auto h = proc.heard();
    const float fall = std::pow (10.0f, -24.0f / 20.0f * (float) juce::jlimit (0.0, 0.2, dt));
    for (int c = 0; c < 2; ++c)
    {
        const float v = h.source[c];
        level[c] = v > level[c] ? v : level[c] * fall;
        if (v >= hold[c]) { hold[c] = v; holdAt[c] = now; }
        else if (now - holdAt[c] > 1.5) hold[c] *= fall;
    }
    mainLed = juce::jmax (juce::jmax (h.main[0], h.main[1]), mainLed * fall);
    sideLed = juce::jmax (juce::jmax (h.side[0], h.side[1]), sideLed * fall);
    echoSeen = echoSeen || h.echoed;
    const float railTarget = (float) at;
    railShown += (railTarget - railShown) * juce::jlimit (0.0f, 1.0f, (float) dt * 9.0f);
    repaint();
}

void CaptureAssistant::showStepNamed (const juce::String& name)
{
    const char* names[] = { "daw", "target", "iac", "wiring", "track", "test" };
    for (int i = 0; i < (int) steps.size(); ++i)
        if (name == names[(int) steps[(size_t) i]])
        {
            go (i);
            movedAt = now - 10.0;
            railShown = (float) at;
        }
}

void CaptureAssistant::chooseForShot (int d, int t)
{
    manualClock = true;
    stopTimer();
    if (d >= 0)
        daw = d;
    target = (Target) t;
    if (target == Target::test)
        applyTest();
    rebuildSteps();
}

void CaptureAssistant::runTestForShot()
{
    startTest();
}

void CaptureAssistant::fakeForShot (int d, const juce::String& name, const juce::String& format, const juce::String& track, bool devices)
{
    manualClock = true;
    stopTimer();
    const Host hosts[] = { Host::live, Host::logic, Host::bitwig, Host::reaper, Host::cubase, Host::studioOne, Host::other };
    if (d >= 0)
    {
        daw = d;
        dawDetected = name.isNotEmpty();
        host.host = hosts[juce::jlimit (0, 6, d)];
        host.hostName = name;
        host.format = format;
    }
    host.trackName = track;
    if (devices)
    {
        fakeMac = true;
        midiOuts.clear();
        midiOuts.add (juce::MidiDeviceInfo ("Minilogue xd", "mxd"));
        midiOuts.add (juce::MidiDeviceInfo ("Scarlett 4i4 USB", "s4i4"));
        midiOuts.add (juce::MidiDeviceInfo ("Gestionnaire IAC Bus 1"_fr, "iac1"));
        iac = midiOuts[2];
        iacFound = true;
        iacFoundAt = now - 5.0;
        ins.devices = { "Scarlett 4i4 USB", "Micro MacBook Pro" };
        ins.shown = ins.devices[0];
        ins.channels = { "Input 1", "Input 2", "Input 3", "Input 4" };
        inDevice = ins.shown;
        inFirst = 2;
        inStereo = true;
        hwPort = "mxd";
    }
}

//============================================================================== the mouse
void CaptureAssistant::mouseMove (const juce::MouseEvent& e)     { mouse = e.position; repaint(); }
void CaptureAssistant::mouseExit (const juce::MouseEvent&)       { mouse = { -1.0f, -1.0f }; repaint(); }

void CaptureAssistant::mouseDown (const juce::MouseEvent& e)
{
    mouse = e.position;
    const auto copy = hits;
    for (auto it = copy.rbegin(); it != copy.rend(); ++it)
        if (it->r.contains (e.position))
        {
            if (it->click)
                it->click();
            repaint();
            return;
        }
}

bool CaptureAssistant::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (k == juce::KeyPress::returnKey && canGoOn() && stepNow() != Step::test)
    {
        go (at + 1);
        return true;
    }
    if (k == juce::KeyPress::leftKey && at > 0)
    {
        go (at - 1);
        return true;
    }
    return false;
}

//============================================================================== drawing
void CaptureAssistant::paint (juce::Graphics& g)
{
    hits.clear();
    auto r = getLocalBounds().toFloat();
    // the panel: a screen over the screen, a shadow under it
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 16.0f);
    te::screen (g, r, 14.0f);
    auto in = r.reduced (24.0f, 14.0f);
    paintHeader (g, in.removeFromTop (50.0f));
    paintFooter (g, in.removeFromBottom (48.0f));
    in.removeFromTop (14.0f);
    in.removeFromBottom (10.0f);

    // the step: slid in from the side it comes from
    const float k = easeOut ((float) ((now - movedAt) / slide));
    const bool sliding = k < 0.999f;
    juce::Graphics::ScopedSaveState state (g);
    if (sliding)
    {
        g.addTransform (juce::AffineTransform::translation ((1.0f - k) * 40.0f * (float) moveDir, 0.0f));
        g.beginTransparencyLayer (k);
    }
    auto left = in.removeFromLeft (in.getWidth() * 0.5f - 12.0f);
    in.removeFromLeft (24.0f);
    const auto hitsBefore = hits.size();
    paintStep (g, left, in);
    if (sliding)
    {
        g.endTransparencyLayer();
        hits.resize (hitsBefore);        // (nothing to click while it moves)
    }
}

void CaptureAssistant::paintHeader (juce::Graphics& g, juce::Rectangle<float> r)
{
    // the chip, as the screen's CAPTURE
    auto chipR = r.removeFromLeft (128.0f).withSizeKeepingCentre (128.0f, 26.0f).withY (r.getY() + 4.0f);
    g.setColour (col::red);
    g.fillRoundedRectangle (chipR, 5.0f);
    picto (g, Picto::jack, chipR.removeFromLeft (30.0f).reduced (6.0f), juce::Colours::white, 1.4f);
    write (g, "ASSISTANT", chipR.withTrimmedRight (8.0f), grotesk (12.0f, 600), juce::Colours::white, juce::Justification::centred);
    r.removeFromLeft (14.0f);
    write (g, kind == 0 ? "CAPTURE NOTES" : "CAPTURE CYCLES", r.removeFromLeft (170.0f).withHeight (34.0f), grotesk (12.0f, 500), col::screenDim);

    // close
    auto x = r.removeFromRight (34.0f).withHeight (34.0f);
    const bool overX = hot (x);
    g.setColour (overX ? col::screenLine : col::screenGrid.brighter (0.2f));
    g.fillRoundedRectangle (x, 8.0f);
    crossMark (g, x.getCentre(), 16.0f, overX ? col::screenText : col::screenDim, 1.6f);
    hit (x, [this] { close(); });
    r.removeFromRight (20.0f);

    // the rail: one node a step, the line filled as far as here
    const int n = (int) steps.size();
    auto rail = r.removeFromRight (juce::jmin (r.getWidth(), 120.0f * (float) n));
    const float y = rail.getY() + 13.0f;
    const float x0 = rail.getX() + 40.0f, x1 = rail.getRight() - 40.0f;
    auto nodeX = [&] (float i) { return n > 1 ? x0 + (x1 - x0) * i / (float) (n - 1) : (x0 + x1) * 0.5f; };
    g.setColour (col::screenLine);
    g.drawLine (x0, y, x1, y, 2.0f);
    g.setColour (col::sRed);
    g.drawLine (x0, y, nodeX (juce::jlimit (0.0f, (float) (n - 1), railShown)), y, 2.0f);
    const char* names[] = { "logiciel", "source", "midi", "branchements", "piste", "test" };
    for (int i = 0; i < n; ++i)
    {
        const juce::Point<float> c (nodeX ((float) i), y);
        auto dot = juce::Rectangle<float> (22.0f, 22.0f).withCentre (c);
        const bool done = i < at, here = i == at;
        if (here)
        {
            const float k = (float) std::fmod (now * 0.9, 1.0);
            g.setColour (col::sRed.withAlpha (0.35f * (1.0f - k)));
            g.drawEllipse (dot.expanded (2.0f + 8.0f * k), 1.5f);
        }
        g.setColour (done ? col::sRed : col::screen);
        g.fillEllipse (dot);
        g.setColour (done || here ? col::sRed : col::screenLine);
        g.drawEllipse (dot.reduced (0.75f), here ? 2.0f : 1.4f);
        if (done)
            tickMark (g, c, 12.0f, juce::Colours::white, 1.8f);
        else
            write (g, juce::String (i + 1), dot, grotesk (11.0f, 600), here ? col::screenText : col::screenDim, juce::Justification::centred);
        write (g, names[(int) steps[(size_t) i]], juce::Rectangle<float> (110.0f, 14.0f).withCentre ({ c.x, y + 23.0f }), grotesk (10.5f, here ? 600 : 400),
               here ? col::screenText : (done ? col::screenDim : col::screenGhost.brighter (0.4f)), juce::Justification::centred);
        if (done)
            hit (dot.expanded (8.0f), [this, i] { go (i); });
    }
}

void CaptureAssistant::paintFooter (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::screenLine);
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    r.removeFromTop (10.0f);
    // opened by itself, or not
    {
        const bool on = automatic (proc);
        auto box = r.removeFromLeft (250.0f);
        auto sq = box.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        const bool over = hot (box.withLeft (sq.getX()));
        g.setColour (over ? col::screenDim : col::screenLine);
        g.drawRoundedRectangle (sq, 4.0f, 1.3f);
        if (on)
            tickMark (g, sq.getCentre(), 12.0f, col::sRed, 1.8f);
        write (g, "s'ouvre tout seul (SETTINGS)"_fr, box.withTrimmedLeft (10.0f), grotesk (12.5f, 400), over ? col::screenText : col::screenDim);
        hit (box.withLeft (sq.getX()), [this] { setAutomatic (proc, ! automatic (proc)); });
    }
    const auto s = stepNow();
    // the keys
    auto next = r.removeFromRight (230.0f);
    if (s == Step::test)
    {
        if (ready)
        {
            bigKey (g, next, "lancer la capture", hot (next), true);
            hit (next, [this] { auto f = onStart; close(); if (f) f(); });
        }
        else if (test == Test::notes || test == Test::cc)
            bigKey (g, next, "test en cours…"_fr, false, false);
        else
        {
            bigKey (g, next, test == Test::done ? "refaire le test" : "tester", hot (next), true);
            hit (next, [this] { startTest(); });
            if (test == Test::done)
            {
                r.removeFromRight (10.0f);
                auto anyway = r.removeFromRight (170.0f);
                bigKey (g, anyway, "lancer quand même"_fr, hot (anyway), true, false);
                hit (anyway, [this] { auto f = onStart; close(); if (f) f(); });
            }
        }
    }
    else
    {
        const bool ok = canGoOn();
        bigKey (g, next, "suivant  →"_fr, hot (next) && ok, ok);
        if (ok)
        {
            // (a nudge when it's the thing to do)
            const float k = 0.5f + 0.5f * std::sin ((float) (now * 3.0));
            g.setColour (col::sRed.withAlpha (0.25f * k));
            g.drawRoundedRectangle (next.expanded (3.0f), 11.0f, 2.0f);
            hit (next, [this] { go (at + 1); });
        }
    }
    if (at > 0)
    {
        r.removeFromRight (10.0f);
        auto back = r.removeFromRight (120.0f);
        bigKey (g, back, "←  retour"_fr, hot (back), true, false);
        hit (back, [this] { go (at - 1); });
    }
    // a word for the step
    const juce::String words[] = { "Reconnu tout seul quand c'est possible.", "Tu pourras changer plus tard.", "macOS l'a déjà : il suffit de l'allumer."_fr,
                            "Les noms viennent de ton Mac.", "En or : ce qu'il y a à régler chez toi."_fr, "Rien n'est enregistré pendant le test."_fr };
    write (g, words[(int) s], r.reduced (16.0f, 0.0f), grotesk (12.5f, 400), col::screenDim, juce::Justification::centred);
}

// the eyebrow, the title, what it's about; the space left under them
static juce::Rectangle<float> heading (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& eyebrow, const juce::String& title, const juce::String& sub)
{
    write (g, eyebrow, r.removeFromTop (16.0f), grotesk (11.0f, 600), col::sRed);
    write (g, title, r.removeFromTop (40.0f), grotesk (30.0f, 500), col::screenText);
    if (sub.isNotEmpty())
    {
        auto a = rich (sub, 14.0f, col::screenDim, col::sBlue);
        const float h = drawRich (g, a, r.withHeight (60.0f));
        r.removeFromTop (h + 4.0f);
    }
    r.removeFromTop (12.0f);
    return r;
}

void CaptureAssistant::paintStep (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    switch (stepNow())
    {
        case Step::daw:     paintDaw (g, left, right); break;
        case Step::target:  paintTarget (g, left, right); break;
        case Step::iac:     paintIac (g, left, right); break;
        case Step::wiring:  paintWiring (g, left, right); break;
        case Step::track:   paintTrack (g, left, right); break;
        case Step::test:    paintTest (g, left, right); break;
    }
}

// numbered lines, [[gold]] where something is to be set; the first `done` ticked
float CaptureAssistant::lines (juce::Graphics& g, juce::Rectangle<float> r, const juce::StringArray& ls, int done)
{
    const float top = r.getY();
    for (int i = 0; i < ls.size(); ++i)
    {
        auto row = r;
        auto bullet = row.removeFromLeft (30.0f).removeFromTop (22.0f).withSizeKeepingCentre (22.0f, 22.0f);
        g.setColour (i < done ? col::sGreen.withAlpha (0.2f) : col::screenGrid.brighter (0.25f));
        g.fillEllipse (bullet);
        g.setColour (i < done ? col::sGreen : col::screenLine);
        g.drawEllipse (bullet.reduced (0.6f), 1.2f);
        if (i < done)
            tickMark (g, bullet.getCentre(), 11.0f, col::sGreen, 1.6f);
        else
            write (g, juce::String (i + 1), bullet, grotesk (11.0f, 600), col::screenText, juce::Justification::centred);
        row.removeFromLeft (8.0f);
        const float h = drawRich (g, rich (ls[i], 14.5f, col::screenText, col::sBlue), row.withTrimmedTop (1.0f));
        r.removeFromTop (juce::jmax (24.0f, h + 2.0f) + 9.0f);
    }
    return r.getY() - top;
}

//============================================================================== LOGICIEL
void CaptureAssistant::paintDaw (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    const juce::String sub = dawDetected ? "REPLIK8 tourne dans [["_fr + host.hostName + "]] (" + host.format + ") sur ton Mac. Pas le bon ? Choisis."
                                         : "Je n'ai pas reconnu ton logiciel : choisis-le.";
    auto r = heading (g, left, "ÉTAPE 1 · LOGICIEL"_fr, "Dans quel logiciel ?"_fr, sub);
    const float cw = (r.getWidth() - 3.0f * 10.0f) / 4.0f, ch = 78.0f;
    for (int i = 0; i < 7; ++i)
    {
        auto c = juce::Rectangle<float> (r.getX() + (float) (i % 4) * (cw + 10.0f), r.getY() + (float) (i / 4) * (ch + 10.0f), cw, ch);
        const bool on = daw == i, over = hot (c);
        pane (g, over && ! on ? c.translated (0.0f, -1.5f) : c, on, over);
        auto in = c.reduced (12.0f, 10.0f);
        dawGlyph (g, i, in.removeFromTop (32.0f).removeFromLeft (32.0f), on ? col::screenText : (over ? col::screenText : col::screenDim));
        write (g, juce::String::fromUTF8 (dawNames[i]), in.removeFromTop (22.0f), grotesk (14.0f, on ? 600 : 500), on || over ? col::screenText : col::screenDim);
        if (dawDetected && dawOf (host.host) == i)
        {
            auto tag = juce::Rectangle<float> (c.getRight() - 64.0f, c.getY() + 9.0f, 54.0f, 15.0f);
            g.setColour (col::sGreen.withAlpha (0.18f));
            g.fillRoundedRectangle (tag, 7.5f);
            write (g, "détecté"_fr, tag, grotesk (9.5f, 600), col::sGreen, juce::Justification::centred);
        }
        hit (c, [this, i] { daw = i; repaint(); });
    }
    if (host.trackName.isNotEmpty())
        write (g, "sur la piste « "_fr + host.trackName + " »"_fr, r.withTrimmedTop (2.0f * ch + 24.0f).removeFromTop (20.0f), grotesk (13.0f, 400), col::screenDim);

    // the Mac, the host on its screen, REPLIK8 in it
    auto m = right.withSizeKeepingCentre (juce::jmin (right.getWidth(), 420.0f), juce::jmin (right.getHeight() - 40.0f, 270.0f)).translated (0.0f, -14.0f);
    laptop (g, m, col::screenDim, 1.6f);
    auto scr = m.withTrimmedBottom (m.getHeight() * 0.1f).reduced (m.getWidth() * 0.06f + 10.0f, 10.0f);
    {
        // the scan across the glass
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (scr.toNearestInt());
        const float yScan = scr.getY() + scr.getHeight() * (float) std::fmod (now * 0.35, 1.0);
        g.setGradientFill (juce::ColourGradient (col::sRed.withAlpha (0.0f), 0.0f, yScan - 30.0f, col::sRed.withAlpha (0.1f), 0.0f, yScan, false));
        g.fillRect (scr.withTop (yScan - 30.0f).withHeight (30.0f));
    }
    const int d = daw >= 0 ? daw : 6;
    dawGlyph (g, d, scr.withTrimmedBottom (40.0f).reduced (20.0f), daw >= 0 ? col::screenText : col::screenGhost.brighter (0.5f));
    replikBlock (g, juce::Rectangle<float> (96.0f, 34.0f).withCentre ({ scr.getCentreX(), scr.getBottom() - 24.0f }), daw >= 0);
    auto cap = right.withTop (m.getBottom() + 10.0f).removeFromTop (20.0f);
    write (g, daw >= 0 ? dawName() + (dawDetected && dawOf (host.host) == daw ? " · "_fr + host.format : juce::String()) : juce::String ("?"), cap, grotesk (13.0f, 500),
           col::screenDim, juce::Justification::centred);
}

//============================================================================== SOURCE
void CaptureAssistant::paintTarget (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    auto r = heading (g, left, inApp ? "ÉTAPE 1 · SOURCE"_fr : "ÉTAPE 2 · SOURCE"_fr, "Que veux-tu sampler ?", {});
    struct Choice { Target t; juce::String name; juce::String what; bool enabled; };
    const Choice cs[] = {
        { Target::plugin, "Un plugin", inApp ? juce::String ("dans un DAW seulement") : "un synthé logiciel, dans "_fr + dawName(), ! inApp },
        { Target::hardware, "Un synthé hardware"_fr, "branché à ta carte son, piloté en MIDI"_fr, true },
        { Target::test, "Le synthé de test"_fr, "pour essayer, sans rien brancher"_fr, true } };
    const float h = juce::jmin (88.0f, (r.getHeight() - 20.0f) / 3.0f);
    Target shown = target;
    for (int i = 0; i < 3; ++i)
    {
        auto c = r.removeFromTop (h);
        r.removeFromTop (10.0f);
        const auto& ch = cs[i];
        const bool on = target == ch.t, over = hot (c) && ch.enabled;
        if (over)
            shown = ch.t;
        pane (g, over && ! on ? c.translated (3.0f, 0.0f) : c, on, over, ch.enabled);
        auto in = c.reduced (14.0f, 10.0f);
        auto pic = in.removeFromLeft (104.0f).reduced (4.0f, 4.0f);
        const auto ink = (on || over ? col::screenText : col::screenDim).withMultipliedAlpha (ch.enabled ? 1.0f : 0.4f);
        if (ch.t == Target::plugin)         pluginSynth (g, pic, ink, on ? now : 0.0);
        else if (ch.t == Target::hardware)  hardwareSynth (g, pic, ink, on ? now : 0.0);
        else                                testSynth (g, pic, ink, on ? now : 0.0);
        in.removeFromLeft (16.0f);
        auto textArea = in.withSizeKeepingCentre (in.getWidth(), 46.0f);
        write (g, ch.name, textArea.removeFromTop (24.0f), grotesk (18.0f, 600), ink);
        write (g, ch.what, textArea, grotesk (13.0f, 400), col::screenDim.withMultipliedAlpha (ch.enabled ? 1.0f : 0.5f));
        if (on)
        {
            auto badge = juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ c.getRight() - 22.0f, c.getCentreY() });
            g.setColour (col::sRed);
            g.fillEllipse (badge);
            tickMark (g, badge.getCentre(), 12.0f, juce::Colours::white, 1.8f);
        }
        if (ch.enabled)
            hit (c, [this, t = ch.t]
            {
                target = t;
                if (t == Target::test)
                    applyTest();
                if (t == Target::hardware)
                    scan (true);
                rebuildSteps();
                proc.settingsFile().setValue ("asTarget", (int) target);
                repaint();
            });
    }
    // the way it will go, for the one chosen (or under the mouse)
    const auto keep = target;
    target = shown;
    paintFlow (g, right, true, true);
    target = keep;
}

//============================================================================== the way it goes: REPLIK8, the synth, the cables
void CaptureAssistant::paintFlow (juce::Graphics& g, juce::Rectangle<float> r, bool midiLit, bool audioLit)
{
    const float lv = juce::jmax (level[0], level[1]);
    auto caption = [&] (const juce::String& s, juce::Rectangle<float> a, juce::Colour c) { label (g, s, a, c); };
    if (target == Target::none)
    {
        write (g, "choisis une source"_fr, r, grotesk (14.0f, 400), col::screenGhost.brighter (0.5f), juce::Justification::centred);
        return;
    }
    if (target == Target::test)
    {
        auto c = r.getCentre();
        auto rb = juce::Rectangle<float> (130.0f, 46.0f).withCentre (c.translated (0.0f, 50.0f));
        auto ts = juce::Rectangle<float> (110.0f, 66.0f).withCentre (c.translated (0.0f, -70.0f));
        testSynth (g, ts, col::screenText, now);
        caption ("le synthé de test (intégré)"_fr, ts.translated (0.0f, -46.0f).withHeight (16.0f).expanded (60.0f, 0.0f), col::screenDim);
        replikBlock (g, rb, true);
        juce::Path down, up;
        down.startNewSubPath (rb.getX() + 20.0f, rb.getY());
        down.cubicTo (rb.getX() - 40.0f, rb.getY() - 30.0f, ts.getX() - 40.0f, ts.getBottom() + 20.0f, ts.getX() + 10.0f, ts.getBottom());
        up.startNewSubPath (ts.getRight() - 10.0f, ts.getBottom());
        up.cubicTo (ts.getRight() + 40.0f, ts.getBottom() + 20.0f, rb.getRight() + 40.0f, rb.getY() - 30.0f, rb.getRight() - 20.0f, rb.getY());
        midiCable (g, down, now, midiLit);
        audioCable (g, up, now, audioLit, lv);
        caption ("notes", { rb.getX() - 120.0f, c.y - 22.0f, 80.0f, 14.0f }, col::sRed);
        caption ("son", { rb.getRight() + 40.0f, c.y - 22.0f, 80.0f, 14.0f }, col::sBlue);
        return;
    }
    if (target == Target::plugin)
    {
        // the Mac; in it, the synth's track (the synth, then REPLIK8) and the IAC bus
        auto mac = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 520.0f), juce::jmin (r.getHeight(), 330.0f));
        laptop (g, mac, col::screenLine.brighter (0.4f), 1.3f);
        auto scr = mac.withTrimmedBottom (mac.getHeight() * 0.1f).reduced (mac.getWidth() * 0.06f + 16.0f, 16.0f);
        caption (dawName() + " · ton Mac"_fr, scr.removeFromTop (16.0f), col::screenDim);
        auto track = scr.removeFromLeft (scr.getWidth() * 0.56f).reduced (6.0f, 8.0f);
        g.setColour (col::screenGrid.brighter (0.25f));
        g.fillRoundedRectangle (track, 8.0f);
        g.setColour (col::screenLine);
        g.drawRoundedRectangle (track, 8.0f, 1.0f);
        auto th = track.removeFromTop (22.0f);
        label (g, host.trackName.isNotEmpty() ? host.trackName : juce::String ("piste du synthé"_fr), th.reduced (10.0f, 0.0f), col::screenText, juce::Justification::centredLeft);
        auto slots = track.reduced (12.0f, 8.0f);
        auto synth = slots.removeFromTop (slots.getHeight() * 0.5f).reduced (0.0f, 6.0f);
        auto rep = slots.reduced (0.0f, 6.0f);
        pluginSynth (g, synth.withSizeKeepingCentre (juce::jmin (synth.getWidth(), 120.0f), synth.getHeight()), col::screenText, now);
        replikBlock (g, rep.withSizeKeepingCentre (juce::jmin (rep.getWidth(), 120.0f), juce::jmin (rep.getHeight(), 44.0f)), true);
        auto bus = scr.reduced (8.0f, 0.0f).withSizeKeepingCentre (scr.getWidth() - 16.0f, 34.0f).translated (0.0f, -20.0f);
        const bool waiting = stepNow() == Step::iac && ! iacFound;
        g.setColour (iacFound ? col::sRed.withAlpha (0.14f) : col::screenGrid.brighter (0.2f));
        g.fillRoundedRectangle (bus, 17.0f);
        g.setColour (waiting ? col::screenDim.withAlpha (0.5f + 0.5f * std::sin ((float) now * 4.0f)) : (iacFound ? col::sRed : col::screenLine));
        g.drawRoundedRectangle (bus, 17.0f, 1.4f);
        label (g, "bus IAC", bus.withTrimmedBottom (14.0f).translated (0.0f, 2.0f), col::screenDim);
        label (g, iacFound ? lower (iac.name) : juce::String ("éteint"_fr), bus.withTrimmedTop (14.0f).translated (0.0f, -2.0f), iacFound ? col::screenText : col::screenDim);
        if (stepNow() == Step::iac)
            pulse (g, bus, now, 17.0f);
        // REPLIK8 -> the bus -> the track's MIDI in -> the synth; the synth -> REPLIK8
        juce::Path toBus, toSynth, back;
        const auto repR = rep.withSizeKeepingCentre (juce::jmin (rep.getWidth(), 120.0f), juce::jmin (rep.getHeight(), 44.0f));
        const auto synR = synth.withSizeKeepingCentre (juce::jmin (synth.getWidth(), 120.0f), synth.getHeight());
        toBus.startNewSubPath (repR.getRight(), repR.getCentreY());
        toBus.cubicTo (bus.getX() + 10.0f, repR.getCentreY(), bus.getCentreX() - 20.0f, bus.getBottom() + 30.0f, bus.getCentreX(), bus.getBottom());
        toSynth.startNewSubPath (bus.getCentreX(), bus.getY());
        toSynth.cubicTo (bus.getCentreX(), synR.getY() - 30.0f, synR.getRight() + 50.0f, synR.getY() - 10.0f, synR.getRight(), synR.getCentreY() - 8.0f);
        back.startNewSubPath (synR.getCentreX(), synR.getBottom());
        back.lineTo (repR.getCentreX(), repR.getY());
        midiCable (g, toBus, now, midiLit && iacFound);
        midiCable (g, toSynth, now + 0.4, midiLit && iacFound);
        audioCable (g, back, now, audioLit, lv);
        caption ("notes", { bus.getRight() - 70.0f, bus.getY() - 44.0f, 70.0f, 14.0f }, col::sRed);
        caption ("son", { repR.getCentreX() + 10.0f, synR.getBottom() + 2.0f, 40.0f, 14.0f }, col::sBlue);
        return;
    }
    // hardware: REPLIK8 in the Mac (in the host, or the app), the synth beside it, the sound card between
    auto mac = r.removeFromLeft (r.getWidth() * 0.5f).withSizeKeepingCentre (juce::jmin (r.getWidth() * 0.5f, 270.0f), 190.0f).translated (0.0f, -40.0f);
    laptop (g, mac, col::screenLine.brighter (0.4f), 1.3f);
    auto scr = mac.withTrimmedBottom (mac.getHeight() * 0.1f).reduced (mac.getWidth() * 0.06f + 12.0f, 12.0f);
    caption (inApp ? juce::String ("l'appli REPLIK8"_fr) : dawName(), scr.removeFromTop (16.0f), col::screenDim);
    auto rep = scr.withSizeKeepingCentre (120.0f, 46.0f);
    replikBlock (g, rep, true);
    auto syn = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 20.0f, 200.0f), 96.0f).translated (0.0f, -40.0f);
    hardwareSynth (g, syn, col::screenText, now);
    caption ("ton synthé"_fr, syn.translated (0.0f, -20.0f).withHeight (16.0f), col::screenDim);
    auto card = juce::Rectangle<float> (220.0f, 52.0f).withCentre ({ (mac.getCentreX() + syn.getCentreX()) * 0.5f, r.getBottom() - 50.0f });
    g.setColour (col::screenGrid.brighter (0.25f));
    g.fillRoundedRectangle (card, 8.0f);
    g.setColour (col::screenLine.brighter (0.3f));
    g.drawRoundedRectangle (card, 8.0f, 1.2f);
    for (int i = 0; i < 4; ++i)
    {
        auto jack = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ card.getX() + 24.0f + (float) i * 20.0f, card.getCentreY() + 6.0f });
        const bool used = i == juce::jlimit (0, 3, inFirst) || (inStereo && i == juce::jlimit (0, 3, inFirst + 1));
        g.setColour (used ? col::sBlue : col::screenDim);
        g.drawEllipse (jack, 1.4f);
        if (used)
            g.fillEllipse (jack.reduced (3.5f));
    }
    label (g, inDevice.isNotEmpty() ? inDevice : juce::String ("ta carte son"), card.withTrimmedBottom (26.0f).withTrimmedLeft (6.0f), col::screenText, juce::Justification::centred);
    label (g, ins.channels.isEmpty() ? juce::String() : inputName (false), card.withTrimmedLeft (100.0f).withTrimmedTop (22.0f), col::sBlue, juce::Justification::centredLeft);
    juce::Path midi, audio1, audio2;
    midi.startNewSubPath (rep.getRight(), rep.getCentreY());
    midi.cubicTo (mac.getRight() + 30.0f, rep.getCentreY(), syn.getX() - 40.0f, syn.getCentreY() - 10.0f, syn.getX(), syn.getCentreY() - 10.0f);
    audio1.startNewSubPath (syn.getCentreX(), syn.getBottom());
    audio1.cubicTo (syn.getCentreX(), card.getCentreY(), card.getRight() + 40.0f, card.getCentreY(), card.getRight(), card.getCentreY());
    audio2.startNewSubPath (card.getX(), card.getCentreY());
    audio2.cubicTo (mac.getCentreX() - 40.0f, card.getCentreY(), mac.getCentreX(), mac.getBottom() + 20.0f, mac.getCentreX(), mac.getBottom());
    midiCable (g, midi, now, midiLit && hwPort.isNotEmpty());
    audioCable (g, audio1, now, audioLit, lv);
    audioCable (g, audio2, now + 0.3, audioLit, lv);
    write (g, hwPort.isNotEmpty() ? "MIDI · "_fr + portName() + "\ncanal "_fr + juce::String (proc.options.channel) : juce::String ("MIDI\nquel port ?"_fr),
           { mac.getRight() + 2.0f, rep.getCentreY() + 10.0f, juce::jmax (60.0f, syn.getX() - mac.getRight() - 4.0f), 30.0f }, grotesk (11.0f, 500),
           hwPort.isNotEmpty() ? col::sRed : col::screenDim, juce::Justification::centredTop, 2);
}

//============================================================================== MIDI (the IAC bus)
void CaptureAssistant::paintIac (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    auto r = heading (g, left, "ÉTAPE "_fr + juce::String (at + 1) + " · MIDI"_fr, "Le câble MIDI du Mac"_fr,
                      "REPLIK8 joue ton synthé par le [[bus IAC]], un câble MIDI virtuel intégré à macOS : ses notes vont d'un logiciel à l'autre."_fr);
    if (iacFound)
    {
        auto row = r.removeFromTop (44.0f);
        const float k = easeBack ((float) ((now - iacFoundAt) / 0.45));
        auto badge = juce::Rectangle<float> (34.0f, 34.0f).withCentre ({ row.getX() + 17.0f, row.getCentreY() });
        g.setColour (col::sGreen.withAlpha (0.2f));
        g.fillEllipse (badge.withSizeKeepingCentre (34.0f * k, 34.0f * k));
        tickMark (g, badge.getCentre(), 18.0f * k, col::sGreen, 2.4f);
        auto t = row.withTrimmedLeft (48.0f);
        write (g, "Trouvé : « "_fr + iac.name + " »"_fr, t.removeFromTop (24.0f), grotesk (17.0f, 600), col::screenText);
        write (g, "REPLIK8 s'en sert déjà comme sortie MIDI."_fr, t, grotesk (13.0f, 400), col::screenDim);
        r.removeFromTop (16.0f);
        // what was set, and the channel
        juce::StringArray set { "SOURCE : [[input]] (le son entre par la piste où REPLIK8 est posé)"_fr,
                                "MIDI out : [[" + iac.name + "]]",
                                "MONITOR : [[direct]] (tu entends le synthé tant que la page CAPTURE est ouverte)"_fr };
        r.removeFromTop (lines (g, r, set, 3));
        auto chRow = r.removeFromTop (30.0f);
        write (g, "canal MIDI", chRow.removeFromLeft (110.0f), grotesk (13.5f, 500), col::screenDim);
        auto minus = chRow.removeFromLeft (30.0f), value = chRow.removeFromLeft (50.0f), plus = chRow.removeFromLeft (30.0f);
        chip (g, minus, "-", false, hot (minus));
        write (g, juce::String (proc.options.channel), value, grotesk (17.0f, 600), col::screenText, juce::Justification::centred);
        chip (g, plus, "+", false, hot (plus));
        hit (minus, [this] { setChannel (proc.options.channel - 1); });
        hit (plus, [this] { setChannel (proc.options.channel + 1); });
        write (g, "la piste du synthé écoute tous les canaux : 1 va très bien"_fr, chRow.withTrimmedLeft (14.0f), grotesk (12.0f, 400), col::screenDim);
    }
    else
    {
        auto row = r.removeFromTop (34.0f);
        auto spin = juce::Rectangle<float> (26.0f, 26.0f).withCentre ({ row.getX() + 13.0f, row.getCentreY() });
        juce::Path arc;
        const float a0 = (float) (now * 5.0);
        arc.addCentredArc (spin.getCentreX(), spin.getCentreY(), 11.0f, 11.0f, 0.0f, a0, a0 + 4.2f, true);
        g.setColour (col::sBlue);
        g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        write (g, "Le bus IAC est éteint. Je le guette : allume-le, je le vois apparaître."_fr, row.withTrimmedLeft (40.0f), grotesk (14.5f, 500), col::screenText);
        r.removeFromTop (12.0f);
        auto open = r.removeFromTop (38.0f).removeFromLeft (300.0f);
        bigKey (g, open, "ouvrir Configuration audio et MIDI", hot (open), true, false);
        hit (open, []
        {
            juce::File ("/System/Applications/Utilities/Audio MIDI Setup.app").startAsProcess();
            juce::File ("/Applications/Utilities/Audio MIDI Setup.app").startAsProcess();
        });
        r.removeFromTop (14.0f);
        lines (g, r, { "Dans [[Configuration audio et MIDI]], menu [[Fenêtre › Afficher le studio MIDI]]."_fr,
                       "Double-clic sur [[Gestionnaire IAC]] (IAC Driver)."_fr,
                       "Coche [[Le gestionnaire est en ligne]] (Device is online), puis [[Appliquer]]."_fr });
    }
    paintFlow (g, right, true, true);
}

//============================================================================== BRANCHEMENTS (hardware)
void CaptureAssistant::paintWiring (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    auto r = heading (g, left, "ÉTAPE "_fr + juce::String (at + 1) + " · BRANCHEMENTS"_fr, "Les branchements", {});
    auto section = [&] (const juce::String& s)
    {
        write (g, s, r.removeFromTop (20.0f), grotesk (12.0f, 600), col::screenDim);
        r.removeFromTop (6.0f);
    };
    // chips in rows; the space they took
    auto flow = [&] (const juce::StringArray& names, std::function<bool (int)> on, std::function<void (int)> pick, int maxRows = 2)
    {
        float x = r.getX(), y = r.getY();
        int rows = 1;
        for (int i = 0; i < names.size(); ++i)
        {
            const float w = chipWidth (names[i]);
            if (x + w > r.getRight() && x > r.getX())
            {
                if (++rows > maxRows)
                    break;
                x = r.getX();
                y += 34.0f;
            }
            auto c = juce::Rectangle<float> (x, y, w, 28.0f);
            chip (g, c, names[i], on (i), hot (c));
            hit (c, [pick, i] { pick (i); });
            x += w + 8.0f;
        }
        r.removeFromTop ((float) rows * 34.0f + 6.0f);
    };

    section ("1 · MIDI VERS LE SYNTHÉ"_fr);
    juce::StringArray ports;
    juce::Array<juce::MidiDeviceInfo> real;
    for (auto& d : midiOuts)
        if (! d.name.containsIgnoreCase ("IAC"))
        {
            ports.add (d.name);
            real.add (d);
        }
    if (ports.isEmpty())
    {
        write (g, "Aucun port MIDI : branche ton interface MIDI, ou le câble USB du synthé. Je regarde chaque seconde."_fr, r.removeFromTop (40.0f),
               grotesk (13.5f, 400), col::sBlue, juce::Justification::topLeft, 2);
    }
    else
        flow (ports, [this, real] (int i) { return real[i].identifier == hwPort; },
              [this, real] (int i) { hwPort = real[i].identifier; applyHardware(); });
    {
        auto chRow = r.removeFromTop (30.0f);
        write (g, "canal", chRow.removeFromLeft (60.0f), grotesk (13.5f, 500), col::screenDim);
        auto minus = chRow.removeFromLeft (30.0f), value = chRow.removeFromLeft (46.0f), plus = chRow.removeFromLeft (30.0f);
        chip (g, minus, "-", false, hot (minus));
        write (g, juce::String (proc.options.channel), value, grotesk (17.0f, 600), col::screenText, juce::Justification::centred);
        chip (g, plus, "+", false, hot (plus));
        hit (minus, [this] { setChannel (proc.options.channel - 1); });
        hit (plus, [this] { setChannel (proc.options.channel + 1); });
        write (g, "le même que celui du synthé (ou omni)"_fr, chRow.withTrimmedLeft (14.0f), grotesk (12.0f, 400), col::screenDim);
    }
    r.removeFromTop (14.0f);

    section ("2 · LE SON DU SYNTHÉ, DANS TA CARTE SON"_fr);
    if (ins.devices.isEmpty())
        write (g, "Branche sa sortie audio dans une entrée de ta carte son."_fr, r.removeFromTop (22.0f), grotesk (13.5f, 400), col::screenText);
    else
    {
        flow (ins.devices, [this] (int i) { return ins.devices[i] == inDevice; },
              [this] (int i) { inDevice = ins.devices[i]; inFirst = 0; ins = proc.audioInputs (inDevice); inStereo = ins.channels.size() > 1; applyInput(); }, 1);
        juce::StringArray chans;
        for (int i = 0; i < juce::jmin (16, ins.channels.size()); ++i)
            chans.add (ins.channels[i].isNotEmpty() ? ins.channels[i] : "Input " + juce::String (i + 1));
        flow (chans, [this] (int i) { return i == inFirst || (inStereo && i == inFirst + 1); },
              [this] (int i) { inFirst = i; if (inFirst + 1 >= ins.channels.size()) inStereo = false; applyInput(); });
        auto st = r.removeFromTop (28.0f);
        auto mono = st.removeFromLeft (80.0f), stereo = st.removeFromLeft (80.0f).translated (8.0f, 0.0f);
        chip (g, mono, "mono", ! inStereo, hot (mono));
        chip (g, stereo, "stéréo"_fr, inStereo, hot (stereo), inFirst + 1 < ins.channels.size());
        hit (mono, [this] { inStereo = false; applyInput(); });
        hit (stereo, [this] { if (inFirst + 1 < ins.channels.size()) { inStereo = true; applyInput(); } });
        if (ins.app && now - appliedAt < 4.0 && applied.isNotEmpty())
            write (g, applied, st.withTrimmedLeft (24.0f), grotesk (12.5f, 500), col::sGreen.withAlpha (clamp01 ((float) (4.0 - (now - appliedAt)))));
    }
    paintFlow (g, right, true, true);
}

//============================================================================== PISTE: what to set in the host
namespace
{
    struct MockRow { juce::String name; int kind = 0; juce::StringArray options; int selected = 0; juce::String value; bool gold = false; };
    // kind: 0 a menu, 1 buttons side by side, 2 a switch (on), 3 a slot (a plugin)
}

void CaptureAssistant::paintTrack (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    const bool hw = target == Target::hardware;
    const auto bus = iacName();
    const auto in = inputName (true);
    juce::StringArray ls;
    juce::String title;
    switch (daw)
    {
        case 0:
            title = hw ? "Dans Live : une piste audio" : "Dans Live : la piste du synthé"_fr;
            if (hw) ls = { "Crée une [[piste audio]] ([[Cmd+T]]) et glisse [[REPLIK8]] dessus."_fr,
                           "Affiche les entrées et sorties : [[Option+Cmd+I]]."_fr,
                           "[[Audio From]] : [[Ext. In]], puis [[" + in + "]].",
                           "[[Monitor]] : [[In]].",
                           "L'entrée n'est pas proposée ? [[Cmd+,]] › Audio › [[Input Config]] : allume-la."_fr };
            else ls = { "Sur la piste MIDI de ton synthé, glisse [[REPLIK8]] juste [[après le synthé]]."_fr,
                        "Affiche les entrées et sorties : [[Option+Cmd+I]]."_fr,
                        "[[MIDI From]] : [[" + bus + "]], puis [[All Channels]].",
                        "[[Monitor]] : [[In]] (la piste écoute sans être armée)."_fr,
                        "Une fois pour toutes : [[Cmd+,]] › Link, Tempo & MIDI › MIDI Ports, entrée « "_fr + bus + " » : [[Track]] sur [[On]]."_fr };
            break;
        case 1:
            title = hw ? "Dans Logic : une piste audio" : "Dans Logic : la piste du synthé"_fr;
            if (hw) ls = { "Nouvelle [[piste audio]] ([[Option+Cmd+A]]), entrée [["_fr + in + "]].",
                           "Allume son bouton [[I]] (monitoring d'entrée)."_fr,
                           "Slot [[Audio FX]] : Audio Units › distortedimages › [[REPLIK8]]."_fr };
            else ls = { "Sur la tranche de ton synthé, slot [[Audio FX]] : Audio Units › distortedimages › [[REPLIK8]], après le synthé."_fr,
                        "[[Sélectionne la piste du synthé]] : Logic lui donne le MIDI qui arrive, celui de l'IAC compris. Ou allume son bouton [[R]]."_fr,
                        "C'est tout : Logic écoute déjà tous les ports MIDI du Mac."_fr };
            break;
        case 2:
            title = hw ? "Dans Bitwig : une piste audio" : "Dans Bitwig : la piste du synthé"_fr;
            if (hw) ls = { "Une [[piste audio]], Input : [[" + in + "]] (les entrées se nomment dans Settings › Audio)."_fr,
                           "Monitoring : [[On]].",
                           "[[REPLIK8]] dans sa chaîne de devices."_fr };
            else ls = { "Une fois pour toutes : [[Settings › Controllers]] › Add Controller › Generic › [[MIDI Keyboard]], entrée [["_fr + bus + "]].",
                        "Dans la chaîne de devices du synthé, ajoute [[REPLIK8 après le synthé]]."_fr,
                        "Input de la piste : [[All ins]] (ou ce MIDI Keyboard), et [[arme-la]] : armée, elle écoute."_fr };
            break;
        case 3:
            title = hw ? "Dans Reaper : une piste audio" : "Dans Reaper : la piste du synthé"_fr;
            if (hw) ls = { "Une piste, entrée [[Input: "_fr + in + "]].",
                           "[[Arme-la]], monitoring [[On]].",
                           "Bouton [[FX]] : [[REPLIK8]]." };
            else ls = { "Une fois pour toutes : [[Cmd+,]] › Audio › MIDI Inputs (ou MIDI Devices) : [["_fr + bus + "]] sur [[Enable input]].",
                        "Bouton [[FX]] de la piste du synthé : [[REPLIK8]] sous le synthé."_fr,
                        "[[Arme]] la piste, entrée [[Input: MIDI › "_fr + bus + " › All channels]], monitoring [[On]]."_fr };
            break;
        case 4:
            title = hw ? "Dans Cubase : une piste audio" : "Dans Cubase : la piste du synthé"_fr;
            if (hw) ls = { "Une [[piste audio]], entrée : le bus de [["_fr + in + "]] (Studio › Audio Connections)."_fr,
                           "Bouton [[Monitor]] allumé."_fr,
                           "Inserts : [[REPLIK8]]." };
            else ls = { "Une fois pour toutes : Studio › Studio Setup › [[MIDI Port Setup]] : « "_fr + bus + " » coché dans « In 'All MIDI Inputs' »."_fr,
                        "Piste instrument du synthé : [[MIDI Input]] = [["_fr + bus + "]] (ou All MIDI Inputs), bouton [[Monitor]] allumé."_fr,
                        "Inserts de la piste : [[REPLIK8]]." };
            break;
        case 5:
            title = hw ? "Dans Studio One : une piste audio" : "Dans Studio One : la piste du synthé"_fr;
            if (hw) ls = { "Une [[piste audio]], entrée [["_fr + in + "]].",
                           "[[Monitoring]] allumé."_fr,
                           "Inserts : [[REPLIK8]]." };
            else ls = { "Une fois pour toutes : [[Cmd+,]] › External Devices › Add › [[New Keyboard]], Receive From [["_fr + bus + "]].",
                        "Piste instrument du synthé : entrée = ce clavier, [[monitoring]] allumé."_fr,
                        "Inserts de la piste : [[REPLIK8]] après le synthé."_fr };
            break;
        default:
            title = hw ? "Dans ton DAW : une piste audio" : "Dans ton DAW : la piste du synthé"_fr;
            if (hw) ls = { "Une [[piste audio]] qui écoute [["_fr + in + "]] de ta carte son, [[monitoring]] allumé."_fr,
                           "[[REPLIK8]] en insert sur cette piste." };
            else ls = { "La piste du synthé écoute le port MIDI [["_fr + bus + "]], [[monitoring]] allumé (ou piste armée)."_fr,
                        "[[REPLIK8]] en insert sur la même piste, [[après le synthé]]."_fr };
            break;
    }
    auto r = heading (g, left, "ÉTAPE "_fr + juce::String (at + 1) + " · PISTE"_fr, title, {});
    lines (g, r, ls);
    paintTrackMock (g, right);
}

// the track as the host shows it, drawn in REPLIK8's way: what to set, in gold
void CaptureAssistant::paintTrackMock (juce::Graphics& g, juce::Rectangle<float> r)
{
    const bool hw = target == Target::hardware;
    const auto bus = lower (iacName());
    const auto in = inputName (true);
    std::vector<MockRow> rows;
    juce::StringArray chain;           // the plugins, REPLIK8 last
    juce::String head = host.trackName.isNotEmpty() ? host.trackName : (hw ? juce::String ("piste audio") : juce::String ("piste du synthé"_fr));
    switch (daw)
    {
        case 0:
            if (hw) rows = { { "Audio From", 0, {}, 0, "Ext. In", true }, { "", 0, {}, 0, in, true }, { "Monitor", 1, { "In", "Auto", "Off" }, 0, {}, true } };
            else rows = { { "MIDI From", 0, {}, 0, bus, true }, { "", 0, {}, 0, "All Channels", false }, { "Monitor", 1, { "In", "Auto", "Off" }, 0, {}, true } };
            break;
        case 1:
            if (hw) rows = { { "Input", 0, {}, 0, in, true }, { "I", 2, {}, 0, "monitoring", true } };
            else rows = { { "R", 2, {}, 0, "ou piste sélectionnée"_fr, true } };
            break;
        case 2:
            if (hw) rows = { { "Input", 0, {}, 0, in, true }, { "Monitoring", 1, { "On", "Auto", "Off" }, 0, {}, true } };
            else rows = { { "Input", 0, {}, 0, "All ins", false }, { "Arm", 2, {}, 0, "armée"_fr, true } };
            break;
        case 3:
            rows = { { "Input", 0, {}, 0, hw ? in : "MIDI › "_fr + bus, true }, { "Arm", 2, {}, 0, "armée"_fr, true }, { "Monitor", 1, { "On", "Off" }, 0, {}, true } };
            break;
        case 4:
            rows = { { hw ? "Input" : "MIDI Input", 0, {}, 0, hw ? in : bus, true }, { "Monitor", 2, {}, 0, "allumé"_fr, true } };
            break;
        case 5:
            rows = { { "Input", 0, {}, 0, hw ? in : "clavier IAC", true }, { "Monitor", 2, {}, 0, "allumé"_fr, true } };
            break;
        default:
            rows = { { hw ? "Input" : "MIDI In", 0, {}, 0, hw ? in : bus, true }, { "Monitor", 2, {}, 0, "allumé"_fr, true } };
            break;
    }
    if (! hw)
        chain.add (juce::String ("ton synthé"_fr));
    chain.add ("REPLIK8");

    const float tall = 38.0f + 28.0f + 40.0f * (float) rows.size() + 8.0f + 16.0f + 6.0f + 46.0f + 6.0f;
    auto card = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 460.0f), juce::jmin (r.getHeight(), tall)).translated (0.0f, -12.0f);
    g.setColour (col::screenGrid.brighter (0.25f));
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (col::screenLine);
    g.drawRoundedRectangle (card, 10.0f, 1.2f);
    auto top = card.removeFromTop (38.0f);
    g.setColour (col::screenLine.withAlpha (0.6f));
    g.fillRoundedRectangle (top, 10.0f);
    g.fillRect (top.withTrimmedTop (20.0f));
    dawGlyph (g, daw >= 0 ? daw : 6, top.removeFromLeft (38.0f).reduced (9.0f), col::screenDim);
    write (g, head, top.withTrimmedRight (12.0f), grotesk (14.0f, 600), col::screenText);
    auto body = card.reduced (18.0f, 14.0f);
    int arrowRow = -1;
    juce::Rectangle<float> arrowAt;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& m = rows[(size_t) i];
        auto row = body.removeFromTop (32.0f);
        body.removeFromTop (8.0f);
        write (g, m.name, row.removeFromLeft (112.0f), grotesk (13.0f, 500), col::screenDim);
        juce::Rectangle<float> field;
        if (m.kind == 0)
        {
            field = row.withWidth (juce::jmin (row.getWidth(), 250.0f));
            g.setColour (col::screen);
            g.fillRoundedRectangle (field, 5.0f);
            g.setColour (m.gold ? col::sBlue : col::screenLine);
            g.drawRoundedRectangle (field, 5.0f, 1.0f);
            write (g, m.value, field.reduced (10.0f, 0.0f).withTrimmedRight (16.0f), grotesk (13.0f, m.gold ? 600 : 400), m.gold ? col::sBlue : col::screenText);
            caret (g, { field.getRight() - 12.0f, field.getCentreY() }, 4.0f, col::screenDim);
        }
        else if (m.kind == 1)
        {
            float x = row.getX();
            for (int o = 0; o < m.options.size(); ++o)
            {
                auto b = juce::Rectangle<float> (x, row.getY(), 58.0f, row.getHeight());
                const bool sel = o == m.selected;
                g.setColour (sel ? col::sBlue.withAlpha (0.85f) : col::screen);
                g.fillRoundedRectangle (b.reduced (1.0f), 4.0f);
                g.setColour (sel ? col::sBlue : col::screenLine);
                g.drawRoundedRectangle (b.reduced (1.0f), 4.0f, 1.0f);
                write (g, m.options[o], b, grotesk (12.5f, sel ? 600 : 400), sel ? col::screen : col::screenDim, juce::Justification::centred);
                if (sel)
                    field = b;
                x += 60.0f;
            }
        }
        else
        {
            auto sw = row.removeFromLeft (34.0f).withSizeKeepingCentre (34.0f, 24.0f);
            g.setColour (col::sBlue);
            g.fillRoundedRectangle (sw, 5.0f);
            write (g, m.name.length() <= 2 ? m.name : juce::String ("●"_fr), sw, grotesk (12.0f, 700), col::screen, juce::Justification::centred);
            write (g, m.value, row.withTrimmedLeft (12.0f), grotesk (12.5f, 400), col::screenDim);
            field = sw;
        }
        if (m.gold)
        {
            pulse (g, field, now + (double) i * 0.4, 5.0f);
            if (arrowRow < 0)
            {
                arrowRow = i;
                arrowAt = field;
            }
        }
    }
    // the plugins on the track
    body.removeFromTop (8.0f);
    write (g, hw ? "INSERTS" : "INSERTS · DANS CET ORDRE"_fr, body.removeFromTop (16.0f), grotesk (10.5f, 600), col::screenDim);
    body.removeFromTop (6.0f);
    auto lane = body.removeFromTop (46.0f);
    float x = lane.getX();
    for (int i = 0; i < chain.size(); ++i)
    {
        auto slot = juce::Rectangle<float> (x, lane.getY(), 130.0f, lane.getHeight());
        if (chain[i] == "REPLIK8")
        {
            replikBlock (g, slot, true);
            pulse (g, slot, now + 0.8, 6.0f);
        }
        else
        {
            g.setColour (col::screen);
            g.fillRoundedRectangle (slot, 6.0f);
            g.setColour (col::screenLine);
            g.drawRoundedRectangle (slot, 6.0f, 1.0f);
            pluginSynth (g, slot.withTrimmedRight (80.0f).reduced (6.0f, 8.0f), col::screenDim, now);
            write (g, chain[i], slot.withTrimmedLeft (54.0f), grotesk (12.5f, 500), col::screenText);
        }
        x += 130.0f;
        if (i + 1 < chain.size())
        {
            juce::Path a;
            a.startNewSubPath (x + 6.0f, lane.getCentreY());
            a.lineTo (x + 30.0f, lane.getCentreY());
            audioCable (g, a, now, true, juce::jmax (level[0], level[1]));
            x += 36.0f;
        }
    }
    // a hand pointing at the first thing to set
    if (arrowRow >= 0)
    {
        const float bob = 4.0f * std::sin ((float) now * 4.0f);
        const juce::Point<float> tip (arrowAt.getRight() + 10.0f + bob, arrowAt.getCentreY());
        juce::Path p;
        p.startNewSubPath (tip.x + 34.0f, tip.y);
        p.lineTo (tip.x, tip.y);
        g.setColour (col::sBlue);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path point;
        point.addTriangle (tip.x, tip.y, tip.x + 9.0f, tip.y - 6.0f, tip.x + 9.0f, tip.y + 6.0f);
        g.fillPath (point);
    }
}

//============================================================================== TEST
void CaptureAssistant::paintTest (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    const auto& o = proc.options;
    const bool synthTest = o.source == Src::testSynth;
    const juce::String route = synthTest ? juce::String ("au synthé de test"_fr) : "sur [[" + o.midiPortName + "]], canal [[" + juce::String (o.channel) + "]]";
    auto r = heading (g, left, "ÉTAPE "_fr + juce::String (at + 1) + " · TEST"_fr, "Le test",
                      juce::String::fromUTF8 ("Trois notes (C3) envoyées ") + route + juce::String::fromUTF8 (" : j'écoute ce qui revient et je mesure la latence.")
                          + (ccWanted() ? juce::String::fromUTF8 (" Puis le CC ") + juce::String (proc.cycleOptions.cc) + juce::String::fromUTF8 (" balayé.") : juce::String()));
    const bool running = test == Test::notes || test == Test::cc;
    // the big round key
    auto keyArea = r.removeFromLeft (150.0f).removeFromTop (150.0f);
    auto key = keyArea.withSizeKeepingCentre (124.0f, 124.0f);
    const bool over = hot (key) && ! running;
    g.setColour (running ? col::screenGrid.brighter (0.3f) : (over ? col::red.brighter (0.15f) : col::red));
    g.fillEllipse (key.reduced (8.0f));
    g.setColour (col::screenLine);
    g.drawEllipse (key.reduced (2.0f), 2.0f);
    if (running)
    {
        const auto& p = test == Test::cc ? ccResult : notesResult;
        const float progress = test == Test::cc ? 0.5f + 0.5f * clamp01 ((float) ((now - testFrom) / 1.6))
                                                : 0.5f * clamp01 (((float) p.played + clamp01 ((float) std::fmod (now - testFrom, 0.9) / 0.9f)) / (float) juce::jmax (1, p.hits));
        const float done = ccWanted() ? progress : juce::jmin (1.0f, progress * 2.0f);
        juce::Path arc;
        arc.addCentredArc (key.getCentreX(), key.getCentreY(), 60.0f, 60.0f, 0.0f, 0.0f, done * juce::MathConstants<float>::twoPi, true);
        g.setColour (col::sRed);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        write (g, test == Test::cc ? juce::String ("CC") : juce::String (juce::jmin (p.played + 1, juce::jmax (1, p.hits))) + " / " + juce::String (juce::jmax (1, p.hits)),
               key, grotesk (22.0f, 600), col::screenText, juce::Justification::centred);
    }
    else
    {
        write (g, test == Test::done ? "ENCORE" : "TEST", key, grotesk (20.0f, 700), juce::Colours::white, juce::Justification::centred);
        hit (key, [this] { startTest(); });
    }

    // what it found
    auto list = r.withTrimmedLeft (16.0f);
    struct Check { int state; juce::String name, detail; };    // state: 0 waiting, 1 ok, 2 warning, 3 wrong, 4 running
    std::vector<Check> checks;
    const auto& n = notesResult;
    const bool measured = test == Test::done && n.state == ReplikantProcessor::Probe::done;
    const bool timedOut = test == Test::done && ! measured;
    auto st = [&] (bool ok, bool warn = false) { return ! measured ? (running ? 4 : 0) : (ok ? 1 : (warn ? 2 : 3)); };
    checks.push_back ({ synthTest ? 1 : (o.midiPortName.isNotEmpty() ? 1 : 3), "MIDI", synthTest ? juce::String ("synthé de test"_fr) : lower (o.midiPortName) + " · canal "_fr + juce::String (o.channel) });
    {
        juce::String d = measured ? juce::String (n.heard) + " / " + juce::String (n.played) + juce::String::fromUTF8 (n.heard == n.played ? " notes revenues" : " notes revenues seulement") : juce::String();
        if (timedOut) d = juce::String::fromUTF8 ("pas de réponse de l'audio : le DAW joue-t-il ?");
        checks.push_back ({ timedOut ? 3 : st (n.heard >= 2), "le son revient", d });
    }
    if (measured && n.heard > 0)
    {
        const bool low = n.peakDb < -30.0f;
        checks.push_back ({ st (! n.clipped && ! low, low && ! n.clipped), "niveau",
                            n.clipped ? juce::String::fromUTF8 ("ça sature : baisse le synthé (crête sous −1 dB)")
                                      : juce::String (n.peakDb, 1) + (low ? " dB crête : faible, monte le synthé vers −12 dB"_fr : " dB crête"_fr) });
        checks.push_back ({ st (! n.echoed && ! echoSeen), "pas de boucle", n.echoed || echoSeen ? juce::String::fromUTF8 ("REPLIK8 entend sa propre sortie") : juce::String ("l'entrée n'est pas la sortie"_fr) });
        float lo = 1.0e9f, hi = 0.0f, sum = 0.0f;
        for (int i = 0; i < n.heard; ++i)
        {
            lo = juce::jmin (lo, n.latencyMs[i]);
            hi = juce::jmax (hi, n.latencyMs[i]);
            sum += n.latencyMs[i];
        }
        const bool steady = hi - lo < 1.0f && ! n.drone;
        checks.push_back ({ st (steady, true), "latence",
                            juce::String (sum / (float) n.heard, 1) + " ms"
                                + (steady ? (kind == 0 ? " · stable : notes calées dessus"_fr : " · stable"_fr)
                                          : (n.drone ? juce::String::fromUTF8 (" · le son ne s'arrête pas entre les notes")
                                                     : juce::String::fromUTF8 (" · variable (") + juce::String (lo, 1) + "–"_fr + juce::String (hi, 1)
                                                           + (kind == 0 ? ") : chaque attaque cherchée"_fr : juce::String (")")))) });
        if (ccWanted())
        {
            const bool ccDone = ccResult.state == ReplikantProcessor::Probe::done;
            const bool moved = std::abs (ccResult.ccDb) > 1.5f || std::abs (ccResult.ccBright) > 0.12f;
            checks.push_back ({ ccDone ? (moved ? 1 : 3) : 0, "CC " + juce::String (proc.cycleOptions.cc),
                                ccDone ? (moved ? "brillance " + juce::String (juce::roundToInt (ccResult.ccBright * 100.0f)) + " %, niveau " + juce::String (ccResult.ccDb, 1) + " dB"
                                                : juce::String::fromUTF8 ("ne change rien : assigne-le au filtre dans ton synthé (MIDI learn)"))
                                       : juce::String() });
        }
    }
    else if (running || ! measured)
    {
        for (auto* nm : { "niveau", "pas de boucle", "latence" })
            checks.push_back ({ running ? 4 : 0, nm, {} });
    }
    for (size_t i = 0; i < checks.size(); ++i)
    {
        auto row = list.removeFromTop (34.0f);
        const auto& c = checks[i];
        auto icon = row.removeFromLeft (26.0f).withSizeKeepingCentre (22.0f, 22.0f);
        const float k = measured ? easeBack ((float) ((now - doneAt - 0.12 * (double) i) / 0.35)) : 1.0f;
        if (c.state == 4)
        {
            juce::Path arc;
            const float a0 = (float) (now * 6.0) + (float) i;
            arc.addCentredArc (icon.getCentreX(), icon.getCentreY(), 8.0f, 8.0f, 0.0f, a0, a0 + 3.8f, true);
            g.setColour (col::screenDim);
            g.strokePath (arc, juce::PathStrokeType (1.8f));
        }
        else if (c.state == 0)
        {
            g.setColour (col::screenLine);
            g.drawEllipse (icon.reduced (2.0f), 1.3f);
        }
        else
        {
            const auto colour = c.state == 1 ? col::sGreen : (c.state == 2 ? col::sBlue : col::sRed);
            auto d = icon.withSizeKeepingCentre (22.0f * k, 22.0f * k);
            g.setColour (colour.withAlpha (0.2f));
            g.fillEllipse (d);
            if (c.state == 1)      tickMark (g, icon.getCentre(), 12.0f * k, colour, 1.8f);
            else if (c.state == 2) write (g, "!", d, grotesk (14.0f * juce::jmax (0.1f, k), 700), colour, juce::Justification::centred);
            else                   crossMark (g, icon.getCentre(), 12.0f * k, colour, 1.8f);
        }
        row.removeFromLeft (10.0f);
        write (g, c.name, row.removeFromLeft (120.0f), grotesk (14.0f, 600), c.state == 0 ? col::screenDim : col::screenText);
        write (g, c.detail, row, grotesk (13.0f, 400), c.state == 3 ? col::sRed : col::screenDim);
    }
    // what went wrong, what to look at
    if (measured && ! ready)
    {
        juce::String why;
        if (n.heard < 2)
        {
            if (target == Target::hardware)
                why = "Le synthé a-t-il joué ? Oui : regarde l'entrée de la carte son, la piste audio et son monitoring. Non : le câble MIDI et le canal du synthé ("_fr
                      + juce::String (o.channel) + ").";
            else if (target == Target::plugin)
                why = "Rien n'est revenu. La piste du synthé écoute-t-elle « "_fr + iacName() + " » ? Son témoin MIDI doit clignoter pendant le test. REPLIK8 est-il bien après le synthé, sur la même piste ?"_fr;
            else
                why = "Rien n'est revenu : la piste où REPLIK8 est posé reçoit-elle le son du synthé ?"_fr;
        }
        else if (n.echoed || echoSeen)
            why = "La piste où REPLIK8 est posé se réécoute elle-même : son entrée doit être le synthé, pas sa propre sortie."_fr;
        else if (n.clipped)
            why = "Baisse le volume du synthé (ou le gain de ta carte son) jusqu'à ce que la crête reste sous −1 dB, puis refais le test."_fr;
        else if (ccWanted())
            why = "Le CC ne bouge pas le son : dans ton synthé, assigne le CC "_fr + juce::String (proc.cycleOptions.cc)
                  + " au filtre ou à la forme d'onde (MIDI learn), ou choisis un autre CC dans CYCLES."_fr;
        list.removeFromTop (6.0f);
        drawRich (g, rich (why, 13.5f, col::sBlue, col::screenText), list.removeFromTop (60.0f));
    }
    if (fixNote.isNotEmpty())
        write (g, fixNote, list.removeFromTop (20.0f), grotesk (12.5f, 500), col::sGreen);

    //---------------------------------------------------------------- the right side: meters, the latency, READY
    auto meters = right.removeFromLeft (120.0f);
    auto mArea = meters.withTrimmedTop (6.0f).withTrimmedBottom (40.0f);
    write (g, "ENTRÉE"_fr, mArea.removeFromTop (16.0f), grotesk (10.5f, 600), col::screenDim, juce::Justification::centred);
    auto scale = mArea.removeFromLeft (34.0f);
    const float dbs[] = { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
    auto yOf = [&] (float db) { return mArea.getY() + 4.0f + (mArea.getHeight() - 8.0f) * (1.0f - (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f); };
    for (float d : dbs)
        write (g, juce::String ((int) d), { scale.getX(), yOf (d) - 6.0f, scale.getWidth() - 4.0f, 12.0f }, grotesk (9.5f, 500), col::screenDim, juce::Justification::centredRight);
    for (int c = 0; c < 2; ++c)
    {
        auto bar = mArea.removeFromLeft (26.0f).reduced (4.0f, 0.0f);
        g.setColour (col::screenGrid);
        g.fillRoundedRectangle (bar, 3.0f);
        const float db = juce::Decibels::gainToDecibels (level[c], -100.0f);
        const int segs = 30;
        for (int s = 0; s < segs; ++s)
        {
            const float segDb = -60.0f + 60.0f * ((float) s + 0.5f) / (float) segs;
            const float y0 = yOf (-60.0f + 60.0f * (float) (s + 1) / (float) segs), y1 = yOf (-60.0f + 60.0f * (float) s / (float) segs);
            const bool lit = db >= segDb;
            const auto colour = segDb > -3.0f ? col::sRed : (segDb > -12.0f ? col::sBlue : col::sGreen);
            g.setColour (lit ? colour : colour.withAlpha (0.1f));
            g.fillRect (juce::Rectangle<float> (bar.getX() + 2.0f, y0 + 1.0f, bar.getWidth() - 4.0f, juce::jmax (1.0f, y1 - y0 - 2.0f)));
        }
        const float hdb = juce::Decibels::gainToDecibels (hold[c], -100.0f);
        if (hdb > -60.0f)
        {
            g.setColour (hdb > -1.0f ? col::sRed : col::screenText);
            g.fillRect (bar.getX() + 1.0f, yOf (hdb) - 1.0f, bar.getWidth() - 2.0f, 2.0f);
        }
        write (g, c == 0 ? "L" : "R", { bar.getX(), mArea.getBottom() + 4.0f, bar.getWidth(), 14.0f }, grotesk (10.0f, 600), col::screenDim, juce::Justification::centred);
    }
    // where the sound comes in: two small lights
    {
        auto lights = meters.removeFromBottom (32.0f);
        auto lamp = [&] (juce::Rectangle<float> a, const juce::String& s, float v)
        {
            const bool on = v > 0.003f;
            auto d = a.removeFromLeft (12.0f).withSizeKeepingCentre (8.0f, 8.0f);
            g.setColour (on ? col::sGreen : col::screenLine);
            g.fillEllipse (d);
            write (g, s, a.withTrimmedLeft (4.0f), grotesk (10.0f, 500), on ? col::screenText : col::screenDim);
        };
        lamp (lights.removeFromTop (16.0f), "input", mainLed);
        lamp (lights, "sidechain", sideLed);
    }

    // the latency: the note sent at 0, each attack where it came back
    right.removeFromLeft (20.0f);
    auto lat = right.removeFromBottom (110.0f);
    {
        write (g, "LATENCE"_fr, lat.removeFromTop (16.0f), grotesk (10.5f, 600), col::screenDim);
        float maxMs = 20.0f;
        for (int i = 0; i < n.heard; ++i)
            maxMs = juce::jmax (maxMs, n.latencyMs[i] * 1.4f);
        maxMs = std::ceil (maxMs / 10.0f) * 10.0f;
        auto axis = lat.withTrimmedTop (34.0f).removeFromTop (34.0f);
        g.setColour (col::screenLine);
        g.drawLine (axis.getX(), axis.getCentreY(), axis.getRight(), axis.getCentreY(), 1.0f);
        for (int i = 0; i <= 4; ++i)
        {
            const float x = axis.getX() + axis.getWidth() * (float) i / 4.0f;
            g.drawLine (x, axis.getCentreY() - 3.0f, x, axis.getCentreY() + 3.0f, 1.0f);
            write (g, juce::String (juce::roundToInt (maxMs * (float) i / 4.0f)) + (i == 4 ? " ms" : ""), { x - 30.0f, axis.getCentreY() + 6.0f, 60.0f, 14.0f },
                   grotesk (9.5f, 500), col::screenDim, juce::Justification::centred);
        }
        g.setColour (col::sRed);
        g.fillRect (axis.getX() - 1.0f, axis.getY() - 6.0f, 2.0f, axis.getHeight() * 0.5f + 6.0f);
        write (g, "note", { axis.getX() - 20.0f, axis.getY() - 22.0f, 40.0f, 14.0f }, grotesk (9.5f, 600), col::sRed, juce::Justification::centred);
        for (int i = 0; i < n.heard; ++i)
        {
            const float x = axis.getX() + axis.getWidth() * clamp01 (n.latencyMs[i] / maxMs);
            const float k = easeBack ((float) ((now - testFrom - 0.75 * (double) (i + 1)) / 0.3));
            juce::Path tri;
            tri.addTriangle (x - 5.0f, axis.getCentreY() - 12.0f * k, x + 5.0f, axis.getCentreY() - 12.0f * k, x, axis.getCentreY() - 2.0f);
            g.setColour (col::sBlue);
            g.fillPath (tri);
        }
        if (n.heard > 0)
        {
            float sum = 0.0f;
            for (int i = 0; i < n.heard; ++i)
                sum += n.latencyMs[i];
            write (g, juce::String (sum / (float) n.heard, 1), lat.removeFromTop (34.0f).removeFromRight (160.0f), grotesk (30.0f, 300), col::screenText, juce::Justification::centredRight);
        }
    }

    // READY: stamped once all is well
    if (ready)
    {
        const float k = easeBack ((float) ((now - readyAt - 0.5) / 0.45));
        if (k > 0.0f)
        {
            auto stamp = right.withSizeKeepingCentre (250.0f, 110.0f).translated (0.0f, -10.0f);
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (-0.06f, stamp.getCentreX(), stamp.getCentreY())
                                .scaled (2.0f - k, 2.0f - k, stamp.getCentreX(), stamp.getCentreY()));
            const float a = clamp01 (k * 1.5f);
            const float glow = 0.5f + 0.5f * std::sin ((float) now * 2.5f);
            g.setColour (col::sGreen.withAlpha (0.08f * a * (0.6f + 0.4f * glow)));
            g.fillRoundedRectangle (stamp.expanded (10.0f), 18.0f);
            g.setColour (col::sGreen.withAlpha (a));
            g.drawRoundedRectangle (stamp, 14.0f, 3.0f);
            g.drawRoundedRectangle (stamp.reduced (6.0f), 10.0f, 1.0f);
            write (g, "PRÊT"_fr, stamp.withTrimmedBottom (30.0f).translated (0.0f, 8.0f), grotesk (46.0f, 700), col::sGreen.withAlpha (a), juce::Justification::centred);
            write (g, "REPLIK8 peut capturer"_fr, stamp.withTrimmedTop (70.0f).translated (0.0f, -8.0f), grotesk (12.5f, 600), col::sGreen.withAlpha (a), juce::Justification::centred);
        }
    }
    else if (! running && test == Test::idle)
    {
        auto c = right.withTrimmedBottom (110.0f);
        write (g, "appuie sur TEST"_fr, c, grotesk (16.0f, 400), col::screenGhost.brighter (0.6f), juce::Justification::centred);
    }
}
}
