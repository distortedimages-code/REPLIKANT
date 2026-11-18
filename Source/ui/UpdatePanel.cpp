#include "UpdatePanel.h"
#include "Look.h"
#include "Text.h"

using namespace te;

namespace
{
    constexpr int cardWidth = 860, pad = 32, buttonHeight = 44;

    // text that wraps at the width given, from the top of its box: the height it took
    int wrapped (juce::Graphics* g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                 int x, int y, int width)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addJustifiedText (font, text, (float) x, (float) y + font.getAscent(), (float) width, juce::Justification::left);
        const auto box = glyphs.getBoundingBox (0, -1, true);
        if (g != nullptr)
        {
            g->setColour (colour);
            glyphs.draw (*g);
        }
        const int lines = juce::jmax (1, (int) std::round ((box.getBottom() - (float) y) / font.getHeight()));
        return (int) std::ceil (font.getHeight() * (float) lines);
    }
}

UpdatePanel::UpdatePanel (Updater& u) : updater (u)
{
    for (auto* b : { &install, &later, &skip, &readAll, &ok })
    {
        b->setClickingTogglesState (false);
        addChildComponent (b);
    }
    install.onClick = [this] { updater.install(); };
    later.onClick = [this] { close(); };
    ok.onClick = [this] { close(); };
    skip.onClick = [this] { updater.skipThisVersion(); close(); };
    readAll.onClick = [this]
    {
        juce::URL (mode == Mode::news || news.empty() ? Updater::pageOf (Updater::currentVersion())
                                                      : updater.getRelease().pageUrl.isNotEmpty() ? updater.getRelease().pageUrl
                                                                                                  : Updater::pageOf (latest))
            .launchInDefaultBrowser();
    };
    updater.changes.addChangeListener (this);
}

UpdatePanel::~UpdatePanel()
{
    updater.changes.removeChangeListener (this);
}

void UpdatePanel::showAvailable (const std::vector<Updater::News>& n, int versionsNewer)
{
    mode = Mode::available;
    news = n;
    latest = news.empty() ? juce::String() : news.front().version;
    more = juce::jmax (0, versionsNewer - (int) news.size());
   #if JUCE_MAC
    install.setButtonText ("Installer la "_fr + latest);
   #else
    install.setButtonText ("Télécharger la "_fr + latest);
   #endif
    refreshButtons();
    resized();
    setVisible (true);
    toFront (true);
}

void UpdatePanel::showNews (const Updater::News& n)
{
    mode = Mode::news;
    news = { n };
    latest = n.version;
    more = 0;
    refreshButtons();
    resized();
    setVisible (true);
    toFront (true);
}

void UpdatePanel::close()
{
    setVisible (false);
    if (onClose)
        onClose();
}

void UpdatePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! isVisible())
        return;
    refreshButtons();
    resized();
    repaint();
}

void UpdatePanel::refreshButtons()
{
    const auto state = updater.getState();
    const bool offer = mode == Mode::available;
    const bool busy = state == Updater::State::Installing;
    const bool done = state == Updater::State::Installed;
    install.setVisible (offer && ! done);
    install.setEnabled (! busy && state == Updater::State::Available);
    later.setVisible (offer && ! done);
    later.setEnabled (! busy);
    skip.setVisible (offer && ! done);
    skip.setEnabled (! busy && state == Updater::State::Available);
    readAll.setVisible (true);
    ok.setVisible (mode == Mode::news || done);
}

//==============================================================================
int UpdatePanel::drawContent (juce::Graphics* g, juce::Rectangle<int> area) const
{
    const int x = area.getX(), w = area.getWidth();
    int y = area.getY();
    const auto you = "tu as la "_fr + Updater::currentVersion();

    // the small line over the title, the version you have on its right
    if (g != nullptr)
    {
        text (*g, mode == Mode::news ? juce::String ("nouvelle version") : "mise à jour"_fr, juce::Rectangle<int> { x, y, w / 2, 18 }.toFloat(),
              mono (12.0f, 600), col::accent);
        text (*g, you, juce::Rectangle<int> { x + w / 2, y, w - w / 2, 18 }.toFloat(), mono (12.0f), col::ink2, juce::Justification::centredRight);
    }
    y += 26;

    const auto title = mode == Mode::news ? "Tu as maintenant la "_fr + latest : "La "_fr + latest + " est sortie";
    y += wrapped (g, title, grotesk (30.0f, 500), col::ink, x, y, w) + 10;

    for (const auto& n : news)
    {
        // the version, in a white tag, then what it brings
        const auto tagFont = mono (12.0f, 600);
        const int tagW = juce::GlyphArrangement::getStringWidthInt (tagFont, n.version) + 16;
        if (g != nullptr)
        {
            g->setColour (col::ink);
            g->fillRoundedRectangle ((float) x, (float) y + 4.0f, (float) tagW, 20.0f, 4.0f);
            text (*g, n.version, juce::Rectangle<int> { x, y + 4, tagW, 20 }.toFloat(), tagFont, col::screenText, juce::Justification::centred, true);
        }
        const int tx = x + tagW + 14, tw = w - tagW - 14;
        y += wrapped (g, n.headline.isNotEmpty() ? n.headline : "Pas de résumé pour cette version."_fr, mono (16.0f, 500), col::ink, tx, y, tw) + 6;
        for (const auto& p : n.points)
        {
            if (g != nullptr)
            {
                g->setColour (col::accent);
                g->fillRoundedRectangle ((float) tx + 2.0f, (float) y + 7.0f, 6.0f, 6.0f, 1.5f);
            }
            y += wrapped (g, p, mono (13.0f), col::ink2, tx + 18, y, tw - 18) + 3;
        }
        y += 14;
    }
    if (more > 0)
        y += wrapped (g, "+ " + juce::String (more) + (more > 1 ? " versions plus anciennes : « Tout lire » les montre."_fr
                                                                 : " version plus ancienne : « Tout lire » la montre."_fr),
                      mono (13.0f), col::ink2, x, y, w) + 8;

    // what the installing says
    const auto state = updater.getState();
    if (mode == Mode::available && (state == Updater::State::Installing || state == Updater::State::Installed || state == Updater::State::Failed))
    {
        const auto colour = state == Updater::State::Failed ? col::red : state == Updater::State::Installed ? col::ink : col::accent;
        y += wrapped (g, updater.getStatusText(), mono (14.0f, 500), colour, x, y, w) + 8;
    }
    else if (mode == Mode::available)
       #if JUCE_MAC
        y += wrapped (g, "Elle remplace le plug-in et l'appli sur ce Mac (macOS peut demander ton mot de passe). Quitte et rouvre ton logiciel ensuite."_fr,
                      mono (12.0f), col::ink2, x, y, w) + 8;
       #else
        y += wrapped (g, "La page de téléchargement s'ouvre dans ton navigateur."_fr, mono (12.0f), col::ink2, x, y, w) + 8;
       #endif
    return y - area.getY();
}

void UpdatePanel::paint (juce::Graphics& g)
{
    g.fillAll (col::ink.withAlpha (0.55f));
    g.setColour (col::body);
    g.fillRoundedRectangle (card.toFloat(), 16.0f);
    g.setColour (mode == Mode::news ? col::ink : col::accent);
    g.drawRoundedRectangle (card.toFloat().reduced (1.5f), 16.0f, 3.0f);
    drawContent (&g, content);
}

void UpdatePanel::resized()
{
    const int w = juce::jmin (cardWidth, getWidth() - 40);
    const int textH = drawContent (nullptr, { 0, 0, w - 2 * pad, 10 });
    const int h = juce::jmin (getHeight() - 40, pad + textH + 16 + buttonHeight + 24);
    card = getLocalBounds().withSizeKeepingCentre (w, h);
    content = card.reduced (pad, 0).withTrimmedTop (24).withTrimmedBottom (buttonHeight + 40);

    auto row = card.reduced (pad, 0).removeFromBottom (buttonHeight + 24).withTrimmedBottom (24);
    auto place = [&row] (juce::TextButton& b, int width, bool right)
    {
        if (! b.isVisible())
            return;
        b.setBounds (right ? row.removeFromRight (width) : row.removeFromLeft (width));
        if (right) row.removeFromRight (10); else row.removeFromLeft (10);
    };
    place (install, 250, false);
    place (ok, 120, false);
    place (later, 140, false);
    place (readAll, 140, true);
    place (skip, juce::jmax (0, juce::jmin (250, row.getWidth())), true);
}
