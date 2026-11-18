// Not a test: plays the synth, runs a short capture on the test synth through the real processor, and draws the
// window to PNG files (each mode and page), to look at the interface without a Mac.
//   Snapshot <folder>

#include <JuceHeader.h>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"
#include "../Source/ui/Machine.h"
#include "../Source/radio/RadioNet.h"

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
    const char* synthShots[] = { "1-synthe-osc-a.png", "2-synthe-osc-b.png", "27-synthe-orbite.png", "28-synthe-mixer.png", "3-synthe-voix.png",
                                 "4-synthe-filtre.png", "5-synthe-ampli.png", "6-synthe-fx.png", "7-synthe-mods.png", "8-synthe-reglages.png" };
    for (int pg = 0; pg < 10; ++pg)
    {
        set (snd::orOn, pg == 2 || pg == 3 ? 1.0f : 0.0f);      // ORBITE on for its pages
        view->showPage (pg);
        run (60);
        shoot (synthShots[pg]);
    }
    // ORBITE's satellites, in CASCADE
    set (snd::orOn, 1.0f);
    set (snd::orAlgo, 1.0f);
    view->showPage (2);
    view->showBank (1);
    run (40);
    shoot ("29-synthe-orbite-sat1.png");
    set (snd::orAlgo, 0.0f);
    set (snd::orOn, 0.0f);
    view->showBank (0);
    // SPECTRE at work: its partials and their trail
    set (snd::spOn, 1.0f);
    view->showPage (7);
    view->showBank (1);
    run (90);
    shoot ("32-synthe-spectre.png");
    // its band turned into a notch, its partials warped, in cross-fading saws
    set (snd::spNotch, 1.0f);
    set (snd::spWidth, 0.25f);
    set (snd::spCentre, 0.42f);
    set (snd::spTrans, 1.0f);
    view->showBank (2);
    run (60);
    shoot ("45-spectre-notch.png");
    set (snd::spWarp, 0.45f);
    set (snd::spWave, 6.0f);
    set (snd::spFine, 3.5f);
    view->showBank (3);
    run (90);
    shoot ("46-spectre-warp.png");
    for (int id : { (int) snd::spNotch, (int) snd::spTrans, (int) snd::spWarp, (int) snd::spWave, (int) snd::spFine })
        set (id, 0.0f);
    set (snd::spWidth, 1.0f);
    set (snd::spCentre, 0.5f);
    set (snd::spOn, 0.0f);
    view->showBank (0);
    // the filter's other banks and models: the drive on tape, the MS-20 high-pass
    view->showPage (5);
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
    const char* samplerShots[] = { "11-sampler-lecture.png", "12-sampler-boucle.png", "13-sampler-hauteur.png", "30-sampler-orbite.png",
                                   "31-sampler-mixer.png", "14-sampler-filtre.png" };
    for (int pg = 0; pg < 6; ++pg)
    {
        set (snd::orOn, pg == 4 ? 1.0f : 0.0f);
        set (snd::tune, pg == 2 ? 7.35f : 0.0f);                // the dial turned a little, on its page
        view->showPage (pg);
        run (60);
        shoot (samplerShots[pg]);
    }
    // the SUB, OCTAVE, on the mixer's second bank
    set (snd::subOn, 1.0f);
    set (snd::subMode, 1.0f);
    view->showPage (4);
    view->showBank (1);
    run (40);
    shoot ("33-sampler-sub.png");
    set (snd::subOn, 0.0f);
    view->showBank (0);
    // a mod armed: the encoders show its routes
    proc.player.setRoute (1, snd::cutoff, -0.3f);
    proc.player.setSceneLock (0, snd::cutoff, 300.0f);
    proc.player.setSceneLock (1, snd::cutoff, 6000.0f);
    set (snd::xfade, 0.35f);
    set (snd::wtf, 0.3f);
    view->showPage (5);
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

    // RADIO, with the stand-ins of Tests/radio/radio_mock.py running at argv[2] (RADIO_MOCK_SHOWCASE=1), else left out
    if (argc > 2)
    {
        const juce::String base (argv[2]);
        auto& net = proc.radioNet();
        net.dirOverride = base;
        net.listOverride = base + "/public/";
        RadioNet::Source s;
        s.kind = 1;
        s.name = "Radio Nowhere";
        s.url = base + "/stream.mp3";
        s.kbps = 128;
        s.country = "FR";
        net.setSource (0, s);
        RadioNet::Source w;
        w.kind = 2;
        w.host = "127.0.0.1";
        w.port = base.fromLastOccurrenceOf (":", false, false).getIntValue() + 1;
        w.khz = 9420.0;
        w.mode = "am";
        w.rxName = "Night owl receiver, long wire";
        net.setSource (1, w);
        set (snd::rdRx, 0.0f);
        set (snd::fModel, 0.0f);
        set (snd::rvOn, 0.0f);
        set (snd::chOn, 0.0f);
        view->showMode (te::Machine::radioMode);
        auto realtime = [&] (double secs)
        {
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            double done = 0.0;
            while (juce::Time::getMillisecondCounterHiRes() - t0 < secs * 1000.0)
            {
                const double due = (juce::Time::getMillisecondCounterHiRes() - t0) * 48.0;
                while (done + 256 <= due)
                {
                    buf.clear();
                    midi.clear();
                    proc.processBlock (buf, midi);
                    done += 256;
                }
                pump (5);
            }
            view->refreshNow();
            pump (60);
        };
        view->showPage (1);
        set (snd::rdFade, 0.2f);
        set (snd::rdCrackle, 0.12f);
        set (snd::rdStatic, 0.08f);
        realtime (11.0);
        juce::String why;
        if (! proc.radioGrab (why))
            std::printf ("grab: %s\n", why.toRawUTF8());
        for (int i = 0; i < 100 && proc.radioTake() == nullptr; ++i)
            realtime (0.05);
        std::printf ("radio take: %d pads\n", proc.radioTake() != nullptr ? (int) proc.radioTake()->slices.size() : -1);
        realtime (1.0);
        proc.audition (radio::firstPadNote + 2, 110, true);
        proc.audition (radio::firstPadNote + 5, 90, true);
        realtime (0.12);
        shoot ("34-radio-ecoute.png");
        proc.audition (radio::firstPadNote + 2, 0, false);
        proc.audition (radio::firstPadNote + 5, 0, false);
        view->showPage (3);
        realtime (0.6);
        shoot ("35-radio-prise.png");
        view->showPage (2);
        realtime (0.6);
        shoot ("38-radio-air.png");
        view->showPage (0);
        realtime (2.5);
        shoot ("36-radio-tune.png");
        set (snd::rdRx, 1.0f);
        realtime (5.0);
        shoot ("37-radio-ondes-courtes.png");
        set (snd::rdRx, 0.0f);
        set (snd::rdFade, 0.0f);
        set (snd::rdCrackle, 0.0f);
    }

    // DRUM: a beat on the synth's voices (the internal clock running), a p-lock, the patterns and their chain; the
    // radio's take on track 6 when there is one
    {
        set (snd::fModel, 0.0f);
        set (snd::rvOn, 0.0f);
        set (snd::chOn, 0.0f);
        set (snd::tempo, 112.0f);
        set (snd::drSwing, 0.56f);
        view->showMode (te::Machine::drumMode);
        auto& d = proc.drum;
        auto& pat = d.pattern (0);
        auto steps = [&] (int t, std::initializer_list<int> ss) { for (int s : ss) pat.tr[t].trig[s] = 1; };
        steps (0, { 0, 6, 8, 11, 14 });
        steps (1, { 4, 12 });
        steps (2, { 0, 2, 4, 6, 8, 10, 12, 14 });
        steps (3, { 7, 15 });
        steps (4, { 10, 13 });
        steps (6, { 3, 9 });
        steps (7, { 15 });
        pat.tr[0].rat[14] = 2;
        pat.tr[2].rat[14] = 3;
        pat.tr[2].lock[6][drum::pDecay] = 0.62f;
        pat.tr[2].lock[10][drum::pMicro] = 0.25f;
        pat.tr[2].lock[10][drum::pProb] = 0.6f;
        pat.tr[6].len = 12;
        pat.tr[7].len = 32;
        steps (7, { 19, 27, 30 });
        if (proc.radioTake() != nullptr)
        {
            juce::String why;
            auto said = proc.onMessage;                 // (its message left out of the shots)
            proc.onMessage = nullptr;
            proc.radioToDrum (-1, 5, why);
            proc.onMessage = said;
            view->showMode (te::Machine::drumMode);
        }
        for (int i : { 1, 2, 9, 17 })
        {
            d.pattern (i) = pat;
            d.pattern (i).used = 1;
        }
        d.pattern (1).tr[1].trig[14] = 1;
        proc.setClockRunning (true);
        proc.drumTrack = 0;
        view->showPage (0);
        run (240);
        shoot ("39-drum-trig.png");
        view->showPage (2);
        run (90);
        shoot ("40-drum-pitch.png");
        proc.drumTrack = 2;
        view->drumLift ({ 6, 10 });
        view->showPage (3);
        run (90);
        shoot ("41-drum-plock.png");
        view->drumLift ({});
        pat.tr[4].p[drum::pFilter] = 0;
        pat.tr[4].p[drum::pCutoff] = 380.0f;
        pat.tr[4].p[drum::pReso] = 0.55f;
        pat.tr[4].p[drum::pFEnv] = 0.62f;
        pat.tr[4].p[drum::pFDecay] = 0.3f;
        proc.drumTrack = 4;
        view->showPage (4);
        run (90);
        shoot ("44-drum-tone.png");
        d.setChain ({ 0, 1, 0, 2 });
        view->drumShowPatterns (true);
        view->showPage (5);
        run (90);
        shoot ("42-drum-patterns.png");
        view->drumShowPatterns (false);
        d.setChain ({});
        if (proc.drumSound (5) != nullptr)
        {
            proc.drumTrack = 5;
            view->showPage (1);
            run (90);
            shoot ("43-drum-slices.png");
        }
        proc.setClockRunning (false);
        run (40);
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
