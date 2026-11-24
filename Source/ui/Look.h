// The look (0.3): black, gold and red; a black screen over a warm grey desk, knobs in relief, capitals in Space
// Grotesk, fine-line pictos.

#pragma once

#include <JuceHeader.h>

namespace te
{
    // the size everything is laid out at (the editor zooms it as a whole)
    constexpr int width = 1360, height = 848;
    constexpr float zoomSteps[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

    // the colours by their role (black #121312, gold #a67d43, red #ad0013, a cream for what must be read)
    namespace col
    {
        // the body, its keys
        inline juce::Colour body, face, key, keyLine, keyShadow, well;
        inline juce::Colour ink, ink2, ink3;                    // text on the body and on keys; labels; off
        inline juce::Colour keyOn, keyOnInk, keyOnEdge;          // a key that is on
        inline juce::Colour accent, accentInk, accentDark;       // the accent (capture, the pack ready, notes played)
        inline juce::Colour tickOff, tickOn, red, violet;
        inline juce::Colour white;                               // a pale cap
        inline juce::Colour deck, deckEdge, deckInk, deckInk2;   // the desk under the screen, its writing
        // the screens
        inline juce::Colour screen, screenEdge, screenLine, screenGrid, screenText, screenDim, screenGhost;
        // on a screen: the main trace, the second one, the accent, the loops and effects, a red, two more
        inline juce::Colour sBlue, sGreen, sOrange, sYellow, sRed, sPink, sViolet;

        juce::Colour mod (int m);
        juce::Colour modOnScreen (int m);
        juce::Colour cap (int i);              // the encoders' caps, in turn
        juce::Colour notch (int i);            // the line on each cap
    }
    // fonts, Space Grotesk: weight 400 / 500 / 600 (the writing, in capitals), 300 / 500 / 700 (the name, the big numbers)
    juce::Font mono (float size, int weight = 400);
    juce::Font grotesk (float size, int weight = 500);

    // lower case, French accents included; the writing's case (capitals)
    juce::String lower (const juce::String&);
    juce::String themed (const juce::String&);

    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();
        void applyColours();                   // the JUCE colours from the palette
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        juce::Font getPopupMenuFont() override                          { return mono (14.0f); }
        void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int> at, juce::Rectangle<int> parent) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override  { return mono (13.0f, 500); }
        void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}
    };

    //========================================================================== drawing
    enum class KeyState { normal, on, accent, off };
    // a key of the body: light, black when on, orange as the accent, faded when off (pressed: its depth gone)
    void key (juce::Graphics&, juce::Rectangle<float>, KeyState, bool pressed = false, float radius = 7.0f);
    juce::Colour keyInk (KeyState);
    // text, in the theme's case unless `asIs`
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&, juce::Colour,
               juce::Justification = juce::Justification::centredLeft, bool asIs = false);
    // a black screen (its rim, a fine grid in NOIR & OR)
    void screen (juce::Graphics&, juce::Rectangle<float>, float radius = 14.0f);
    // a vertical LED meter (dBFS -60..0)
    void ledMeter (juce::Graphics&, juce::Rectangle<float>, float level, const juce::String& name);
    // (0.4.12) the big tuning knob: `turn` how far round (0..1), its dial (0..9) printed round it in `ink`
    void tuningKnob (juce::Graphics&, juce::Point<float> centre, float radius, float turn, juce::Colour cap, bool dim, juce::Colour ink);
    // an encoder: its value (lit from `from`: the start, or where its zero is), a band in `modColour` from
    // `modFrom` to `modTo` (none when equal); in CLAIR ticks and a coloured cap with its notch, in NOIR & OR a black
    // knob in relief with a lit arc
    void encoder (juce::Graphics&, juce::Point<float> centre, float radius, float value, float from, juce::Colour cap,
                  juce::Colour notch, float modFrom = 0.0f, float modTo = 0.0f, juce::Colour modColour = {}, bool dim = false,
                  juce::Colour lit = juce::Colour());      // (none: the theme's)
    // the name: REPLIK8 over a band that says SAMPLER, in a rounded frame
    void badge (juce::Graphics&, juce::Rectangle<float>, juce::Colour ink, juce::Colour face, juce::Colour band, juce::Colour bandInk);

    //========================================================================== pictos (Pictos.cpp)
    // small drawings in thin lines, in one colour: what a mode, a page, a filter, a drive or an effect is
    enum class Picto
    {
        none,
        sine, drum, cassette,                                   // the modes
        cycles, saw, voices, filterBox, envelope, chain, cable, sliders,   // the pages
        playhead, loop, fork,
        lowpass, bandpass, highpass, notch,                     // the filter's types (the IEC box: the waves let through)
        off, ladder, ms20, sem, oscar, wasp, polivoks,          // the filter's models, as bits of their circuits
        tube, console, reels,                                   // the drive's
        spectrum, shift, chorus, crush, space,                  // the effects
        steps, random, follower,                                // the modulators (with sine and envelope)
        jack, keys, clock, onset, vuMeter, resample, record,    // the capture
        orbit, mixer,                                           // (0.3.2) ORBITE, the MIXEUR
        radio, antenna, air, slice, pads,                       // (0.3.5) RADIO: the mode, its pages
        cube,                                                   // (0.4.4) the filter's CUBE
        echo,                                                   // (0.4.5) ECHO
        drop, weave                                             // (0.4.20) a sound to drop here; WEAVE
    };
    // drawn in the square centred in the area, `thickness` in pixels
    void picto (juce::Graphics&, Picto, juce::Rectangle<float>, juce::Colour, float thickness = 1.5f);
    Picto filterModelPicto (int model);         // snd::fModel's choices
    Picto filterTypePicto (int type);           // snd::fType's
    Picto driveModelPicto (int model);          // snd::dModel's
    Picto stagePicto (int stage, int driveModel);   // Player's stages (drive, spectre, pitch, chorus, crush, espace, echo)
    Picto modPicto (int type);                  // snd::mType's (onde, pas, hasard, env, suiveur)
}
