// The whole plug-in as a host runs it: the processor prepared, MIDI notes in, blocks out. Run on GitHub's Macs at
// each push (Apple Silicon), so that what a host hears is checked there too: a sound that stays in range, numbers
// only, and that moves on from one block to the next (a block played over and over is a fault).

#include <JuceHeader.h>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/radio/RadioNet.h"

#include <tuple>

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                                                      \
    do {                                                                                      \
        ++checks;                                                                             \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } \
    } while (0)

static void playAsAHost (double rate, int block)
{
    ReplikantProcessor proc;
    proc.setRateAndBufferSizeDetails (rate, block);
    proc.prepareToPlay (rate, block);
    const int chans = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (chans, block);
    const int total = (int) (3.0 * rate);
    float peak = 0.0f, firstNote = 0.0f;
    double energy = 0.0;
    bool finite = true;
    int frames = 0, repeats = 0, blocks = 0;
    std::vector<float> previous ((size_t) block, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        const int chordAt = (int) (1.0 * rate);
        if (chordAt >= pos && chordAt < pos + block)
            for (int n : { 48, 55, 64 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), chordAt - pos);
        if (pos < chordAt)
            firstNote = peak;
        buf.clear();
        proc.processBlock (buf, midi);
        bool same = blocks > 2;
        for (int i = 0; i < block; ++i)
        {
            const float x = buf.getSample (0, i);
            same = same && x == previous[(size_t) i];
            previous[(size_t) i] = x;
        }
        if (same && peak > 0.01f)
            ++repeats;
        ++blocks;
        for (int ch = 0; ch < juce::jmin (2, proc.getTotalNumOutputChannels()); ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample (ch, i);
                finite = finite && std::isfinite (x);
                peak = std::max (peak, std::abs (x));
                energy += (double) x * x;
            }
        frames += block;
    }
    const double rms = std::sqrt (energy / (2.0 * frames));
    std::printf ("  as a host at %.0f Hz, blocks of %d: a note %.2f, with a chord %.2f, rms %.3f, %d blocks repeated\n",
                 rate, block, firstNote, peak, rms, repeats);
    CHECK (finite, "numbers only at %.0f Hz / %d", rate, block);
    CHECK (firstNote > 0.1f && firstNote < 0.5f, "a note at %.2f (%.0f Hz / %d)", firstNote, rate, block);
    CHECK (peak < 1.2f && rms > 0.03 && rms < 0.4, "with a chord: peak %.2f, rms %.3f (%.0f Hz / %d)", peak, rms, rate, block);
    CHECK (repeats == 0, "%d blocks played twice (%.0f Hz / %d)", repeats, rate, block);
}

// A host that hands the plug-in the same buffer each time without clearing it (in place, nothing on the input):
// what comes in is what went out the block before. Notes must still end: the sound back to silence after the
// release, never piling up.
static void playAsALazyHost (double rate, int block, int lag = 1, float hostGain = 1.0f, bool directUnderCapture = false)
{
    ReplikantProcessor proc;
    proc.setRateAndBufferSizeDetails (rate, block);
    proc.prepareToPlay (rate, block);
    if (directUnderCapture)
    {
        // the input heard (DIRECT, the capture page open) and handed back a little softer: no loop either
        proc.options.monitor = 1;
        proc.settingsChanged();
        proc.captureOpen = true;
    }
    const int chans = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    // (lag 2: two buffers taking turns, so what comes in went out two blocks before)
    std::vector<juce::AudioBuffer<float>> bufs ((size_t) lag, juce::AudioBuffer<float> (chans, block));
    for (auto& b : bufs)
        b.clear();
    int turn = 0;
    const int total = (int) (3.0 * rate);
    float peak = 0.0f, during = 0.0f;
    double tail = 0.0;
    int tailFrames = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        for (auto [at, note, on] : { std::tuple<double, int, bool> { 0.0, 60, true }, { 0.2, 64, true }, { 0.4, 67, true },
                                     { 0.6, 60, false }, { 0.6, 64, false }, { 0.6, 67, false } })
        {
            const int s = (int) (at * rate);
            if (s >= pos && s < pos + block)
                midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), s - pos);
        }
        auto& buf = bufs[(size_t) turn];
        turn = (turn + 1) % lag;
        proc.processBlock (buf, midi);          // (not cleared: an earlier block's output is this block's input)
        if (hostGain != 1.0f)
            buf.applyGain (hostGain);           // (a host that touches it on the way: not the same numbers any more)
        for (int ch = 0; ch < juce::jmin (2, proc.getTotalNumOutputChannels()); ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample (ch, i);
                peak = std::max (peak, std::isfinite (x) ? std::abs (x) : 1.0e9f);
                if (pos < (int) (0.6 * rate))
                    during = std::max (during, std::abs (x));
                if (pos >= (int) (2.5 * rate))
                {
                    tail += (double) x * x;
                    ++tailFrames;
                }
            }
    }
    const double tailRms = std::sqrt (tail / juce::jmax (1, tailFrames));
    std::printf ("  a lazy host at %.0f Hz, blocks of %d (input = output %d block%s before%s): peak %.2f, after the release %.5f rms\n",
                 rate, block, lag, lag > 1 ? "s" : "", directUnderCapture ? ", DIRECT under CAPTURE" : "", peak, tailRms);
    CHECK (during > 0.05f && peak < 1.2f, "lazy host: notes at %.2f, peak %.2f (%.0f Hz / %d)", during, peak, rate, block);
    CHECK (tailRms < 1.0e-3, "lazy host: the notes end (%.5f rms left, %.0f Hz / %d)", tailRms, rate, block);
}

// RADIO: the tab heard (a receiver fed by hand, no network), its pads on the notes from C1, a take cut at its hits and
// sent to the SAMPLER, the receivers and the take kept with the project
static bool waitFor (std::function<bool()> done, double seconds)
{
    const auto until = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (! done())
    {
        if (juce::Time::getMillisecondCounterHiRes() > until)
            return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
    return true;
}

static void radioAsAHost()
{
    const double rate = 48000.0;
    const int block = 512;
    auto proc = std::make_unique<ReplikantProcessor>();
    proc->setRateAndBufferSizeDetails (rate, block);
    proc->prepareToPlay (rate, block);
    proc->setSoundParam (snd::engine, (float) snd::radio);
    proc->radioNet();                                      // (as the tab does: the radio's memory, the network)
    proc->setSoundParam (snd::rdStatic, 0.0f);
    proc->setSoundParam (snd::rdTake, 1.0f);               // GRAB: 2 s
    auto& bank = proc->radio.bank;
    bank.setState (0, radio::live, radio::web);
    const int chans = juce::jmax (proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (chans, block);
    // the receiver's sound: four hits a second (a burst of noise, decaying) over a quiet tone
    juce::Random rnd (5);
    int64_t fed = 0;
    std::vector<int16_t> chunk;
    auto feed = [&] (int frames)
    {
        chunk.resize ((size_t) frames * 2);
        for (int i = 0; i < frames; ++i, ++fed)
        {
            const int64_t inBeat = fed % (radio::rate / 4);
            const double hit = inBeat < 6000 ? std::exp (-(double) inBeat / 1200.0) * (rnd.nextFloat() * 2.0 - 1.0) : 0.0;
            const double v = 0.6 * hit + 0.03 * std::sin (6.283185307 * 330.0 * (double) fed / radio::rate);
            chunk[(size_t) (2 * i)] = chunk[(size_t) (2 * i + 1)] = (int16_t) (v * 30000.0);
        }
        bank.push (0, chunk.data(), frames);
        // (the network's own thread for RX 1, started late on a busy machine, could set it back to idle: fed by hand, live)
        bank.setState (0, radio::live, radio::web);
    };
    double energy = 0.0;
    float peak = 0.0f;
    bool finite = true;
    auto play = [&] (double seconds, int note = -1)
    {
        energy = 0.0;
        int frames = 0;
        for (int pos = 0; pos < (int) (seconds * rate); pos += block)
        {
            feed (block);
            buf.clear();
            juce::MidiBuffer midi;
            if (note >= 0 && pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
            proc->processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (0, i);
                finite = finite && std::isfinite (v);
                peak = juce::jmax (peak, std::abs (v));
                energy += (double) v * v;
            }
            frames += block;
        }
        return std::sqrt (energy / juce::jmax (1, frames));
    };
    const double heard = play (4.0);
    std::printf ("  radio heard: %.3f rms, receiver bits %d\n", heard, bank.use.load());
    CHECK (heard > 0.02 && finite && peak < 1.5f, "the radio heard: %.4f rms, peak %.2f", heard, peak);
    CHECK (bank.use.load() == 1, "only RX 1 kept connected: %d", bank.use.load());
    // GRAB: the last two seconds, cut at their hits (eight)
    juce::String why;
    CHECK (proc->radioGrab (why), "grab: %s", why.toRawUTF8());
    CHECK (waitFor ([&] { return proc->radioTake() != nullptr; }, 10.0), "the take cut");
    auto take = proc->radioTake();
    if (take == nullptr)
        return;
    const int pads = (int) take->slices.size();
    std::printf ("  take: %.2f s, %d pads\n", take->length() / rate, pads);
    CHECK (std::abs (take->length() / rate - 2.0) < 0.02, "a 2 s take: %.3f", take->length() / rate);
    CHECK (pads >= 7 && pads <= 9, "eight hits, eight pads: %d", pads);
    // its pads on the keys: the radio silent (LEVEL 0), C1 + 2 plays the third pad
    proc->setSoundParam (snd::rdLevel, 0.0f);
    play (0.3);
    const double quiet = play (0.2);
    const double pad = play (0.2, radio::firstPadNote + 2);
    std::printf ("  pads: %.4f rms quiet, %.4f with pad 3\n", quiet, pad);
    CHECK (quiet < 1.0e-4 && pad > 0.01, "pad 3 on D1: %.5f -> %.4f", quiet, pad);
    // ON AIR off: the radio silent, the pads still played
    proc->setSoundParam (snd::rdLevel, 0.8f);
    proc->setSoundParam (snd::rdOn, 0.0f);
    play (0.2);
    const double offAir = play (0.2);
    const double padOff = play (0.2, radio::firstPadNote + 2);
    std::printf ("  off air: %.5f rms, pad 3 %.4f\n", offAir, padOff);
    CHECK (offAir < 1.0e-4 && padOff > 0.01 && bank.use.load() == 0, "off air: %.5f, pad %.4f, receivers %d", offAir, padOff, bank.use.load());
    proc->setSoundParam (snd::rdOn, 1.0f);
    // the capture page over the radio: quiet (nothing to loop back through DIRECT), the receiver kept
    proc->captureOpen = true;
    play (0.2);
    const double underCapture = play (0.2);
    CHECK (underCapture < 1.0e-4 && bank.use.load() == 1, "under the capture page: %.5f rms, receivers %d", underCapture, bank.use.load());
    proc->captureOpen = false;
    // the take into the SAMPLER: its instrument, the sampler playing it
    CHECK (proc->radioToSampler (-1, why), "to sampler: %s", why.toRawUTF8());
    CHECK ((int) proc->player.get (snd::engine) == snd::sampler, "the sampler shown");
    CHECK (waitFor ([&] { auto ins = proc->getInstrument(); return ins != nullptr && ! ins->samples.empty() && ins->name.startsWith (juce::String (take->name)); }, 20.0),
           "the take's instrument");
    if (auto ins = proc->getInstrument(); ins != nullptr && ! ins->samples.empty())
        CHECK (std::abs (ins->samples[0].length() - take->length()) < 4 && ins->samples[0].loop.valid(), "its sample: %d frames (take %d), looped",
               ins->samples[0].length(), take->length());
    // one pad
    CHECK (proc->radioToSampler (3, why), "pad 4 to sampler: %s", why.toRawUTF8());
    CHECK (waitFor ([&] { auto ins = proc->getInstrument(); return ins != nullptr && ins->name.endsWith ("pad 4"); }, 20.0), "pad 4's instrument");
    // the take onto DRUM track 4: its slices, a trig on each step, the drum shown
    CHECK (proc->radioToDrum (-1, 3, why), "to drum: %s", why.toRawUTF8());
    CHECK ((int) proc->player.get (snd::engine) == snd::drum, "the drum shown");
    if (proc->drum.allocated())
    {
        const auto& tk = proc->drum.pattern (proc->drum.playingPattern()).tr[3];
        int trigs = 0;
        for (int st = 0; st < drum::numSteps; ++st)
            trigs += tk.trig[st];
        const auto ds = proc->drumSound (3);
        CHECK (ds != nullptr && (int) ds->slices.size() == pads && trigs == pads && (int) tk.len == pads
               && (int) std::lround (tk.p[drum::pSource]) == drum::slices,
               "track 4: %d slices, %d trigs, length %d", ds != nullptr ? (int) ds->slices.size() : -1, trigs, (int) tk.len);
    }
    // one pad onto track 5
    CHECK (proc->radioToDrum (2, 4, why), "pad 3 to drum: %s", why.toRawUTF8());
    CHECK (proc->drumSound (4) != nullptr && proc->drumSound (4)->length() == take->slices[2].end - take->slices[2].start, "track 5: pad 3");
    // the project: a receiver tuned, the take; read back by another
    RadioNet::Source s;
    s.kind = 1;
    s.name = "Test station";
    s.url = "http://127.0.0.1:9/stream.mp3";
    proc->radioNet().setSource (2, s);
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    const int takeLen = take->length();
    proc.reset();
    auto again = std::make_unique<ReplikantProcessor>();
    again->setRateAndBufferSizeDetails (rate, block);
    again->prepareToPlay (rate, block);
    again->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (waitFor ([&] { return again->radioTake() != nullptr; }, 10.0), "the take read back");
    if (auto t2 = again->radioTake())
        CHECK (std::abs (t2->length() - takeLen) < 2 && ! t2->slices.empty(), "the take back: %d frames, %zu pads", t2->length(), t2->slices.size());
    CHECK (again->radioNet().getSource (2).name == "Test station", "RX 3 tuned as it was");
    CHECK (waitFor ([&] { return again->drumSound (3) != nullptr; }, 10.0), "track 4's take back");
    if (auto d3 = again->drumSound (3))
        CHECK ((int) d3->slices.size() == pads, "its slices back: %zu", d3->slices.size());
}


// DRUM as a host plays it: its sequencer on the host's transport (a kick on the quarters), a track muted, a sound
// dropped on a track, the project read back (the steps, a p-lock, a ratchet, the sound)
struct TestHead : juce::AudioPlayHead
{
    bool playing = true;
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        return p;
    }
};

static void drumAsAHost()
{
    const double rate = 48000.0;
    const int block = 480;
    TestHead head;
    auto proc = std::make_unique<ReplikantProcessor>();
    proc->setPlayHead (&head);
    proc->setRateAndBufferSizeDetails (rate, block);
    proc->prepareToPlay (rate, block);
    proc->setSoundParam (snd::engine, (float) snd::drum);
    proc->drumReady();
    CHECK (proc->drum.allocated(), "the drum made");
    auto& pat = proc->drum.pattern (0);
    for (int s : { 0, 4, 8, 12 })
        pat.tr[0].trig[s] = 1;
    const int chans = juce::jmax (proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (chans, block);
    std::vector<float> out;
    bool finite = true;
    auto play = [&] (double seconds)
    {
        out.clear();
        for (int pos = 0; pos < (int) (seconds * rate); pos += block)
        {
            buf.clear();
            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);
            if (head.playing)
                head.ppq += block / rate * head.bpm / 60.0;
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (0, i);
                finite = finite && std::isfinite (v);
                out.push_back (v);
            }
        }
    };
    auto rms = [&] (int from, int len)
    {
        double e = 0.0;
        for (int i = from; i < from + len && i < (int) out.size(); ++i)
            e += (double) out[(size_t) i] * out[(size_t) i];
        return std::sqrt (e / juce::jmax (1, len));
    };
    // a bar at 120: the kick on each quarter (every 24000 frames), quiet just before each
    play (2.0);
    int onBeats = 0;
    for (int q = 0; q < 4; ++q)
    {
        const int at = q * 24000;
        const double before = q == 0 ? 0.0 : rms (at - 1200, 1000), after = rms (at, 1200);
        onBeats += after > 0.02 && after > 4.0 * before ? 1 : 0;
    }
    std::printf ("  drum on the host's transport: %d of 4 quarters hit, first %.3f rms\n", onBeats, rms (0, 1200));
    CHECK (finite && onBeats == 4, "the kick on the four quarters: %d", onBeats);
    // WTF?!: at 0 the four kicks alike; at its end each one pulled its own way (its tune, decay, sweep...)
    auto spread = [&]
    {
        double lo = 1e9, hi = 0.0;
        for (int q = 0; q < 4; ++q)
        {
            const double h = rms (q * 24000, 4800);
            lo = std::min (lo, h);
            hi = std::max (hi, h);
        }
        return hi / std::max (1.0e-9, lo);
    };
    head.ppq = 0.0;
    play (2.0);
    const double still = spread();
    proc->setSoundParam (snd::wtf, 1.0f);
    head.ppq = 0.0;
    play (0.2);
    head.ppq = 0.0;
    play (2.0);
    const double pulled = spread();
    proc->setSoundParam (snd::wtf, 0.0f);
    play (0.5);
    std::printf ("  drum WTF?!: the four kicks within x%.3f at 0, x%.2f at its end\n", still, pulled);
    CHECK (still < 1.02 && pulled > 1.15, "WTF on the drum: x%.3f -> x%.2f", still, pulled);
    // the host stopped: silence once the last hit has died away
    head.playing = false;
    play (3.0);
    const double stopped = rms (96000, 48000);
    CHECK (stopped < 1.0e-4, "stopped, silent: %.5f", stopped);
    // a track muted: nothing from it
    head.playing = true;
    head.ppq = 0.0;
    proc->drum.mute[0] = true;
    play (1.0);
    CHECK (rms (0, 48000) < 1.0e-4, "track 1 muted: %.5f", rms (0, 48000));
    proc->drum.mute[0] = false;
    // a sound dropped on track 2: a short burst at 1 kHz, played on step 3
    const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("replik8-drum-test.wav");
    {
        wav.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os (wav.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), 48000.0, 1, 24, {}, 0));
        if (w != nullptr)
            os.release();
        juce::AudioBuffer<float> b (1, 9600);
        for (int i = 0; i < 9600; ++i)
            b.setSample (0, i, 0.5f * std::exp (-(float) i / 3000.0f) * (float) std::sin (6.283185307 * 1000.0 * i / 48000.0));
        if (w != nullptr)
            w->writeFromAudioSampleBuffer (b, 0, 9600);
    }
    juce::String why;
    CHECK (proc->drumLoadFile (1, wav, why), "the sound on track 2: %s", why.toRawUTF8());
    CHECK (proc->drumSound (1) != nullptr && (int) std::lround (pat.tr[1].p[drum::pSource]) == drum::sample, "track 2 plays a sample");
    for (int s : { 0, 4, 8, 12 })
        pat.tr[0].trig[s] = 0;
    pat.tr[1].trig[2] = 1;
    pat.tr[1].rat[10] = 3;
    pat.tr[1].trig[10] = 1;
    pat.tr[1].lock[2][drum::pTune] = 12.0f;
    pat.chg = 32;
    proc->drumTrack = 1;
    head.ppq = 0.0;
    play (1.0);
    const double sample = rms (12000, 4800), beforeIt = rms (6000, 4800);
    std::printf ("  the sample on step 3: %.4f rms (before %.5f)\n", sample, beforeIt);
    CHECK (sample > 0.02 && beforeIt < 1.0e-4, "the sample on step 3: %.4f, before %.5f", sample, beforeIt);
    // the project, read back
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    proc.reset();
    auto again = std::make_unique<ReplikantProcessor>();
    again->setRateAndBufferSizeDetails (rate, block);
    again->prepareToPlay (rate, block);
    again->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (again->drum.allocated(), "the drum back");
    if (again->drum.allocated())
    {
        const auto& p2 = again->drum.pattern (0);
        CHECK (p2.tr[1].trig[2] == 1 && p2.tr[1].trig[10] == 1 && p2.tr[1].rat[10] == 3 && p2.chg == 32, "its steps back (ratchet %d, change %d)", p2.tr[1].rat[10], p2.chg);
        CHECK (std::abs (p2.tr[1].lock[2][drum::pTune] - 12.0f) < 1.0e-4f && std::isnan (p2.tr[1].lock[3][drum::pTune]), "its p-lock back");
        CHECK (waitFor ([&] { return again->drumSound (1) != nullptr; }, 10.0), "its sound back");
        if (auto s2 = again->drumSound (1))
            CHECK (std::abs (s2->length() - 9600) < 2, "the sound back: %d frames", s2->length());
    }
    wav.deleteFile();
}

// (0.4.20) WEAVE through the processor: three sounds dropped (each a tone of its own, a bar long), read in the
// background, heard with the transport (the beat's on the beat), silent when it stops; the project keeps them
static void weaveAsAHost()
{
    const double rate = 48000.0;
    const int block = 480;
    TestHead head;
    auto proc = std::make_unique<ReplikantProcessor>();
    proc->setPlayHead (&head);
    proc->setRateAndBufferSizeDetails (rate, block);
    proc->prepareToPlay (rate, block);
    proc->setSoundParam (snd::engine, (float) snd::weave);
    proc->setSoundParam (snd::fModel, 0.0f);
    proc->setSoundParam (snd::rvOn, 0.0f);
    proc->setSoundParam (snd::chOn, 0.0f);
    proc->setSoundParam (snd::wvMove, 0.0f);       // (the beat's sound held through)
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("replik8-weave-test");
    dir.deleteRecursively();
    dir.createDirectory();
    juce::StringArray files;
    for (int k = 0; k < 3; ++k)
    {
        const auto f = dir.getChildFile ("tone" + juce::String (k) + ".wav");
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), 48000.0, 1, 24, {}, 0));
        if (w != nullptr)
            os.release();
        juce::AudioBuffer<float> b (1, 96000);
        for (int i = 0; i < 96000; ++i)
            b.setSample (0, i, 0.3f * (float) std::sin (6.283185307 * (300.0 + 200.0 * k) * i / 48000.0));
        if (w != nullptr)
            w->writeFromAudioSampleBuffer (b, 0, 96000);
        files.add (f.getFullPathName());
    }
    proc->weaveAdd (files);
    CHECK (waitFor ([&] { return proc->weaveCount() == 3; }, 10.0), "three sounds woven (%d)", proc->weaveCount());
    const int chans = juce::jmax (proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (chans, block);
    std::vector<float> out;
    auto play = [&] (double seconds)
    {
        out.clear();
        for (int pos = 0; pos < (int) (seconds * rate); pos += block)
        {
            buf.clear();
            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);
            if (head.playing)
                head.ppq += block / rate * head.bpm / 60.0;
            for (int i = 0; i < block; ++i)
                out.push_back (buf.getSample (0, i));
        }
    };
    auto pitch = [&] (int from, int len)
    {
        int ups = 0;
        for (int i = from + 1; i < from + len; ++i)
            ups += out[(size_t) i - 1] < 0.0f && out[(size_t) i] >= 0.0f;
        return ups * rate / len;
    };
    head.playing = true;
    head.ppq = 0.0;
    play (2.0);
    const double hz = pitch (4800, 38400);
    CHECK (std::abs (hz - 300.0) < 8.0, "BEAT's sound (the first) heard: %.1f Hz", hz);
    proc->setSoundParam (snd::wvBeat, 3.0f);
    play (1.0);
    const double hz3 = pitch (9600, 28800);
    CHECK (std::abs (hz3 - 700.0) < 10.0, "BEAT on the third: %.1f Hz", hz3);
    head.playing = false;
    play (1.0);
    double tail = 0.0;
    for (int i = 24000; i < 48000; ++i)
        tail = juce::jmax (tail, (double) std::abs (out[(size_t) i]));
    CHECK (tail < 1.0e-4, "the transport stopped: silent (%.5f)", tail);
    // the project, read back: its sounds in their order
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    proc.reset();
    auto again = std::make_unique<ReplikantProcessor>();
    again->setRateAndBufferSizeDetails (rate, block);
    again->prepareToPlay (rate, block);
    again->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (waitFor ([&] { return again->weaveCount() == 3; }, 10.0), "its sounds back (%d)", again->weaveCount());
    CHECK (again->weaveName (2) == "tone2" && (int) again->player.get (snd::engine) == snd::weave, "in their order, WEAVE playing");
    again->weaveRemove (0);
    CHECK (again->weaveCount() == 2 && again->weaveName (0) == "tone1", "one taken away");
    dir.deleteRecursively();
}

// (0.4.24) a resample sent to the DRUM: a bar at 120 with hits on known sixteenths, cut at its attacks onto a free
// track, each slice on the step it fell on; then to WEAVE
static void resampleElsewhere()
{
    const double rate = 48000.0;
    auto proc = std::make_unique<ReplikantProcessor>();
    proc->setRateAndBufferSizeDetails (rate, 480);
    proc->prepareToPlay (rate, 480);
    proc->setSoundParam (snd::tempo, 120.0f);
    proc->resampleOptions.length = 0;               // one bar
    proc->resampleOptions.dest = ReplikantProcessor::ResampleOptions::toDrum;
    proc->resampleOptions.sens = 0.5f;
    const int n = 96000, step = n / 16;
    const int hits[] = { 0, 4, 6, 10, 12, 14 };
    rk::Channels take (2, std::vector<float> ((size_t) n, 0.0f));
    juce::Random rnd (5);
    for (int h : hits)
        for (int i = 0; i < 2400; ++i)
        {
            const float v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-(float) i / 300.0f);
            take[0][(size_t) (h * step + 30 + i)] = v;
            take[1][(size_t) (h * step + 30 + i)] = v;
        }
    proc->injectResample (take);
    const int t = proc->drumTrack.load();
    const auto snd = proc->drumSound (t);
    CHECK ((int) proc->player.get (snd::engine) == snd::drum, "the DRUM playing");
    CHECK (snd != nullptr && snd->slices.size() == 6, "six slices (%d)", snd != nullptr ? (int) snd->slices.size() : -1);
    const auto& tk = proc->drum.pattern (proc->drum.playingPattern()).tr[t];
    juce::String got;
    bool right = true;
    int k = 0;
    for (int s = 0; s < 16; ++s)
        if (tk.trig[s] != 0)
        {
            got << s << " ";
            right = right && k < 6 && s == hits[k] && (int) tk.lock[s][drum::pSlice] == k;
            ++k;
        }
    CHECK (right && k == 6 && tk.len == 16, "each slice on its step, in order: %s", got.toRawUTF8());
    CHECK ((int) tk.p[drum::pSource] == drum::slices, "the track plays its slices");
    // to WEAVE: one more sound
    proc->resampleOptions.dest = ReplikantProcessor::ResampleOptions::toWeave;
    proc->injectResample (take);
    CHECK (proc->weaveCount() == 1 && (int) proc->player.get (snd::engine) == snd::weave, "to WEAVE: its sound (%d)", proc->weaveCount());
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    // a home of its own: no settings from before
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("replik8-processor-tests");
    home.deleteRecursively();
    home.createDirectory();
   #if JUCE_MAC || JUCE_LINUX
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
   #endif
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 512, 1024 })
        {
            playAsAHost (rate, block);
            playAsALazyHost (rate, block);
        }
    playAsALazyHost (48000.0, 512, 2);
    playAsALazyHost (48000.0, 1024, 1, 0.9999f);
    playAsALazyHost (44100.0, 256, 3, 1.0001f);
    playAsALazyHost (48000.0, 512, 1, 0.97f, true);
    playAsALazyHost (44100.0, 256, 2, 1.0f, true);

    // MONITOR: OFF by default (an input that hears the speakers would howl), a capture included; a project from
    // before 0.3.6 with DIRECT (the old default) opens with OFF
    {
        ReplikantProcessor proc;
        CHECK (proc.options.monitor == 0, "monitor off by default: %d", proc.options.monitor);
        juce::MemoryBlock st;
        proc.options.monitor = 1;
        proc.getStateInformation (st);
        ReplikantProcessor again;
        again.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (again.options.monitor == 1, "DIRECT chosen now is kept: %d", again.options.monitor);
        auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        xml->setAttribute ("version", "0.3.4");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ReplikantProcessor before;
        before.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (before.options.monitor == 0, "a 0.3.4 project's DIRECT back to OFF: %d", before.options.monitor);
    }

    // (0.4.7) LIMIT's look-ahead told to the host
    {
        ReplikantProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        CHECK (proc.getLatencySamples() == proc.limiter.latency() && proc.getLatencySamples() > 0, "the latency told: %d frames", proc.getLatencySamples());
    }

    // (0.4.8) the assistant's test: three notes to the test synth, all heard, a steady latency; the CC test; the
    // host's MIDI out carrying the notes when nothing comes back; no MIDI out at all, no test
    {
        using Probe = ReplikantProcessor::Probe;
        ReplikantProcessor proc;
        proc.options.source = ReplikantProcessor::Source::testSynth;
        proc.settingsChanged();
        proc.setRateAndBufferSizeDetails (48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels()), 256);
        int notesOut = 0;
        auto runUntilDone = [&]
        {
            for (int b = 0; b < 3000 && proc.probing(); ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
                for (const auto m : midi)
                    if (m.getMessage().isNoteOn())
                        ++notesOut;
            }
            for (int b = 0; b < 4; ++b)     // (what it found, handed over)
            {
                buf.clear();
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
            }
        };
        juce::String why;
        CHECK (proc.startProbe (false, why), "the test starts on the test synth: %s", why.toRawUTF8());
        runUntilDone();
        auto p = proc.probe();
        CHECK (p.state == Probe::done, "the test ends: %d", (int) p.state);
        CHECK (p.played == 3 && p.heard == 3, "three notes played, three heard: %d / %d", p.heard, p.played);
        float lo = 1.0e9f, hi = -1.0f;
        for (int i = 0; i < p.heard; ++i)
        {
            lo = juce::jmin (lo, p.latencyMs[i]);
            hi = juce::jmax (hi, p.latencyMs[i]);
        }
        CHECK (lo >= 0.0f && hi < 60.0f && hi - lo < 1.0f, "a steady latency: %.2f .. %.2f ms", lo, hi);
        CHECK (p.peakDb > -40.0f && p.peakDb < 0.0f && ! p.clipped && ! p.echoed, "a sound in range: %.1f dB", p.peakDb);
        CHECK (proc.startProbe (true, why), "the CC test starts: %s", why.toRawUTF8());
        runUntilDone();
        p = proc.probe();
        CHECK (p.state == Probe::done && p.ccTest && p.played == 1 && p.heard == 1, "the CC test: the note held, heard (%d)", p.heard);
        CHECK (std::isfinite (p.ccDb) && std::isfinite (p.ccBright) && p.ccMoved, "CC 74 moves the test synth: %.2f dB, %.2f", p.ccDb, p.ccBright);
        // a CC the synth doesn't answer: its own envelope going on isn't taken for it
        proc.cycleOptions.cc = 20;
        CHECK (proc.startProbe (true, why), "the CC test starts: %s", why.toRawUTF8());
        runUntilDone();
        p = proc.probe();
        CHECK (p.state == Probe::done && ! p.ccMoved, "CC 20 moves nothing: %.2f dB, %.2f", p.ccDb, p.ccBright);
        proc.cycleOptions.cc = 74;
        // nothing coming back: the notes go out to the host all the same, none heard
        proc.options.source = ReplikantProcessor::Source::input;
        proc.setMidiPort ("host", "DAW");
        notesOut = 0;
        CHECK (proc.startProbe (false, why), "the test starts on the host's MIDI out: %s", why.toRawUTF8());
        runUntilDone();
        p = proc.probe();
        CHECK (p.state == Probe::done && p.heard == 0 && notesOut == 3, "silence: %d heard, %d notes sent to the host", p.heard, notesOut);
        proc.setMidiPort ({}, {});
        CHECK (! proc.startProbe (false, why) && why.isNotEmpty(), "no MIDI out: no test");
        auto h = proc.hostInfo();
        CHECK (h.format.isNotEmpty() && h.host == ReplikantProcessor::Host::other, "the host: %s %s", h.hostName.toRawUTF8(), h.format.toRawUTF8());
    }

    // (0.4.8) a low note coming in, passed straight through (MONITOR DIRECT, the capture page open), isn't taken for
    // our own output handed back: 64 samples of it look like any other 64 samples of it
    for (int test = 0; test < 6; ++test)
    {
        const int block = test % 3 == 0 ? 64 : (test % 3 == 1 ? 256 : 1024);
        const bool dc = test >= 3;            // (or a synth's own tiny DC, left when it's silent)
        ReplikantProcessor proc;
        proc.options.source = ReplikantProcessor::Source::input;
        proc.options.monitor = 1;
        proc.settingsChanged();
        proc.captureOpen = true;
        proc.setRateAndBufferSizeDetails (48000.0, block);
        proc.prepareToPlay (48000.0, block);
        juce::AudioBuffer<float> buf (juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels()), block);
        double ph = 0.0;
        bool echo = false;
        float outPeak = 0.0f;
        for (int pos = 0; pos < 96000; pos += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const float x = dc ? 0.001f
                                   : 0.5f * (float) (2.0 * ph - 1.0) * 0.6f + 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * ph);   // C1, a soft saw
                ph = std::fmod (ph + 32.7 / 48000.0, 1.0);
                for (int c = 0; c < buf.getNumChannels(); ++c)
                    buf.setSample (c, i, c < 2 ? x : 0.0f);
            }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            echo = echo || proc.heard().echoed;
            if (pos > 48000)
                outPeak = juce::jmax (outPeak, buf.getMagnitude (0, 0, block));
        }
        CHECK (! echo && outPeak > (dc ? 0.0005f : 0.2f), "%s passed through, block %d: no loop seen (%d), heard (%.4f)", dc ? "a DC" : "a low note", block, (int) echo, outPeak);
    }

    // (0.4.5) a project from before: its six stages get ECHO before ESPACE; its ENV and FOLLOW stay one-way (+);
    // WTF?!'s seed comes back
    {
        ReplikantProcessor proc;
        auto set = [&proc] (int id, float v) { if (auto* q = proc.params.getParameter (snd::def (id).id)) q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        set (snd::m2Type, 4.0f);         // FOLLOW
        set (snd::m2Polarity, 0.0f);
        proc.player.setFxOrder ({ 0, 5, 1, 2, 3, 4, 6 });
        proc.player.wtfSeed = 12345u;
        proc.player.setRoute (0, snd::drumDest (2, drum::pCutoff), 0.5f);
        proc.player.setRoute (1, snd::rdLen, -0.3f);
        juce::MemoryBlock st;
        proc.getStateInformation (st);
        ReplikantProcessor same;
        same.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (same.player.fxOrder() == proc.player.fxOrder(), "the chain's order comes back");
        CHECK (same.player.wtfSeed.load() == 12345u, "WTF?!'s seed comes back: %u", same.player.wtfSeed.load());
        CHECK (same.player.get (snd::m2Polarity) < 0.5f, "a FOLLOW saved in ± stays ±");
        CHECK (std::abs (same.player.routeAmount (0, snd::drumDest (2, drum::pCutoff)) - 0.5f) < 1.0e-6f, "a route to DRUM 3's cutoff comes back");
        CHECK (std::abs (same.player.routeAmount (1, snd::rdLen) + 0.3f) < 1.0e-6f, "a route to a stepped setting (RADIO's LENGTH) comes back");
        // as a 0.4.4 project had them: six stages, no polarity mark
        auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        xml->setAttribute ("fxOrder", "[\"drive\", \"espace\", \"spectre\", \"pitch\", \"chorus\", \"crush\"]");
        auto mods = juce::JSON::parse (xml->getStringAttribute ("routes"));
        for (int m = 0; m < mods.size(); ++m)
            if (auto* o = mods[m].getDynamicObject())
                o->removeProperty ("polarity");
        xml->setAttribute ("routes", juce::JSON::toString (mods, true));
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ReplikantProcessor before;
        before.setStateInformation (old.getData(), (int) old.getSize());
        const auto o = before.player.fxOrder();
        CHECK (o[0] == 0 && o[1] == 6 && o[2] == 5 && o[3] == 1 && o[6] == 4, "ECHO in before ESPACE: %d %d %d %d %d %d %d", o[0], o[1], o[2], o[3], o[4], o[5], o[6]);
        CHECK (before.player.get (snd::m2Polarity) > 0.5f, "an old FOLLOW stays one-way");
    }

    // the capture page open, a real input: ÉCOUTE DIRECT lets it through (and closed, it doesn't)
    for (bool open : { true, false })
    {
        ReplikantProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        proc.captureOpen = open;
        proc.options.monitor = 1;                  // DIRECT (OFF is the default)
        proc.settingsChanged();
        juce::AudioBuffer<float> buf (juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels()), 512);
        double phase = 0.0, energy = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i, phase += 440.0 / 48000.0)
                for (int ch = 0; ch < juce::jmin (2, proc.getTotalNumInputChannels()); ++ch)
                    buf.setSample (ch, i, 0.3f * (float) std::sin (6.283185307 * phase));
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            if (b >= 20)
                for (int i = 0; i < 512; ++i)
                    energy += (double) buf.getSample (0, i) * buf.getSample (0, i);
        }
        const double rms = std::sqrt (energy / (20.0 * 512.0));
        std::printf ("  the input with the capture page %s: %.3f rms out\n", open ? "open" : "closed", rms);
        CHECK (open ? rms > 0.15 : rms < 1.0e-4, "the input heard only with the capture page open (%s: %.4f rms)", open ? "open" : "closed", rms);
    }
    radioAsAHost();
    drumAsAHost();
    weaveAsAHost();
    resampleElsewhere();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
