#include "DrumUi.h"
#include <array>
#include <cmath>

namespace te
{
namespace
{
    int trackShown (const ReplikantProcessor& p)     { return juce::jlimit (0, drum::numTracks - 1, p.drumTrack.load()); }
    drum::Track& trackOf (ReplikantProcessor& p)      { return p.drum.pattern (p.drum.playingPattern()).tr[trackShown (p)]; }
    void edited (ReplikantProcessor& p)               { p.drum.pattern (p.drum.playingPattern()).used = 1; }

    juce::String sourceName (ReplikantProcessor& p, int t)
    {
        const auto& tk = p.drum.pattern (p.drum.playingPattern()).tr[t];
        const int src = juce::jlimit (0, 2, (int) std::lround (tk.p[drum::pSource]));
        if (src == drum::synth)
            return "synth";
        const auto s = p.drumSound (t);
        return s == nullptr ? juce::String (src == drum::sample ? "sample ?" : "slices ?") : juce::String (src == drum::sample ? "sample" : "slices");
    }
}

namespace
{
    // the scale printed round a drum setting's knob: short values, as a panel would print them
    void scaleOf (Ctl& c, int param)
    {
        const auto& d = drum::def (param);
        const std::string u = d.unit;
        auto at = [param] (double v) { return drum::toNorm (param, (float) v); };
        auto add = [&c] (float t, const juce::String& l) { c.marks.push_back ({ juce::jlimit (0.0f, 1.0f, t), l }); };
        c.ticks = 11;
        if (d.kind == drum::Kind::choice || d.kind == drum::Kind::toggle)
        {
            const auto o = drum::choicesOf (param);
            c.ticks = (int) o.size();
            if (o.size() > 6)
            {
                add (0.0f, juce::String::fromUTF8 (o.front().c_str()));
                add (0.5f, juce::String::fromUTF8 (o[(o.size() - 1) / 2].c_str()));
                add (1.0f, juce::String::fromUTF8 (o.back().c_str()));
            }
            return;
        }
        if (u == "note")       { add (0.0f, "C0"); add (at (36), "C1"); add (at (60), "C3"); add (at (84), "C5"); add (1.0f, "C6"); c.ticks = 7; }
        else if (u == "st")    { add (0.0f, juce::String ((int) d.lo)); add (at (0), "0"); add (1.0f, "+" + juce::String ((int) d.hi)); c.ticks = d.hi > 30 ? 13 : 9; }
        else if (u == "s")     { add (0.0f, "5MS"); add (at (0.05), "50"); add (at (0.5), "1/2S"); add (1.0f, "2S"); }
        else if (u == "x")     { add (0.0f, "1/4"); add (at (1), "1"); add (at (4), "4"); add (1.0f, "16"); c.ticks = 9; }
        else if (u == "Hz")    { add (0.0f, "20"); add (at (100), "100"); add (at (1000), "1K"); add (at (10000), "10K"); add (1.0f, "OPEN"); }
        else if (u == "atk")   { add (0.0f, "0"); add (at (0.5), "50"); add (1.0f, "200"); }
        else if (u == "dec")   { add (0.0f, "10MS"); add (at (0.5), "200"); add (1.0f, "HOLD"); }
        else if (u == "pan")   { add (0.0f, "L"); add (0.5f, "C"); add (1.0f, "R"); }
        else if (u == "fenv")  { add (0.0f, "-6"); add (0.5f, "0"); add (1.0f, "+6"); c.ticks = 13; }
        else if (u == "micro") { add (0.0f, "-1/2"); add (0.5f, "0"); add (1.0f, "+1/2"); c.ticks = 9; }
        else if (u == "crv")   { add (0.0f, "-"); add (0.5f, "EVEN"); add (1.0f, "+"); c.ticks = 9; }
        else if (u == "hits")  { add (0.0f, "1"); add (at (4), "4"); add (at (8), "8"); add (1.0f, "16"); c.ticks = 16; }
        else if (u == "slice") { add (0.0f, "1"); add (at (7), "8"); add (1.0f, "16"); c.ticks = 16; }
        else                   { add (0.0f, "0"); add (0.5f, "5"); add (1.0f, "10"); }
    }
}

juce::String patternName (int i)
{
    i = juce::jlimit (0, drum::numPatterns - 1, i);
    return juce::String::charToString ((juce::juce_wchar) ('A' + i / 16)) + juce::String (i % 16 + 1).paddedLeft ('0', 2);
}

juce::String speedName (int i)
{
    static const char* n[drum::numSpeeds] = { "1/8x", "1/4x", "1/2x", "3/4x", "1x", "3/2x", "2x" };
    return n[juce::jlimit (0, drum::numSpeeds - 1, i)];
}

//============================================================================== the controls
Ctl drumCtl (ReplikantProcessor& p, DrumEdit& e, int param, const char* label, std::function<bool()> shown)
{
    const auto& d = drum::def (param);
    Ctl c;
    auto* pp = &p;
    auto* ee = &e;
    c.name = juce::String::fromUTF8 (label != nullptr ? label : d.name);
    // the value shown: the first step lifted (its lock, else the track's), else the track's
    auto value = [pp, ee, param]
    {
        if (! pp->drum.allocated())
            return drum::def (param).def;
        const auto& tk = trackOf (*pp);
        if (! ee->lifted.empty())
            return tk.at (ee->lifted.front(), param);
        return tk.p[param];
    };
    auto isLocked = [pp, ee, param]
    {
        if (ee->lifted.empty() || ! pp->drum.allocated())
            return false;
        return ! std::isnan (trackOf (*pp).lock[ee->lifted.front()][param]);
    };
    c.getNorm = [value, param] { return drum::toNorm (param, value()); };
    c.setNorm = [pp, ee, param] (float t)
    {
        if (! pp->drum.allocated())
            return;
        const float v = drum::legal (param, drum::fromNorm (param, juce::jlimit (0.0f, 1.0f, t)));
        edited (*pp);
        auto& tk = trackOf (*pp);
        if (! ee->lifted.empty())
            for (int s : ee->lifted)
                tk.lock[s][param] = v;
        else
            tk.p[param] = v;
    };
    c.text = [value, isLocked, param]
    {
        const auto t = juce::String::fromUTF8 (drum::text (param, value()).c_str());
        return isLocked() ? "* " + t : t;
    };
    c.shown = std::move (shown);
    c.defaultNorm = drum::toNorm (param, d.def);
    if (d.kind == drum::Kind::choice || d.kind == drum::Kind::toggle || d.kind == drum::Kind::integer)
        c.steps = (int) std::lround (d.hi - d.lo) + 1;
    c.bipolar = d.kind != drum::Kind::choice && d.lo < 0.0f && d.hi > 0.0f;
    c.toggle = d.kind == drum::Kind::toggle;
    if (d.kind == drum::Kind::choice || d.kind == drum::Kind::toggle)
        for (auto& o : drum::choicesOf (param))
            c.options.add (juce::String::fromUTF8 (o.c_str()));
    scaleOf (c, param);
    dress (c);
    return c;
}

//============================================================================== the screen
void DrumScreen::paint (juce::Graphics& g, juce::Rectangle<float> r, ReplikantProcessor& p, DrumEdit& e)
{
    auto& d = p.drum;
    if (! d.allocated())
    {
        text (g, "drum", r, grotesk (24.0f, 300), col::screenDim, juce::Justification::centred);
        return;
    }
    const int playingPat = d.playingPattern(), queued = d.queuedPattern();
    const auto& pat = d.pattern (playingPat);
    const int sel = trackShown (p);
    // the line above: the pattern, what comes next, the chain
    {
        auto head = r.removeFromTop (16.0f);
        juce::String t = patternName (playingPat) + (d.runningNow.load() ? " · playing"_fr : juce::String (" · stopped"_fr));
        if (queued >= 0)
            t << " · next "_fr << patternName (queued);
        const auto ch = d.chain();
        if (! ch.empty())
        {
            t << " · chain"_fr;
            for (int c : ch)
                t << " " << patternName (c);
        }
        t << " · page "_fr << (e.page + 1) << "/4 · length "_fr << pat.length16() << " sixteenths";
        text (g, t, head, mono (10.0f), col::screenDim);
    }
    r.removeFromTop (4.0f);
    // the grid: the eight tracks, the sixteen steps of the page shown
    grid = r.removeFromTop (juce::jmin (r.getHeight() * 0.6f, 8.0f * 16.0f));
    labelW = 92.0f;
    const float rowH = grid.getHeight() / 8.0f;
    const float colW = (grid.getWidth() - labelW) / 16.0f;
    for (int t = 0; t < drum::numTracks; ++t)
    {
        const auto& tk = pat.tr[t];
        const bool muted = d.mute[t].load();
        auto row = juce::Rectangle<float> (grid.getX(), grid.getY() + (float) t * rowH, grid.getWidth(), rowH);
        if (t == sel)
        {
            g.setColour (col::sOrange.withAlpha (0.10f));
            g.fillRect (row);
        }
        const float glow = d.glow[(size_t) t].load();
        auto lab = row.withWidth (labelW).reduced (2.0f, 1.0f);
        g.setColour (col::sRed.withAlpha (0.15f + 0.6f * glow));
        g.fillEllipse (lab.getX() + 2.0f, lab.getCentreY() - 3.0f, 6.0f, 6.0f);
        text (g, juce::String (t + 1) + " " + sourceName (p, t), lab.withTrimmedLeft (12.0f), mono (10.0f, t == sel ? 600 : 400),
              muted ? col::screenGhost : (t == sel ? col::sOrange : col::screenDim));
        const int now = d.stepNow[(size_t) t].load();
        const int len = juce::jmax (1, (int) tk.len);
        for (int k = 0; k < drum::pageSteps; ++k)
        {
            const int s = e.page * drum::pageSteps + k;
            auto cell = juce::Rectangle<float> (grid.getX() + labelW + (float) k * colW, row.getY(), colW, rowH).reduced (1.5f, 2.0f);
            const bool beyond = s >= len;
            const bool on = tk.trig[s] != 0;
            if (k % 4 == 0)
            {
                g.setColour (col::screenLine);
                g.fillRect (cell.getX() - 1.5f, row.getY() + 1.0f, 1.0f, rowH - 2.0f);
            }
            if (now == s && d.runningNow.load())
            {
                g.setColour (col::screenText.withAlpha (0.22f));
                g.fillRect (cell.expanded (1.0f, 1.0f));
            }
            if (beyond)
            {
                g.setColour (col::screenLine.withAlpha (0.35f));
                g.fillRect (cell.withSizeKeepingCentre (2.0f, 2.0f));
                continue;
            }
            if (on)
            {
                const int nr = juce::jmax (1, (int) tk.rat[s]);
                g.setColour ((muted ? col::screenDim : col::sOrange).withAlpha (muted ? 0.5f : 0.95f));
                if (nr == 1)
                    g.fillRoundedRectangle (cell, 2.0f);
                else
                {
                    const float w = cell.getWidth() / (float) nr;
                    for (int q = 0; q < nr; ++q)
                        g.fillRect (cell.getX() + (float) q * w + 0.5f, cell.getY(), juce::jmax (1.0f, w - 1.0f), cell.getHeight());
                }
            }
            else
            {
                g.setColour (col::screenLine);
                g.drawRoundedRectangle (cell, 2.0f, 1.0f);
            }
            if (tk.locked (s))
            {
                juce::Path corner;
                corner.addTriangle (cell.getRight() - 6.0f, cell.getY(), cell.getRight(), cell.getY(), cell.getRight(), cell.getY() + 6.0f);
                g.setColour (on ? col::screen : col::sYellow);
                g.fillPath (corner);
            }
            if (t == sel && e.isLifted (s))
            {
                g.setColour (col::sYellow);
                g.drawRoundedRectangle (cell.expanded (1.0f, 1.0f), 2.0f, 1.5f);
            }
        }
    }
    r.removeFromTop (8.0f);
    // the track shown: its sound
    {
        const auto& tk = pat.tr[sel];
        const int src = juce::jlimit (0, 2, (int) std::lround (tk.p[drum::pSource]));
        auto head = r.removeFromTop (14.0f);
        const auto s = p.drumSound (sel);
        juce::String t = "track " + juce::String (sel + 1) + " · "_fr + sourceName (p, sel);
        if (src != drum::synth && s != nullptr)
            t << " · "_fr << juce::String::fromUTF8 (s->name.c_str()) << " · "_fr << juce::String ((double) s->length() / s->audio->sampleRate, 2) << " s";
        if (src == drum::synth)
            t << " · "_fr << juce::String::fromUTF8 (drum::text (drum::pNote, tk.p[drum::pNote]).c_str()) << " · bank "_fr
              << (tk.p[drum::pBank] > 0.5f ? "captured" : "basic");
        text (g, t, head, mono (10.0f), col::screenDim, juce::Justification::centredLeft, true);
        auto w = r.reduced (0.0f, 2.0f);
        if (w.getHeight() < 12.0f)
            return;
        g.setColour (col::screenLine);
        g.drawHorizontalLine ((int) w.getCentreY(), w.getX(), w.getRight());
        if (src != drum::synth && s != nullptr && s->length() > 1)
        {
            const int len = s->length();
            if (cachedSound != s.get() || std::abs (cachedW - w.getWidth()) > 0.5f || std::abs (cachedH - w.getHeight()) > 0.5f)
            {
                cachedSound = s.get();
                cachedW = w.getWidth();
                cachedH = w.getHeight();
                wave.clear();
                const int cols = juce::jmax (2, (int) w.getWidth());
                const float mid = w.getHeight() * 0.5f;
                std::vector<float> hi ((size_t) cols, 0.0f);
                float peak = 1.0e-6f;
                for (int x = 0; x < cols; ++x)
                {
                    const int a = (int) ((int64_t) x * len / cols), b = juce::jmax (a + 1, (int) ((int64_t) (x + 1) * len / cols));
                    for (int i = a; i < b && i < len; ++i)
                        hi[(size_t) x] = juce::jmax (hi[(size_t) x], std::abs (s->audio->l[(size_t) i]));
                    peak = juce::jmax (peak, hi[(size_t) x]);
                }
                wave.startNewSubPath (0.0f, mid);
                for (int x = 0; x < cols; ++x)
                    wave.lineTo ((float) x, mid - hi[(size_t) x] / peak * mid * 0.95f);
                for (int x = cols - 1; x >= 0; --x)
                    wave.lineTo ((float) x, mid + hi[(size_t) x] / peak * mid * 0.95f);
                wave.closeSubPath();
            }
            g.setColour (col::screenText.withAlpha (0.85f));
            g.fillPath (wave, juce::AffineTransform::translation (w.getX(), w.getY()));
            if (src == drum::slices)
                for (int i = 0; i < (int) s->slices.size(); ++i)
                {
                    const float x = w.getX() + (float) s->slices[(size_t) i].start / (float) len * w.getWidth();
                    g.setColour (col::sOrange);
                    g.fillRect (x - 0.5f, w.getY(), 1.0f, w.getHeight());
                    text (g, juce::String (i + 1), { x + 2.0f, w.getY(), 20.0f, 11.0f }, mono (9.0f, 600), col::sOrange);
                }
        }
        else if (src == drum::synth)
        {
            // the hit: its pitch falling (SWEEP), its body dying away (DECAY), its noise (NOISE)
            const float sweep = tk.p[drum::pSweep], swt = tk.p[drum::pSweepTime];
            const float dec = tk.p[drum::pDecay] >= 0.99f ? 8.0f : 0.01f * std::pow (400.0f, tk.p[drum::pDecay]);
            const float span = juce::jlimit (0.05f, 2.0f, dec * 2.5f);
            juce::Path amp, pitch;
            for (int x = 0; x <= (int) w.getWidth(); x += 2)
            {
                const float when = span * (float) x / w.getWidth();
                const float a = std::exp (-when / dec);
                const float pp = juce::jlimit (-1.0f, 1.0f, sweep * std::exp (-when / std::max (0.002f, swt)) / 48.0f);
                const auto pa = juce::Point<float> (w.getX() + (float) x, w.getBottom() - a * w.getHeight() * 0.9f);
                const auto pq = juce::Point<float> (w.getX() + (float) x, w.getCentreY() - pp * w.getHeight() * 0.45f);
                if (x == 0) { amp.startNewSubPath (pa); pitch.startNewSubPath (pq); }
                else { amp.lineTo (pa); pitch.lineTo (pq); }
            }
            g.setColour (col::screenText.withAlpha (0.85f));
            g.strokePath (amp, juce::PathStrokeType (1.8f));
            g.setColour (col::sOrange);
            g.strokePath (pitch, juce::PathStrokeType (1.4f));
            text (g, "amp · pitch · "_fr + juce::String (span < 1.0f ? juce::String (juce::roundToInt (span * 1000.0f)) + " ms" : juce::String (span, 2) + " s"),
                  w.removeFromBottom (12.0f), mono (9.0f), col::screenGhost, juce::Justification::centredRight);
        }
        else
            text (g, "glisse un son sur la fenêtre, ou envoie une prise de la radio (→ drum)"_fr, w, mono (11.0f), col::screenDim, juce::Justification::centred);
    }
}

bool DrumScreen::click (juce::Point<float> at, ReplikantProcessor& p, DrumEdit& e)
{
    if (! p.drum.allocated() || ! grid.contains (at) || grid.getHeight() <= 0.0f)
        return false;
    const int t = juce::jlimit (0, drum::numTracks - 1, (int) ((at.y - grid.getY()) / grid.getHeight() * 8.0f));
    if (t != p.drumTrack.load())
        e.lifted.clear();
    p.drumTrack = t;
    if (at.x >= grid.getX() + labelW)
    {
        const int k = juce::jlimit (0, drum::pageSteps - 1, (int) ((at.x - grid.getX() - labelW) / (grid.getWidth() - labelW) * 16.0f));
        const int s = e.page * drum::pageSteps + k;
        auto& tk = trackOf (p);
        edited (p);
        tk.trig[s] = tk.trig[s] ? 0 : 1;
        if (! tk.trig[s])
            tk.rat[s] = 1;
        else if (s >= (int) tk.len)
            tk.len = (uint8_t) (s + 1);
    }
    return true;
}

//============================================================================== the strip
DrumStrip::DrumStrip (ReplikantProcessor& p, DrumEdit& e, std::function<void (const juce::String&, bool)> m, std::function<void()> c)
    : proc (p), edit (e), message (std::move (m)), changed (std::move (c)) {}

drum::Track& DrumStrip::track()                 { return trackOf (proc); }

void DrumStrip::layout()
{
    auto r = getLocalBounds().toFloat();
    hint = r.removeFromTop (20.0f);
    r.removeFromTop (2.0f);
    auto bottom = r.removeFromBottom (34.0f);
    r.removeFromBottom (6.0f);
    auto top = r.removeFromTop (r.getHeight() * 0.42f);
    r.removeFromTop (6.0f);
    const float gap = 6.0f;
    const float tw = (top.getWidth() - 7.0f * gap) / 8.0f;
    for (int t = 0; t < drum::numTracks; ++t)
        tracks[t] = { top.getX() + (float) t * (tw + gap), top.getY(), tw, top.getHeight() };
    const float sw = (r.getWidth() - 15.0f * 4.0f - 3.0f * 6.0f) / 16.0f;
    float x = r.getX();
    for (int s = 0; s < drum::pageSteps; ++s)
    {
        steps[s] = { x, r.getY(), sw, r.getHeight() };
        x += sw + 4.0f + (s % 4 == 3 ? 6.0f : 0.0f);
    }
    for (int i = 0; i < 4; ++i)
        pages[i] = bottom.removeFromLeft (40.0f).withTrimmedRight (4.0f);
    bottom.removeFromLeft (10.0f);
    const float kw = (bottom.getWidth() - 4.0f * 6.0f) / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        keys[i] = bottom.removeFromLeft (kw);
        bottom.removeFromLeft (6.0f);
    }
}

void DrumStrip::paint (juce::Graphics& g)
{
    layout();
    auto& d = proc.drum;
    if (! d.allocated())
        return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float fade = lastPaint > 0.0 ? (float) std::exp (-(now - lastPaint) / 0.12) : 1.0f;
    lastPaint = now;
    const int sel = trackShown (proc);
    const int playingPat = d.playingPattern(), queued = d.queuedPattern();
    const bool blink = std::fmod (now, 0.5) < 0.25;
    // the line above
    {
        juce::String t;
        if (edit.patterns)
            t = edit.pending == 1 ? "paste : touche le pattern où coller"_fr
              : edit.pending == 2 ? "clear : touche le pattern à vider"_fr
              : edit.chaining ? "chain : touche les patterns dans l'ordre, puis chain"_fr + (edit.chainDraft.empty() ? juce::String() : " ·")
              : "patterns · un pattern part à la fin de celui qui joue · banques a – h"_fr;
        if (edit.patterns && edit.chaining)
            for (int c : edit.chainDraft)
                t << " " << patternName (c);
        if (! edit.patterns)
            t = edit.lifted.empty() ? "piste "_fr + juce::String (sel + 1) + " · clic : trig · glisser haut-bas : ratchet · maj-clic : p-lock · c1 – g1 sur ton clavier"_fr
                                    : "p-lock : "_fr + juce::String ((int) edit.lifted.size()) + " pas · les encodeurs écrivent sur eux · ok pour finir"_fr;
        text (g, t, hint, mono (11.0f, 500), edit.lifted.empty() ? col::deckInk2 : col::red);
    }
    // the tracks (PTN: the banks)
    for (int t = 0; t < drum::numTracks; ++t)
    {
        if (edit.patterns)
        {
            const bool here = playingPat / 16 == t;
            key (g, tracks[t], edit.bank == t ? KeyState::on : KeyState::normal, false, 8.0f);
            auto f = tracks[t].withTrimmedBottom (3.0f);
            text (g, juce::String::charToString ((juce::juce_wchar) ('a' + t)), f, grotesk (18.0f, 500), edit.bank == t ? col::keyOnInk : col::ink, juce::Justification::centred);
            if (here)
            {
                g.setColour (col::red);
                g.fillEllipse (f.getRight() - 12.0f, f.getY() + 6.0f, 6.0f, 6.0f);
            }
            continue;
        }
        const float glow = d.glow[(size_t) t].load();
        if (glow > 0.0f)
            d.glow[(size_t) t].store (glow * fade > 0.02f ? glow * fade : 0.0f);
        const bool muted = d.mute[t].load();
        key (g, tracks[t], muted ? KeyState::off : (t == sel ? KeyState::on : (glow > 0.4f ? KeyState::accent : KeyState::normal)), false, 8.0f);
        auto f = tracks[t].withTrimmedBottom (3.0f).reduced (8.0f, 4.0f);
        const auto ink = muted ? col::ink3 : (t == sel ? col::keyOnInk : (glow > 0.4f ? col::accentInk : col::ink));
        text (g, juce::String (t + 1), f.removeFromTop (18.0f), grotesk (15.0f, 600), ink);
        text (g, muted ? juce::String ("mute") : sourceName (proc, t), f, mono (10.0f), ink);
        g.setColour (col::red.withAlpha (0.25f + 0.75f * glow));
        g.fillEllipse (tracks[t].getRight() - 14.0f, tracks[t].getY() + 8.0f, 6.0f, 6.0f);
    }
    // the steps (PTN: the patterns of the bank)
    const auto& tk = track();
    const int stepNow = d.stepNow[(size_t) sel].load();
    for (int k = 0; k < drum::pageSteps; ++k)
    {
        auto c = steps[k];
        if (edit.patterns)
        {
            const int i = edit.bank * 16 + k;
            const bool isPlaying = i == playingPat, isQueued = i == queued, used = d.pattern (i).used != 0;
            const bool inChain = edit.chaining ? std::find (edit.chainDraft.begin(), edit.chainDraft.end(), i) != edit.chainDraft.end()
                                               : [&] { const auto ch = d.chain(); return std::find (ch.begin(), ch.end(), i) != ch.end(); }();
            key (g, c, isPlaying ? KeyState::on : (isQueued && blink ? KeyState::accent : (used ? KeyState::normal : KeyState::off)), false, 6.0f);
            auto f = c.withTrimmedBottom (3.0f);
            text (g, juce::String (k + 1), f.removeFromTop (f.getHeight() * 0.6f), mono (12.0f, 600), isPlaying ? col::keyOnInk : (used ? col::ink : col::ink3), juce::Justification::centred);
            if (inChain)
            {
                g.setColour (col::sYellow);
                g.fillRect (f.reduced (8.0f, 0.0f).withHeight (3.0f).withY (f.getY() + 4.0f));
            }
            continue;
        }
        const int s = edit.page * drum::pageSteps + k;
        const bool beyond = s >= (int) tk.len;
        const bool on = tk.trig[s] != 0;
        const bool lifted = edit.isLifted (s);
        key (g, lifted ? c.translated (0.0f, -4.0f) : c, beyond ? KeyState::off : (on ? KeyState::on : KeyState::normal), lifted, 6.0f);
        auto f = (lifted ? c.translated (0.0f, -4.0f) : c).withTrimmedBottom (3.0f).reduced (5.0f, 5.0f);
        // its LED: the trig, split in its ratchet's hits; the playhead
        auto led = f.removeFromTop (6.0f);
        const int nr = juce::jmax (1, (int) tk.rat[s]);
        const float w = led.getWidth() / (float) nr;
        for (int q = 0; q < nr; ++q)
        {
            g.setColour (on ? col::red : col::tickOff);
            g.fillRoundedRectangle (led.getX() + (float) q * w + (nr > 1 ? 0.5f : 0.0f), led.getY(), juce::jmax (1.5f, w - (nr > 1 ? 1.0f : 0.0f)), led.getHeight(), 2.0f);
        }
        if (stepNow == s && d.runningNow.load())
        {
            g.setColour (col::white.withAlpha (0.85f));
            g.fillRoundedRectangle (led.reduced (0.0f, 1.0f), 2.0f);
        }
        text (g, juce::String (s + 1), f, mono (10.0f, 500), beyond ? col::ink3 : (on ? col::keyOnInk : col::ink), juce::Justification::centred);
        if (tk.locked (s))
        {
            juce::Path corner;
            const auto b = lifted ? c.translated (0.0f, -4.0f) : c;
            corner.addTriangle (b.getRight() - 9.0f, b.getY() + 2.0f, b.getRight() - 2.0f, b.getY() + 2.0f, b.getRight() - 2.0f, b.getY() + 9.0f);
            g.setColour (col::sYellow.darker (0.2f));
            g.fillPath (corner);
        }
        if (lifted)
        {
            g.setColour (col::sYellow);
            g.drawRoundedRectangle (c.translated (0.0f, -4.0f).reduced (0.5f), 6.0f, 1.5f);
        }
    }
    // the pages (the one playing lit), the keys
    const int playingPage = stepNow >= 0 ? stepNow / drum::pageSteps : -1;
    for (int i = 0; i < 4; ++i)
    {
        const bool beyond = i * drum::pageSteps >= (int) tk.len;
        key (g, pages[i], edit.page == i && ! edit.patterns ? KeyState::on : (beyond ? KeyState::off : KeyState::normal), false, 5.0f);
        text (g, juce::String (i + 1), pages[i].withTrimmedBottom (3.0f), mono (12.0f, 600), edit.page == i && ! edit.patterns ? col::keyOnInk : col::ink,
              juce::Justification::centred);
        if (playingPage == i && d.runningNow.load())
        {
            g.setColour (col::red);
            g.fillEllipse (pages[i].getRight() - 9.0f, pages[i].getY() + 4.0f, 5.0f, 5.0f);
        }
    }
    const char* names[5] = { "ptn", "chain", "copy", "paste", "clear" };
    const char* trackNames[5] = { "ptn", "mute", "ok", "clear", "←→" };
    for (int i = 0; i < 5; ++i)
    {
        const auto label = juce::String (edit.patterns ? names[i] : trackNames[i]);
        bool lit = false;
        if (edit.patterns)
            lit = i == 0 || (i == 1 && edit.chaining) || (i == 3 && edit.pending == 1) || (i == 4 && edit.pending == 2) || (i == 2 && edit.clipboard != nullptr);
        else
            lit = (i == 1 && d.mute[sel].load()) || (i == 2 && ! edit.lifted.empty());
        const bool off = ! edit.patterns && i == 2 && edit.lifted.empty();
        key (g, keys[i], off ? KeyState::off : (lit ? (i == 0 ? KeyState::on : KeyState::accent) : KeyState::normal), false, 6.0f);
        text (g, i == 4 && ! edit.patterns ? juce::String ("shift ←→"_fr) : label, keys[i].withTrimmedBottom (3.0f), mono (11.0f, 600),
              off ? col::ink3 : (lit ? (i == 0 ? col::keyOnInk : col::accentInk) : col::ink), juce::Justification::centred);
    }
}

void DrumStrip::mouseDown (const juce::MouseEvent& e)
{
    layout();
    auto& d = proc.drum;
    if (! d.allocated())
        return;
    dragged = false;
    downStep = -1;
    downTrack = -1;
    const auto pos = e.position;
    for (int t = 0; t < drum::numTracks; ++t)
        if (tracks[t].contains (pos))
        {
            if (edit.patterns)
            {
                edit.bank = t;
                repaint();
                return;
            }
            if (e.mods.isPopupMenu() || e.mods.isAltDown())
            {
                d.mute[t] = ! d.mute[t].load();
                repaint();
                return;
            }
            if (t != proc.drumTrack.load())
                edit.lifted.clear();
            proc.drumTrack = t;
            downTrack = t;
            proc.audition (drum::firstNote + t, juce::jlimit (30, 127, (int) (60 + 67 * (pos.y - tracks[t].getY()) / tracks[t].getHeight())), true);
            if (changed) changed();
            repaint();
            return;
        }
    for (int k = 0; k < drum::pageSteps; ++k)
        if (steps[k].contains (pos))
        {
            if (edit.patterns)
            {
                const int i = edit.bank * 16 + k;
                if (edit.pending == 1 && edit.clipboard != nullptr)
                {
                    auto& to = d.pattern (i);
                    to = *edit.clipboard;
                    to.used = 1;
                    edit.pending = 0;
                    message ("Collé dans "_fr + patternName (i) + ".", false);
                }
                else if (edit.pending == 2)
                {
                    auto& to = d.pattern (i);
                    if (i == d.playingPattern())
                    {
                        // (the one playing: its trigs and locks gone, its sounds and lengths kept)
                        for (auto& tk : to.tr)
                            for (int s = 0; s < drum::numSteps; ++s)
                            {
                                tk.trig[s] = 0;
                                tk.rat[s] = 1;
                                for (int q = 0; q < drum::numParams; ++q)
                                    tk.lock[s][q] = NAN;
                            }
                    }
                    else
                        to.reset();
                    edit.pending = 0;
                    message (patternName (i) + " vidé."_fr, false);
                }
                else if (edit.chaining)
                {
                    if (edit.chainDraft.size() < 64)
                        edit.chainDraft.push_back (i);
                }
                else
                {
                    if (! d.chain().empty())
                        d.setChain ({});
                    d.queue (i);
                }
                if (changed) changed();
                repaint();
                return;
            }
            downStep = edit.page * drum::pageSteps + k;
            startY = pos.y;
            startRat = juce::jmax (1, (int) track().rat[downStep]);
            if (e.mods.isShiftDown() || e.mods.isPopupMenu())
            {
                // lift it (or put it back down)
                auto it = std::find (edit.lifted.begin(), edit.lifted.end(), downStep);
                if (it != edit.lifted.end())
                    edit.lifted.erase (it);
                else
                    edit.lifted.push_back (downStep);
                downStep = -1;
                if (changed) changed();
                repaint();
            }
            return;
        }
    for (int i = 0; i < 4; ++i)
        if (pages[i].contains (pos))
        {
            edit.patterns = false;
            edit.page = i;
            // a page past the track's length: the track goes to its end
            auto& tk = track();
            if (i * drum::pageSteps >= (int) tk.len)
            {
                tk.len = (uint8_t) ((i + 1) * drum::pageSteps);
                edited (proc);
            }
            repaint();
            if (changed) changed();
            return;
        }
    for (int i = 0; i < 5; ++i)
        if (keys[i].contains (pos))
        {
            if (i == 0)
            {
                edit.patterns = ! edit.patterns;
                edit.pending = 0;
                edit.chaining = false;
                edit.bank = d.playingPattern() / 16;
            }
            else if (edit.patterns)
            {
                if (i == 1)
                {
                    if (edit.chaining)
                    {
                        if (edit.chainDraft.size() >= 2)
                        {
                            d.setChain (edit.chainDraft);
                            message ("Chaîne : "_fr + juce::String ((int) edit.chainDraft.size()) + " patterns, l'un après l'autre."_fr, false);
                        }
                        else
                            d.setChain ({});
                        edit.chaining = false;
                    }
                    else
                    {
                        edit.chaining = true;
                        edit.chainDraft.clear();
                    }
                }
                else if (i == 2)
                {
                    edit.clipboard = std::make_unique<drum::Pattern> (d.pattern (d.playingPattern()));
                    message (patternName (d.playingPattern()) + " copié : paste, puis le pattern où le coller."_fr, false);
                }
                else if (i == 3)
                    edit.pending = edit.clipboard != nullptr ? (edit.pending == 1 ? 0 : 1) : 0;
                else if (i == 4)
                    edit.pending = edit.pending == 2 ? 0 : 2;
            }
            else
            {
                const int sel = trackShown (proc);
                if (i == 1)
                    d.mute[sel] = ! d.mute[sel].load();
                else if (i == 2)
                    edit.lifted.clear();
                else if (i == 3)
                {
                    // the track's steps and locks (its sound stays); with steps lifted, only their locks
                    auto& tk = track();
                    edited (proc);
                    if (! edit.lifted.empty())
                    {
                        for (int s : edit.lifted)
                            for (int q = 0; q < drum::numParams; ++q)
                                tk.lock[s][q] = NAN;
                        message ("Les p-locks des pas levés sont effacés."_fr, false);
                    }
                    else if (e.getNumberOfClicks() >= 2)
                    {
                        for (int s = 0; s < drum::numSteps; ++s)
                        {
                            tk.trig[s] = 0;
                            tk.rat[s] = 1;
                            for (int q = 0; q < drum::numParams; ++q)
                                tk.lock[s][q] = NAN;
                        }
                        message ("Piste "_fr + juce::String (sel + 1) + " vidée (son gardé)."_fr, false);
                    }
                    else
                        message ("Clear : deux clics pour vider la piste (ses trigs et ses p-locks)."_fr, false);
                }
                else if (i == 4)
                {
                    // shift the track's steps (and their locks) one step: right, or left with shift
                    auto& tk = track();
                    edited (proc);
                    const int len = juce::jmax (1, (int) tk.len);
                    const int dir = e.mods.isShiftDown() ? 1 : len - 1;
                    std::vector<uint8_t> trig ((size_t) len), rat ((size_t) len);
                    std::vector<std::array<float, drum::numParams>> lk ((size_t) len);
                    for (int s = 0; s < len; ++s)
                    {
                        trig[(size_t) s] = tk.trig[s];
                        rat[(size_t) s] = tk.rat[s];
                        std::copy (tk.lock[s], tk.lock[s] + drum::numParams, lk[(size_t) s].begin());
                    }
                    for (int s = 0; s < len; ++s)
                    {
                        const int from = (s + dir) % len;
                        tk.trig[s] = trig[(size_t) from];
                        tk.rat[s] = rat[(size_t) from];
                        std::copy (lk[(size_t) from].begin(), lk[(size_t) from].end(), tk.lock[s]);
                    }
                }
            }
            if (changed) changed();
            repaint();
            return;
        }
}

void DrumStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (downStep < 0)
        return;
    const float dy = startY - e.position.y;
    if (! dragged && std::abs (dy) < 6.0f)
        return;
    dragged = true;
    auto& tk = track();
    edited (proc);
    const int nr = juce::jlimit (1, 16, startRat + (int) std::lround (dy / 14.0f));
    tk.rat[downStep] = (uint8_t) nr;
    if (nr > 1)
        tk.trig[downStep] = 1;
    message ("ratchet · pas "_fr + juce::String (downStep + 1) + " ×"_fr + juce::String (nr), false);
    repaint();
}

void DrumStrip::mouseUp (const juce::MouseEvent&)
{
    if (downTrack >= 0)
        proc.audition (drum::firstNote + downTrack, 0, false);
    if (downStep >= 0 && ! dragged)
    {
        edited (proc);
        auto& tk = track();
        if (tk.trig[downStep] && tk.rat[downStep] > 1)
            tk.rat[downStep] = 1;
        tk.trig[downStep] = tk.trig[downStep] ? 0 : 1;
        // (a trig on a step past the track's end: the track goes that far)
        if (tk.trig[downStep] && downStep >= (int) tk.len)
            tk.len = (uint8_t) (downStep + 1);
    }
    downStep = -1;
    downTrack = -1;
    dragged = false;
    if (changed) changed();
    repaint();
}
}
