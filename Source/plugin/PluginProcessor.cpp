#include "PluginProcessor.h"
#include "../radio/RadioNet.h"
#include "../radio/RadioSlicer.h"
#include "../core/Wav.h"
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
    radioPool = std::make_unique<juce::ThreadPool> (1);
    radioEvents.reserve (512);
    startTimerHz (10);
}

ReplikantProcessor::~ReplikantProcessor()
{
    lifeline->store (false);
    stopTimer();
    radioPool->removeAllJobs (true, 5000);
    net.reset();
    worker.onInstrument = nullptr;
    worker.onStatus = nullptr;
    recorder.stop();
    port = nullptr;
}

void ReplikantProcessor::later (std::function<void()> f)
{
    juce::MessageManager::callAsync ([a = lifeline, f = std::move (f)] { if (a->load()) f(); });
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
    silence.setSize (2, juce::jmax (block, 4096) * 2, false, true, false);
    silence.clear();
    for (auto& h : sentHeads)
        h.valid = false;
    playMidi.ensureSize (4096);
    synthMidi.ensureSize (2048);
    radio.prepare (sr, block);
    drum.prepare (sr, juce::jmax (block, 4096) * 2);
    limiter.prepare (sr);
    setLatencySamples (limiter.latency());          // (LIMIT's look-ahead, on or off: always the same)
    radioBuffer.setSize (2, juce::jmax (block, 4096) * 2, false, true, false);
    followCopy.setSize (4, juce::jmax (block, 4096) * 2, false, true, false);
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
    // the assistant's test: its notes, at the start of the block (as the recorder sends them)
    if (! sampling)
        probeBegin (sink, n);

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
    // some hosts hand back, as the input, the buffer just played (in place, nothing written into it since): our
    // own output again. Heard, it would add to itself block after block (every note forever, a buzz at the
    // block rate). Spotted by comparison with what went out lately, and taken as silence
    const bool echoed = src != (int) Source::testSynth && isOwnOutput (input, n, ! sampling);
    if (echoed)
    {
        input.clear();
        if (src == (int) Source::input)
            for (int c = 0; c < numIn; ++c)
                chans[c] = silence.getReadPointer (c);
    }
    // the assistant: the meters (the capture page open), its test listening
    if (captureOpen.load (std::memory_order_relaxed) || probeRunning.load (std::memory_order_relaxed))
    {
        auto keep = [] (std::atomic<float>& a, float v) { if (v > a.load (std::memory_order_relaxed)) a.store (v, std::memory_order_relaxed); };
        const bool scOn = getBusCount (true) > 1 && getBus (true, 1)->isEnabled();
        auto sc = scOn ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
        for (int c = 0; c < 2; ++c)
        {
            if (numIn > 0)
            {
                const auto r = juce::FloatVectorOperations::findMinAndMax (chans[juce::jmin (c, numIn - 1)], n);
                keep (heardSource[c], juce::jmax (-r.getStart(), r.getEnd()));
            }
            if (input.getNumChannels() > 0)
                keep (heardMain[c], input.getMagnitude (juce::jmin (c, input.getNumChannels() - 1), 0, n));
            if (sc.getNumChannels() > 0)
                keep (heardSide[c], sc.getMagnitude (juce::jmin (c, sc.getNumChannels() - 1), 0, n));
        }
        if (echoed)
            heardEcho.store (true, std::memory_order_relaxed);
        if (! sampling)
            probeListen (chans, numIn, n, input, scOn ? &sc : nullptr, echoed);
    }
    if (numIn > 0 && n <= synthBuffer.getNumSamples())
        recorder.capture (chans, numIn, n);
    if (numIn > 0)
        listener.process (chans, numIn, n);

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
    // ÉCOUTE : DIRECT lets the input through only while capturing or with the capture page open (to hear the synth
    // being set up). Playing, never: some hosts hand back as the input the buffer just played, and passing it on
    // would loop it for good (every note forever); with nothing let through, no host can close that loop.
    // OFF (the default since 0.3.6) is OFF, a capture included: an input that hears the speakers (the Mac's own
    // microphone, a track listening to its output) would howl
    const int monWanted = monitorAudio.load();
    const int mon = sampling ? (monWanted > 0 ? 1 : 0) : (monWanted == 1 && ! captureOpen.load() ? 0 : monWanted);
    const float* kept[2] = { nullptr, nullptr };
    if (numIn > 0 && n <= inputCopy.getNumSamples())
    {
        for (int c = 0; c < numIn; ++c)
        {
            inputCopy.copyFrom (c, 0, chans[c], n);
            kept[c] = inputCopy.getReadPointer (c);
        }
    }

    // FOLLOW's INPUT and SIDECHAIN, kept as well (the output is written over the first, the second cleared)
    const bool followable = n <= followCopy.getNumSamples();
    const float* followIn[2] = { nullptr, nullptr };
    const float* followSc[2] = { nullptr, nullptr };
    if (followable)
    {
        const int mainIn = juce::jmin (2, input.getNumChannels());
        if (mainIn > 0 && ! echoed)
            for (int c = 0; c < 2; ++c)
            {
                followCopy.copyFrom (c, 0, input, juce::jmin (c, mainIn - 1), 0, n);
                followIn[c] = followCopy.getReadPointer (c);
            }
        if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            const int scIn = juce::jmin (2, sc.getNumChannels());
            if (scIn > 0)
                for (int c = 0; c < 2; ++c)
                {
                    followCopy.copyFrom (2 + c, 0, sc, juce::jmin (c, scIn - 1), 0, n);
                    followSc[c] = followCopy.getReadPointer (2 + c);
                }
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
    // RADIO: the radio and its pads (the notes play them), then through the filter and the chain as the input would
    // go in FX mode; only the receiver listened to stays connected
    const bool radioOn = (int) player.get (snd::engine) == snd::radio;
    // (the capture page over the RADIO: the radio quiet, as the other modes are, its receiver kept connected)
    const bool wanted = player.get (snd::rdOn) > 0.5f;
    const bool onAir = wanted && ! captureOpen.load();
    // (the receiver moved by a mod, or CUT playing them all: every receiver kept connected)
    const bool allRx = player.modulatedNorm (snd::rdRx) >= 0.0f || (int) player.get (snd::rdMode) == radio::modeCut;
    radio.bank.use.store (radioOn && wanted && ! sampling ? (allRx ? (1 << radio::numRx) - 1 : 1 << juce::jlimit (0, radio::numRx - 1, (int) player.get (snd::rdRx))) : 0,
                          std::memory_order_relaxed);
    const bool drumOn = (int) player.get (snd::engine) == snd::drum && drum.allocated();
    if (! sampling && (radioOn || drumOn) && n <= radioBuffer.getNumSamples())
    {
        radioEvents.clear();
        for (const auto meta : playMidi)
        {
            const auto m = meta.getMessage();
            if (radioEvents.size() >= radioEvents.capacity())
                break;
            if (m.isNoteOn())
                radioEvents.push_back ({ meta.samplePosition, m.getNoteNumber(), m.getVelocity() });
            else if (m.isNoteOff())
                radioEvents.push_back ({ meta.samplePosition, m.getNoteNumber(), 0 });
        }
        // (its settings as the mods and the scenes move them, a block late)
        auto V = [this] (int id) { const float m = player.modulatedNorm (id); return m >= 0.0f ? snd::fromNorm (id, m) : player.get (id); };
        radio::Settings rs;
        rs.on = onAir;
        rs.rx = (int) V (snd::rdRx);
        rs.mode = (int) V (snd::rdMode);
        rs.len = (int) V (snd::rdLen);
        rs.density = V (snd::rdDensity);
        rs.vary = V (snd::rdVary);
        rs.squelch = V (snd::rdSquelch);
        rs.grab = V (snd::rdGrab);
        rs.depth = V (snd::rdDepth);
        rs.tune = V (snd::rdTune);
        rs.stat = V (snd::rdStatic);
        rs.level = V (snd::rdLevel);
        rs.band = V (snd::rdBand);
        rs.centre = V (snd::rdCentre);
        rs.fade = V (snd::rdFade);
        rs.fadeRate = V (snd::rdRate);
        rs.whistle = V (snd::rdWhistle);
        rs.crackle = V (snd::rdCrackle);
        rs.drive = V (snd::rdDrive);
        rs.padTune = V (snd::rdPadTune);
        rs.padDecay = V (snd::rdPadDecay);
        rs.padLevel = V (snd::rdPadLevel);
        rs.choke = player.get (snd::rdChoke) > 0.5f;
        rs.bpm = transport.bpm;
        rs.ppq = transport.ppq;
        rs.playing = transport.playing;
        auto* rl = radioBuffer.getWritePointer (0);
        auto* rr = radioBuffer.getWritePointer (1);
        if (radioOn)
            radio.render (rl, rr, n, rs, radioEvents.data(), (int) radioEvents.size());
        else
        {
            // DRUM: the host's song position when it plays, else REPLIK8's own clock (the play key), from 0
            drum::Engine::Clock dc;
            const bool own = ! transport.playing && clock.getMode() == MidiClock::Mode::internal && clock.isWanted();
            if (transport.playing)
            {
                dc.running = true;
                dc.ppq = transport.ppq;
            }
            else if (own)
            {
                if (! drumClockWas)
                    drumPpq = 0.0;
                dc.running = true;
                dc.ppq = drumPpq;
                drumPpq += (double) n / sampleRate * transport.bpm / 60.0;
            }
            drumClockWas = own;
            dc.bpm = transport.bpm;
            static_assert (Player::numWtfPulls == drum::Engine::numWtf, "WTF's pulls, one for one");
            drum.setWtf (player.wtfPulls());
            static_assert (drum::Engine::modStride == snd::drumDestStride && drum::numTracks == snd::drumDestTracks);
            drum.setMods (player.drumMods());          // (the player's, a block late: drawn as it renders)
            drum.render (rl, rr, n, dc, player.get (snd::drSwing), radioEvents.data(), (int) radioEvents.size());
        }
        if (mon == 2 && kept[0] != nullptr)
            for (int i = 0; i < n; ++i)
            {
                rl[i] += kept[0][i];
                rr[i] += kept[juce::jmin (1, numIn - 1)][i];
            }
        const float* heard[2] = { rl, rr };
        player.setFollowSignal (0, followIn[0], followIn[1]);
        player.setFollowSignal (1, rl, rr);
        player.setFollowSignal (2, followSc[0], followSc[1]);
        for (int t = 0; t < drum::numTracks; ++t)
            player.setFollowSignal (3 + t, drumOn ? drum.trackSignal (t) : nullptr);
        player.render (out, noMidi, transport, heard, 2, true);
    }
    else if (! sampling)
    {
        player.setFollowSignal (0, followIn[0], followIn[1]);
        player.setFollowSignal (2, followSc[0], followSc[1]);
        player.render (out, playMidi, transport, kept[0] != nullptr ? kept : nullptr, numIn, mon == 2);
    }

    // LIMIT: nothing past CEILING goes out (its settings as set: not moved by the mods)
    {
        dsp::LimiterSettings ls;
        ls.on = player.get (snd::lmOn) > 0.5f;
        ls.ceilingDb = player.get (snd::lmCeiling);
        ls.releaseMs = player.get (snd::lmRelease);
        ls.driveDb = player.get (snd::lmDrive);
        limiter.set (ls);
        if (out.getNumChannels() > 0)
            limiter.process (out.getWritePointer (0), out.getWritePointer (juce::jmin (1, out.getNumChannels() - 1)), n);
    }

    rememberOutput (out, n);

    clock.process (transport.playing, transport.ppq, transport.bpm, n, sampleRate, outMidi);
    midi.swapWith (outMidi);
}

// the start of the output, kept for the last few blocks (a host may hand back one played a block or two ago)
void ReplikantProcessor::rememberOutput (const juce::AudioBuffer<float>& out, int n)
{
    auto& h = sentHeads[(size_t) sentNext];
    sentNext = (sentNext + 1) % (int) sentHeads.size();
    h.n = juce::jmin (n, headLength);
    h.channels = juce::jmin (2, out.getNumChannels());
    h.valid = h.n > 0 && h.channels > 0;
    h.silent = true;
    for (int c = 0; c < h.channels; ++c)
        for (int i = 0; i < h.n; ++i)
        {
            h.x[c][i] = out.getSample (c, i);
            h.silent = h.silent && h.x[c][i] == 0.0f;
        }
}

bool ReplikantProcessor::isOwnOutput (const juce::AudioBuffer<float>& in, int n, bool scaledToo) const
{
    const int len = juce::jmin (n, headLength);
    const int chans = juce::jmin (2, in.getNumChannels());
    if (len <= 0 || chans <= 0)
        return false;
    for (const auto& h : sentHeads)
    {
        // (the very same numbers: a real input never repeats what went out, sample for sample)
        if (! h.valid || h.silent || h.n != len || h.channels < chans)
            continue;
        bool same = true;
        for (int c = 0; c < chans && same; ++c)
            for (int i = 0; i < len && same; ++i)
                same = in.getSample (c, i) == h.x[c][i];
        if (same)
            return true;
        // (0.3.6) or the same shape at another level (a host, a fader, a send handing it back a little louder or
        // softer): a correlation of 0.9999 over the 64 first samples of each side, which a sound of its own never
        // has with ours. Not while capturing: the synth is recorded as it comes
        if (! scaledToo)
            continue;
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int c = 0; c < chans; ++c)
            for (int i = 0; i < len; ++i)
            {
                const double x = in.getSample (c, i), y = h.x[c][i];
                xy += x * y;
                xx += x * x;
                yy += y * y;
            }
        if (yy > 1.0e-8 && xx > 1.0e-8 && xy > 0.0 && xy * xy > 0.9998 * xx * yy && xx > 0.04 * yy && xx < 25.0 * yy)
            return true;
    }
    return false;
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
    listenTick();
    resampleTick();
    radioTick();
    drumTick();
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
    if (probeRunning.load())
    {
        why = "Le test de l'assistant joue encore : attends sa fin."_fr;
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
    if (probeRunning.load())
    {
        why = "Le test de l'assistant joue encore : attends sa fin."_fr;
        return false;
    }
    const bool listen = cycleOptions.vary == 3;
    if (! listen && options.source != Source::testSynth && port.load() == nullptr && ! hostMidiOut.load())
    {
        why = "Choisis la sortie MIDI qui va au synthé (SOURCE > MIDI)."_fr;
        return false;
    }
    if (options.source == Source::sidechain && (getBusCount (true) < 2 || ! getBus (true, 1)->isEnabled()))
    {
        why = "Le sidechain n'est pas branché : choisis-le dans ton DAW, ou prends l'entrée principale."_fr;
        return false;
    }
    if (listen)
    {
        // LISTEN: armed; the take starts when the input goes over the threshold
        if (! listener.arm (sampleRate, (float) juce::jlimit (-70.0, -6.0, cycleOptions.listenDb), 30.0))
        {
            why = "L'écoute n'a pas pu démarrer."_fr;
            return false;
        }
        worker.listening ("EN ÉCOUTE · JOUE TA NOTE"_fr, 0.0f);
        lastCaptureWasCycles = true;
        sendChangeMessage();
        return true;
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
    if (listener.busy())
    {
        listener.stop();
        return;
    }
    worker.stopSession();
}

// LISTEN, on the message thread: what it shows while it waits and records; the take, once over, to the worker
void ReplikantProcessor::listenTick()
{
    const int st = listener.state.load();
    if (st == CycleListener::idle)
    {
        if (listenShown)
        {
            listenShown = false;
            worker.listening ("ÉCOUTE ARRÊTÉE"_fr, -1.0f, true);       // (stopped before anything came)
            sendChangeMessage();
        }
        return;
    }
    if (st == CycleListener::done)
    {
        listenShown = false;
        auto take = listener.take();
        auto base = getInstrument();
        const auto folder = base != nullptr && base->folder.isDirectory() ? base->folder : Worker::uniqueFolder (instrumentsRoot(), instrumentName);
        auto s = session;
        s.loop = false;
        s.normalise = rk::Normalise::off;
        s.oneShot = false;
        worker.cyclesFromTake (std::move (take), sampleRate, CycleOptions::countOf (cycleOptions.count), instrumentName, s, folder, base);
        sendChangeMessage();
        return;
    }
    listenShown = true;
    if (st == CycleListener::waiting)
        worker.listening ("EN ÉCOUTE · JOUE TA NOTE"_fr, 0.0f);
    else
        worker.listening ("ENREGISTREMENT "_fr + juce::String (listener.seconds(), 1) + " S · STOP OU SILENCE POUR FINIR",
                          (float) (listener.seconds() / listener.maxSeconds()));
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
            later ([this, err] { if (onMessage) onMessage (err); });
            return;
        }
        later ([this, ins, withItsSound]
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
                        (int) snd::rvOn, (int) snd::ecOn, (int) snd::cpOn, (int) snd::wtf })
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
            later ([this, r] { if (onMessage) onMessage (r.getErrorMessage()); });
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
                later ([this, err] { if (onMessage) onMessage (err); });
                if (ins == nullptr)
                    return;
            }
            later ([this, ins]
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
        later ([this, folder, err, name = f.getFileName()]
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
        later ([this, r, zip, done]
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
        later ([this, text, target, ok = r.wasOk()]
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

//============================================================================== the capture assistant
ReplikantProcessor::HostInfo ReplikantProcessor::hostInfo() const
{
    HostInfo h;
    const juce::PluginHostType type;
    h.hostName = juce::String (type.getHostDescription());
    if (wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_AudioUnitv3) h.format = "AU";
    else if (wrapperType == wrapperType_VST3)                                       h.format = "VST3";
    else if (wrapperType == wrapperType_Standalone)                                 h.format = "app";
    else                                                                            h.format = juce::AudioProcessor::getWrapperTypeDescription (wrapperType);
    if (wrapperType == wrapperType_Standalone)  h.host = Host::app;
    else if (type.isAbletonLive())              h.host = Host::live;
    else if (type.isLogic())                    h.host = Host::logic;
    else if (type.isBitwigStudio())             h.host = Host::bitwig;
    else if (type.isReaper())                   h.host = Host::reaper;
    else if (type.isCubase() || type.isNuendo()) h.host = Host::cubase;
    else if (type.isStudioOne())                h.host = Host::studioOne;
    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        h.trackName = trackName;
    }
    if (getBusCount (true) > 0 && getBus (true, 0)->isEnabled())
        h.mainInputs = getBus (true, 0)->getNumberOfChannels();
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
        h.sidechainInputs = getBus (true, 1)->getNumberOfChannels();
    return h;
}

void ReplikantProcessor::updateTrackProperties (const TrackProperties& p)
{
    const juce::SpinLock::ScopedLockType lock (trackLock);
    trackName = p.name;
}

ReplikantProcessor::Heard ReplikantProcessor::heard()
{
    Heard h;
    for (int c = 0; c < 2; ++c)
    {
        h.source[c] = heardSource[c].exchange (0.0f);
        h.main[c] = heardMain[c].exchange (0.0f);
        h.side[c] = heardSide[c].exchange (0.0f);
    }
    h.echoed = heardEcho.exchange (false);
    return h;
}

ReplikantProcessor::AudioIns ReplikantProcessor::audioInputs (const juce::String& device) const
{
    AudioIns a;
   #if JucePlugin_Build_Standalone
    if (wrapperType == wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            auto& dm = holder->deviceManager;
            a.app = true;
            a.current = dm.getAudioDeviceSetup().inputDeviceName;
            if (auto* type = dm.getCurrentDeviceTypeObject())
            {
                a.devices = type->getDeviceNames (true);
                a.shown = device.isNotEmpty() ? device : a.current;
                auto* now = dm.getCurrentAudioDevice();
                if (a.shown == a.current && now != nullptr)
                {
                    a.channels = now->getInputChannelNames();
                    a.active = now->getActiveInputChannels();
                }
                else if (a.shown.isNotEmpty())
                    if (std::unique_ptr<juce::AudioIODevice> d { type->createDevice ({}, a.shown) })
                        a.channels = d->getInputChannelNames();
            }
            return a;
        }
   #endif
    // in a DAW: the Mac's inputs as Core Audio lists them (the DAW records them, REPLIK8 only names them)
    std::unique_ptr<juce::AudioIODeviceType> type (juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
    if (type == nullptr)
        return a;
    type->scanForDevices();
    a.devices = type->getDeviceNames (true);
    const int def = type->getDefaultDeviceIndex (true);
    a.shown = device.isNotEmpty() ? device : (juce::isPositiveAndBelow (def, a.devices.size()) ? a.devices[def] : a.devices[0]);
    if (a.shown.isNotEmpty())
        if (std::unique_ptr<juce::AudioIODevice> d { type->createDevice ({}, a.shown) })
            a.channels = d->getInputChannelNames();
    return a;
}

bool ReplikantProcessor::useAudioInput (const juce::String& device, int first, bool stereo, juce::String& why)
{
   #if JucePlugin_Build_Standalone
    if (wrapperType == wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            auto& dm = holder->deviceManager;
            auto setup = dm.getAudioDeviceSetup();
            setup.inputDeviceName = device;
            setup.useDefaultInputChannels = false;
            setup.inputChannels.clear();
            setup.inputChannels.setBit (juce::jmax (0, first));
            if (stereo)
                setup.inputChannels.setBit (juce::jmax (0, first) + 1);
            why = dm.setAudioDeviceSetup (setup, true);
            if (why.isNotEmpty())
                return false;
            holder->saveAudioDeviceState();
            return true;
        }
   #endif
    juce::ignoreUnused (device, first, stereo);
    why = "Dans un DAW, c'est le DAW qui choisit la carte son."_fr;
    return false;
}

bool ReplikantProcessor::startProbe (bool ccTest, juce::String& why)
{
    if (isSampling())
    {
        why = "Une capture est en cours."_fr;
        return false;
    }
    if (options.source != Source::testSynth && port.load() == nullptr && ! hostMidiOut.load())
    {
        why = "Aucune sortie MIDI vers le synthé : choisis-la d'abord."_fr;
        return false;
    }
    probeNote = juce::jlimit (0, 127, ccTest ? cycleOptions.note : 60);
    probeChannel = juce::jlimit (1, 16, options.channel);
    probeCc = juce::jlimit (0, 127, cycleOptions.cc);
    probeCcFrom = juce::jlimit (0, 127, cycleOptions.ccFrom);
    probeCcTo = juce::jlimit (0, 127, cycleOptions.ccTo);
    {
        const juce::SpinLock::ScopedLockType lock (probeLock);
        probeShared = {};
        probeShared.state = Probe::running;
        probeShared.ccTest = ccTest;
        probeShared.hits = ccTest ? 1 : 3;
    }
    probeRunning = true;
    probeWanted = ccTest ? 2 : 1;
    return true;
}

void ReplikantProcessor::stopProbe()
{
    probeWanted = -1;
}

ReplikantProcessor::Probe ReplikantProcessor::probe() const
{
    const juce::SpinLock::ScopedLockType lock (probeLock);
    return probeShared;
}

void ReplikantProcessor::probePublish()
{
    const juce::SpinLock::ScopedTryLockType lock (probeLock);
    if (! lock.isLocked())
        return;          // (next block)
    probeShared = run.result;
    run.dirty = false;
}

// at the start of a block: what the test sends (a note on or off, a CC), as the recorder sends its notes
void ReplikantProcessor::probeBegin (Recorder::MidiSink& sink, int n)
{
    juce::ignoreUnused (n);
    auto& r = run;
    const int want = probeWanted.exchange (0);
    if (want != 0 && r.phase != ProbeRun::off)
    {
        sink.send (juce::MidiMessage::noteOff (r.channel, r.note));     // (whatever was held)
        r.phase = ProbeRun::off;
    }
    if (want < 0)
    {
        r.result.state = Probe::idle;
        r.dirty = true;
        probeRunning = false;
    }
    else if (want > 0)
    {
        r = {};
        r.note = probeNote.load();
        r.channel = probeChannel.load();
        r.cc = probeCc.load();
        r.ccFromValue = probeCcFrom.load();
        r.ccToValue = probeCcTo.load();
        r.result.state = Probe::running;
        r.result.ccTest = want == 2;
        r.result.hits = want == 2 ? 1 : 3;
        r.phase = ProbeRun::floor;
        r.dirty = true;
    }
    switch ((int) r.phase)
    {
        case ProbeRun::noteOn:
            sink.send (juce::MidiMessage::noteOn (r.channel, r.note, (juce::uint8) 100));
            r.phase = ProbeRun::held;
            r.t = 0;
            r.onset = -1;
            r.peak = r.mainPeak = r.sidePeak = 0.0f;
            break;
        case ProbeRun::noteOff:
        {
            sink.send (juce::MidiMessage::noteOff (r.channel, r.note));
            auto& res = r.result;
            ++res.played;
            if (r.onset >= 0 && res.heard < 8)
                res.latencyMs[res.heard++] = (float) (1000.0 * r.onset / sampleRate);
            res.peakDb = juce::jmax (res.peakDb, juce::Decibels::gainToDecibels (r.peak, -100.0f));
            res.clipped = res.clipped || r.peak >= 0.995f;
            if (r.mainPeak > 0.003f) res.heardOn |= 1;
            if (r.sidePeak > 0.003f) res.heardOn |= 2;
            r.phase = ProbeRun::gap;
            r.t = 0;
            r.dirty = true;
            break;
        }
        case ProbeRun::ccFrom:
            sink.send (juce::MidiMessage::controllerEvent (r.channel, r.cc, r.ccFromValue));
            r.phase = ProbeRun::ccSettle;
            r.t = 0;
            break;
        case ProbeRun::ccNote:
            sink.send (juce::MidiMessage::noteOn (r.channel, r.note, (juce::uint8) 100));
            r.phase = ProbeRun::ccWaitA;
            r.t = 0;
            r.peak = r.mainPeak = r.sidePeak = 0.0f;
            break;
        case ProbeRun::ccTo:
            sink.send (juce::MidiMessage::controllerEvent (r.channel, r.cc, r.ccToValue));
            r.phase = ProbeRun::ccWaitB;
            r.t = 0;
            break;
        case ProbeRun::ccOff:
        {
            sink.send (juce::MidiMessage::noteOff (r.channel, r.note));
            auto& res = r.result;
            res.played = 1;
            const double a = r.countA > 0 ? std::sqrt (r.sumA / r.countA) : 0.0;
            const double b = r.countB > 0 ? std::sqrt (r.sumB / r.countB) : 0.0;
            res.ccLevelA = (float) a;
            res.ccLevelB = (float) b;
            res.heard = a > 1.0e-4 || b > 1.0e-4 ? 1 : 0;
            res.ccDb = a > 1.0e-6 && b > 1.0e-6 ? (float) (20.0 * std::log10 (b / a)) : 0.0f;
            // brightness: the slope's share of the signal (what a first difference keeps grows with the highs)
            const double ba = r.sumA > 1.0e-12 ? std::sqrt (r.diffA / r.sumA) : 0.0;
            const double bb = r.sumB > 1.0e-12 ? std::sqrt (r.diffB / r.sumB) : 0.0;
            res.ccBright = ba > 1.0e-9 ? (float) (bb / ba - 1.0) : 0.0f;
            res.peakDb = juce::Decibels::gainToDecibels (r.peak, -100.0f);
            res.clipped = r.peak >= 0.995f;
            if (r.mainPeak > 0.003f) res.heardOn |= 1;
            if (r.sidePeak > 0.003f) res.heardOn |= 2;
            res.state = Probe::done;
            r.phase = ProbeRun::off;
            r.dirty = true;
            probeRunning = false;
            break;
        }
        default:
            break;
    }
    if (r.dirty)
        probePublish();
}

// through the block: the floor, the attacks, the levels
void ReplikantProcessor::probeListen (const float* const* chans, int numIn, int n, const juce::AudioBuffer<float>& mainIn,
                                      const juce::AudioBuffer<float>* side, bool echoed)
{
    auto& r = run;
    if (r.phase == ProbeRun::off)
        return;
    if (echoed)
        r.result.echoed = true;
    const float sr = (float) sampleRate;
    auto secs = [sr] (float s) { return (int) (s * sr); };
    const float* l = numIn > 0 ? chans[0] : nullptr;
    const float* rr = numIn > 1 ? chans[1] : l;
    const int mainCh = mainIn.getNumChannels();
    const int sideCh = side != nullptr ? side->getNumChannels() : 0;
    for (int i = 0; i < n; ++i)
    {
        const float xl = l != nullptr ? l[i] : 0.0f, xr = rr != nullptr ? rr[i] : 0.0f;
        const float a = juce::jmax (std::abs (xl), std::abs (xr));
        const float mono = 0.5f * (xl + xr);
        ++r.t;
        switch ((int) r.phase)
        {
            case ProbeRun::floor:
            case ProbeRun::ccSettle:
                r.floorPeak = juce::jmax (r.floorPeak, a);
                if (r.t >= secs (0.15f))
                {
                    r.thr = juce::jmax (r.floorPeak * 4.0f, 0.001f);
                    r.result.floorDb = juce::Decibels::gainToDecibels (r.floorPeak, -100.0f);
                    r.phase = r.result.ccTest ? (r.phase == ProbeRun::floor ? ProbeRun::ccFrom : ProbeRun::ccNote) : ProbeRun::quiet;
                    r.t = 0;
                    r.quietFor = 0;
                    r.dirty = true;
                }
                break;
            case ProbeRun::quiet:
                r.quietFor = a < r.thr ? r.quietFor + 1 : 0;
                if (r.quietFor >= secs (0.05f) || r.t >= secs (2.0f))
                {
                    if (r.quietFor < secs (0.05f))
                        r.result.drone = true;
                    r.phase = ProbeRun::noteOn;
                }
                break;
            case ProbeRun::held:
            case ProbeRun::ccWaitA:
            case ProbeRun::ccWaitB:
            case ProbeRun::ccMeasureA:
            case ProbeRun::ccMeasureB:
                if (r.phase == ProbeRun::held && r.onset < 0 && a > r.thr)
                    r.onset = r.t - 1;
                r.peak = juce::jmax (r.peak, a);
                for (int c = 0; c < juce::jmin (2, mainCh); ++c)
                    r.mainPeak = juce::jmax (r.mainPeak, std::abs (mainIn.getSample (c, i)));
                for (int c = 0; c < juce::jmin (2, sideCh); ++c)
                    r.sidePeak = juce::jmax (r.sidePeak, std::abs (side->getSample (c, i)));
                if (r.phase == ProbeRun::ccMeasureA || r.phase == ProbeRun::ccMeasureB)
                {
                    const double d = mono - r.lastX;
                    if (r.phase == ProbeRun::ccMeasureA) { r.sumA += mono * mono; r.diffA += d * d; ++r.countA; }
                    else                                  { r.sumB += mono * mono; r.diffB += d * d; ++r.countB; }
                }
                if (r.phase == ProbeRun::held && r.t >= secs (0.4f))
                    r.phase = ProbeRun::noteOff;
                else if (r.phase == ProbeRun::ccWaitA && r.t >= secs (0.35f))     { r.phase = ProbeRun::ccMeasureA; r.t = 0; }
                else if (r.phase == ProbeRun::ccMeasureA && r.t >= secs (0.2f))   r.phase = ProbeRun::ccTo;
                else if (r.phase == ProbeRun::ccWaitB && r.t >= secs (0.35f))     { r.phase = ProbeRun::ccMeasureB; r.t = 0; }
                else if (r.phase == ProbeRun::ccMeasureB && r.t >= secs (0.2f))   r.phase = ProbeRun::ccOff;
                break;
            case ProbeRun::gap:
                if (r.t >= secs (0.2f))
                {
                    if (r.result.played < r.result.hits)
                    {
                        r.phase = ProbeRun::quiet;
                        r.t = 0;
                        r.quietFor = 0;
                    }
                    else
                    {
                        r.result.state = Probe::done;
                        r.phase = ProbeRun::off;
                        r.dirty = true;
                        probeRunning = false;
                    }
                }
                break;
            default:
                break;
        }
        r.lastX = mono;
    }
    if (r.dirty)
        probePublish();
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
    xml.setAttribute ("cyListen", cycleOptions.listenDb);
    xml.setAttribute ("rsLength", resampleOptions.length);
    xml.setAttribute ("rsSynced", resampleOptions.synced);
    xml.setAttribute ("rsRoot", resampleOptions.root);
    xml.setAttribute ("rsResetFx", resampleOptions.resetFx);
    if (auto ins = getInstrument(); ins != nullptr && ins->folder.isDirectory())
        xml.setAttribute ("instrument", ins->folder.getFullPathName());
    if (auto st = params.copyState().createXml())
        xml.addChildElement (st.release());
    xml.setAttribute ("routes", juce::JSON::toString (modsToVar(), true));
    xml.setAttribute ("wtfSeed", juce::String ((juce::int64) player.wtfSeed.load()));
    xml.setAttribute ("scenes", juce::JSON::toString (scenesToVar(), true));
    xml.setAttribute ("fxOrder", juce::JSON::toString (fxOrderToVar(), true));
    {
        // RADIO: the four receivers' sources, the take (its sound kept as a WAV beside the instruments)
        const juce::ScopedLock sl (radioLock);
        auto* r = new juce::DynamicObject();
        r->setProperty ("sources", net != nullptr ? net->toVar() : pendingSources);
        if (takeFile.existsAsFile())
            r->setProperty ("take", takeFile.getFullPathName());
        xml.setAttribute ("radio", juce::JSON::toString (juce::var (r), true));
    }
    if (drum.allocated())
        xml.setAttribute ("drum", juce::JSON::toString (drumToVar(), true));
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
    // (before 0.3.6, DIRECT was the default: kept by projects that never chose it. Back to OFF: no howl at CAPTURE)
    if (options.monitor == 1 && Updater::isNewer ("0.3.6", xml->getStringAttribute ("version", "0.0.0")))
        options.monitor = 0;
    options.thru = xml->getBoolAttribute ("thru", options.thru);
    cycleOptions.note = juce::jlimit (0, 127, xml->getIntAttribute ("cyNote", cycleOptions.note));
    cycleOptions.count = juce::jlimit (0, 3, xml->getIntAttribute ("cyCount", cycleOptions.count));
    cycleOptions.vary = juce::jlimit (0, 3, xml->getIntAttribute ("cyVary", cycleOptions.vary));
    cycleOptions.cc = juce::jlimit (0, 127, xml->getIntAttribute ("cyCc", cycleOptions.cc));
    cycleOptions.ccFrom = juce::jlimit (0, 127, xml->getIntAttribute ("cyFrom", cycleOptions.ccFrom));
    cycleOptions.ccTo = juce::jlimit (0, 127, xml->getIntAttribute ("cyTo", cycleOptions.ccTo));
    cycleOptions.hold = juce::jlimit (0.15, 4.0, xml->getDoubleAttribute ("cyHold", cycleOptions.hold));
    cycleOptions.settle = juce::jlimit (0.05, 4.0, xml->getDoubleAttribute ("cySettle", cycleOptions.settle));
    cycleOptions.listenDb = juce::jlimit (-70.0, -6.0, xml->getDoubleAttribute ("cyListen", cycleOptions.listenDb));
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
    player.wtfSeed = (uint32_t) xml->getStringAttribute ("wtfSeed", "2135587861").getLargeIntValue();
    scenesFromVar (juce::JSON::parse (xml->getStringAttribute ("scenes")));
    fxOrderFromVar (juce::JSON::parse (xml->getStringAttribute ("fxOrder")));
    // RADIO: the receivers tuned as they were (connected when the tab listens to one), the take read back
    if (xml->hasAttribute ("radio"))
    {
        const auto r = juce::JSON::parse (xml->getStringAttribute ("radio"));
        {
            const juce::ScopedLock sl (radioLock);
            if (net != nullptr)
                net->fromVar (r.getProperty ("sources", {}));
            else
                pendingSources = r.getProperty ("sources", {});
        }
        const juce::File take (r.getProperty ("take", "").toString());
        if (take.existsAsFile())
            radioPool->addJob ([this, take]
            {
                rk::Channels ch;
                double rate = 0.0;
                rk::WavInfo info;
                if (! rk::readWav (take.getFullPathName().toStdString(), ch, rate, info) || ch.empty() || rate <= 0.0)
                    return;
                auto a = std::make_shared<radio::Take::Audio>();
                a->l = ch[0];
                a->r = ch.size() > 1 ? ch[1] : ch[0];
                a->sampleRate = rate;
                later ([this, a, take] { takeFile = take; useTake (a, take.getFileNameWithoutExtension(), false); });
            });
    }
    if (xml->hasAttribute ("drum"))
        drumFromVar (juce::JSON::parse (xml->getStringAttribute ("drum")));
    settingsChanged();
    const auto f = instrumentio::movedFolder (juce::File (xml->getStringAttribute ("instrument")));
    if (xml->hasAttribute ("instrument"))
    {
        if (f.isDirectory())
            openInstrument (f, false);     // (the project's own sound, not the one kept with the instrument)
        else
            later ([this, f] { if (onMessage) onMessage ("Instrument introuvable : "_fr + f.getFullPathName()); });
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
            // (a DRUM track's setting: "drum3.cut")
            if (snd::isDrumDest (d))
                ro->setProperty ("to", "drum" + juce::String (snd::drumDestTrack (d) + 1) + "." + juce::String (drum::def (snd::drumDestParam (d)).id));
            else
                ro->setProperty ("to", juce::String (snd::def (d).id));
            ro->setProperty ("amount", (double) r.amount.load());
            routes.add (juce::var (ro));
        }
        for (auto& st : player.steps[(size_t) m])
            steps.add ((double) st.load());
        o->setProperty ("routes", routes);
        o->setProperty ("steps", steps);
        o->setProperty ("polarity", 2);          // (ENV and FOLLOW obey their polarity since 0.4.5)
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
        // (before 0.4.5, ENV and FOLLOW went one way whatever their polarity said: kept so)
        if (! o.hasProperty ("polarity"))
        {
            const int type = (int) player.get (snd::modParam (m, snd::mType));
            if (type == (int) dsp::ModType::envelope || type == (int) dsp::ModType::follower)
                if (auto* pol = params.getParameter (snd::def (snd::modParam (m, snd::mPolarity)).id))
                    pol->setValueNotifyingHost (pol->convertTo0to1 (1.0f));
        }
        player.clearRoutes (m);
        if (auto* rs = o["routes"].getArray())
            for (auto& r : *rs)
            {
                const auto to = r["to"].toString();
                int d = snd::find (to.toStdString());
                if (to.startsWith ("drum") && to.containsChar ('.'))
                {
                    const int t = to.fromFirstOccurrenceOf ("drum", false, false).upToFirstOccurrenceOf (".", false, false).getIntValue() - 1;
                    const auto id = to.fromFirstOccurrenceOf (".", false, false);
                    d = -1;
                    for (int q = 0; q < drum::numParams && t >= 0 && t < drum::numTracks; ++q)
                        if (id == drum::def (q).id)
                            d = snd::drumDest (t, q);
                }
                if (d >= 0 && snd::modulates (d))
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
    o->setProperty ("wtfSeed", (juce::int64) player.wtfSeed.load());
    o->setProperty ("scenes", scenesToVar());
    o->setProperty ("fxOrder", fxOrderToVar());
    return juce::var (o);
}

// the chain's order, by the stages' names (an unknown or missing order: the usual one)
juce::var ReplikantProcessor::fxOrderToVar() const
{
    static const char* names[Player::numStages] = { "drive", "spectre", "pitch", "chorus", "crush", "espace", "echo" };
    juce::Array<juce::var> a;
    for (int st : player.fxOrder())
        a.add (juce::String (names[st]));
    return a;
}

void ReplikantProcessor::fxOrderFromVar (const juce::var& v)
{
    // the stages by name; one missing (ECHO, in an order kept before 0.4.5) goes in before ESPACE
    static const char* names[Player::numStages] = { "drive", "spectre", "pitch", "chorus", "crush", "espace", "echo" };
    std::vector<int> listed;
    if (v.isArray())
        for (int p = 0; p < v.size(); ++p)
            for (int st = 0; st < Player::numStages; ++st)
                if (v[p].toString() == names[st] && std::find (listed.begin(), listed.end(), st) == listed.end())
                    listed.push_back (st);
    if (listed.empty())
        listed = { 0, 1, 2, 3, 4, 6, 5 };
    for (int st = 0; st < Player::numStages; ++st)
        if (std::find (listed.begin(), listed.end(), st) == listed.end())
        {
            const auto space = std::find (listed.begin(), listed.end(), 5);
            listed.insert (st == 6 ? space : listed.end(), st);
        }
    std::array<int, Player::numStages> order {};
    for (int p = 0; p < Player::numStages; ++p)
        order[(size_t) p] = listed[(size_t) p];
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
    player.wtfSeed = v.hasProperty ("wtfSeed") ? (uint32_t) (juce::int64) v["wtfSeed"] : 0x7f4a7c15u;
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

//============================================================================== RADIO
RadioNet& ReplikantProcessor::radioNet()
{
    if (net == nullptr)
    {
        const juce::ScopedLock sl (radioLock);
        radio.allocate();
        net = std::make_unique<RadioNet> (radio.bank);
        if (! pendingSources.isVoid())
            net->fromVar (pendingSources);
        pendingSources = juce::var();
    }
    return *net;
}

juce::File ReplikantProcessor::radioFolder() const
{
    return instrumentsRoot().getChildFile ("Radio");
}

juce::String ReplikantProcessor::radioStationName (int rx) const
{
    if (net == nullptr)
        return {};
    const auto s = net->getSource (rx);
    if (s.kind == 1)
        return s.name.isNotEmpty() ? s.name : s.url;
    if (s.kind == 2)
        return juce::String (s.khz, s.khz < 10000.0 ? 2 : 1) + " kHz " + s.mode.toUpperCase();
    return {};
}

// a take in hand: cut at its attacks (on the radio's own thread), kept as a WAV when it's new, then the pads play it
void ReplikantProcessor::useTake (std::shared_ptr<const radio::Take::Audio> a, const juce::String& name, bool save)
{
    if (a == nullptr || a->l.empty())
        return;
    const int pads = 4 << juce::jlimit (0, 2, (int) player.get (snd::rdPads));
    const float sens = player.get (snd::rdSens);
    juce::File file = takeFile;
    if (save)
    {
        auto clean = juce::File::createLegalFileName (name).trim();
        file = radioFolder().getNonexistentChildFile (clean.isNotEmpty() ? clean : juce::String ("take"), ".wav", false);
    }
    ++slicing;
    radioPool->addJob ([this, a, name, pads, sens, file, save]
    {
        auto t = std::make_shared<radio::Take>();
        t->audio = a;
        t->name = name.toStdString();
        t->slices = radio::slice (a->l.data(), a->r.data(), (int) a->l.size(), a->sampleRate, pads, sens);
        bool saved = ! save;
        if (save && file.getParentDirectory().createDirectory().wasOk())
            saved = rk::writeWav (file.getFullPathName().toStdString(), { a->l, a->r }, a->sampleRate, 24, {});
        later ([this, t, pads, sens, file, saved]
        {
            --slicing;
            takeNow = t;
            {
                const juce::ScopedLock sl (radioLock);
                takeFile = saved ? file : juce::File();
            }
            slicedWith = sens;
            slicedPads = pads;
            takes.push_back ({ t, 0.0 });
            radio.setTake (t.get());
            sendChangeMessage();
        });
    });
}

bool ReplikantProcessor::radioGrab (juce::String& why)
{
    if (radio.recording())
    {
        why = "REC tourne : arrête-le d'abord."_fr;
        return false;
    }
    auto a = std::make_shared<radio::Take::Audio>();
    const double secs = radio::takeSeconds ((int) player.get (snd::rdTake));
    float peak = 0.0f;
    if (radio.grab (secs, *a))
        for (size_t i = 0; i < a->l.size(); ++i)
            peak = juce::jmax (peak, std::abs (a->l[i]), std::abs (a->r[i]));
    if (peak < 1.0e-4f)
    {
        why = "Rien n'a été entendu : accorde un récepteur (page TUNE) et écoute-le dans l'onglet RADIO."_fr;
        return false;
    }
    const auto station = radioStationName ((int) player.get (snd::rdRx));
    useTake (a, (station.isNotEmpty() ? station : juce::String ("radio")) + " " + juce::Time::getCurrentTime().formatted ("%H-%M-%S"), true);
    return true;
}

bool ReplikantProcessor::radioRec (juce::String& why)
{
    if (! radio.recording())
    {
        radio.recStart();
        return true;
    }
    auto a = std::make_shared<radio::Take::Audio>();
    if (! radio.recStop (*a))
    {
        why = "Trop court : rien n'a été gardé."_fr;
        return false;
    }
    const auto station = radioStationName ((int) player.get (snd::rdRx));
    useTake (a, (station.isNotEmpty() ? station : juce::String ("radio")) + " " + juce::Time::getCurrentTime().formatted ("%H-%M-%S"), true);
    return true;
}

void ReplikantProcessor::radioReslice()
{
    if (takeNow != nullptr && takeNow->audio != nullptr)
        useTake (takeNow->audio, juce::String (takeNow->name), false);
}

void ReplikantProcessor::radioClear()
{
    radio.recCancel();
    radio.setTake (nullptr);
    takeNow = nullptr;
    const juce::ScopedLock sl (radioLock);
    takeFile = juce::File();
}

bool ReplikantProcessor::radioToSampler (int pad, juce::String& why)
{
    const auto t = takeNow;
    if (t == nullptr || t->audio == nullptr || t->audio->l.empty())
    {
        why = "Pas encore de prise : GRAB ou REC d'abord."_fr;
        return false;
    }
    if (isSampling())
    {
        why = "Une capture tourne."_fr;
        return false;
    }
    const auto& A = *t->audio;
    int a = 0, b = (int) A.l.size();
    if (pad >= 0)
    {
        if (pad >= (int) t->slices.size())
        {
            why = "Ce pad est vide."_fr;
            return false;
        }
        a = juce::jlimit (0, b - 1, t->slices[(size_t) pad].start);
        b = juce::jlimit (a + 1, b, t->slices[(size_t) pad].end);
    }
    if (b - a < 64)
    {
        why = "Trop court."_fr;
        return false;
    }
    rk::Channels audio { std::vector<float> (A.l.begin() + a, A.l.begin() + b), std::vector<float> (A.r.begin() + a, A.r.begin() + b) };
    // (a pad: its end faded in 3 ms, it was cut there)
    const int fade = juce::jmin (b - a, (int) (0.003 * A.sampleRate));
    for (int i = 0; i < fade; ++i)
        for (auto& ch : audio)
            ch[ch.size() - 1 - (size_t) i] *= (float) i / (float) fade;
    rk::Loop loop;
    if (pad < 0)
    {
        loop.start = 0;
        loop.end = b - a;
    }
    const auto name = juce::String (t->name) + (pad >= 0 ? " pad " + juce::String (pad + 1) : juce::String());
    auto ins = instrumentio::soundInstrument (std::move (audio), A.sampleRate, name, 60, 0.0f, false, loop);
    setSoundParam (snd::engine, (float) snd::sampler);
    setSoundParam (snd::playMode, 0.0f);
    setSoundParam (snd::loops, pad < 0 ? 1.0f : 0.0f);
    setSoundParam (snd::start, 0.0f);
    setSoundParam (snd::speed, 1.0f);
    setSoundParam (snd::reverse, 0.0f);
    setSoundParam (snd::loopStart, 0.0f);
    setSoundParam (snd::loopSize, 1.0f);
    setSoundParam (snd::transpose, 0.0f);
    setSoundParam (snd::fine, 0.0f);
    setSoundParam (snd::tune, 0.0f);
    instrumentName = name;
    session.renderKeys = false;
    settingsChanged();
    const auto folder = Worker::uniqueFolder (instrumentsRoot(), name);
    worker.post ("RADIO", [this, ins, folder]
    {
        const auto r = instrumentio::save (*ins, folder);
        if (r.failed())
            later ([this, r] { if (onMessage) onMessage (r.getErrorMessage()); });
        worker.renderAndPublish (ins, true);
    });
    if (onMessage)
        onMessage ((pad >= 0 ? "Pad "_fr + juce::String (pad + 1) : juce::String ("La prise"_fr)) + " dans le sampler : "_fr
                   + juce::String ((double) (b - a) / A.sampleRate, 2) + " s sur C3"_fr + (pad < 0 ? juce::String (", bouclée"_fr) : juce::String()) + ".");
    sendChangeMessage();
    return true;
}

void ReplikantProcessor::radioTick()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool radioOn = (int) player.get (snd::engine) == snd::radio;
    if (radioOn && net == nullptr)
        radioNet();
    // REC stops on its own at the longest take
    if (radio.recording() && radio.recSeconds() >= radio::longestTake - 0.05)
    {
        juce::String why;
        radioRec (why);
        if (onMessage)
            onMessage ("REC arrêté au bout de 30 s : la prise est découpée."_fr);
    }
    // the take cut again once SENSITIVITY or PADS stop moving
    {
        double& changedAt = sliceChangedAt;
        const int pads = 4 << juce::jlimit (0, 2, (int) player.get (snd::rdPads));
        const float sens = player.get (snd::rdSens);
        if (takeNow != nullptr && slicing.load() == 0 && (std::abs (sens - slicedWith) > 1.0e-4f || pads != slicedPads))
        {
            if (changedAt == 0.0)
                changedAt = now;
            else if (now - changedAt > 0.3)
            {
                changedAt = 0.0;
                slicedWith = sens;
                slicedPads = pads;
                radioReslice();
            }
        }
        else
            changedAt = 0.0;
    }
    // the takes the audio no longer plays, let go (two seconds after they were replaced)
    for (auto& k : takes)
        if (k.take != takeNow && k.retiredAt == 0.0)
            k.retiredAt = now;
    const auto* used = radio.takeInUse();
    const auto* set = radio.takeSet();
    takes.erase (std::remove_if (takes.begin(), takes.end(), [&] (const KeptTake& k)
    {
        return k.retiredAt > 0.0 && now - k.retiredAt > 2.0 && k.take.get() != used && k.take.get() != set;
    }), takes.end());
}

//============================================================================== DRUM
void ReplikantProcessor::drumReady()
{
    if (drum.allocated())
        return;
    drum.allocate();
    drumBasic = dsp::CycleBank::basics();
    drum.setBanks (drumBasic.get(), drumCaptured.get());
}

std::shared_ptr<const drum::Sound> ReplikantProcessor::drumSound (int track) const
{
    return track >= 0 && track < drum::numTracks ? drumSlots[(size_t) track].sound : nullptr;
}

// a sound on a track: the audio swaps it in (the old one kept until it has), kept as a WAV when it isn't one already
void ReplikantProcessor::useDrumSound (int track, std::shared_ptr<const radio::Take::Audio> a, std::vector<radio::Slice> slices,
                                       const juce::String& name, const juce::String& path, int from, int to)
{
    if (track < 0 || track >= drum::numTracks || a == nullptr || a->l.empty())
        return;
    drumReady();
    auto s = std::make_shared<drum::Sound>();
    s->audio = a;
    s->slices = std::move (slices);
    s->name = name.toStdString();
    auto& slot = drumSlots[(size_t) track];
    if (slot.sound != nullptr)
        drumKept.push_back ({ slot.sound, juce::Time::getMillisecondCounterHiRes() * 0.001 });
    slot.sound = s;
    slot.path = path;
    slot.from = from;
    slot.to = to;
    drum.setSound (track, s.get());
    if (path.isEmpty())
    {
        // (kept beside the instruments: the project finds it again)
        const auto file = instrumentsRoot().getChildFile ("Drums").getNonexistentChildFile (juce::File::createLegalFileName (name).trim().isNotEmpty()
                                                                                            ? juce::File::createLegalFileName (name).trim() : juce::String ("drum"), ".wav", false);
        radioPool->addJob ([this, a, file, track, s]
        {
            if (! file.getParentDirectory().createDirectory().wasOk()
                || ! rk::writeWav (file.getFullPathName().toStdString(), { a->l, a->r }, a->sampleRate, 24, {}))
                return;
            later ([this, file, track, s, len = (int) a->l.size()]
            {
                auto& sl = drumSlots[(size_t) track];
                if (sl.sound == s)
                {
                    sl.path = file.getFullPathName();
                    sl.from = 0;
                    sl.to = len;
                }
            });
        });
    }
    sendChangeMessage();
}

// a track made ready for a sound (its voice's own colours off: no sweep, click, noise; open; held to its end)
static void soundAsIs (drum::Track& tk)
{
    tk.p[drum::pStart] = 0.0f;
    tk.p[drum::pReverse] = 0.0f;
    tk.p[drum::pTune] = 0.0f;
    tk.p[drum::pSweep] = 0.0f;
    tk.p[drum::pPmod] = 0.0f;
    tk.p[drum::pBody] = 1.0f;
    tk.p[drum::pClick] = 0.0f;
    tk.p[drum::pNoise] = 0.0f;
    tk.p[drum::pAttack] = 0.0f;
    tk.p[drum::pDecay] = 1.0f;
    tk.p[drum::pCutoff] = 20000.0f;
    tk.p[drum::pReso] = 0.0f;
    tk.p[drum::pFEnv] = 0.0f;
    tk.p[drum::pDrive] = 0.0f;
    tk.p[drum::pNote] = 60.0f;
}

bool ReplikantProcessor::radioToDrum (int pad, int track, juce::String& why)
{
    const auto t = takeNow;
    if (t == nullptr || t->audio == nullptr || t->audio->l.empty())
    {
        why = "Pas encore de prise : GRAB ou REC d'abord."_fr;
        return false;
    }
    track = juce::jlimit (0, drum::numTracks - 1, track);
    drumReady();
    const auto& A = *t->audio;
    const juce::String takePath = takeFile.existsAsFile() ? takeFile.getFullPathName() : juce::String();
    auto& tk = drum.pattern (drum.playingPattern()).tr[track];
    bool empty = true;
    for (int s = 0; s < drum::numSteps; ++s)
        empty = empty && tk.trig[s] == 0;
    if (pad < 0)
    {
        // the whole take, its slices: on SLICES; a track with no trig gets them on its steps, one a step, in order
        useDrumSound (track, t->audio, t->slices, juce::String (t->name), takePath, 0, (int) A.l.size());
        tk.p[drum::pSource] = drum::slices;
        soundAsIs (tk);
        if (empty)
        {
            const int n = juce::jmin (drum::pageSteps, (int) t->slices.size());
            tk.len = (uint8_t) juce::jmax (1, n);
            for (int s = 0; s < n; ++s)
            {
                tk.trig[s] = 1;
                tk.lock[s][drum::pSlice] = (float) s;
            }
        }
        if (onMessage)
            onMessage ("La prise est sur la piste "_fr + juce::String (track + 1) + " : ses " + juce::String ((int) t->slices.size())
                       + (empty ? " tranches sur les pas, dans l'ordre (SLICE se verrouille par pas)."_fr : juce::String (" tranches (SLICE choisit laquelle joue)."_fr)));
    }
    else
    {
        if (pad >= (int) t->slices.size())
        {
            why = "Ce pad est vide."_fr;
            return false;
        }
        const int a = juce::jlimit (0, (int) A.l.size() - 1, t->slices[(size_t) pad].start);
        const int b = juce::jlimit (a + 1, (int) A.l.size(), t->slices[(size_t) pad].end);
        auto cut = std::make_shared<radio::Take::Audio>();
        cut->sampleRate = A.sampleRate;
        cut->l.assign (A.l.begin() + a, A.l.begin() + b);
        cut->r.assign (A.r.begin() + a, A.r.begin() + b);
        useDrumSound (track, cut, {}, juce::String (t->name) + " pad " + juce::String (pad + 1), takePath, a, b);
        tk.p[drum::pSource] = drum::sample;
        soundAsIs (tk);
        if (onMessage)
            onMessage ("Pad "_fr + juce::String (pad + 1) + " sur la piste "_fr + juce::String (track + 1) + ".");
    }
    drumTrack = track;
    setSoundParam (snd::engine, (float) snd::drum);
    sendChangeMessage();
    return true;
}

bool ReplikantProcessor::drumLoadFile (int track, const juce::File& f, juce::String& why)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0.0)
    {
        why = "Je ne sais pas lire ce son : "_fr + f.getFileName();
        return false;
    }
    const int frames = (int) juce::jmin ((juce::int64) (30.0 * r->sampleRate), r->lengthInSamples);
    juce::AudioBuffer<float> buf (2, frames);
    r->read (&buf, 0, frames, 0, true, true);
    auto a = std::make_shared<radio::Take::Audio>();
    a->sampleRate = r->sampleRate;
    a->l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + frames);
    a->r.assign (buf.getReadPointer (r->numChannels > 1 ? 1 : 0), buf.getReadPointer (r->numChannels > 1 ? 1 : 0) + frames);
    drumReady();
    // (a WAV is found again where it is; anything else is kept as a WAV beside the instruments)
    const bool asIs = f.hasFileExtension ("wav") && frames == (int) r->lengthInSamples;
    useDrumSound (track, a, {}, f.getFileNameWithoutExtension(), asIs ? f.getFullPathName() : juce::String(), 0, frames);
    auto& tk = drum.pattern (drum.playingPattern()).tr[juce::jlimit (0, drum::numTracks - 1, track)];
    tk.p[drum::pSource] = drum::sample;
    soundAsIs (tk);
    drum.pattern (drum.playingPattern()).used = 1;
    drumTrack = juce::jlimit (0, drum::numTracks - 1, track);
    if (onMessage)
        onMessage (f.getFileName() + " sur la piste "_fr + juce::String (track + 1) + ".");
    return true;
}

void ReplikantProcessor::drumClearSound (int track)
{
    if (track < 0 || track >= drum::numTracks)
        return;
    auto& slot = drumSlots[(size_t) track];
    if (slot.sound != nullptr)
        drumKept.push_back ({ slot.sound, juce::Time::getMillisecondCounterHiRes() * 0.001 });
    slot = {};
    drum.setSound (track, nullptr);
}

void ReplikantProcessor::drumTick()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if ((int) player.get (snd::engine) == snd::drum)
        drumReady();
    if (! drum.allocated())
        return;
    // the CAPTURED cycles: the instrument's (the old bank kept a moment, the audio may still read it)
    auto ins = getInstrument();
    auto bank = ins != nullptr ? ins->cycles : nullptr;
    if (bank != drumCaptured)
    {
        drumCapturedBefore = drumCaptured;
        drumCapturedSince = now;
        drumCaptured = bank;
        drum.setBanks (drumBasic.get(), drumCaptured.get());
    }
    else if (drumCapturedBefore != nullptr && now - drumCapturedSince > 2.0)
        drumCapturedBefore = nullptr;
    // the sounds the audio no longer plays, let go
    drumKept.erase (std::remove_if (drumKept.begin(), drumKept.end(), [&] (const KeptSound& k)
    {
        if (now - k.retiredAt < 2.0)
            return false;
        for (int t = 0; t < drum::numTracks; ++t)
            if (drum.soundInUse (t) == k.sound.get() || drum.soundSet (t) == k.sound.get())
                return false;
        return true;
    }), drumKept.end());
}

// the project: the patterns written (their sounds, steps, ratchets, locks), the mutes, the chain, the sounds' files
juce::var ReplikantProcessor::drumToVar() const
{
    auto* o = new juce::DynamicObject();
    auto& d = const_cast<drum::Engine&> (drum);
    o->setProperty ("playing", d.playingPattern());
    juce::Array<juce::var> chain, mutes, sounds, pats;
    for (int c : d.chain())
        chain.add (c);
    for (int t = 0; t < drum::numTracks; ++t)
    {
        mutes.add (d.mute[t].load());
        const auto& sl = drumSlots[(size_t) t];
        if (sl.sound == nullptr || sl.path.isEmpty())
        {
            sounds.add (juce::var());
            continue;
        }
        auto* s = new juce::DynamicObject();
        s->setProperty ("path", sl.path);
        s->setProperty ("from", sl.from);
        s->setProperty ("to", sl.to);
        s->setProperty ("name", juce::String (sl.sound->name));
        juce::Array<juce::var> cuts;
        for (const auto& c : sl.sound->slices)
            cuts.add (juce::Array<juce::var> { c.start, c.end });
        s->setProperty ("slices", cuts);
        sounds.add (juce::var (s));
    }
    for (int i = 0; i < drum::numPatterns; ++i)
    {
        const auto& pat = d.pattern (i);
        if (! pat.used)
            continue;
        auto* po = new juce::DynamicObject();
        po->setProperty ("i", i);
        po->setProperty ("chg", (int) pat.chg);
        juce::Array<juce::var> tracks;
        for (const auto& tk : pat.tr)
        {
            auto* to = new juce::DynamicObject();
            juce::Array<juce::var> rat, locks;
            juce::String trig;
            for (int s = 0; s < drum::numSteps; ++s)
            {
                trig << (tk.trig[s] ? "1" : "0");
                if (tk.rat[s] > 1)
                    rat.add (juce::Array<juce::var> { s, (int) tk.rat[s] });
                for (int k = 0; k < drum::numParams; ++k)
                    if (! std::isnan (tk.lock[s][k]))
                        locks.add (juce::Array<juce::var> { s, juce::String (drum::def (k).id), tk.lock[s][k] });
            }
            auto* pv = new juce::DynamicObject();
            for (int k = 0; k < drum::numParams; ++k)
                pv->setProperty (drum::def (k).id, tk.p[k]);
            to->setProperty ("p", juce::var (pv));
            to->setProperty ("len", (int) tk.len);
            to->setProperty ("speed", (int) tk.speed);
            to->setProperty ("trig", trig);
            to->setProperty ("rat", rat);
            to->setProperty ("locks", locks);
            tracks.add (juce::var (to));
        }
        po->setProperty ("tracks", tracks);
        pats.add (juce::var (po));
    }
    o->setProperty ("chain", chain);
    o->setProperty ("mute", mutes);
    o->setProperty ("sounds", sounds);
    o->setProperty ("patterns", pats);
    return juce::var (o);
}

void ReplikantProcessor::drumFromVar (const juce::var& v)
{
    if (! v.isObject())
        return;
    drumReady();
    auto paramOf = [] (const juce::String& id) { for (int k = 0; k < drum::numParams; ++k) if (id == drum::def (k).id) return k; return -1; };
    for (int i = 0; i < drum::numPatterns; ++i)
        drum.pattern (i).reset();
    if (auto* pats = v.getProperty ("patterns", {}).getArray())
        for (const auto& po : *pats)
        {
            const int i = (int) po.getProperty ("i", -1);
            if (i < 0 || i >= drum::numPatterns)
                continue;
            auto& pat = drum.pattern (i);
            pat.used = 1;
            pat.chg = (uint8_t) juce::jlimit (0, 255, (int) po.getProperty ("chg", 0));
            if (auto* tracks = po.getProperty ("tracks", {}).getArray())
                for (int t = 0; t < juce::jmin (drum::numTracks, tracks->size()); ++t)
                {
                    const auto& to = (*tracks)[t];
                    auto& tk = pat.tr[t];
                    if (auto* pv = to.getProperty ("p", {}).getDynamicObject())
                        for (const auto& kv : pv->getProperties())
                            if (const int k = paramOf (kv.name.toString()); k >= 0)
                                tk.p[k] = drum::legal (k, (float) (double) kv.value);
                    tk.len = (uint8_t) juce::jlimit (1, drum::numSteps, (int) to.getProperty ("len", 16));
                    tk.speed = (uint8_t) juce::jlimit (0, drum::numSpeeds - 1, (int) to.getProperty ("speed", 4));
                    const auto trig = to.getProperty ("trig", "").toString();
                    for (int s = 0; s < drum::numSteps && s < trig.length(); ++s)
                        tk.trig[s] = trig[s] == '1' ? 1 : 0;
                    if (auto* rat = to.getProperty ("rat", {}).getArray())
                        for (const auto& r : *rat)
                            if (const int s = (int) r[0]; s >= 0 && s < drum::numSteps)
                                tk.rat[s] = (uint8_t) juce::jlimit (1, 16, (int) r[1]);
                    if (auto* locks = to.getProperty ("locks", {}).getArray())
                        for (const auto& l : *locks)
                        {
                            const int s = (int) l[0], k = paramOf (l[1].toString());
                            if (s >= 0 && s < drum::numSteps && k >= 0)
                                tk.lock[s][k] = drum::legal (k, (float) (double) l[2]);
                        }
                }
        }
    drum.pattern (0).used = 1;
    if (auto* m = v.getProperty ("mute", {}).getArray())
        for (int t = 0; t < juce::jmin (drum::numTracks, m->size()); ++t)
            drum.mute[t] = (bool) (*m)[t];
    std::vector<int> chain;
    if (auto* c = v.getProperty ("chain", {}).getArray())
        for (const auto& x : *c)
            chain.push_back ((int) x);
    drum.queue (juce::jlimit (0, drum::numPatterns - 1, (int) v.getProperty ("playing", 0)));
    drum.setChain (chain);
    // the sounds, read back from their files (on the radio's thread)
    for (int t = 0; t < drum::numTracks; ++t)
        drumClearSound (t);
    if (auto* sounds = v.getProperty ("sounds", {}).getArray())
        for (int t = 0; t < juce::jmin (drum::numTracks, sounds->size()); ++t)
        {
            const auto s = (*sounds)[t];
            if (! s.isObject())
                continue;
            const juce::File file (s.getProperty ("path", "").toString());
            const int from = (int) s.getProperty ("from", 0), to = (int) s.getProperty ("to", 0);
            const juce::String name = s.getProperty ("name", "").toString();
            std::vector<radio::Slice> cuts;
            if (auto* cs = s.getProperty ("slices", {}).getArray())
                for (const auto& c : *cs)
                    cuts.push_back ({ (int) c[0], (int) c[1], 0.0f });
            if (! file.existsAsFile())
                continue;
            radioPool->addJob ([this, file, from, to, name, cuts, t]
            {
                rk::Channels ch;
                double rate = 0.0;
                rk::WavInfo info;
                if (! rk::readWav (file.getFullPathName().toStdString(), ch, rate, info) || ch.empty() || rate <= 0.0)
                    return;
                const int len = (int) ch[0].size();
                const int a = juce::jlimit (0, juce::jmax (0, len - 1), from), b = to > a ? juce::jmin (len, to) : len;
                auto au = std::make_shared<radio::Take::Audio>();
                au->sampleRate = rate;
                au->l.assign (ch[0].begin() + a, ch[0].begin() + b);
                au->r.assign (ch[ch.size() > 1 ? 1 : 0].begin() + a, ch[ch.size() > 1 ? 1 : 0].begin() + b);
                later ([this, au, name, file, from, to, cuts, t] { useDrumSound (t, au, cuts, name, file.getFullPathName(), from, to); });
            });
        }
}
