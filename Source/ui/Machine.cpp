#include "Machine.h"
#include "RadioUi.h"
#include "../dsp/Modulators.h"
#include "../dsp/VAFilter.h"

#include <RkNotesData.h>

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace te
{
namespace
{
    juce::String noteText (double v)        { return lower (rk::noteName ((int) v)); }
    juce::String intText (double v)         { return juce::String ((int) std::lround (v)); }
    juce::String pct (double v)             { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }
    juce::String dbText (double v)          { return (v > 0 ? "+" : "") + juce::String (v, 1) + " db"; }
    juce::String semis (double v)           { return juce::String ((int) std::lround (v)) + " dt"; }
    juce::String seconds (double v)         { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, v < 10.0 ? 2 : 1) + " s"; }
    juce::String duration (double s)
    {
        const int t = (int) std::lround (s);
        return t >= 60 ? juce::String (t / 60) + " min " + juce::String (t % 60).paddedLeft ('0', 2) : juce::String (t) + " s";
    }
    juce::String megabytes (int64_t b)
    {
        const double mb = (double) b / 1048576.0;
        if (mb >= 1000.0)
            return juce::String (mb / 1024.0, 1) + " go";
        return (mb < 10.0 ? juce::String (mb, 1) : juce::String (juce::roundToInt (mb))) + " mo";
    }
    bool standalone (const juce::AudioProcessor& p)   { return p.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }
    float P (const ReplikantProcessor& p, int id)      { return p.player.get (id); }

    // where a setting is now, moved by the mods and scenes (its own value when nothing moves it)
    float liveNorm (const ReplikantProcessor& p, int id)
    {
        const float m = p.player.modulatedNorm (id);
        return m >= 0.0f ? m : snd::toNorm (id, p.player.get (id));
    }
    float liveValue (const ReplikantProcessor& p, int id)   { return snd::fromNorm (id, liveNorm (p, id)); }

    void setSound (ReplikantProcessor& p, int id, float value)
    {
        if (auto* param = p.params.getParameter (snd::def (id).id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (snd::legal (id, value)));
            param->endChangeGesture();
        }
    }

    const dsp::CycleBank& bankOf (const ReplikantProcessor& p, int choice)
    {
        auto ins = p.getInstrument();
        if (choice == 1 && ins != nullptr && ins->cycles != nullptr && ins->cycles->count() > 0)
            return *ins->cycles;
        return *dsp::CycleBank::basics();
    }
}

//==============================================================================
// a page's picto
static Picto pagePicto (const Machine::Page& pg)
{
    const auto& v = pg.viz;
    const auto& n = pg.name;
    if (v == "osc")       return n.endsWith ("b") ? Picto::saw : Picto::cycles;
    if (v == "voices")    return Picto::voices;
    if (v == "orbite")    return Picto::orbit;
    if (v == "mix")       return Picto::mixer;
    if (v == "filter")    return Picto::filterBox;
    if (v == "adsr")      return Picto::envelope;
    if (v == "fx")        return Picto::chain;
    if (v.startsWith ("mod")) return Picto::cable;
    if (v == "scenes" || v == "settings") return Picto::sliders;
    if (v == "lecture")   return Picto::playhead;
    if (v == "loop")      return Picto::loop;
    if (v == "pitch")     return Picto::fork;
    // the capture's
    if (n == "source")    return Picto::jack;
    if (n == "notes")     return Picto::keys;
    if (n == "timing")    return Picto::clock;
    if (n == "loops")     return Picto::loop;
    if (n == "attack")    return Picto::onset;
    if (n == "level")     return Picto::vuMeter;
    if (n == "transpose") return Picto::shift;
    if (v == "resample")  return Picto::resample;
    if (v == "cycleplan") return Picto::cycles;
    // RADIO's
    if (v == "tune")      return Picto::antenna;
    if (v == "listen")    return Picto::radio;
    if (v == "air")       return Picto::air;
    if (v == "take")      return Picto::slice;
    if (v == "pads")      return Picto::pads;
    return Picto::none;
}

void Key::paintButton (juce::Graphics& g, bool, bool down)
{
    KeyState st = look ? look() : (getToggleState() ? KeyState::on : KeyState::normal);
    if (! isEnabled())
        st = KeyState::off;
    auto r = getLocalBounds().toFloat();
    key (g, r, st, down && isEnabled());
    auto t = r.withTrimmedBottom (3.0f);
    if (down && isEnabled())
        t.translate (0.0f, 2.0f);
    const auto label = getButtonText();
    const auto f = mono (fontSize, 500);
    if (icon != Picto::none)
    {
        const float side = juce::jmin (18.0f, t.getHeight() - 10.0f);
        const float tw = label.isEmpty() ? 0.0f : (float) juce::GlyphArrangement::getStringWidthInt (f, themed (label)) + 2.0f;
        const float w = side + (label.isEmpty() ? 0.0f : 8.0f) + tw;
        const float x = t.getCentreX() - w * 0.5f;
        picto (g, icon, { x, t.getCentreY() - side * 0.5f, side, side }, keyInk (st), 1.4f);
        if (label.isNotEmpty())
            te::text (g, label, { x + side + 8.0f, t.getY(), tw + 4.0f, t.getHeight() }, f, keyInk (st));
    }
    else if (dot.getAlpha() > 0)
    {
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (f, themed (label)) + 14.0f;
        const float x = t.getCentreX() - w * 0.5f;
        g.setColour (dot);
        g.fillEllipse (x, t.getCentreY() - 4.0f, 8.0f, 8.0f);
        te::text (g, label, { x + 14.0f, t.getY(), w, t.getHeight() }, f, keyInk (st));
    }
    else
        te::text (g, label, t, f, keyInk (st), juce::Justification::centred);
}

//==============================================================================
// the black screen: what the mode shows (the cycles, the sample, the capture's plan) and, on the right, what was
// last touched with a picture of the page
class Screen : public juce::Component
{
public:
    explicit Screen (Machine& mm) : m (mm) { setInterceptsMouseClicks (true, false); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        screen (g, r);
        auto in = r.reduced (16.0f, 12.0f);
        paintTitle (g, in.removeFromTop (22.0f));
        in.removeFromTop (8.0f);
        auto panel = in.removeFromRight (392.0f);
        in.removeFromRight (14.0f);
        tunerShown = false;
        const auto& shownPages = m.pagesShown();
        const auto vizNow = m.pageIndex() >= 0 && m.pageIndex() < (int) shownPages.size() ? shownPages[(size_t) m.pageIndex()].viz : juce::String();
        if (m.capture)
            paintCapture (g, in);
        else if (m.mode == Machine::radioMode && vizNow == "tune")
            {}                                  // (the tuner over it)
        else if (m.mode == Machine::radioMode && ! (vizNow == "fx" && (m.bankOf[juce::String ("fx") + juce::String (m.mode)] == 1 || m.bankOf[juce::String ("fx") + juce::String (m.mode)] == 2)))
            radioScreen.paint (g, in, proc(), m.radioPad());
        else if (vizNow == "orbite")
            paintOrbite (g, in);
        else if (vizNow == "mix")
            paintMixer (g, in);
        else if (vizNow == "pitch" && m.mode == Machine::samplerMode)
            paintTuner (g, in);
        else if (vizNow == "fx" && (m.bankOf[juce::String ("fx") + juce::String (m.mode)] == 1 || m.bankOf[juce::String ("fx") + juce::String (m.mode)] == 2))
            paintSpectre (g, in);
        else if (m.mode == Machine::synthMode)
            paintSynth (g, in);
        else if (m.mode == Machine::samplerMode)
            paintSampler (g, in);
        else
            paintDrum (g, in);
        paintPanel (g, panel);
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (m.message.isNotEmpty() && now < m.messageUntil)
        {
            auto bar = in.removeFromBottom (26.0f);
            g.setColour (col::screen.withAlpha (0.85f));
            g.fillRect (bar);
            text (g, m.message, bar, mono (13.0f), m.messageWarning ? col::sYellow : col::screenText, juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragPos = -1;
        tuning = 0;
        if (tunerShown && (tunerBand.contains (e.position) || tunerFine.contains (e.position)))
        {
            // the dial: dragged sideways (the fine band, or shift: by the cent; alt: whole semitones)
            tuning = tunerFine.contains (e.position) ? 2 : 1;
            tuneFrom = P (proc(), snd::tune);
            if (auto* t = proc().params.getParameter (snd::def (snd::tune).id))
                t->beginChangeGesture();
            return;
        }
        if (fxShown)
            for (int i = 0; i < Player::numStages; ++i)
                if (fxSwitches[i].contains (e.position))
                {
                    toggleStage (proc().player.fxOrder()[(size_t) i]);
                    repaint();
                    return;
                }
        if (fxShown)
            for (int i = 0; i < Player::numStages; ++i)
                if (fxBoxes[i].contains (e.position))
                {
                    dragPos = i;
                    dragX = e.position.x;
                    return;
                }
        editSteps (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (tuning > 0)
        {
            const float perPixel = e.mods.isShiftDown() || tuning == 2 ? 0.01f : 0.12f;
            float v = tuneFrom + (float) e.getDistanceFromDragStartX() * perPixel;
            if (e.mods.isAltDown())
                v = std::round (v);
            setTune (v);
            return;
        }
        if (dragPos >= 0)
        {
            dragX = e.position.x;
            repaint();
            return;
        }
        editSteps (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (tuning > 0)
        {
            tuning = 0;
            if (auto* t = proc().params.getParameter (snd::def (snd::tune).id))
                t->endChangeGesture();
            return;
        }
        if (dragPos < 0)
            return;
        const int from = dragPos;
        dragPos = -1;
        auto order = proc().player.fxOrder();
        if (e.getDistanceFromDragStart() < 4)
        {
            // a click: that effect's bank (the drive lives on the filter's page)
            const int st = order[(size_t) from];
            const int banks[Player::numStages] = { 0, 1, 3, 4, 5, 6 };
            m.bankOf[juce::String ("fx") + juce::String (m.mode)] = banks[st];
            m.focus = nullptr;
            m.layoutEncoders();
            m.repaint();
            return;
        }
        // a drag: the stage moved to where it was let go
        const float w = (fxRow.getWidth() - 5.0f * 10.0f) / 6.0f;
        const int to = juce::jlimit (0, Player::numStages - 1, (int) std::floor ((dragX - fxRow.getX()) / (w + 10.0f)));
        const int st = order[(size_t) from];
        std::vector<int> v (order.begin(), order.end());
        v.erase (v.begin() + from);
        v.insert (v.begin() + to, st);
        std::copy (v.begin(), v.end(), order.begin());
        proc().player.setFxOrder (order);
        proc().settingsChanged();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (tunerShown && (tunerBand.contains (e.position) || tunerFine.contains (e.position)))
            setTune (0.0f);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! (tunerShown && (tunerBand.contains (e.position) || tunerFine.contains (e.position))))
            return;
        // a notch: a semitone (the fine band, or shift: a cent)
        const float d = (std::abs (w.deltaY) > 0.0f ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        const float step = e.mods.isShiftDown() || tunerFine.contains (e.position) ? 0.01f : 1.0f;
        const float now = P (proc(), snd::tune);
        setTune (step >= 1.0f ? std::round (now) + (d > 0.0f ? 1.0f : -1.0f) : now + (d > 0.0f ? step : -step));
    }
    void setTune (float v)
    {
        if (auto* t = dynamic_cast<juce::RangedAudioParameter*> (proc().params.getParameter (snd::def (snd::tune).id)))
            t->setValueNotifyingHost (t->convertTo0to1 (juce::jlimit (-96.0f, 96.0f, v)));
        repaint();
    }

    //------------------------------------------------------------------ SPECTRE: its partials, as they glide (a phosphor trail)
    void paintSpectre (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto& p = proc();
        const bool on = P (p, snd::spOn) > 0.5f;
        const bool frozen = P (p, snd::spFreeze) > 0.5f;
        // this frame's partials, kept with the last ones
        auto& hz = histHz[(size_t) histPos];
        auto& lv = histLv[(size_t) histPos];
        float loudest = 1.0e-9f;
        for (int i = 0; i < Player::maxPeaks; ++i)
        {
            hz[(size_t) i] = on ? p.player.peakHz[(size_t) i].load() : 0.0f;
            lv[(size_t) i] = on ? p.player.peakLevel[(size_t) i].load() : 0.0f;
            if (hz[(size_t) i] > 0.0f && lv[(size_t) i] > 0.0f)
                loudest = juce::jmax (loudest, lv[(size_t) i]);
        }
        histLoud[(size_t) histPos] = loudest;
        histPos = (histPos + 1) % (int) histHz.size();

        // the readout
        auto top = r.removeFromTop (60.0f);
        text (g, "spectre", top.removeFromTop (40.0f), grotesk (32.0f, 300), on ? col::screenText : col::screenDim, juce::Justification::centredLeft);
        const double centre = 40.0 * std::pow (250.0, (double) liveNorm (p, snd::spCentre)) ;
        const double halfOct = 0.5 + 5.0 * (double) liveValue (p, snd::spWidth);
        const double bandLo = centre / std::exp2 (halfOct), bandHi = centre * std::exp2 (halfOct);
        juce::String sub;
        if (! on)
            sub = "off · switch it on in the chain";
        else
            sub << (int) P (p, snd::spVoices) << " voices · band " << juce::String::fromUTF8 (snd::text (snd::cutoff, (float) juce::jmax (20.0, bandLo)).c_str()).toLowerCase()
                << " – " << juce::String::fromUTF8 (snd::text (snd::cutoff, (float) juce::jmin (20000.0, bandHi)).c_str()).toLowerCase() << (frozen ? " · frozen" : "");
        text (g, sub, top, mono (11.0f), on ? col::screenDim : col::sYellow, juce::Justification::centredLeft);

        // the frequency axis (30 Hz .. 16 kHz, log), its decades; the band analysed, shaded
        auto plot = r.reduced (2.0f, 0.0f);
        auto labels = plot.removeFromBottom (14.0f);
        const double fLo = 30.0, fHi = 16000.0;
        auto xOf = [&plot, fLo, fHi] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (juce::jlimit (fLo, fHi, f) / fLo) / std::log (fHi / fLo)); };
        g.setColour (col::sBlue.withAlpha (on ? 0.07f : 0.03f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (bandLo), plot.getY(), xOf (bandHi), plot.getBottom()));
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            g.setColour (col::screenLine);
            g.fillRect (xOf (f), plot.getY(), 1.0f, plot.getHeight());
            text (g, f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), { xOf (f) - 20.0f, labels.getY(), 40.0f, labels.getHeight() },
                  mono (9.5f), col::screenDim, juce::Justification::centred);
        }
        g.setColour (col::screenLine);
        g.fillRect (plot.getX(), plot.getBottom() - 1.0f, plot.getWidth(), 1.0f);
        auto heightOf = [&plot] (float level, float loud)
        {
            const float d = 20.0f * std::log10 (juce::jmax (1.0e-6f, level / loud));          // 0 .. -48 dB
            return plot.getHeight() * 0.92f * juce::jlimit (0.0f, 1.0f, 1.0f + d / 48.0f);
        };
        // the trail: the older frames thinner and fainter, the newest last
        const int frames = (int) histHz.size();
        for (int k = 1; k < frames; ++k)
        {
            const int f = (histPos + k - 1) % frames;                   // oldest first
            const float age = (float) k / (float) frames;
            for (int i = 0; i < Player::maxPeaks; ++i)
            {
                const float h = histHz[(size_t) f][(size_t) i], l = histLv[(size_t) f][(size_t) i];
                if (h <= 0.0f || l <= 0.0f)
                    continue;
                const float x = xOf (h), y = plot.getBottom() - heightOf (l, histLoud[(size_t) f]);
                g.setColour (col::sBlue.withAlpha (0.05f + 0.22f * age * age));
                g.fillRect (x - 0.5f, y, 1.0f, plot.getBottom() - y);
            }
        }
        // now: each partial a lit line, a glow round it, a bead on top
        const auto& nowHz = histHz[(size_t) ((histPos + frames - 1) % frames)];
        const auto& nowLv = histLv[(size_t) ((histPos + frames - 1) % frames)];
        for (int i = 0; i < Player::maxPeaks; ++i)
        {
            const float h = nowHz[(size_t) i], l = nowLv[(size_t) i];
            if (h <= 0.0f || l <= 0.0f)
                continue;
            const float x = xOf (h), y = plot.getBottom() - heightOf (l, loudest);
            if (plot.getBottom() - y < 2.0f)
                continue;                                               // (one fading out, under -48 dB)
            g.setGradientFill (juce::ColourGradient (col::sBlue.withAlpha (0.35f), x, y, col::sBlue.withAlpha (0.0f), x, plot.getBottom(), false));
            g.fillRect (x - 3.0f, y, 6.0f, plot.getBottom() - y);
            g.setColour (frozen ? col::sViolet : col::screenText);
            g.fillRect (x - 0.75f, y, 1.5f, plot.getBottom() - y);
            g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }
    }

    //------------------------------------------------------------------ SAMPLER: the tuning dial (-96..+96 semitones)
    void paintTuner (juce::Graphics& g, juce::Rectangle<float> r)
    {
        tunerShown = true;
        const float v = liveValue (proc(), snd::tune);
        // the readout: the value, and what it makes in octaves, semitones, cents
        auto top = r.removeFromTop (64.0f);
        text (g, juce::String::fromUTF8 (snd::text (snd::tune, v).c_str()), top.removeFromTop (44.0f), grotesk (40.0f, 300), col::screenText,
              juce::Justification::centredLeft);
        {
            const float a = std::abs (v);
            const int oct = (int) (a / 12.0f), semi = (int) std::fmod (a, 12.0f);
            const int cents = juce::roundToInt ((a - std::floor (a)) * 100.0f);
            juce::String line = juce::String (v < 0.0f ? "-" : "+") + juce::String (oct) + " oct " + juce::String (semi) + " st " + juce::String (cents) + " ct"
                              + " · x" + juce::String (std::exp2 (v / 12.0f), v > 24.0f ? 1 : 3);
            if (proc().player.modulatedNorm (snd::tune) >= 0.0f)
                line << " · modulated";
            text (g, line, top, mono (11.0f), col::screenDim, juce::Justification::centredLeft);
        }
        text (g, "drag · shift or lower band: fine · alt: semitones · double-click: 0"_fr, r.removeFromBottom (16.0f), mono (10.0f), col::screenGhost,
              juce::Justification::centredRight);
        r.removeFromBottom (6.0f);

        // the band: -96..+96, a tick a semitone, a long one an octave (its number), the needle
        tunerFine = r.removeFromBottom (44.0f);
        r.removeFromBottom (10.0f);
        tunerBand = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 92.0f));
        auto band = tunerBand;
        g.setColour (col::screenLine.withAlpha (0.45f));
        g.fillRoundedRectangle (band, 8.0f);
        g.setColour (col::screenLine);
        g.drawRoundedRectangle (band, 8.0f, 1.0f);
        auto scale = band.reduced (18.0f, 10.0f);
        auto xOf = [&scale] (float st) { return scale.getX() + scale.getWidth() * (st + 96.0f) / 192.0f; };
        for (int st = -96; st <= 96; ++st)
        {
            const bool octave = st % 12 == 0, half = st % 6 == 0;
            const float x = xOf ((float) st), h = octave ? scale.getHeight() * 0.55f : (half ? scale.getHeight() * 0.32f : scale.getHeight() * 0.18f);
            g.setColour (octave ? col::screenText.withAlpha (0.8f) : col::screenDim.withAlpha (half ? 0.7f : 0.45f));
            g.fillRect (x - (octave ? 0.75f : 0.5f), scale.getBottom() - h, octave ? 1.5f : 1.0f, h);
            if (octave)
                text (g, (st > 0 ? "+" : "") + juce::String (st / 12), { x - 16.0f, scale.getY() - 2.0f, 32.0f, 14.0f }, mono (11.0f, st == 0 ? 700 : 500),
                      st == 0 ? col::screenText : col::screenDim, juce::Justification::centred);
        }
        text (g, "oct", { scale.getRight() - 30.0f, scale.getY() + 12.0f, 30.0f, 12.0f }, mono (9.0f), col::screenGhost, juce::Justification::centredRight);
        {
            const float x = xOf (v);
            g.setColour (col::sRed.withAlpha (0.25f));
            g.fillRect (x - 4.0f, band.getY() + 4.0f, 8.0f, band.getHeight() - 8.0f);
            g.setColour (col::sRed);
            g.fillRect (x - 1.0f, band.getY() + 4.0f, 2.0f, band.getHeight() - 8.0f);
        }

        // the fine band: a semitone each side of where it is, a tick every 5 cents; the needle stays, the band moves
        g.setColour (col::screenLine.withAlpha (0.3f));
        g.fillRoundedRectangle (tunerFine, 6.0f);
        g.setColour (col::screenLine);
        g.drawRoundedRectangle (tunerFine, 6.0f, 1.0f);
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (tunerFine.toNearestInt());
            const float cx = tunerFine.getCentreX(), pxPerCent = tunerFine.getWidth() / 240.0f;
            const int c0 = (int) std::floor ((v - 1.3f) * 20.0f), c1 = (int) std::ceil ((v + 1.3f) * 20.0f);
            for (int k = c0; k <= c1; ++k)                          // k: in steps of 5 cents
            {
                const float st = (float) k / 20.0f;
                const float x = cx + (st - v) * 100.0f * pxPerCent;
                const bool whole = k % 20 == 0, fifty = k % 10 == 0;
                const float h = whole ? tunerFine.getHeight() * 0.6f : (fifty ? tunerFine.getHeight() * 0.4f : tunerFine.getHeight() * 0.22f);
                g.setColour (whole ? col::screenText.withAlpha (0.85f) : col::screenDim.withAlpha (fifty ? 0.8f : 0.5f));
                g.fillRect (x - 0.5f, tunerFine.getBottom() - 4.0f - h, whole ? 1.5f : 1.0f, h);
                if (whole)
                    text (g, (k > 0 ? "+" : "") + juce::String (k / 20), { x - 18.0f, tunerFine.getY() + 2.0f, 36.0f, 12.0f }, mono (10.0f, 600), col::screenText,
                          juce::Justification::centred);
                else if (fifty)
                    text (g, "50", { x - 14.0f, tunerFine.getY() + 2.0f, 28.0f, 12.0f }, mono (9.0f), col::screenDim, juce::Justification::centred);
            }
            g.setColour (col::sRed);
            g.fillRect (cx - 1.0f, tunerFine.getY() + 2.0f, 2.0f, tunerFine.getHeight() - 4.0f);
        }
    }

    // an effect on, or passed by (the drive back on its last model)
    void toggleStage (int st)
    {
        auto& pr = proc();
        if (st == 0)
        {
            const int model = (int) P (pr, snd::dModel);
            if (model > 0)
                lastDriveModel = model;
            setSound (pr, snd::dModel, model > 0 ? 0.0f : (float) lastDriveModel);
            return;
        }
        const int ids[Player::numStages] = { -1, snd::spOn, snd::piOn, snd::chOn, snd::crOn, snd::rvOn };
        setSound (pr, ids[st], P (pr, ids[st]) > 0.5f ? 0.0f : 1.0f);
    }

private:
    Machine& m;
    RadioScreen radioScreen;
    int lastDriveModel = 1;
    juce::Rectangle<float> fxBoxes[Player::numStages], fxSwitches[Player::numStages], fxRow;
    bool fxShown = false;
    int dragPos = -1;
    float dragX = 0.0f;
    // SPECTRE's last frames of partials (a trail of 24, a screen tick each)
    std::array<std::array<float, Player::maxPeaks>, 24> histHz {}, histLv {};
    std::array<float, 24> histLoud {};
    int histPos = 0;
    // the tuning dial: where it was drawn, being turned (1 the band, 2 the fine band), from where
    bool tunerShown = false;
    juce::Rectangle<float> tunerBand, tunerFine;
    int tuning = 0;
    float tuneFrom = 0.0f;
    juce::Rectangle<float> stepsArea;
    int stepsMod = -1, stepsLen = 0;
    const rk::Sample* cachedSample = nullptr;
    juce::Path cachedWave;
    float cachedW = 0.0f, cachedH = 0.0f;
    int lastSample = 0;

    ReplikantProcessor& proc() const     { return m.proc; }

    void paintTitle (juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::String tag, title, sub;
        auto ins = proc().getInstrument();
        if (m.capture)
        {
            tag = proc().isSampling() ? "capture en cours" : "capture";
            title = proc().instrumentName;
            sub = m.status.sampling ? m.status.text : juce::String ("notes : un multisample de ton synthé, joué dans le sampler"_fr);
            if (m.captureKind == Machine::cyclesKind && ! m.status.sampling)
                sub = "cycles : une banque pour les oscillateurs du synthé"_fr;
            if (m.captureKind == Machine::resampleKind)
            {
                tag = proc().player.resampleState() == Player::Resample::recording ? "resampling" : "resample";
                sub = "ce qui sort des effets, injecté dans le sampler"_fr;
            }
        }
        else if (m.mode == Machine::synthMode)
        {
            tag = "synth";
            title = proc().instrumentName;
            const int bA = (int) P (proc(), snd::oaBank), bB = (int) P (proc(), snd::obBank);
            sub << "osc a " << (bA == 1 ? "captured" : "basic") << " · osc b "_fr << (bB == 1 ? "captured" : "basic");
            sub = juce::String::fromUTF8 (sub.toRawUTF8());
        }
        else if (m.mode == Machine::samplerMode)
        {
            tag = "sampler";
            title = proc().instrumentName;
            if (ins != nullptr && ! ins->samples.empty())
            {
                const int ns = (int) ins->samples.size(), nk = (int) ins->keys.size();
                sub << ns << (ns > 1 ? " samples · "_fr : " sample · "_fr)
                    << (nk > 0 ? juce::String (nk) + " keys rendered" : juce::String ("played live across the keys"));
            }
            else
                sub = "aucun son : glisse un wav, ou capture ton synthé"_fr;
        }
        else if (m.mode == Machine::radioMode)
        {
            tag = "radio";
            const int rx = juce::jlimit (0, radio::numRx - 1, (int) P (proc(), snd::rdRx));
            title = proc().radioStationName (rx);
            if (title.isEmpty())
                title = "rx " + juce::String (rx + 1);
            if (proc().hasRadioNet())
            {
                const auto st = proc().radioNet().getStatus (rx);
                sub = radioStateText (st.state);
                if (st.title.isNotEmpty())
                    sub << " · "_fr << st.title;
                else if (st.msg.isNotEmpty())
                    sub << " · "_fr << st.msg;
            }
        }
        else
        {
            tag = "drum";
            sub = "en 0.4";
        }
        if (proc().options.monitor == 2)
            sub = "input thru fx · "_fr + sub;
        auto f = mono (12.0f, 600);
        const Picto icon = m.capture ? (proc().isSampling() ? Picto::record : (m.captureKind == Machine::resampleKind ? Picto::resample : Picto::jack))
                         : (m.mode == Machine::synthMode ? Picto::sine : (m.mode == Machine::samplerMode ? Picto::cassette
                                                                         : (m.mode == Machine::radioMode ? Picto::radio : Picto::drum)));
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (f, themed (tag)) + 14.0f + 20.0f;
        auto t = r.removeFromLeft (tw).reduced (0.0f, 1.0f);
        g.setColour (proc().isSampling() || proc().player.resampleState() == Player::Resample::recording
                     || (m.mode == Machine::radioMode && ! m.capture && proc().radio.recording()) ? col::sRed : col::sOrange);
        g.fillRoundedRectangle (t, 4.0f);
        auto ti = t.reduced (7.0f, 0.0f);
        picto (g, icon, ti.removeFromLeft (15.0f).withSizeKeepingCentre (15.0f, 15.0f), col::screen, 1.4f);
        text (g, tag, ti.withTrimmedLeft (5.0f), f, col::screen, juce::Justification::centredLeft);
        r.removeFromLeft (10.0f);
        // the page and its banks, on the right
        auto& pages = m.pagesShown();
        const int pi = m.pageIndex();
        if (pi >= 0 && pi < (int) pages.size())
        {
            const auto& pg = pages[(size_t) pi];
            const int bank = m.bankOf[pg.name + juce::String (m.capture ? 9 : m.mode)];
            for (int b = (int) pg.banks.size() - 1; b >= 0; --b)
            {
                const auto name = pg.banks.size() > 1 ? pg.banks[(size_t) b].name : pg.name;
                const float w = (float) juce::GlyphArrangement::getStringWidthInt (mono (12.0f, 500), themed (name)) + 14.0f;
                auto cell = r.removeFromRight (w);
                if (b == bank)
                {
                    g.setColour (col::screenLine);
                    g.fillRoundedRectangle (cell.reduced (0.0f, 1.0f), 4.0f);
                }
                text (g, name, cell, mono (12.0f, 500), b == bank ? col::sOrange : col::screenDim, juce::Justification::centred);
                r.removeFromRight (2.0f);
            }
        }
        r.removeFromRight (12.0f);
        const float titleW = juce::jmin (r.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (mono (14.0f, 500), title) + 14.0f);
        text (g, title, r.removeFromLeft (titleW), mono (14.0f, 500), col::screenText, juce::Justification::centredLeft, true);
        text (g, sub, r, mono (12.0f), col::screenDim);
    }

    //------------------------------------------------------------------ SYNTHÉ: the bank, its cycles in a pile
    void paintSynth (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const bool b = m.pageIndex() == 1;          // OSC B's page: its bank
        const int bankId = b ? snd::obBank : snd::oaBank, posId = b ? snd::obPos : snd::oaPos;
        const auto& bank = bankOf (proc(), (int) P (proc(), bankId));
        const float pos = liveNorm (proc(), posId);
        const int frames = 14;
        const float W = r.getWidth() - frames * 14.0f - 40.0f, A = r.getHeight() * 0.13f;
        auto drawCycle = [&] (float p, float ox, float oy, juce::Colour c, float thick)
        {
            juce::Path path;
            const int pts = 160;
            for (int i = 0; i <= pts; ++i)
            {
                const float x = ox + W * (float) i / pts;
                const float y = oy - A * bank.read (p, (double) i / pts, 6);
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (thick, juce::PathStrokeType::curved));
        };
        const float baseY = r.getBottom() - A - 12.0f;
        for (int f = frames - 1; f >= 0; --f)
            drawCycle ((float) f / (frames - 1), r.getX() + 8.0f + f * 14.0f, baseY - f * 10.0f, col::screenGhost, 1.2f);
        const float fp = pos * (frames - 1);
        drawCycle (pos, r.getX() + 8.0f + fp * 14.0f, baseY - fp * 10.0f, b ? col::sGreen : col::sBlue, 3.0f);
        // the readout
        const int n = bank.count();
        auto top = r.removeFromTop (60.0f).removeFromRight (300.0f);
        text (g, juce::String (juce::roundToInt (pos * (float) (n - 1)) + 1) + " / " + juce::String (n), top.removeFromTop (40.0f),
              grotesk (32.0f, 300), col::screenText, juce::Justification::centredRight, true);
        juce::String sub = (b ? "osc b" : "osc a") + juce::String (" · position "_fr) + juce::String (juce::roundToInt (pos * 100.0f)) + " %";
        if (proc().player.modulatedNorm (posId) >= 0.0f)
            sub << " · modulated";
        text (g, sub, top, mono (11.0f), col::screenDim, juce::Justification::centredRight);
    }

    //------------------------------------------------------------------ ORBITE: its sound over two cycles, its orbits
    dsp::OrbiteSettings orbiteSettings() const
    {
        auto& p = proc();
        dsp::OrbiteSettings s;
        s.algo = (int) P (p, snd::orAlgo);
        s.shape = liveValue (p, snd::orShape);
        s.sym = liveValue (p, snd::orSym);
        s.wave[0] = (int) P (p, snd::s1Wave);
        s.wave[1] = (int) P (p, snd::s2Wave);
        s.symS[0] = liveValue (p, snd::s1Sym);
        s.symS[1] = liveValue (p, snd::s2Sym);
        s.ring[0] = liveValue (p, snd::s1Ring);
        s.ring[1] = liveValue (p, snd::s2Ring);
        s.sync[0] = P (p, snd::s1Sync) > 0.5f;
        s.sync[1] = P (p, snd::s2Sync) > 0.5f;
        s.phase2 = liveValue (p, snd::s2Phase);
        return s;
    }
    // where satellite 2 has circled to (for the screen: from the clock, at ROTATION)
    double orbiteTurn() const
    {
        return juce::Time::getMillisecondCounterHiRes() * 0.001 * (double) liveValue (proc(), snd::orOrbit);
    }
    // ORBITE's sound at x carrier cycles (the screen's own, without the anti-aliasing): its three parts too
    static float orbiteAt (const dsp::OrbiteSettings& s, double r1, double r2, double turn, double x, float* parts = nullptr)
    {
        const double pc = x - std::floor (x);
        auto frac = [] (double v) { return v - std::floor (v); };
        const double p1 = frac (s.sync[0] ? pc * r1 : x * r1);
        const double p2 = frac ((double) s.phase2 + turn + (s.sync[1] ? pc * r2 : x * r2));
        const float a = dsp::Orbite::satelliteWave (p1, s.wave[0], s.symS[0]);
        const float b = dsp::Orbite::satelliteWave (p2, s.wave[1], s.symS[1]);
        const double pcEff = s.algo == 2 ? frac (pc + 1.5 * (double) s.ring[0] * (double) a) : pc;
        const float c = dsp::Orbite::carrierWave (pcEff, s.shape, s.sym);
        auto ring = [] (float v, float k) { return (1.0f - k) + k * v; };
        if (parts != nullptr)
        {
            parts[0] = c;
            parts[1] = a;
            parts[2] = b;
        }
        switch (s.algo)
        {
            case 1:  return c * ring (a * ring (b, s.ring[1]), s.ring[0]);
            case 2:  return c * ring (b, s.ring[1]);
            default: return c * ring (a, s.ring[0]) * ring (b, s.ring[1]);
        }
    }

    void paintOrbite (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto& p = proc();
        const bool on = P (p, snd::orOn) > 0.5f;
        const auto s = orbiteSettings();
        const double r1 = liveValue (p, snd::s1Ratio), r2 = liveValue (p, snd::s2Ratio), turn = orbiteTurn();
        const juce::Colour cC = on ? col::sOrange : col::screenGhost, c1 = on ? col::sBlue : col::screenGhost, c2 = on ? col::sViolet : col::screenGhost;
        static const char* algos[] = { "parallel", "cascade", "fm", "loop" };

        // the orbits, on the right: the carrier, satellite 1 on the inner orbit, satellite 2 circling on the outer
        auto diag = r.removeFromRight (juce::jmin (r.getWidth() * 0.36f, r.getHeight() * 1.5f));
        r.removeFromRight (12.0f);
        {
            const auto c = diag.getCentre().translated (0.0f, 10.0f);
            const float R = juce::jmin (diag.getWidth() * 0.46f, diag.getHeight() * 0.62f);
            auto orbitAt = [&] (float k, float angle) { return juce::Point<float> (c.x + std::cos (angle) * R * k, c.y + std::sin (angle) * R * k * 0.55f); };
            auto ellipse = [&] (float k, juce::Colour col, bool dashed)
            {
                juce::Path o;
                o.addEllipse (c.x - R * k, c.y - R * k * 0.55f, 2.0f * R * k, 2.0f * R * k * 0.55f);
                g.setColour (col.withMultipliedAlpha (0.5f));
                if (dashed)
                {
                    const float dash[] = { 4.0f, 5.0f };
                    juce::PathStrokeType (1.2f).createDashedStroke (o, o, dash, 2);
                    g.fillPath (o);
                }
                else
                    g.strokePath (o, juce::PathStrokeType (1.2f));
            };
            ellipse (0.55f, c1, ! s.sync[0]);
            ellipse (1.0f, c2, ! s.sync[1]);
            const auto pos1 = orbitAt (0.55f, -2.4f);
            const auto pos2 = orbitAt (1.0f, (float) (juce::MathConstants<double>::twoPi * ((double) s.phase2 + turn)) - 0.6f);
            // the wiring
            auto link = [&] (juce::Point<float> a, juce::Point<float> b, juce::Colour col, bool dashed)
            {
                juce::Path l;
                l.startNewSubPath (a);
                l.lineTo (b);
                g.setColour (col.withMultipliedAlpha (0.8f));
                if (dashed)
                {
                    const float dash[] = { 3.0f, 3.0f };
                    juce::PathStrokeType (1.6f).createDashedStroke (l, l, dash, 2);
                    g.fillPath (l);
                }
                else
                    g.strokePath (l, juce::PathStrokeType (1.6f));
            };
            if (s.algo == 1)
            {
                link (pos2, pos1, c2, false);
                link (pos1, c, c1, false);
            }
            else
            {
                link (pos1, c, c1, s.algo == 2);
                link (pos2, c, c2, false);
            }
            if (s.algo == 3)
            {
                // the output back into the satellites
                juce::Path back;
                back.addCentredArc (c.x, c.y, R * 1.12f, R * 0.72f, 0.0f, 0.4f, 2.6f, true);
                g.setColour (cC.withMultipliedAlpha (0.7f));
                g.strokePath (back, juce::PathStrokeType (1.4f));
            }
            g.setColour (cC);
            g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (c));
            const float d1 = 8.0f + 10.0f * s.ring[0], d2 = 8.0f + 10.0f * s.ring[1];
            g.setColour (c1);
            g.fillEllipse (juce::Rectangle<float> (d1, d1).withCentre (pos1));
            g.setColour (c2);
            g.fillEllipse (juce::Rectangle<float> (d2, d2).withCentre (pos2));
            text (g, "1", juce::Rectangle<float> (14.0f, 14.0f).withCentre (pos1.translated (0.0f, -d1)), mono (11.0f, 500), c1, juce::Justification::centred);
            text (g, "2", juce::Rectangle<float> (14.0f, 14.0f).withCentre (pos2.translated (0.0f, -d2)), mono (11.0f, 500), c2, juce::Justification::centred);
            if (s.algo == 2)
                text (g, "fm", juce::Rectangle<float> (24.0f, 14.0f).withCentre ((pos1 + c) * 0.5f).translated (0.0f, -10.0f), mono (10.0f), c1, juce::Justification::centred);
        }

        // the readout
        auto top = r.removeFromTop (60.0f);
        text (g, juce::String::fromUTF8 (algos[juce::jlimit (0, 3, s.algo)]), top.removeFromTop (40.0f), grotesk (32.0f, 300),
              on ? col::screenText : col::screenDim, juce::Justification::centredLeft);
        juce::String sub;
        if (! on)
            sub = "off · switch it on: carrier or mixer";
        else
        {
            sub << "sat 1 x" << juce::String (r1, 2) << " · sat 2 x"_fr << juce::String (r2, 2) << " · rotation "_fr
                << juce::String::fromUTF8 (snd::text (snd::orOrbit, liveValue (p, snd::orOrbit)).c_str()).toLowerCase();
        }
        text (g, sub, top, mono (11.0f), on ? col::screenDim : col::sYellow, juce::Justification::centredLeft);

        // two cycles of its sound, the carrier's ghost behind
        auto w = r.reduced (4.0f, 10.0f);
        const int pts = 360;
        juce::Path out, carrier;
        for (int i = 0; i <= pts; ++i)
        {
            const double x = 2.0 * (double) i / pts;
            float parts[3];
            const float y = orbiteAt (s, r1, r2, turn, x, parts);
            const float px = w.getX() + w.getWidth() * (float) i / pts;
            const auto py = w.getCentreY() - w.getHeight() * 0.45f * juce::jlimit (-1.0f, 1.0f, y);
            const auto pc = w.getCentreY() - w.getHeight() * 0.45f * parts[0];
            if (i == 0) { out.startNewSubPath (px, py); carrier.startNewSubPath (px, pc); }
            else { out.lineTo (px, py); carrier.lineTo (px, pc); }
        }
        g.setColour (col::screenLine);
        g.drawHorizontalLine ((int) w.getCentreY(), w.getX(), w.getRight());
        g.setColour (col::screenGhost);
        g.strokePath (carrier, juce::PathStrokeType (1.2f));
        g.setColour (cC);
        g.strokePath (out, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
    }

    //------------------------------------------------------------------ the MIXEUR: a fader per source, into the filter
    void paintMixer (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto& p = proc();
        const bool sampler = m.mode == Machine::samplerMode;
        const bool orbOn = P (p, snd::orOn) > 0.5f;
        struct Ch { const char* name; int id; juce::Colour c; bool live; };
        std::vector<Ch> chs;
        if (sampler)
            chs = { { "sample", snd::mxSample, col::sBlue, true }, { "orbite", snd::mxOrbite, col::sOrange, orbOn },
                    { "sub", snd::mxSub, col::sGreen, P (p, snd::subOn) > 0.5f } };
        else
            chs = { { "osc a", snd::oaLevel, col::sBlue, true }, { "osc b", snd::obLevel, col::sGreen, true },
                    { "sub", snd::subLevel, col::sYellow, true }, { "noise", snd::noiseLevel, col::screenText, true },
                    { "orbite", snd::mxOrbite, col::sOrange, orbOn } };
        // the filter it all goes into, on the right
        auto into = r.removeFromRight (150.0f);
        {
            const int model = (int) P (p, snd::fModel);
            picto (g, filterModelPicto (model), into.withSizeKeepingCentre (64.0f, 64.0f).translated (20.0f, 0.0f), model > 0 ? col::sBlue : col::screenDim, 1.6f);
            text (g, model > 0 ? "filter" : "filter off", into.withSizeKeepingCentre (150.0f, 20.0f).translated (20.0f, 48.0f), mono (11.0f), col::screenDim,
                  juce::Justification::centred);
            juce::Path arrow;
            const float y = into.getCentreY();
            arrow.startNewSubPath (into.getX() - 6.0f, y);
            arrow.lineTo (into.getX() + 40.0f, y);
            g.setColour (col::screenDim);
            g.strokePath (arrow, juce::PathStrokeType (1.5f));
            juce::Path head;
            head.addTriangle (into.getX() + 46.0f, y, into.getX() + 38.0f, y - 5.0f, into.getX() + 38.0f, y + 5.0f);
            g.fillPath (head);
        }
        const float w = juce::jmin (120.0f, r.getWidth() / (float) chs.size());
        for (size_t i = 0; i < chs.size(); ++i)
        {
            const auto& ch = chs[i];
            auto strip = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (10.0f, 0.0f);
            const float level = liveNorm (p, ch.id);
            const auto c = ch.live ? ch.c : col::screenGhost;
            text (g, ch.live ? juce::String (juce::roundToInt (level * 100.0f)) + " %" : juce::String ("off"), strip.removeFromTop (20.0f), mono (12.0f, 500),
                  ch.live ? col::screenText : col::screenDim, juce::Justification::centred);
            text (g, juce::String::fromUTF8 (ch.name), strip.removeFromBottom (20.0f), mono (11.0f, 500), c, juce::Justification::centred);
            auto track = strip.reduced (0.0f, 8.0f);
            const auto rail = track.withSizeKeepingCentre (6.0f, track.getHeight());
            g.setColour (col::screenLine);
            g.fillRoundedRectangle (rail, 3.0f);
            const float y = track.getBottom() - level * track.getHeight();
            g.setColour (c.withMultipliedAlpha (0.35f));
            g.fillRoundedRectangle (rail.withTop (y), 3.0f);
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (46.0f, strip.getWidth()), 12.0f).withCentre ({ rail.getCentreX(), y }), 3.0f);
        }
    }

    //------------------------------------------------------------------ SAMPLER: the sample, its start, its loop
    void paintSampler (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto ins = proc().getInstrument();
        if (ins == nullptr || ins->samples.empty())
        {
            text (g, "glisse un son ici (wav, aiff, flac) pour le jouer", r.withTrimmedBottom (r.getHeight() * 0.5f), grotesk (24.0f, 300),
                  col::screenText, juce::Justification::centredBottom);
            text (g, "ou capture ton synthé : la touche capture, en haut"_fr, r.withTrimmedTop (r.getHeight() * 0.5f + 10.0f), mono (13.0f),
                  col::screenDim, juce::Justification::centredTop);
            return;
        }
        const int playing = proc().player.shownSample.load();
        if (playing >= 0 && playing < (int) ins->samples.size())
            lastSample = playing;
        lastSample = juce::jlimit (0, (int) ins->samples.size() - 1, lastSample);
        const auto& s = ins->samples[(size_t) lastSample];
        auto info = r.removeFromBottom (18.0f);
        auto w = r.withTrimmedTop (16.0f);
        const int len = s.length();
        if (len <= 0)
            return;
        // the waveform (made once per sample and size)
        if (cachedSample != &s || std::abs (cachedW - w.getWidth()) > 0.5f || std::abs (cachedH - w.getHeight()) > 0.5f)
        {
            cachedSample = &s;
            cachedW = w.getWidth();
            cachedH = w.getHeight();
            cachedWave.clear();
            const int cols = (int) w.getWidth();
            const auto& d = s.audio[0];
            const float mid = w.getHeight() * 0.5f;
            std::vector<float> lo ((size_t) cols), hi ((size_t) cols);
            for (int x = 0; x < cols; ++x)
            {
                const int a = (int) ((int64_t) x * len / cols), b = juce::jmax (a + 1, (int) ((int64_t) (x + 1) * len / cols));
                float mn = 0.0f, mx = 0.0f;
                for (int i = a; i < b && i < len; ++i)
                {
                    mn = juce::jmin (mn, d[(size_t) i]);
                    mx = juce::jmax (mx, d[(size_t) i]);
                }
                lo[(size_t) x] = mn;
                hi[(size_t) x] = mx;
            }
            cachedWave.startNewSubPath (0.0f, mid - hi[0] * mid);
            for (int x = 1; x < cols; ++x)
                cachedWave.lineTo ((float) x, mid - hi[(size_t) x] * mid * 0.96f);
            for (int x = cols - 1; x >= 0; --x)
                cachedWave.lineTo ((float) x, mid - lo[(size_t) x] * mid * 0.96f);
            cachedWave.closeSubPath();
        }
        auto X = [&] (double frame) { return w.getX() + (float) (frame / (double) len) * w.getWidth(); };
        // the loop as moved, its crossfade
        int ls = -1, le = -1;
        if (s.loop.valid() && P (proc(), snd::loops) > 0.5f)
        {
            ls = s.loop.start;
            le = s.loop.end;
            const float size = liveValue (proc(), snd::loopSize), shift = liveValue (proc(), snd::loopStart);
            if (size < 0.999f || std::abs (shift) > 0.001f)
            {
                const int L = juce::jlimit (64, juce::jmax (64, len - 1), (int) std::lround (s.loop.length() * size));
                const int room = juce::jmax (0, len - 1 - L);
                ls = juce::jlimit (0, room, s.loop.start + (int) std::lround (shift > 0 ? shift * (room - s.loop.start) : shift * s.loop.start));
                le = ls + L;
            }
            g.setColour (col::sGreen.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (X (ls), w.getY(), X (le), w.getBottom()));
            const double xf = juce::jmin ((double) P (proc(), snd::loopXfade) * 0.5 * (le - ls), (double) ls);
            if (xf > 1.0)
            {
                juce::Path ramp;
                ramp.addTriangle (X (le - xf), w.getY(), X (le), w.getY(), X (le), w.getBottom());
                g.setColour (col::sGreen.withAlpha (0.35f));
                g.fillPath (ramp);
            }
        }
        g.setColour (col::screenLine);
        g.drawHorizontalLine ((int) w.getCentreY(), w.getX(), w.getRight());
        g.setColour (col::screenText);
        g.fillPath (cachedWave, juce::AffineTransform::translation (w.getX(), w.getY()));
        auto line = [&] (float x, juce::Colour c, float t) { g.setColour (c); g.fillRect (x - t * 0.5f, w.getY(), t, w.getHeight()); };
        const double start = (double) liveValue (proc(), snd::start) * (len - 1);
        line (X (start), col::sOrange, 2.5f);
        text (g, "start", { X (start) + 5.0f, r.getY(), 60.0f, 14.0f }, mono (11.0f), col::sOrange);
        if (ls >= 0)
        {
            line (X (ls), col::sGreen, 2.5f);
            line (X (le), col::sGreen, 2.5f);
            // (under the start's name when they meet)
            const bool meet = std::abs (X (ls) - X (start)) < 70.0f;
            text (g, "boucle", { X (ls) + 5.0f, r.getY() + (meet ? 14.0f : 0.0f), 80.0f, 14.0f }, mono (11.0f), col::sGreen);
        }
        if (playing == lastSample)
        {
            const float at = proc().player.shownPosition.load();
            if (at >= 0.0f)
                line (X (at), col::sRed, 2.0f);
        }
        juce::String t;
        t << lower (rk::noteName (s.root)) << " · v"_fr << s.velocity << " · "_fr << seconds ((double) len / s.sampleRate);
        if (s.loop.valid())
            t << " · loop "_fr << seconds ((double) s.loop.length() / s.sampleRate);
        t << " · "_fr << (int) std::lround (s.detuneCents) << " ct";
        text (g, t, info, mono (11.0f), col::screenDim);
    }

    void paintDrum (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto grid = r.withSizeKeepingCentre (r.getWidth() * 0.6f, r.getHeight() * 0.55f).translated (0.0f, 18.0f);
        for (int i = 0; i < 8; ++i)
        {
            auto cell = juce::Rectangle<float> (grid.getX() + (float) (i % 4) * grid.getWidth() / 4.0f, grid.getY() + (float) (i / 4) * grid.getHeight() / 2.0f,
                                                grid.getWidth() / 4.0f, grid.getHeight() / 2.0f).reduced (6.0f);
            g.setColour (col::screenLine);
            g.drawRoundedRectangle (cell, 8.0f, 1.5f);
        }
        text (g, "drum arrive en 0.4", r.removeFromTop (40.0f), grotesk (24.0f, 300), col::screenText, juce::Justification::centred);
        text (g, "8 pads · tranches · séquenceur · voix générées depuis ton synthé · ram"_fr, r.removeFromTop (20.0f), mono (12.0f),
              col::screenDim, juce::Justification::centred);
    }

    //------------------------------------------------------------------ CAPTURE: the plan, or the session going
    void paintCapture (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (m.captureKind == Machine::resampleKind)
        {
            paintResample (g, r);
            return;
        }
        if (m.captureKind == Machine::cyclesKind && ! m.status.sampling)
        {
            paintCyclePlan (g, r);
            return;
        }
        auto& s = proc().session;
        if (m.status.sampling)
        {
            auto top = r.removeFromTop (60.0f);
            text (g, m.status.text, top.removeFromTop (34.0f), grotesk (22.0f, 300), col::screenText);
            if (m.status.secondsLeft > 0)
                text (g, "reste " + duration (m.status.secondsLeft), top, mono (12.0f), col::screenDim);
            auto bar = r.removeFromTop (10.0f);
            g.setColour (col::screenLine);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (col::sRed);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, m.status.progress)), 3.0f);
            r.removeFromTop (14.0f);
            // the input, and the last sample taken
            const float lvl = proc().inputLevel();
            const float dbv = juce::Decibels::gainToDecibels (lvl, -100.0f);
            auto meter = r.removeFromBottom (14.0f);
            g.setColour (col::screenLine);
            g.fillRoundedRectangle (meter, 3.0f);
            g.setColour (dbv > -3.0f ? col::sRed : col::sGreen);
            g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, (dbv + 72.0f) / 72.0f)), 3.0f);
            if (m.status.last != nullptr && m.status.last->length() > 0)
            {
                const auto& d = m.status.last->audio[0];
                const int len = (int) d.size(), cols = (int) r.getWidth();
                const float mid = r.getCentreY(), h = r.getHeight() * 0.45f;
                g.setColour (col::screenText);
                for (int x = 0; x < cols; x += 2)
                {
                    const int a = (int) ((int64_t) x * len / cols), b = juce::jmin (len, (int) ((int64_t) (x + 2) * len / cols));
                    float mx = 0.0f;
                    for (int i = a; i < b; ++i)
                        mx = juce::jmax (mx, std::abs (d[(size_t) i]));
                    g.fillRect (r.getX() + (float) x, mid - mx * h, 1.5f, juce::jmax (1.0f, 2.0f * mx * h));
                }
            }
            return;
        }
        // the plan: the notes on a keyboard, a dot per velocity layer above each
        const auto notes = rk::plannedNotes (s);
        const auto vels = rk::plannedVelocities (s);
        auto summary = r.removeFromTop (40.0f);
        juce::String line;
        line << (int) notes.size() << " notes x " << (int) vels.size() << (vels.size() > 1 ? " vélocités"_fr : " vélocité"_fr);
        if (s.roundRobins > 1)
            line << " x " << s.roundRobins << " prises";
        line << " = " << (int) rk::planJobs (s).size() << " samples";
        text (g, juce::String::fromUTF8 (line.toRawUTF8()), summary.removeFromTop (28.0f), grotesk (22.0f, 300), col::screenText, juce::Justification::centredLeft, true);
        const double sr = proc().currentSampleRate() > 0 ? proc().currentSampleRate() : 48000.0;
        const auto est = rk::estimateSize (s, sr, 2);
        text (g, "≈ "_fr + duration (rk::estimatedSeconds (s)) + " · ≈ "_fr + megabytes (est.diskBytes) + " sur le disque",
              summary, mono (12.0f), col::screenDim, juce::Justification::centredLeft, true);
        r.removeFromTop (8.0f);
        auto timeline = r.removeFromBottom (24.0f);
        r.removeFromBottom (10.0f);
        const int lo = 24, hi = 107;                          // C1 .. B7
        const int whites = 49;
        const float ww = r.getWidth() / (float) whites;
        auto kb = r.removeFromBottom (r.getHeight() * 0.48f);
        auto dots = r;
        const float X[12] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 6.5f };
        auto keyX = [&] (int note) { const int k = note - lo; return kb.getX() + ((float) (k / 12) * 7.0f + X[k % 12]) * ww; };
        auto isBlack = [] (int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; };
        for (int i = 0; i < whites; ++i)
        {
            g.setColour (col::screenText);
            g.fillRoundedRectangle (kb.getX() + (float) i * ww + 1.0f, kb.getY(), ww - 2.0f, kb.getHeight(), 2.0f);
        }
        for (int n = lo; n <= hi; ++n)
            if (isBlack (n))
            {
                g.setColour (col::screen);
                g.fillRoundedRectangle (keyX (n) - ww * 0.3f, kb.getY(), ww * 0.6f, kb.getHeight() * 0.6f, 2.0f);
            }
        for (int n : notes)
        {
            if (n < lo || n > hi)
                continue;
            g.setColour (col::sOrange);
            const float x = keyX (n), y = isBlack (n) ? kb.getY() + kb.getHeight() * 0.6f - 10.0f : kb.getBottom() - 12.0f;
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            const int rows = juce::jmin (4, (int) vels.size());
            for (int v = 0; v < rows; ++v)
            {
                const float rr = 2.5f + 1.2f * (float) (rows - v);
                const float yy = dots.getY() + 8.0f + (float) v * (dots.getHeight() - 16.0f) / (float) juce::jmax (1, rows - 1) * (rows > 1 ? 1.0f : 0.0f);
                g.setColour (col::sOrange.withAlpha (1.0f - 0.2f * (float) v));
                g.fillEllipse (x - rr, yy - rr, rr * 2.0f, rr * 2.0f);
            }
        }
        // one note's time: held, its release, the pause
        const double total = s.hold + s.releaseMax + s.pause;
        auto seg = [&] (double d, juce::Colour c, const juce::String& t, juce::Colour ink)
        {
            auto cell = timeline.removeFromLeft (timeline.getWidth() * (float) (d / juce::jmax (0.001, total)) + 0.0f);
            g.setColour (c);
            g.fillRect (cell.reduced (1.0f, 0.0f));
            text (g, t, cell.reduced (8.0f, 0.0f), mono (11.0f, 500), ink);
        };
        auto tl = timeline;
        const float full = tl.getWidth();
        juce::ignoreUnused (full);
        seg (s.hold, col::sOrange, "tenue " + seconds (s.hold), col::screen);
        seg (s.releaseMax, col::sOrange.withAlpha (0.35f), "relâche "_fr + seconds (s.releaseMax), col::screenText);
        seg (s.pause, col::screenLine, seconds (s.pause), col::screenDim);
    }

    //------------------------------------------------------------------ CAPTURE CYCLES: the takes and what moves
    void paintCyclePlan (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& o = proc().cycleOptions;
        const int n = ReplikantProcessor::CycleOptions::countOf (o.count);
        const double each = o.hold + o.settle + 0.15;
        juce::String head, line;
        head << n << " cycles · "_fr << lower (rk::noteName (o.note));
        if (o.vary == 0)
            line << "cc " << o.cc << " de "_fr << o.ccFrom << " à "_fr << o.ccTo;
        else if (o.vary == 1)
            line << "vélocité de 1 à 127"_fr;
        else
            line << "tourne un bouton du synthé pendant la capture"_fr;
        line << " · ≈ "_fr << juce::roundToInt (n * each) << " s";
        auto top = r.removeFromTop (60.0f);
        text (g, head, top.removeFromTop (34.0f), grotesk (24.0f, 300), col::screenText, juce::Justification::centredLeft, true);
        text (g, line, top, mono (12.0f), col::screenDim);
        r.removeFromTop (10.0f);
        // one column a take, as high as what it sends
        auto row = r.removeFromTop (juce::jmin (110.0f, r.getHeight() - 10.0f));
        const float w = row.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float u = n > 1 ? (float) i / (float) (n - 1) : 0.0f;
            const float v = o.vary == 0 ? ((float) o.ccFrom + u * (float) (o.ccTo - o.ccFrom)) / 127.0f : (o.vary == 1 ? u : 0.5f);
            auto col = juce::Rectangle<float> (row.getX() + (float) i * w, row.getY(), w, row.getHeight()).reduced (juce::jmin (3.0f, w * 0.2f), 0.0f);
            g.setColour (col::screenLine);
            g.fillRoundedRectangle (col, 2.0f);
            g.setColour (col::sBlue.withAlpha (0.8f));
            g.fillRoundedRectangle (col.withTop (col.getBottom() - col.getHeight() * juce::jmax (0.04f, v)), 2.0f);
        }
    }

    //------------------------------------------------------------------ RÉÉCHANTILLONNER: the bars, as they fill
    void paintResample (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto& o = proc().resampleOptions;
        auto& pl = proc().player;
        const auto st = pl.resampleState();
        const int bars = ReplikantProcessor::ResampleOptions::barsOf (o.length);
        juce::String head, line;
        switch (st)
        {
            case Player::Resample::idle:
                head = bars > 0 ? juce::String (bars) + (bars > 1 ? " mesures" : " mesure") : juce::String ("libre");
                line = (bars > 0 && o.synced ? juce::String ("sur la mesure suivante quand le transport joue") : juce::String ("dès que tu lances"_fr))
                       + " · rejoué sur "_fr + lower (rk::noteName (o.root)) + (o.resetFx ? " · effets à zéro ensuite"_fr : " · effets gardés"_fr);
                break;
            case Player::Resample::armed:     head = "en attente de la mesure"; line = "lance le transport, ou annule"; break;
            case Player::Resample::recording: head = juce::String (pl.resampleSeconds(), 2) + " s"; line = "ce qui sort des effets, avant le volume"; break;
            case Player::Resample::handing:   head = "injection"; line = "la boucle prend le relais"; break;
            case Player::Resample::looping:   head = "boucle injectée"_fr; line = "joue une note pour reprendre la main · ou arrête-la"_fr; break;
        }
        auto top = r.removeFromTop (60.0f);
        text (g, head, top.removeFromTop (34.0f), grotesk (24.0f, 300), col::screenText, juce::Justification::centredLeft, true);
        text (g, line, top, mono (12.0f), col::screenDim);
        r.removeFromTop (8.0f);
        // the bars (free: one), filled as recorded
        auto row = r.removeFromTop (juce::jmin (90.0f, r.getHeight() - 40.0f));
        const int cells = bars > 0 ? bars : 1;
        const float gap = 6.0f, w = (row.getWidth() - gap * (float) (cells - 1)) / (float) cells;
        const float done = st == Player::Resample::recording ? pl.resampleProgress() * (float) cells
                           : (st >= Player::Resample::handing ? (float) cells : 0.0f);
        for (int i = 0; i < cells; ++i)
        {
            auto cell = juce::Rectangle<float> (row.getX() + (float) i * (w + gap), row.getY(), w, row.getHeight());
            g.setColour (col::screenLine);
            g.drawRoundedRectangle (cell.reduced (0.5f), 6.0f, 1.0f);
            const float f = juce::jlimit (0.0f, 1.0f, done - (float) i);
            if (f > 0.0f)
            {
                g.setColour ((st == Player::Resample::looping ? col::sBlue : col::sRed).withAlpha (0.75f));
                g.fillRoundedRectangle (cell.withWidth (cell.getWidth() * f).reduced (2.0f), 5.0f);
            }
            text (g, juce::String (i + 1), cell.reduced (8.0f, 6.0f), mono (11.0f, 500), col::screenDim, juce::Justification::topLeft, true);
        }
        r.removeFromTop (14.0f);
        // what goes in: the output
        const float dbv = juce::Decibels::gainToDecibels (pl.outLevel.load(), -100.0f);
        auto meter = r.removeFromTop (12.0f);
        g.setColour (col::screenLine);
        g.fillRoundedRectangle (meter, 3.0f);
        g.setColour (dbv > -1.0f ? col::sRed : col::sGreen);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, (dbv + 60.0f) / 60.0f)), 3.0f);
    }

    //------------------------------------------------------------------ the right: what was touched, the page's picture
    void paintPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (col::screenLine);
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
        auto in = r.reduced (14.0f, 12.0f);
        if (m.focus != nullptr)
        {
            auto head = in.removeFromTop (60.0f);
            // a choice with a picture (the modulator's kind): it, on the right
            Picto fp = Picto::none;
            for (int i = 0; i < snd::numMods; ++i)
                if (m.focus->param == snd::modParam (i, snd::mType))
                    fp = modPicto ((int) P (proc(), m.focus->param));
            if (fp != Picto::none)
                picto (g, fp, head.removeFromRight (54.0f).withSizeKeepingCentre (44.0f, 44.0f), col::sBlue, 1.8f);
            text (g, m.focus->name, head.removeFromTop (16.0f), mono (11.0f), col::screenDim);
            text (g, Encoder::valueText (*m.focus, m.arm), head, grotesk (34.0f, 300), col::screenText);
        }
        else
            in.removeFromTop (60.0f);
        in.removeFromTop (6.0f);
        auto& pages = m.pagesShown();
        const int pi = m.pageIndex();
        stepsMod = -1;
        fxShown = false;
        if (pi < 0 || pi >= (int) pages.size())
            return;
        const auto& pg = pages[(size_t) pi];
        auto viz = pg.viz;
        if (viz == "filter" && ! m.capture)
        {
            // the filter's page: its envelope's bank, the drive's, have their own pictures
            const int bank = m.bankOf[pg.name + juce::String (m.mode)];
            viz = bank == 1 ? "fenv" : (bank == 2 ? "drive" : "filter");
        }
        paintViz (g, viz, in);
    }

    void paintViz (juce::Graphics& g, const juce::String& viz, juce::Rectangle<float> r)
    {
        auto& p = proc();
        auto stroke = [&] (const juce::Path& path, juce::Colour c, float t = 2.5f) { g.setColour (c); g.strokePath (path, juce::PathStrokeType (t, juce::PathStrokeType::curved)); };
        auto fill = [&] (juce::Path path, juce::Colour c) { path.lineTo (r.getRight(), r.getBottom()); path.lineTo (r.getX(), r.getBottom()); path.closeSubPath(); g.setColour (c); g.fillPath (path); };
        auto envelope = [&] (float a, float d, float s, float rel, juce::Colour c)
        {
            // times on a root scale (0 .. 10 s each), the sustain held a while
            auto t = [] (float ms) { return std::sqrt (juce::jlimit (0.0f, 10000.0f, ms) / 10000.0f); };
            const float total = t (a) + t (d) + 0.35f + t (rel) + 1.0e-3f;
            const float sx = r.getWidth() / total;
            juce::Path path;
            float x = r.getX();
            path.startNewSubPath (x, r.getBottom());
            x += t (a) * sx;
            path.lineTo (x, r.getY() + 4.0f);
            const float sy = r.getBottom() - s * (r.getHeight() - 4.0f);
            path.quadraticTo (x + t (d) * sx * 0.25f, sy, x + t (d) * sx, sy);
            x += t (d) * sx;
            path.lineTo (x + 0.35f * sx, sy);
            x += 0.35f * sx;
            path.quadraticTo (x + t (rel) * sx * 0.25f, r.getBottom(), x + t (rel) * sx, r.getBottom());
            fill (path, c.withAlpha (0.18f));
            stroke (path, c);
        };
        if (viz == "osc")
        {
            const bool b = m.pageIndex() == 1;
            const auto& bank = bankOf (p, (int) P (p, b ? snd::obBank : snd::oaBank));
            const float pos = liveNorm (p, b ? snd::obPos : snd::oaPos);
            juce::Path path;
            for (int i = 0; i <= 180; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / 180.0f;
                const float y = r.getCentreY() - r.getHeight() * 0.42f * bank.read (pos, (double) i / 180.0, 4);
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (col::screenLine);
            g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
            stroke (path, b ? col::sGreen : col::sBlue, 3.0f);
        }
        else if (viz == "filter")
        {
            {
                auto side = r.removeFromRight (74.0f);
                r.removeFromRight (10.0f);
                const int model = (int) P (p, snd::fModel);
                picto (g, filterModelPicto (model), side.removeFromTop (side.getHeight() * 0.6f).withSizeKeepingCentre (70.0f, 70.0f), model > 0 ? col::sBlue : col::screenDim, 1.7f);
                picto (g, filterTypePicto ((int) P (p, snd::fType)), side.withSizeKeepingCentre (38.0f, 38.0f), model > 0 ? col::screenText : col::screenGhost, 1.5f);
                g.setColour (col::screenLine);
                g.fillRect (r.getRight() + 4.0f, r.getY(), 1.0f, r.getHeight());
            }
            dsp::FilterSettings fs;
            fs.model = (dsp::FilterModel) (int) P (p, snd::fModel);
            fs.type = (dsp::FilterType) (int) P (p, snd::fType);
            fs.res = liveValue (p, snd::reso);
            fs.drive = P (p, snd::fDrive);
            fs.sep = P (p, snd::sep);
            const float cutoff = liveValue (p, snd::cutoff);
            juce::Path path;
            for (int i = 0; i <= 160; ++i)
            {
                const float hz = 20.0f * std::pow (1000.0f, (float) i / 160.0f);
                float dbv = fs.model == dsp::FilterModel::off ? 0.0f : dsp::VAFilter::responseDb (fs, cutoff, hz);
                dbv = juce::jlimit (-48.0f, 24.0f, dbv);
                const float x = r.getX() + r.getWidth() * (float) i / 160.0f, y = r.getY() + (24.0f - dbv) / 72.0f * r.getHeight();
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            fill (path, col::sBlue.withAlpha (0.18f));
            stroke (path, col::sBlue);
            const float cx = r.getX() + r.getWidth() * std::log (cutoff / 20.0f) / std::log (1000.0f);
            g.setColour (col::screenText);
            g.fillEllipse (cx - 5.0f, r.getY() + (24.0f - juce::jlimit (-48.0f, 24.0f, fs.model == dsp::FilterModel::off ? 0.0f : dsp::VAFilter::responseDb (fs, cutoff, cutoff))) / 72.0f * r.getHeight() - 5.0f, 10.0f, 10.0f);
        }
        else if (viz == "adsr")
            envelope (P (p, snd::attack), P (p, snd::decay), P (p, snd::sustain), P (p, snd::release), col::sGreen);
        else if (viz == "fenv")
            envelope (P (p, snd::fAttack), P (p, snd::fDecay), P (p, snd::fSustain), P (p, snd::fRelease), col::sOrange);
        else if (viz == "drive")
        {
            {
                auto side = r.removeFromRight (74.0f);
                r.removeFromRight (10.0f);
                const int model = (int) P (p, snd::dModel);
                picto (g, driveModelPicto (model), side.withSizeKeepingCentre (70.0f, 70.0f), model > 0 ? col::sOrange : col::screenDim, 1.7f);
                g.setColour (col::screenLine);
                g.fillRect (r.getRight() + 4.0f, r.getY(), 1.0f, r.getHeight());
            }
            const float k = 1.0f + 12.0f * P (p, snd::dAmt);
            juce::Path path;
            for (int i = 0; i <= 120; ++i)
            {
                const float x = (float) i / 60.0f - 1.0f, y = std::tanh (k * x) / std::tanh (k);
                const float px = r.getX() + r.getWidth() * (float) i / 120.0f, py = r.getCentreY() - y * r.getHeight() * 0.45f;
                if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
            }
            stroke (path, (int) P (p, snd::dModel) > 0 ? col::sOrange : col::screenGhost);
        }
        else if (viz == "fx")
        {
            // the chain in its order: drag a stage to move it, click it for its settings
            const int ons[6] = { (int) P (p, snd::dModel) > 0, P (p, snd::spOn) > 0.5f, P (p, snd::piOn) > 0.5f, P (p, snd::chOn) > 0.5f,
                                 P (p, snd::crOn) > 0.5f, P (p, snd::rvOn) > 0.5f };
            const char* names[6] = { "drive", "spectre", "pitch", "chorus", "crush", "espace" };
            const int bankStage[7] = { 0, 1, 1, 2, 3, 4, 5 };
            const int shownStage = bankStage[juce::jlimit (0, 6, m.bankOf[juce::String ("fx") + juce::String (m.mode)])];
            const auto order = p.player.fxOrder();
            fxShown = true;
            fxRow = r.withSizeKeepingCentre (r.getWidth(), 80.0f).translated (0.0f, -8.0f);
            const float w = (fxRow.getWidth() - 5.0f * 10.0f) / 6.0f;
            text (g, "in → … → out · drag to reorder · the switch bypasses"_fr, r.removeFromBottom (18.0f), mono (10.0f), col::screenDim, juce::Justification::centred, true);
            for (int pos = 0; pos < 6; ++pos)
            {
                const int st = order[(size_t) pos];
                auto box = juce::Rectangle<float> (fxRow.getX() + (float) pos * (w + 10.0f), fxRow.getY(), w, fxRow.getHeight());
                fxBoxes[pos] = box;
                if (pos == dragPos)
                    box = box.withX (juce::jlimit (fxRow.getX(), fxRow.getRight() - w, dragX - w * 0.5f)).translated (0.0f, -6.0f);
                if (pos < 5)
                {
                    g.setColour (col::screenGhost);
                    g.fillRect (fxBoxes[pos].getRight() + 1.0f, fxRow.getCentreY() - 1.0f, 8.0f, 2.0f);
                }
                g.setColour (ons[st] ? col::sYellow : col::screen);
                g.fillRoundedRectangle (box, 6.0f);
                g.setColour (st == shownStage ? col::screenText : (ons[st] ? col::sYellow : col::screenLine));
                g.drawRoundedRectangle (box, 6.0f, st == shownStage ? 2.0f : 1.5f);
                auto inBox = box.reduced (4.0f, 6.0f);
                const auto ink = ons[st] ? col::screen : col::screenDim;
                // its switch: on, or passed by
                auto sw = inBox.removeFromTop (14.0f).withSizeKeepingCentre (26.0f, 14.0f);
                fxSwitches[pos] = sw.expanded (6.0f, 4.0f);
                g.setColour (ons[st] ? col::screen : col::screenLine);
                g.fillRoundedRectangle (sw, 7.0f);
                g.setColour (ons[st] ? col::sYellow : col::screenDim);
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ ons[st] ? sw.getRight() - 7.0f : sw.getX() + 7.0f, sw.getCentreY() }));
                picto (g, stagePicto (st, (int) P (p, snd::dModel)), inBox.removeFromTop (inBox.getHeight() - 16.0f).withSizeKeepingCentre (26.0f, 26.0f), ink, 1.5f);
                text (g, names[st], inBox, mono (10.0f, 500), ink, juce::Justification::centred);
            }
        }
        else if (viz == "cycleplan")
        {
            // the cycles already captured with the instrument, laid over each other (else the picture of it)
            auto ins = p.getInstrument();
            if (ins == nullptr || ins->cycles == nullptr || ins->cycles->count() == 0)
            {
                picto (g, Picto::cycles, r.withSizeKeepingCentre (64.0f, 64.0f), col::screenDim, 1.6f);
                text (g, "pas encore de cycles capturés"_fr, r.removeFromBottom (16.0f), mono (10.0f), col::screenDim, juce::Justification::centred);
            }
            else
            {
                const auto& bank = *ins->cycles;
                const int nc = bank.count();
                for (int c = 0; c < nc; ++c)
                {
                    juce::Path path;
                    const float* cy = bank.cycle (c);
                    for (int i = 0; i <= 128; ++i)
                    {
                        const float x = r.getX() + r.getWidth() * (float) i / 128.0f;
                        const float y = r.getCentreY() - r.getHeight() * 0.42f * cy[(i * dsp::CycleBank::size / 128) % dsp::CycleBank::size];
                        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                    }
                    stroke (path, col::sBlue.withAlpha (0.25f + 0.75f * (float) c / (float) juce::jmax (1, nc - 1)), 1.2f);
                }
                text (g, juce::String (nc) + " cycles capturés"_fr, r.removeFromBottom (16.0f), mono (10.0f), col::screenDim, juce::Justification::centredRight);
            }
        }
        else if (viz == "resample")
        {
            // the way the sound goes: the effects, recorded, into the sampler, looped
            const Picto steps[4] = { Picto::chain, Picto::record, Picto::cassette, Picto::loop };
            const char* names[4] = { "effets", "enregistré", "sampler", "en boucle" };
            const float w = r.getWidth() / 4.0f;
            for (int i = 0; i < 4; ++i)
            {
                auto cell = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY() + 10.0f, w, r.getHeight() - 20.0f);
                picto (g, steps[i], cell.removeFromTop (cell.getHeight() - 22.0f).withSizeKeepingCentre (40.0f, 40.0f),
                       i == 1 ? col::sRed : col::sBlue, 1.7f);
                text (g, juce::String::fromUTF8 (names[i]), cell, mono (10.0f, 500), col::screenDim, juce::Justification::centred);
                if (i < 3)
                    text (g, "→"_fr, { cell.getRight() - 8.0f, r.getCentreY() - 20.0f, 16.0f, 16.0f }, mono (12.0f), col::screenGhost, juce::Justification::centred, true);
            }
        }
        else if (viz == "orbite")
        {
            // its three parts, one cycle each; the bank being set lit
            const auto s = orbiteSettings();
            const double r1 = liveValue (p, snd::s1Ratio), r2 = liveValue (p, snd::s2Ratio), turn = orbiteTurn();
            const int bank = m.bankOf[juce::String ("orbite") + juce::String (m.mode)];
            const char* names[3] = { "carrier", "sat 1", "sat 2" };
            const juce::Colour cols[3] = { col::sOrange, col::sBlue, col::sViolet };
            const float rowH = r.getHeight() / 3.0f;
            for (int k = 0; k < 3; ++k)
            {
                auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) k, r.getWidth(), rowH).reduced (0.0f, 3.0f);
                const bool lit = bank == k;
                text (g, juce::String::fromUTF8 (names[k]), row.removeFromLeft (70.0f), mono (11.0f, lit ? 500 : 400), lit ? cols[k] : col::screenDim,
                      juce::Justification::centredLeft);
                juce::Path path;
                for (int i = 0; i <= 120; ++i)
                {
                    float parts[3];
                    orbiteAt (s, r1, r2, turn, (double) i / 120.0, parts);
                    const float x = row.getX() + row.getWidth() * (float) i / 120.0f, y = row.getCentreY() - row.getHeight() * 0.42f * parts[k];
                    if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                }
                stroke (path, lit ? cols[k] : col::screenGhost, lit ? 2.2f : 1.4f);
            }
        }
        else if (viz == "mix")
        {
            // where the mix goes
            const char* steps[] = { "sources", "mixer", "filter", "env", "fx" };
            const float w = r.getWidth() / 5.0f;
            for (int i = 0; i < 5; ++i)
            {
                auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getCentreY() - 18.0f, w - 14.0f, 36.0f);
                g.setColour (i == 1 ? col::sOrange : col::screenLine);
                g.drawRoundedRectangle (cell, 6.0f, i == 1 ? 1.8f : 1.0f);
                text (g, juce::String::fromUTF8 (steps[i]), cell, mono (10.5f, i == 1 ? 500 : 400), i == 1 ? col::sOrange : col::screenDim, juce::Justification::centred);
                if (i < 4)
                    text (g, "→"_fr, { cell.getRight(), cell.getY(), 14.0f, cell.getHeight() }, mono (11.0f), col::screenGhost, juce::Justification::centred, true);
            }
        }
        else if (viz == "voices")
        {
            const int n = p.player.activeVoices.load(), poly = (int) P (p, snd::voices);
            const float w = (r.getWidth() - 7.0f * 8.0f) / 8.0f;
            for (int i = 0; i < 8; ++i)
            {
                auto bar = juce::Rectangle<float> (r.getX() + (float) i * (w + 8.0f), r.getY() + 10.0f, w, r.getHeight() - 10.0f);
                g.setColour (i < n ? col::sOrange : (i < poly ? col::screenLine : col::screenGrid));
                g.fillRoundedRectangle (bar, 4.0f);
            }
        }
        else if (viz.startsWith ("mod"))
        {
            const int mi = juce::jlimit (0, snd::numMods - 1, m.bankOf["mods" + juce::String (m.mode)]);
            auto M = [&] (snd::ModSetting w) { return P (p, snd::modParam (mi, w)); };
            const auto type = (dsp::ModType) (int) M (snd::mType);
            const auto colour = col::modOnScreen (mi);
            if (type == dsp::ModType::steps || type == dsp::ModType::random)
            {
                const int len = juce::jlimit (1, snd::maxSteps, (int) M (snd::mLen));
                const int now = p.player.modStep[(size_t) mi].load();
                const float w = r.getWidth() / (float) len;
                for (int i = 0; i < len; ++i)
                {
                    const float v = type == dsp::ModType::steps ? p.player.steps[(size_t) mi][(size_t) i].load()
                                                                : (float) std::sin ((double) i * 12.9898 + mi) * 0.5f;
                    const float y0 = r.getCentreY(), y1 = r.getCentreY() - v * r.getHeight() * 0.45f;
                    g.setColour (i == now ? col::screenText : colour);
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX() + (float) i * w + 2.0f, juce::jmin (y0, y1), r.getX() + (float) (i + 1) * w - 2.0f, juce::jmax (y0, y1) + 1.0f));
                }
                if (type == dsp::ModType::steps)
                {
                    stepsArea = r;
                    stepsMod = mi;
                    stepsLen = len;
                }
            }
            else if (type == dsp::ModType::envelope || type == dsp::ModType::follower)
                envelope (M (snd::mAttack), M (snd::mDecay), type == dsp::ModType::envelope ? M (snd::mSustain) : 1.0f, M (snd::mRelease), colour);
            else
            {
                const int shape = (int) M (snd::mShape);
                // the shape at t (in cycles), as the screen draws it
                auto shapeAt = [shape] (float t)
                {
                    const float ph = t - std::floor (t);
                    switch (shape)
                    {
                        case 0:  return std::sin (ph * juce::MathConstants<float>::twoPi);
                        case 1:  return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
                        case 2:  return 2.0f * ph - 1.0f;
                        case 3:  return 1.0f - 2.0f * ph;
                        case 4:  return ph < 0.5f ? 1.0f : -1.0f;
                        default: return std::sin ((float) ((int) (t * 4.0f)) * 7.3f);
                    }
                };
                juce::Path path;
                for (int i = 0; i <= 200; ++i)
                {
                    const float t = (float) i / 100.0f;
                    const float x = r.getX() + r.getWidth() * (float) i / 200.0f, py = r.getCentreY() - shapeAt (t) * r.getHeight() * 0.42f;
                    if (i == 0) path.startNewSubPath (x, py); else path.lineTo (x, py);
                }
                stroke (path, colour);
                // where the wave is now: a dot riding the curve (over the two cycles drawn)
                const float at = juce::jlimit (0.0f, 1.999f, p.player.modPhase[(size_t) mi].load());
                const float x = r.getX() + r.getWidth() * at * 0.5f, y = r.getCentreY() - shapeAt (at) * r.getHeight() * 0.42f;
                g.setColour (colour.withAlpha (0.35f));
                g.fillEllipse (x - 8.0f, y - 8.0f, 16.0f, 16.0f);
                g.setColour (col::screenText);
                g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f);
            }
            // (what it sends, after its depth: on its key, under the screen)
        }
        else if (viz == "scenes")
        {
            const float xf = liveNorm (p, snd::xfade);
            auto track = r.withSizeKeepingCentre (r.getWidth(), 10.0f);
            g.setColour (col::screenLine);
            g.fillRoundedRectangle (track, 5.0f);
            g.setColour (col::sViolet);
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * xf), 5.0f);
            g.setColour (col::screenText);
            g.fillRoundedRectangle (track.getX() + track.getWidth() * xf - 10.0f, track.getCentreY() - 22.0f, 20.0f, 44.0f, 4.0f);
            const int a = (int) P (p, snd::sceneA), b = (int) P (p, snd::sceneB);
            text (g, "a " + juce::String (a), r.removeFromTop (20.0f), mono (13.0f, 500), col::sViolet);
            text (g, "b " + juce::String (b), r.removeFromBottom (20.0f), mono (13.0f, 500), col::sViolet, juce::Justification::centredRight);
        }
        else if (viz == "lecture")
        {
            const bool elastic = P (p, snd::playMode) > 0.5f;
            juce::Path path;
            for (int k = 0; k < 7; ++k)
            {
                const float x0 = r.getX() + (float) k * r.getWidth() / 6.5f - 30.0f, w = elastic ? r.getWidth() / 3.0f : r.getWidth() / 6.5f;
                for (int s = 0; s <= 24; ++s)
                {
                    const float t = (float) s / 24.0f;
                    const float y = r.getBottom() - r.getHeight() * 0.85f * (elastic ? 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::twoPi) : 1.0f);
                    if (s == 0) path.startNewSubPath (x0 + t * w, y); else path.lineTo (x0 + t * w, y);
                }
            }
            stroke (path, col::screenText, 2.0f);
        }
        else if (viz == "loop")
        {
            const float xf = P (p, snd::loopXfade);
            juce::Path a, b;
            const float x0 = r.getX(), w = r.getWidth();
            const float f0 = x0 + w * (0.5f - 0.25f * xf), f1 = x0 + w * (0.5f + 0.25f * xf);
            a.startNewSubPath (x0, r.getY() + 10.0f); a.lineTo (f0, r.getY() + 10.0f); a.lineTo (f1, r.getBottom() - 10.0f); a.lineTo (x0 + w, r.getBottom() - 10.0f);
            b.startNewSubPath (x0, r.getBottom() - 10.0f); b.lineTo (f0, r.getBottom() - 10.0f); b.lineTo (f1, r.getY() + 10.0f); b.lineTo (x0 + w, r.getY() + 10.0f);
            g.setColour (col::sGreen.withAlpha (0.15f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (f0, r.getY(), f1, r.getBottom()));
            stroke (a, col::sGreen);
            stroke (b, col::screenText, 1.5f);
        }
        else if (viz == "pitch")
        {
            const float idx = juce::jlimit (0.0f, 1.0f, liveValue (p, snd::pmIndex) / 256.0f);
            juce::Path path;
            for (int i = 0; i <= 200; ++i)
            {
                const float t = (float) i / 200.0f;
                const float y = std::sin (t * 12.0f + idx * 6.0f * std::sin (t * 30.0f));
                const float x = r.getX() + r.getWidth() * t, py = r.getCentreY() - y * r.getHeight() * 0.42f;
                if (i == 0) path.startNewSubPath (x, py); else path.lineTo (x, py);
            }
            stroke (path, col::sPink);
        }
        else if (viz == "plan")
        {
            text (g, "les réglages de la capture"_fr, r.removeFromTop (20.0f), mono (12.0f), col::screenDim);
            juce::String l;
            l << "midi → "_fr << (p.options.midiPort.isEmpty() ? juce::String ("aucune sortie") : p.options.midiPortName) << " · canal "_fr << p.options.channel;
            text (g, juce::String::fromUTF8 (l.toRawUTF8()), r.removeFromTop (20.0f), mono (12.0f), col::screenText);
            text (g, juce::String ("source : ") + (p.options.source == ReplikantProcessor::Source::input ? "entrée"_fr : p.options.source == ReplikantProcessor::Source::sidechain ? "sidechain" : "synthé test"_fr),
                  r.removeFromTop (20.0f), mono (12.0f), col::screenText);
            if (m.status.latency >= 0 && p.currentSampleRate() > 0)
                text (g, "latence mesurée "_fr + juce::String (m.status.latency * 1000.0 / p.currentSampleRate(), 1) + " ms", r.removeFromTop (20.0f), mono (12.0f), col::screenDim);
        }
    }

    // PAS: the steps drawn with the mouse
    void editSteps (const juce::MouseEvent& e)
    {
        if (stepsMod < 0 || ! stepsArea.contains (e.position))
            return;
        const int i = juce::jlimit (0, stepsLen - 1, (int) ((e.position.x - stepsArea.getX()) / stepsArea.getWidth() * (float) stepsLen));
        const float v = juce::jlimit (-1.0f, 1.0f, (stepsArea.getCentreY() - e.position.y) / (stepsArea.getHeight() * 0.45f));
        proc().player.steps[(size_t) stepsMod][(size_t) i] = v;
        proc().settingsChanged();
        repaint();
    }
};

//==============================================================================
// the keyboard under the encoders (SYNTHÉ, SAMPLER): played with the mouse, the keys held lit
class Keyboard : public juce::Component
{
public:
    explicit Keyboard (Machine& mm) : m (mm) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto top = r.removeFromTop (24.0f);
        text (g, "keys · "_fr + lower (rk::noteName (whiteNote (0))) + " – "_fr + lower (rk::noteName (whiteNote (14))), top, mono (12.0f, 500), col::deckInk2);
        octDown = top.removeFromRight (56.0f).reduced (0.0f, 1.0f);
        top.removeFromRight (6.0f);
        octUp = octDown;
        octDown = top.removeFromRight (56.0f).reduced (0.0f, 1.0f);
        key (g, octDown, KeyState::normal, false, 5.0f);
        key (g, octUp, KeyState::normal, false, 5.0f);
        text (g, "oct -", octDown.withTrimmedBottom (3.0f), mono (11.0f, 500), col::ink, juce::Justification::centred);
        text (g, "oct +", octUp.withTrimmedBottom (3.0f), mono (11.0f, 500), col::ink, juce::Justification::centred);
        r.removeFromTop (6.0f);
        keys = r;
        const int whites = 15;
        const float ww = r.getWidth() / (float) whites;
        for (int i = 0; i < whites; ++i)
        {
            const int note = whiteNote (i);
            auto k = juce::Rectangle<float> (r.getX() + (float) i * ww + 2.0f, r.getY(), ww - 4.0f, r.getHeight());
            const bool down = isDown (note);
            const auto ivory = juce::Colour (0xffe8e1d1);
            g.setColour (down ? col::accentDark : col::keyShadow);
            g.fillRoundedRectangle (k, 6.0f);
            auto face = k.withTrimmedBottom (down ? 1.0f : 4.0f);
            if (down)
                face.translate (0.0f, 2.0f);
            g.setGradientFill (juce::ColourGradient (down ? col::accent : ivory, face.getX(), face.getY(),
                                                     (down ? col::accent : ivory).darker (0.08f), face.getX(), face.getBottom(), false));
            g.fillRoundedRectangle (face, 6.0f);
            if (note % 12 == 0)
                text (g, lower (rk::noteName (note)), k.removeFromBottom (22.0f), mono (11.0f, 500), down ? col::accentInk : juce::Colour (0xff4a463f), juce::Justification::centred);
        }
        for (int i = 0; i < whites - 1; ++i)
        {
            const int n = whiteNote (i) + 1;
            if (! isBlack (n))
                continue;
            auto k = juce::Rectangle<float> (r.getX() + (float) (i + 1) * ww - ww * 0.3f, r.getY(), ww * 0.6f, r.getHeight() * 0.58f);
            g.setColour (isDown (n) ? col::accent : col::well);
            g.fillRoundedRectangle (k, 5.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (octDown.contains (e.position)) { base = juce::jmax (0, base - 12); repaint(); return; }
        if (octUp.contains (e.position))   { base = juce::jmin (96, base + 12); repaint(); return; }
        play (noteAt (e.position), e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int n = noteAt (e.position);
        if (n != held)
            play (n, e.position);
    }
    void mouseUp (const juce::MouseEvent&) override   { play (-1, {}); }

    int base = 60;           // the keyboard shows an octave under it to an octave over it (C2 .. C4)

private:
    Machine& m;
    juce::Rectangle<float> keys, octDown, octUp;
    int held = -1;

    static bool isBlack (int n)        { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    int whiteNote (int i) const        { const int map[7] = { 0, 2, 4, 5, 7, 9, 11 }; return base - 12 + (i / 7) * 12 + map[i % 7]; }
    bool isDown (int note) const       { return note >= 0 && note < 128 && m.proc.player.keyDown[(size_t) note].load(); }

    int noteAt (juce::Point<float> p) const
    {
        if (! keys.contains (p))
            return -1;
        const float ww = keys.getWidth() / 15.0f;
        const int i = juce::jlimit (0, 14, (int) ((p.x - keys.getX()) / ww));
        if (p.y < keys.getY() + keys.getHeight() * 0.58f)
        {
            const float within = (p.x - keys.getX()) - (float) i * ww;
            if (within < ww * 0.3f && i > 0 && isBlack (whiteNote (i) - 1))
                return whiteNote (i) - 1;
            if (within > ww * 0.7f && i < 14 && isBlack (whiteNote (i) + 1))
                return whiteNote (i) + 1;
        }
        return whiteNote (i);
    }

    void play (int note, juce::Point<float> at)
    {
        if (held >= 0)
            m.proc.audition (held, 0, false);
        held = note;
        if (note >= 0)
        {
            const float y = keys.getHeight() > 0 ? (at.y - keys.getY()) / keys.getHeight() : 0.8f;
            m.proc.audition (note, juce::jlimit (30, 127, (int) (40 + 87 * y)), true);
        }
        repaint();
    }
};

//==============================================================================
// the modulators (their keys arm them) and the scenes (sixteen, A and B, the fader)
class ModScenes : public juce::Component
{
public:
    explicit ModScenes (Machine& mm) : m (mm) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::face);
        g.fillRoundedRectangle (r, 14.0f);
        layout();
        auto& p = m.proc;
        for (int i = 0; i < snd::numMods; ++i)
        {
            const bool armed = m.arm.mod == i;
            key (g, modKeys[i], armed ? KeyState::on : KeyState::normal, false, 6.0f);
            auto k = modKeys[i].withTrimmedBottom (3.0f).reduced (8.0f, 5.0f);
            g.setColour (col::mod (i));
            g.fillRoundedRectangle (k.getX(), k.getY() + 3.0f, 8.0f, 8.0f, 2.0f);
            // its number, its kind drawn
            const auto ink = armed ? col::keyOnInk : col::ink;
            auto line = k.withTrimmedLeft (12.0f).withHeight (14.0f);
            text (g, "m" + juce::String (i + 1), line.removeFromLeft (22.0f), mono (11.0f, 600), ink);
            picto (g, modPicto ((int) P (p, snd::modParam (i, snd::mType))), line.removeFromRight (30.0f).withTrimmedTop (1.0f), ink, 1.3f);
            auto bar = k.removeFromBottom (4.0f);
            g.setColour (armed ? col::screenLine : col::tickOff);
            g.fillRoundedRectangle (bar, 2.0f);
            const float v = p.player.modValue[(size_t) i].load();
            const float c = bar.getCentreX(), w = bar.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, v);
            g.setColour (col::mod (i));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (c, c + w), bar.getY(), juce::jmax (c, c + w), bar.getBottom()));
        }
        const int a = (int) P (p, snd::sceneA) - 1, b = (int) P (p, snd::sceneB) - 1;
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (assigning >= 0 && now > assignUntil)
            assigning = -1;
        const bool blink = assigning >= 0 && std::fmod (now, 0.5) < 0.3;
        text (g, "scenes", sceneTitle, mono (12.0f, 600), col::ink);
        if (assigning >= 0)
            text (g, juce::String ("pick a scene for ") + (assigning == snd::sceneA ? "a" : "b"), sceneTitle, mono (12.0f, 600), col::red,
                  juce::Justification::centredRight);
        else
            text (g, "a " + juce::String (a + 1) + " → b "_fr + juce::String (b + 1) + " · "_fr + juce::String (juce::roundToInt (liveNorm (p, snd::xfade) * 100.0f)) + " %",
                  sceneTitle, mono (12.0f), col::ink2, juce::Justification::centredRight, true);
        for (int i = 0; i < snd::numScenes; ++i)
        {
            const bool armed = m.arm.scene == i, holds = p.player.sceneHolds (i);
            key (g, sceneKeys[i], armed ? KeyState::on : KeyState::normal, false, 5.0f);
            auto k = sceneKeys[i].withTrimmedBottom (3.0f);
            if (armed)
            {
                g.setColour (col::violet);
                g.fillRoundedRectangle (k.reduced (1.0f), 5.0f);
            }
            if (assigning >= 0)
            {
                // waiting for a scene: every pad offered
                g.setColour (col::red.withAlpha (blink ? 0.55f : 0.3f));
                g.drawRoundedRectangle (k.reduced (0.5f), 5.0f, 1.4f);
            }
            text (g, juce::String (i + 1), k, mono (11.0f, holds ? 600 : 400), armed ? juce::Colours::white : (holds ? col::ink : col::ink3), juce::Justification::centred);
            if (i == a || i == b)
                text (g, i == a ? "a" : "b", k.reduced (3.0f, 1.0f), mono (9.0f, 600), armed ? juce::Colours::white : col::violet, juce::Justification::topRight);
        }
        key (g, aKey, assigning == snd::sceneA ? (blink ? KeyState::accent : KeyState::normal) : KeyState::on, false, 8.0f);
        key (g, bKey, assigning == snd::sceneB ? (blink ? KeyState::accent : KeyState::normal) : KeyState::normal, false, 8.0f);
        text (g, "a" + juce::String (a + 1), aKey.withTrimmedBottom (3.0f), mono (13.0f, 500), col::keyOnInk, juce::Justification::centred);
        text (g, "b" + juce::String (b + 1), bKey.withTrimmedBottom (3.0f), mono (13.0f, 500), col::ink, juce::Justification::centred);
        auto slot = fader.withSizeKeepingCentre (fader.getWidth(), 10.0f);
        g.setColour (col::well);
        g.fillRoundedRectangle (slot, 5.0f);
        const float x = fader.getX() + liveNorm (p, snd::xfade) * fader.getWidth();
        auto cap = juce::Rectangle<float> (x - 13.0f, fader.getY(), 26.0f, fader.getHeight());
        key (g, cap, KeyState::normal, false, 5.0f);
        g.setColour (col::accent);
        g.fillRect (cap.withSizeKeepingCentre (14.0f, 3.0f).translated (0.0f, -1.5f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& p = m.proc;
        layout();
        for (int i = 0; i < snd::numMods; ++i)
            if (modKeys[i].contains (e.position))
            {
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu menu;
                    menu.addItem (1, "Effacer les routes de M"_fr + juce::String (i + 1));
                    menu.showMenuAsync ({}, [this, i] (int r) { if (r == 1) { m.proc.player.clearRoutes (i); m.proc.settingsChanged(); m.armChanged(); } });
                    return;
                }
                m.arm.mod = m.arm.mod == i ? -1 : i;
                m.arm.scene = -1;
                m.armChanged();
                return;
            }
        for (int i = 0; i < snd::numScenes; ++i)
            if (sceneKeys[i].contains (e.position))
            {
                // A (or B) pressed just before: this pad becomes its scene
                if (assigning >= 0 && ! e.mods.isPopupMenu())
                {
                    setSound (p, assigning, (float) (i + 1));
                    assigning = -1;
                    m.proc.settingsChanged();
                    repaint();
                    return;
                }
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu menu;
                    menu.addItem (1, "Effacer la scène "_fr + juce::String (i + 1));
                    menu.showMenuAsync ({}, [this, i] (int r) { if (r == 1) { m.proc.player.clearScene (i); m.proc.settingsChanged(); m.armChanged(); } });
                    return;
                }
                m.arm.scene = m.arm.scene == i ? -1 : i;
                m.arm.mod = -1;
                m.armChanged();
                return;
            }
        // A or B: waits a few seconds for the pad that becomes its scene (again: no more waiting)
        auto pick = [&] (int id)
        {
            if (m.arm.scene >= 0)
            {
                setSound (p, id, (float) (m.arm.scene + 1));     // a scene being set: it, at once
                assigning = -1;
            }
            else if (assigning == id)
                assigning = -1;
            else
            {
                assigning = id;
                assignUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 4.0;
            }
            repaint();
        };
        if (aKey.contains (e.position)) { pick (snd::sceneA); return; }
        if (bKey.contains (e.position)) { pick (snd::sceneB); return; }
        if (fader.expanded (14.0f, 6.0f).contains (e.position))
        {
            dragging = true;
            if (auto* x = p.params.getParameter (snd::def (snd::xfade).id))
                x->beginChangeGesture();
            mouseDrag (e);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        if (auto* x = m.proc.params.getParameter (snd::def (snd::xfade).id))
            x->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (e.position.x - fader.getX()) / fader.getWidth()));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            if (auto* x = m.proc.params.getParameter (snd::def (snd::xfade).id))
                x->endChangeGesture();
        dragging = false;
    }

private:
    Machine& m;
    juce::Rectangle<float> modKeys[snd::numMods], sceneKeys[snd::numScenes], sceneTitle, aKey, bKey, fader;
    bool dragging = false;
    int assigning = -1;                 // snd::sceneA or sceneB: waiting for a pad
    double assignUntil = 0.0;

    void layout()
    {
        auto r = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
        auto mods = r.removeFromTop (40.0f);
        const float mw = (mods.getWidth() - 3.0f * 6.0f) / 4.0f;
        for (int i = 0; i < snd::numMods; ++i)
            modKeys[i] = { mods.getX() + (float) i * (mw + 6.0f), mods.getY(), mw, mods.getHeight() };
        r.removeFromTop (8.0f);
        sceneTitle = r.removeFromTop (18.0f);
        r.removeFromTop (4.0f);
        auto grid = r.removeFromTop (62.0f);
        const float sw = (grid.getWidth() - 7.0f * 5.0f) / 8.0f;
        for (int i = 0; i < snd::numScenes; ++i)
            sceneKeys[i] = { grid.getX() + (float) (i % 8) * (sw + 5.0f), grid.getY() + (float) (i / 8) * 32.0f, sw, 29.0f };
        auto row = r.removeFromBottom (44.0f);
        aKey = row.removeFromLeft (46.0f);
        bKey = row.removeFromRight (46.0f);
        fader = row.reduced (22.0f, 2.0f);
    }
};

//==============================================================================
// WTF?!: how far the sound is pulled from itself, LIVE or LOOP, back to itself
class WtfPanel : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit WtfPanel (Machine& mm) : m (mm)
    {
        setTooltip ("WTF?! : le son tiré loin de lui-même, un nouveau tirage à chaque croche (LIVE) ou une boucle de huit qui revient (LOOP). RETOUR : le son tel qu'il est."_fr);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::face);
        g.fillRoundedRectangle (r, 14.0f);
        auto in = r.reduced (10.0f, 8.0f);
        text (g, "wtf?!", in.removeFromTop (18.0f), mono (13.0f, 600), col::red, juce::Justification::centred);
        knob = in.removeFromTop (96.0f);
        const float v = P (m.proc, snd::wtf);
        encoder (g, knob.getCentre(), 44.0f, v, false, col::red, juce::Colours::white, 0.0f, 0.0f, {}, false, col::red);
        text (g, juce::String (juce::roundToInt (v * 100.0f)) + " %", in.removeFromTop (20.0f), mono (14.0f, 500), col::ink, juce::Justification::centred);
        auto row = in.removeFromBottom (30.0f);
        modeKey = row.removeFromLeft (row.getWidth() * 0.5f - 3.0f);
        row.removeFromLeft (6.0f);
        backKey = row;
        const bool loop = P (m.proc, snd::wtfMode) > 0.5f;
        key (g, modeKey, KeyState::on, false, 6.0f);
        key (g, backKey, KeyState::normal, false, 6.0f);
        text (g, loop ? "loop" : "live", modeKey.withTrimmedBottom (3.0f), mono (11.0f, 500), col::keyOnInk, juce::Justification::centred);
        text (g, "back", backKey.withTrimmedBottom (3.0f), mono (11.0f, 500), col::ink, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (modeKey.contains (e.position)) { setSound (m.proc, snd::wtfMode, P (m.proc, snd::wtfMode) > 0.5f ? 0.0f : 1.0f); repaint(); return; }
        if (backKey.contains (e.position)) { setSound (m.proc, snd::wtf, 0.0f); repaint(); return; }
        if (knob.contains (e.position))
        {
            start = P (m.proc, snd::wtf);
            turning = true;
            if (auto* x = m.proc.params.getParameter (snd::def (snd::wtf).id))
                x->beginChangeGesture();
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! turning)
            return;
        if (auto* x = m.proc.params.getParameter (snd::def (snd::wtf).id))
            x->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, start - (float) e.getDistanceFromDragStartY() / 240.0f));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (turning)
            if (auto* x = m.proc.params.getParameter (snd::def (snd::wtf).id))
                x->endChangeGesture();
        turning = false;
    }

private:
    Machine& m;
    juce::Rectangle<float> knob, modeKey, backKey;
    float start = 0.0f;
    bool turning = false;
};

//==============================================================================
// CAPTURE's keys: what to capture, a test note, start (stop)
class CaptureStrip : public juce::Component
{
public:
    explicit CaptureStrip (Machine& mm) : m (mm) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        layout();
        const char* names[3] = { "notes", "cycles", "rééchantillonner" };
        const char* tips[3] = { "un multisample, joué dans le sampler", "une banque de cycles pour les oscillateurs du synthé",
                                "ce qui sort des effets, injecté dans le sampler sans coupure" };
        const int kind = m.captureKind;
        for (int i = 0; i < 3; ++i)
        {
            const bool on = i == kind;
            const bool ready = true;
            key (g, kinds[i], on ? KeyState::on : (ready ? KeyState::normal : KeyState::off), false, 10.0f);
            auto k = kinds[i].withTrimmedBottom (3.0f).reduced (14.0f, 8.0f);
            const Picto icons[3] = { Picto::keys, Picto::cycles, Picto::resample };
            auto nameLine = k.removeFromTop (28.0f);
            picto (g, icons[i], nameLine.removeFromRight (30.0f), on ? col::keyOnInk : (ready ? col::ink : col::ink3), 1.6f);
            text (g, juce::String::fromUTF8 (names[i]), nameLine, grotesk (18.0f, 500), on ? col::keyOnInk : (ready ? col::ink : col::ink3));
            k.removeFromTop (2.0f);
            g.setColour (on ? col::keyOnInk.withAlpha (0.7f) : col::ink3);
            g.setFont (mono (11.0f));
            g.drawFittedText (themed (juce::String::fromUTF8 (tips[i])), k.toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
        }
        if (kind == Machine::resampleKind)
        {
            paintResampleKeys (g);
            return;
        }
        const bool sampling = m.proc.isSampling();
        key (g, test, sampling ? KeyState::off : KeyState::normal, false, 10.0f);
        text (g, "tester une note", test.withTrimmedBottom (3.0f), mono (13.0f, 500), sampling ? col::ink3 : col::ink, juce::Justification::centred);
        // the big one
        g.setColour (sampling ? col::well : col::red);
        g.fillRoundedRectangle (go, 12.0f);
        auto t = go.reduced (18.0f, 0.0f);
        g.setColour (sampling ? col::sRed : col::well);
        if (sampling)
            g.fillRect (t.getX(), t.getCentreY() - 7.0f, 14.0f, 14.0f);
        else
            g.fillEllipse (t.getX(), t.getCentreY() - 7.0f, 14.0f, 14.0f);
        text (g, sampling ? "arrêter"_fr : (kind == Machine::cyclesKind ? "capturer les cycles" : "lancer la capture"), t.withTrimmedLeft (24.0f),
              mono (15.0f, 600), sampling ? col::screenText : juce::Colours::white);
        juce::ignoreUnused (r);
    }

    // RÉÉCHANTILLONNER: what the input does (heard through the effects, or not), and the big key, as it goes
    void paintResampleKeys (juce::Graphics& g)
    {
        const int mon = m.proc.options.monitor;
        const char* monNames[3] = { "entrée : coupée", "entrée : directe", "entrée : par les effets" };
        key (g, test, mon == 2 ? KeyState::on : KeyState::normal, false, 10.0f);
        text (g, juce::String::fromUTF8 (monNames[juce::jlimit (0, 2, mon)]), test.withTrimmedBottom (3.0f), mono (12.0f, 500),
              mon == 2 ? col::keyOnInk : col::ink, juce::Justification::centred);
        const auto st = m.proc.player.resampleState();
        const bool busy = st != Player::Resample::idle;
        g.setColour (busy ? col::well : col::red);
        g.fillRoundedRectangle (go, 12.0f);
        if (st == Player::Resample::recording)
        {
            // the bars filling up
            g.setColour (col::red.withAlpha (0.55f));
            g.fillRoundedRectangle (go.withWidth (go.getWidth() * m.proc.player.resampleProgress()), 12.0f);
        }
        auto t = go.reduced (18.0f, 0.0f);
        const bool blink = std::fmod (juce::Time::getMillisecondCounterHiRes(), 600.0) < 300.0;
        juce::String label;
        switch (st)
        {
            case Player::Resample::idle:      label = "rééchantillonner"_fr; break;
            case Player::Resample::armed:     label = "en attente de la mesure · annuler"_fr; break;
            case Player::Resample::recording: label = "enregistre · "_fr + juce::String (m.proc.player.resampleSeconds(), 1) + " s · arrêter"_fr; break;
            case Player::Resample::handing:   label = "injection"; break;
            case Player::Resample::looping:   label = "boucle injectée · arrêter"_fr; break;
        }
        picto (g, st == Player::Resample::looping ? Picto::loop : Picto::resample, t.removeFromLeft (22.0f).withSizeKeepingCentre (22.0f, 22.0f),
               busy ? (st == Player::Resample::armed && blink ? col::sRed.withAlpha (0.4f) : col::sRed) : juce::Colours::white, 1.6f);
        text (g, label, t.withTrimmedLeft (10.0f), mono (15.0f, 600), busy ? col::screenText : juce::Colours::white);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        layout();
        for (int i = 0; i < 3; ++i)
            if (kinds[i].contains (e.position) && ! m.proc.isSampling())
            {
                m.setCaptureKind (i);
                repaint();
                return;
            }
        if (m.captureKind == Machine::resampleKind)
        {
            if (go.contains (e.position))
                m.toggleResample();
            else if (test.contains (e.position))
            {
                auto& o = m.proc.options;
                o.monitor = o.monitor == 2 ? 1 : 2;
                m.proc.settingsChanged();
                m.showMessage (o.monitor == 2 ? "L'entrée passe par les effets : ce que tu entends est ce qui sera enregistré."_fr
                                              : "L'entrée est entendue directement : seul ce que joue REPLIK8 sera enregistré."_fr);
            }
            repaint();
            return;
        }
        if (go.contains (e.position)) { m.toggleSampling(); repaint(); return; }
        if (test.contains (e.position) && ! m.proc.isSampling())
        {
            m.proc.sendTestNote();
            m.showMessage ("Note de test envoyée : C3 vélocité 100."_fr);
        }
    }

private:
    Machine& m;
    juce::Rectangle<float> kinds[3], test, go;

    void layout()
    {
        auto r = getLocalBounds().toFloat();
        auto top = r.removeFromTop (r.getHeight() * 0.56f);
        const float w = (top.getWidth() - 2.0f * 8.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            kinds[i] = { top.getX() + (float) i * (w + 8.0f), top.getY(), w, top.getHeight() };
        r.removeFromTop (10.0f);
        test = r.removeFromLeft (200.0f);
        r.removeFromLeft (10.0f);
        go = r;
    }
};

//==============================================================================
void Machine::PackDrag::mouseDrag (const juce::MouseEvent& e)
{
    if (m.packDragging || e.getDistanceFromDragStart() < 6 || ! m.packReady.existsAsFile() || m.packFor != m.packSignature())
        return;
    m.packDragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ m.packReady.getFullPathName() }, false, &m.packKey,
                                                                [safe = juce::Component::SafePointer<Machine> (&m)] { if (safe != nullptr) safe->packDragging = false; });
}

Machine::Machine (ReplikantProcessor& p) : proc (p), updatePanel (p.updater)
{
    setSize (width, height);
    setWantsKeyboardFocus (true);
    arm.player = &proc.player;
    arm.changed = [this] { armChanged(); };
    zoom = (float) proc.settingsFile().getDoubleValue ("zoom", 1.0);

    // the header
    const char* modeNames[numModes] = { "synth", "drum", "sampler", "radio" };
    const juce::Colour none = juce::Colours::transparentBlack;
    for (int i = 0; i < numModes; ++i)
    {
        auto& k = modeKeys[i];
        k.setButtonText (juce::String::fromUTF8 (modeNames[i]));
        k.look = [this, i] { return mode == i && ! capture ? KeyState::on : KeyState::normal; };
        k.onClick = [this, i] { showMode (i); };
        k.icon = i == synthMode ? Picto::sine : (i == drumMode ? Picto::drum : (i == samplerMode ? Picto::cassette : Picto::radio));
        addAndMakeVisible (k);
    }
    modeKeys[drumMode].setEnabled (false);
    modeKeys[drumMode].setTooltip ("DRUM arrive en 0.4 : 8 pads, tranches, séquenceur, voix générées depuis ton synthé."_fr);
    modeKeys[synthMode].setTooltip ("Deux oscillateurs qui lisent des cycles (les basiques, ou ceux de ton synthé), un sub, du bruit, l'unisson."_fr);
    modeKeys[samplerMode].setTooltip ("L'instrument : ton synthé capturé note par note, ou un son glissé sur la fenêtre."_fr);
    modeKeys[radioMode].setTooltip ("La radio : une station sur internet ou un récepteur ondes courtes, son air, des prises découpées en pads ou envoyées au sampler."_fr);
    captureKey.setButtonText ("capture");
    captureKey.dot = col::red;
    captureKey.look = [this] { return capture ? KeyState::on : KeyState::normal; };
    captureKey.onClick = [this] { showCapture (! capture); };
    captureKey.setTooltip ("Sampler ton synthé : ses notes (pour le sampler) ; bientôt ses cycles (pour le synthé) et ce qui sort des effets."_fr);
    addAndMakeVisible (captureKey);
    playKey.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6"));
    playKey.look = [this] { return proc.clock.isRunning() ? KeyState::accent : KeyState::normal; };
    playKey.onClick = [this] { proc.setClockRunning (! proc.clock.isRunning()); };
    playKey.setTooltip ("L'horloge MIDI interne : lance ou arrête (au tempo, vers la sortie choisie dans RÉGLAGES)."_fr);
    addAndMakeVisible (playKey);
    fileKey.setButtonText ("file");
    fileKey.onClick = [this] { showFileMenu(); };
    addAndMakeVisible (fileKey);
    packKey.setButtonText ("pack .zip");
    packKey.look = [this] { return packReady.existsAsFile() && packFor == packSignature() ? KeyState::accent : KeyState::normal; };
    packKey.onClick = [this]
    {
        if (packMaking || proc.getInstrument() == nullptr || proc.isSampling() || (packReady.existsAsFile() && packFor == packSignature()))
            return;
        packMaking = true;
        packKey.setButtonText ("pack...");
        const auto signature = packSignature();
        juce::Component::SafePointer<Machine> safe (this);
        proc.buildPack ([safe, signature] (const juce::File& zip)
        {
            if (safe == nullptr)
                return;
            safe->packMaking = false;
            safe->packReady = zip;
            safe->packFor = signature;
            safe->packKey.setButtonText (zip.existsAsFile() ? "drag me" : "pack .zip");
            if (zip.existsAsFile())
                safe->showMessage ("Pack prêt : glisse-le vers le Finder, AirDrop ou un mail (il est aussi dans "_fr + zip.getParentDirectory().getFullPathName() + ")");
        });
    };
    packKey.addMouseListener (&packDrag, false);
    packKey.setTooltip ("Le pack à partager (l'instrument, ses samples et son son, en un zip) : clique pour le faire, puis glisse-le vers le Finder, AirDrop ou un mail. Chez un pote : il le glisse sur la fenêtre."_fr);
    addAndMakeVisible (packKey);
    updateKey.setButtonText ("mise à jour"_fr);
    updateKey.look = [] { return KeyState::accent; };
    updateKey.onClick = [this] { updatePanel.showAvailable (proc.updater.getNews (3), (int) proc.updater.getNewer().size()); };
    addChildComponent (updateKey);
    juce::ignoreUnused (none);

    nameEditor.setFont (mono (14.0f, 500));
    nameEditor.setJustification (juce::Justification::centred);
    nameEditor.onReturnKey = [this] { proc.instrumentName = nameEditor.getText(); nameEditor.setVisible (false); repaint(); };
    nameEditor.onEscapeKey = [this] { nameEditor.setVisible (false); };
    nameEditor.onFocusLost = [this] { if (nameEditor.isVisible()) { proc.instrumentName = nameEditor.getText(); nameEditor.setVisible (false); repaint(); } };
    addChildComponent (nameEditor);

    screen = std::make_unique<Screen> (*this);
    addAndMakeVisible (*screen);
    for (int i = 0; i < numPageKeys; ++i)
    {
        pageKeys[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (pageKeys[i]);
    }
    for (int i = 0; i < 8; ++i)
    {
        encoders[i] = std::make_unique<Encoder> (i, arm);
        encoders[i]->onTouch = [this] (const Ctl& c) { touch (c); };
        addAndMakeVisible (*encoders[i]);
    }
    keyboard = std::make_unique<Keyboard> (*this);
    modScenes = std::make_unique<ModScenes> (*this);
    wtfPanel = std::make_unique<WtfPanel> (*this);
    captureStrip = std::make_unique<CaptureStrip> (*this);
    auto say = [this] (const juce::String& t, bool warning) { showMessage (t, warning); };
    radioStrip = std::make_unique<RadioStrip> (proc, say);
    radioTuner = std::make_unique<RadioTuner> (proc, say);
    addAndMakeVisible (*keyboard);
    addAndMakeVisible (*modScenes);
    addAndMakeVisible (*wtfPanel);
    addChildComponent (*captureStrip);
    addChildComponent (*radioStrip);
    addChildComponent (*radioTuner);

    addChildComponent (updatePanel);
    updatePanel.onClose = [this] { updatePanel.setVisible (false); };

    midiDevices = proc.midiOutputs();
    buildPages();
    buildCapturePages();
    buildResamplePages();
    buildCyclePages();

    proc.onMessage = [this] (const juce::String& msg) { showMessage (msg); };
    proc.addChangeListener (this);
    proc.updater.changes.addChangeListener (this);

    // what this version brought, the first time it opens
    {
        auto& set = proc.settingsFile();
        const auto nowVersion = Updater::currentVersion();
        const auto before = set.getValue ("lastRunVersion");
        if (before.isNotEmpty() && before != nowVersion)
            pendingNews = Updater::summarise (nowVersion, juce::String::fromUTF8 (RkNotesData::RELEASE_NOTES_md, RkNotesData::RELEASE_NOTES_mdSize));
        if (before != nowVersion)
        {
            set.setValue ("lastRunVersion", nowVersion);
            set.saveIfNeeded();
        }
    }
    for (int m = 0; m < numModes; ++m)
        page[m] = juce::jlimit (0, juce::jmax (0, (int) pages[m].size() - 1), proc.settingsFile().getIntValue ("page" + juce::String (m), 0));
    captureKind = juce::jlimit ((int) notesKind, (int) resampleKind, proc.settingsFile().getIntValue ("captureKind", notesKind));
    shownResample = proc.lastResample;
    const int engine = (int) P (proc, snd::engine);
    mode = engine == snd::drum ? samplerMode : engine;
    showPage (page[mode]);
    refreshUpdate();
    proc.updater.checkIfDue();
    startTimerHz (30);
}

Machine::~Machine()
{
    stopTimer();
    proc.captureOpen = false;
    proc.onMessage = nullptr;
    proc.removeChangeListener (this);
    proc.updater.changes.removeChangeListener (this);
    packKey.removeMouseListener (&packDrag);
}

//==============================================================================
std::vector<Machine::Page>& Machine::pagesShown()
{
    if (! capture)
        return pages[mode];
    return captureKind == resampleKind ? resamplePages : (captureKind == cyclesKind ? cyclePages : capturePages);
}
int& Machine::pageIndex()
{
    if (! capture)
        return page[mode];
    return captureKind == resampleKind ? resamplePage : (captureKind == cyclesKind ? cyclePage : capturePage);
}

void Machine::buildPages()
{
    auto S = [this] (int id, const char* label = nullptr, std::function<bool()> shown = nullptr) { return soundCtl (proc, id, label, std::move (shown)); };
    auto Pv = [this] (int id) { return [this, id] { return P (proc, id); }; };
    auto elastic = [this] { return P (proc, snd::playMode) > 0.5f; };
    auto fOn = [this] { return P (proc, snd::fModel) > 0.5f; };
    auto disabledUnless = [] (Ctl c, std::function<bool()> when) { c.enabled = std::move (when); return c; };
    juce::ignoreUnused (Pv);

    // the pages every mode shares: the filter, its envelope and the drive; the effects; the mods; the settings
    auto filterPage = [&]
    {
        Page pg { "filter", "filter", {} };
        pg.banks.push_back ({ "filter", { as (S (snd::fModel), Ctl::Look::display), disabledUnless (S (snd::fType), fOn), sep (disabledUnless (S (snd::cutoff), fOn)),
                                          disabledUnless (S (snd::reso), fOn), disabledUnless (S (snd::fDrive), fOn),
                                          sep (S (snd::sep, nullptr, [this] { const int md = (int) P (proc, snd::fModel); return md == 2 || md == 4; })),
                                          disabledUnless (S (snd::fm), fOn), sep (disabledUnless (S (snd::quality), fOn)) } });
        pg.banks.push_back ({ "filter env", { S (snd::fEnv), sep (S (snd::fAttack)), S (snd::fDecay), S (snd::fSustain), S (snd::fRelease),
                                               sep (S (snd::keyTrack)), S (snd::velCutoff) } });
        pg.banks.push_back ({ "drive", { S (snd::dModel), sep (S (snd::dAmt)), S (snd::dTone), sep (S (snd::dMix)) } });
        return pg;
    };
    auto fxPage = [&]
    {
        // (each effect's on and off: the switch on its block, in the chain on the screen)
        Page pg { "fx", "fx", {} };
        pg.banks.push_back ({ "drive", { S (snd::dModel), sep (S (snd::dAmt)), S (snd::dTone), sep (S (snd::dMix)) } });
        pg.banks.push_back ({ "spectre", { S (snd::spVoices), S (snd::spSlice), S (snd::spBlur), S (snd::spGlide), sep (S (snd::spCentre)), S (snd::spWidth), sep (S (snd::spMix)) } });
        pg.banks.push_back ({ "spectre +", { S (snd::spOct), S (snd::spWave), sep (S (snd::spFreeze)), S (snd::spFeedback) } });
        pg.banks.push_back ({ "pitch", { S (snd::piA), S (snd::piB), S (snd::piDetune), sep (S (snd::piDelay)), S (snd::piLevelB), S (snd::piFeedback), sep (S (snd::piMix)) } });
        pg.banks.push_back ({ "chorus", { S (snd::chMode), sep (S (snd::chRate)), S (snd::chDepth), S (snd::chFeedback), S (snd::chTone), sep (S (snd::chMix)), sep (S (snd::piCrystals)) } });
        pg.banks.push_back ({ "crush", { S (snd::crBits), S (snd::crRate), S (snd::crJitter), sep (S (snd::crTone)), sep (S (snd::crMix)) } });
        pg.banks.push_back ({ "espace", { S (snd::rvSize), S (snd::rvDecay), S (snd::rvTone), sep (S (snd::rvShimmer)), S (snd::rvMod), sep (S (snd::rvMix)) } });
        return pg;
    };
    // ORBITE: the carrier and how it's wired, then each satellite
    auto orbitePage = [&]
    {
        auto fm = [this] { return (int) P (proc, snd::orAlgo) == 2; };
        auto ring = [this] { return (int) P (proc, snd::orAlgo) != 2; };
        Page pg { "orbite", "orbite", {} };
        pg.banks.push_back ({ "carrier", { S (snd::orOn, "On"), S (snd::orAlgo), sep (S (snd::orShape)), S (snd::orSym), sep (S (snd::orOct)), S (snd::orSemi),
                                            sep (S (snd::orOrbit)), S (snd::orTrack) } });
        pg.banks.push_back ({ "sat 1", { S (snd::s1Wave), S (snd::s1Ratio), S (snd::s1Sym), sep (S (snd::s1Ring, nullptr, ring)), sep (S (snd::s1Ring, "FM depth", fm)),
                                         sep (S (snd::s1Sync)) } });
        pg.banks.push_back ({ "sat 2", { S (snd::s2Wave), S (snd::s2Ratio), S (snd::s2Sym), sep (S (snd::s2Ring)), sep (S (snd::s2Sync)), S (snd::s2Phase) } });
        return pg;
    };
    // the MIXEUR, before the filter: every source's level (and ORBITE's switch, at hand)
    auto mixerPage = [&] (bool sampler)
    {
        Page pg { "mixer", "mix", {} };
        if (sampler)
        {
            pg.banks.push_back ({ "mixer", { S (snd::mxSample), sep (S (snd::mxOrbite)), S (snd::orOn, "On"), sep (S (snd::mxSub)), S (snd::subOn, "On") } });
            pg.banks.push_back ({ "sub", { S (snd::subOn, "On"), S (snd::subMode), S (snd::subOct), sep (S (snd::subXover)), S (snd::subReplace),
                                           sep (S (snd::subDrive)), S (snd::subFollow, nullptr, [this] { return (int) P (proc, snd::subMode) == 0; }),
                                           sep (S (snd::mxSub, "Level")) } });
        }
        else
            pg.banks.push_back ({ "mixer", { S (snd::oaLevel, "Osc A"), S (snd::obLevel, "Osc B"), S (snd::subLevel), S (snd::noiseLevel),
                                              sep (S (snd::mxOrbite)), S (snd::orOn, "On") } });
        return pg;
    };
    auto modsPage = [&]
    {
        Page pg { "mods", "mods", {} };
        using dsp::ModType;
        for (int mi = 0; mi < snd::numMods; ++mi)
        {
            auto type = [this, mi] { return (ModType) (int) P (proc, snd::modParam (mi, snd::mType)); };
            auto is = [type] (std::initializer_list<ModType> ts)
            {
                std::vector<ModType> v (ts);
                return std::function<bool()> ([type, v] { return std::find (v.begin(), v.end(), type()) != v.end(); });
            };
            auto periodic = is ({ ModType::wave, ModType::steps, ModType::random });
            auto M = [mi] (snd::ModSetting w) { return snd::modParam (mi, w); };
            Bank b { "m" + juce::String (mi + 1), {} };
            b.ctls.push_back (S (M (snd::mType)));
            b.ctls.push_back (S (M (snd::mShape), nullptr, is ({ ModType::wave })));
            b.ctls.push_back (S (M (snd::mLen), nullptr, is ({ ModType::steps, ModType::random })));
            b.ctls.push_back (S (M (snd::mVari), nullptr, is ({ ModType::random })));
            b.ctls.push_back (sep (S (M (snd::mSync), nullptr, periodic)));
            b.ctls.push_back (S (M (snd::mDiv), nullptr, [this, periodic, mi] { return periodic() && P (proc, snd::modParam (mi, snd::mSync)) < 0.5f; }));
            b.ctls.push_back (S (M (snd::mHz), nullptr, [this, periodic, mi] { return periodic() && P (proc, snd::modParam (mi, snd::mSync)) > 0.5f; }));
            b.ctls.push_back (S (M (snd::mSmooth), nullptr, is ({ ModType::wave, ModType::steps })));
            b.ctls.push_back (S (M (snd::mPolarity), nullptr, periodic));
            b.ctls.push_back (S (M (snd::mRetrig), nullptr, is ({ ModType::wave, ModType::steps })));
            b.ctls.push_back (S (M (snd::mSource), nullptr, is ({ ModType::follower })));
            b.ctls.push_back (S (M (snd::mGain), nullptr, is ({ ModType::follower })));
            b.ctls.push_back (S (M (snd::mAttack), nullptr, is ({ ModType::envelope, ModType::follower })));
            b.ctls.push_back (S (M (snd::mDecay), nullptr, is ({ ModType::envelope })));
            b.ctls.push_back (S (M (snd::mSustain), nullptr, is ({ ModType::envelope })));
            b.ctls.push_back (S (M (snd::mRelease), nullptr, is ({ ModType::envelope, ModType::follower })));
            b.ctls.push_back (sep (S (M (snd::mAmt))));
            pg.banks.push_back (std::move (b));
        }
        return pg;
    };
    auto settingsPage = [&]
    {
        Page pg { "settings", "settings", {} };
        auto& o = proc.options;
        // (the scenes and their fader: in their own panel, under the screen)
        Bank b { "settings", { S (snd::tempo) } };
        auto clockMode = choiceCtl ("Clock", { "internal", "host" }, [this] { return (int) proc.clock.getMode(); },
                                    [this] (int v) { proc.setClockMode ((MidiClock::Mode) v); });
        clockMode.tip = "INTERNAL : au tempo, lancée par la touche lecture en haut. HOST : démarre et s'arrête avec le DAW, sur sa grille."_fr;
        clockMode.sepBefore = true;
        b.ctls.push_back (clockMode);
        juce::StringArray dests { "none", "daw" };
        for (auto& d : midiDevices)
            dests.add (lower (d.name));
        b.ctls.push_back (choiceCtl ("Clock out", dests, [this]
        {
            const auto id = proc.clock.destinationId();
            if (id == "host") return 1;
            for (int i = 0; i < midiDevices.size(); ++i)
                if (midiDevices[i].identifier == id) return i + 2;
            return 0;
        }, [this] (int v)
        {
            if (v == 0) proc.setClockDestination ({}, {});
            else if (v == 1) proc.setClockDestination ("host", "DAW");
            else if (v - 2 < midiDevices.size()) proc.setClockDestination (midiDevices[v - 2].identifier, midiDevices[v - 2].name);
        }));
        auto monitor = choiceCtl ("Monitor", { "off", "direct", "thru fx" }, [&o] { return o.monitor; }, [this, &o] (int v) { o.monitor = v; proc.settingsChanged(); });
        monitor.sepBefore = true;
        monitor.tip = "L'audio qui arrive (le synthé, ou la piste où REPLIK8 est en insert). OFF : coupé. DIRECT : tel quel, pendant la capture (page CAPTURE ouverte). THRU FX : passé dans le filtre et les effets (multi-effet, avec les mods)."_fr;
        b.ctls.push_back (monitor);
        b.ctls.push_back (choiceCtl ("Keys to", { "replik8", "synth" }, [&o] { return o.thru ? 1 : 0; }, [this, &o] (int v) { o.thru = v == 1; proc.settingsChanged(); }));
        pg.banks.push_back (std::move (b));
        return pg;
    };
    auto amp = [&] (bool withVoices)
    {
        Page pg { "env", "adsr", {} };
        Bank b { "amp env", { S (snd::attack), S (snd::decay), S (snd::sustain), S (snd::release), sep (S (snd::velocity)), S (snd::volume) } };
        if (withVoices)
        {
            b.ctls.push_back (sep (S (snd::voiceMode)));
            b.ctls.push_back (S (snd::voices, nullptr, [this] { return P (proc, snd::voiceMode) < 0.5f; }));
        }
        pg.banks.push_back (std::move (b));
        return pg;
    };

    // SYNTHÉ
    {
        auto& v = pages[synthMode];
        v.clear();
        // (their levels, the sub's and the noise's: on the MIXEUR)
        v.push_back ({ "osc a", "osc", { { "osc a", { S (snd::oaBank), S (snd::oaPos), sep (S (snd::oaOct)), S (snd::oaSemi), S (snd::oaFine), sep (S (snd::pmBA)) } } } });
        v.push_back ({ "osc b", "osc", { { "osc b", { S (snd::obBank), S (snd::obPos), sep (S (snd::obOct)), S (snd::obSemi), S (snd::obFine) } } } });
        v.push_back (orbitePage());
        v.push_back (mixerPage (false));
        v.push_back ({ "voices", "voices", { { "voices", { S (snd::voiceMode), S (snd::voices, nullptr, [this] { return P (proc, snd::voiceMode) < 0.5f; }),
                                                       S (snd::glide), sep (S (snd::unison)), S (snd::detune), S (snd::width), sep (S (snd::bend)), S (snd::transpose) } } } });
        v.push_back (filterPage());
        v.push_back (amp (false));
        v.push_back (fxPage());
        v.push_back (modsPage());
        v.push_back (settingsPage());
    }
    // DRUM (0.4): the shared pages for now
    {
        auto& v = pages[drumMode];
        v.clear();
        v.push_back (filterPage());
    }
    // SAMPLER
    {
        auto& v = pages[samplerMode];
        v.clear();
        v.push_back ({ "play", "lecture", { { "play", { S (snd::playMode), S (snd::start), S (snd::speed, nullptr, elastic), S (snd::grain, nullptr, elastic),
                                                              S (snd::spray, nullptr, elastic), S (snd::transients, nullptr, [elastic] { return ! elastic(); }),
                                                              sep (S (snd::reverse)), sep (S (snd::alias)) } } } });
        v.push_back ({ "loop", "loop", { { "loop", { S (snd::loops), sep (S (snd::loopStart)), S (snd::loopSize), S (snd::loopXfade), sep (S (snd::bits)) } } } });
        v.push_back ({ "pitch", "pitch", { { "pitch", { S (snd::tune), S (snd::transpose), S (snd::fine), S (snd::glide), S (snd::bend), sep (S (snd::pmSource)),
                                                            S (snd::pmRatio, nullptr, [this] { const int s = (int) P (proc, snd::pmSource); return s == 1 || s == 2; }),
                                                            S (snd::pmIndex, nullptr, [this] { return (int) P (proc, snd::pmSource) != 0; }) } } } });
        v.push_back (orbitePage());
        v.push_back (mixerPage (true));
        v.push_back (filterPage());
        v.push_back (amp (true));
        v.push_back (fxPage());
        v.push_back (modsPage());
        v.push_back (settingsPage());
    }
    // RADIO
    {
        auto modeIs = [this] (int md) { return std::function<bool()> ([this, md] { return (int) P (proc, snd::rdMode) == md; }); };
        auto loops = [this] { return (int) P (proc, snd::rdMode) != radio::modeLive; };
        auto rxNow = [this] { return juce::jlimit (0, radio::numRx - 1, (int) P (proc, snd::rdRx)); };
        auto shortwave = [this, rxNow] { return proc.hasRadioNet() && proc.radioNet().getSource (rxNow()).kind == 2; };
        auto& v = pages[radioMode];
        v.clear();
        // TUNE: the receiver listened to (the tuner on the screen chooses its station, or its shortwave receiver), and a
        // shortwave receiver's frequency and mode at hand
        Bank tb { "tune", { S (snd::rdOn), S (snd::rdRx) } };
        auto f = numberCtl ("Frequency", 10.0, 30000.0, 0.0,
                            [this, rxNow] { return proc.hasRadioNet() ? proc.radioNet().getSource (rxNow()).khz : 9420.0; },
                            [this, rxNow] (double khz)
                            {
                                auto s = proc.radioNet().getSource (rxNow());
                                if (s.kind != 2)
                                    return;
                                s.khz = std::round (khz * 10.0) / 10.0;
                                proc.radioNet().setSource (rxNow(), s);
                            },
                            [] (double khz) { return juce::String (khz, khz < 10000.0 ? 1 : 0) + " khz"; }, true);
        f.shown = shortwave;
        f.dragPixels = 4000.0f;
        f.sepBefore = true;
        f.tip = "La fréquence du récepteur ondes courtes (glisse lentement : 4000 pixels pour toute la bande ; le tuner a ses pas de 0,1 à 5 kHz)."_fr;
        tb.ctls.push_back (f);
        auto md = choiceCtl ("Demod", { "am", "usb", "lsb", "cw" },
                             [this, rxNow]
                             {
                                 const auto m = proc.hasRadioNet() ? proc.radioNet().getSource (rxNow()).mode : juce::String ("am");
                                 return juce::jmax (0, juce::StringArray { "am", "usb", "lsb", "cw" }.indexOf (m));
                             },
                             [this, rxNow] (int i)
                             {
                                 auto s = proc.radioNet().getSource (rxNow());
                                 if (s.kind != 2)
                                     return;
                                 s.mode = juce::StringArray { "am", "usb", "lsb", "cw" }[juce::jlimit (0, 3, i)];
                                 proc.radioNet().setSource (rxNow(), s);
                             });
        md.shown = shortwave;
        md.tip = "AM : la radiodiffusion. USB, LSB : la bande latérale (les radioamateurs au-dessus et au-dessous de 10 MHz). CW : le morse."_fr;
        tb.ctls.push_back (md);
        tb.ctls.push_back (sep (S (snd::rdLevel)));
        tb.ctls.push_back (S (snd::rdStatic));
        v.push_back ({ "tune", "tune", { tb } });
        v.push_back ({ "listen", "listen", { { "listen", { S (snd::rdOn), sep (S (snd::rdMode)), S (snd::rdTune), S (snd::rdLen, nullptr, loops),
                                                              S (snd::rdSquelch, nullptr, modeIs (radio::modeLive)), S (snd::rdGrab, nullptr, modeIs (radio::modeLoop)),
                                                              S (snd::rdDepth, nullptr, modeIs (radio::modeDig)), sep (S (snd::rdLevel)), S (snd::rdStatic) } } } });
        v.push_back ({ "air", "air", { { "air", { S (snd::rdBand), S (snd::rdCentre), sep (S (snd::rdFade)), S (snd::rdRate), sep (S (snd::rdWhistle)),
                                                    S (snd::rdCrackle), sep (S (snd::rdDrive)), S (snd::rdStatic) } } } });
        v.push_back ({ "take", "take", { { "take", { S (snd::rdTake), sep (S (snd::rdSens)), S (snd::rdPads) } } } });
        v.push_back ({ "pads", "pads", { { "pads", { S (snd::rdPadTune), S (snd::rdPadDecay), sep (S (snd::rdChoke)), sep (S (snd::rdPadLevel)) } } } });
        v.push_back (filterPage());
        v.push_back (fxPage());
        v.push_back (modsPage());
        v.push_back (settingsPage());
        auto& rv = pages[radioMode];
        auto rtip = [&rv] (int id, const juce::String& t)
        {
            for (auto& pg : rv)
                for (auto& b : pg.banks)
                    for (auto& c : b.ctls)
                        if (c.param == id)
                            c.tip = t;
        };
        rtip (snd::rdOn, "ON AIR : la radio écoutée. OFF : elle se tait (et lâche sa connexion au bout de 45 s), la prise et ses pads restent jouables."_fr);
        rtip (snd::rdRx, "Le récepteur écouté : quatre, chacun accordé dans la page TUNE (une station, ou une fréquence sur un récepteur ondes courtes). Seul celui qu'on écoute reste connecté."_fr);
        rtip (snd::rdMode, "LIVE : ce que joue le récepteur, maintenant. LOOP : sa dernière LENGTH rejouée en mesure, une nouvelle prise au hasard (GRAB). DIG : à chaque tour, LENGTH prise plus loin en arrière (DEPTH)."_fr);
        rtip (snd::rdTune, "En LIVE : le son décalé vers le haut ou le bas comme par un récepteur accordé à côté (±600 Hz, la voix de canard d'une BLU mal calée). En LOOP et DIG : la transposition, comme une bande."_fr);
        rtip (snd::rdLen, "La boucle, du seizième à deux mesures, au tempo."_fr);
        rtip (snd::rdSquelch, "Seul ce qui est assez fort passe (de -66 à -10 dB) : sur une voix, il hache entre les mots."_fr);
        rtip (snd::rdGrab, "La chance qu'un tour prenne une nouvelle boucle. KEEP : la première reste. À fond : une nouvelle à chaque tour."_fr);
        rtip (snd::rdDepth, "Jusqu'où DIG va chercher en arrière : de 1 à 20 s."_fr);
        rtip (snd::rdStatic, "Le souffle et les craquements de l'air : ce qu'on entend seul quand rien n'arrive."_fr);
        rtip (snd::rdBand, "La largeur du récepteur, de FULL à 250 Hz (un filtre à quatre pôles)."_fr);
        rtip (snd::rdCentre, "Où la bande se place quand elle se resserre, de 300 Hz à 3 kHz."_fr);
        rtip (snd::rdFade, "Le signal qui s'en va et revient, une encoche qui le traverse : le fading des ondes courtes."_fr);
        rtip (snd::rdRate, "La vitesse du fading."_fr);
        rtip (snd::rdWhistle, "Le sifflement d'une porteuse voisine, qui erre lentement."_fr);
        rtip (snd::rdCrackle, "Des craquements ; au-delà de 70 %, les fracas d'un orage lointain."_fr);
        rtip (snd::rdDrive, "Le détecteur et le petit haut-parleur poussés."_fr);
        rtip (snd::rdTake, "Jusqu'où GRAB remonte dans ce qui a été entendu (l'air compris) : de 1 à 30 s."_fr);
        rtip (snd::rdSens, "La sensibilité aux attaques : plus haut, plus de coups trouvés (et des pads plus courts)."_fr);
        rtip (snd::rdPads, "Combien de pads : les attaques les plus fortes, dans l'ordre de la prise, chacune jusqu'à la suivante."_fr);
        rtip (snd::rdPadTune, "La hauteur des pads (±24 demi-tons)."_fr);
        rtip (snd::rdPadDecay, "Leur décroissance. HOLD : chaque pad joue jusqu'au bout de sa tranche."_fr);
        rtip (snd::rdChoke, "ON : un pad coupe celui qui joue (comme un charleston)."_fr);
        rtip (snd::rdPadLevel, "Le niveau des pads."_fr);
    }

    // the tips of the sound's settings
    auto tip = [] (std::vector<Page>& v, int id, const juce::String& t)
    {
        for (auto& pg : v)
            for (auto& b : pg.banks)
                for (auto& c : b.ctls)
                    if (c.param == id)
                        c.tip = t;
    };
    for (auto* v : { &pages[synthMode], &pages[samplerMode] })
    {
        tip (*v, snd::oaPos, "Où lire dans la banque : les cycles se suivent, la position passe de l'un à l'autre en fondu (à moduler)."_fr);
        tip (*v, snd::oaBank, "BASIQUES : du sinus à la scie, au carré, aux impulsions, à la synchro et aux formants. CAPTURÉS : les cycles de ton synthé (CAPTURE CYCLES)."_fr);
        tip (*v, snd::pmBA, "L'oscillateur B pousse la phase de A (modulation de phase, la FM des DX) : des harmoniques qui grincent ou qui chantent."_fr);
        tip (*v, snd::alias, "0 : la lecture HQ, sans repliement. Puis l'interpolation se relâche, devient linéaire, puis disparaît, jusqu'aux alias plus forts que le son."_fr);
        tip (*v, snd::loopXfade, "Un fondu entre la fin de la boucle et ce qui précède son début : un raccord qui ne claque pas, même sur une boucle prise au hasard."_fr);
        tip (*v, snd::transients, "GARDÉS : l'attaque de chaque sample est jouée à sa vitesse, le corps seul est transposé : un kick descendu de deux octaves garde son clic."_fr);
        tip (*v, snd::quality, "Le suréchantillonnage du filtre (4X : le plus propre)."_fr);
        tip (*v, snd::unison, "Jusqu'à quatre voix par note, désaccordées et étalées dans la stéréo."_fr);
        tip (*v, snd::orOn, "ORBITE : un porteur et deux satellites synchronisés qui le modulent en anneau. Allume-le, puis règle son niveau dans le MIXEUR."_fr);
        tip (*v, snd::orAlgo, "Le câblage. PARALLÈLE : les deux satellites sur le porteur. CASCADE : le 2 module le 1, qui module le porteur. FM : le 1 pousse la phase du porteur. BOUCLE : la sortie revient dans les satellites."_fr);
        tip (*v, snd::orShape, "La forme du porteur : triangle, scie, carré, et tout ce qu'il y a entre."_fr);
        tip (*v, snd::orOrbit, "Le satellite 2 tourne autour du porteur : le timbre bouge, la note reste juste. FIXE : il ne tourne pas."_fr);
        tip (*v, snd::orTrack, "OUI : les satellites suivent la note. NON : ils restent fixes, comme des formants, et le timbre change d'une touche à l'autre."_fr);
        tip (*v, snd::s1Ring, "Combien le satellite multiplie le porteur (modulation en anneau, harmonique tant qu'il est synchronisé)."_fr);
        tip (*v, snd::s2Ring, "Combien le satellite multiplie le porteur (modulation en anneau, harmonique tant qu'il est synchronisé)."_fr);
        tip (*v, snd::s1Sync, "OUI : il repart avec chaque cycle du porteur, le son reste sur la note. NON : métallique, inharmonique."_fr);
        tip (*v, snd::s2Sync, "OUI : il repart avec chaque cycle du porteur, le son reste sur la note. NON : métallique, inharmonique."_fr);
        tip (*v, snd::s2Phase, "Où le satellite 2 repart dans son cycle : une autre couleur, sans changer la note."_fr);
        tip (*v, snd::mxSample, "Le niveau du sample, avant le filtre."_fr);
        tip (*v, snd::subOn, "SUB : un grave propre sous le sample, même transposé très bas. Il ne descend jamais sous 25 Hz."_fr);
        tip (*v, snd::subMode, "SINE : un sinus sur la note, une ou deux octaves en dessous. OCTAVE : le sample lui-même relu une ou deux octaves plus bas, en grains calés sur son temps."_fr);
        tip (*v, snd::subXover, "La fréquence où le sample laisse la place au sub."_fr);
        tip (*v, snd::subReplace, "Combien du grave du sample (étiré, souvent boueux une fois transposé) est retiré sous le crossover : à 100 %, seul le sub y reste."_fr);
        tip (*v, snd::subDrive, "Réchauffe le sub (des harmoniques, gardées sous le crossover) : on l'entend aussi sur de petites enceintes."_fr);
        tip (*v, snd::subFollow, "ON : le sinus suit le niveau du sample (il frappe et s'éteint avec lui). OFF : il tient, l'enveloppe ENV seule le façonne."_fr);
        tip (*v, snd::mxOrbite, "Le niveau d'ORBITE, avant le filtre (s'il est allumé)."_fr);
        tip (*v, snd::pmSource, "Ce qui pousse la tête de lecture : un sinus, le sample lui-même, l'entrée, RETOUR (la voix se module elle-même) ou ORBITE."_fr);
    }
}

void Machine::buildCapturePages()
{
    auto& s = proc.session;
    auto& o = proc.options;
    auto changed = [this] { proc.settingsChanged(); };
    auto mapping = [this] { proc.mappingChanged(); };
    auto num = [&] (const juce::String& n, double lo, double hi, double st, std::function<double()> g, std::function<void (double)> set,
                    std::function<juce::String (double)> f, bool log = false, bool map = false)
    {
        auto c = numberCtl (n, lo, hi, st, std::move (g), std::move (set), std::move (f), log);
        c.changed = map ? std::function<void()> (mapping) : std::function<void()> (changed);
        c.enabled = [this] { return ! proc.isSampling(); };
        return c;
    };
    auto choice = [&] (const juce::String& n, juce::StringArray ch, std::function<int()> g, std::function<void (int)> set, bool map = false)
    {
        auto c = choiceCtl (n, std::move (ch), std::move (g), std::move (set));
        c.changed = map ? std::function<void()> (mapping) : std::function<void()> (changed);
        c.enabled = [this] { return ! proc.isSampling(); };
        return c;
    };
    auto ms = [] (double v) { return seconds (v); };
    capturePages.clear();
    {
        Page pg { "source", "plan", {} };
        Bank b { "source", {} };
        b.ctls.push_back (choice ("Source", { "input", "sidechain", "test synth" }, [&o] { return (int) o.source; }, [&o] (int v) { o.source = (ReplikantProcessor::Source) v; }));
        b.ctls.back().tip = "D'où vient le son du synthé : l'entrée du plug-in (ou de la carte son), le sidechain, ou le petit synthé intégré pour essayer."_fr;
        juce::StringArray ports { "aucune", "daw" };
        for (auto& d : midiDevices)
            ports.add (lower (d.name));
        b.ctls.push_back (choice ("MIDI out", ports, [this]
        {
            if (proc.options.midiPort == "host") return 1;
            for (int i = 0; i < midiDevices.size(); ++i)
                if (midiDevices[i].identifier == proc.options.midiPort) return i + 2;
            return 0;
        }, [this] (int v)
        {
            if (v == 0) proc.setMidiPort ({}, {});
            else if (v == 1) proc.setMidiPort ("host", "DAW");
            else if (v - 2 < midiDevices.size()) proc.setMidiPort (midiDevices[v - 2].identifier, midiDevices[v - 2].name);
        }));
        b.ctls.back().tip = "Le port MIDI qui va au synthé. DAW : la sortie MIDI du plug-in (si ton DAW la route)."_fr;
        b.ctls.push_back (num ("Channel", 1, 16, 1, [&o] { return (double) o.channel; }, [&o] (double v) { o.channel = (int) v; }, intText));
        b.ctls.push_back (num ("Program", -1, 127, 1, [&o] { return (double) o.program; }, [&o] (double v) { o.program = (int) v; },
                               [] (double v) { return v < 0 ? juce::String ("none") : juce::String ((int) v + 1); }));
        b.ctls.push_back (choice ("Monitor", { "off", "direct", "thru fx" }, [&o] { return o.monitor; }, [&o] (int v) { o.monitor = v; }));
        b.ctls.push_back (choice ("Files", { "24 bit", "32 bit float" }, [&s] { return s.fileBits == 32 ? 1 : 0; }, [&s] (int v) { s.fileBits = v == 1 ? 32 : 24; }));
        pg.banks.push_back (std::move (b));
        capturePages.push_back (std::move (pg));
    }
    {
        Page pg { "notes", "plan", {} };
        Bank b { "notes", {} };
        b.ctls.push_back (choice ("Mode", { "interval", "count", "custom" }, [&s] { return (int) s.noteMode; },
                                  [&s] (int v) { s.noteMode = (rk::NoteMode) v; if (s.noteMode == rk::NoteMode::custom && s.customNotes.empty()) s.customNotes = rk::plannedNotes (rk::Session { s }); }));
        b.ctls.push_back (num ("From", 0, 127, 1, [&s] { return (double) s.lowNote; }, [&s] (double v) { s.lowNote = (int) v; if (s.highNote < s.lowNote) s.highNote = s.lowNote; }, noteText));
        b.ctls.push_back (num ("To", 0, 127, 1, [&s] { return (double) s.highNote; }, [&s] (double v) { s.highNote = (int) v; if (s.lowNote > s.highNote) s.lowNote = s.highNote; }, noteText));
        auto iv = num ("Every", 1, 24, 1, [&s] { return (double) s.interval; }, [&s] (double v) { s.interval = (int) v; }, semis);
        iv.shown = [&s] { return s.noteMode == rk::NoteMode::interval; };
        b.ctls.push_back (iv);
        auto nc = num ("Count", 1, 64, 1, [&s] { return (double) s.noteCount; }, [&s] (double v) { s.noteCount = (int) v; }, intText);
        nc.shown = [&s] { return s.noteMode == rk::NoteMode::count; };
        b.ctls.push_back (nc);
        b.ctls.push_back (num ("Layers", 1, 16, 1, [&s] { return (double) s.layers; }, [&s] (double v) { s.layers = (int) v; }, intText));
        b.ctls.push_back (choice ("Curve", { "linear", "soft", "hard" }, [&s] { return juce::jmin (2, (int) s.curve); }, [&s] (int v) { s.curve = (rk::VelCurve) v; }));
        b.ctls.push_back (num ("Round robin", 1, 8, 1, [&s] { return (double) s.roundRobins; }, [&s] (double v) { s.roundRobins = (int) v; },
                               [] (double v) { return juce::String ((int) v) + ((int) v > 1 ? " prises" : " prise"); }));
        pg.banks.push_back (std::move (b));
        capturePages.push_back (std::move (pg));
    }
    {
        Page pg { "timing", "plan", {} };
        Bank b { "timing", {} };
        b.ctls.push_back (choice ("Play", { "hold", "one-shot" }, [&s] { return s.oneShot ? 1 : 0; }, [&s] (int v) { s.oneShot = v == 1; }));
        b.ctls.push_back (num ("Hold", 0.05, 30.0, 0.0, [&s] { return s.hold; }, [&s] (double v) { s.hold = v; }, ms, true));
        b.ctls.push_back (num ("Max release", 0.0, 30.0, 0.05, [&s] { return s.releaseMax; }, [&s] (double v) { s.releaseMax = v; }, ms));
        b.ctls.push_back (num ("Pause", 0.0, 5.0, 0.05, [&s] { return s.pause; }, [&s] (double v) { s.pause = v; }, ms));
        pg.banks.push_back (std::move (b));
        capturePages.push_back (std::move (pg));
    }
    {
        Page pg { "loops", "plan", {} };
        Bank b { "loops", {} };
        auto loopOn = [&s] { return s.loop && ! s.oneShot; };
        b.ctls.push_back (choice ("Loop", { "auto", "none" }, [&s] { return s.loop ? 0 : 1; }, [&s] (int v) { s.loop = v == 0; }));
        auto a = num ("Search from", 0.05, 0.9, 0.01, [&s] { return s.loopSettings.searchStart; },
                      [&s] (double v) { s.loopSettings.searchStart = v; if (s.loopSettings.searchEnd < v + 0.05) s.loopSettings.searchEnd = v + 0.05; }, pct);
        a.shown = loopOn;
        b.ctls.push_back (a);
        auto e = num ("Search to", 0.1, 1.0, 0.01, [&s] { return s.loopSettings.searchEnd; },
                      [&s] (double v) { s.loopSettings.searchEnd = v; if (s.loopSettings.searchStart > v - 0.05) s.loopSettings.searchStart = v - 0.05; }, pct);
        e.shown = loopOn;
        b.ctls.push_back (e);
        auto l = num ("Min length", 0.01, 10.0, 0.0, [&s] { return s.loopSettings.minSeconds; }, [&s] (double v) { s.loopSettings.minSeconds = v; }, ms, true);
        l.shown = loopOn;
        b.ctls.push_back (l);
        auto f = num ("Crossfade", 0.0, 1.0, 0.005, [&s] { return s.loopSettings.crossfadeSeconds; }, [&s] (double v) { s.loopSettings.crossfadeSeconds = v; }, ms);
        f.shown = loopOn;
        b.ctls.push_back (f);
        pg.banks.push_back (std::move (b));
        capturePages.push_back (std::move (pg));
    }
    {
        Page pg { "attack", "plan", {} };
        Bank b { "attack", {} };
        b.ctls.push_back (choice ("Align", { "per note", "fixed latency" }, [&s] { return (int) s.align; }, [&s] (int v) { s.align = (rk::Align) v; }));
        b.ctls.push_back (num ("Threshold", 3.0, 40.0, 1.0, [&s] { return s.thresholdDb; }, [&s] (double v) { s.thresholdDb = v; },
                               [] (double v) { return "+" + juce::String ((int) v) + " db"; }));
        b.ctls.push_back (num ("Pre-roll", 0.0, 20.0, 0.5, [&s] { return s.preMs; }, [&s] (double v) { s.preMs = v; },
                               [] (double v) { return juce::String (v, 1) + " ms"; }));
        pg.banks.push_back (std::move (b));
        capturePages.push_back (std::move (pg));
    }
    {
        Page pg { "level", "plan", {} };
        Bank b { "level", {} };
        b.ctls.push_back (choice ("Normalize", { "instrument", "per sample", "off" }, [&s] { return (int) s.normalise; }, [&s] (int v) { s.normalise = (rk::Normalise) v; }));
        auto t = num ("Target", -24.0, 0.0, 0.1, [&s] { return s.normaliseDb; }, [&s] (double v) { s.normaliseDb = v; }, dbText);
        t.enabled = [&s, this] { return s.normalise != rk::Normalise::off && ! proc.isSampling(); };
        b.ctls.push_back (t);
        b.ctls.push_back (choice ("Remove DC", { "on", "off" }, [&s] { return s.removeDc ? 0 : 1; }, [&s] (int v) { s.removeDc = v == 0; }));
        b.ctls.push_back (choice ("Tuning", { "snap", "fine", "off" }, [&s] { return (int) s.tuning; }, [&s] (int v) { s.tuning = (rk::Tuning) v; }, true));
        b.ctls.push_back (num ("Master tune", -100.0, 100.0, 1.0, [&s] { return s.tuneCents; }, [&s] (double v) { s.tuneCents = v; },
                               [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) std::lround (v)) + " ct"; }, false, true));
        pg.banks.push_back (std::move (b));
        capturePages.push_back (std::move (pg));
    }
    {
        Page pg { "transpose", "plan", {} };
        Bank b { "transpose", {} };
        b.ctls.push_back (choice ("Algorithm", { "hq formants", "classic hd", "fixed length" }, [&s] { return s.algo == rk::Algo::formant ? 0 : s.algo == rk::Algo::classic ? 1 : 2; },
                                  [&s] (int v) { s.algo = v == 0 ? rk::Algo::formant : v == 1 ? rk::Algo::classic : rk::Algo::fixedTime; }, true));
        auto f = num ("Formants", 0.0, 1.0, 0.01, [&s] { return s.formant; }, [&s] (double v) { s.formant = v; }, pct, false, true);
        f.enabled = [&s] { return s.algo != rk::Algo::classic; };
        b.ctls.push_back (f);
        b.ctls.push_back (choice ("Placement", { "centred", "upward", "downward" }, [&s] { return (int) s.placement; }, [&s] (int v) { s.placement = (rk::Placement) v; }, true));
        b.ctls.push_back (num ("Extend", 0, 48, 1, [&s] { return (double) s.extend; }, [&s] (double v) { s.extend = (int) v; }, semis, false, true));
        b.ctls.push_back (choice ("Keys", { "hq render", "live" }, [&s] { return s.renderKeys ? 0 : 1; }, [&s] (int v) { s.renderKeys = v == 0; }, true));
        pg.banks.push_back (std::move (b));
        capturePages.push_back (std::move (pg));
    }
}

// RÉÉCHANTILLONNER's settings: one page
void Machine::buildResamplePages()
{
    auto& o = proc.resampleOptions;
    auto idle = [this] { return proc.player.resampleState() == Player::Resample::idle; };
    resamplePages.clear();
    Page pg { "resample", "resample", {} };
    Bank b { "resample", {} };
    auto keep = [this] (Ctl c, std::function<bool()> when)
    {
        c.enabled = std::move (when);
        c.changed = [this] { proc.settingsChanged(); };
        return c;
    };
    b.ctls.push_back (keep (choiceCtl ("Length", { "1 bar", "2 bars", "4 bars", "8 bars", "free" },
                                        [&o] { return o.length; }, [&o] (int v) { o.length = v; }), idle));
    b.ctls.back().tip = "Combien enregistrer : des mesures au tempo (la boucle tombe juste), ou libre jusqu'à ce que tu arrêtes (une minute au plus)."_fr;
    b.ctls.push_back (keep (choiceCtl ("Start", { "on the bar", "now" }, [&o] { return o.synced ? 0 : 1; },
                                        [&o] (int v) { o.synced = v == 0; }), idle));
    b.ctls.back().tip = "À LA MESURE : quand le transport joue, l'enregistrement part sur la mesure suivante (sinon, tout de suite)."_fr;
    b.ctls.push_back (keep (numberCtl ("Note", 0.0, 127.0, 1.0, [&o] { return (double) o.root; }, [&o] (double v) { o.root = (int) v; },
                                       [] (double v) { return lower (rk::noteName ((int) v)); }), [] { return true; }));
    b.ctls.back().tip = "La touche qui rejoue le son tel qu'il a été enregistré (les autres le transposent)."_fr;
    b.ctls.push_back (keep (choiceCtl ("Effects", { "reset", "kept" }, [&o] { return o.resetFx ? 0 : 1; },
                                        [&o] (int v) { o.resetFx = v == 0; }), [] { return true; }));
    b.ctls.back().tip = "À ZÉRO : une fois injecté, le filtre, le drive, les effets et WTF?! sont coupés, pour ne pas traiter le son deux fois."_fr;
    pg.banks.push_back (std::move (b));
    resamplePages.push_back (std::move (pg));
}

// CAPTURE CYCLES's settings: the source (shared with the notes), then the takes
void Machine::buildCyclePages()
{
    auto& o = proc.cycleOptions;
    cyclePages.clear();
    if (! capturePages.empty())
        cyclePages.push_back (capturePages.front());          // the source and the MIDI out, the same
    auto keep = [this] (Ctl c, std::function<bool()> when = nullptr)
    {
        c.enabled = when != nullptr ? std::move (when) : std::function<bool()> ([this] { return ! proc.isSampling(); });
        c.changed = [this] { proc.settingsChanged(); };
        return c;
    };
    Page pg { "cycles", "cycleplan", {} };
    Bank b { "cycles", {} };
    b.ctls.push_back (keep (numberCtl ("Note", 0.0, 127.0, 1.0, [&o] { return (double) o.note; }, [&o] (double v) { o.note = (int) v; },
                                       [] (double v) { return lower (rk::noteName ((int) v)); })));
    b.ctls.back().tip = "La note tenue à chaque prise. Grave, c'est mieux : plus de points par période (C1 par défaut)."_fr;
    b.ctls.push_back (keep (choiceCtl ("Takes", { "8", "16", "32", "64" }, [&o] { return o.count; }, [&o] (int v) { o.count = v; })));
    b.ctls.back().tip = "Combien de cycles dans la banque : un par prise."_fr;
    b.ctls.push_back (keep (choiceCtl ("Sweep", { "cc", "velocity", "by hand" }, [&o] { return o.vary; }, [&o] (int v) { o.vary = v; })));
    b.ctls.back().tip = "D'une prise à l'autre : un CC balayé (le filtre, une forme d'onde...), la vélocité de 1 à 127, ou rien, et tu tournes un bouton du synthé pendant la capture."_fr;
    auto ccOn = [this] { return ! proc.isSampling() && proc.cycleOptions.vary == 0; };
    b.ctls.push_back (keep (numberCtl ("CC", 0.0, 127.0, 1.0, [&o] { return (double) o.cc; }, [&o] (double v) { o.cc = (int) v; },
                                       [] (double v) { return juce::String ((int) v); }), ccOn));
    b.ctls.back().tip = "Le numéro du CC envoyé avant chaque prise (74 : souvent la brillance, le filtre)."_fr;
    b.ctls.push_back (keep (numberCtl ("From", 0.0, 127.0, 1.0, [&o] { return (double) o.ccFrom; }, [&o] (double v) { o.ccFrom = (int) v; },
                                       [] (double v) { return juce::String ((int) v); }), ccOn));
    b.ctls.push_back (keep (numberCtl ("To", 0.0, 127.0, 1.0, [&o] { return (double) o.ccTo; }, [&o] (double v) { o.ccTo = (int) v; },
                                       [] (double v) { return juce::String ((int) v); }), ccOn));
    b.ctls.push_back (keep (numberCtl ("Hold", 0.15, 4.0, 0.05, [&o] { return o.hold; }, [&o] (double v) { o.hold = v; },
                                       [] (double v) { return seconds (v); }, true)));
    b.ctls.back().tip = "Combien de temps chaque note est tenue : le cycle est pris aux deux tiers, une fois l'attaque passée."_fr;
    b.ctls.push_back (keep (numberCtl ("Pause", 0.05, 4.0, 0.05, [&o] { return o.settle; }, [&o] (double v) { o.settle = v; },
                                       [] (double v) { return seconds (v); }, true)));
    b.ctls.back().tip = "Le temps laissé au synthé pour suivre le CC avant la note."_fr;
    pg.banks.push_back (std::move (b));
    cyclePages.push_back (std::move (pg));
}

void Machine::setCaptureKind (int k)
{
    if (k == captureKind)
        return;
    captureKind = k;
    proc.settingsFile().setValue ("captureKind", k);
    focus = nullptr;
    showPage (pageIndex());
}

void Machine::toggleResample()
{
    switch (proc.player.resampleState())
    {
        case Player::Resample::idle:
        {
            juce::String why;
            if (! proc.startResample (why))
                showMessage (why, true);
            else if (proc.resampleOptions.synced)
                showMessage ("Armé : l'enregistrement part sur la prochaine mesure si le transport joue, sinon tout de suite."_fr);
            break;
        }
        case Player::Resample::armed:
        case Player::Resample::recording:
        case Player::Resample::looping:
            proc.stopResample();
            break;
        case Player::Resample::handing:
            break;
    }
    captureStrip->repaint();
}

//==============================================================================
void Machine::showMode (int m)
{
    if (m == drumMode)
        return;
    capture = false;
    if (m != mode || (int) P (proc, snd::engine) != m)
        setSound (proc, snd::engine, (float) m);
    mode = m;
    focus = nullptr;
    if (mode == radioMode)
        proc.radioNet();
    showPage (page[mode]);
}

bool Machine::tunerPage() const
{
    if (capture || mode != radioMode)
        return false;
    const auto& v = pages[radioMode];
    return page[radioMode] >= 0 && page[radioMode] < (int) v.size() && v[(size_t) page[radioMode]].viz == "tune";
}

int Machine::radioPad() const
{
    return radioStrip != nullptr ? static_cast<RadioStrip&> (*radioStrip).selected : -1;
}

void Machine::showCapture (bool on)
{
    capture = on;
    focus = nullptr;
    showPage (pageIndex());
}

void Machine::showPage (int p)
{
    auto& pgs = pagesShown();
    if (pgs.empty())
        return;
    p = juce::jlimit (0, (int) pgs.size() - 1, p);
    auto& idx = pageIndex();
    if (p != idx)
        focus = nullptr;
    const auto key = pgs[(size_t) p].name + juce::String (capture ? 9 : mode);
    if (p == idx && pgs[(size_t) p].banks.size() > 1 && isShowing())
        bankOf[key] = (bankOf[key] + 1) % (int) pgs[(size_t) p].banks.size();   // the same key again: the next bank
    idx = p;
    if (! capture)
    {
        proc.settingsFile().setValue ("page" + juce::String (mode), p);
        proc.settingsFile().saveIfNeeded();
    }
    for (int i = 0; i < numPageKeys; ++i)
    {
        auto& k = pageKeys[i];
        const bool exists = i < (int) pgs.size();
        k.setVisible (exists);
        if (! exists)
            continue;
        k.setButtonText (pgs[(size_t) i].name);
        k.look = [this, i] { return pageIndex() == i ? KeyState::on : KeyState::normal; };
        k.dot = juce::Colours::transparentBlack;
        k.icon = pagePicto (pgs[(size_t) i]);
    }
    keyboard->setVisible (! capture && mode != radioMode);
    captureStrip->setVisible (capture);
    radioStrip->setVisible (! capture && mode == radioMode);
    radioTuner->setVisible (tunerPage());
    layoutEncoders();
    resized();
    repaint();
}

void Machine::layoutEncoders()
{
    auto& pgs = pagesShown();
    const int pi = pageIndex();
    std::vector<const Ctl*> shown;
    if (pi >= 0 && pi < (int) pgs.size())
    {
        auto& pg = pgs[(size_t) pi];
        auto& b = bankOf[pg.name + juce::String (capture ? 9 : mode)];
        b = juce::jlimit (0, (int) pg.banks.size() - 1, b);
        for (auto& c : pg.banks[(size_t) b].ctls)
            if (c.isShown() && shown.size() < 8)
                shown.push_back (&c);
    }
    for (int i = 0; i < 8; ++i)
    {
        encoders[i]->setCtl (i < (int) shown.size() ? shown[(size_t) i] : nullptr);
        encoders[i]->focused = encoders[i]->getCtl() != nullptr && encoders[i]->getCtl() == focus;
    }
    if (focus == nullptr && ! shown.empty())
        focus = shown[0];
    for (auto& e : encoders)
        e->focused = e->getCtl() != nullptr && e->getCtl() == focus;
}

void Machine::touch (const Ctl& c)
{
    focus = &c;
    for (auto& e : encoders)
    {
        e->focused = e->getCtl() == &c;
        e->repaint();
    }
    screen->repaint();
}

void Machine::armChanged()
{
    for (auto& e : encoders)
        e->repaint();
    modScenes->repaint();
    screen->repaint();
}

//==============================================================================
void Machine::paint (juce::Graphics& g)
{
    g.fillAll (col::body);
    g.setColour (col::keyLine);
    g.drawHorizontalLine (header.getBottom(), 0.0f, (float) getWidth());
    // the desk under the screen (NOIR & OR: lighter, a warm grey, lit from above)
    if (col::deck != col::body)
    {
        auto d = deckArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (d.translated (0.0f, 3.0f), 18.0f);
        g.setGradientFill (juce::ColourGradient (col::deck.brighter (0.08f), d.getX(), d.getY(), col::deck.darker (0.12f), d.getX(), d.getBottom(), false));
        g.fillRoundedRectangle (d, 18.0f);
        g.setColour (col::deck.brighter (0.3f).withAlpha (0.6f));
        g.drawHorizontalLine ((int) d.getY() + 1, d.getX() + 18.0f, d.getRight() - 18.0f);
    }
    ledMeter (g, inMeter.toFloat(), proc.isSampling() || proc.options.monitor > 0 ? proc.inputLevel() : 0.0f, "in");
    ledMeter (g, outMeter.toFloat(), proc.player.outLevel.load(), "out");
    // the lines between the groups of the row of controls, engraved in the desk
    for (int i = 1; i < 8; ++i)
        if (encoders[i] != nullptr && encoders[i]->getCtl() != nullptr && encoders[i]->getCtl()->sepBefore)
        {
            const float x = (float) encoders[i]->getX(), y0 = (float) encArea.getY() + 16.0f, y1 = (float) encArea.getBottom() - 12.0f;
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRect (x - 1.0f, y0, 1.0f, y1 - y0);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRect (x, y0, 1.0f, y1 - y0);
        }
    // the name (its band lit while a capture runs)
    auto l = logo.toFloat();
    badge (g, l.removeFromLeft (138.0f).withSizeKeepingCentre (138.0f, 40.0f), col::ink, col::body,
           proc.isSampling() || proc.player.resampleState() == Player::Resample::recording ? col::red : col::accent, col::ink);
    l.removeFromLeft (10.0f);
    text (g, Updater::currentVersion(), l, mono (11.0f), col::ink2, juce::Justification::centredLeft, true);
    // the instrument's name in its black window
    g.setColour (col::screen);
    g.fillRoundedRectangle (presetBox.toFloat(), 7.0f);
    g.setColour (col::keyLine);
    g.drawRoundedRectangle (presetBox.toFloat().reduced (0.5f), 7.0f, 1.0f);
    text (g, proc.instrumentName, presetBox.toFloat().reduced (10.0f, 0.0f), mono (14.0f, 500), col::screenText, juce::Justification::centred, true);
    // the tempo
    g.setColour (col::screen);
    g.fillRoundedRectangle (bpmBox.toFloat(), 7.0f);
    g.setColour (col::keyLine);
    g.drawRoundedRectangle (bpmBox.toFloat().reduced (0.5f), 7.0f, 1.0f);
    auto bb = bpmBox.toFloat().reduced (10.0f, 0.0f);
    text (g, juce::String (P (proc, snd::tempo), 1), bb.removeFromLeft (62.0f), mono (15.0f, 500), col::screenText, juce::Justification::centredRight, true);
    text (g, "bpm", bb.withTrimmedLeft (8.0f), mono (11.0f), col::screenDim);
    // the output (the input while capturing)
    const float lvl = proc.isSampling() ? proc.inputLevel() : proc.player.outLevel.load();
    const float dbv = juce::Decibels::gainToDecibels (lvl, -100.0f);
    const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (dbv + 60.0f) / 60.0f) * 10.0f);
    auto mb = meterBox.toFloat();
    for (int i = 0; i < 10; ++i)
    {
        const float h = 6.0f + (float) i * 2.0f;
        g.setColour (i < lit ? (i >= 9 ? col::red : col::ink) : col::tickOff);
        g.fillRoundedRectangle (mb.getX() + (float) i * 5.0f, mb.getBottom() - h, 3.5f, h, 1.0f);
    }
}

void Machine::paintOverChildren (juce::Graphics& g)
{
    if (dropping)
    {
        g.setColour (col::accent.withAlpha (0.18f));
        g.fillRect (getLocalBounds());
        g.setColour (col::accent);
        g.drawRect (getLocalBounds(), 4);
        text (g, "lâche : un son pour le sampler, un instrument ou un pack replikant"_fr, getLocalBounds().toFloat().removeFromBottom (60.0f),
              mono (16.0f, 600), col::ink, juce::Justification::centred);
    }
}

void Machine::showBank (int bank)
{
    auto& pgs = pagesShown();
    const int p = pageIndex();
    if (p < 0 || p >= (int) pgs.size())
        return;
    bankOf[pgs[(size_t) p].name + juce::String (capture ? 9 : mode)] = juce::jlimit (0, (int) pgs[(size_t) p].banks.size() - 1, bank);
    layoutEncoders();
    repaint();
}

void Machine::resized()
{
    if (screen == nullptr || keyboard == nullptr || captureStrip == nullptr || encoders[7] == nullptr)
        return;     // (the constructor's setSize, before the parts are made)
    auto r = getLocalBounds();
    header = r.removeFromTop (56);
    auto h = header.reduced (16, 10);
    logo = h.removeFromLeft (200);
    h.removeFromLeft (2);
    for (auto& k : modeKeys)
    {
        k.setBounds (h.removeFromLeft (100));
        h.removeFromLeft (6);
    }
    h.removeFromLeft (4);
    captureKey.setBounds (h.removeFromLeft (104));
    h.removeFromLeft (10);
    presetBox = h.removeFromLeft (150);
    nameEditor.setBounds (presetBox);
    packKey.setBounds (h.removeFromRight (104));
    h.removeFromRight (8);
    fileKey.setBounds (h.removeFromRight (84));
    h.removeFromRight (12);
    meterBox = h.removeFromRight (50).reduced (0, 4);
    h.removeFromRight (12);
    bpmBox = h.removeFromRight (118);
    h.removeFromRight (8);
    playKey.setBounds (h.removeFromRight (40));
    h.removeFromRight (8);
    updateKey.setBounds (h.removeFromRight (112));

    auto body = r.reduced (14, 0).withTrimmedTop (14).withTrimmedBottom (14);
    screenArea = body.removeFromTop (304);
    screen->setBounds (screenArea);
    body.removeFromTop (12);
    deckArea = body.expanded (0, 0).withTop (body.getY()).withBottom (getHeight() - 8);
    body = body.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (4);
    keysArea = body.removeFromTop (44);
    // the page keys: eight across, or as many as the mode has (SYNTHÉ and SAMPLER: ten)
    const int nk = juce::jlimit (8, numPageKeys, (int) pagesShown().size());
    const int kw = (keysArea.getWidth() - (nk - 1) * 8) / nk;
    for (int i = 0; i < numPageKeys; ++i)
        pageKeys[i].setBounds (keysArea.getX() + i * (kw + 8), keysArea.getY(), kw, keysArea.getHeight());
    body.removeFromTop (8);
    encArea = body.removeFromTop (176);
    inMeter = encArea.removeFromLeft (26).reduced (0, 6);
    outMeter = encArea.removeFromRight (26).reduced (0, 6);
    encArea = encArea.reduced (8, 0);
    const int ew = encArea.getWidth() / 8;
    for (int i = 0; i < 8; ++i)
        encoders[i]->setBounds (encArea.getX() + i * ew, encArea.getY(), ew, encArea.getHeight());
    body.removeFromTop (6);
    bottomArea = body;
    auto b = bottomArea;
    wtfPanel->setBounds (b.removeFromRight (128));
    b.removeFromRight (12);
    modScenes->setBounds (b.removeFromRight (372));
    b.removeFromRight (12);
    keyboard->setBounds (b);
    captureStrip->setBounds (b);
    radioStrip->setBounds (b);
    // the tuner: over the screen's left part (its title and the panel on the right stay)
    radioTuner->setBounds (screenArea.getX() + 16, screenArea.getY() + 12 + 30, screenArea.getWidth() - 32 - 392 - 14, screenArea.getHeight() - 24 - 30);
    updatePanel.setBounds (getLocalBounds());
}

bool Machine::keyPressed (const juce::KeyPress& k)
{
    // 1..8: the pages
    const auto c = k.getTextCharacter();
    if (c >= '1' && c <= '8' && ! nameEditor.isVisible())
    {
        showPage (c - '1');
        return true;
    }
    return false;
}

void Machine::mouseDown (const juce::MouseEvent& e)
{
    if (presetBox.contains (e.getPosition()) && e.getNumberOfClicks() >= 2 && ! proc.isSampling())
        renameInstrument();
}

void Machine::renameInstrument()
{
    nameEditor.setText (proc.instrumentName, false);
    nameEditor.setVisible (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

//==============================================================================
bool Machine::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (instrumentio::isImportable (juce::File (f)))
            return ! proc.isSampling();
    return false;
}

void Machine::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    repaint();
    for (auto& f : files)
        if (instrumentio::isImportable (juce::File (f)))
        {
            proc.importAny (juce::File (f));
            return;
        }
}

juce::String Machine::packSignature() const
{
    auto ins = proc.getInstrument();
    if (ins == nullptr)
        return {};
    return juce::String ((juce::pointer_sized_int) ins.get()) + "|" + ins->name + "|" + proc.soundSignature();
}

void Machine::showMessage (const juce::String& m, bool warning)
{
    message = m;
    messageWarning = warning;
    messageUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 7.0;
    screen->repaint();
}

void Machine::toggleSampling()
{
    if (proc.isSampling())
    {
        proc.stopSampling();
        return;
    }
    if (proc.options.source != ReplikantProcessor::Source::testSynth && proc.options.monitor == 0)
        showMessage ("MONITOR sur OFF : la capture enregistre, mais tu n'entends pas ton synthé ici (DIRECT dans SETTINGS, au casque, pour l'écouter)."_fr, false);
    juce::String why;
    if (! (captureKind == cyclesKind ? proc.startCycles (why) : proc.startSampling (why)))
        showMessage (why, true);
}

void Machine::openInstrument()
{
    chooser = std::make_unique<juce::FileChooser> ("Ouvrir un instrument REPLIK8, un pack (zip) ou un son"_fr, proc.instrumentsRoot(), "*.replikant;*.zip;*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() || f.isDirectory())
            proc.importAny (f);
    });
}

void Machine::exportMenu (juce::PopupMenu& m)
{
    const bool has = proc.getInstrument() != nullptr && ! proc.getInstrument()->samples.empty();
    m.addItem (5, "Enregistrer le pack (zip) sous..."_fr, has);
    m.addItem (1, "Exporter les samples en WAV"_fr, has);
    m.addItem (2, "Exporter toutes les touches en WAV (rendu REPLIK8)"_fr, has);
    m.addItem (3, "Exporter en Bitwig .multisample"_fr, has);
    m.addItem (4, "Exporter toutes les touches en .multisample"_fr, has);
    m.addItem (6, "Ouvrir le dossier de l'instrument"_fr);
}

void Machine::showFileMenu()
{
    juce::PopupMenu m, zoomMenu;
    m.addItem (10, "Nouveau"_fr, ! proc.isSampling());
    m.addItem (11, "Ouvrir... (instrument, pack ou son)"_fr, ! proc.isSampling());
    m.addItem (12, "Renommer"_fr, ! proc.isSampling());
    m.addSeparator();
    exportMenu (m);
    m.addSeparator();
    int id = 100;
    for (float z : zoomSteps)
        zoomMenu.addItem (id++, juce::String (juce::roundToInt (z * 100)) + " %", true, std::abs (z - zoom) < 0.01f);
    m.addSubMenu ("Taille de la fenêtre"_fr, zoomMenu);
    if (standalone (proc))
        m.addItem (13, "Réglages audio et MIDI..."_fr);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileKey), [this] (int r)
    {
        if (r <= 0)
            return;
        if (r >= 100)
        {
            zoom = zoomSteps[r - 100];
            proc.settingsFile().setValue ("zoom", (double) zoom);
            proc.settingsFile().saveIfNeeded();
            if (onZoom)
                onZoom (zoom);
            return;
        }
        if (r == 10) { proc.newInstrument(); return; }
        if (r == 11) { openInstrument(); return; }
        if (r == 12) { renameInstrument(); return; }
        if (r == 13)
        {
           #if JucePlugin_Build_Standalone
            if (auto* h = juce::StandalonePluginHolder::getInstance())
                h->showAudioSettingsDialog();
           #endif
            return;
        }
        auto ins = proc.getInstrument();
        if (ins == nullptr)
            return;
        const auto start = ins->folder.isDirectory() ? ins->folder.getParentDirectory() : proc.instrumentsRoot();
        if (r == 6)
        {
            auto f = ins->folder.isDirectory() ? ins->folder : proc.instrumentsRoot();
            f.createDirectory();
            f.revealToUser();
            return;
        }
        if (r == 5)
        {
            chooser = std::make_unique<juce::FileChooser> ("Enregistrer le pack"_fr, start.getChildFile (instrumentio::safeName (ins->name) + ".zip"), "*.zip");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                if (! f.hasFileExtension ("zip"))
                    f = f.withFileExtension ("zip");
                proc.buildPack ([this, f] (const juce::File& zip)
                {
                    if (zip.existsAsFile() && (zip == f || zip.copyFileTo (f)))
                    {
                        showMessage ("Pack enregistré : "_fr + f.getFullPathName());
                        f.revealToUser();
                    }
                });
            });
            return;
        }
        const bool bitwig = r >= 3, all = r == 2 || r == 4;
        const auto base = instrumentio::safeName (ins->name) + (all ? " (touches)" : "");
        if (bitwig)
        {
            chooser = std::make_unique<juce::FileChooser> ("Exporter en .multisample", start.getChildFile (base + ".multisample"), "*.multisample");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, all] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                if (! f.hasFileExtension ("multisample"))
                    f = f.withFileExtension ("multisample");
                proc.exportInstrument (true, all, f);
            });
        }
        else
        {
            chooser = std::make_unique<juce::FileChooser> ("Choisis où créer le dossier de WAV"_fr, start);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, all, base] (const juce::FileChooser& fc)
            {
                const auto dir = fc.getResult();
                if (dir == juce::File())
                    return;
                auto target = dir.getChildFile (base + " WAV");
                for (int n = 2; target.exists(); ++n)
                    target = dir.getChildFile (base + " WAV " + juce::String (n));
                proc.exportInstrument (false, all, target);
            });
        }
    });
}

//==============================================================================
void Machine::timerCallback()
{
    proc.captureOpen = capture;            // (the input heard as it is only with the capture page open)
    status = proc.status();
    const bool sampling = proc.isSampling();
    // a capture over and its instrument made: the sampler plays it
    if (sampling)
        wasSampling = true;
    else if (wasSampling && ! status.busy)
    {
        wasSampling = false;
        auto ins = proc.getInstrument();
        if (proc.lastCaptureWasCycles)
        {
            if (ins != nullptr && ins->cycles != nullptr)
            {
                // the cycles in the SYNTHÉ's osc A, to hear them at once
                setSound (proc, snd::oaBank, 1.0f);
                capture = false;
                page[synthMode] = 0;
                showMode (synthMode);
                showMessage (juce::String (ins->cycles->count()) + " cycles capturés : dans l'osc A (CAPTURÉS), tourne POSITION pour les parcourir."_fr);
            }
        }
        else if (ins != nullptr && ! ins->samples.empty())
        {
            capture = false;
            showMode (samplerMode);
        }
    }
    // RÉÉCHANTILLONNER's instrument in: the sampler shown, to play and change it
    if (proc.lastResample != shownResample)
    {
        shownResample = proc.lastResample;
        if (shownResample.isNotEmpty())
        {
            capture = false;
            mode = samplerMode;
            focus = nullptr;
            showPage (page[mode]);
        }
    }
    if (capture && captureKind == resampleKind)
        captureStrip->repaint();
    // the engine moved by the host (automation, a project loaded): the pages follow
    const int engine = (int) P (proc, snd::engine);
    if (! capture && engine != mode && engine != snd::drum)
    {
        mode = engine;
        focus = nullptr;
        showPage (page[mode]);
    }
    // the controls shown or hidden by a choice (a type, a model)
    {
        auto& pgs = pagesShown();
        const int pi = pageIndex();
        if (pi >= 0 && pi < (int) pgs.size())
        {
            auto& pg = pgs[(size_t) pi];
            const int b = juce::jlimit (0, (int) pg.banks.size() - 1, bankOf[pg.name + juce::String (capture ? 9 : mode)]);
            int at = 0;
            bool same = true;
            for (auto& c : pg.banks[(size_t) b].ctls)
                if (c.isShown() && at < 8)
                    same = same && encoders[at++]->getCtl() == &c;
            for (; at < 8; ++at)
                same = same && encoders[at]->getCtl() == nullptr;
            if (! same)
                layoutEncoders();
        }
    }
    for (auto& e : encoders)
        e->refreshIfChanged();
    screen->repaint();
    modScenes->repaint();
    if (radioStrip->isVisible())
        radioStrip->repaint();
    if (radioTuner->isVisible() != tunerPage())
        radioTuner->setVisible (tunerPage());
    static int tick = 0;
    if (++tick % 3 == 0)
    {
        wtfPanel->repaint();
        keyboard->repaint();
        captureStrip->repaint();
        playKey.repaint();
        captureKey.repaint();
    }
    repaint (header.withTrimmedLeft (header.getWidth() - 420));      // the tempo, the meter
    if (tick % 4 == 0)
        repaint (logo);
    maybeShowUpdate();
}

void Machine::changeListenerCallback (juce::ChangeBroadcaster* b)
{
    if (b == &proc.updater.changes)
    {
        refreshUpdate();
        maybeShowUpdate();
        return;
    }
    if (! nameEditor.isVisible())
        repaint();
    for (auto& e : encoders)
        e->repaint();
}

void Machine::refreshUpdate()
{
    auto& up = proc.updater;
    const bool show = up.getState() == Updater::State::Available || up.getState() == Updater::State::Installing
                   || up.getState() == Updater::State::Installed;
    updateKey.setButtonText (up.getState() == Updater::State::Installed ? "redémarrer"_fr : "mise à jour"_fr);
    updateKey.setTooltip (up.getStatusText());
    updateKey.setVisible (show);
}

void Machine::maybeShowUpdate()
{
    static juce::String shownUpdate;
    static bool shownNews = false;
    if (updatePanel.isVisible() || ! isShowing())
        return;
    if (pendingNews.has_value())
    {
        if (! std::exchange (shownNews, true))
            updatePanel.showNews (*pendingNews);
        pendingNews.reset();
        return;
    }
    auto& up = proc.updater;
    if (up.getState() == Updater::State::Available && up.getRelease().version != shownUpdate)
    {
        shownUpdate = up.getRelease().version;
        updatePanel.showAvailable (up.getNews (3), (int) up.getNewer().size());
    }
}
}
