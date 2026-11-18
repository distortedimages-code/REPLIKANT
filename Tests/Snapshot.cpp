// Not a test: plays the synth, runs a short capture on the test synth through the real processor, and draws the
// window to PNG files (each mode and page), to look at the interface without a Mac.
//   Snapshot <folder>

#include <JuceHeader.h>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"
#include "../Source/ui/Machine.h"

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
    te::Machine* view = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* v = dynamic_cast<te::Machine*> (child)) { view = v; return; }
            find (*child);
        }
    };
    find (*ed);
    if (view == nullptr)
        return 1;
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();
            proc.processBlock (buf, midi);
            if (b % 8 == 0)
                pump (20);
        }
        view->refreshNow();
        pump (150);
    };
    auto set = [&] (int id, float v)
    {
        auto* param = proc.params.getParameter (snd::def (id).id);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    auto shoot = [&] (const char* name) { save (*ed, dir.getChildFile (name)); };

    // the synth, a chord held, a few things moving
    pump (300);
    shoot ("0-start.png");
    set (snd::fModel, 1.0f);
    set (snd::cutoff, 1800.0f);
    set (snd::reso, 0.45f);
    set (snd::unison, 3.0f);
    set (snd::rvOn, 1.0f);
    set (snd::chOn, 1.0f);
    proc.player.setRoute (0, snd::oaPos, 0.25f);
    proc.player.setRoute (0, snd::cutoff, 0.2f);
    for (int n : { 48, 55, 63 })
        proc.audition (n, 100, true);
    const char* synthShots[] = { "1-synthe-osc-a.png", "2-synthe-osc-b.png", "3-synthe-voix.png", "4-synthe-filtre.png", "5-synthe-ampli.png",
                                 "6-synthe-fx.png", "7-synthe-mods.png", "8-synthe-reglages.png" };
    for (int pg = 0; pg < 8; ++pg)
    {
        view->showPage (pg);
        run (60);
        shoot (synthShots[pg]);
    }
    // the filter's other banks and models: the drive on tape, the MS-20 high-pass, the light theme
    view->showPage (3);
    set (snd::dModel, 3.0f);
    set (snd::dAmt, 0.5f);
    view->showBank (2);
    run (40);
    shoot ("16-synthe-drive-bande.png");
    set (snd::fModel, 2.0f);
    set (snd::fType, 2.0f);
    view->showBank (0);
    run (40);
    shoot ("17-synthe-filtre-ms20.png");
    te::setTheme (te::Theme::clair);
    ed->sendLookAndFeelChange();
    run (20);
    shoot ("18-clair-filtre.png");
    view->showPage (5);
    run (20);
    shoot ("19-clair-fx.png");
    te::setTheme (te::Theme::noirEtOr);
    ed->sendLookAndFeelChange();
    set (snd::fModel, 1.0f);
    set (snd::fType, 0.0f);
    set (snd::dModel, 0.0f);
    // every picto, on the screen and on a key
    {
        struct Sheet : juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                using te::Picto;
                const std::pair<Picto, const char*> all[] = {
                    { Picto::sine, "synthé" }, { Picto::drum, "drum" }, { Picto::cassette, "sampler" }, { Picto::cycles, "osc a" },
                    { Picto::saw, "osc b" }, { Picto::voices, "voix" }, { Picto::filterBox, "filtre" }, { Picto::envelope, "ampli" },
                    { Picto::chain, "fx" }, { Picto::cable, "mods" }, { Picto::sliders, "réglages" }, { Picto::playhead, "lecture" },
                    { Picto::loop, "boucle" }, { Picto::fork, "hauteur" }, { Picto::lowpass, "passe-bas" }, { Picto::bandpass, "passe-bande" },
                    { Picto::highpass, "passe-haut" }, { Picto::notch, "réjecteur" }, { Picto::off, "aucun" }, { Picto::ladder, "moog" },
                    { Picto::ms20, "ms-20" }, { Picto::sem, "sem" }, { Picto::oscar, "oscar" }, { Picto::wasp, "wasp" },
                    { Picto::polivoks, "polivoks" }, { Picto::tube, "préampli" }, { Picto::console, "console" }, { Picto::reels, "bande" },
                    { Picto::spectrum, "spectre" }, { Picto::shift, "pitch" }, { Picto::chorus, "chorus" }, { Picto::crush, "crush" },
                    { Picto::space, "espace" }, { Picto::steps, "pas" }, { Picto::random, "hasard" }, { Picto::follower, "suiveur" },
                    { Picto::jack, "source" }, { Picto::keys, "notes" }, { Picto::clock, "durées" }, { Picto::onset, "attaque" },
                    { Picto::vuMeter, "niveau" }, { Picto::resample, "rééchantillonner" }, { Picto::record, "capture" } };
                g.fillAll (te::col::body);
                te::screen (g, getLocalBounds().toFloat().reduced (16.0f));
                const int cols = 9;
                const float cw = (getWidth() - 64.0f) / (float) cols, ch = 118.0f;
                int i = 0;
                for (auto& [p, name] : all)
                {
                    const float x = 32.0f + (float) (i % cols) * cw, y = 32.0f + (float) (i / cols) * ch;
                    te::picto (g, p, { x + cw * 0.5f - 30.0f, y + 6.0f, 60.0f, 60.0f }, te::col::sBlue, 1.8f);
                    te::picto (g, p, { x + cw * 0.5f - 9.0f, y + 74.0f, 18.0f, 18.0f }, te::col::screenText, 1.4f);
                    te::text (g, juce::String::fromUTF8 (name), { x, y + 94.0f, cw, 16.0f }, te::mono (10.0f), te::col::screenDim, juce::Justification::centred);
                    ++i;
                }
            }
        } sheet;
        sheet.setSize (1360, 640);
        save (sheet, dir.getChildFile ("20-pictos.png"));
    }
    for (int n : { 48, 55, 63 })
        proc.audition (n, 0, false);
    run (40);

    // the capture: the plan, then a session going
    view->showCapture (true);
    view->setCaptureKind (te::Machine::notesKind);
    view->showPage (1);
    run (10);
    shoot ("9-capture-plan.png");
    juce::String why;
    if (! proc.startSampling (why))
    {
        std::printf ("can't start: %s\n", why.toRawUTF8());
        return 1;
    }
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
            view->refreshNow();
            pump (100);
            shoot ("10-capture-en-cours.png");
            midShot = true;
        }
    }
    for (int i = 0; i < 600; ++i)
    {
        pump (100);
        auto ins = proc.getInstrument();
        if (ins != nullptr && ! ins->keys.empty() && ! proc.status().busy)
            break;
    }
    view->refreshNow();     // the capture over: the sampler plays it

    // the sampler, its pages
    set (snd::loopXfade, 0.4f);
    set (snd::loopStart, 0.2f);
    for (int n : { 48, 55 })
        proc.audition (n, 100, true);
    const char* samplerShots[] = { "11-sampler-lecture.png", "12-sampler-boucle.png", "13-sampler-hauteur.png", "14-sampler-filtre.png" };
    for (int pg = 0; pg < 4; ++pg)
    {
        view->showPage (pg);
        run (60);
        shoot (samplerShots[pg]);
    }
    // a mod armed: the encoders show its routes
    proc.player.setRoute (1, snd::cutoff, -0.3f);
    proc.player.setSceneLock (0, snd::cutoff, 300.0f);
    proc.player.setSceneLock (1, snd::cutoff, 6000.0f);
    set (snd::xfade, 0.35f);
    set (snd::wtf, 0.3f);
    view->showPage (3);
    run (60);
    shoot ("15-sampler-filtre-wtf.png");
    for (int n : { 48, 55 })
        proc.audition (n, 0, false);
    run (40);

    // RÉÉCHANTILLONNER: the chord through the reverb, two seconds recorded, then the loop injected into the sampler
    view->showCapture (true);
    view->setCaptureKind (te::Machine::resampleKind);
    run (10);
    shoot ("21-reechantillonner.png");
    set (snd::rvOn, 1.0f);
    set (snd::chOn, 1.0f);
    proc.resampleOptions.length = 0;
    proc.resampleOptions.synced = false;
    for (int n : { 48, 55, 60 })
        proc.audition (n, 100, true);
    run (60);
    {
        juce::String why;
        if (! proc.startResample (why))
            std::printf ("can't resample: %s\n", why.toRawUTF8());
    }
    run ((int) (1.2 * 48000 / 256));
    shoot ("22-reechantillonne.png");
    for (int i = 0; i < 2000 && proc.player.resampleState() != Player::Resample::looping; ++i)
        run (4);
    for (int n : { 48, 55, 60 })
        proc.audition (n, 0, false);
    for (int i = 0; i < 40 && proc.lastResample.isEmpty(); ++i)
        run (8);
    run (40);
    std::printf ("resample: %s, %s\n", proc.lastResample.toRawUTF8(),
                 proc.player.resampleState() == Player::Resample::looping ? "looping" : "not looping");
    shoot ("23-sampler-reechantillon.png");
    proc.stopResample();
    run (20);

    // CAPTURE CYCLES: the test synth's filter swept by CC 74, eight takes, then the synth plays them
    view->showCapture (true);
    view->setCaptureKind (te::Machine::cyclesKind);
    view->showPage (1);
    proc.cycleOptions.count = 0;
    run (10);
    shoot ("24-cycles-plan.png");
    {
        juce::String why;
        if (! proc.startCycles (why))
            std::printf ("can't capture cycles: %s\n", why.toRawUTF8());
        int cb = 0;
        bool shot = false;
        while (proc.isSampling() && cb < 200000)
        {
            buf.clear();
            midi.clear();
            proc.processBlock (buf, midi);
            if (++cb % 40 == 0)
                pump (2);
            if (! shot && proc.status().done >= 4)
            {
                view->refreshNow();
                pump (100);
                shoot ("25-cycles-en-cours.png");
                shot = true;
            }
        }
        for (int i = 0; i < 100; ++i)
        {
            pump (50);
            auto ins = proc.getInstrument();
            if (ins != nullptr && ins->cycles != nullptr && ! proc.status().busy)
                break;
        }
        view->refreshNow();
        for (int n : { 48, 55, 60 })
            proc.audition (n, 100, true);
        run (60);
        shoot ("26-synthe-cycles-captures.png");
        for (int n : { 48, 55, 60 })
            proc.audition (n, 0, false);
        run (20);
        auto ins = proc.getInstrument();
        std::printf ("cycles: %d\n", ins != nullptr && ins->cycles != nullptr ? ins->cycles->count() : 0);
    }

    // the project keeps the sound: settings, routes, steps, scenes
    {
        proc.player.steps[1][3] = -0.75f;
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        ReplikantProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        const bool same = std::abs (other.player.get (snd::cutoff) - proc.player.get (snd::cutoff)) < 0.01f
                       && std::abs (other.player.routeAmount (1, snd::cutoff) + 0.3f) < 1.0e-4f
                       && std::abs (other.player.sceneLock (1, snd::cutoff) - 6000.0f) < 1.0f
                       && std::abs (other.player.steps[1][3].load() + 0.75f) < 1.0e-4f
                       && (int) other.player.get (snd::engine) == (int) proc.player.get (snd::engine);
        std::printf ("state round trip: %s\n", same ? "same" : "DIFFERENT");
    }
    ed.reset();
    pump (100);
    return 0;
}
