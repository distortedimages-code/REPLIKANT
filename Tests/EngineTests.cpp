// A whole session on the test synth, faster than real time: the recorder in a fake audio thread, the worker,
// the instrument (zones, loops, tuning, rendered keys), playing it, saving, loading, exporting.

#include <JuceHeader.h>
#include "../Source/engine/Instrument.h"
#include "../Source/engine/Recorder.h"
#include "../Source/engine/Worker.h"
#include "../Source/engine/Player.h"
#include "../Source/engine/TestSynth.h"
#include "../Source/core/Wav.h"

#include <cstdio>
#include <mutex>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

namespace
{
    struct SynthSink : Recorder::MidiSink
    {
        TestSynth& synth;
        int notes = 0;
        explicit SynthSink (TestSynth& s) : synth (s) {}
        void send (const juce::MidiMessage& m) override
        {
            if (m.isNoteOn()) ++notes;
            synth.handle (m);
        }
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0;
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("replikant-engine-test");
    home.deleteRecursively();
    home.createDirectory();

    Recorder rec;
    Worker worker (rec);
    TestSynth synth;
    synth.prepare (sr);

    std::mutex lock;
    std::shared_ptr<Instrument> got;
    int versions = 0;
    worker.onInstrument = [&] (std::shared_ptr<Instrument> ins)
    {
        std::lock_guard<std::mutex> l (lock);
        got = ins;
        ++versions;
    };

    rk::Session s;
    s.lowNote = 48;
    s.highNote = 60;
    s.interval = 6;          // 48, 54, 60
    s.layers = 2;
    s.hold = 1.2;
    s.releaseMax = 1.0;
    s.pause = 0.1;
    s.extend = 5;
    Recorder::Plan plan;
    plan.jobs = rk::planJobs (s);
    plan.hold = s.hold;
    plan.releaseMax = s.releaseMax;
    plan.pause = s.pause;
    CHECK (plan.jobs.size() == 6, "%zu jobs", plan.jobs.size());

    const auto folder = Worker::uniqueFolder (home, "Test synth");
    CHECK (worker.startSession ("Test synth", s, plan, sr, 2, folder), "start");

    // the fake audio thread: 256-sample blocks, about ten times faster than real time
    SynthSink sink (synth);
    juce::AudioBuffer<float> buf (2, 256);
    int blocks = 0;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (rec.isRunning() && blocks < 20000)
    {
        rec.beginBlock (sink);
        synth.render (buf.getArrayOfWritePointers(), 2, 256);
        const float* in[2] = { buf.getReadPointer (0), buf.getReadPointer (1) };
        rec.capture (in, 2, 256);
        if (++blocks % 20 == 0)
            juce::Thread::sleep (1);
    }
    CHECK (! rec.isRunning(), "the session didn't end");
    CHECK (sink.notes == 6, "%d notes played", sink.notes);
    std::printf ("  session: %.1f s of audio in %.2f s\n", blocks * 256 / sr, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);

    // the worker builds, saves and renders
    std::shared_ptr<Instrument> ins;
    for (int i = 0; i < 1200; ++i)
    {
        juce::Thread::sleep (50);
        std::lock_guard<std::mutex> l (lock);
        if (got != nullptr && ! got->keys.empty())
        {
            ins = got;
            break;
        }
    }
    CHECK (ins != nullptr, "no rendered instrument");
    if (ins == nullptr)
    {
        std::printf ("status: %s / %s\n", worker.getStatus().text.toRawUTF8(), worker.getStatus().warning.toRawUTF8());
        return 1;
    }
    const auto st = worker.getStatus();
    std::printf ("  status: %s  latency %d  floor %.1f dB  warning '%s'\n", st.text.toRawUTF8(), st.latency, st.floorDb, st.warning.toRawUTF8());
    CHECK (ins->samples.size() == 6, "%zu samples", ins->samples.size());
    CHECK (std::abs (st.latency - 168) < 12, "latency %d", st.latency);
    CHECK (st.floorDb < -80.0f && st.floorDb > -95.0f, "floor %.1f", st.floorDb);
    for (auto& x : ins->samples)
    {
        CHECK (x.loop.valid() && x.loop.quality > 0.7f, "%s v%d loop q %.2f", rk::noteName (x.root).c_str(), x.velocity, x.loop.quality);
        // the test synth: oscillators at +7 and +14.8 cents (and a drift of 3): heard somewhere in between
        CHECK (x.pitchConfident && x.detuneCents > 5.0f && x.detuneCents < 16.0f, "%s detune %.2f", rk::noteName (x.root).c_str(), x.detuneCents);
        std::printf ("  %s v%3d: %.2f s, loop %.2f s q %.3f, %+.1f cents, keys %s..%s, vel %d..%d\n", rk::noteName (x.root).c_str(), x.velocity,
                     x.seconds(), x.loop.length() / sr, x.loop.quality, x.detuneCents, rk::noteName (x.keyLo).c_str(),
                     rk::noteName (x.keyHi).c_str(), x.velLo, x.velHi);
    }
    // the layers of one note agree on its tuning
    for (auto& x : ins->samples)
        for (auto& y : ins->samples)
            if (x.root == y.root)
                CHECK (std::abs (x.detuneCents - y.detuneCents) < 3.0f, "%s layers %.2f vs %.2f", rk::noteName (x.root).c_str(), x.detuneCents, y.detuneCents);
    int lo = 127, hi = 0;
    for (auto& x : ins->samples)
    {
        lo = std::min (lo, x.keyLo);
        hi = std::max (hi, x.keyHi);
    }
    CHECK (lo == 43 && hi == 65, "keys %d..%d", lo, hi);
    CHECK ((int) ins->keys.size() == ins->countKeysToRender(), "%zu keys rendered, %d to render", ins->keys.size(), ins->countKeysToRender());
    std::printf ("  %zu keys rendered, %.1f MB\n", ins->keys.size(), (double) ins->memoryBytes() / 1048576.0);

    // playing: a key between two recordings, in tune
    {
        Player player;
        player.prepare (sr, 512);
        player.setInstrument (ins.get());
        juce::AudioBuffer<float> out (2, 512);
        std::vector<float> heard;
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 51, (juce::uint8) 120), 0);
        for (int b = 0; b < 120; ++b)
        {
            out.clear();
            player.render (out, midi, {});
            midi.clear();
            for (int i = 0; i < 512; ++i)
                heard.push_back (out.getSample (0, i));
        }
        float peak = 0;
        for (float v : heard) peak = std::max (peak, std::abs (v));
        CHECK (peak > 0.1f, "peak %.3f", peak);
        const auto p = rk::detectPitch (heard, sr, rk::noteHz (51), 20000, (int) heard.size());
        CHECK (p.ok && std::abs (p.cents) < 3.0, "D#2 plays %.2f cents off (ok %d)", p.cents, (int) p.ok);
        std::printf ("  played D#2: %.2f cents off, peak %.2f\n", p.cents, peak);
    }

    // saved, and loaded back
    {
        CHECK (folder.getChildFile ("Test synth.replikant").existsAsFile(), "description file");
        CHECK (folder.getChildFile ("Samples").findChildFiles (juce::File::findFiles, false, "*.wav").size() == 6, "6 wav");
        juce::String err;
        auto back = instrumentio::load (folder, err);
        CHECK (back != nullptr, "load: %s", err.toRawUTF8());
        if (back != nullptr)
        {
            CHECK (back->samples.size() == 6, "loaded %zu", back->samples.size());
            for (size_t i = 0; i < back->samples.size() && i < ins->samples.size(); ++i)
            {
                CHECK (back->samples[i].loop.start == ins->samples[i].loop.start && back->samples[i].loop.end == ins->samples[i].loop.end, "loop %zu", i);
                CHECK (back->samples[i].keyLo == ins->samples[i].keyLo && back->samples[i].velHi == ins->samples[i].velHi, "zone %zu", i);
                CHECK (std::abs (back->samples[i].detuneCents - ins->samples[i].detuneCents) < 0.01f, "detune %zu", i);
            }
            CHECK (back->session.interval == 6 && back->session.layers == 2 && back->session.extend == 5, "session");
        }
    }

    // exports
    {
        const auto bw = home.getChildFile ("Test.multisample");
        auto r = instrumentio::exportBitwig (*ins, bw, false);
        CHECK (r.wasOk() && bw.existsAsFile(), "bitwig: %s", r.getErrorMessage().toRawUTF8());
        juce::ZipFile zip (bw);
        CHECK (zip.getNumEntries() == 7, "%d entries", zip.getNumEntries());
        const auto* xmlEntry = zip.getEntry ("multisample.xml");
        CHECK (xmlEntry != nullptr, "no multisample.xml");
        if (xmlEntry != nullptr)
        {
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*xmlEntry));
            auto xml = juce::parseXML (in->readEntireStreamAsString());
            CHECK (xml != nullptr && xml->getNumChildElements() >= 6 + 4, "xml");
            int samples = 0, loops = 0;
            if (xml != nullptr)
                for (auto* e : xml->getChildWithTagNameIterator ("sample"))
                {
                    ++samples;
                    if (auto* l = e->getChildByName ("loop"))
                        loops += l->getStringAttribute ("mode") == "loop";
                }
            CHECK (samples == 6 && loops == 6, "%d samples %d loops", samples, loops);
        }
        const auto wavs = home.getChildFile ("Test WAV keys");
        r = instrumentio::exportWavFolder (*ins, wavs, true);
        CHECK (r.wasOk(), "wav export: %s", r.getErrorMessage().toRawUTF8());
        const int files = wavs.findChildFiles (juce::File::findFiles, false, "*.wav").size();
        CHECK (files == (hi - lo + 1) * 2, "%d files", files);
        // a rendered key carries its root and loop
        rk::Channels ch;
        double rate;
        rk::WavInfo info;
        const auto any = wavs.findChildFiles (juce::File::findFiles, false, "* D#2 v127.wav");
        CHECK (any.size() == 1, "D#2 v127 file");
        if (any.size() == 1)
        {
            CHECK (rk::readWav (any[0].getFullPathName().toStdString(), ch, rate, info), "read");
            CHECK (info.root == 51 && info.loop.valid() && info.hasRange && info.keyLo == 51 && info.keyHi == 51, "root %d", info.root);
        }
    }

    // a pack to share: out with its sound, in at a friend's (another folder), the same instrument and sound
    {
        juce::var snd (new juce::DynamicObject());      // (held by the var: a raw pointer would be freed by the first var)
        snd.getDynamicObject()->setProperty ("params", juce::var (new juce::DynamicObject()));
        snd["params"].getDynamicObject()->setProperty ("cutoff", 777.0);
        const auto zip = home.getChildFile ("Packs").getChildFile ("Test synth.zip");
        auto r = instrumentio::exportPack (*ins, snd, zip);
        CHECK (r.wasOk() && zip.existsAsFile(), "pack: %s", r.getErrorMessage().toRawUTF8());
        juce::ZipFile z (zip);
        bool layout = z.getNumEntries() == 7;
        for (int i = 0; i < z.getNumEntries(); ++i)
            layout = layout && z.getEntry (i)->filename.startsWith ("Test synth/");
        CHECK (layout, "pack layout (%d entries)", z.getNumEntries());
        juce::String err;
        const auto friendRoot = home.getChildFile ("Chez un pote");
        const auto unpacked = instrumentio::importPack (zip, friendRoot, err);
        CHECK (unpacked.isDirectory() && unpacked.isAChildOf (friendRoot), "import: %s", err.toRawUTF8());
        auto back = instrumentio::load (unpacked, err);
        CHECK (back != nullptr && back->samples.size() == ins->samples.size(), "pack loads: %s", err.toRawUTF8());
        if (back != nullptr)
        {
            CHECK (std::abs ((double) back->sound["params"]["cutoff"] - 777.0) < 1.0e-9, "the sound came with it");
            bool same = true;
            for (size_t i = 0; i < back->samples.size(); ++i)
                same = same && back->samples[i].root == ins->samples[i].root && back->samples[i].loop.start == ins->samples[i].loop.start
                       && std::abs (back->samples[i].audio[0][1000] - ins->samples[i].audio[0][1000]) < 1.0e-6f;
            CHECK (same, "the same samples");
        }
        // twice: a second folder, not the first one overwritten
        const auto again = instrumentio::importPack (zip, friendRoot, err);
        CHECK (again.isDirectory() && again != unpacked, "imported twice: %s", again.getFileName().toRawUTF8());
        // a sound saved into the folder; then the instrument saved again keeps it
        r = instrumentio::saveSound (unpacked, snd);
        CHECK (r.wasOk(), "save sound");
        if (back != nullptr)
        {
            back->sound = juce::var();
            instrumentio::save (*back, unpacked);
            auto third = instrumentio::load (unpacked, err);
            CHECK (third != nullptr && std::abs ((double) third->sound["params"]["cutoff"] - 777.0) < 1.0e-9, "the sound survives a save");
        }
        CHECK (instrumentio::isImportable (zip) && instrumentio::isImportable (unpacked) && ! instrumentio::isImportable (home.getChildFile ("x.wav")),
               "what can be dropped");
    }

    // CAPTURE CYCLES: one low note held eight times while CC 74 opens the test synth's filter: eight cycles, darker
    // to brighter, kept with the instrument (a wavetable beside it) and read back
    {
        {
            std::lock_guard<std::mutex> l (lock);
            got = nullptr;
        }
        rk::Session cs;
        cs.loop = false;
        cs.normalise = rk::Normalise::off;
        Recorder::Plan cp;
        for (int i = 0; i < 8; ++i)
        {
            cp.jobs.push_back ({ 36, 100, 0, i });
            cp.ccValues.push_back (i * 127 / 7);
        }
        cp.ccNumber = 74;
        cp.hold = 0.5;
        cp.releaseMax = 0.25;
        cp.pause = 0.15;
        const auto cf = Worker::uniqueFolder (home, "Cycles test");
        CHECK (worker.startCycleSession ("Cycles test", cs, cp, sr, 2, cf, nullptr), "cycles: start");
        int cb = 0;
        while (rec.isRunning() && cb < 20000)
        {
            rec.beginBlock (sink);
            synth.render (buf.getArrayOfWritePointers(), 2, 256);
            const float* in[2] = { buf.getReadPointer (0), buf.getReadPointer (1) };
            rec.capture (in, 2, 256);
            if (++cb % 20 == 0)
                juce::Thread::sleep (1);
        }
        std::shared_ptr<Instrument> ci;
        for (int i = 0; i < 400 && ci == nullptr; ++i)
        {
            juce::Thread::sleep (25);
            std::lock_guard<std::mutex> l (lock);
            if (got != nullptr && got->cycles != nullptr)
                ci = got;
        }
        CHECK (ci != nullptr && ci->cycles->count() == 8, "cycles: %d (%s)", ci != nullptr ? ci->cycles->count() : -1,
               worker.getStatus().warning.toRawUTF8());
        if (ci != nullptr && ci->cycles->count() == 8)
        {
            // brighter as the CC goes up: more of the cycle's energy in its fast moves
            auto roughness = [] (const float* c)
            {
                double d = 0.0, e = 0.0;
                for (int i = 0; i < dsp::CycleBank::size; ++i)
                {
                    const double a = c[i], b = c[(i + 1) % dsp::CycleBank::size];
                    d += (b - a) * (b - a);
                    e += a * a;
                }
                return d / std::max (1.0e-12, e);
            };
            const double r0 = roughness (ci->cycles->cycle (0)), r7 = roughness (ci->cycles->cycle (7));
            CHECK (r7 > r0 * 4.0, "cycles brighter with the CC: %.2e -> %.2e", r0, r7);
            std::printf ("  cycles: 8, roughness %.2e -> %.2e\n", r0, r7);
            juce::String err;
            auto back = instrumentio::load (cf, err);
            CHECK (back != nullptr && back->cycles != nullptr && back->cycles->count() == 8, "cycles saved and read: %s", err.toRawUTF8());
            CHECK (cf.getChildFile ("Cycles").findChildFiles (juce::File::findFiles, false, "*.wav").size() == 1, "the wavetable beside it");
            if (back != nullptr && back->cycles != nullptr && back->cycles->count() == 8)
            {
                float worst = 0.0f;
                for (int i = 0; i < dsp::CycleBank::size; ++i)
                    worst = std::max (worst, std::abs (back->cycles->cycle (5)[i] - ci->cycles->cycle (5)[i]));
                CHECK (worst < 1.0e-5f, "the same cycles: off by %.2e", worst);
            }
        }
    }

    // a sound dropped: its note from its name, else from its pitch; read through JUCE when it isn't a WAV
    {
        auto write = [&] (const juce::File& f, double hz, bool aiff)
        {
            f.deleteFile();
            juce::AudioBuffer<float> b (2, (int) sr);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr);
                b.setSample (0, i, x);
                b.setSample (1, i, x);
            }
            std::unique_ptr<juce::AudioFormat> fmt;
            if (aiff) fmt = std::make_unique<juce::AiffAudioFormat>(); else fmt = std::make_unique<juce::WavAudioFormat>();
            std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w (fmt->createWriterFor (out.get(), sr, 2, 24, {}, 0));
            if (w != nullptr)
            {
                out.release();
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        };
        const auto named = home.getChildFile ("Basse A2.wav");
        const auto plain = home.getChildFile ("son.aiff");
        write (named, 110.0, false);
        write (plain, 330.0, true);
        CHECK (instrumentio::isImportable (named) && instrumentio::isImportable (plain), "sounds can be dropped");
        juce::String err;
        auto a = instrumentio::readSound (named, err);
        CHECK (a != nullptr && a->samples.size() == 1 && a->samples[0].root == 57 && a->samples[0].keyLo == 0 && a->samples[0].keyHi == 127
                   && ! a->session.renderKeys && a->name == "Basse A2", "named: %s", err.toRawUTF8());
        auto b = instrumentio::readSound (plain, err);
        CHECK (b != nullptr && b->samples[0].root == 64 && b->samples[0].pitchConfident && std::abs (b->samples[0].detuneCents - 2.0f) < 3.0f
                   && b->samples[0].length() == (int) sr && b->samples[0].channels() == 2,
               "pitch found: %d %.1f c", b != nullptr ? b->samples[0].root : -1, b != nullptr ? b->samples[0].detuneCents : 0.0f);
        if (b != nullptr)
        {
            // played: E3 on its key, an octave up on E4
            CHECK (std::abs (b->shiftFor (0, 64) + 0.02) < 0.03 && std::abs (b->shiftFor (0, 76) - 11.98) < 0.03, "shift %.3f", b->shiftFor (0, 76));
            const auto where = Worker::uniqueFolder (home, b->name);
            CHECK (instrumentio::save (*b, where).wasOk(), "saved");
            auto back = instrumentio::load (where, err);
            CHECK (back != nullptr && back->samples.size() == 1 && back->samples[0].root == 64 && back->samples[0].keyLo == 0
                       && back->samples[0].keyHi == 127 && ! back->session.renderKeys, "loaded back: %s", err.toRawUTF8());
        }
        CHECK (instrumentio::readSound (home.getChildFile ("nothing.wav"), err) == nullptr && err.isNotEmpty(), "missing");
    }

    std::printf ("%d checks, %d failed\n", checks, failures);
    worker.onInstrument = nullptr;
    return failures == 0 ? 0 : 1;
}
