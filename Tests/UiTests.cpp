// The look on its own: fonts and text layout. A tooltip with characters the pixel fonts don't have makes JUCE look
// for a fallback font through the look: that must end (REPLIKANT 0.1 recursed until the stack ran out, in Live).

#include <JuceHeader.h>
#include "../Source/ui/Pixel.h"

#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    {
        px::Look look;
        juce::LookAndFeel::setDefaultLookAndFeel (&look);
        // arrows, approximately, guillemets, an ellipsis, the modulators' signs, accents
        const auto tip = juce::String::fromUTF8 ("Un pack \xe2\x86\x92 \xe2\x89\x88 \xc2\xab test \xc2\xbb \xe2\x80\xa6 \xe2\x96\xb2 \xe2\x96\xa0 \xe2\xac\xa2 \xe2\x97\x8f "
                                                 "\xc3\xa9\xc3\xa8\xc3\xa0\xc3\xa7 \xc5\x93 \xc3\x89");
        const auto r = look.getTooltipBounds (tip, { 10, 10 }, { 0, 0, 1000, 800 });
        CHECK (r.getWidth() > 50 && r.getHeight() > 10, "tooltip %d x %d", r.getWidth(), r.getHeight());
        // the plain fonts JUCE asks for come out as VT323
        auto plain = look.getTypefaceForFont (juce::Font (juce::FontOptions (12.0f)));
        CHECK (plain != nullptr, "default typeface");
        // a named font goes to JUCE's own lookup (whatever the system has): it must come back, not loop
        look.getTypefaceForFont (juce::Font (juce::FontOptions ("Helvetica", 12.0f, juce::Font::plain)));
        // and text drawn with the pixel fonts, odd characters included
        juce::Image img (juce::Image::ARGB, 400, 60, true);
        juce::Graphics g (img);
        px::drawText (g, tip, { 0, 0, 400, 30 }, juce::Justification::centredLeft, px::text (20.0f), px::white);
        px::drawText (g, tip, { 0, 30, 400, 30 }, juce::Justification::centredLeft, px::label (10.0f), px::white);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
