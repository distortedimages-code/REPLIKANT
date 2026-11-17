// Not a test: runs a short session on the test synth through the real processor, and draws the window to PNG
// files (during the session, then on each page with the instrument), to look at the interface without a Mac.
//   Snapshot <folder>

#include <JuceHeader.h>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"
#include "../Source/ui/MainView.h"

static void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

static void save (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, out);
    std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();

    ReplikantProcessor proc;
    proc.options.source = ReplikantProcessor::Source::testSynth;
    proc.instrumentName = "Synthe test";
    proc.session.lowNote = 36;
    proc.session.highNote = 84;
    proc.session.interval = 6;
    proc.session.layers = 3;
    proc.session.hold = 0.8;
    proc.session.releaseMax = 0.6;
    proc.session.pause = 0.05;
    proc.settingsChanged();
    proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    pump (300);
    save (*ed, dir.getChildFile ("0-start.png"));

    juce::String why;
    if (! proc.startSampling (why))
    {
        std::printf ("can't start: %s\n", why.toRawUTF8());
        return 1;
    }
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    int blocks = 0;
    bool midShot = false;
    while (proc.isSampling() && blocks < 200000)
    {
        buf.clear();
        midi.clear();
        proc.processBlock (buf, midi);
        if (++blocks % 40 == 0)
            pump (2);
        if (! midShot && proc.status().done >= 13)
        {
            pump (100);
            save (*ed, dir.getChildFile ("1-sampling.png"));
            midShot = true;
        }
    }
    // the worker assembles and renders
    for (int i = 0; i < 600; ++i)
    {
        pump (100);
        auto ins = proc.getInstrument();
        if (ins != nullptr && ! ins->keys.empty() && ! proc.status().busy)
            break;
    }
    // play a chord so the map and the keyboard light up
    for (int n : { 48, 55, 63 })
        proc.audition (n, 100, true);
    for (int b = 0; b < 60; ++b)
    {
        buf.clear();
        midi.clear();
        proc.processBlock (buf, midi);
    }
    pump (200);
    save (*ed, dir.getChildFile ("2-instrument.png"));

    // the other pages
    std::function<void (juce::Component&)> clickPage = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (child))
                if (b->getButtonText() == juce::String (std::getenv ("RK_PAGE_NAME") ? std::getenv ("RK_PAGE_NAME") : ""))
                {
                    b->triggerClick();
                    return;
                }
            clickPage (*child);
        }
    };
    for (auto [name, file] : { std::pair<const char*, const char*> { "TRAITEMENT", "3-traitement.png" }, { "TRANSPOSITION", "4-transposition.png" } })
    {
        setenv ("RK_PAGE_NAME", name, 1);
        clickPage (*ed);
        pump (200);
        save (*ed, dir.getChildFile (file));
    }
    // the player's pages, with a sound that shows something on each
    MainView* view = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* v = dynamic_cast<MainView*> (child)) { view = v; return; }
            find (*child);
        }
    };
    find (*ed);
    auto set = [&] (int id, float v)
    {
        auto* param = proc.params.getParameter (snd::def (id).id);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    set (snd::fModel, 2.0f);
    set (snd::cutoff, 1400.0f);
    set (snd::reso, 0.55f);
    set (snd::fEnv, 0.4f);
    set (snd::fm, 0.2f);
    set (snd::dModel, 3.0f);
    set (snd::dAmt, 0.45f);
    set (snd::spOn, 1.0f);
    set (snd::rvOn, 1.0f);
    set (snd::playMode, 1.0f);
    set (snd::speed, 0.4f);
    set (snd::m2Type, 1.0f);
    proc.player.setRoute (0, snd::cutoff, 0.35f);
    proc.player.setRoute (1, snd::cutoff, -0.2f);
    proc.player.setRoute (1, snd::pmIndex, 0.6f);
    proc.player.setRoute (2, snd::spMix, 0.5f);
    proc.player.setSceneLock (0, snd::cutoff, 300.0f);
    proc.player.setSceneLock (1, snd::cutoff, 6000.0f);
    proc.player.setSceneLock (1, snd::rvMix, 0.7f);
    set (snd::xfade, 0.35f);
    if (view != nullptr)
    {
        const char* names[] = { "5-son.png", "6-lecture.png", "7-filtre.png", "8-spectre.png", "9-espace.png", "10-mods.png", "11-scenes.png" };
        for (int pg = 0; pg < 7; ++pg)
        {
            view->showPage (1, pg);
            for (int b = 0; b < 120; ++b)
            {
                buf.clear();
                midi.clear();
                proc.processBlock (buf, midi);
                if (b % 8 == 0)
                    pump (34);
            }
            pump (200);
            save (*ed, dir.getChildFile (names[pg]));
        }
        view->showPage (0, 0);
    }
    for (int n : { 48, 55, 63 })
        proc.audition (n, 0, false);

    // the project keeps the sound: settings, routes, steps, scenes
    {
        proc.player.steps[1][3] = -0.75f;
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        ReplikantProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        const bool same = std::abs (other.player.get (snd::cutoff) - proc.player.get (snd::cutoff)) < 0.01f
                       && std::abs (other.player.routeAmount (1, snd::pmIndex) - 0.6f) < 1.0e-4f
                       && std::abs (other.player.sceneLock (1, snd::rvMix) - 0.7f) < 1.0e-4f
                       && std::abs (other.player.steps[1][3].load() + 0.75f) < 1.0e-4f
                       && (int) other.player.get (snd::fModel) == 2;
        std::printf ("state round trip: %s\n", same ? "same" : "DIFFERENT");
    }
    ed.reset();
    pump (100);
    return 0;
}
