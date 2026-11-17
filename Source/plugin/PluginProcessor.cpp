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

juce::AudioProcessorValueTreeState::ParameterLayout ReplikantProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attaque", NormalisableRange<float> (0.0f, 4000.0f, 0.1f, 0.3f), 1.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, String::fromUTF8 ("Rel\xc3\xa2" "che"), NormalisableRange<float> (5.0f, 10000.0f, 0.1f, 0.3f), 300.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "velocity", 1 }, String::fromUTF8 ("V\xc3\xa9locit\xc3\xa9"), NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bend", 1 }, "Pitch bend", NormalisableRange<float> (0.0f, 12.0f, 1.0f), 2.0f,
                                                  AudioParameterFloatAttributes().withLabel ("st")));
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
    pVolume = params.getRawParameterValue ("volume");
    pAttack = params.getRawParameterValue ("attack");
    pRelease = params.getRawParameterValue ("release");
    pVelocity = params.getRawParameterValue ("velocity");
    pBend = params.getRawParameterValue ("bend");

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
    player.prepare (sr);
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
    player.params.gainDb = pVolume->load();
    player.params.attackMs = pAttack->load();
    player.params.releaseMs = pRelease->load();
    player.params.velocity = pVelocity->load();
    player.params.bendRange = pBend->load();

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

    // what is heard: the synth (monitor) and the instrument
    auto out = getBusBuffer (buffer, false, 0);
    const bool monitor = monitorAudio.load() || sampling;
    if (src == (int) Source::testSynth)
    {
        for (int c = 0; c < out.getNumChannels(); ++c)
        {
            if (monitor && n <= synthBuffer.getNumSamples())
                out.copyFrom (c, 0, synthBuffer, juce::jmin (c, 1), 0, n);
            else
                out.clear (c, 0, n);
        }
    }
    else if (src == (int) Source::sidechain)
    {
        // the main input goes through untouched; the sidechain is only heard when monitoring
        if (monitor && numIn > 0)
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addFrom (c, 0, chans[juce::jmin (c, numIn - 1)], n);
    }
    else if (! monitor)
        out.clear();
    for (int c = out.getNumChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    playMidi.clear();
    toPlayer.removeNextBlockOfMessages (playMidi, n);
    if (! thruAudio.load())
        playMidi.addEvents (midi, 0, n, 0);
    if (! sampling)
        player.render (out, playMidi);

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
            next->session.tune = session.tune;
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

void ReplikantProcessor::openInstrument (const juce::File& f)
{
    worker.post ("OUVERTURE"_fr, [this, f]
    {
        juce::String err;
        auto ins = instrumentio::load (f, err);
        if (ins == nullptr)
        {
            juce::MessageManager::callAsync ([this, err] { if (onMessage) onMessage (err); });
            return;
        }
        juce::MessageManager::callAsync ([this, ins]
        {
            instrumentName = ins->name;
            // the mapping settings come with the instrument
            session.placement = ins->session.placement;
            session.extend = ins->session.extend;
            session.algo = ins->session.algo;
            session.formant = ins->session.formant;
            session.renderKeys = ins->session.renderKeys;
            session.tune = ins->session.tune;
            settingsChanged();
        });
        worker.renderAndPublish (ins, true);
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
}

void ReplikantProcessor::saveGlobalSettings()
{
    auto& s = *globalSettings;
    s.setValue ("midiPort", options.midiPort);
    s.setValue ("midiPortName", options.midiPortName);
    s.setValue ("midiChannel", options.channel);
    s.setValue ("source", (int) options.source);
    s.setValue ("session", juce::JSON::toString (instrumentio::sessionToVar (session), true));
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
    if (auto ins = getInstrument(); ins != nullptr && ins->folder.isDirectory())
        xml.setAttribute ("instrument", ins->folder.getFullPathName());
    if (auto st = params.copyState().createXml())
        xml.addChildElement (st.release());
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
    options.monitor = xml->getBoolAttribute ("monitor", options.monitor);
    options.thru = xml->getBoolAttribute ("thru", options.thru);
    if (auto* st = xml->getChildByName (params.state.getType()))
        params.replaceState (juce::ValueTree::fromXml (*st));
    settingsChanged();
    const juce::File f (xml->getStringAttribute ("instrument"));
    if (xml->hasAttribute ("instrument"))
    {
        if (f.isDirectory())
            openInstrument (f);
        else
            juce::MessageManager::callAsync ([this, f] { if (onMessage) onMessage ("Instrument introuvable : "_fr + f.getFullPathName()); });
    }
}

juce::AudioProcessorEditor* ReplikantProcessor::createEditor()
{
    return new ReplikantEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReplikantProcessor();
}
