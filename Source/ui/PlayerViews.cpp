#include "PlayerViews.h"
#include "../dsp/VAFilter.h"
#include "../dsp/Modulators.h"

#include <cmath>
#include <limits>

namespace ui
{
namespace
{
    const char* modTypes[] = { "ONDE", "PAS", "HASARD", "ENV", "SUIVEUR" };

    // a setting as it is now (moved by the scenes and the modulators), in its own unit
    float now (const Player& p, int id)
    {
        const float n = p.modulatedNorm (id);
        return n >= 0.0f ? snd::fromNorm (id, n) : p.get (id);
    }

    void tag (juce::Graphics& g, const juce::String& t, juce::Point<int> at, juce::Colour band, juce::Colour ink = px::ink)
    {
        const auto f = px::label (8.0f, true);
        const int w = juce::GlyphArrangement::getStringWidthInt (f, t) + 10;
        g.setColour (band);
        g.fillRect (at.x, at.y, w, 14);
        px::drawText (g, t, { at.x + 5, at.y, w - 6, 14 }, juce::Justification::centredLeft, f, ink);
    }

    void dottedH (juce::Graphics& g, int x0, int x1, int y, juce::Colour c)
    {
        g.setColour (c);
        for (int x = x0; x < x1; x += 4)
            g.fillRect (x, y, 2, 1);
    }

    void dottedV (juce::Graphics& g, int x, int y0, int y1, juce::Colour c)
    {
        g.setColour (c);
        for (int y = y0; y < y1; y += 4)
            g.fillRect (x, y, 1, 2);
    }
}

//============================================================================== FilterView
void FilterView::paint (juce::Graphics& g)
{
    auto& p = proc.player;
    auto r = getLocalBounds();
    auto curve = r.removeFromLeft (r.getWidth() * 3 / 4);
    r.removeFromLeft (10);
    auto driveBox = r;

    g.setColour (px::black);
    g.fillRect (curve);
    px::frameRect (g, curve, px::frame, 2);
    auto plot = curve.reduced (10, 18);
    const float topDb = 18.0f, bottomDb = -42.0f;
    auto yOf = [&] (float db) { return plot.getY() + juce::roundToInt ((topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb) * (float) plot.getHeight()); };
    auto xOf = [&] (float hz) { return plot.getX() + juce::roundToInt (std::log (hz / 20.0f) / std::log (1000.0f) * (float) plot.getWidth()); };
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        dottedV (g, xOf (hz), plot.getY(), plot.getBottom(), px::frame);
        px::drawText (g, hz < 1000.0f ? "100" : hz < 10000.0f ? "1K" : "10K", { xOf (hz) + 3, plot.getBottom() - 12, 40, 12 },
                      juce::Justification::centredLeft, px::label (7.0f), px::dim);
    }
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f })
        dottedH (g, plot.getX(), plot.getRight(), yOf (db), db > -1.0f ? px::faint : px::frame);

    dsp::FilterSettings fs;
    fs.model = (dsp::FilterModel) (int) p.get (snd::fModel);
    fs.type = (dsp::FilterType) (int) now (p, snd::fType);
    fs.res = now (p, snd::reso);
    fs.drive = now (p, snd::fDrive);
    fs.sep = now (p, snd::sep);
    fs.fm = now (p, snd::fm);
    const auto models = snd::choicesOf (snd::fModel);
    const auto types = snd::choicesOf (snd::fType);
    if (fs.model == dsp::FilterModel::off)
    {
        px::drawText (g, "AUCUN FILTRE : CHOISIS UN MODÈLE (MOOG, MS-20, SEM, OSCAR, WASP, POLIVOKS)"_fr, plot,
                      juce::Justification::centred, px::label (9.0f), px::dim);
    }
    else
    {
        // the cutoff now: the knob, the scenes, the modulators, and the newest voice's envelope
        const float base = now (p, snd::cutoff);
        const float envOct = now (p, snd::fEnv) * 6.0f * p.filterEnv.load();
        const float hz = juce::jlimit (20.0f, 20000.0f, base * std::exp2 (envOct));
        int prevY = -1;
        for (int x = plot.getX(); x < plot.getRight(); x += 2)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) (x - plot.getX()) / (float) plot.getWidth());
            const int y = yOf (dsp::VAFilter::responseDb (fs, hz, f));
            px::dither (g, { x, y, 2, plot.getBottom() - y }, px::cyan.withAlpha (0.55f), 1);
            const int a = prevY < 0 ? y : juce::jmin (prevY, y), b = prevY < 0 ? y : juce::jmax (prevY, y);
            g.setColour (px::cyan);
            g.fillRect (x, a, 2, juce::jmax (2, b - a + 1));
            prevY = y;
        }
        g.setColour (px::red);
        g.fillRect (xOf (hz) - 1, plot.getY(), 2, plot.getHeight());
        const int tx = xOf (hz) + 4;
        tag (g, ui::soundText (snd::cutoff, hz), { tx + 80 > plot.getRight() ? xOf (hz) - 84 : tx, plot.getY() + 16 }, px::red);
        tag (g, juce::String::fromUTF8 (models[(size_t) fs.model].c_str()) + "  " + juce::String::fromUTF8 (types[(size_t) fs.type].c_str()),
             { curve.getX() + 6, curve.getY() + 3 }, px::cyan);
        if (fs.fm > 0.001f)
            tag (g, "FM " + juce::String (juce::roundToInt (fs.fm * 100.0f)) + " %", { curve.getRight() - 70, curve.getY() + 3 }, px::grey);
    }

    // the drive: its curve (a picture of how hard it pushes, not its model's exact one)
    g.setColour (px::black);
    g.fillRect (driveBox);
    px::frameRect (g, driveBox, px::frame, 2);
    const int dm = (int) p.get (snd::dModel);
    const auto dmodels = snd::choicesOf (snd::dModel);
    tag (g, "DRIVE " + juce::String::fromUTF8 (dmodels[(size_t) dm].c_str()), { driveBox.getX() + 6, driveBox.getY() + 3 }, dm > 0 ? px::accent : px::faint);
    auto box = driveBox.reduced (14, 24).withTrimmedTop (4);
    const int side = juce::jmin (box.getWidth(), box.getHeight());
    box = box.withSizeKeepingCentre (side, side);
    dottedH (g, box.getX(), box.getRight(), box.getCentreY(), px::frame);
    dottedV (g, box.getCentreX(), box.getY(), box.getBottom(), px::frame);
    const float k = dm > 0 ? 1.0f + now (p, snd::dAmt) * 9.0f : 1.0f;
    const float norm = std::tanh (k);
    for (int x = 0; x < side; x += 2)
    {
        const float in = (float) x / (float) side * 2.0f - 1.0f;
        const float out = dm > 0 ? std::tanh (in * k) / norm : in;
        const int y = box.getCentreY() - juce::roundToInt (out * (float) side * 0.5f);
        g.setColour (dm > 0 ? px::accent : px::faint);
        g.fillRect (box.getX() + x, y - 1, 2, 2);
    }
}

//============================================================================== ChainView
void ChainView::paint (juce::Graphics& g)
{
    auto& p = proc.player;
    auto r = getLocalBounds();
    auto strip = r.removeFromTop (72);
    r.removeFromTop (10);
    const int ids[6] = { snd::dModel, snd::spOn, snd::piOn, snd::chOn, snd::crOn, snd::rvOn };
    const char* names[6] = { "DRIVE", "SPECTRE", "PITCH", "CHORUS", "CRUSH", "ESPACE" };
    const juce::Colour colours[6] = { px::accent, px::accent, px::accent, px::accent, px::accent, px::accent };
    const int w = (strip.getWidth() - 5 * 14) / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto b = strip.removeFromLeft (w);
        blocks[(size_t) i] = b;
        if (i < 5)
        {
            auto gap = strip.removeFromLeft (14);
            g.setColour (px::dim);
            for (int k = 0; k < 3; ++k)
                g.fillRect (gap.getX() + 3 + k * 2, gap.getCentreY() - 3 + k, 2, 6 - 2 * k);
        }
        const bool on = p.get (ids[i]) > 0.5f;
        g.setColour (on ? colours[i] : px::panel);
        g.fillRect (b);
        px::frameRect (g, b, on ? colours[i] : px::frame, 2);
        px::drawText (g, names[i], b.removeFromTop (24).reduced (8, 0), juce::Justification::centredLeft, px::label (10.0f, true), on ? px::ink : px::dim);
        juce::String what;
        switch (i)
        {
            case 0: what = on ? ui::soundText (snd::dModel, p.get (snd::dModel)) + " " + ui::soundText (snd::dAmt, now (p, snd::dAmt)) : "OFF"; break;
            case 1: what = on ? juce::String ((int) p.get (snd::spVoices)) + " OSC" + (now (p, snd::spFreeze) > 0.5f ? juce::String (" GEL") : juce::String()) : "OFF"; break;
            case 2: what = on ? ui::soundText (snd::piA, now (p, snd::piA)) + (p.get (snd::piLevelB) > 0.01f ? " " + ui::soundText (snd::piB, now (p, snd::piB)) : juce::String()) : "OFF"; break;
            case 3: what = on ? ui::soundText (snd::chMode, p.get (snd::chMode)) : "OFF"; break;
            case 4: what = on ? ui::soundText (snd::crBits, now (p, snd::crBits)) : "OFF"; break;
            default: what = on ? ui::soundText (snd::rvDecay, now (p, snd::rvDecay)) : "OFF"; break;
        }
        px::drawText (g, what, b.reduced (8, 2), juce::Justification::centredLeft, px::text (21.0f), on ? px::ink : px::faint);
    }

    // ESPACE: the output, as a scope (held still on a rising zero crossing)
    if (! showSpectrum)
    {
        g.setColour (px::black);
        g.fillRect (r);
        px::frameRect (g, r, px::frame, 2);
        tag (g, "LA SORTIE", { r.getX() + 6, r.getY() + 3 }, px::accent);
        auto plot = r.reduced (10, 22);
        dottedH (g, plot.getX(), plot.getRight(), plot.getCentreY(), px::frame);
        constexpr int N = Player::scopeSize, shownN = 1200;
        const int head = p.scopeWrite.load();
        auto at = [&] (int i) { return p.scope[(size_t) ((head + i) & (N - 1))].load(); };   // i from -N
        int start = -shownN;
        for (int i = -shownN - 1; i > -N + 1; --i)
            if (at (i - 1) < 0.0f && at (i) >= 0.0f)
            {
                start = i;
                break;
            }
        float peak = 1.0e-4f;
        for (int i = 0; i < shownN; ++i)
            peak = juce::jmax (peak, std::abs (at (start + i)));
        const float scale = 0.45f / juce::jmax (0.05f, peak);
        int prevY = -1;
        const int cols = plot.getWidth() / 2;
        for (int c = 0; c < cols; ++c)
        {
            const float v = at (start + c * shownN / cols);
            const int y = plot.getCentreY() - juce::roundToInt (v * scale * (float) plot.getHeight());
            const int a = prevY < 0 ? y : juce::jmin (prevY, y), b = prevY < 0 ? y : juce::jmax (prevY, y);
            g.setColour (px::accent);
            g.fillRect (plot.getX() + c * 2, a, 2, juce::jmax (2, b - a + 1));
            prevY = y;
        }
        return;
    }
    // SPECTRE: its oscillators now, on a log scale of frequency
    g.setColour (px::black);
    g.fillRect (r);
    px::frameRect (g, r, px::frame, 2);
    auto plot = r.reduced (10, 16);
    const bool on = p.get (snd::spOn) > 0.5f;
    tag (g, on ? "SPECTRE : LES OSCILLATEURS" : "SPECTRE ÉTEINT"_fr, { r.getX() + 6, r.getY() + 3 }, on ? px::cyan : px::faint);
    auto xOf = [&] (float hz) { return plot.getX() + juce::roundToInt (std::log (hz / 20.0f) / std::log (1000.0f) * (float) plot.getWidth()); };
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
        dottedV (g, xOf (hz), plot.getY(), plot.getBottom(), px::frame);
    if (! on)
        return;
    // the band it listens to
    const float centre = 40.0f * std::pow (250.0f, now (p, snd::spCentre));
    const float halfOct = 0.5f + 5.0f * now (p, snd::spWidth);
    const int bx0 = xOf (juce::jmax (20.0f, centre / std::exp2 (halfOct))), bx1 = xOf (juce::jmin (20000.0f, centre * std::exp2 (halfOct)));
    px::dither (g, { bx0, plot.getY(), bx1 - bx0, plot.getHeight() }, px::cyan.withAlpha (0.25f), 1);
    float loudest = 1.0e-6f;
    for (int i = 0; i < Player::maxPeaks; ++i)
        loudest = juce::jmax (loudest, p.peakLevel[(size_t) i].load());
    for (int i = 0; i < Player::maxPeaks; ++i)
    {
        const float hz = p.peakHz[(size_t) i].load(), lv = p.peakLevel[(size_t) i].load();
        if (hz < 20.0f || hz > 20000.0f || lv <= 0.0f)
            continue;
        const float db = juce::jmax (-48.0f, 20.0f * std::log10 (lv / loudest));
        const int h = juce::roundToInt ((1.0f + db / 48.0f) * (float) plot.getHeight());
        g.setColour (px::cyan);
        g.fillRect (xOf (hz) - 1, plot.getBottom() - h, 3, h);
    }
}

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    const int ids[6] = { snd::dModel, snd::spOn, snd::piOn, snd::chOn, snd::crOn, snd::rvOn };
    for (int i = 0; i < 6; ++i)
        if (blocks[(size_t) i].contains (e.getPosition()))
        {
            auto& p = proc.player;
            if (i == 0)
                ui::setSound (proc, snd::dModel, p.get (snd::dModel) > 0.5f ? 0.0f : 3.0f);   // (on: the tape)
            else
                ui::setSound (proc, ids[i], p.get (ids[i]) > 0.5f ? 0.0f : 1.0f);
            repaint();
            return;
        }
}

//============================================================================== ModView
void ModView::layout()
{
    auto r = getLocalBounds();
    const int type = (int) proc.player.get (snd::modParam (selected, snd::mType));
    const bool steps = type == (int) dsp::ModType::steps, random = type == (int) dsp::ModType::random;
    stepsArea = {};
    redraw = {};
    if (steps)
        stepsArea = r.removeFromBottom (96);
    else if (random)
        redraw = r.removeFromBottom (34).removeFromLeft (220).reduced (0, 2);
    if (steps || random)
        r.removeFromBottom (8);
    routesArea = r.removeFromRight (juce::jmin (330, r.getWidth() * 2 / 5));
    r.removeFromRight (10);
    lanes = r;
}

void ModView::refresh()
{
    for (int m = 0; m < snd::numMods; ++m)
    {
        auto& h = history[(size_t) m];
        h.push_back (proc.player.modValue[(size_t) m].load());
        while (h.size() > 120)
            h.pop_front();
    }
    repaint();
}

void ModView::paint (juce::Graphics& g)
{
    layout();
    auto& p = proc.player;
    // the lanes
    const int lh = lanes.getHeight() / snd::numMods;
    for (int m = 0; m < snd::numMods; ++m)
    {
        auto lane = juce::Rectangle<int> (lanes.getX(), lanes.getY() + m * lh, lanes.getWidth(), lh - 4);
        g.setColour (m == selected ? px::cell : px::black);
        g.fillRect (lane);
        px::frameRect (g, lane, m == selected ? px::mod[m] : px::frame, 2);
        const int type = (int) p.get (snd::modParam (m, snd::mType));
        const bool uni = type >= (int) dsp::ModType::envelope || p.get (snd::modParam (m, snd::mPolarity)) > 0.5f;
        auto plot = lane.reduced (8, 6).withTrimmedLeft (110);
        dottedH (g, plot.getX(), plot.getRight(), uni ? plot.getBottom() - 1 : plot.getCentreY(), px::frame);
        const auto& h = history[(size_t) m];
        bool routed = false;
        for (int k = 0; k < snd::routesPerMod; ++k)
            routed = routed || p.routes[(size_t) m][(size_t) k].dest.load() >= 0;
        int prevY = -1;
        const float depth = juce::jmax (0.05f, p.get (snd::modParam (m, snd::mAmt)));
        for (size_t i = 0; i < h.size(); ++i)
        {
            const float v = h[i] / depth;
            const float yn = uni ? 1.0f - juce::jlimit (0.0f, 1.0f, v) : 0.5f - 0.5f * juce::jlimit (-1.0f, 1.0f, v);
            const int x = plot.getX() + (int) ((float) (i + 120 - h.size()) / 120.0f * (float) plot.getWidth());
            const int y = plot.getY() + juce::roundToInt (yn * (float) (plot.getHeight() - 2));
            const int a = prevY < 0 ? y : juce::jmin (prevY, y), b = prevY < 0 ? y : juce::jmax (prevY, y);
            g.setColour (px::mod[m].withAlpha (routed ? 1.0f : 0.45f));
            g.fillRect (x, a, 2, juce::jmax (2, b - a + 1));
            prevY = y;
        }
        px::modSign (g, m, { lane.getX() + 16, lane.getY() + 15 }, 10, px::mod[m]);
        px::drawText (g, "M" + juce::String (m + 1) + " " + modTypes[juce::jlimit (0, 4, type)], { lane.getX() + 28, lane.getY() + 6, 90, 18 },
                      juce::Justification::centredLeft, px::label (9.0f, true), m == selected ? px::white : px::grey);
        int count = 0;
        for (int k = 0; k < snd::routesPerMod; ++k)
            count += p.routes[(size_t) m][(size_t) k].dest.load() >= 0;
        px::drawText (g, count > 0 ? juce::String (count) + (count > 1 ? " ROUTES" : " ROUTE") : juce::String ("AUCUNE ROUTE"),
                      { lane.getX() + 10, lane.getY() + 24, 100, 14 }, juce::Justification::centredLeft, px::label (7.0f), px::dim);
    }

    // the routes of the one chosen
    g.setColour (px::black);
    g.fillRect (routesArea);
    px::frameRect (g, routesArea, px::mod[selected], 2);
    auto ra = routesArea.reduced (8, 6);
    auto head = ra.removeFromTop (20);
    px::modSign (g, selected, { head.getX() + 6, head.getCentreY() }, 10, px::mod[selected]);
    px::drawText (g, "M" + juce::String (selected + 1) + " : SES ROUTES", head.withTrimmedLeft (16), juce::Justification::centredLeft, px::label (9.0f, true), px::white);
    ra.removeFromTop (4);
    rows.clear();
    bool any = false;
    for (int k = 0; k < snd::routesPerMod; ++k)
    {
        const int d = p.routes[(size_t) selected][(size_t) k].dest.load();
        if (d < 0)
            continue;
        any = true;
        const float a = p.routes[(size_t) selected][(size_t) k].amount.load();
        auto row = ra.removeFromTop (24);
        RouteRow rr { d, {}, {} };
        rr.cross = row.removeFromRight (20);
        auto val = row.removeFromRight (44);
        rr.bar = row.removeFromRight (juce::jmin (110, row.getWidth() / 2)).reduced (0, 6);
        px::drawText (g, ui::soundName (d), row.reduced (2, 0), juce::Justification::centredLeft, px::label (8.0f), px::grey);
        g.setColour (px::cell);
        g.fillRect (rr.bar);
        g.setColour (px::faint);
        g.fillRect (rr.bar.getCentreX(), rr.bar.getY() - 2, 1, rr.bar.getHeight() + 4);
        const int w = juce::roundToInt (std::abs (a) * (float) rr.bar.getWidth() * 0.5f);
        g.setColour (px::mod[selected]);
        g.fillRect (a < 0 ? rr.bar.getCentreX() - w : rr.bar.getCentreX(), rr.bar.getY(), juce::jmax (2, w), rr.bar.getHeight());
        px::drawText (g, juce::String (a > 0 ? "+" : a < 0 ? "-" : "") + juce::String (juce::roundToInt (std::abs (a) * 100.0f)), val,
                      juce::Justification::centredRight, px::text (20.0f), px::white);
        g.setColour (px::dim);
        for (int i = 0; i < 4; ++i)
        {
            g.fillRect (rr.cross.getCentreX() - 4 + i * 2, rr.cross.getCentreY() - 4 + i * 2, 2, 2);
            g.fillRect (rr.cross.getCentreX() + 2 - i * 2, rr.cross.getCentreY() - 4 + i * 2, 2, 2);
        }
        rows.push_back (rr);
        ra.removeFromTop (2);
    }
    if (! any)
        px::drawText (g, "CLIQUE M" + juce::String (selected + 1) + " EN BAS, PUIS TOURNE LES RÉGLAGES À MODULER"_fr, ra.withHeight (40),
                      juce::Justification::centredLeft, px::label (8.0f), px::dim);

    // PAS: its steps, drawn with the mouse
    if (! stepsArea.isEmpty())
    {
        g.setColour (px::black);
        g.fillRect (stepsArea);
        px::frameRect (g, stepsArea, px::frame, 2);
        auto in = stepsArea.reduced (8, 8);
        const int n = juce::jlimit (1, 16, (int) p.get (snd::modParam (selected, snd::mLen)));
        const int cur = p.modStep[(size_t) selected].load();
        const int sw = in.getWidth() / 16;
        dottedH (g, in.getX(), in.getRight(), in.getCentreY(), px::frame);
        for (int i = 0; i < 16; ++i)
        {
            auto c = juce::Rectangle<int> (in.getX() + i * sw, in.getY(), sw - 4, in.getHeight());
            const float v = p.steps[(size_t) selected][(size_t) i].load();
            const int h = juce::roundToInt (std::abs (v) * (float) c.getHeight() * 0.5f);
            const auto col = i >= n ? px::faint : i == cur ? px::white : px::mod[selected];
            g.setColour (col);
            g.fillRect (c.getX(), v >= 0 ? c.getCentreY() - h : c.getCentreY(), c.getWidth(), juce::jmax (2, h));
        }
    }
    if (! redraw.isEmpty())
    {
        px::button (g, redraw, false, false, px::mod[selected]);
        px::drawText (g, "NOUVEAU TIRAGE", redraw, juce::Justification::centred, px::label (9.0f, true), px::white);
    }
}

void ModView::mouseDown (const juce::MouseEvent& e)
{
    layout();
    auto& p = proc.player;
    const auto pos = e.getPosition();
    dragRoute = -1;
    if (lanes.contains (pos))
    {
        const int m = juce::jlimit (0, snd::numMods - 1, (pos.y - lanes.getY()) / juce::jmax (1, lanes.getHeight() / snd::numMods));
        selected = m;
        if (onSelect)
            onSelect (m);
        repaint();
        return;
    }
    for (auto& rr : rows)
    {
        if (rr.cross.contains (pos))
        {
            p.setRoute (selected, rr.dest, 0.0f);
            if (arm.changed) arm.changed();
            repaint();
            return;
        }
        if (rr.bar.expanded (0, 6).contains (pos))
        {
            dragRoute = rr.dest;
            dragStart = p.routeAmount (selected, rr.dest);
            mouseDrag (e);
            return;
        }
    }
    if (redraw.contains (pos))
    {
        p.seeds[(size_t) selected] = (uint32_t) juce::Random::getSystemRandom().nextInt() | 1u;
        repaint();
        return;
    }
    if (stepsArea.contains (pos))
        mouseDrag (e);
}

void ModView::mouseDrag (const juce::MouseEvent& e)
{
    auto& p = proc.player;
    const auto pos = e.getPosition();
    if (dragRoute >= 0)
    {
        for (auto& rr : rows)
            if (rr.dest == dragRoute)
            {
                const float a = juce::jlimit (-1.0f, 1.0f, (float) (pos.x - rr.bar.getCentreX()) / ((float) rr.bar.getWidth() * 0.5f));
                p.setRoute (selected, dragRoute, a);
                if (arm.changed) arm.changed();
                repaint();
            }
        return;
    }
    if (! stepsArea.isEmpty() && stepsArea.expanded (0, 20).contains (pos))
    {
        auto in = stepsArea.reduced (8, 8);
        const int i = juce::jlimit (0, 15, (pos.x - in.getX()) / juce::jmax (1, in.getWidth() / 16));
        float v = juce::jlimit (-1.0f, 1.0f, (float) (in.getCentreY() - pos.y) / ((float) in.getHeight() * 0.5f));
        if (std::abs (v) < 0.06f)
            v = 0.0f;
        p.steps[(size_t) selected][(size_t) i] = v;
        repaint();
    }
}

//============================================================================== SceneView
juce::Rectangle<int> SceneView::padArea() const
{
    auto r = getLocalBounds();
    return r.removeFromLeft (juce::jmin (r.getHeight(), r.getWidth() / 2));
}

juce::Rectangle<int> SceneView::padRect (int i) const
{
    const auto a = padArea();
    const int s = a.getWidth() / 4, t = a.getHeight() / 4;
    return { a.getX() + (i % 4) * s, a.getY() + (i / 4) * t, s - 6, t - 6 };
}

void SceneView::paint (juce::Graphics& g)
{
    auto& p = proc.player;
    const int a = (int) p.get (snd::sceneA) - 1, b = (int) p.get (snd::sceneB) - 1;
    for (int i = 0; i < snd::numScenes; ++i)
    {
        const auto c = padRect (i);
        const bool armed = arm.scene == i, holds = p.sceneHolds (i);
        g.setColour (armed ? px::violet : px::cell);
        g.fillRect (c);
        px::frameRect (g, c, armed || holds ? px::violet : px::frame, 2);
        px::drawText (g, juce::String (i + 1), c.reduced (6, 4), juce::Justification::topLeft, px::text (24.0f), armed ? px::ink : holds ? px::white : px::dim);
        if (i == a || i == b)
            tag (g, i == a && i == b ? "A B" : i == a ? "A" : "B", { c.getRight() - (i == a && i == b ? 30 : 18), c.getBottom() - 18 },
                 armed ? px::ink : px::violet, armed ? px::violet : px::ink);
        if (holds && ! armed)
        {
            g.setColour (px::violet);
            g.fillRect (c.getX() + 8, c.getBottom() - 12, 6, 6);
        }
    }

    // what A and B hold
    auto r = getLocalBounds();
    r.removeFromLeft (padArea().getWidth() + 10);
    g.setColour (px::black);
    g.fillRect (r);
    px::frameRect (g, r, px::frame, 2);
    auto in = r.reduced (10, 8);
    px::drawText (g, "SCÈNE "_fr + juce::String (a + 1) + "  →  SCÈNE "_fr + juce::String (b + 1), in.removeFromTop (20),
                  juce::Justification::centredLeft, px::label (10.0f, true), px::violet);
    in.removeFromTop (4);
    int shown = 0;
    for (int id = 0; id < snd::count && in.getHeight() >= 18; ++id)
    {
        const float la = p.sceneLock (a, id), lb = p.sceneLock (b, id);
        if (std::isnan (la) && std::isnan (lb))
            continue;
        auto row = in.removeFromTop (20);
        const auto own = p.get (id);
        px::drawText (g, ui::soundName (id), row.removeFromLeft (row.getWidth() * 2 / 5), juce::Justification::centredLeft, px::label (8.0f), px::grey);
        px::drawText (g, ui::soundText (id, std::isnan (la) ? own : la) + "  →  " + ui::soundText (id, std::isnan (lb) ? own : lb), row,
                      juce::Justification::centredLeft, px::text (20.0f), px::white);
        ++shown;
    }
    if (shown == 0)
        px::drawText (g, "CLIQUE UNE SCÈNE, TOURNE LES RÉGLAGES À Y VERROUILLER, RECLIQUE-LA. LE FADER EN BAS VA DE A À B."_fr,
                      in.withHeight (60), juce::Justification::topLeft, px::label (8.0f), px::dim);
}

void SceneView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < snd::numScenes; ++i)
        if (padRect (i).contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem (1, "Mettre en scène A"_fr);
                m.addItem (2, "Mettre en scène B"_fr);
                m.addSeparator();
                m.addItem (3, "Copier");
                m.addItem (4, "Coller", clipboard.isArray());
                m.addItem (5, "Vider la scène"_fr, proc.player.sceneHolds (i));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, i] (int r)
                {
                    auto& p = proc.player;
                    if (r == 1) ui::setSound (proc, snd::sceneA, (float) (i + 1));
                    if (r == 2) ui::setSound (proc, snd::sceneB, (float) (i + 1));
                    if (r == 3)
                    {
                        juce::Array<juce::var> v;
                        for (int id = 0; id < snd::count; ++id)
                            v.add ((double) p.sceneLock (i, id));
                        clipboard = v;
                    }
                    if (r == 4 && clipboard.isArray())
                        for (int id = 0; id < snd::count && id < clipboard.size(); ++id)
                            p.setSceneLock (i, id, (float) (double) clipboard[id]);
                    if (r == 5)
                        p.clearScene (i);
                    if (arm.changed) arm.changed();
                    repaint();
                });
                return;
            }
            arm.scene = arm.scene == i ? -1 : i;
            arm.mod = -1;
            if (arm.changed) arm.changed();
            repaint();
            return;
        }
}

//============================================================================== PlayerStrip
void PlayerStrip::resized()
{
    auto r = getLocalBounds();
    if (clockOnly)
    {
        auto clock = r;
        clockButton = clock.removeFromLeft (58).withSizeKeepingCentre (58, 52);
        clock.removeFromLeft (8);
        clockText = clock;
        for (auto& b : modButtons) b = {};
        fader = sceneA = sceneB = {};
        return;
    }
    clockButton = clockText = {};
    for (int m = 0; m < snd::numMods; ++m)
    {
        modButtons[(size_t) m] = r.removeFromLeft (58).withSizeKeepingCentre (58, 56);
        r.removeFromLeft (6);
    }
    r.removeFromLeft (10);
    r.removeFromRight (12);
    auto f = r.withSizeKeepingCentre (r.getWidth(), 40);
    sceneA = f.removeFromLeft (46);
    sceneB = f.removeFromRight (46);
    fader = f.reduced (8, 0);
}

void PlayerStrip::paint (juce::Graphics& g)
{
    auto& p = proc.player;
    if (! clockOnly)
    {
        for (int m = 0; m < snd::numMods; ++m)
        {
            const auto b = modButtons[(size_t) m];
            const bool armed = arm.mod == m;
            g.setColour (armed ? px::mod[m] : px::cell);
            g.fillRect (b);
            px::frameRect (g, b, px::mod[m], 2);
            px::modSign (g, m, { b.getX() + 14, b.getY() + 14 }, 10, armed ? px::ink : px::mod[m]);
            px::drawText (g, "M" + juce::String (m + 1), { b.getX() + 24, b.getY() + 6, 30, 16 }, juce::Justification::centredLeft,
                          px::label (10.0f, true), armed ? px::ink : px::white);
            // its value now
            const float v = p.modValue[(size_t) m].load();
            auto bar = juce::Rectangle<int> (b.getX() + 6, b.getBottom() - 16, b.getWidth() - 12, 8);
            g.setColour (armed ? px::ink : px::black);
            g.fillRect (bar);
            const int type = (int) p.get (snd::modParam (m, snd::mType));
            const bool uni = type >= (int) dsp::ModType::envelope || p.get (snd::modParam (m, snd::mPolarity)) > 0.5f;
            const float depth = juce::jmax (0.05f, p.get (snd::modParam (m, snd::mAmt)));
            if (uni)
            {
                const int w = juce::roundToInt (juce::jlimit (0.0f, 1.0f, v / depth) * (float) bar.getWidth());
                g.setColour (armed ? px::white : px::mod[m]);
                g.fillRect (bar.withWidth (juce::jmax (2, w)));
            }
            else
            {
                const int w = juce::roundToInt (juce::jlimit (-1.0f, 1.0f, v / depth) * (float) bar.getWidth() * 0.5f);
                g.setColour (armed ? px::white : px::mod[m]);
                g.fillRect (w < 0 ? bar.getCentreX() + w : bar.getCentreX(), bar.getY(), juce::jmax (2, std::abs (w)), bar.getHeight());
            }
        }

        // the crossfader, A and B
        const int a = (int) p.get (snd::sceneA), b = (int) p.get (snd::sceneB);
        for (int side = 0; side < 2; ++side)
        {
            const auto box = side == 0 ? sceneA : sceneB;
            const bool armed = arm.scene == (side == 0 ? a : b) - 1;
            g.setColour (armed ? px::violet : px::cell);
            g.fillRect (box);
            px::frameRect (g, box, px::violet, 2);
            px::drawText (g, (side == 0 ? "A " : "B ") + juce::String (side == 0 ? a : b), box, juce::Justification::centred, px::text (22.0f),
                          armed ? px::ink : px::violet);
        }
        g.setColour (px::black);
        g.fillRect (fader);
        px::frameRect (g, fader, px::frame, 2);
        const float x = juce::jlimit (0.0f, 1.0f, p.get (snd::xfade));
        const float moved = p.modulatedNorm (snd::xfade);
        auto track = fader.reduced (10, 17);
        g.setColour (px::faint);
        g.fillRect (track);
        for (int k = 0; k <= 8; ++k)
            g.fillRect (track.getX() + k * track.getWidth() / 8, fader.getY() + 6, 2, 4);
        const int hx = track.getX() + juce::roundToInt (x * (float) track.getWidth());
        g.setColour (px::violet);
        g.fillRect (hx - 8, fader.getY() + 6, 16, fader.getHeight() - 12);
        if (moved >= 0.0f)
        {
            const int mx = track.getX() + juce::roundToInt (moved * (float) track.getWidth());
            g.setColour (px::white);
            g.fillRect (mx - 2, fader.getY() + 4, 4, fader.getHeight() - 8);
        }
    }

    if (! clockOnly)
        return;
    // the clock
    auto& c = proc.clock;
    const bool on = c.isRunning();
    const bool host = c.getMode() == MidiClock::Mode::host;
    g.setColour (on ? px::red : px::cell);
    g.fillRect (clockButton);
    px::frameRect (g, clockButton, on ? px::red : px::frame, 2);
    g.setColour (on ? px::ink : host ? px::faint : px::white);
    if (on)
        g.fillRect (clockButton.withSizeKeepingCentre (14, 14));
    else
        for (int k = 0; k < 8; ++k)
            g.fillRect (clockButton.getCentreX() - 6 + k * 2, clockButton.getCentreY() - 8 + k, 2, 16 - 2 * k);
    auto t = clockText;
    px::drawText (g, "HORLOGE MIDI", t.removeFromTop (16).translated (0, 6), juce::Justification::centredLeft, px::label (8.0f), px::grey);
    const juce::String bpm = host ? juce::String ("SUIT L'HÔTE"_fr) : juce::String (p.get (snd::tempo), 1) + " BPM";
    px::drawText (g, bpm, t.removeFromTop (22).translated (0, 4), juce::Justification::centredLeft, px::text (24.0f), on ? px::red : px::white);
    const auto dest = c.destinationId().isEmpty() ? juce::String ("AUCUNE SORTIE") : c.isMissing() ? "INTROUVABLE : " + upper (c.destinationName())
                    : c.destinationId() == "host" ? juce::String ("VERS LE DAW") : "VERS " + upper (c.destinationName());
    px::drawText (g, dest, t.removeFromTop (16).translated (0, 2), juce::Justification::centredLeft, px::label (7.0f),
                  c.isMissing() ? px::yellow : px::dim);
}

void PlayerStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (clockButton.contains (pos))
    {
        if (proc.clock.getMode() == MidiClock::Mode::internal)
            proc.setClockRunning (! proc.clock.isRunning());
        repaint();
        return;
    }
    for (int m = 0; m < snd::numMods; ++m)
        if (modButtons[(size_t) m].contains (pos))
        {
            arm.mod = arm.mod == m ? -1 : m;
            arm.scene = -1;
            if (arm.changed) arm.changed();
            repaint();
            return;
        }
    for (int side = 0; side < 2; ++side)
        if ((side == 0 ? sceneA : sceneB).contains (pos))
        {
            const int sc = (int) proc.player.get (side == 0 ? snd::sceneA : snd::sceneB) - 1;
            arm.scene = arm.scene == sc ? -1 : sc;
            arm.mod = -1;
            if (arm.changed) arm.changed();
            repaint();
            return;
        }
    if (fader.contains (pos))
    {
        draggingFader = true;
        if (auto* param = proc.params.getParameter ("xfade"))
            param->beginChangeGesture();
        mouseDrag (e);
    }
}

void PlayerStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingFader)
        return;
    auto track = fader.reduced (10, 17);
    const float x = juce::jlimit (0.0f, 1.0f, (float) (e.x - track.getX()) / (float) juce::jmax (1, track.getWidth()));
    if (auto* param = proc.params.getParameter ("xfade"))
        param->setValueNotifyingHost (x);
    repaint();
}

void PlayerStrip::mouseUp (const juce::MouseEvent&)
{
    if (draggingFader)
        if (auto* param = proc.params.getParameter ("xfade"))
            param->endChangeGesture();
    draggingFader = false;
}
}
