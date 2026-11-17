#include "KeyMap.h"
#include "Pixel.h"
#include "Text.h"

namespace
{
    bool isBlack (int k)
    {
        const int n = k % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
}

KeyMap::KeyMap (ReplikantProcessor& p) : proc (p)
{
    setRepaintsOnMouseActivity (false);
}

void KeyMap::refresh (InstrumentPtr i, const Worker::Status& st, bool isSampling, int currentJob)
{
    ins = std::move (i);
    status = st;
    sampling = isSampling;
    job = currentJob;
    ++blink;
    updateRange();
    repaint();
}

void KeyMap::updateRange()
{
    const auto& s = proc.session;
    int lo = juce::jmax (0, juce::jmin (s.lowNote, s.highNote) - s.extend);
    int hi = juce::jmin (127, juce::jmax (s.lowNote, s.highNote) + s.extend);
    if (ins != nullptr)
        for (auto& x : ins->samples)
        {
            lo = juce::jmin (lo, x.keyLo);
            hi = juce::jmax (hi, x.keyHi);
        }
    lo = (lo / 12) * 12;
    hi = juce::jmin (127, (hi / 12) * 12 + 11);
    while (hi - lo + 1 < 48)
    {
        if (lo >= 12) lo -= 12;
        if (hi - lo + 1 < 48 && hi <= 115) hi += 12;
        if (lo < 12 && hi > 115) break;
    }
    if (lo != first || hi != last)
    {
        first = lo;
        last = hi;
        resized();
    }
}

void KeyMap::resized()
{
    auto r = getLocalBounds();
    labels = r.removeFromBottom (18);
    handles = r.removeFromBottom (22);
    keys = r.removeFromBottom (64);
    r.removeFromBottom (6);
    map = r;
    const int n = last - first + 1;
    colW = (float) map.getWidth() / (float) juce::jmax (1, n);
}

int KeyMap::columnX (int key) const
{
    return map.getX() + juce::roundToInt ((float) (key - first) * colW);
}

int KeyMap::keyAt (int x) const
{
    return juce::jlimit (first, last, first + (int) std::floor ((float) (x - map.getX()) / colW));
}

int KeyMap::velocityAt (int y) const
{
    const float t = 1.0f - (float) (y - map.getY()) / (float) juce::jmax (1, map.getHeight());
    return juce::jlimit (1, 127, 1 + juce::roundToInt (t * 126.0f));
}

juce::Rectangle<int> KeyMap::zoneRect (int kLo, int kHi, int vLo, int vHi) const
{
    kLo = juce::jmax (kLo, first);
    kHi = juce::jmin (kHi, last);
    const int x0 = columnX (kLo), x1 = columnX (kHi + 1);
    auto vy = [this] (int v) { return map.getBottom() - juce::roundToInt ((float) (v - 1) / 127.0f * (float) map.getHeight()); };
    const int y0 = vy (vHi + 1), y1 = vy (vLo);
    return { x0, y0, juce::jmax (1, x1 - x0), juce::jmax (1, y1 - y0) };
}

void KeyMap::paint (juce::Graphics& g)
{
    g.setColour (px::black);
    g.fillRect (map);
    // octave lines and velocity guides
    for (int k = first; k <= last + 1; ++k)
        if (k % 12 == 0)
        {
            g.setColour (px::frame);
            g.fillRect (columnX (k), map.getY(), 1, map.getHeight());
        }
    for (int v : { 32, 64, 96 })
    {
        const int y = zoneRect (first, first, v, v).getBottom();
        g.setColour (px::cell);
        for (int x = map.getX(); x < map.getRight(); x += 4)
            g.fillRect (x, y, 2, 1);
    }

    const auto& s = proc.session;
    const auto planned = rk::planJobs (s);
    const auto notes = rk::plannedNotes (s);
    const auto vels = rk::plannedVelocities (s);
    const auto kz = rk::keyZones (notes, s.placement, juce::jmin (s.lowNote, s.highNote) - s.extend, juce::jmax (s.lowNote, s.highNote) + s.extend);
    const auto vz = rk::velocityZones (vels);
    auto zoneOfPlan = [&] (const rk::Job& j)
    {
        const auto ni = (size_t) (std::find (notes.begin(), notes.end(), j.note) - notes.begin());
        const auto vi = (size_t) (std::find (vels.begin(), vels.end(), j.velocity) - vels.begin());
        if (ni >= kz.size() || vi >= vz.size())
            return juce::Rectangle<int>();
        return zoneRect (kz[ni].lo, kz[ni].hi, vz[vi].lo, vz[vi].hi);
    };

    const bool showPlan = sampling || ins == nullptr;
    if (showPlan)
    {
        // what will be recorded: frames; what is: filled
        for (size_t z = 0; z < kz.size(); ++z)
            for (size_t v = 0; v < vz.size(); ++v)
            {
                auto r = zoneRect (kz[z].lo, kz[z].hi, vz[v].lo, vz[v].hi).reduced (1);
                px::frameRect (g, r, px::faint, 1);
                const auto root = zoneRect (notes[z], notes[z], vz[v].lo, vz[v].hi).reduced (1);
                px::dither (g, root, px::faint, 2);
            }
        for (auto& j : status.recorded)
        {
            auto r = zoneOfPlan (j).reduced (1);
            px::dither (g, r, px::grey, 2);
            px::frameRect (g, r, px::grey, 1);
            g.setColour (px::white);
            g.fillRect (zoneRect (j.note, j.note, vz.empty() ? 1 : 1, 1).withY (r.getY()).withHeight (r.getHeight()).reduced (1, 0));
        }
        if (sampling && job >= 0 && job < (int) planned.size())
        {
            auto r = zoneOfPlan (planned[(size_t) job]).reduced (1);
            if ((blink / 3) % 2 == 0)
                px::dither (g, r, px::red, 3);
            px::frameRect (g, r, px::red, 2);
        }
    }
    else
    {
        const int playing = proc.player.shownSample.load();
        for (size_t i = 0; i < ins->samples.size(); ++i)
        {
            const auto& x = ins->samples[i];
            if (x.rr > 0)
                continue;   // takes share the zone: drawn once
            auto r = zoneRect (x.keyLo, x.keyHi, x.velLo, x.velHi).reduced (1);
            const bool sel = (int) i == selected
                          || (selected >= 0 && selected < (int) ins->samples.size()
                              && ins->samples[(size_t) selected].root == x.root && ins->samples[(size_t) selected].velocity == x.velocity);
            px::dither (g, r, sel ? px::yellow : px::grey, 2);
            px::frameRect (g, r, sel ? px::yellow : px::grey, 1);
            auto root = zoneRect (x.root, x.root, x.velLo, x.velHi).reduced (1);
            root = root.withY (r.getY()).withHeight (r.getHeight()).reduced (1, 0);
            g.setColour (sel ? px::yellow : px::white);
            g.fillRect (root);
            if (playing >= 0 && playing < (int) ins->samples.size())
            {
                const auto& p = ins->samples[(size_t) playing];
                if (p.root == x.root && p.velocity == x.velocity)
                    px::frameRect (g, r.expanded (1), px::red, 2);
            }
            // takes: a notch each
            if (ins->session.roundRobins > 1)
                for (int t = 0; t < ins->session.roundRobins; ++t)
                {
                    g.setColour (px::black);
                    g.fillRect (r.getX() + 3 + t * 5, r.getY() + 3, 3, 3);
                }
        }
        if (ins->samples.empty())
            px::drawText (g, "AUCUN SAMPLE", map, juce::Justification::centred, px::label (12.0f), px::dim);
    }

    // the hover readout
    if (hoverKey >= 0)
    {
        const auto t = juce::String (rk::noteName (hoverKey)) + (hoverVel > 0 ? "  V" + juce::String (hoverVel) : juce::String());
        px::drawText (g, t, map.withTrimmedTop (4).withTrimmedRight (8).withHeight (18), juce::Justification::topRight, px::text (20.0f), px::white);
    }

    // the keyboard: white keys full height, black ones shorter, lit red when sounding
    g.setColour (px::black);
    g.fillRect (keys);
    for (int k = first; k <= last; ++k)
    {
        const int x0 = columnX (k), x1 = columnX (k + 1);
        const bool down = proc.player.keyDown[(size_t) k].load() || heldKey == k;
        juce::Rectangle<int> key (x0, keys.getY(), juce::jmax (1, x1 - x0 - 1), keys.getHeight());
        if (isBlack (k))
        {
            g.setColour (px::frame);
            g.fillRect (key);
            g.setColour (down ? px::red : px::black);
            g.fillRect (key.withHeight (keys.getHeight() * 5 / 8).reduced (1, 0));
            g.setColour (down ? px::red : px::cell);
            g.fillRect (key.withTrimmedTop (keys.getHeight() * 5 / 8));
        }
        else
        {
            g.setColour (down ? px::red : px::white);
            g.fillRect (key);
            g.setColour (down ? px::red.darker (0.3f) : px::grey);
            g.fillRect (key.removeFromBottom (4));
        }
    }

    // the range to record, and the notes that will be
    g.setColour (px::black);
    g.fillRect (handles);
    const int lo = juce::jmin (s.lowNote, s.highNote), hi = juce::jmax (s.lowNote, s.highNote);
    {
        const int x0 = columnX (lo), x1 = columnX (hi + 1);
        px::dither (g, { x0, handles.getY() + 8, x1 - x0, 6 }, px::yellow, 1);
        for (int n : notes)
        {
            g.setColour (px::white);
            g.fillRect (columnX (n) + (int) (colW * 0.5f) - 2, handles.getY() + 8, 4, 6);
        }
        auto tri = [&] (int key, bool left)
        {
            const int cx = left ? columnX (key) : columnX (key + 1);
            g.setColour (px::yellow);
            for (int r = 0; r < 5; ++r)
                g.fillRect (left ? cx : cx - (r + 1) * 2, handles.getY() + 4 + r * 3, (r + 1) * 2, 3);
        };
        tri (lo, true);
        tri (hi, false);
    }

    // octave names
    for (int k = first; k <= last; ++k)
        if (k % 12 == 0)
            px::drawText (g, rk::noteName (k), { columnX (k) + 2, labels.getY(), 40, labels.getHeight() }, juce::Justification::centredLeft,
                          px::label (9.0f), px::dim);
}

void KeyMap::playKey (int key, int velocity)
{
    if (heldKey == key)
        return;
    releaseKey();
    heldKey = key;
    proc.audition (key, velocity, true);
    repaint();
}

void KeyMap::releaseKey()
{
    if (heldKey >= 0)
        proc.audition (heldKey, 0, false);
    heldKey = -1;
    repaint();
}

void KeyMap::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    auto& s = proc.session;
    if (handles.contains (p) && ! proc.isSampling())
    {
        const int k = keyAt (p.x);
        const int lo = juce::jmin (s.lowNote, s.highNote), hi = juce::jmax (s.lowNote, s.highNote);
        dragHandle = std::abs (k - lo) <= std::abs (k - hi) ? 0 : 1;
        mouseDrag (e);
        return;
    }
    if (keys.contains (p))
    {
        const int k = keyAt (p.x);
        if ((e.mods.isAltDown() || e.mods.isCommandDown()) && s.noteMode == rk::NoteMode::custom && ! proc.isSampling())
        {
            auto& c = s.customNotes;
            auto it = std::find (c.begin(), c.end(), k);
            if (it != c.end()) c.erase (it); else c.push_back (k);
            std::sort (c.begin(), c.end());
            if (onRangeChanged) onRangeChanged();
            repaint();
            return;
        }
        // higher on the key = softer
        const float t = (float) (p.y - keys.getY()) / (float) juce::jmax (1, keys.getHeight());
        playKey (k, juce::jlimit (1, 127, 20 + juce::roundToInt (t * 107.0f)));
        return;
    }
    if (map.contains (p) && ins != nullptr && ! sampling)
    {
        const int k = keyAt (p.x), v = velocityAt (p.y);
        for (size_t i = 0; i < ins->samples.size(); ++i)
        {
            const auto& x = ins->samples[i];
            if (k >= x.keyLo && k <= x.keyHi && v >= x.velLo && v <= x.velHi)
            {
                // a second click on the same zone goes through its takes
                int pick = (int) i;
                if (selected >= 0 && selected < (int) ins->samples.size())
                {
                    const auto& cur = ins->samples[(size_t) selected];
                    if (cur.root == x.root && cur.velocity == x.velocity)
                        for (size_t j = 0; j < ins->samples.size(); ++j)
                        {
                            const auto& y = ins->samples[j];
                            if (y.root == x.root && y.velocity == x.velocity && y.rr == (cur.rr + 1) % juce::jmax (1, ins->session.roundRobins))
                                pick = (int) j;
                        }
                }
                selected = pick;
                if (onSelect) onSelect (pick);
                repaint();
                return;
            }
        }
    }
}

void KeyMap::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (dragHandle >= 0)
    {
        auto& s = proc.session;
        const int k = keyAt (p.x);
        int lo = juce::jmin (s.lowNote, s.highNote), hi = juce::jmax (s.lowNote, s.highNote);
        if (dragHandle == 0) lo = juce::jmin (k, hi); else hi = juce::jmax (k, lo);
        if (lo != s.lowNote || hi != s.highNote)
        {
            s.lowNote = lo;
            s.highNote = hi;
            if (onRangeChanged) onRangeChanged();
            repaint();
        }
        return;
    }
    if (heldKey >= 0 && keys.contains (p))
    {
        const float t = (float) (p.y - keys.getY()) / (float) juce::jmax (1, keys.getHeight());
        playKey (keyAt (p.x), juce::jlimit (1, 127, 20 + juce::roundToInt (t * 107.0f)));
    }
}

void KeyMap::mouseUp (const juce::MouseEvent&)
{
    dragHandle = -1;
    releaseKey();
}

void KeyMap::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int k = (map.contains (p) || keys.contains (p) || handles.contains (p)) ? keyAt (p.x) : -1;
    const int v = map.contains (p) ? velocityAt (p.y) : -1;
    if (k != hoverKey || v != hoverVel)
    {
        hoverKey = k;
        hoverVel = v;
        repaint (map);
    }
}

void KeyMap::mouseExit (const juce::MouseEvent&)
{
    hoverKey = hoverVel = -1;
    repaint (map);
}
