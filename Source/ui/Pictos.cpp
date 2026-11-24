// The pictos: small drawings in thin lines, in the spirit of the symbols on a schematic or the reels of a tape
// machine. Each is drawn in a unit square (x right, y down, 0..1) placed in the area, square.

#include "Look.h"

namespace te
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    struct Pen
    {
        juce::Graphics& g;
        juce::Rectangle<float> box;
        float t;
        juce::Colour c;

        juce::Point<float> p (float x, float y) const    { return { box.getX() + x * box.getWidth(), box.getY() + y * box.getHeight() }; }
        float s (float v) const                           { return v * box.getWidth(); }

        void stroke (const juce::Path& path, float alpha = 1.0f) const
        {
            g.setColour (c.withMultipliedAlpha (alpha));
            g.strokePath (path, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        void fill (const juce::Path& path, float alpha = 1.0f) const
        {
            g.setColour (c.withMultipliedAlpha (alpha));
            g.fillPath (path);
        }
        void line (float x0, float y0, float x1, float y1, float alpha = 1.0f) const
        {
            juce::Path q;
            q.startNewSubPath (p (x0, y0));
            q.lineTo (p (x1, y1));
            stroke (q, alpha);
        }
        juce::Path poly (std::initializer_list<juce::Point<float>> pts, bool close = false) const
        {
            juce::Path q;
            bool first = true;
            for (auto pt : pts)
            {
                if (first) q.startNewSubPath (p (pt.x, pt.y)); else q.lineTo (p (pt.x, pt.y));
                first = false;
            }
            if (close)
                q.closeSubPath();
            return q;
        }
        void lines (std::initializer_list<juce::Point<float>> pts, float alpha = 1.0f) const   { stroke (poly (pts), alpha); }
        void shape (std::initializer_list<juce::Point<float>> pts, bool filled, float alpha = 1.0f) const
        {
            auto q = poly (pts, true);
            if (filled) fill (q, alpha); else stroke (q, alpha);
        }
        void circle (float x, float y, float r, bool filled = false, float alpha = 1.0f) const
        {
            juce::Path q;
            q.addEllipse (p (x - r, y - r).x, p (x - r, y - r).y, s (2.0f * r), s (2.0f * r));
            if (filled) fill (q, alpha); else stroke (q, alpha);
        }
        void rect (float x0, float y0, float x1, float y1, float corner, bool filled = false, float alpha = 1.0f) const
        {
            juce::Path q;
            q.addRoundedRectangle (p (x0, y0).x, p (x0, y0).y, s (x1 - x0), s (y1 - y0), s (corner));
            if (filled) fill (q, alpha); else stroke (q, alpha);
        }
        // y = centre - amp · f(phase), the phase running over `cycles` from x0 to x1
        template <typename F>
        juce::Path wave (float x0, float x1, float y, float amp, float cycles, F f, int steps = 48) const
        {
            juce::Path q;
            for (int i = 0; i <= steps; ++i)
            {
                const float u = (float) i / (float) steps;
                const auto pt = p (x0 + (x1 - x0) * u, y - amp * f (u * cycles, u));
                if (i == 0) q.startNewSubPath (pt); else q.lineTo (pt);
            }
            return q;
        }
        // an arc around (x, y), angles in radians from twelve o'clock, clockwise
        juce::Path arc (float x, float y, float r, float from, float to) const
        {
            juce::Path q;
            q.addCentredArc (p (x, y).x, p (x, y).y, s (r), s (r), 0.0f, from, to, true);
            return q;
        }
        // a capacitor across a horizontal wire at x (its plates vertical)
        void capacitor (float x, float y, float h) const
        {
            line (x - 0.035f, y - h, x - 0.035f, y + h);
            line (x + 0.035f, y - h, x + 0.035f, y + h);
        }
        // an amplifier, pointing right
        void amp (float x0, float x1, float y, float h, bool filled = false) const
        {
            shape ({ { x0, y - h }, { x0, y + h }, { x1, y } }, filled);
        }
    };

    float sinePhase (float ph, float)  { return std::sin (ph * twoPi); }
}

void picto (juce::Graphics& g, Picto kind, juce::Rectangle<float> area, juce::Colour c, float thickness)
{
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    if (kind == Picto::none || side < 4.0f)
        return;
    const Pen d { g, area.withSizeKeepingCentre (side, side), thickness, c };

    switch (kind)
    {
        case Picto::none: break;

        //------------------------------------------------------------------ the modes
        case Picto::sine:
            d.stroke (d.wave (0.06f, 0.94f, 0.5f, 0.3f, 1.0f, sinePhase));
            break;

        case Picto::drum:
            d.rect (0.1f, 0.1f, 0.46f, 0.46f, 0.07f, true);
            d.rect (0.54f, 0.1f, 0.9f, 0.46f, 0.07f);
            d.rect (0.1f, 0.54f, 0.46f, 0.9f, 0.07f);
            d.rect (0.54f, 0.54f, 0.9f, 0.9f, 0.07f);
            break;

        case Picto::cassette:
            d.rect (0.04f, 0.18f, 0.96f, 0.82f, 0.06f);
            d.rect (0.2f, 0.3f, 0.8f, 0.56f, 0.13f, false, 0.6f);
            d.circle (0.34f, 0.43f, 0.065f);
            d.circle (0.66f, 0.43f, 0.065f);
            d.lines ({ { 0.25f, 0.82f }, { 0.3f, 0.68f }, { 0.7f, 0.68f }, { 0.75f, 0.82f } });
            break;

        //------------------------------------------------------------------ the pages
        case Picto::cycles:
        {
            // three cycles of a bank, one behind the other
            const float off[3][3] = { { 0.16f, -0.18f, 0.3f }, { 0.08f, -0.09f, 0.6f }, { 0.0f, 0.0f, 1.0f } };
            const float bright[3] = { 0.9f, 0.45f, 0.0f };
            for (int i = 0; i < 3; ++i)
            {
                const float k = bright[i];
                d.stroke (d.wave (0.04f + off[i][0], 0.8f + off[i][0], 0.6f + off[i][1], 0.2f, 1.0f,
                                  [k] (float ph, float) { return std::sin (ph * twoPi) + k * 0.45f * std::sin (2.0f * ph * twoPi); }),
                          off[i][2]);
            }
            break;
        }

        case Picto::saw:
            d.lines ({ { 0.05f, 0.5f }, { 0.05f, 0.72f }, { 0.5f, 0.28f }, { 0.5f, 0.72f }, { 0.95f, 0.28f }, { 0.95f, 0.5f } });
            break;

        case Picto::voices:
            d.circle (0.3f, 0.5f, 0.2f, false, 0.45f);
            d.circle (0.5f, 0.5f, 0.2f, false, 0.7f);
            d.circle (0.7f, 0.5f, 0.2f);
            break;

        case Picto::filterBox:
        case Picto::lowpass:
        case Picto::bandpass:
        case Picto::highpass:
        case Picto::notch:
        {
            // the IEC symbol: a box, three waves (high, middle, low), the ones it stops struck out
            d.rect (0.1f, 0.1f, 0.9f, 0.9f, 0.08f);
            const float ys[3] = { 0.3f, 0.5f, 0.7f };
            bool struck[3] = { true, false, false };            // (the box alone: a low-pass)
            if (kind == Picto::bandpass) { struck[0] = true; struck[2] = true; }
            if (kind == Picto::highpass) { struck[0] = false; struck[2] = true; }
            if (kind == Picto::notch)    { struck[0] = false; struck[1] = true; }
            for (int i = 0; i < 3; ++i)
            {
                d.stroke (d.wave (0.3f, 0.7f, ys[i], 0.055f, 1.0f, sinePhase, 24));
                if (struck[i])
                    d.line (0.38f, ys[i] + 0.1f, 0.62f, ys[i] - 0.1f);
            }
            break;
        }

        case Picto::envelope:
            d.line (0.04f, 0.84f, 0.96f, 0.84f, 0.35f);
            d.lines ({ { 0.04f, 0.84f }, { 0.22f, 0.16f }, { 0.4f, 0.48f }, { 0.7f, 0.48f }, { 0.96f, 0.84f } });
            break;

        case Picto::chain:
            d.rect (0.02f, 0.36f, 0.26f, 0.64f, 0.05f);
            d.rect (0.38f, 0.36f, 0.62f, 0.64f, 0.05f, true);
            d.rect (0.74f, 0.36f, 0.98f, 0.64f, 0.05f);
            d.line (0.26f, 0.5f, 0.38f, 0.5f);
            d.line (0.62f, 0.5f, 0.74f, 0.5f);
            break;

        case Picto::cable:
        {
            // two jacks and the patch cable between them
            d.circle (0.18f, 0.24f, 0.11f);
            d.circle (0.18f, 0.24f, 0.035f, true);
            d.circle (0.82f, 0.24f, 0.11f);
            d.circle (0.82f, 0.24f, 0.035f, true);
            juce::Path q;
            q.startNewSubPath (d.p (0.18f, 0.35f));
            q.cubicTo (d.p (0.18f, 1.0f), d.p (0.82f, 1.0f), d.p (0.82f, 0.35f));
            d.stroke (q);
            break;
        }

        case Picto::orbit:
        {
            // the carrier, its round orbit, two satellites on it (one each side)
            d.circle (0.5f, 0.5f, 0.13f, true);
            d.circle (0.5f, 0.5f, 0.36f, false, 0.55f);
            d.circle (0.81f, 0.32f, 0.08f, true);
            d.circle (0.19f, 0.68f, 0.08f, true);
            break;
        }

        case Picto::mixer:
            // three faders, their caps at different heights
            for (int i = 0; i < 3; ++i)
            {
                const float x = 0.22f + 0.28f * (float) i, y = i == 0 ? 0.36f : (i == 1 ? 0.62f : 0.45f);
                d.line (x, 0.12f, x, 0.88f, 0.5f);
                d.rect (x - 0.09f, y - 0.06f, x + 0.09f, y + 0.06f, 0.03f, true);
            }
            break;

        //------------------------------------------------------------------ RADIO
        case Picto::radio:
            // a transistor radio: its case, its speaker, its dial, its aerial
            d.rect (0.06f, 0.36f, 0.94f, 0.88f, 0.07f);
            d.circle (0.32f, 0.62f, 0.15f);
            d.circle (0.32f, 0.62f, 0.05f, true);
            d.line (0.58f, 0.52f, 0.84f, 0.52f, 0.7f);
            d.line (0.58f, 0.64f, 0.84f, 0.64f, 0.7f);
            d.line (0.58f, 0.76f, 0.84f, 0.76f, 0.7f);
            d.line (0.28f, 0.36f, 0.78f, 0.06f);
            break;

        case Picto::antenna:
            // a mast and its waves
            d.line (0.5f, 0.42f, 0.5f, 0.94f);
            d.lines ({ { 0.32f, 0.94f }, { 0.5f, 0.6f }, { 0.68f, 0.94f } }, 0.6f);
            d.circle (0.5f, 0.38f, 0.05f, true);
            d.stroke (d.arc (0.5f, 0.38f, 0.17f, 0.67f, 2.47f), 0.8f);
            d.stroke (d.arc (0.5f, 0.38f, 0.17f, 3.81f, 5.61f), 0.8f);
            d.stroke (d.arc (0.5f, 0.38f, 0.32f, 0.77f, 2.37f), 0.5f);
            d.stroke (d.arc (0.5f, 0.38f, 0.32f, 3.91f, 5.51f), 0.5f);
            break;

        case Picto::air:
            // the air: a wave that fades, and the hiss under it
            d.stroke (d.wave (0.06f, 0.94f, 0.4f, 0.16f, 2.5f, [] (float ph, float u) { return std::sin (ph * twoPi) * (1.0f - 0.7f * u); }));
            for (int i = 0; i < 9; ++i)
            {
                const float x = 0.1f + 0.1f * (float) i, h = 0.04f + 0.05f * (float) ((i * 7) % 4);
                d.line (x, 0.76f - h, x, 0.76f + h, 0.55f);
            }
            break;

        case Picto::slice:
            // a waveform, cut at its attacks
            d.stroke (d.wave (0.06f, 0.94f, 0.5f, 0.3f, 3.0f, [] (float ph, float) { const float f = ph - std::floor (ph); return std::sin (f * 11.0f) * std::exp (-f * 4.0f); }, 72), 0.75f);
            d.line (0.06f, 0.12f, 0.06f, 0.88f);
            d.line (0.36f, 0.12f, 0.36f, 0.88f);
            d.line (0.65f, 0.12f, 0.65f, 0.88f);
            break;

        case Picto::pads:
            for (int i = 0; i < 16; ++i)
            {
                const float x = 0.08f + 0.22f * (float) (i % 4), y = 0.08f + 0.22f * (float) (i / 4);
                d.rect (x, y, x + 0.17f, y + 0.17f, 0.03f, i == 5 || i == 10, 0.9f);
            }
            break;

        case Picto::sliders:
            d.line (0.1f, 0.25f, 0.9f, 0.25f, 0.5f);
            d.line (0.1f, 0.5f, 0.9f, 0.5f, 0.5f);
            d.line (0.1f, 0.75f, 0.9f, 0.75f, 0.5f);
            d.circle (0.66f, 0.25f, 0.075f, true);
            d.circle (0.3f, 0.5f, 0.075f, true);
            d.circle (0.54f, 0.75f, 0.075f, true);
            break;

        case Picto::playhead:
        {
            for (int i = 0; i < 13; ++i)
            {
                const float x = 0.08f + 0.07f * (float) i;
                const float h = 0.06f + 0.3f * std::abs (std::sin ((float) i * 1.7f + 0.4f)) * (1.0f - (float) i / 16.0f);
                d.line (x, 0.54f - h, x, 0.54f + h, 0.55f);
            }
            d.line (0.4f, 0.12f, 0.4f, 0.94f);
            d.shape ({ { 0.33f, 0.04f }, { 0.47f, 0.04f }, { 0.4f, 0.13f } }, true);
            break;
        }

        case Picto::loop:
            d.rect (0.08f, 0.3f, 0.92f, 0.74f, 0.22f);
            d.shape ({ { 0.46f, 0.2f }, { 0.6f, 0.3f }, { 0.46f, 0.4f } }, true);
            break;

        case Picto::fork:
        {
            juce::Path q;
            q.startNewSubPath (d.p (0.38f, 0.06f));
            q.lineTo (d.p (0.38f, 0.44f));
            q.quadraticTo (d.p (0.38f, 0.6f), d.p (0.5f, 0.6f));
            q.quadraticTo (d.p (0.62f, 0.6f), d.p (0.62f, 0.44f));
            q.lineTo (d.p (0.62f, 0.06f));
            d.stroke (q);
            d.line (0.5f, 0.6f, 0.5f, 0.94f);
            d.stroke (d.arc (0.5f, 0.24f, 0.26f, -1.15f, -0.55f), 0.5f);
            d.stroke (d.arc (0.5f, 0.24f, 0.26f, 0.55f, 1.15f), 0.5f);
            break;
        }

        //------------------------------------------------------------------ the filter's models
        case Picto::off:
            d.circle (0.5f, 0.5f, 0.3f, false, 0.6f);
            d.line (0.29f, 0.71f, 0.71f, 0.29f, 0.6f);
            break;

        case Picto::ladder:
            // the Moog's: two rails, four rungs, a capacitor across each
            d.line (0.26f, 0.06f, 0.26f, 0.94f);
            d.line (0.74f, 0.06f, 0.74f, 0.94f);
            for (int i = 0; i < 4; ++i)
            {
                const float y = 0.2f + 0.2f * (float) i;
                d.line (0.26f, y, 0.465f, y);
                d.line (0.535f, y, 0.74f, y);
                d.capacitor (0.5f, y, 0.06f);
            }
            break;

        case Picto::ms20:
        {
            // the MS-20's: a high-pass, then a low-pass
            d.line (0.0f, 0.5f, 0.04f, 0.5f);
            d.rect (0.04f, 0.3f, 0.44f, 0.7f, 0.05f);
            d.line (0.44f, 0.5f, 0.56f, 0.5f);
            d.rect (0.56f, 0.3f, 0.96f, 0.7f, 0.05f);
            juce::Path hp;
            hp.startNewSubPath (d.p (0.1f, 0.63f));
            hp.quadraticTo (d.p (0.2f, 0.62f), d.p (0.24f, 0.4f));
            hp.lineTo (d.p (0.38f, 0.4f));
            d.stroke (hp);
            juce::Path lp;
            lp.startNewSubPath (d.p (0.62f, 0.4f));
            lp.lineTo (d.p (0.76f, 0.4f));
            lp.quadraticTo (d.p (0.8f, 0.62f), d.p (0.9f, 0.63f));
            d.stroke (lp);
            break;
        }

        case Picto::sem:
            // the SEM's state variable: two integrators (a capacitor across each) in a loop
            d.line (0.0f, 0.5f, 0.12f, 0.5f);
            d.amp (0.12f, 0.34f, 0.5f, 0.14f);
            d.line (0.34f, 0.5f, 0.58f, 0.5f);
            d.amp (0.58f, 0.8f, 0.5f, 0.14f);
            d.line (0.8f, 0.5f, 1.0f, 0.5f);
            d.lines ({ { 0.07f, 0.5f }, { 0.07f, 0.2f }, { 0.195f, 0.2f } });
            d.capacitor (0.23f, 0.2f, 0.07f);
            d.lines ({ { 0.265f, 0.2f }, { 0.4f, 0.2f }, { 0.4f, 0.5f } });
            d.lines ({ { 0.52f, 0.5f }, { 0.52f, 0.2f }, { 0.655f, 0.2f } });
            d.capacitor (0.69f, 0.2f, 0.07f);
            d.lines ({ { 0.725f, 0.2f }, { 0.86f, 0.2f }, { 0.86f, 0.5f } });
            d.lines ({ { 0.93f, 0.5f }, { 0.93f, 0.82f }, { 0.02f, 0.82f }, { 0.02f, 0.5f } }, 0.6f);
            break;

        case Picto::oscar:
        {
            // the OSCar's: two resonances, apart
            d.line (0.04f, 0.88f, 0.96f, 0.88f, 0.35f);
            juce::Path q;
            for (int i = 0; i <= 64; ++i)
            {
                const float x = 0.04f + 0.92f * (float) i / 64.0f;
                auto peak = [] (float u, float centre) { const float k = (u - centre) / 0.04f; return 1.0f / (1.0f + k * k); };
                float y = 0.5f - 0.3f * peak (x, 0.44f) - 0.3f * peak (x, 0.62f) + (x > 0.62f ? (x - 0.62f) * 1.2f : 0.0f);
                y = juce::jlimit (0.08f, 0.88f, y);
                if (i == 0) q.startNewSubPath (d.p (x, y)); else q.lineTo (d.p (x, y));
            }
            d.stroke (q);
            break;
        }

        case Picto::wasp:
            // the Wasp's: a CMOS inverter, a capacitor to the ground
            d.line (0.0f, 0.5f, 0.16f, 0.5f);
            d.amp (0.16f, 0.52f, 0.5f, 0.2f);
            d.circle (0.57f, 0.5f, 0.045f);
            d.line (0.615f, 0.5f, 1.0f, 0.5f);
            d.line (0.8f, 0.5f, 0.8f, 0.64f);
            d.line (0.71f, 0.64f, 0.89f, 0.64f);
            d.line (0.71f, 0.71f, 0.89f, 0.71f);
            d.line (0.8f, 0.71f, 0.8f, 0.8f);
            d.line (0.72f, 0.8f, 0.88f, 0.8f);
            d.line (0.75f, 0.86f, 0.85f, 0.86f);
            d.line (0.78f, 0.92f, 0.82f, 0.92f);
            break;

        case Picto::echo:
        {
            // a blip and its repeats, evenly spaced, each one softer (a tape's heads)
            d.line (0.04f, 0.78f, 0.96f, 0.78f, 0.35f);
            const float xs[3] = { 0.2f, 0.5f, 0.8f }, hs[3] = { 0.56f, 0.38f, 0.22f };
            for (int i = 0; i < 3; ++i)
            {
                juce::Path q;
                q.startNewSubPath (d.p (xs[i] - 0.11f, 0.78f));
                q.quadraticTo (d.p (xs[i], 0.78f - 2.0f * hs[i]), d.p (xs[i] + 0.11f, 0.78f));
                d.stroke (q, i == 0 ? 1.0f : 0.8f - 0.15f * (float) i);
            }
            break;
        }

        case Picto::drop:
            // a tray, an arrow coming down into it
            d.lines ({ { 0.08f, 0.56f }, { 0.08f, 0.9f }, { 0.92f, 0.9f }, { 0.92f, 0.56f } });
            d.line (0.5f, 0.06f, 0.5f, 0.68f);
            d.lines ({ { 0.28f, 0.47f }, { 0.5f, 0.69f }, { 0.72f, 0.47f } });
            break;

        case Picto::weave:
            // three threads crossing over and under each other
            d.stroke (d.wave (0.04f, 0.96f, 0.5f, 0.3f, 1.0f, sinePhase));
            d.stroke (d.wave (0.04f, 0.96f, 0.5f, 0.3f, 1.0f, [] (float ph, float) { return -std::sin (ph * twoPi); }), 0.8f);
            d.line (0.04f, 0.5f, 0.96f, 0.5f, 0.6f);
            break;

        case Picto::cube:
        {
            // a cube seen from above, a point inside it
            const float m = 0.5f, w = 0.4f, h = 0.23f, t = 0.46f;
            d.lines ({ { m - w, m - h + 0.06f }, { m, m - 2.0f * h + 0.06f }, { m + w, m - h + 0.06f }, { m, m + 0.06f }, { m - w, m - h + 0.06f } });
            d.lines ({ { m - w, m - h + 0.06f }, { m - w, m - h + t + 0.06f }, { m, m + t + 0.06f }, { m + w, m - h + t + 0.06f }, { m + w, m - h + 0.06f } });
            d.line (m, m + 0.06f, m, m + t + 0.06f);
            d.circle (m + 0.08f, m - 0.02f, 0.06f, true);
            break;
        }

        case Picto::polivoks:
            // the Polivoks's: an op amp driven to its rails
            d.line (0.0f, 0.36f, 0.14f, 0.36f);
            d.line (0.0f, 0.64f, 0.14f, 0.64f);
            d.amp (0.14f, 0.58f, 0.5f, 0.27f);
            d.line (0.19f, 0.36f, 0.26f, 0.36f);
            d.line (0.19f, 0.64f, 0.26f, 0.64f);
            d.line (0.225f, 0.605f, 0.225f, 0.675f);
            d.line (0.58f, 0.5f, 1.0f, 0.5f);
            d.lines ({ { 0.64f, 0.24f }, { 0.68f, 0.12f }, { 0.74f, 0.12f }, { 0.8f, 0.36f }, { 0.86f, 0.36f }, { 0.92f, 0.12f }, { 0.97f, 0.12f } });
            break;

        //------------------------------------------------------------------ the drive's
        case Picto::tube:
            // a triode: its plate, its grid, its cathode, glowing a little
            d.circle (0.5f, 0.52f, 0.38f, true, 0.1f);
            d.circle (0.5f, 0.52f, 0.38f);
            d.line (0.34f, 0.3f, 0.66f, 0.3f);
            d.line (0.5f, 0.3f, 0.5f, 0.02f);
            d.line (0.02f, 0.5f, 0.2f, 0.5f);
            d.line (0.3f, 0.5f, 0.38f, 0.5f);
            d.line (0.46f, 0.5f, 0.54f, 0.5f);
            d.line (0.62f, 0.5f, 0.7f, 0.5f);
            d.lines ({ { 0.36f, 0.74f }, { 0.38f, 0.7f }, { 0.62f, 0.7f } });
            d.lines ({ { 0.62f, 0.7f }, { 0.62f, 0.82f } });
            d.line (0.5f, 0.9f, 0.5f, 1.0f);
            break;

        case Picto::console:
            for (int i = 0; i < 3; ++i)
            {
                const float x = 0.25f + 0.25f * (float) i, y = (i == 0 ? 0.62f : (i == 1 ? 0.32f : 0.5f));
                d.line (x, 0.08f, x, 0.92f, 0.45f);
                d.rect (x - 0.09f, y - 0.06f, x + 0.09f, y + 0.06f, 0.02f, true);
            }
            break;

        case Picto::reels:
            // a tape machine: two reels, the tape over the head
            for (float x : { 0.27f, 0.73f })
            {
                d.circle (x, 0.36f, 0.21f);
                d.circle (x, 0.36f, side < 36.0f ? 0.07f : 0.04f, true);
                for (int k = 0; k < (side < 36.0f ? 0 : 3); ++k)     // (small: no spokes)
                {
                    const float a = (float) k * twoPi / 3.0f + 0.3f;
                    d.line (x + 0.07f * std::sin (a), 0.36f - 0.07f * std::cos (a), x + 0.16f * std::sin (a), 0.36f - 0.16f * std::cos (a));
                }
            }
            d.lines ({ { 0.122f, 0.508f }, { 0.38f, 0.86f }, { 0.62f, 0.86f }, { 0.878f, 0.508f } });
            d.rect (0.44f, 0.86f, 0.56f, 0.96f, 0.02f, true);
            break;

        //------------------------------------------------------------------ the effects
        case Picto::spectrum:
        {
            const float h[7] = { 0.5f, 0.72f, 0.6f, 0.44f, 0.32f, 0.22f, 0.12f };
            for (int i = 0; i < 7; ++i)
            {
                const float x = 0.08f + 0.13f * (float) i;
                d.rect (x, 0.9f - h[i], x + 0.07f, 0.9f, 0.015f, true);
            }
            break;
        }

        case Picto::shift:
            d.line (0.32f, 0.86f, 0.32f, 0.14f);
            d.lines ({ { 0.18f, 0.3f }, { 0.32f, 0.14f }, { 0.46f, 0.3f } });
            d.line (0.68f, 0.14f, 0.68f, 0.86f);
            d.lines ({ { 0.54f, 0.7f }, { 0.68f, 0.86f }, { 0.82f, 0.7f } });
            break;

        case Picto::chorus:
            d.stroke (d.wave (0.04f, 0.96f, 0.5f, 0.26f, 1.5f, [] (float ph, float) { return std::sin ((ph + 0.14f) * twoPi); }), 0.45f);
            d.stroke (d.wave (0.04f, 0.96f, 0.5f, 0.26f, 1.5f, sinePhase));
            break;

        case Picto::crush:
        {
            // a sine on four steps
            d.stroke (d.wave (0.06f, 0.94f, 0.5f, 0.32f, 1.0f, sinePhase), 0.25f);
            juce::Path q;
            const int n = 10;
            for (int i = 0; i < n; ++i)
            {
                const float x0 = 0.06f + 0.88f * (float) i / (float) n, x1 = 0.06f + 0.88f * (float) (i + 1) / (float) n;
                const float v = std::round (std::sin (((float) i + 0.5f) / (float) n * twoPi) * 2.0f) / 2.0f;
                const float y = 0.5f - 0.32f * v;
                if (i == 0) q.startNewSubPath (d.p (x0, y)); else q.lineTo (d.p (x0, y));
                q.lineTo (d.p (x1, y));
            }
            d.stroke (q);
            break;
        }

        case Picto::space:
        {
            // an impulse and its echoes, dying away
            float x = 0.1f;
            for (int i = 0; i < 10; ++i)
            {
                const float h = 0.76f * std::exp (-0.32f * (float) i);
                d.line (x, 0.88f, x, 0.88f - h, i == 0 ? 1.0f : 0.75f);
                x += 0.15f * std::exp (-0.12f * (float) i);
            }
            break;
        }

        //------------------------------------------------------------------ the modulators
        case Picto::steps:
        {
            const float h[6] = { 0.4f, 0.7f, 0.25f, 0.55f, 0.85f, 0.45f };
            for (int i = 0; i < 6; ++i)
            {
                const float x0 = 0.04f + 0.153f * (float) i, y = 0.92f - 0.8f * h[i];
                d.rect (x0 + 0.02f, y, x0 + 0.133f, 0.92f, 0.015f, true, i == 4 ? 1.0f : 0.7f);
            }
            break;
        }

        case Picto::random:
        {
            // sample and hold: held values, a line through the noise
            const float v[7] = { 0.3f, 0.72f, 0.5f, 0.16f, 0.86f, 0.42f, 0.62f };
            juce::Path q;
            for (int i = 0; i < 7; ++i)
            {
                const float x0 = 0.04f + 0.92f * (float) i / 7.0f, x1 = 0.04f + 0.92f * (float) (i + 1) / 7.0f;
                if (i == 0) q.startNewSubPath (d.p (x0, v[i])); else q.lineTo (d.p (x0, v[i]));
                q.lineTo (d.p (x1, v[i]));
            }
            d.stroke (q);
            break;
        }

        case Picto::follower:
        {
            // a sound and the envelope that follows it
            auto env = [] (float u) { return std::sin (u * juce::MathConstants<float>::pi) * (0.55f + 0.45f * std::sin (u * 2.6f)); };
            d.stroke (d.wave (0.04f, 0.96f, 0.5f, 0.36f, 4.5f, [env] (float ph, float u) { return std::sin (ph * twoPi) * env (u); }, 96), 0.45f);
            d.stroke (d.wave (0.04f, 0.96f, 0.5f, 0.38f, 1.0f, [env] (float, float u) { return env (u); }));
            break;
        }

        //------------------------------------------------------------------ the capture
        case Picto::jack:
            d.line (0.0f, 0.5f, 0.16f, 0.5f);
            d.rect (0.16f, 0.36f, 0.46f, 0.64f, 0.06f);
            d.lines ({ { 0.46f, 0.43f }, { 0.74f, 0.43f } });
            d.lines ({ { 0.46f, 0.57f }, { 0.74f, 0.57f } });
            d.line (0.62f, 0.43f, 0.62f, 0.57f, 0.6f);
            {
                juce::Path q;
                q.startNewSubPath (d.p (0.74f, 0.43f));
                q.quadraticTo (d.p (0.9f, 0.43f), d.p (0.92f, 0.5f));
                q.quadraticTo (d.p (0.9f, 0.57f), d.p (0.74f, 0.57f));
                q.closeSubPath();
                d.stroke (q);
            }
            break;

        case Picto::keys:
            d.rect (0.06f, 0.2f, 0.94f, 0.8f, 0.04f);
            d.line (0.28f, 0.2f, 0.28f, 0.8f, 0.6f);
            d.line (0.5f, 0.2f, 0.5f, 0.8f, 0.6f);
            d.line (0.72f, 0.2f, 0.72f, 0.8f, 0.6f);
            d.rect (0.22f, 0.2f, 0.34f, 0.54f, 0.01f, true);
            d.rect (0.44f, 0.2f, 0.56f, 0.54f, 0.01f, true);
            break;

        case Picto::clock:
            d.circle (0.5f, 0.5f, 0.38f);
            d.line (0.5f, 0.5f, 0.5f, 0.24f);
            d.line (0.5f, 0.5f, 0.69f, 0.6f);
            d.circle (0.5f, 0.5f, 0.035f, true);
            break;

        case Picto::onset:
            d.line (0.06f, 0.14f, 0.06f, 0.86f, 0.45f);
            d.stroke (d.wave (0.06f, 0.96f, 0.5f, 0.38f, 4.5f,
                              [] (float ph, float u) { return std::sin (ph * twoPi) * std::exp (-3.5f * u); }, 96));
            break;

        case Picto::vuMeter:
        {
            d.stroke (d.arc (0.5f, 0.86f, 0.6f, -0.87f, 0.87f));
            for (int i = 0; i < 5; ++i)
            {
                const float a = -0.75f + 0.375f * (float) i;
                d.line (0.5f + 0.5f * std::sin (a), 0.86f - 0.5f * std::cos (a), 0.5f + 0.6f * std::sin (a), 0.86f - 0.6f * std::cos (a), 0.6f);
            }
            const float a = 0.35f;
            d.line (0.5f, 0.86f, 0.5f + 0.62f * std::sin (a), 0.86f - 0.62f * std::cos (a));
            d.circle (0.5f, 0.86f, 0.05f, true);
            break;
        }

        case Picto::resample:
        {
            // what comes out, around and back in
            const float a0 = 0.5f, a1 = 5.4f, r = 0.32f;
            d.stroke (d.arc (0.5f, 0.5f, r, a0, a1));
            const juce::Point<float> end { 0.5f + r * std::sin (a1), 0.5f - r * std::cos (a1) };
            const juce::Point<float> dir { std::cos (a1), std::sin (a1) }, nrm { std::sin (a1), -std::cos (a1) };
            d.shape ({ end + dir * 0.13f, end + nrm * 0.1f, end - nrm * 0.1f }, true);
            d.circle (0.5f, 0.5f, 0.12f, true);
            break;
        }

        case Picto::record:
            d.circle (0.5f, 0.5f, 0.3f, true);
            break;
    }
}

Picto filterModelPicto (int model)
{
    const Picto p[] = { Picto::off, Picto::ladder, Picto::ms20, Picto::sem, Picto::oscar, Picto::wasp, Picto::polivoks, Picto::cube };
    return p[juce::jlimit (0, 7, model)];
}

Picto filterTypePicto (int type)
{
    const Picto p[] = { Picto::lowpass, Picto::bandpass, Picto::highpass, Picto::notch };
    return p[juce::jlimit (0, 3, type)];
}

Picto driveModelPicto (int model)
{
    const Picto p[] = { Picto::off, Picto::tube, Picto::console, Picto::reels };
    return p[juce::jlimit (0, 3, model)];
}

Picto stagePicto (int stage, int driveModel)
{
    switch (stage)
    {
        case 0:  return driveModel > 0 ? driveModelPicto (driveModel) : Picto::tube;
        case 1:  return Picto::spectrum;
        case 2:  return Picto::shift;
        case 3:  return Picto::chorus;
        case 4:  return Picto::crush;
        case 5:  return Picto::space;
        case 6:  return Picto::echo;
        default: return Picto::none;
    }
}

Picto modPicto (int type)
{
    const Picto p[] = { Picto::sine, Picto::steps, Picto::random, Picto::envelope, Picto::follower };
    return p[juce::jlimit (0, 4, type)];
}
}
