#include "WaveView.h"
#include "Pixel.h"
#include "Text.h"

void WaveView::show (std::shared_ptr<const rk::Sample> s, int i)
{
    if (s == sample && i == index)
        return;
    sample = std::move (s);
    index = i;
    playhead = -1.0f;
    computePeaks();
    repaint();
}

juce::String WaveView::info() const
{
    if (sample == nullptr)
        return {};
    const auto& s = *sample;
    juce::String t = juce::String (rk::noteName (s.root)) + "  V" + juce::String (s.velocity);
    if (s.rr > 0)
        t << "  PRISE " << (s.rr + 1);
    t << "  " << juce::String (s.seconds(), 2) << " S";
    if (s.loop.valid())
        t << "  BOUCLE " << juce::String (s.loop.length() / s.sampleRate, 2) << " S (" << juce::roundToInt (s.loop.quality * 100.0f) << " %)";
    else
        t << "  SANS BOUCLE";
    if (s.pitchConfident)
        t << "  " << (s.detuneCents >= 0 ? "+" : "") << juce::String (s.detuneCents, 1) << " CENTS";
    return t;
}

void WaveView::computePeaks()
{
    auto r = getLocalBounds();
    seam = r.removeFromRight (juce::jmin (170, r.getWidth() / 5));
    r.removeFromRight (8);
    wave = r;
    peaks.clear();
    if (sample == nullptr || sample->length() == 0 || wave.getWidth() <= 0)
        return;
    const int cols = wave.getWidth() / 2;
    const int len = sample->length();
    peaks.resize ((size_t) cols, { 0.0f, 0.0f });
    for (int c = 0; c < cols; ++c)
    {
        const int a = (int) ((int64_t) c * len / cols), b = juce::jmax (a + 1, (int) ((int64_t) (c + 1) * len / cols));
        float lo = 0.0f, hi = 0.0f;
        for (auto& ch : sample->audio)
            for (int i = a; i < b && i < len; ++i)
            {
                lo = juce::jmin (lo, ch[(size_t) i]);
                hi = juce::jmax (hi, ch[(size_t) i]);
            }
        peaks[(size_t) c] = { lo, hi };
    }
}

void WaveView::paint (juce::Graphics& g)
{
    g.setColour (px::black);
    g.fillRect (wave);
    g.fillRect (seam);
    px::frameRect (g, seam, px::frame, 2);
    if (sample == nullptr || peaks.empty())
    {
        px::drawText (g, "CLIQUE UNE ZONE DE LA CARTE", wave, juce::Justification::centred, px::label (11.0f), px::dim);
        px::drawText (g, "RACCORD", seam.reduced (6), juce::Justification::centredTop, px::label (9.0f), px::dim);
        return;
    }
    const auto& s = *sample;
    const int len = s.length();
    auto xOf = [&] (double i) { return wave.getX() + juce::roundToInt (i / (double) len * (double) wave.getWidth()); };

    // the loop, and its blend
    if (s.loop.valid())
    {
        const int x0 = xOf (s.loop.start), x1 = xOf (s.loop.end);
        px::dither (g, { x0, wave.getY(), x1 - x0, wave.getHeight() }, px::yellow.withAlpha (0.35f), 1);
        if (s.loop.crossfade > 0)
        {
            const int xc = xOf (s.loop.end - s.loop.crossfade);
            px::dither (g, { xc, wave.getY(), x1 - xc, wave.getHeight() }, px::yellow.withAlpha (0.5f), 2);
        }
    }
    // the centre line
    g.setColour (px::frame);
    for (int x = wave.getX(); x < wave.getRight(); x += 4)
        g.fillRect (x, wave.getCentreY(), 2, 1);

    // the waveform, 2-pixel columns, scaled so the loudest reaches the edge
    const float peak = juce::jmax (1.0e-4f, s.peak > 0 ? s.peak : rk::peakOf (s.audio));
    const float half = (float) wave.getHeight() * 0.46f;
    const int noteOffX = s.noteOff >= 0 ? xOf (s.noteOff) : wave.getRight();
    for (size_t c = 0; c < peaks.size(); ++c)
    {
        const int x = wave.getX() + (int) c * 2;
        const int y0 = wave.getCentreY() - juce::roundToInt (peaks[c].second / peak * half);
        const int y1 = wave.getCentreY() - juce::roundToInt (peaks[c].first / peak * half);
        g.setColour (x < noteOffX ? px::white : px::grey);
        g.fillRect (x, y0, 2, juce::jmax (2, y1 - y0));
    }

    // markers
    auto marker = [&] (int x, juce::Colour c, const juce::String& t, bool top)
    {
        g.setColour (c);
        g.fillRect (x - 1, wave.getY(), 2, wave.getHeight());
        const int w = juce::GlyphArrangement::getStringWidthInt (px::label (8.0f), t) + 8;
        juce::Rectangle<int> flag (x, top ? wave.getY() : wave.getBottom() - 14, w, 14);
        g.fillRect (flag);
        px::drawText (g, t, flag.reduced (4, 0), juce::Justification::centredLeft, px::label (8.0f), px::black);
    };
    if (s.noteOff >= 0 && s.noteOff < len)
        marker (noteOffX, px::grey, "NOTE OFF", false);
    if (s.loop.valid())
    {
        marker (xOf (s.loop.start), px::yellow, "BOUCLE", true);
        marker (xOf (s.loop.end), px::yellow, "FIN", true);
    }
    if (playhead >= 0.0f && playhead < (float) len)
    {
        g.setColour (px::red);
        g.fillRect (xOf (playhead) - 1, wave.getY(), 3, wave.getHeight());
    }

    // the seam as heard: the end of the loop, then its start
    auto inner = seam.reduced (4);
    px::drawText (g, "RACCORD", inner.removeFromTop (14), juce::Justification::centredTop, px::label (8.0f), px::dim);
    if (s.loop.valid())
    {
        const int span = juce::jlimit (32, 4000, (int) (s.sampleRate * 0.004));   // 4 ms each side
        const auto& ch = s.audio[0];
        const int cols = inner.getWidth() / 2;
        const int mid = inner.getX() + inner.getWidth() / 2;
        g.setColour (px::yellow);
        for (int y = inner.getY(); y < inner.getBottom(); y += 4)
            g.fillRect (mid - 1, y, 2, 2);
        int prevY = -1;
        for (int c = 0; c < cols; ++c)
        {
            const int off = (int) ((int64_t) (c - cols / 2) * span * 2 / juce::jmax (1, cols));
            const int i = off < 0 ? s.loop.end + off : s.loop.start + off;
            if (i < 0 || i >= len)
                continue;
            const int y = inner.getCentreY() - juce::roundToInt (ch[(size_t) i] / peak * (float) inner.getHeight() * 0.45f);
            g.setColour (px::white);
            const int a = prevY < 0 ? y : juce::jmin (prevY, y), b = prevY < 0 ? y : juce::jmax (prevY, y);
            g.fillRect (inner.getX() + c * 2, a, 2, juce::jmax (2, b - a + 1));
            prevY = y;
        }
    }
    else
        px::drawText (g, "PAS DE BOUCLE", inner, juce::Justification::centred, px::label (8.0f), px::faint);
}
