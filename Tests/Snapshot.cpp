// Not a test: runs a short session on the test synth through the real processor, and draws the window to PNG
// files (during the session, then on each page with the instrument), to look at the interface without a Mac.
//   Snapshot <folder>

#include <JuceHeader.h>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"

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
    for (int n : { 48, 55, 63 })
        proc.audition (n, 0, false);
    ed.reset();
    pump (100);
    return 0;
}
