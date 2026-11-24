#include "RadioUi.h"

namespace te
{
namespace
{
    float P (const ReplikantProcessor& p, int id)    { return p.player.get (id); }
    juce::String kbText (double khz)                 { return juce::String (khz, khz < 10000.0 ? 2 : 1); }

    // the bands a shortwave receiver hears (their middle), the radio amateurs', the time signals
    struct Band { const char* name; double khz; const char* mode; };
    const Band bands[] = {
        { "lw", 198.0, "am" }, { "mw", 999.0, "am" }, { "120m", 2400.0, "am" }, { "90m", 3300.0, "am" }, { "75m", 3950.0, "am" },
        { "60m", 4900.0, "am" }, { "49m", 6000.0, "am" }, { "41m", 7300.0, "am" }, { "31m", 9600.0, "am" }, { "25m", 11850.0, "am" },
        { "22m", 13700.0, "am" }, { "19m", 15400.0, "am" }, { "16m", 17700.0, "am" }, { "13m", 21600.0, "am" }, { "11m", 25800.0, "am" },
        { "ham 40", 7150.0, "lsb" }, { "ham 20", 14200.0, "usb" }, { "time", 10000.0, "am" } };

    juce::Colour stateColour (int st)
    {
        switch (st)
        {
            case radio::live:       return col::sGreen;
            case radio::connecting:
            case radio::buffering:
            case radio::tuning:     return col::sYellow;
            case radio::busy:
            case radio::failed:     return col::sRed;
            default:                return col::screenDim;
        }
    }
}

juce::String radioStateText (int st)
{
    switch (st)
    {
        case radio::off:        return "off";
        case radio::connecting: return "connecting";
        case radio::buffering:  return "buffering";
        case radio::live:       return "on air";
        case radio::tuning:     return "tuning";
        case radio::busy:       return "busy";
        case radio::failed:     return "failed";
        case radio::standby:    return "standby";
        default:                return "no station";
    }
}

//============================================================================== the screen
void RadioScreen::paint (juce::Graphics& g, juce::Rectangle<float> r, ReplikantProcessor& p, int selectedPad)
{
    auto& e = p.radio;
    const int rx = juce::jlimit (0, radio::numRx - 1, (int) P (p, snd::rdRx));
    const auto& R = e.bank.rx[rx];
    const int st = R.state.load();
    auto info = r.removeFromBottom (18.0f);
    r.removeFromBottom (4.0f);
    const auto take = p.radioTake();
    const bool hasTake = take != nullptr && take->audio != nullptr && take->length() > 0;
    auto live = hasTake ? r.removeFromTop (r.getHeight() * 0.46f) : r;
    if (hasTake)
        r.removeFromTop (8.0f);

    // ---- what is heard going by (the newest on the right); (0.4.16) a window on the last two minutes: the wheel zooms,
    // sideways (or Shift) goes back; a click replays from there, a drag takes the passage
    {
        const double takeSecs = radio::takeSeconds ((int) P (p, snd::rdTake));
        sr = e.sampleRate();
        const double autoSecs = juce::jlimit (6.0, radio::memorySeconds, std::round (takeSecs * 1.5));
        const double secs = viewSecs > 0.0 ? juce::jlimit (2.0, radio::memorySeconds, viewSecs) : autoSecs;
        viewBack = juce::jlimit (0.0, radio::memorySeconds - secs, viewBack);
        shownRight = heldRight >= 0 ? heldRight : e.heardNow() - (int64_t) (viewBack * sr);
        shownSecs = secs;
        auto head = live.removeFromTop (14.0f);
        const int mode = (int) P (p, snd::rdMode);
        const int64_t rp = e.replaying();
        juce::String what = "heard · "_fr + juce::String (juce::roundToInt (secs)) + " s"
                          + (viewBack > 0.05 ? " · " + juce::String (viewBack, 1) + " s back" : juce::String())
                          + " · 2 min kept";
        if (rp >= 0)
            what << " · replay "_fr << juce::String ((double) (rp - e.heardNow()) / sr, 1) << " s · until the click · right end: live";
        if (mode == radio::modeCut)
            what << " · cut "_fr << e.cutCount.load() << " steps";
        else if (mode != radio::modeLive && e.loopSeconds.load() > 0.0f)
            what << " · "_fr << (mode == radio::modeLoop ? "loop " : "dig ") << juce::String (e.loopSeconds.load(), 2) << " s";
        text (g, what, head, mono (10.0f), rp >= 0 ? col::sGreen : col::screenDim);
        auto w = live;
        heardArea = w;
        const float mid = w.getCentreY(), half = w.getHeight() * 0.47f;
        g.setColour (col::screenLine);
        g.drawHorizontalLine ((int) mid, w.getX(), w.getRight());
        // how far back GRAB reaches (from now)
        const float gx = xOf (e.heardNow() - (int64_t) (takeSecs * sr));
        g.setColour (col::sOrange.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (gx, w.getY(), w.getRight(), w.getBottom()));
        g.setColour (col::sOrange.withAlpha (0.5f));
        g.fillRect (gx, w.getY(), 1.0f, w.getHeight());
        text (g, "grab " + juce::String (juce::roundToInt (takeSecs)) + " s", { gx + 4.0f, w.getY(), 80.0f, 12.0f }, mono (9.0f), col::sOrange.withAlpha (0.8f));
        // REC: from where it started
        if (e.recording())
        {
            const double rs = e.recSeconds();
            const float rx0 = juce::jmax (w.getX(), xOf (e.heardNow() - (int64_t) (rs * sr)));
            g.setColour (col::sRed.withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (rx0, w.getY(), w.getRight(), w.getBottom()));
            g.setColour (col::sRed);
            g.fillRect (rx0, w.getY(), 1.5f, w.getHeight());
            text (g, "rec " + juce::String (rs, 1) + " s", { rx0 + 4.0f, w.getBottom() - 13.0f, 90.0f, 12.0f }, mono (9.0f, 600), col::sRed);
        }
        // the peaks (the window's: its right end counted in peaks from the newest)
        const int64_t written = juce::jmin (e.peaksWritten(), (shownRight + radio::peakFrames - 1) / radio::peakFrames);
        const int total = juce::jmax (1, (int) (secs * sr / radio::peakFrames));
        const int cols = juce::jmax (1, (int) w.getWidth());
        const int64_t from = written - total;
        const int64_t oldestPeak = juce::jmax ((int64_t) 0, e.peaksWritten() - (int64_t) e.peakCount() + 2);
        g.setColour (col::screenText);
        for (int x = 0; x < cols; ++x)
        {
            const int64_t a = from + (int64_t) x * total / cols, b = juce::jmax (a + 1, from + (int64_t) (x + 1) * total / cols);
            float pk = 0.0f;
            for (int64_t i = juce::jmax (oldestPeak, a); i < b && i < written; ++i)
                pk = juce::jmax (pk, e.peakAt (i));
            const float h = juce::jmin (1.0f, std::sqrt (pk)) * half;
            if (h > 0.4f)
                g.fillRect (w.getX() + (float) x, mid - h, 1.0f, 2.0f * h);
        }
        // (0.4.16) the passage being chosen; where the replay reads
        if (selA >= 0 && selB >= 0 && selA != selB)
        {
            const float x0 = juce::jlimit (w.getX(), w.getRight(), xOf (juce::jmin (selA, selB)));
            const float x1 = juce::jlimit (w.getX(), w.getRight(), xOf (juce::jmax (selA, selB)));
            g.setColour (col::sBlue.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, w.getY(), x1, w.getBottom()));
            g.setColour (col::sBlue);
            g.fillRect (x0, w.getY(), 1.5f, w.getHeight());
            g.fillRect (x1 - 1.5f, w.getY(), 1.5f, w.getHeight());
            text (g, "take " + juce::String ((double) std::abs (selB - selA) / sr, 2) + " s", { x0 + 4.0f, w.getBottom() - 13.0f, 120.0f, 12.0f },
                  mono (9.0f, 600), col::sBlue);
        }
        if (rp >= 0)
        {
            const float x = xOf (rp);
            if (x >= w.getX() && x <= w.getRight())
            {
                g.setColour (col::sGreen);
                g.fillRect (x - 1.0f, w.getY(), 2.0f, w.getHeight());
                juce::Path tri;
                tri.addTriangle (x - 5.0f, w.getY(), x + 5.0f, w.getY(), x, w.getY() + 7.0f);
                g.fillPath (tri);
            }
        }
        // the squelch shut: a dim veil
        if (mode == radio::modeLive && P (p, snd::rdSquelch) > 0.005f && e.gateOpen.load() < 0.5f)
        {
            g.setColour (col::screen.withAlpha (0.45f));
            g.fillRect (w.withLeft (w.getRight() - 40.0f));
            text (g, "sql", { w.getRight() - 40.0f, w.getY(), 40.0f, 12.0f }, mono (9.0f, 600), col::sYellow, juce::Justification::centred);
        }
        // CUT: the pattern, a cell a step (its receiver's colour, its ratchets, backwards, cut short), the step
        // playing lit; a click on it draws another
        cutArea = {};
        if (mode == radio::modeCut && P (p, snd::rdOn) > 0.5f)
        {
            const int count = juce::jlimit (1, radio::cutSteps, e.cutCount.load());
            const int now = e.cutNow.load();
            cutArea = w.removeFromBottom (juce::jmin (34.0f, w.getHeight() * 0.3f)).reduced (0.0f, 2.0f);
            g.setColour (col::screen.withAlpha (0.85f));
            g.fillRect (cutArea.expanded (0.0f, 2.0f));
            const juce::Colour rxCol[radio::numRx] = { col::sYellow, col::sBlue, col::sOrange, col::sViolet };
            const float cw = cutArea.getWidth() / (float) juce::jmax (16, count);
            for (int j = 0; j < count; ++j)
            {
                const auto cell = juce::Rectangle<float> (cutArea.getX() + cw * (float) j, cutArea.getY(), cw, cutArea.getHeight()).reduced (1.5f, 0.0f);
                const int v = e.cutShown[(size_t) j].load();
                const bool lit = j == now;
                if (v < 0)
                {
                    g.setColour (lit ? col::screenDim : col::screenLine);
                    g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.0f);
                    continue;
                }
                const auto c = rxCol[juce::jlimit (0, radio::numRx - 1, v & 15)];
                const int hits = 1 + ((v >> 4) & 15);
                auto body = (v & 512) != 0 ? cell.withWidth (cell.getWidth() * 0.6f) : cell;
                g.setColour (c.withAlpha (lit ? 1.0f : 0.55f));
                g.fillRoundedRectangle (body, 3.0f);
                for (int h = 1; h < hits; ++h)
                {
                    g.setColour (col::screen.withAlpha (0.8f));
                    g.fillRect (cell.getX() + cell.getWidth() * (float) h / (float) hits - 0.75f, cell.getY() + 3.0f, 1.5f, cell.getHeight() - 6.0f);
                }
                if ((v & 256) != 0)
                {
                    juce::Path a;
                    const float y = cell.getCentreY(), x0 = cell.getX() + 3.0f;
                    a.addTriangle (x0, y, x0 + 5.0f, y - 4.0f, x0 + 5.0f, y + 4.0f);
                    g.setColour (col::screen);
                    g.fillPath (a);
                }
            }
            text (g, "cut · clique pour un autre motif"_fr, cutArea.withY (cutArea.getY() - 13.0f).withHeight (12.0f), mono (9.0f), col::screenDim,
                  juce::Justification::centredRight);
        }
        // the loop playing (LOOP, DIG): where in it
        const float lp = e.loopPos.load();
        if (mode != radio::modeLive && mode != radio::modeCut && lp >= 0.0f)
        {
            auto bar = w.removeFromBottom (3.0f).withTrimmedLeft (w.getWidth() * 0.75f);
            g.setColour (col::screenLine);
            g.fillRect (bar);
            g.setColour (col::sGreen);
            g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, lp)));
        }
        if (P (p, snd::rdOn) < 0.5f)
            text (g, "off air : la radio est coupée, les pads restent jouables"_fr, live, mono (12.0f), col::screenDim, juce::Justification::centred);
        else if (st == radio::none && R.hw == 0)
            text (g, "page tune : accorde un récepteur sur une station ou une fréquence"_fr, live, mono (12.0f), col::screenDim, juce::Justification::centred);
    }

    // ---- the take, cut into its pads
    if (hasTake)
    {
        const auto& A = *take->audio;
        const int len = take->length();
        auto head = r.removeFromTop (14.0f);
        juce::String what;
        what << "take · "_fr << juce::String (A.l.size() / A.sampleRate, 2) << " s · "_fr << (int) take->slices.size()
             << (take->slices.size() == 1 ? " pad" : " pads") << (p.radioSlicing() ? juce::String (" · découpe…"_fr) : juce::String());
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (mono (10.0f), themed (what)) + 12.0f;
        text (g, what, head.removeFromLeft (tw), mono (10.0f), col::screenDim);
        text (g, juce::String (take->name), head, mono (10.0f), col::screenGhost, juce::Justification::centredLeft, true);
        auto w = r;
        if (cachedTake != take.get() || std::abs (cachedW - w.getWidth()) > 0.5f || std::abs (cachedH - w.getHeight()) > 0.5f)
        {
            cachedTake = take.get();
            cachedW = w.getWidth();
            cachedH = w.getHeight();
            takeWave.clear();
            const int cols = juce::jmax (2, (int) w.getWidth());
            const float mid = w.getHeight() * 0.5f;
            std::vector<float> lo ((size_t) cols), hi ((size_t) cols);
            for (int x = 0; x < cols; ++x)
            {
                const int a = (int) ((int64_t) x * len / cols), b = juce::jmax (a + 1, (int) ((int64_t) (x + 1) * len / cols));
                float mn = 0.0f, mx = 0.0f;
                for (int i = a; i < b && i < len; ++i)
                {
                    const float v = 0.5f * (A.l[(size_t) i] + A.r[(size_t) i]);
                    mn = juce::jmin (mn, v);
                    mx = juce::jmax (mx, v);
                }
                lo[(size_t) x] = mn;
                hi[(size_t) x] = mx;
            }
            float peak = 1.0e-6f;
            for (int x = 0; x < cols; ++x)
                peak = juce::jmax (peak, hi[(size_t) x], -lo[(size_t) x]);
            const float k = 0.96f / peak;
            takeWave.startNewSubPath (0.0f, mid - hi[0] * k * mid);
            for (int x = 1; x < cols; ++x)
                takeWave.lineTo ((float) x, mid - hi[(size_t) x] * k * mid);
            for (int x = cols - 1; x >= 0; --x)
                takeWave.lineTo ((float) x, mid - lo[(size_t) x] * k * mid);
            takeWave.closeSubPath();
        }
        auto X = [&] (double frame) { return w.getX() + (float) (frame / (double) len) * w.getWidth(); };
        // the pad chosen, shaded; the pads struck, lit
        for (int i = 0; i < (int) take->slices.size(); ++i)
        {
            const auto& s = take->slices[(size_t) i];
            const float glow = e.padGlow[(size_t) i].load();
            if (i == selectedPad || glow > 0.05f)
            {
                g.setColour ((i == selectedPad ? col::sOrange : col::sRed).withAlpha (i == selectedPad ? 0.14f : 0.22f * glow));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (X (s.start), w.getY(), X (s.end), w.getBottom()));
            }
        }
        g.setColour (col::screenLine);
        g.drawHorizontalLine ((int) w.getCentreY(), w.getX(), w.getRight());
        g.setColour (col::screenText.withAlpha (0.9f));
        g.fillPath (takeWave, juce::AffineTransform::translation (w.getX(), w.getY()));
        for (int i = 0; i < (int) take->slices.size(); ++i)
        {
            const auto& s = take->slices[(size_t) i];
            const float x = X (s.start);
            g.setColour (col::sOrange);
            g.fillRect (x - 0.75f, w.getY(), 1.5f, w.getHeight());
            text (g, juce::String (i + 1), { x + 3.0f, w.getY(), 24.0f, 12.0f }, mono (10.0f, 600), col::sOrange);
            const float at = e.padAt[(size_t) i].load();
            if (at >= 0.0f)
            {
                g.setColour (col::sRed);
                g.fillRect (X (s.start + at * (float) (s.end - s.start)) - 1.0f, w.getY(), 2.0f, w.getHeight());
            }
        }
    }

    // ---- the receiver: its state, what comes in
    {
        juce::String t;
        t << "rx " << (rx + 1) << " · "_fr << radioStateText (st);
        if (st == radio::live || st == radio::buffering || st == radio::tuning)
        {
            t << " · "_fr << juce::String (R.fill.load(), 1) << " s in hand";
            const float sp = (R.speed.load() - 1.0f) * 100.0f;
            if (std::abs (sp) > 0.01f)
                t << " · speed "_fr << (sp > 0 ? "+" : "") << juce::String (sp, 2) << " %";
            if (R.dropouts.load() > 0)
                t << " · "_fr << R.dropouts.load() << " dropouts";
            const int mc = R.meterC.load();
            if (mc != -32768)
            {
                // the S-meter: S9 at -73 dBm, 6 dB an S unit
                const float dbm = (float) mc * 0.01f;
                const int s = juce::jlimit (0, 9, (int) std::floor ((dbm + 127.0f) / 6.0f));
                t << " · s"_fr << s << (dbm > -73.0f ? "+" + juce::String (juce::roundToInt (dbm + 73.0f)) : juce::String()) << " (" << juce::roundToInt (dbm) << " dbm)";
            }
        }
        const int mode = (int) P (p, snd::rdMode);
        const float tune = P (p, snd::rdTune);
        if (std::abs (tune) > 0.01f)
            t << " · "_fr << (mode == radio::modeLive ? "shift " + juce::String (juce::roundToInt (radio::shiftHz (tune))) + " hz"
                                                    : "tune " + juce::String (tune, 2) + " st");
        text (g, t, info, mono (11.0f), stateColour (st).interpolatedWith (col::screenDim, 0.35f));
    }
}

//============================================================================== the strip
RadioStrip::RadioStrip (ReplikantProcessor& p, std::function<void (const juce::String&, bool)> m) : proc (p), message (std::move (m)) {}

void RadioStrip::layout()
{
    auto r = getLocalBounds().toFloat();
    auto right = r.removeFromRight (300.0f);
    r.removeFromRight (12.0f);
    r.removeFromTop (24.0f);
    const float gap = 6.0f;
    const float pw = (r.getWidth() - 7.0f * gap) / 8.0f, ph = (r.getHeight() - gap) / 2.0f;
    for (int i = 0; i < radio::numPads; ++i)
        pads[i] = { r.getX() + (float) (i % 8) * (pw + gap), r.getY() + (float) (i / 8) * (ph + gap), pw, ph };
    const float h = (right.getHeight() - 2.0f * 8.0f) / 3.0f;
    auto row = right.removeFromTop (h);
    const float kw = (row.getWidth() - 2.0f * 8.0f) / 3.0f;
    airKey = row.removeFromLeft (kw);
    row.removeFromLeft (8.0f);
    recKey = row.removeFromLeft (kw);
    row.removeFromLeft (8.0f);
    grabKey = row;
    right.removeFromTop (8.0f);
    // the routes: what is sent (a chip), then where: the SAMPLER, the DRUM track shown
    auto takeRow = right.removeFromTop (h);
    right.removeFromTop (8.0f);
    auto padRow = right;
    for (auto* rowp : { &takeRow, &padRow })
    {
        auto& rw = *rowp;
        auto chip = rw.removeFromLeft (70.0f);
        rw.removeFromLeft (14.0f);
        const float bw = (rw.getWidth() - 6.0f) * 0.5f;
        auto a = rw.removeFromLeft (bw);
        rw.removeFromLeft (6.0f);
        if (rowp == &takeRow) { takeChip = chip; takeKey = a; takeDrumKey = rw; }
        else                  { padChip = chip; padKey = a; padDrumKey = rw; }
    }
}

// GRAB's picture: the time just gone, caught (an arrow running back round a clock)
static void grabPicto (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto b = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())).reduced (1.0f);
    const auto ctr = b.getCentre();
    const float rad = b.getWidth() * 0.42f;
    juce::Path arc;
    arc.addCentredArc (ctr.x, ctr.y, rad, rad, 0.0f, 0.35f, juce::MathConstants<float>::twoPi - 0.15f, true);
    g.setColour (c);
    g.strokePath (arc, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // its head, at the start (going back)
    const float a0 = 0.35f;
    const juce::Point<float> tip (ctr.x + std::sin (a0) * rad, ctr.y - std::cos (a0) * rad);
    juce::Path head;
    head.addTriangle (tip.x - 3.5f, tip.y - 2.5f, tip.x + 2.0f, tip.y - 4.0f, tip.x + 0.5f, tip.y + 2.5f);
    g.fillPath (head);
    // the hands: what was just played
    g.drawLine (ctr.x, ctr.y, ctr.x, ctr.y - rad * 0.55f, 1.4f);
    g.drawLine (ctr.x, ctr.y, ctr.x - rad * 0.45f, ctr.y + rad * 0.2f, 1.4f);
}

// a route's key: its picture, its name; the DRUM's with the track's number on a badge
static void routeKey (juce::Graphics& g, juce::Rectangle<float> k, bool ok, bool accent, Picto icon, const juce::String& name, const juce::String& badge)
{
    key (g, k, ! ok ? KeyState::off : (accent ? KeyState::accent : KeyState::normal), false, 9.0f);
    const auto ink = ! ok ? col::ink3 : (accent ? col::accentInk : col::ink);
    auto face = k.withTrimmedBottom (3.0f).reduced (8.0f, 4.0f);
    auto iconArea = face.removeFromLeft (24.0f);
    picto (g, icon, iconArea.withSizeKeepingCentre (20.0f, 20.0f), ink, 1.3f);
    if (badge.isNotEmpty())
    {
        // the track, on a badge at the key's end
        const auto b = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ face.getRight() - 9.0f, face.getCentreY() });
        face.removeFromRight (22.0f);
        g.setColour (ok ? (accent ? col::accentInk.withAlpha (0.9f) : col::red) : col::ink3);
        g.fillEllipse (b);
        text (g, badge, b, mono (10.0f, 700), ok && accent ? col::red : juce::Colours::white, juce::Justification::centred);
    }
    face.removeFromLeft (4.0f);
    text (g, name, face, mono (11.0f, 600), ink, juce::Justification::centredLeft);
}

// what is sent: a chip with its picture, an arrow out of it
static void routeChip (juce::Graphics& g, juce::Rectangle<float> c, bool ok, Picto icon, const juce::String& name)
{
    g.setColour (ok ? col::well : col::well.withAlpha (0.5f));
    g.fillRoundedRectangle (c, 8.0f);
    const auto ink = ok ? col::screenText : col::screenGhost;
    auto face = c.reduced (6.0f, 4.0f);
    picto (g, icon, face.removeFromTop (face.getHeight() * 0.55f).withSizeKeepingCentre (20.0f, 20.0f), ink, 1.3f);
    text (g, name, face, mono (10.0f, 600), ink, juce::Justification::centred, true);
    // the arrow to the two places
    const float x0 = c.getRight() + 2.0f, x1 = c.getRight() + 12.0f, y = c.getCentreY();
    g.setColour (ok ? col::deckInk2 : col::deckInk2.withAlpha (0.35f));
    g.drawLine (x0, y, x1 - 2.0f, y, 1.4f);
    juce::Path head;
    head.addTriangle (x1, y, x1 - 4.5f, y - 3.5f, x1 - 4.5f, y + 3.5f);
    g.fillPath (head);
}

void RadioStrip::paint (juce::Graphics& g)
{
    layout();
    auto& e = proc.radio;
    const auto take = proc.radioTake();
    const int n = take != nullptr ? (int) take->slices.size() : 0;
    if (selected >= n)
        selected = -1;
    // each pad's outline, made once per take
    if (take.get() != cachedTake)
    {
        cachedTake = take.get();
        padWaves.assign ((size_t) n, {});
        if (take != nullptr && take->audio != nullptr)
            for (int i = 0; i < n; ++i)
            {
                const auto& s = take->slices[(size_t) i];
                auto& pw = padWaves[(size_t) i];
                pw.assign (28, 0.0f);
                const int len = juce::jmax (1, s.end - s.start);
                float peak = 1.0e-6f;
                for (int k = 0; k < 28; ++k)
                {
                    const int a = s.start + len * k / 28, b = juce::jmax (a + 1, s.start + len * (k + 1) / 28);
                    for (int j = a; j < b && j < take->length(); ++j)
                        pw[(size_t) k] = juce::jmax (pw[(size_t) k], std::abs (take->audio->l[(size_t) j]));
                    peak = juce::jmax (peak, pw[(size_t) k]);
                }
                for (auto& v : pw)
                    v /= peak;
            }
    }
    // the line above: what the pads are, where they are on the keys
    {
        auto top = getLocalBounds().toFloat().withTrimmedRight (312.0f).removeFromTop (20.0f);
        juce::String t = n > 0 ? juce::String ("pads : c1 – d#2 sur ton clavier · clique sur un pad pour le choisir"_fr)
                               : juce::String ("grab : les dernières secondes entendues · rec : à partir de maintenant"_fr);
        if (proc.radioSlicing())
            t = "découpe de la prise…"_fr;
        text (g, t, top, mono (11.0f, 500), col::deckInk2);
    }
    // (a pad struck glows, and stays lit while it plays; the glow fades in time, whatever the frame rate)
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float fade = lastPaint > 0.0 ? (float) std::exp (-(now - lastPaint) / 0.15) : 1.0f;
    lastPaint = now;
    for (int i = 0; i < radio::numPads; ++i)
    {
        const bool has = i < n;
        const float glow = e.padGlow[(size_t) i].load();
        if (glow > 0.0f)
            e.padGlow[(size_t) i].store (glow * fade > 0.02f ? glow * fade : 0.0f);
        const bool lit = glow > 0.3f || e.padAt[(size_t) i].load() >= 0.0f;
        const bool down = held == i;
        key (g, pads[i], ! has ? KeyState::off : (i == selected ? KeyState::on : (lit ? KeyState::accent : KeyState::normal)), down, 7.0f);
        auto face = pads[i].withTrimmedBottom (3.0f).reduced (7.0f, 5.0f);
        if (down)
            face.translate (0.0f, 2.0f);
        const auto ink = ! has ? col::ink3 : (i == selected ? col::keyOnInk : (lit ? col::accentInk : col::ink));
        text (g, juce::String (i + 1), face.removeFromTop (14.0f), mono (11.0f, 600), ink);
        if (has && i < (int) padWaves.size())
        {
            const auto& pw = padWaves[(size_t) i];
            const float bw = face.getWidth() / (float) pw.size();
            g.setColour (ink.withAlpha (0.75f));
            for (size_t k = 0; k < pw.size(); ++k)
            {
                const float h = juce::jmax (1.0f, pw[k] * face.getHeight() * 0.9f);
                g.fillRect (face.getX() + (float) k * bw, face.getCentreY() - h * 0.5f, juce::jmax (1.0f, bw - 1.0f), h);
            }
        }
    }
    // ON AIR, REC, GRAB: a picture and a word each
    const bool onAir = P (proc, snd::rdOn) > 0.5f;
    key (g, airKey, onAir ? KeyState::on : KeyState::normal, false, 10.0f);
    {
        auto t = airKey.withTrimmedBottom (3.0f).reduced (8.0f, 5.0f);
        const auto ink = onAir ? col::keyOnInk : col::ink;
        picto (g, Picto::antenna, t.removeFromTop (t.getHeight() * 0.58f).withSizeKeepingCentre (22.0f, 22.0f), ink, 1.3f);
        text (g, onAir ? "on air" : "off air", t, mono (10.5f, 600), ink, juce::Justification::centred);
        g.setColour (onAir ? col::sRed : col::ink3.withAlpha (0.5f));
        g.fillEllipse (airKey.getRight() - 15.0f, airKey.getY() + 7.0f, 7.0f, 7.0f);
    }
    const bool rec = e.recording();
    const bool blink = std::fmod (juce::Time::getMillisecondCounterHiRes(), 700.0) < 350.0;
    g.setColour (rec ? col::well : col::red);
    g.fillRoundedRectangle (recKey, 10.0f);
    {
        auto t = recKey.reduced (8.0f, 5.0f);
        auto icon = t.removeFromTop (t.getHeight() * 0.58f).withSizeKeepingCentre (14.0f, 14.0f);
        g.setColour (rec ? (blink ? col::sRed : col::sRed.withAlpha (0.4f)) : juce::Colours::white);
        if (rec)
            g.fillRoundedRectangle (icon.reduced (1.0f), 2.0f);
        else
            g.fillEllipse (icon);
        text (g, rec ? juce::String (e.recSeconds(), 1) + " s" : juce::String ("rec"), t, mono (10.5f, 600), rec ? col::screenText : juce::Colours::white,
              juce::Justification::centred);
    }
    key (g, grabKey, rec ? KeyState::off : KeyState::normal, false, 10.0f);
    {
        auto t = grabKey.withTrimmedBottom (3.0f).reduced (8.0f, 5.0f);
        const auto ink = rec ? col::ink3 : col::ink;
        grabPicto (g, t.removeFromTop (t.getHeight() * 0.58f).withSizeKeepingCentre (22.0f, 22.0f), ink);
        text (g, "grab " + juce::String (juce::roundToInt (radio::takeSeconds ((int) P (proc, snd::rdTake)))) + " s", t, mono (10.5f, 600), ink,
              juce::Justification::centred);
    }
    // the routes: the take, or the pad chosen, to the SAMPLER or to the DRUM track shown
    const bool hasTake = take != nullptr && take->audio != nullptr;
    const bool padOk = hasTake && selected >= 0;
    const auto track = juce::String (juce::jlimit (0, drum::numTracks - 1, proc.drumTrack.load()) + 1);
    routeChip (g, takeChip, hasTake, Picto::slice, "take");
    routeKey (g, takeKey, hasTake, true, Picto::cassette, "sampler", {});
    routeKey (g, takeDrumKey, hasTake, true, Picto::drum, "drum", track);
    routeChip (g, padChip, padOk, Picto::pads, padOk ? "pad " + juce::String (selected + 1) : juce::String ("pad"));
    routeKey (g, padKey, padOk, false, Picto::cassette, "sampler", {});
    routeKey (g, padDrumKey, padOk, false, Picto::drum, "drum", track);
}

void RadioStrip::mouseDown (const juce::MouseEvent& e)
{
    layout();
    const auto take = proc.radioTake();
    const int n = take != nullptr ? (int) take->slices.size() : 0;
    for (int i = 0; i < radio::numPads; ++i)
        if (pads[i].contains (e.position))
        {
            if (i >= n)
                return;
            selected = i;
            held = i;
            const float y = (e.position.y - pads[i].getY()) / juce::jmax (1.0f, pads[i].getHeight());
            proc.audition (radio::firstPadNote + i, juce::jlimit (30, 127, (int) (50 + 77 * y)), true);
            repaint();
            return;
        }
    juce::String why;
    if (airKey.contains (e.position))
    {
        if (auto* param = proc.params.getParameter (snd::def (snd::rdOn).id))
        {
            const bool on = param->getValue() > 0.5f;
            param->beginChangeGesture();
            param->setValueNotifyingHost (on ? 0.0f : 1.0f);
            param->endChangeGesture();
            message (on ? "Radio coupée : les pads et la prise restent jouables. ON AIR pour la rallumer."_fr
                        : "Radio rallumée."_fr, false);
        }
    }
    else if (recKey.contains (e.position))
    {
        const bool was = proc.radio.recording();
        if (! proc.radioRec (why))
            message (why, true);
        else if (! was)
            message ("REC : tout ce que tu entends est gardé jusqu'à STOP (30 s au plus)."_fr, false);
    }
    else if (grabKey.contains (e.position))
    {
        if (! proc.radioGrab (why))
            message (why, true);
        else
            message ("Prise : les "_fr + juce::String (juce::roundToInt (radio::takeSeconds ((int) P (proc, snd::rdTake))))
                     + " dernières secondes, découpées à leurs attaques."_fr, false);
    }
    else if (takeKey.contains (e.position))
    {
        if (! proc.radioToSampler (-1, why))
            message (why, true);
    }
    else if (padKey.contains (e.position))
    {
        if (selected < 0)
            message ("Choisis un pad d'abord (clique dessus)."_fr, true);
        else if (! proc.radioToSampler (selected, why))
            message (why, true);
    }
    else if (takeDrumKey.contains (e.position))
    {
        if (! proc.radioToDrum (-1, proc.drumTrack.load(), why))
            message (why, true);
    }
    else if (padDrumKey.contains (e.position))
    {
        if (selected < 0)
            message ("Choisis un pad d'abord (clique dessus)."_fr, true);
        else if (! proc.radioToDrum (selected, proc.drumTrack.load(), why))
            message (why, true);
    }
    repaint();
}

void RadioStrip::mouseUp (const juce::MouseEvent&)
{
    if (held >= 0)
        proc.audition (radio::firstPadNote + held, 0, false);
    held = -1;
    repaint();
}

//============================================================================== the tuner
RadioTuner::RadioTuner (ReplikantProcessor& p, std::function<void (const juce::String&, bool)> m) : proc (p), message (std::move (m))
{
    for (auto* ed : { &query, &freq, &address })
    {
        ed->setColour (juce::TextEditor::textColourId, col::screenText);
        ed->setColour (juce::TextEditor::highlightColourId, col::sOrange.withAlpha (0.35f));
        ed->setColour (juce::TextEditor::highlightedTextColourId, col::screenText);
        ed->setColour (juce::CaretComponent::caretColourId, col::sOrange);
        ed->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        ed->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        ed->setSelectAllWhenFocused (true);
        addAndMakeVisible (*ed);
    }
    query.setFont (mono (13.0f));
    query.setIndents (10, 6);
    query.setTextToShowWhenEmpty ("un nom, un genre, un pays (fr, de, jp)", col::screenGhost);
    query.onReturnKey = [this]
    {
        const auto t = query.getText().trim();
        auto* o = new juce::DynamicObject();
        if (t.length() == 2 && t.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
            o->setProperty ("cc", t.toUpperCase());
        else if (t.isNotEmpty())
            o->setProperty ("name", t);
        search (juce::var (o));
    };
    freq.setFont (grotesk (22.0f, 300));
    freq.setIndents (10, 2);
    freq.setJustification (juce::Justification::centredLeft);
    freq.onReturnKey = [this]
    {
        const double khz = freq.getText().retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue();
        if (khz > 0.0)
            setFrequency (khz);
        unfocusAllComponents();
    };
    address.setFont (mono (12.0f));
    address.setIndents (10, 4);
    address.setTextToShowWhenEmpty ("ou colle l'adresse d'un récepteur (http://…:8073)"_fr, col::screenGhost);
    address.onReturnKey = [this] { tuneAddress (address.getText()); unfocusAllComponents(); };
    list.setModel (this);
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, col::screenLine.brighter (0.4f));
    addAndMakeVisible (list);
}

RadioTuner::~RadioTuner()
{
    list.setModel (nullptr);
}

int RadioTuner::rx() const                      { return juce::jlimit (0, radio::numRx - 1, (int) P (proc, snd::rdRx)); }
RadioNet::Source RadioTuner::source() const     { return proc.hasRadioNet() ? const_cast<ReplikantProcessor&> (proc).radioNet().getSource (rx()) : RadioNet::Source {}; }

void RadioTuner::tune (const RadioNet::Source& s)
{
    proc.radioNet().setSource (rx(), s);
    list.repaint();
    repaint();
}

void RadioTuner::search (const juce::var& q)
{
    waitingFor = ++asked;
    error.clear();
    stations.clear();
    list.updateContent();
    proc.radioNet().search (asked, q);
    repaint();
}

void RadioTuner::askReceivers (bool fresh)
{
    waitingFor = ++asked;
    error.clear();
    proc.radioNet().receivers (asked, fresh);
    repaint();
}

void RadioTuner::setFrequency (double khz, const juce::String& mode)
{
    auto s = source();
    khz = juce::jlimit (10.0, 30000.0, khz);
    swKhz = khz;
    if (mode.isNotEmpty())
        swMode = mode;
    freq.setText (kbText (khz) + " khz", false);
    if (s.kind == 2)
    {
        s.khz = khz;
        s.mode = swMode;
        tune (s);
    }
    else
        message ("Choisis un récepteur dans la liste : il s'accordera sur "_fr + kbText (khz) + " kHz.", false);
    repaint();
}

// a receiver's address as a browser shows it ("http://host:8073/?f=9420am"): tuned as it is, at the frequency and in the
// mode its address says (else the ones chosen here)
void RadioTuner::tuneAddress (const juce::String& text)
{
    auto t = text.trim();
    if (t.isEmpty())
        return;
    const bool scheme = t.contains ("://");
    if (scheme && ! t.startsWithIgnoreCase ("http://"))
    {
        message ("Seules les adresses http:// des récepteurs KiwiSDR se branchent ici."_fr, true);
        return;
    }
    auto rest = scheme ? t.fromFirstOccurrenceOf ("://", false, false) : t;
    const auto hostPort = rest.upToFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    const auto params = rest.fromFirstOccurrenceOf ("?", false, false);
    juce::String host = hostPort;
    int port = scheme ? 80 : 8073;
    if (hostPort.containsChar (':'))
    {
        host = hostPort.upToLastOccurrenceOf (":", false, false);
        port = hostPort.fromLastOccurrenceOf (":", false, false).getIntValue();
    }
    else if (host.endsWithIgnoreCase (".proxy.kiwisdr.com"))
        port = 8073;
    if (host.isEmpty() || ! host.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") || port <= 0 || port > 65535)
    {
        message ("Adresse illisible : colle-la telle que ton navigateur l'affiche (http://…:8073)."_fr, true);
        return;
    }
    // ?f=9420am, f=7055lsb, f=27185/-2700,2700amnz6...
    for (const auto& part : juce::StringArray::fromTokens (params, "&", ""))
        if (part.startsWith ("f="))
        {
            const auto v = part.substring (2);
            const double khz = v.initialSectionContainingOnly ("0123456789.").getDoubleValue();
            if (khz > 0.0)
                swKhz = juce::jlimit (10.0, 30000.0, khz);
            const auto letters = v.fromFirstOccurrenceOf (v.initialSectionContainingOnly ("0123456789./-,"), false, false).toLowerCase();
            for (auto* md : { "usb", "lsb", "cw", "am" })
                if (letters.startsWith (md))
                {
                    swMode = md;
                    break;
                }
        }
    RadioNet::Source s;
    s.kind = 2;
    s.host = host;
    s.port = port;
    s.rxName = host;
    s.khz = swKhz;
    s.mode = swMode;
    freq.setText (kbText (swKhz) + " khz", false);
    tune (s);
    message ("Récepteur "_fr + host + ":" + juce::String (port) + " · " + kbText (swKhz) + " kHz " + swMode.toUpperCase(), false);
}

void RadioTuner::showView (int v)
{
    view = v;
    viewChosen = true;
    if (view == 1 && receivers.isEmpty())
        askReceivers (false);
    if (view == 0 && stations.isEmpty() && waitingFor < 0)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("order", "clickcount");
        search (juce::var (o));
    }
    list.updateContent();
    list.deselectAllRows();
    resized();
    repaint();
}

void RadioTuner::visibilityChanged()
{
    if (! isVisible())
    {
        stopTimer();
        return;
    }
    proc.radioNet();
    const auto s = source();
    if (s.kind == 2)
    {
        swKhz = s.khz;
        swMode = s.mode;
    }
    freq.setText (kbText (swKhz) + " khz", false);
    lastRx = rx();
    showView (s.kind == 2 ? 1 : (viewChosen ? view : 0));
    startTimerHz (10);
}

void RadioTuner::resized()
{
    auto r = getLocalBounds().toFloat();
    auto top = r.removeFromTop (24.0f);
    tabWeb = top.removeFromLeft (92.0f);
    top.removeFromLeft (4.0f);
    tabSw = top.removeFromLeft (104.0f);
    top.removeFromLeft (4.0f);
    tabOff = top.removeFromLeft (48.0f);
    top.removeFromLeft (12.0f);
    status = top;
    r.removeFromTop (6.0f);
    chips.clear();
    auto chip = [this] (juce::Rectangle<float>& row, const juce::String& label, juce::var what, float pad = 14.0f)
    {
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (mono (10.0f, 500), themed (label)) + pad;
        if (w > row.getWidth())
            return;
        chips.push_back ({ row.removeFromLeft (w), label, what });
        row.removeFromLeft (4.0f);
    };
    if (view == 0)
    {
        auto row = r.removeFromTop (28.0f);
        query.setBounds (row.removeFromLeft (260.0f).toNearestInt());
        row.removeFromLeft (6.0f);
        searchKey = row.removeFromLeft (66.0f);
        row.removeFromLeft (10.0f);
        row = row.reduced (0.0f, 3.0f);
        auto q = [] (const char* k, const char* v) { auto* o = new juce::DynamicObject(); o->setProperty (k, v); return juce::var (o); };
        chip (row, "top", q ("order", "clickcount"));
        chip (row, "random", q ("order", "random"));
        for (auto* tag : { "ambient", "electronic", "jazz", "news", "classical", "experimental", "world" })
            chip (row, tag, q ("tag", tag));
        freq.setVisible (false);
        address.setVisible (false);
        query.setVisible (true);
        refreshKey = {};
    }
    else
    {
        auto row = r.removeFromTop (32.0f);
        freq.setBounds (row.removeFromLeft (170.0f).toNearestInt());
        row.removeFromLeft (8.0f);
        row = row.reduced (0.0f, 4.0f);
        for (double st : { -5.0, -1.0, -0.1, 0.1, 1.0, 5.0 })
            chip (row, (st > 0 ? "+" : "") + juce::String (st, std::abs (st) < 1.0 ? 1 : 0), st, 16.0f);
        row.removeFromLeft (8.0f);
        for (auto* md : { "am", "usb", "lsb", "cw" })
            chip (row, md, juce::String (md), 16.0f);
        row.removeFromLeft (8.0f);
        refreshKey = row.removeFromRight (juce::jmin (row.getWidth(), 76.0f));
        r.removeFromTop (5.0f);
        auto bandRow = r.removeFromTop (20.0f);
        for (auto& b : bands)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("khz", b.khz);
            o->setProperty ("mode", b.mode);
            chip (bandRow, b.name, juce::var (o), 10.0f);
        }
        r.removeFromTop (6.0f);
        address.setBounds (r.removeFromTop (24.0f).removeFromLeft (juce::jmin (r.getWidth(), 380.0f)).toNearestInt());
        freq.setVisible (true);
        address.setVisible (true);
        query.setVisible (false);
        searchKey = {};
    }
    r.removeFromTop (6.0f);
    list.setBounds (r.toNearestInt());
}

void RadioTuner::paint (juce::Graphics& g)
{
    const auto s = source();
    // the tabs
    auto tab = [&] (juce::Rectangle<float> r, const juce::String& t, bool on)
    {
        g.setColour (on ? col::screenLine : col::screenLine.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 5.0f);
        text (g, t, r, mono (11.0f, 600), on ? col::sOrange : col::screenDim, juce::Justification::centred);
    };
    tab (tabWeb, "internet", view == 0);
    tab (tabSw, "shortwave", view == 1);
    tab (tabOff, "off", false);
    // the receiver's state, what it plays
    if (proc.hasRadioNet())
    {
        const auto stt = proc.radioNet().getStatus (rx());
        auto r = status;
        juce::String head = "rx " + juce::String (rx() + 1) + " · "_fr + radioStateText (stt.state);
        const float hw = (float) juce::GlyphArrangement::getStringWidthInt (mono (11.0f, 600), themed (head)) + 12.0f;
        text (g, head, r.removeFromLeft (hw), mono (11.0f, 600), stateColour (stt.state));
        juce::String line = s.kind == 1 ? s.name : (s.kind == 2 ? kbText (s.khz) + " kHz " + s.mode.toUpperCase() + " · "_fr + (s.rxName.isNotEmpty() ? s.rxName : s.host) : juce::String());
        if (stt.title.isNotEmpty())
            line << " · "_fr << stt.title;
        if (stt.msg.isNotEmpty())
            line << " · "_fr << stt.msg;
        if (stt.kbps > 0)
            line << " · "_fr << stt.kbps << " kbps";
        text (g, line, r, mono (11.0f), col::screenDim, juce::Justification::centredLeft, true);
    }
    // the fields' frames
    for (auto* ed : { &query, &freq })
        if (ed->isVisible())
        {
            g.setColour (ed->hasKeyboardFocus (true) ? col::sOrange.withAlpha (0.7f) : col::screenLine.brighter (0.2f));
            g.drawRoundedRectangle (ed->getBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
        }
    if (! searchKey.isEmpty())
    {
        g.setColour (col::screenLine);
        g.fillRoundedRectangle (searchKey, 6.0f);
        text (g, "search", searchKey, mono (11.0f, 600), col::screenText, juce::Justification::centred);
    }
    if (! refreshKey.isEmpty())
    {
        g.setColour (col::screenLine.withAlpha (0.6f));
        g.fillRoundedRectangle (refreshKey, 5.0f);
        text (g, "refresh", refreshKey, mono (10.0f, 500), col::screenDim, juce::Justification::centred);
    }
    for (auto& c : chips)
    {
        bool on = false;
        if (view == 1 && c.what.isString())
            on = c.what.toString() == swMode;
        else if (view == 1 && c.what.isObject())
            on = std::abs ((double) c.what.getProperty ("khz", 0.0) - swKhz) < 0.01;
        g.setColour (on ? col::screenLine : col::screenLine.withAlpha (0.35f));
        g.fillRoundedRectangle (c.r, 4.0f);
        text (g, c.label, c.r, mono (10.0f, 500), on ? col::sOrange : col::screenDim, juce::Justification::centred);
    }
    // the list's state
    const auto lr = list.getBounds().toFloat();
    if (waitingFor >= 0 && getNumRows() == 0)
        text (g, view == 0 ? "recherche des stations…"_fr : "lecture de la liste des récepteurs…"_fr, lr, mono (12.0f), col::screenDim, juce::Justification::centred);
    else if (error.isNotEmpty() && getNumRows() == 0)
        text (g, error, lr, mono (12.0f), col::sRed, juce::Justification::centred, true);
    else if (getNumRows() == 0)
        text (g, view == 0 ? "aucune station trouvée"_fr : "aucun récepteur"_fr, lr, mono (12.0f), col::screenDim, juce::Justification::centred);
}

void RadioTuner::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (tabWeb.contains (p))       { showView (0); return; }
    if (tabSw.contains (p))        { showView (1); return; }
    if (tabOff.contains (p))       { tune (RadioNet::Source {}); return; }
    if (searchKey.contains (p))    { query.onReturnKey(); return; }
    if (refreshKey.contains (p))   { askReceivers (true); return; }
    for (auto& c : chips)
        if (c.r.contains (p))
        {
            if (view == 0)
            {
                query.clear();
                search (c.what);
            }
            else if (c.what.isDouble() || c.what.isInt())
                setFrequency (swKhz + (double) c.what);
            else if (c.what.isString())
                setFrequency (swKhz, c.what.toString());
            else if (c.what.isObject())
                setFrequency ((double) c.what.getProperty ("khz", 9420.0), c.what.getProperty ("mode", "am").toString());
            return;
        }
}

void RadioTuner::timerCallback()
{
    if (! proc.hasRadioNet())
        return;
    auto& net = proc.radioNet();
    for (const auto& a : net.takeAnswers())
    {
        const int id = (int) a.getProperty ("id", -1);
        if (id != waitingFor)
            continue;
        waitingFor = -1;
        const bool ok = (bool) a.getProperty ("ok", false);
        error = ok ? juce::String() : "Pas de réponse : "_fr + a.getProperty ("err", "").toString();
        juce::Array<juce::var> res;
        if (auto* arr = a.getProperty ("res", {}).getArray())
            res = *arr;
        if (a.getProperty ("q", "").toString() == "search")
            stations = res;
        else
            receivers = res;
        list.updateContent();
        list.repaint();
        repaint();
    }
    if (rx() != lastRx)
    {
        lastRx = rx();
        const auto s = source();
        if (s.kind == 2)
        {
            swKhz = s.khz;
            swMode = s.mode;
            freq.setText (kbText (swKhz) + " khz", false);
        }
        showView (s.kind == 2 ? 1 : (s.kind == 1 ? 0 : view));
    }
    if (net.version() != lastVersion)
    {
        lastVersion = net.version();
        list.repaint();
        repaint (status.toNearestInt());
        // (0.4.18) the frequency tuned elsewhere (the big knob, SEEK): the tuner's own figures follow, unless they're
        // being typed
        const auto s = source();
        if (s.kind == 2 && ! freq.hasKeyboardFocus (true) && (std::abs (s.khz - swKhz) > 0.001 || s.mode != swMode))
        {
            swKhz = s.khz;
            swMode = s.mode;
            freq.setText (kbText (swKhz) + " khz", false);
            repaint();
        }
    }
}

int RadioTuner::getNumRows()
{
    return view == 0 ? stations.size() : receivers.size();
}

void RadioTuner::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    const auto s = source();
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    if (view == 0)
    {
        if (row < 0 || row >= stations.size())
            return;
        const auto& st = stations.getReference (row);
        const bool on = s.kind == 1 && s.url == st.getProperty ("url", "").toString();
        if (on || row % 2 == 1)
        {
            g.setColour (on ? col::sOrange.withAlpha (0.16f) : col::screenLine.withAlpha (0.25f));
            g.fillRect (r);
        }
        auto t = r.reduced (8.0f, 0.0f);
        const int kbps = (int) st.getProperty ("kbps", 0);
        text (g, kbps > 0 ? juce::String (kbps) + " kbps" : juce::String(), t.removeFromRight (70.0f), mono (10.0f), col::screenDim, juce::Justification::centredRight);
        text (g, st.getProperty ("cc", "").toString(), t.removeFromLeft (30.0f), mono (10.0f, 600), on ? col::sOrange : col::screenDim);
        const auto name = st.getProperty ("name", "").toString();
        const float nw = juce::jmin (t.getWidth() * 0.6f, (float) juce::GlyphArrangement::getStringWidthInt (mono (12.0f, 500), name) + 14.0f);
        text (g, name, t.removeFromLeft (nw), mono (12.0f, 500), on ? col::sOrange : col::screenText, juce::Justification::centredLeft, true);
        text (g, st.getProperty ("tags", "").toString().replace (",", " · "_fr), t, mono (10.0f), col::screenGhost, juce::Justification::centredLeft, true);
    }
    else
    {
        if (row < 0 || row >= receivers.size())
            return;
        const auto& rc = receivers.getReference (row);
        const bool on = s.kind == 2 && s.host == rc.getProperty ("host", "").toString() && s.port == (int) rc.getProperty ("port", 0);
        if (on || row % 2 == 1)
        {
            g.setColour (on ? col::sOrange.withAlpha (0.16f) : col::screenLine.withAlpha (0.25f));
            g.fillRect (r);
        }
        auto t = r.reduced (8.0f, 0.0f);
        const int users = (int) rc.getProperty ("users", 0), mx = (int) rc.getProperty ("max", 0);
        text (g, juce::String (users) + "/" + juce::String (mx), t.removeFromRight (46.0f), mono (10.0f, 600), users < mx ? col::sGreen : col::sRed,
              juce::Justification::centredRight);
        const auto name = rc.getProperty ("name", "").toString();
        const float nw = juce::jmin (t.getWidth() * 0.62f, (float) juce::GlyphArrangement::getStringWidthInt (mono (12.0f, 500), name) + 14.0f);
        text (g, name, t.removeFromLeft (nw), mono (12.0f, 500), on ? col::sOrange : col::screenText, juce::Justification::centredLeft, true);
        text (g, rc.getProperty ("loc", "").toString(), t, mono (10.0f), col::screenGhost, juce::Justification::centredLeft, true);
    }
}

void RadioTuner::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (view == 0 && row >= 0 && row < stations.size())
    {
        const auto& st = stations.getReference (row);
        RadioNet::Source s;
        s.kind = 1;
        s.name = st.getProperty ("name", "").toString();
        s.url = st.getProperty ("url", "").toString();
        s.uuid = st.getProperty ("uuid", "").toString();
        s.country = st.getProperty ("country", "").toString();
        s.codec = "MP3";
        s.kbps = (int) st.getProperty ("kbps", 0);
        tune (s);
    }
    else if (view == 1 && row >= 0 && row < receivers.size())
    {
        const auto& rc = receivers.getReference (row);
        RadioNet::Source s;
        s.kind = 2;
        s.host = rc.getProperty ("host", "").toString();
        s.port = (int) rc.getProperty ("port", 8073);
        s.rxName = rc.getProperty ("name", "").toString();
        s.khz = swKhz;
        s.mode = swMode;
        const double lo = (double) rc.getProperty ("lo", 0.0), hi = (double) rc.getProperty ("hi", 0.0);
        if (hi > lo && (swKhz < lo * 1000.0 || swKhz > hi * 1000.0))
            message ("Ce récepteur ne descend ou ne monte pas jusqu'à "_fr + kbText (swKhz) + " kHz.", true);
        tune (s);
    }
}
}
