#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../ui/Text.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

//==============================================================================
// Where the notes for the synth go: the MIDI port, the host's MIDI out, or the test synth.
struct ReplikantProcessor::Sink : Recorder::MidiSink
{
    ReplikantProcessor& p;
    explicit Sink (ReplikantProcessor& o) : p (o) {}
    void send (const juce::MidiMessage& m) override
    {
        if (p.sourceAudio.load() == (int) Source::testSynth)
        {
            p.testSynth.handle (m);
            return;
        }
        if (auto* out = p.port.load())
            out->sendMessageNow (m);
        if (p.hostMidiOut.load())
            p.outMidi.addEvent (m, 0);
    }
};

namespace
{
    // the name a host shows: the setting with its section
    juce::String hostName (int id)       { return juce::String::fromUTF8 (snd::longName (id).c_str()); }

    // the settings that were there in 0.1 keep their version
    int idVersion (int id)
    {
        if (id >= snd::engine)
            return 3;      // (0.3)
        return id == snd::volume || id == snd::attack || id == snd::release || id == snd::velocity || id == snd::bend ? 1 : 2;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout ReplikantProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < snd::count; ++i)
    {
        const auto& d = snd::def (i);
        const ParameterID pid { d.id, idVersion (i) };
        const auto name = hostName (i);
        if (d.kind == snd::Kind::choice)
        {
            StringArray c;
            for (auto& t : snd::choicesOf (i))
                c.add (String::fromUTF8 (t.c_str()));
            l.add (std::make_unique<AudioParameterChoice> (pid, name, c, (int) d.def));
        }
        else if (d.kind == snd::Kind::toggle)
            l.add (std::make_unique<AudioParameterBool> (pid, name, d.def > 0.5f));
        else
        {
            NormalisableRange<float> range (d.lo, d.hi,
                [i] (float, float, float t) { return snd::fromNorm (i, t); },
                [i] (float, float, float v) { return snd::toNorm (i, v); },
                [i] (float, float, float v) { return snd::legal (i, v); });
            auto attributes = AudioParameterFloatAttributes()
                .withLabel (d.unit)
                .withStringFromValueFunction ([i] (float v, int) { return String::fromUTF8 (snd::text (i, v).c_str()); });
            l.add (std::make_unique<AudioParameterFloat> (pid, name, range, d.def, attributes));
        }
    }
    return l;
}

ReplikantProcessor::ReplikantProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      params (*this, nullptr, "REPLIKANT", layout()),
      updater ([this]() -> juce::PropertiesFile& { return *globalSettings; })
{
    instrumentName = "Mon synthé"_fr;
    for (int i = 0; i < snd::count; ++i)
        player.bind (i, params.getRawParameterValue (snd::def (i).id));
    clock.bindTempo (params.getRawParameterValue ("tempo"));

    juce::PropertiesFile::Options o;
    o.applicationName = "REPLIKANT";
    o.filenameSuffix = ".settings";
    o.folderName = "REPLIKANT";
    o.osxLibrarySubFolder = "Application Support";
    globalSettings = std::make_unique<juce::PropertiesFile> (o);
    loadGlobalSettings();

    worker.onInstrument = [this] (std::shared_ptr<Instrument> ins) { publish (std::move (ins)); };
    worker.onStatus = [this] { sendChangeMessage(); };
    startTimerHz (10);
}

ReplikantProcessor::~ReplikantProcessor()
{
    stopTimer();
    worker.onInstrument = nullptr;
    worker.onStatus = nullptr;
    recorder.stop();
    port = nullptr;
}

bool ReplikantProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto in = l.getMainInputChannelSet();
    if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void ReplikantProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    player.prepare (sr, block);
    inputCopy.setSize (2, juce::jmax (block, 4096) * 2, false, true, false);
    testSynth.prepare (sr);
    toSynth.reset (sr);
    toPlayer.reset (sr);
    synthBuffer.setSize (2, juce::jmax (block, 4096) * 2, false, true, false);
    playMidi.ensureSize (4096);
    synthMidi.ensureSize (2048);
    outMidi.ensureSize (4096);

   #if JucePlugin_Build_Standalone
    // the standalone app mutes its input by default (feedback): here the input is the whole point
    if (wrapperType == wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            juce::MessageManager::callAsync ([holder] { holder->getMuteInputValue().setValue (false); });
   #endif
}

void ReplikantProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // a new instrument to play
    if (hasPending.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (instrumentLock);
        if (lock.isLocked())
        {
            player.setInstrument (pending);
            audioUses = pending;
            hasPending = false;
        }
    }
    outMidi.clear();
    Sink sink (*this);
    const bool sampling = recorder.isRunning();
    if (sampling && ! wasSampling)
        player.allNotesOff (true);
    wasSampling = sampling;

    recorder.beginBlock (sink);

    // the editor's test notes, and the notes played in when they go to the synth
    {
        synthMidi.clear();
        toSynth.removeNextBlockOfMessages (synthMidi, n);
        for (const auto meta : synthMidi)
            sink.send (meta.getMessage());
        if (thruAudio.load() && ! sampling)
            for (const auto meta : midi)
                if (! meta.getMessage().isSysEx())
                    sink.send (meta.getMessage());
    }

    // the sound of the synth: the input, the sidechain, or the test synth
    const int src = sourceAudio.load();
    auto input = getBusBuffer (buffer, true, 0);
    const float* chans[2] = { nullptr, nullptr };
    int numIn = 0;
    if (src == (int) Source::testSynth)
    {
        const int m = juce::jmin (n, synthBuffer.getNumSamples());
        testSynth.render (synthBuffer.getArrayOfWritePointers(), 2, m);
        if (m < n)
            for (int c = 0; c < 2; ++c)
                synthBuffer.clear (c, m, synthBuffer.getNumSamples() - m);
        chans[0] = synthBuffer.getReadPointer (0);
        chans[1] = synthBuffer.getReadPointer (1);
        numIn = 2;
    }
    else if (src == (int) Source::sidechain && getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        numIn = juce::jmin (2, sc.getNumChannels());
        for (int c = 0; c < numIn; ++c)
            chans[c] = sc.getReadPointer (c);
    }
    else
    {
        numIn = juce::jmin (2, input.getNumChannels());
        for (int c = 0; c < numIn; ++c)
            chans[c] = input.getReadPointer (c);
    }
    if (numIn > 0 && n <= synthBuffer.getNumSamples())
        recorder.capture (chans, numIn, n);

    // the transport (the modulators and the clock follow it)
    Player::Transport transport;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            transport.playing = pos->getIsPlaying();
            if (auto b = pos->getBpm())
                transport.bpm = *b;
            if (auto q = pos->getPpqPosition())
                transport.ppq = *q;
            else
                transport.playing = false;
        }
    if (transport.bpm <= 0.0)
        transport.bpm = player.get (snd::tempo);

    // the input, kept before the output is written over it (same buffer)
    const int mon = sampling ? 1 : monitorAudio.load();
    const float* kept[2] = { nullptr, nullptr };
    if (numIn > 0 && n <= inputCopy.getNumSamples())
    {
        for (int c = 0; c < numIn; ++c)
        {
            inputCopy.copyFrom (c, 0, chans[c], n);
            kept[c] = inputCopy.getReadPointer (c);
        }
    }

    // what is heard: the synth (monitor) and the instrument
    auto out = getBusBuffer (buffer, false, 0);
    const bool direct = mon == 1;
    if (src == (int) Source::testSynth)
    {
        for (int c = 0; c < out.getNumChannels(); ++c)
        {
            if (direct && n <= synthBuffer.getNumSamples())
                out.copyFrom (c, 0, synthBuffer, juce::jmin (c, 1), 0, n);
            else
                out.clear (c, 0, n);
        }
    }
    else if (src == (int) Source::sidechain)
    {
        // the main input goes through untouched; the sidechain is only heard when monitoring
        if (direct && numIn > 0)
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addFrom (c, 0, chans[juce::jmin (c, numIn - 1)], n);
    }
    else if (! direct)
        out.clear();
    for (int c = out.getNumChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    playMidi.clear();
    toPlayer.removeNextBlockOfMessages (playMidi, n);
    if (! thruAudio.load())
        playMidi.addEvents (midi, 0, n, 0);
    if (! sampling)
        player.render (out, playMidi, transport, kept[0] != nullptr ? kept : nullptr, numIn, mon == 2);

    clock.process (transport.playing, transport.ppq, transport.bpm, n, sampleRate, outMidi);
    midi.swapWith (outMidi);
}

//==============================================================================
void ReplikantProcessor::publish (std::shared_ptr<Instrument> ins)
{
    {
        const juce::SpinLock::ScopedLockType lock (instrumentLock);
        current = ins;
        pending = ins.get();
        hasPending = true;
        alive.push_back (ins);
    }
    sendChangeMessage();
}

void ReplikantProcessor::collectOldInstruments()
{
    const juce::SpinLock::ScopedLockType lock (instrumentLock);
    const auto* inUse = audioUses.load();
    alive.erase (std::remove_if (alive.begin(), alive.end(), [&] (const std::shared_ptr<Instrument>& p)
    {
        return p != current && p.get() != inUse && p.get() != pending;
    }), alive.end());
}

InstrumentPtr ReplikantProcessor::getInstrument() const
{
    const juce::SpinLock::ScopedLockType lock (instrumentLock);
    return current;
}

void ReplikantProcessor::timerCallback()
{
    collectOldInstruments();
    resampleTick();
    // the clock a session started stops with it
    if (clockStartedBySampling && ! isSampling())
    {
        clockStartedBySampling = false;
        clock.setRunning (false);
        sendChangeMessage();
    }
    // the sound goes into the instrument's folder once it stops moving (2 s): opened again, it sounds the same
    if (auto ins = getInstrument(); ins != nullptr && ins->folder.isDirectory() && ! isSampling())
    {
        static int tick = 0;
        if (++tick % 5 == 0)
        {
            const auto now = soundSignature();
            if (now != pendingSound)
            {
                pendingSound = now;
                soundCountdown = 4;
            }
            else if (now != savedSound && --soundCountdown <= 0)
            {
                savedSound = now;
                const auto folder = ins->folder;
                const auto sound = soundToVar();
                worker.post ({}, [folder, sound] { instrumentio::saveSound (folder, sound); });
            }
        }
    }
    // a recording retuned by hand
    if (retune.index >= 0 && --retune.countdown <= 0)
    {
        const auto r = retune;
        retune.index = -1;
        if (auto ins = getInstrument(); ins != nullptr && ! isSampling() && r.index < (int) ins->samples.size())
        {
            auto next = std::make_shared<Instrument> (*ins);
            const auto& chosen = next->samples[(size_t) r.index];
            const int sent = chosen.sent, oldRoot = chosen.root;
            for (auto& smp : next->samples)
                if (sent >= 0 ? smp.sent == sent : smp.root == oldRoot)
                {
                    smp.root = r.root;
                    smp.detuneCents = (float) r.cents;
                    smp.pitchConfident = true;
                    smp.manualTune = true;
                }
            rk::assignZones (next->samples, next->session);
            next->keys.clear();
            next->keyOf.clear();
            next->rebuildIndex();
            if (next->folder.isDirectory())
                instrumentio::save (*next, next->folder);
            worker.renderAndPublish (next, true);
        }
    }
    // the mapping settings of the instrument: re-rendered once they stop moving
    if (mappingDirty && --mappingCountdown <= 0)
    {
        mappingDirty = false;
        if (auto ins = getInstrument(); ins != nullptr && ! isSampling())
        {
            auto next = std::make_shared<Instrument> (*ins);
            next->session.placement = session.placement;
            next->session.extend = session.extend;
            next->session.algo = session.algo;
            next->session.formant = session.formant;
            next->session.renderKeys = session.renderKeys;
            next->session.tuning = session.tuning;
            next->session.tuneCents = session.tuneCents;
            rk::assignZones (next->samples, next->session);
            next->keys.clear();
            next->keyOf.clear();
            next->rebuildIndex();
            if (next->folder.isDirectory())
                instrumentio::save (*next, next->folder);
            worker.renderAndPublish (next, true);
        }
    }
}

void ReplikantProcessor::settingsChanged()
{
    sourceAudio = (int) options.source;
    monitorAudio = options.monitor;
    thruAudio = options.thru;
    hostMidiOut = options.midiPort == "host";
    saveGlobalSettings();
    sendChangeMessage();
}

void ReplikantProcessor::retuneSample (int index, int root, double cents)
{
    retune.index = index;
    retune.root = juce::jlimit (0, 127, root);
    retune.cents = juce::jlimit (-100.0, 100.0, cents);
    retune.countdown = 6;      // 0.6 s after the last change
}

void ReplikantProcessor::mappingChanged()
{
    mappingDirty = true;
    mappingCountdown = 6;   // 0.6 s after the last change
    settingsChanged();
}

bool ReplikantProcessor::startSampling (juce::String& why)
{
    if (isSampling())
    {
        why = "Une session est en cours."_fr;
        return false;
    }
    if (options.source != Source::testSynth && port.load() == nullptr && ! hostMidiOut.load())
    {
        why = "Choisis la sortie MIDI qui va au synthé (SOURCE > MIDI)."_fr;
        return false;
    }
    if (options.source == Source::sidechain && (getBusCount (true) < 2 || ! getBus (true, 1)->isEnabled()))
    {
        why = "Le sidechain n'est pas branché : choisis-le dans ton DAW, ou prends l'entrée principale."_fr;
        return false;
    }
    auto jobs = rk::planJobs (session);
    if (jobs.empty())
    {
        why = "Aucune note à enregistrer : vérifie la plage de notes."_fr;
        return false;
    }
    Recorder::Plan plan;
    plan.jobs = std::move (jobs);
    plan.hold = session.oneShot ? juce::jmin (session.hold, 0.5) : session.hold;
    plan.releaseMax = session.oneShot ? juce::jmax (session.releaseMax, session.hold) : session.releaseMax;
    plan.pause = session.pause;
    plan.channel = juce::jlimit (1, 16, options.channel);
    plan.program = options.program;
    const auto folder = Worker::uniqueFolder (instrumentsRoot(), instrumentName);
    const int channels = options.source == Source::testSynth ? 2 : juce::jmax (1, juce::jmin (2, getTotalNumInputChannels()));
    if (! worker.startSession (instrumentName, session, plan, sampleRate, channels, folder))
    {
        why = "La session n'a pas pu démarrer."_fr;
        return false;
    }
    lastCaptureWasCycles = false;
    if (clockWithSampling && clock.getMode() == MidiClock::Mode::internal && ! clock.isRunning())
    {
        clock.setRunning (true);
        clockStartedBySampling = true;
    }
    sendChangeMessage();
    return true;
}

bool ReplikantProcessor::startCycles (juce::String& why)
{
    if (isSampling())
    {
        why = "Une session est en cours."_fr;
        return false;
    }
    if (options.source != Source::testSynth && port.load() == nullptr && ! hostMidiOut.load())
    {
        why = "Choisis la sortie MIDI qui va au synthé (SOURCE > MIDI)."_fr;
        return false;
    }
    if (options.source == Source::sidechain && (getBusCount (true) < 2 || ! getBus (true, 1)->isEnabled()))
    {
        why = "Le sidechain n'est pas branché : choisis-le dans ton DAW, ou prends l'entrée principale."_fr;
        return false;
    }
    const auto& c = cycleOptions;
    const int n = CycleOptions::countOf (c.count);
    Recorder::Plan plan;
    for (int i = 0; i < n; ++i)
    {
        const double u = n > 1 ? (double) i / (double) (n - 1) : 0.0;
        rk::Job j { juce::jlimit (0, 127, c.note), 100, 0, i };
        if (c.vary == 1)
            j.velocity = juce::jlimit (1, 127, (int) std::lround (1.0 + u * 126.0));
        plan.jobs.push_back (j);
        if (c.vary == 0)
            plan.ccValues.push_back ((int) std::lround (c.ccFrom + u * (c.ccTo - c.ccFrom)));
    }
    plan.ccNumber = c.vary == 0 ? juce::jlimit (0, 127, c.cc) : -1;
    plan.hold = juce::jlimit (0.15, 4.0, c.hold);
    plan.releaseMax = 0.25;
    plan.pause = juce::jlimit (0.05, 4.0, c.settle);
    plan.channel = juce::jlimit (1, 16, options.channel);
    plan.program = options.program;
    // the takes cut and measured as the notes are, without looking for loops
    auto s = session;
    s.loop = false;
    s.normalise = rk::Normalise::off;
    s.oneShot = false;
    auto base = getInstrument();
    const auto folder = base != nullptr && base->folder.isDirectory() ? base->folder : Worker::uniqueFolder (instrumentsRoot(), instrumentName);
    const int channels = options.source == Source::testSynth ? 2 : juce::jmax (1, juce::jmin (2, getTotalNumInputChannels()));
    if (! worker.startCycleSession (instrumentName, s, plan, sampleRate, channels, folder, base))
    {
        why = "La session n'a pas pu démarrer."_fr;
        return false;
    }
    lastCaptureWasCycles = true;
    sendChangeMessage();
    return true;
}

void ReplikantProcessor::stopSampling()
{
    worker.stopSession();
}

void ReplikantProcessor::newInstrument()
{
    if (isSampling())
        return;
    publish (nullptr);
}

void ReplikantProcessor::openInstrument (const juce::File& f, bool withItsSound)
{
    worker.post ("OUVERTURE"_fr, [this, f, withItsSound]
    {
        juce::String err;
        auto ins = instrumentio::load (f, err);
        if (ins == nullptr)
        {
            juce::MessageManager::callAsync ([this, err] { if (onMessage) onMessage (err); });
            return;
        }
        juce::MessageManager::callAsync ([this, ins, withItsSound]
        {
            instrumentName = ins->name;
            if (withItsSound && ins->sound.isObject())
                soundFromVar (ins->sound);
            savedSound = withItsSound || ! ins->sound.isObject() ? soundSignature() : juce::String();
            // the mapping settings come with the instrument
            session.placement = ins->session.placement;
            session.extend = ins->session.extend;
            session.algo = ins->session.algo;
            session.formant = ins->session.formant;
            session.renderKeys = ins->session.renderKeys;
            session.tuning = ins->session.tuning;
            session.tuneCents = ins->session.tuneCents;
            settingsChanged();
        });
        worker.renderAndPublish (ins, true);
    });
}

//============================================================================== RÉÉCHANTILLONNER
bool ReplikantProcessor::startResample (juce::String& why)
{
    if (isSampling())
    {
        why = "Une capture est en cours."_fr;
        return false;
    }
    if (player.resampleState() == Player::Resample::looping)
    {
        why = "La boucle injectée tourne encore : joue une note ou arrête-la d'abord."_fr;
        return false;
    }
    const int bars = ResampleOptions::barsOf (resampleOptions.length);
    // room for its bars down to 40 bpm (the host's tempo isn't known here), two minutes at most; a minute when free
    const double seconds = bars > 0 ? juce::jmin (120.0, bars * 4.0 * 60.0 / 40.0) : 60.0;
    if (! player.armResample (seconds, bars, resampleOptions.synced))
    {
        why = "Un rééchantillonnage est déjà en cours."_fr;
        return false;
    }
    return true;
}

void ReplikantProcessor::resampleTick()
{
    if (releaseNeutralIn > 0 && --releaseNeutralIn == 0)
        player.releaseNeutral();
    if (player.resampleReady())
        injectResample (player.takeResample());
}

void ReplikantProcessor::injectResample (rk::Channels audio)
{
    if (audio.empty() || audio[0].size() < 64)
        return;
    const int length = (int) audio[0].size();
    rk::Loop loop;
    loop.start = 0;
    loop.end = length;
    const auto base = instrumentName.isNotEmpty() ? instrumentName : juce::String ("Son");
    const auto name = base.endsWith (" rééch."_fr) ? base : base + " rééch."_fr;
    auto ins = instrumentio::soundInstrument (std::move (audio), sampleRate, name, resampleOptions.root, 0.0f, false, loop);
    // the sound, to play it as it was heard (the Player holds the chain neutral until these are in)
    if (resampleOptions.resetFx)
        for (int id : { (int) snd::fModel, (int) snd::dModel, (int) snd::spOn, (int) snd::piOn, (int) snd::chOn, (int) snd::crOn,
                        (int) snd::rvOn, (int) snd::wtf })
            setSoundParam (id, 0.0f);
    setSoundParam (snd::engine, (float) snd::sampler);
    setSoundParam (snd::playMode, 0.0f);
    setSoundParam (snd::loops, 1.0f);
    setSoundParam (snd::start, 0.0f);
    setSoundParam (snd::speed, 1.0f);
    setSoundParam (snd::loopStart, 0.0f);
    setSoundParam (snd::loopSize, 1.0f);
    setSoundParam (snd::loopXfade, 0.0f);
    setSoundParam (snd::transpose, 0.0f);
    setSoundParam (snd::fine, 0.0f);
    releaseNeutralIn = 2;         // the settings taken by the Player first (next tick)
    // the input was heard through the effects: it is the loop now
    juce::String also;
    if (options.monitor == 2)
    {
        options.monitor = 0;
        monitorAudio = 0;
        also = " · écoute de l'entrée coupée"_fr;
    }
    instrumentName = name;
    session.renderKeys = false;
    lastResample = name;
    settingsChanged();
    const auto folder = Worker::uniqueFolder (instrumentsRoot(), name);
    worker.post ("RÉÉCHANTILLON"_fr, [this, ins, folder]
    {
        const auto r = instrumentio::save (*ins, folder);
        if (r.failed())
            juce::MessageManager::callAsync ([this, r] { if (onMessage) onMessage (r.getErrorMessage()); });
        worker.renderAndPublish (ins, true);
    });
    if (onMessage)
        onMessage ("Injecté dans le sampler : "_fr + juce::String ((double) length / sampleRate, 2) + " s sur "_fr
                   + juce::String (rk::noteName (resampleOptions.root))
                   + (resampleOptions.resetFx ? juce::String (", effets à zéro"_fr) : juce::String()) + also
                   + ". Joue une note pour reprendre la main."_fr);
    sendChangeMessage();
}

void ReplikantProcessor::setSoundParam (int id, float value)
{
    if (auto* param = params.getParameter (snd::def (id).id))
        param->setValueNotifyingHost (param->convertTo0to1 (snd::legal (id, value)));
}

void ReplikantProcessor::importAny (const juce::File& f)
{
    if (isSampling())
        return;
    if (f.isDirectory() || f.hasFileExtension ("replikant"))
    {
        openInstrument (f, true);
        return;
    }
    const auto root = instrumentsRoot();
    if (instrumentio::isSound (f))
    {
        // a sound: an instrument of its own (saved, so it can be shared), played by the sampler
        worker.post ("IMPORT", [this, f, root]
        {
            juce::String err;
            auto ins = instrumentio::readSound (f, err);
            if (ins != nullptr)
            {
                const auto r = instrumentio::save (*ins, Worker::uniqueFolder (root, ins->name));
                if (r.failed())
                    err = r.getErrorMessage();
            }
            if (ins == nullptr || err.isNotEmpty())
            {
                juce::MessageManager::callAsync ([this, err] { if (onMessage) onMessage (err); });
                if (ins == nullptr)
                    return;
            }
            juce::MessageManager::callAsync ([this, ins]
            {
                instrumentName = ins->name;
                session.renderKeys = false;
                setSoundParam (snd::engine, (float) snd::sampler);
                settingsChanged();
                if (onMessage)
                    onMessage (ins->name + " · joué sur tout le clavier depuis "_fr + juce::String (rk::noteName (ins->samples[0].root))
                               + (ins->samples[0].loop.valid() ? juce::String (" · bouclé"_fr) : juce::String()));
            });
            worker.renderAndPublish (ins, true);
        });
        return;
    }
    worker.post ("IMPORT", [this, f, root]
    {
        juce::String err;
        const auto folder = instrumentio::importPack (f, root, err);
        juce::MessageManager::callAsync ([this, folder, err, name = f.getFileName()]
        {
            if (folder == juce::File())
            {
                if (onMessage) onMessage (err);
                return;
            }
            if (onMessage) onMessage ("Importé : "_fr + name + " → "_fr + folder.getFullPathName());
            openInstrument (folder, true);
        });
    });
}

juce::File ReplikantProcessor::packFor (const Instrument& ins) const
{
    return instrumentsRoot().getChildFile ("Packs").getChildFile (instrumentio::safeName (ins.name) + ".zip");
}

juce::String ReplikantProcessor::soundSignature() const
{
    return juce::JSON::toString (soundToVar(), true);
}

void ReplikantProcessor::buildPack (std::function<void (const juce::File&)> done)
{
    auto ins = getInstrument();
    if (ins == nullptr || ins->samples.empty())
    {
        if (done) done ({});
        return;
    }
    const auto sound = soundToVar();
    const auto zip = packFor (*ins);
    worker.post ("PACK", [this, ins, sound, zip, done]
    {
        const auto r = instrumentio::exportPack (*ins, sound, zip);
        juce::MessageManager::callAsync ([this, r, zip, done]
        {
            if (r.failed() && onMessage)
                onMessage (r.getErrorMessage());
            if (done)
                done (r.wasOk() ? zip : juce::File());
        });
    });
}

void ReplikantProcessor::exportInstrument (bool bitwig, bool allKeys, const juce::File& target)
{
    auto ins = getInstrument();
    if (ins == nullptr)
        return;
    worker.post ("EXPORT"_fr, [this, ins, bitwig, allKeys, target]
    {
        const auto r = bitwig ? instrumentio::exportBitwig (*ins, target, allKeys)
                              : instrumentio::exportWavFolder (*ins, target, allKeys);
        const auto text = r.wasOk() ? "Exporté : "_fr + target.getFullPathName() : r.getErrorMessage();
        juce::MessageManager::callAsync ([this, text, target, ok = r.wasOk()]
        {
            if (onMessage)
                onMessage (text);
            if (ok)
                target.revealToUser();
        });
    });
}

juce::File ReplikantProcessor::instrumentsRoot() const
{
    const auto custom = globalSettings->getValue ("instrumentsRoot");
    return custom.isNotEmpty() ? juce::File (custom) : instrumentio::defaultRoot();
}

//==============================================================================
juce::Array<juce::MidiDeviceInfo> ReplikantProcessor::midiOutputs() const
{
    return juce::MidiOutput::getAvailableDevices();
}

void ReplikantProcessor::setMidiPort (const juce::String& id, const juce::String& name)
{
    if (isSampling())
        return;
    options.midiPort = id;
    options.midiPortName = name;
    // the audio thread only sends while sampling (or for a test note): the old port is kept one change longer
    auto* old = port.exchange (nullptr);
    juce::ignoreUnused (old);
    retiredPort = std::move (portOwner);
    if (id.isNotEmpty() && id != "host")
    {
        portOwner = juce::MidiOutput::openDevice (id);
        port = portOwner.get();
    }
    settingsChanged();
}

juce::String ReplikantProcessor::midiPortStatus() const
{
    if (options.source == Source::testSynth)
        return {};
    if (options.midiPort.isEmpty())
        return "Aucune sortie MIDI choisie"_fr;
    if (options.midiPort != "host" && port.load() == nullptr)
        return "Sortie MIDI introuvable : "_fr + options.midiPortName;
    return {};
}

void ReplikantProcessor::sendTestNote()
{
    const int ch = juce::jlimit (1, 16, options.channel);
    auto on = juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 100);
    on.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    toSynth.addMessageToQueue (on);
    juce::Timer::callAfterDelay (500, [this, ch]
    {
        auto off = juce::MidiMessage::noteOff (ch, 60);
        off.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        toSynth.addMessageToQueue (off);
    });
}

void ReplikantProcessor::audition (int note, int velocity, bool on)
{
    auto m = on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity))
                : juce::MidiMessage::noteOff (1, note);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    if (options.thru)
        toSynth.addMessageToQueue (m);
    else
        toPlayer.addMessageToQueue (m);
}

//==============================================================================
void ReplikantProcessor::loadGlobalSettings()
{
    auto& s = *globalSettings;
    options.midiPort = s.getValue ("midiPort");
    options.midiPortName = s.getValue ("midiPortName");
    options.channel = s.getIntValue ("midiChannel", 1);
    if (s.containsKey ("session"))
        session = instrumentio::sessionFromVar (juce::JSON::parse (s.getValue ("session")));
    if (s.containsKey ("source"))
        options.source = (Source) juce::jlimit (0, 2, s.getIntValue ("source", 0));
    if (options.midiPort.isNotEmpty() && options.midiPort != "host")
    {
        // the same port, found by its id, else by its name (ids can change between sessions)
        auto id = options.midiPort;
        bool found = false;
        for (auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.identifier == id)
                found = true;
        if (! found)
            for (auto& d : juce::MidiOutput::getAvailableDevices())
                if (d.name == options.midiPortName)
                    id = d.identifier;
        portOwner = juce::MidiOutput::openDevice (id);
        port = portOwner.get();
    }
    sourceAudio = (int) options.source;
    hostMidiOut = options.midiPort == "host";
    clock.setDestination (s.getValue ("clockPort"), s.getValue ("clockPortName"));
    clock.setMode ((MidiClock::Mode) juce::jlimit (0, 1, s.getIntValue ("clockMode", 0)));
    clockWithSampling = s.getBoolValue ("clockWithSampling", false);
}

void ReplikantProcessor::saveGlobalSettings()
{
    auto& s = *globalSettings;
    s.setValue ("midiPort", options.midiPort);
    s.setValue ("midiPortName", options.midiPortName);
    s.setValue ("midiChannel", options.channel);
    s.setValue ("source", (int) options.source);
    s.setValue ("session", juce::JSON::toString (instrumentio::sessionToVar (session), true));
    s.setValue ("clockPort", clock.destinationId());
    s.setValue ("clockPortName", clock.destinationName());
    s.setValue ("clockMode", (int) clock.getMode());
    s.setValue ("clockWithSampling", clockWithSampling);
    s.saveIfNeeded();
}

void ReplikantProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("REPLIKANT");
    xml.setAttribute ("version", Updater::currentVersion());
    xml.setAttribute ("name", instrumentName);
    xml.setAttribute ("session", juce::JSON::toString (instrumentio::sessionToVar (session), true));
    xml.setAttribute ("source", (int) options.source);
    xml.setAttribute ("channel", options.channel);
    xml.setAttribute ("program", options.program);
    xml.setAttribute ("monitor", options.monitor);
    xml.setAttribute ("thru", options.thru);
    xml.setAttribute ("cyNote", cycleOptions.note);
    xml.setAttribute ("cyCount", cycleOptions.count);
    xml.setAttribute ("cyVary", cycleOptions.vary);
    xml.setAttribute ("cyCc", cycleOptions.cc);
    xml.setAttribute ("cyFrom", cycleOptions.ccFrom);
    xml.setAttribute ("cyTo", cycleOptions.ccTo);
    xml.setAttribute ("cyHold", cycleOptions.hold);
    xml.setAttribute ("cySettle", cycleOptions.settle);
    xml.setAttribute ("rsLength", resampleOptions.length);
    xml.setAttribute ("rsSynced", resampleOptions.synced);
    xml.setAttribute ("rsRoot", resampleOptions.root);
    xml.setAttribute ("rsResetFx", resampleOptions.resetFx);
    if (auto ins = getInstrument(); ins != nullptr && ins->folder.isDirectory())
        xml.setAttribute ("instrument", ins->folder.getFullPathName());
    if (auto st = params.copyState().createXml())
        xml.addChildElement (st.release());
    xml.setAttribute ("routes", juce::JSON::toString (modsToVar(), true));
    xml.setAttribute ("scenes", juce::JSON::toString (scenesToVar(), true));
    xml.setAttribute ("fxOrder", juce::JSON::toString (fxOrderToVar(), true));
    copyXmlToBinary (xml, dest);
}

void ReplikantProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("REPLIKANT"))
        return;
    instrumentName = xml->getStringAttribute ("name", instrumentName);
    if (xml->hasAttribute ("session"))
        session = instrumentio::sessionFromVar (juce::JSON::parse (xml->getStringAttribute ("session")));
    options.source = (Source) juce::jlimit (0, 2, xml->getIntAttribute ("source", (int) options.source));
    options.channel = juce::jlimit (1, 16, xml->getIntAttribute ("channel", options.channel));
    options.program = juce::jlimit (-1, 127, xml->getIntAttribute ("program", options.program));
    options.monitor = juce::jlimit (0, 2, xml->getIntAttribute ("monitor", options.monitor));
    options.thru = xml->getBoolAttribute ("thru", options.thru);
    cycleOptions.note = juce::jlimit (0, 127, xml->getIntAttribute ("cyNote", cycleOptions.note));
    cycleOptions.count = juce::jlimit (0, 3, xml->getIntAttribute ("cyCount", cycleOptions.count));
    cycleOptions.vary = juce::jlimit (0, 2, xml->getIntAttribute ("cyVary", cycleOptions.vary));
    cycleOptions.cc = juce::jlimit (0, 127, xml->getIntAttribute ("cyCc", cycleOptions.cc));
    cycleOptions.ccFrom = juce::jlimit (0, 127, xml->getIntAttribute ("cyFrom", cycleOptions.ccFrom));
    cycleOptions.ccTo = juce::jlimit (0, 127, xml->getIntAttribute ("cyTo", cycleOptions.ccTo));
    cycleOptions.hold = juce::jlimit (0.15, 4.0, xml->getDoubleAttribute ("cyHold", cycleOptions.hold));
    cycleOptions.settle = juce::jlimit (0.05, 4.0, xml->getDoubleAttribute ("cySettle", cycleOptions.settle));
    resampleOptions.length = juce::jlimit (0, 4, xml->getIntAttribute ("rsLength", resampleOptions.length));
    resampleOptions.synced = xml->getBoolAttribute ("rsSynced", resampleOptions.synced);
    resampleOptions.root = juce::jlimit (0, 127, xml->getIntAttribute ("rsRoot", resampleOptions.root));
    resampleOptions.resetFx = xml->getBoolAttribute ("rsResetFx", resampleOptions.resetFx);
    if (auto* st = xml->getChildByName (params.state.getType()))
    {
        params.replaceState (juce::ValueTree::fromXml (*st));
        // a project from before 0.3 played its instrument: the SAMPLER (the SYNTHÉ is what a new one starts on)
        if (st->getChildByAttribute ("id", snd::def (snd::engine).id) == nullptr && xml->hasAttribute ("instrument"))
            if (auto* e = params.getParameter (snd::def (snd::engine).id))
                e->setValueNotifyingHost (e->convertTo0to1 ((float) snd::sampler));
    }
    // the routes, steps and loops of the modulators (not parameters)
    if (xml->hasAttribute ("routes"))
        modsFromVar (juce::JSON::parse (xml->getStringAttribute ("routes")));
    scenesFromVar (juce::JSON::parse (xml->getStringAttribute ("scenes")));
    fxOrderFromVar (juce::JSON::parse (xml->getStringAttribute ("fxOrder")));
    settingsChanged();
    const juce::File f (xml->getStringAttribute ("instrument"));
    if (xml->hasAttribute ("instrument"))
    {
        if (f.isDirectory())
            openInstrument (f, false);     // (the project's own sound, not the one kept with the instrument)
        else
            juce::MessageManager::callAsync ([this, f] { if (onMessage) onMessage ("Instrument introuvable : "_fr + f.getFullPathName()); });
    }
}

//==============================================================================
void ReplikantProcessor::setClockDestination (const juce::String& id, const juce::String& name)
{
    clock.setDestination (id, name);
    saveGlobalSettings();
    sendChangeMessage();
}

void ReplikantProcessor::setClockMode (MidiClock::Mode m)
{
    clock.setMode (m);
    saveGlobalSettings();
    sendChangeMessage();
}

void ReplikantProcessor::setClockRunning (bool on)
{
    clock.setRunning (on);
    clockStartedBySampling = false;
    sendChangeMessage();
}

//============================================================================== the sound
juce::var ReplikantProcessor::modsToVar() const
{
    juce::Array<juce::var> mods;
    for (int m = 0; m < snd::numMods; ++m)
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> routes, steps;
        for (auto& r : player.routes[(size_t) m])
        {
            const int d = r.dest.load();
            if (d < 0)
                continue;
            auto* ro = new juce::DynamicObject();
            ro->setProperty ("to", juce::String (snd::def (d).id));
            ro->setProperty ("amount", (double) r.amount.load());
            routes.add (juce::var (ro));
        }
        for (auto& st : player.steps[(size_t) m])
            steps.add ((double) st.load());
        o->setProperty ("routes", routes);
        o->setProperty ("steps", steps);
        o->setProperty ("seed", (int) player.seeds[(size_t) m].load());
        mods.add (juce::var (o));
    }
    return mods;
}

void ReplikantProcessor::modsFromVar (const juce::var& v)
{
    if (! v.isArray())
        return;
    for (int m = 0; m < snd::numMods && m < v.size(); ++m)
    {
        const auto& o = v[m];
        player.clearRoutes (m);
        if (auto* rs = o["routes"].getArray())
            for (auto& r : *rs)
            {
                const int d = snd::find (r["to"].toString().toStdString());
                if (d >= 0 && snd::def (d).modulatable)
                    player.setRoute (m, d, (float) (double) r["amount"]);
            }
        if (auto* st = o["steps"].getArray())
            for (int i = 0; i < snd::maxSteps && i < st->size(); ++i)
                player.steps[(size_t) m][(size_t) i] = juce::jlimit (-1.0f, 1.0f, (float) (double) (*st)[i]);
        if (o.hasProperty ("seed"))
            player.seeds[(size_t) m] = (uint32_t) (int) o["seed"];
    }
}

juce::var ReplikantProcessor::soundToVar() const
{
    auto* o = new juce::DynamicObject();
    auto* p = new juce::DynamicObject();
    for (int i = 0; i < snd::count; ++i)
        if (i != snd::tempo)
            p->setProperty (snd::def (i).id, (double) player.get (i));
    o->setProperty ("params", juce::var (p));
    o->setProperty ("mods", modsToVar());
    o->setProperty ("scenes", scenesToVar());
    o->setProperty ("fxOrder", fxOrderToVar());
    return juce::var (o);
}

// the chain's order, by the stages' names (an unknown or missing order: the usual one)
juce::var ReplikantProcessor::fxOrderToVar() const
{
    static const char* names[Player::numStages] = { "drive", "spectre", "pitch", "chorus", "crush", "espace" };
    juce::Array<juce::var> a;
    for (int st : player.fxOrder())
        a.add (juce::String (names[st]));
    return a;
}

void ReplikantProcessor::fxOrderFromVar (const juce::var& v)
{
    static const char* names[Player::numStages] = { "drive", "spectre", "pitch", "chorus", "crush", "espace" };
    std::array<int, Player::numStages> order { 0, 1, 2, 3, 4, 5 };
    if (v.isArray() && v.size() == Player::numStages)
        for (int p = 0; p < Player::numStages; ++p)
            for (int st = 0; st < Player::numStages; ++st)
                if (v[p].toString() == names[st])
                    order[(size_t) p] = st;
    player.setFxOrder (order);      // (not a permutation: ignored, the order stays)
}

void ReplikantProcessor::soundFromVar (const juce::var& v)
{
    if (! v.isObject())
        return;
    if (auto* p = v["params"].getDynamicObject())
        for (int i = 0; i < snd::count; ++i)
        {
            if (i == snd::tempo)
                continue;
            const auto& d = snd::def (i);
            auto* param = params.getParameter (d.id);
            if (param == nullptr)
                continue;
            // (a sound kept before 0.3 had no engine: it was the sampler's)
            const float missing = i == snd::engine ? (float) snd::sampler : d.def;
            const float value = p->hasProperty (d.id) ? (float) (double) p->getProperty (d.id) : missing;
            param->setValueNotifyingHost (param->convertTo0to1 (snd::legal (i, value)));
        }
    modsFromVar (v["mods"]);
    scenesFromVar (v["scenes"]);
    fxOrderFromVar (v["fxOrder"]);
    sendChangeMessage();
}

juce::var ReplikantProcessor::scenesToVar() const
{
    juce::Array<juce::var> all;
    for (int sc = 0; sc < snd::numScenes; ++sc)
    {
        auto* o = new juce::DynamicObject();
        for (int i = 0; i < snd::count; ++i)
        {
            const float v = player.sceneLock (sc, i);
            if (! std::isnan (v))
                o->setProperty (snd::def (i).id, (double) v);
        }
        all.add (juce::var (o));
    }
    return all;
}

void ReplikantProcessor::scenesFromVar (const juce::var& v)
{
    for (int sc = 0; sc < snd::numScenes; ++sc)
    {
        player.clearScene (sc);
        if (! v.isArray() || sc >= v.size())
            continue;
        if (auto* o = v[sc].getDynamicObject())
            for (auto& prop : o->getProperties())
            {
                const int id = snd::find (prop.name.toString().toStdString());
                if (id >= 0)
                    player.setSceneLock (sc, id, (float) (double) prop.value);
            }
    }
}

void ReplikantProcessor::resetSound()
{
    auto* o = new juce::DynamicObject();
    auto* params0 = new juce::DynamicObject();
    params0->setProperty (snd::def (snd::engine).id, (double) snd::def (snd::engine).def);    // (a fresh sound: the synth)
    o->setProperty ("params", juce::var (params0));
    juce::Array<juce::var> mods;
    for (int m = 0; m < snd::numMods; ++m)
        mods.add (juce::var (new juce::DynamicObject()));
    o->setProperty ("mods", mods);
    soundFromVar (juce::var (o));
    for (int m = 0; m < snd::numMods; ++m)
        player.clearRoutes (m);
    for (int sc = 0; sc < snd::numScenes; ++sc)
        player.clearScene (sc);
}

juce::AudioProcessorEditor* ReplikantProcessor::createEditor()
{
    return new ReplikantEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReplikantProcessor();
}
