// The player on made-up samples, measured: the anti-aliasing that follows the pitch, the ALIASING scale (continuous,
// up to aliases louder than the sound), the phase modulation (the read address moved by so many samples), reverse,
// elastic, moved loops, bits, MONO / LEGATO, the polyphony, the filters, the modulators, the FX mode, stability, CPU.

#include <JuceHeader.h>
#include "../Source/engine/Player.h"
#include "../Source/core/Analysis.h"
#include "../Source/core/Sinc.h"
#include "../Source/plugin/MidiClock.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <set>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

namespace
{
    constexpr double sr = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    // an instrument of one recording, root C3, answering every key
    std::unique_ptr<Instrument> instrumentOf (std::vector<float> mono, int loopStart = -1, int loopEnd = -1, bool stereo = false)
    {
        auto ins = std::make_unique<Instrument>();
        ins->name = "test";
        ins->sampleRate = sr;
        ins->session.tuning = rk::Tuning::off;
        rk::Sample s;
        s.root = 60;
        s.sent = 60;
        s.velocity = 127;
        s.keyLo = 0;
        s.keyHi = 127;
        s.velLo = 1;
        s.velHi = 127;
        s.sampleRate = sr;
        s.audio.push_back (mono);
        if (stereo)
            s.audio.push_back (mono);
        s.loop.start = loopStart;
        s.loop.end = loopEnd;
        ins->samples.push_back (std::move (s));
        ins->rebuildIndex();
        return ins;
    }

    std::vector<float> sine (double hz, double seconds, float level = 0.5f)
    {
        std::vector<float> x ((size_t) (seconds * sr));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = level * (float) std::sin (2.0 * pi * hz * (double) i / sr);
        return x;
    }

    struct Event { double at; int note, velocity; };   // velocity 0 = note off

    // plays the events, returns the left channel (and the right, if asked)
    std::vector<float> play (Player& p, double seconds, std::vector<Event> events, std::vector<float>* right = nullptr,
                             const std::vector<float>* input = nullptr, bool inputThroughFx = false, int block = 256)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, block);
        const int total = (int) (seconds * sr);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buf.setSize (2, n, false, false, true);
            buf.clear();
            juce::MidiBuffer midi;
            for (auto& e : events)
            {
                const int at = (int) (e.at * sr);
                if (at >= pos && at < pos + n)
                    midi.addEvent (e.velocity > 0 ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.velocity)
                                                  : juce::MidiMessage::noteOff (1, e.note), at - pos);
            }
            const float* in[2] = { nullptr, nullptr };
            if (input != nullptr)
                in[0] = in[1] = input->data() + pos;
            p.render (buf, midi, {}, input != nullptr ? in : nullptr, input != nullptr ? 2 : 0, inputThroughFx);
            for (int i = 0; i < n; ++i)
            {
                out.push_back (buf.getSample (0, i));
                if (right != nullptr)
                    right->push_back (buf.getSample (1, i));
            }
        }
        return out;
    }

    double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0.0;
        to = std::min (to, x.size());
        for (size_t i = from; i < to; ++i)
            s += (double) x[i] * x[i];
        return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
    }

    double db (double g)        { return 20.0 * std::log10 (std::max (1.0e-12, g)); }

    // the level of one frequency (a single-bin DFT over a Hann window)
    double toneLevel (const std::vector<float>& x, size_t from, size_t n, double hz)
    {
        double re = 0.0, im = 0.0, w = 0.0;
        for (size_t i = 0; i < n && from + i < x.size(); ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n);
            re += h * x[from + i] * std::cos (2.0 * pi * hz * (double) i / sr);
            im += h * x[from + i] * std::sin (2.0 * pi * hz * (double) i / sr);
            w += h;
        }
        return 2.0 * std::sqrt (re * re + im * im) / w;
    }

    // a plain player: instant attack, full sustain, no filter, nothing else
    void plain (Player& p)
    {
        p.prepare (sr, 512);
        p.safetyHighPass = false;
        p.set (snd::attack, 0.0f);
        p.set (snd::sustain, 1.0f);
        p.set (snd::release, 5.0f);
        p.set (snd::velocity, 0.0f);
    }
}

//==============================================================================
static void testAntiAliasing()
{
    // an 18 kHz tone played a fourth higher (24.0 kHz: over Nyquist) must vanish; a 1 kHz one must pass untouched
    auto hi = instrumentOf (sine (18000.0, 1.0));
    Player p;
    p.set (snd::engine, (float) snd::sampler);
    plain (p);
    p.setInstrument (hi.get());
    auto out = play (p, 0.5, { { 0.0, 65, 127 } });
    const double folded = toneLevel (out, 4800, 8192, 48000.0 - 18000.0 * std::pow (2.0, 5.0 / 12.0));
    CHECK (db (folded / 0.5) < -60.0, "a tone pushed over Nyquist folds back at %.1f dB", db (folded / 0.5));
    std::printf ("  18 kHz up a fourth: its alias at %.1f dB (ALIASING 0)\n", db (folded / 0.5));

    auto lo = instrumentOf (sine (1000.0, 1.0));
    p.setInstrument (lo.get());
    out = play (p, 0.5, { { 0.0, 65, 127 } });
    const double kept = toneLevel (out, 4800, 8192, 1000.0 * std::pow (2.0, 5.0 / 12.0));
    CHECK (std::abs (db (kept / 0.5)) < 0.1, "1 kHz up a fourth: %.2f dB", db (kept / 0.5));

    // down: the sinc stays at the source's band (no dulling of what is there)
    out = play (p, 0.5, { { 0.0, 48, 127 } });
    const double down = toneLevel (out, 4800, 8192, 500.0);
    CHECK (std::abs (db (down / 0.5)) < 0.1, "1 kHz down an octave: %.2f dB", db (down / 0.5));
}

static void testAliasingScale()
{
    // the alias of an 18 kHz tone played a fourth up, across ALIASING: it grows from nothing, without a jump, and
    // past 89 % it passes the sound itself
    auto ins = instrumentOf (sine (18000.0, 1.0));
    const double up = 18000.0 * std::pow (2.0, 5.0 / 12.0), alias = 48000.0 - up;
    std::vector<double> aliasDb, toneDb;
    for (int k = 0; k <= 100; k += 2)
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::alias, (float) k / 100.0f);
        auto out = play (p, 0.3, { { 0.0, 65, 127 } });
        aliasDb.push_back (db (toneLevel (out, 4800, 8192, alias) / 0.5));
        toneDb.push_back (db (toneLevel (out, 4800, 8192, up) / 0.5));
    }
    CHECK (aliasDb.front() < -60.0, "no alias at 0: %.1f dB", aliasDb.front());
    CHECK (aliasDb[25] > -12.0, "linear (50 %%) lets the alias through: %.1f dB", aliasDb[25]);
    // no step anywhere: on each side of where one way of reading hands over to the next, the same sound
    for (float b : { 0.25f, 0.36f, 0.51f, 0.89f })
    {
        std::vector<float> sides[2];
        for (int k = 0; k < 2; ++k)
        {
            Player p;
            p.set (snd::engine, (float) snd::sampler);
            plain (p);
            p.setInstrument (ins.get());
            p.set (snd::alias, b + (k == 0 ? -0.0005f : 0.0005f));
            sides[k] = play (p, 0.2, { { 0.0, 65, 127 } });
        }
        double diff = 0.0;
        for (size_t i = 0; i < sides[0].size(); ++i)
            diff = std::max (diff, (double) std::abs (sides[0][i] - sides[1][i]));
        CHECK (diff < 0.01, "ALIASING continuous at %.0f %%: the two sides differ by %.4f", b * 100.0f, diff);
    }
    std::printf ("  ALIASING 0 / 30 / 50 / 70 / 89 / 100 %%: alias %.0f / %.0f / %.0f / %.0f / %.0f / %.0f dB\n",
                 aliasDb[0], aliasDb[15], aliasDb[25], aliasDb[35], aliasDb[44], aliasDb[50]);

    // the extreme end: a tone below Nyquist, its image above the band (what drop sample makes) louder than itself
    auto mid = instrumentOf (sine (9000.0, 1.0));
    Player p;
    p.set (snd::engine, (float) snd::sampler);
    plain (p);
    p.setInstrument (mid.get());
    p.set (snd::alias, 1.0f);
    auto out = play (p, 0.3, { { 0.0, 67, 127 } });
    // the source's first image (at its rate minus the tone) moves with the playback speed, then folds into the band
    const double ratio = std::pow (2.0, 7.0 / 12.0), f = 9000.0 * ratio;
    double imageHz = std::fmod ((48000.0 - 9000.0) * ratio, 48000.0);
    if (imageHz > 24000.0)
        imageHz = 48000.0 - imageHz;
    const double tone = toneLevel (out, 4800, 8192, f), image = toneLevel (out, 4800, 8192, imageHz);
    CHECK (image > tone * 1.4, "at 100 %% the image (%.1f dB, %.0f Hz) is louder than the tone (%.1f dB)", db (image / 0.5), imageHz, db (tone / 0.5));
    std::printf ("  ALIASING 100 %%: the image at %.1f dB, the tone at %.1f dB\n", db (image / 0.5), db (tone / 0.5));
    // and at 0 the same image is gone
    Player q;
    q.set (snd::engine, (float) snd::sampler);
    plain (q);
    q.setInstrument (mid.get());
    auto clean = play (q, 0.3, { { 0.0, 67, 127 } });
    CHECK (db (toneLevel (clean, 4800, 8192, imageHz) / 0.5) < -60.0, "at 0 the image is gone (%.1f dB)", db (toneLevel (clean, 4800, 8192, imageHz) / 0.5));
}

static void testPhaseModulation()
{
    // a ramp sample: what plays tells where it is read; the phase modulation must move that by INDICE samples
    const int len = 96000;
    std::vector<float> ramp ((size_t) len);
    for (int i = 0; i < len; ++i)
        ramp[(size_t) i] = (float) i / (float) len;
    auto ins = instrumentOf (ramp);
    Player p;
    p.set (snd::engine, (float) snd::sampler);
    plain (p);
    p.setInstrument (ins.get());
    p.set (snd::pmSource, 1.0f);        // SINUS
    p.set (snd::pmIndex, 100.0f);
    p.set (snd::pmRatio, 1.0f);
    auto out = play (p, 0.5, { { 0.0, 60, 127 } });
    double maxOffset = 0.0, minOffset = 0.0;
    for (int i = 2000; i < 20000; ++i)
    {
        const double offset = (double) out[(size_t) i] * len - (double) i;    // read position minus where it would be
        maxOffset = std::max (maxOffset, offset);
        minOffset = std::min (minOffset, offset);
    }
    CHECK (std::abs (maxOffset - 100.0) < 1.0 && std::abs (minOffset + 100.0) < 1.0,
           "the read address moves %.2f .. %.2f samples (INDICE 100)", minOffset, maxOffset);
    std::printf ("  phase modulation: the read address moved %.2f .. %.2f samples\n", minOffset, maxOffset);

    // its rate: the sine sits on the note (C3, 261.6 Hz) times RAPPORT
    std::vector<float> offsets;
    for (int i = 2000; i < juce::jmin (2000 + 24000, (int) out.size()); ++i)      // (no further than it played)
        offsets.push_back ((float) ((double) out[(size_t) i] * len - (double) i));
    int first = -1, last = -1, crossings = 0;
    for (size_t i = 1; i < offsets.size(); ++i)
        if (offsets[i - 1] < 0.0f && offsets[i] >= 0.0f)
        {
            if (first < 0) first = (int) i; else ++crossings;
            last = (int) i;
        }
    const double hz = crossings > 0 ? sr * crossings / (double) (last - first) : 0.0;
    CHECK (std::abs (1200.0 * std::log2 (hz / rk::noteHz (60))) < 1.0, "the modulator at the note: %.2f Hz", hz);

    // from the audio input: the address follows it
    Player q;
    q.set (snd::engine, (float) snd::sampler);
    plain (q);
    q.setInstrument (ins.get());
    q.set (snd::pmSource, 3.0f);        // ENTRÉE
    q.set (snd::pmIndex, 50.0f);
    std::vector<float> in ((size_t) sr, 0.0f);
    for (size_t i = 24000; i < in.size(); ++i)
        in[i] = 1.0f;                     // half way: a step to +1
    out = play (q, 1.0, { { 0.0, 60, 127 } }, nullptr, &in, false);
    const double before = (double) out[20000] * len - 20000.0, after = (double) out[30000] * len - 30000.0;
    CHECK (std::abs (before) < 0.5 && std::abs (after - 50.0) < 0.5, "input PM: %.2f then %.2f samples", before, after);
}

static void testReverseElasticLoops()
{
    const int len = 96000;
    std::vector<float> ramp ((size_t) len);
    for (int i = 0; i < len; ++i)
        ramp[(size_t) i] = (float) i / (float) len;
    auto ins = instrumentOf (ramp);
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::reverse, 1.0f);
        auto out = play (p, 0.2, { { 0.0, 60, 127 } });
        CHECK (out[100] > 0.99f && out[5000] < out[100] && std::abs (out[5000] - (float) (len - 1 - 5000) / len) < 1.0e-4f,
               "reverse from the end: %.4f, %.4f", out[100], out[5000]);
    }
    {
        // elastic at half speed: the grains read around where the half-speed head is
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::playMode, 1.0f);
        p.set (snd::speed, 0.5f);
        p.set (snd::spray, 0.0f);
        p.set (snd::grain, 40.0f);
        auto out = play (p, 1.0, { { 0.0, 60, 127 } });
        // a grain of 40 ms reads ahead of its start: the mean position runs half a grain ahead of the head
        const double at = rms (out, 47000, 49000) * len, head = 0.5 * 48000.0;
        CHECK (std::abs (at - head) < 0.03 * sr, "elastic: reading at %.0f, head at %.0f", at, head);
        CHECK (std::abs ((double) p.shownPosition.load() - 0.5 * sr) < 600.0, "the head shown at %.0f", (double) p.shownPosition.load());
    }
    {
        // elastic keeps the pitch at any speed
        auto tone = instrumentOf (sine (440.0, 2.0));
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::playMode, 1.0f);
        p.set (snd::speed, 0.3f);
        p.set (snd::spray, 0.0f);
        auto out = play (p, 1.0, { { 0.0, 60, 127 } });
        const auto pitch = rk::detectPitch (out, sr, 440.0, 12000, 24000);
        CHECK (pitch.ok && std::abs (pitch.cents) < 3.0, "elastic at 0.3x: %.2f cents (ok %d)", pitch.cents, (int) pitch.ok);
        // the grains aligned: the level steady (no phasing between them)
        double lo = 1.0, hi = 0.0;
        for (size_t i = 12000; i + 480 < 44000; i += 480)
        {
            const double l = rms (out, i, i + 480);
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        CHECK (db (hi / lo) < 1.5, "elastic level steady: %.2f dB of ripple", db (hi / lo));
    }
    {
        // a loop made shorter and moved: its seam smoothed (no jump bigger than the sound's own steps)
        auto tone = instrumentOf (sine (220.0, 2.0), 24000, 24000 + 218 * 40);     // (218 samples a period, about)
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::loopSize, 0.137f);
        p.set (snd::loopStart, 0.4f);
        auto out = play (p, 2.0, { { 0.0, 60, 127 } });
        float worst = 0.0f;
        for (size_t i = 60000; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - out[i - 1]));
        const float natural = (float) (0.5 * 2.0 * pi * 220.0 / sr);
        CHECK (worst < 2.5f * natural, "moved loop: steps up to %.4f (the sine's own %.4f)", worst, natural);
        // and the sound keeps going (looped)
        CHECK (rms (out, 80000, 96000) > 0.3, "moved loop still sounds");
    }
    {
        // BITS 4: only a few levels
        auto tone = instrumentOf (sine (220.0, 1.0, 0.9f));
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::bits, 4.0f);
        auto out = play (p, 0.3, { { 0.0, 60, 127 } });
        std::set<float> levels (out.begin() + 1000, out.end());
        CHECK (levels.size() <= 17, "4 bits: %zu levels", levels.size());
    }
}

static void testFarPitch()
{
    // far up, three octaves: a tone that lands in the band comes out whole, one that lands over Nyquist leaves no
    // alias (read from the octave copies, at the cost of an unshifted voice)
    std::mt19937 rng (11);
    std::normal_distribution<float> g (0.0f, 0.2f);
    std::vector<float> two ((size_t) sr * 2);
    for (size_t i = 0; i < two.size(); ++i)
        two[i] = (float) (0.3 * std::sin (2.0 * pi * 2000.0 * (double) i / sr) + 0.3 * std::sin (2.0 * pi * 3500.0 * (double) i / sr));
    auto ins = instrumentOf (two, 48000, 48000 + 96 * 200);       // (96 samples: 4 periods of one, 7 of the other)
    ins->prepareForPlaying();
    Player p;
    p.set (snd::engine, (float) snd::sampler);
    plain (p);
    p.setInstrument (ins.get());
    auto out = play (p, 0.3, { { 0.0, 96, 127 } });
    const double inBand = toneLevel (out, 4800, 8192, 16000.0), alias = toneLevel (out, 4800, 8192, 48000.0 - 28000.0);
    CHECK (std::abs (db (inBand / 0.3)) < 0.5 && db (alias / 0.3) < -70.0, "+3 octaves: 16 kHz at %.2f dB, the alias at %.1f dB",
           db (inBand / 0.3), db (alias / 0.3));
    std::printf ("  +3 octaves: the tone at %.2f dB, the alias at %.1f dB\n", db (inBand / 0.3), db (alias / 0.3));
    auto timeOf = [&] (int note)
    {
        Player q;
        q.set (snd::engine, (float) snd::sampler);
        plain (q);
        q.setInstrument (ins.get());
        q.set (snd::fine, 1.0f);      // (moved a little: interpolated, not the exact copy)
        const auto t0 = std::chrono::steady_clock::now();
        play (q, 1.0, { { 0.0, note, 127 } });
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };
    const double flat = timeOf (60), far = timeOf (96);
    CHECK (far < flat * 2.5, "+3 octaves costs %.2fx an unshifted voice", far / flat);
    std::printf ("  +3 octaves costs %.2fx an unshifted voice\n", far / flat);

    // the attack kept: a click and a low body, two octaves down; the click stays as short as it was
    std::vector<float> kick ((size_t) sr);
    for (size_t i = 0; i < kick.size(); ++i)
    {
        const double t = (double) i / sr;
        const float click = i < 240 ? g (rng) * 3.0f * (1.0f - (float) i / 240.0f) : 0.0f;
        kick[i] = click + (float) (0.6 * std::exp (-t * 6.0) * std::sin (2.0 * pi * 110.0 * t));
    }
    auto drum = instrumentOf (kick);
    drum->prepareForPlaying();
    CHECK (drum->samples[0].attackEnd > 0 && drum->samples[0].attackEnd < 600, "attack measured at %d", drum->samples[0].attackEnd);
    auto clickLength = [&] (bool keep)
    {
        Player q;
        q.set (snd::engine, (float) snd::sampler);
        plain (q);
        q.setInstrument (drum.get());
        q.set (snd::transients, keep ? 1.0f : 0.0f);
        auto o = play (q, 0.3, { { 0.0, 36, 127 } });
        // where the first differences (the click's highs) die down
        int lastLoud = 0;
        for (int i = 1; i < 4000; ++i)
            if (std::abs (o[(size_t) i] - o[(size_t) i - 1]) > 0.05f)
                lastLoud = i;
        return lastLoud;
    };
    const int kept = clickLength (true), stretched = clickLength (false);
    CHECK (kept < 420 && stretched > 700, "the click: %d samples kept, %d in varispeed", kept, stretched);
    std::printf ("  a click two octaves down: %d samples with TRANSITOIRES GARDÉS, %d without\n", kept, stretched);
}

static void testScenes()
{
    auto tone = instrumentOf (sine (220.0, 1.0));
    Player p;
    p.set (snd::engine, (float) snd::sampler);
    plain (p);
    p.setInstrument (tone.get());
    p.set (snd::fModel, 1.0f);
    p.setSceneLock (0, snd::cutoff, 200.0f);
    p.setSceneLock (1, snd::cutoff, 8000.0f);
    p.setSceneLock (1, snd::voiceMode, 1.0f);          // not a scene's: refused
    CHECK (std::isnan (p.sceneLock (1, snd::voiceMode)), "a scene does not hold the voice mode");
    auto at = [&] (float fader)
    {
        p.set (snd::xfade, fader);
        play (p, 0.02, {});
        return snd::fromNorm (snd::cutoff, p.modulatedNorm (snd::cutoff));
    };
    const float a = at (0.0f), m = at (0.5f), b = at (1.0f);
    CHECK (std::abs (a - 200.0f) < 1.0f && std::abs (b - 8000.0f) < 10.0f && std::abs (m - 1265.0f) < 15.0f,
           "fader: %.0f, %.0f, %.0f Hz", a, m, b);
    // a setting held by scene B only goes from where it is
    p.set (snd::reso, 0.1f);
    p.setSceneLock (1, snd::reso, 0.9f);
    p.set (snd::xfade, 0.5f);
    play (p, 0.02, {});
    CHECK (std::abs (snd::fromNorm (snd::reso, p.modulatedNorm (snd::reso)) - 0.5f) < 0.01f, "half way from its own value");
    // a modulator moves the fader itself
    p.setRoute (0, snd::xfade, 1.0f);
    p.set (snd::m1Shape, 4.0f);
    p.set (snd::xfade, 0.0f);
    float lo = 1.0e9f, hi = 0.0f;
    for (int k = 0; k < 50; ++k)
    {
        play (p, 0.02, {});
        const float f = snd::fromNorm (snd::cutoff, p.modulatedNorm (snd::cutoff));
        lo = std::min (lo, f);
        hi = std::max (hi, f);
    }
    CHECK (lo < 250.0f && hi > 7000.0f, "a square on the fader: %.0f .. %.0f Hz", lo, hi);
}

static void testVoices()
{
    auto tone = instrumentOf (sine (rk::noteHz (60), 3.0), 24000, 24000 + 3670);
    {
        // LEGATO with a glide: one voice, the pitch going on to the second note
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::voiceMode, 2.0f);
        p.set (snd::glide, 80.0f);
        auto out = play (p, 1.0, { { 0.0, 60, 100 }, { 0.3, 67, 100 } });
        CHECK (p.activeVoices.load() == 1, "legato: %d voices", p.activeVoices.load());
        const auto a = rk::detectPitch (out, sr, rk::noteHz (60), 4800, 9600);
        const auto b = rk::detectPitch (out, sr, rk::noteHz (67), 40000, 47000);
        CHECK (a.ok && std::abs (a.cents) < 3.0 && b.ok && std::abs (b.cents) < 3.0, "legato: %.1f then %.1f cents", a.cents, b.cents);
        // no gap: the level stays up through the change
        double lowest = 1.0;
        for (size_t i = 14000; i + 480 < 20000; i += 480)
            lowest = std::min (lowest, rms (out, i, i + 480));
        CHECK (lowest > 0.25, "legato keeps sounding (lowest %.3f)", lowest);
    }
    {
        // POLY: never more than 8 (and the ones let go fade out)
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        std::vector<Event> ev;
        for (int k = 0; k < 12; ++k)
            ev.push_back ({ 0.01 * k, 48 + k, 100 });
        play (p, 0.3, ev);
        CHECK (p.activeVoices.load() == 8, "poly: %d voices", p.activeVoices.load());
    }
    {
        // MONO: each note starts again, one voice once the old one has faded
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::voiceMode, 1.0f);
        auto out = play (p, 0.5, { { 0.0, 60, 100 }, { 0.2, 64, 100 } });
        CHECK (p.activeVoices.load() == 1, "mono: %d voices", p.activeVoices.load());
        float worst = 0.0f;
        for (size_t i = 9500; i < 10500; ++i)
            worst = std::max (worst, std::abs (out[i] - out[i - 1]));
        CHECK (worst < 0.06f, "mono retrigger without a click (step %.3f)", worst);
    }
}

static void testFilterModsFx()
{
    // a saw through each filter at 300 Hz: the top gone
    std::vector<float> saw ((size_t) sr * 2);
    for (size_t i = 0; i < saw.size(); ++i)
    {
        double v = 0.0;
        for (int h = 1; h * 110.0 < 20000.0; ++h)
            v += std::sin (2.0 * pi * 110.0 * h * (double) i / sr) / h;
        saw[i] = (float) (0.3 * v);
    }
    auto ins = instrumentOf (saw, 48000, 48000 + 436 * 20);
    double open = 0.0;
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        auto out = play (p, 0.5, { { 0.0, 60, 127 } });
        open = toneLevel (out, 4800, 8192, 110.0 * 40);
    }
    const char* names[] = { "", "MOOG", "MS-20", "SEM", "OSCAR", "WASP", "POLIVOKS" };
    for (int model = 1; model <= 6; ++model)
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::fModel, (float) model);
        p.set (snd::cutoff, 300.0f);
        p.set (snd::reso, 0.3f);
        p.set (snd::keyTrack, 0.0f);
        p.set (snd::velCutoff, 0.0f);
        auto out = play (p, 0.5, { { 0.0, 60, 127 } });
        const double top = toneLevel (out, 4800, 8192, 110.0 * 40);
        CHECK (db (top / open) < -30.0, "%s at 300 Hz: 4.4 kHz down %.1f dB", names[model], db (top / open));
    }

    // a modulator on the volume: a square, a quarter note at 120 (2 Hz), the level going up and down
    {
        auto tone = instrumentOf (sine (440.0, 3.0), 24000, 24000 + 4800);
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::m1Type, 0.0f);
        p.set (snd::m1Shape, 4.0f);         // CARRÉ
        p.set (snd::m1Div, 4.0f);           // 1/4
        p.set (snd::tempo, 120.0f);
        p.setRoute (0, snd::volume, -0.5f);
        auto out = play (p, 1.0, { { 0.0, 60, 127 } });
        const double a = rms (out, 1000, 11000), b = rms (out, 13000, 23000);
        CHECK (std::abs (db (a / b)) > 10.0, "square on the volume: %.1f dB between its halves", db (a / b));
        CHECK (p.modulatedNorm (snd::volume) >= 0.0f && p.modulatedNorm (snd::cutoff) < 0.0f, "what the rows are told");
    }

    // FX mode: the input through the chain; with nothing on it comes out as it went in
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        auto in = sine (330.0, 0.5, 0.4f);
        auto out = play (p, 0.5, {}, nullptr, &in, true);
        float worst = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - in[i]));
        CHECK (worst < 1.0e-6f, "FX mode, nothing on: off by %.2e", worst);
        p.set (snd::crOn, 1.0f);
        p.set (snd::crBits, 3.0f);
        out = play (p, 0.5, {}, nullptr, &in, true);
        double diff = 0.0;
        for (size_t i = 12000; i < out.size(); ++i)
            diff += std::abs (out[i] - in[i]);
        CHECK (diff / (double) (out.size() - 12000) > 0.01, "FX mode with CRUSH changes the sound");
        // and the input is not added when it isn't through the effects
        Player q;
        q.set (snd::engine, (float) snd::sampler);
        plain (q);
        out = play (q, 0.2, {}, nullptr, &in, false);
        CHECK (rms (out, 0, out.size()) < 1.0e-9, "input not through the effects: silence from the player");
    }
}

static void testStability()
{
    // everything on, everything modulated hard, every filter model in turn: nothing blows up
    std::vector<float> noise ((size_t) sr);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-0.7f, 0.7f);
    for (auto& x : noise) x = u (rng);
    auto ins = instrumentOf (noise, 12000, 36000, true);
    for (int model = 1; model <= 6; ++model)
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        p.prepare (sr, 512);
        p.setInstrument (ins.get());
        p.set (snd::fModel, (float) model);
        p.set (snd::reso, 1.0f);
        p.set (snd::fDrive, 1.0f);
        p.set (snd::fm, 1.0f);
        p.set (snd::quality, 2.0f);
        p.set (snd::dModel, (float) (model % 3 + 1));
        p.set (snd::dAmt, 1.0f);
        for (int fx : { snd::spOn, snd::piOn, snd::chOn, snd::crOn, snd::rvOn })
            p.set (fx, 1.0f);
        p.set (snd::rvShimmer, 1.0f);
        p.set (snd::rvDecay, 1.0f);
        p.set (snd::piFeedback, 0.95f);
        p.set (snd::chFeedback, 0.95f);
        p.set (snd::pmSource, 2.0f);
        p.set (snd::pmIndex, 256.0f);
        p.set (snd::alias, 0.95f);
        for (int m = 0; m < 4; ++m)
        {
            p.set (snd::modParam (m, snd::mType), (float) (m % 3));
            p.set (snd::modParam (m, snd::mDiv), 7.0f);
        }
        p.setRoute (0, snd::cutoff, 1.0f);
        p.setRoute (1, snd::pmIndex, -1.0f);
        p.setRoute (2, snd::loopSize, -1.0f);
        p.setRoute (3, snd::transpose, 1.0f);
        p.setRoute (0, snd::reverse, 1.0f);
        std::vector<Event> ev;
        for (int k = 0; k < 40; ++k)
            ev.push_back ({ 0.1 * k, 30 + (k * 7) % 60, k % 3 == 2 ? 0 : 127 });
        std::vector<float> right;
        auto out = play (p, 4.0, ev, &right);
        bool finite = true;
        float peak = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
        {
            finite = finite && std::isfinite (out[i]) && std::isfinite (right[i]);
            peak = std::max (peak, std::max (std::abs (out[i]), std::abs (right[i])));
        }
        CHECK (finite && peak < 30.0f, "filter %d with everything: finite %d, peak %.2f", model, (int) finite, peak);
    }
}

static void testCpu()
{
    // eight stereo voices, an MS-20 at 4x, phase modulation, elastic, everything in the chain on
    std::vector<float> noise ((size_t) sr * 2);
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    for (auto& x : noise) x = u (rng);
    auto ins = instrumentOf (noise, 24000, 72000, true);
    for (int elastic = 0; elastic < 2; ++elastic)
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        p.prepare (sr, 512);
        p.setInstrument (ins.get());
        p.set (snd::fModel, 2.0f);
        p.set (snd::quality, 2.0f);
        p.set (snd::pmSource, 1.0f);
        p.set (snd::pmIndex, 40.0f);
        p.set (snd::playMode, (float) elastic);
        p.set (snd::dModel, 3.0f);
        for (int fx : { snd::spOn, snd::piOn, snd::chOn, snd::crOn, snd::rvOn })
            p.set (fx, 1.0f);
        std::vector<Event> ev;
        for (int k = 0; k < 8; ++k)
            ev.push_back ({ 0.0, 48 + k * 3, 120 });
        const auto t0 = std::chrono::steady_clock::now();
        play (p, 5.0, ev, nullptr, nullptr, false, 512);
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  8 voices %s, MS-20 4x, PM, whole chain: %.1f %% of a core\n", elastic ? "ELASTIQUE" : "SAMPLE", took / 5.0 * 100.0);
        // (a loose bound: a shared CI machine can be slow; the figure printed is what to watch)
        CHECK (took / 5.0 < 1.5, "CPU %.0f %%", took / 5.0 * 100.0);
    }
    // SYNTHÉ: eight voices of four unison voices each, both oscillators, PM, an MS-20 at 4x, the whole chain
    {
        Player p;
        p.set (snd::engine, (float) snd::synth);
        p.prepare (sr, 512);
        p.set (snd::fModel, 2.0f);
        p.set (snd::quality, 2.0f);
        p.set (snd::unison, 4.0f);
        p.set (snd::pmBA, 0.5f);
        p.set (snd::subLevel, 0.3f);
        p.set (snd::dModel, 3.0f);
        for (int fx : { snd::spOn, snd::piOn, snd::chOn, snd::crOn, snd::rvOn })
            p.set (fx, 1.0f);
        std::vector<Event> ev;
        for (int k = 0; k < 8; ++k)
            ev.push_back ({ 0.0, 48 + k * 3, 120 });
        const auto t0 = std::chrono::steady_clock::now();
        play (p, 5.0, ev, nullptr, nullptr, false, 512);
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  8 voices SYNTHÉ x4 unison, PM, MS-20 4x, whole chain: %.1f %% of a core\n", took / 5.0 * 100.0);
        CHECK (took / 5.0 < 1.5, "CPU %.0f %%", took / 5.0 * 100.0);
    }
}

static void testClock()
{
    // out of the plug-in, following the host: Start, then 24 ticks a beat on its grid, Stop
    MidiClock clock;
    clock.setDestination ("host", "DAW");
    clock.setMode (MidiClock::Mode::host);
    juce::MidiBuffer out;
    std::vector<int> ticks;
    bool started = false, stopped = false;
    for (int b = 0; b < 100; ++b)
    {
        out.clear();
        const double ppq = 0.5 + b * 480.0 / 24000.0;          // 480 samples a block at 120 bpm, 48 kHz
        clock.process (true, ppq, 120.0, 480, sr, out);
        for (const auto m : out)
        {
            const auto msg = m.getMessage();
            if (msg.getRawData()[0] == 0xF8) ticks.push_back (b * 480 + m.samplePosition);
            if (msg.getRawData()[0] == 0xFA) started = true;
        }
    }
    out.clear();
    clock.process (false, 0.0, 120.0, 480, sr, out);
    for (const auto m : out)
        stopped = stopped || m.getMessage().getRawData()[0] == 0xFC;
    bool even = ticks.size() > 2;
    for (size_t i = 1; i < ticks.size(); ++i)
        even = even && std::abs ((ticks[i] - ticks[i - 1]) - 1000) <= 1;
    CHECK (started && stopped && ticks.size() == 48 && even && ticks[0] == 0, "host clock: %zu ticks, start %d stop %d, first at %d",
           ticks.size(), (int) started, (int) stopped, ticks.empty() ? -1 : ticks[0]);

    // its own tempo, out of the plug-in: started by its button
    MidiClock own;
    own.setDestination ("host", "DAW");
    std::atomic<float> tempo { 90.0f };
    own.bindTempo (&tempo);
    own.setRunning (true);
    int count = 0;
    for (int b = 0; b < 99; ++b)      // (0.99 s: the 37th tick would fall on the second exactly)
    {
        out.clear();
        own.process (false, 0.0, 120.0, 480, sr, out);
        for (const auto m : out)
            count += m.getMessage().getRawData()[0] == 0xF8;
    }
    CHECK (count == 36, "own clock at 90 bpm over a second: %d ticks", count);   // 1.5 beats x 24
}

//==============================================================================
// No stairs: the settings move every 32 samples and the voices ramp in between (the cutoff, the pitch); a knob thrown
// across glides instead of jumping
static void testSmoothing()
{
    auto tone = instrumentOf (sine (230.0, 3.0), 48000, 96000);     // 230 cycles in its loop
    auto resonant = [] (Player& p, float cutoff)
    {
        p.set (snd::fModel, 1.0f);           // MOOG
        p.set (snd::cutoff, cutoff);
        p.set (snd::reso, 0.7f);
        p.set (snd::keyTrack, 0.0f);
        p.set (snd::velCutoff, 0.0f);
    };
    // a slow sine on the cutoff (a quarter note at 120): stairs every 32 samples would be a sample-and-hold at 1.5 kHz,
    // the tone's sidebands at 1500 -+ 230 Hz (away from its harmonics and theirs)
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        resonant (p, 2000.0f);               // its peak sweeping well above the tone (no legitimate sidebands that far)
        p.set (snd::m1Type, 0.0f);
        p.set (snd::m1Shape, 0.0f);          // SINUS
        p.set (snd::m1Div, 5.0f);            // 1/8
        p.set (snd::tempo, 120.0f);
        p.setRoute (0, snd::cutoff, 0.1f);
        auto out = play (p, 1.5, { { 0.0, 60, 127 } });
        const double t = toneLevel (out, 24000, 32768, 230.0);
        const double side = std::max (toneLevel (out, 24000, 32768, 1270.0), toneLevel (out, 24000, 32768, 1730.0));
        std::printf ("  cutoff swept by a sine: the 1.5 kHz stairs' sidebands at %.1f dB of the tone\n", db (side / t));
        CHECK (db (side / t) < -110.0, "the stairs' sidebands at %.1f dB", db (side / t));
    }
    // vibrato: the pitch moved by a sine, the same sidebands
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::m1Type, 0.0f);
        p.set (snd::m1Shape, 0.0f);
        p.set (snd::m1Div, 6.0f);            // 1/16
        p.set (snd::tempo, 120.0f);
        p.setRoute (0, snd::fine, 0.3f);
        auto out = play (p, 1.5, { { 0.0, 60, 127 } });
        const double t = toneLevel (out, 24000, 32768, 230.0);
        const double side = std::max (toneLevel (out, 24000, 32768, 1270.0), toneLevel (out, 24000, 32768, 1730.0));
        std::printf ("  vibrato: the stairs' sidebands at %.1f dB of the tone\n", db (side / t));
        CHECK (db (side / t) < -120.0, "vibrato stairs' sidebands at %.1f dB", db (side / t));
    }
    // the cutoff knob thrown from 400 Hz to 8 kHz while the note sounds: no click (the curvature of the output, its
    // second difference, stays near the tone's own)
    {
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        resonant (p, 400.0f);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, 256);
        const int thrownAt = 94;
        for (int b = 0; b < (int) (1.0 * sr / 256); ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
            if (b == thrownAt)
                p.set (snd::cutoff, 8000.0f);
            buf.clear();
            p.render (buf, midi, {});
            for (int i = 0; i < 256; ++i)
                out.push_back (buf.getSample (0, i));
        }
        auto curvature = [&] (size_t from, size_t to)
        {
            double m = 0.0;
            for (size_t i = from; i < to && i < out.size(); ++i)
                m = std::max (m, (double) std::abs (out[i] - 2.0f * out[i - 1] + out[i - 2]));
            return m;
        };
        const size_t at = (size_t) thrownAt * 256;
        const double before = curvature (at - 9600, at), during = curvature (at, at + 4800), after = curvature (at + 12000, at + 24000);
        std::printf ("  cutoff thrown across: curvature %.5f (before %.5f, after %.5f)\n", during, before, after);
        CHECK (during < 2.0 * std::max (before, after), "a knob thrown across clicks: %.5f against %.5f", during, std::max (before, after));
    }
}

//==============================================================================
// SYNTHÉ: in tune, nothing folding back even at the top of the keyboard, the bank's cycles morphing, the unison;
// SAMPLER: the loop's seam crossfaded
static void testSynth()
{
    auto synthPlayer = [] (Player& p)
    {
        plain (p);
        p.set (snd::engine, (float) snd::synth);
        p.set (snd::oaPos, 2.0f / 7.0f);      // the saw
        p.set (snd::obLevel, 0.0f);
        p.set (snd::unison, 1.0f);
    };
    {
        Player p;
        synthPlayer (p);
        auto out = play (p, 0.6, { { 0.0, 69, 127 } });
        const auto pitch = rk::detectPitch (out, sr, 440.0, 4800, 24000);
        CHECK (pitch.ok && std::abs (pitch.cents) < 0.5, "the saw at A4: %.2f cents (ok %d)", pitch.cents, (int) pitch.ok);
        // a saw: its second harmonic half the first (-6 dB)
        const double h1 = toneLevel (out, 4800, 16384, 440.0), h2 = toneLevel (out, 4800, 16384, 880.0);
        CHECK (std::abs (db (h2 / h1) + 6.0) < 0.5, "saw: the 2nd harmonic at %.2f dB", db (h2 / h1));
    }
    {
        // C8 (4186 Hz): its 7th harmonic (29.3 kHz) would fold back to 18.7 kHz
        Player p;
        synthPlayer (p);
        auto out = play (p, 0.6, { { 0.0, 108, 127 } });
        const double f = toneLevel (out, 4800, 16384, rk::noteHz (108));
        const double alias = std::max (toneLevel (out, 4800, 16384, 48000.0 - 7.0 * rk::noteHz (108)),
                                       toneLevel (out, 4800, 16384, 48000.0 - 6.0 * rk::noteHz (108)));
        std::printf ("  SYNTHÉ saw at C8: the fold-back at %.1f dB\n", db (alias / f));
        CHECK (db (alias / f) < -90.0, "saw at C8 folds back at %.1f dB", db (alias / f));
    }
    {
        // the bank: position 0 a sine (no 2nd or 3rd harmonic), the square (odd only)
        Player p;
        synthPlayer (p);
        p.set (snd::oaPos, 0.0f);
        auto out = play (p, 0.5, { { 0.0, 57, 127 } });
        const double h1 = toneLevel (out, 4800, 16384, 220.0), h3 = toneLevel (out, 4800, 16384, 660.0);
        CHECK (db (h3 / h1) < -80.0, "position 0 is a sine: 3rd harmonic at %.1f dB", db (h3 / h1));
        Player q;
        synthPlayer (q);
        q.set (snd::oaPos, 3.0f / 7.0f);
        auto sq = play (q, 0.5, { { 0.0, 57, 127 } });
        const double s1 = toneLevel (sq, 4800, 16384, 220.0), s2 = toneLevel (sq, 4800, 16384, 440.0), s3 = toneLevel (sq, 4800, 16384, 660.0);
        CHECK (db (s2 / s1) < -60.0 && std::abs (db (s3 / s1) + 9.54) < 0.6, "the square: 2nd at %.1f dB, 3rd at %.2f dB", db (s2 / s1), db (s3 / s1));
    }
    {
        // unison: four voices, the stereo apart
        Player p;
        synthPlayer (p);
        p.set (snd::unison, 4.0f);
        p.set (snd::detune, 0.5f);
        p.set (snd::width, 1.0f);
        std::vector<float> right;
        auto left = play (p, 0.5, { { 0.0, 57, 127 } }, &right);
        double diff = 0.0;
        for (size_t i = 4800; i < left.size(); ++i)
            diff += std::abs (left[i] - right[i]);
        CHECK (diff / (double) (left.size() - 4800) > 0.05 * Player::synthLevel && rms (left, 4800, left.size()) > 0.1 * Player::synthLevel,
               "unison: left and right apart by %.3f",
               diff / (double) (left.size() - 4800));
    }
    {
        // a loop whose ends don't meet (44.1 cycles): the seam jumps; crossfaded, it doesn't
        auto ins = instrumentOf (sine (440.0, 2.0), 24000, 24000 + 4811);
        auto worstStep = [&] (float xf)
        {
            Player p;
            p.set (snd::engine, (float) snd::sampler);
            plain (p);
            p.setInstrument (ins.get());
            p.set (snd::loopXfade, xf);
            auto out = play (p, 1.5, { { 0.0, 60, 127 } });
            float worst = 0.0f;
            for (size_t i = 30000; i < out.size(); ++i)
                worst = std::max (worst, std::abs (out[i] - out[i - 1]));
            return worst;
        };
        const float natural = 0.5f * (float) (2.0 * pi * 440.0 / sr);
        const float raw = worstStep (0.0f), smooth = worstStep (0.5f);
        std::printf ("  loop seam: steps up to %.3f, crossfaded %.3f (the sine's own %.3f)\n", raw, smooth, natural);
        CHECK (raw > 3.0f * natural && smooth < 1.3f * natural, "crossfade: %.3f then %.3f", raw, smooth);
    }
    {
        // the chain reordered: CRUSH after ESPACE crushes the reverb's tail too (another sound), and the change
        // passes through a dip, no click
        auto render = [&] (bool reorder, float* worst)
        {
            Player p;
            synthPlayer (p);
            p.set (snd::crOn, 1.0f);
            p.set (snd::crBits, 5.0f);
            p.set (snd::crMix, 1.0f);
            p.set (snd::rvOn, 1.0f);
            p.set (snd::rvMix, 0.6f);
            std::vector<float> out;
            juce::AudioBuffer<float> buf (2, 256);
            for (int b = 0; b < (int) (0.8 * sr / 256); ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 127), 0);
                if (reorder && b == 60)
                    p.setFxOrder ({ 0, 1, 2, 3, 5, 6, 4 });
                buf.clear();
                p.render (buf, midi, {});
                for (int i = 0; i < 256; ++i)
                    out.push_back (buf.getSample (0, i));
            }
            if (worst != nullptr)
            {
                *worst = 0.0f;
                for (size_t i = 60 * 256; i < 60 * 256 + 1200; ++i)      // the dip: 10 ms down, 10 up
                    *worst = std::max (*worst, std::abs (out[i] - out[i - 1]));
            }
            return out;
        };
        float stepAtChange = 0.0f;
        auto a = render (false, nullptr), b = render (true, &stepAtChange);
        double diff = 0.0;
        for (size_t i = 25000; i < a.size(); ++i)
            diff += std::abs (a[i] - b[i]);
        float before = 0.0f;      // the largest steps of either order around the change
        for (size_t i = 40 * 256; i < 60 * 256; ++i)
            before = std::max (before, std::abs (b[i] - b[i - 1]));
        for (size_t i = 60 * 256 + 2400; i < 60 * 256 + 12000; ++i)
            before = std::max (before, std::abs (b[i] - b[i - 1]));
        std::printf ("  chain reordered: %.3f apart, steps %.3f at the change (%.3f either side)\n", diff / (double) (a.size() - 25000), stepAtChange, before);
        CHECK (diff / (double) (a.size() - 25000) > 0.005 * Player::synthLevel && stepAtChange <= before * 1.05f, "reorder: %.4f apart, step %.3f against %.3f",
               diff / (double) (a.size() - 25000), stepAtChange, before);
        Player q;
        q.setFxOrder ({ 0, 0, 1, 2, 3, 4, 5 });
        CHECK (q.fxOrder()[1] == 1, "a stage twice is refused");
    }
}

// RÉÉCHANTILLONNER: the processed input recorded, then looped into the chain made neutral: what was heard goes on,
// the same, without a click; the effects' settings can then be set to zero without a change; a note takes over
static void testResample()
{
    Player p;
    p.set (snd::engine, (float) snd::sampler);
    plain (p);
    p.set (snd::tempo, 120.0f);
    p.set (snd::chOn, 1.0f);
    p.set (snd::fModel, 1.0f);
    p.set (snd::cutoff, 2500.0f);
    const int block = 256, total = (int) (5.0 * sr);
    std::vector<float> in ((size_t) total);
    for (int i = 0; i < total; ++i)
        in[(size_t) i] = 0.4f * (float) (std::sin (2.0 * pi * 220.0 * i / sr) * (0.6 + 0.4 * std::sin (2.0 * pi * 1.3 * i / sr)));
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, block);
    int armedAt = -1, loopingAt = -1, idleAt = -1, takenAt = -1, releasedAt = -1;
    const int noteAt = (int) (4.2 * sr);
    rk::Channels rec;
    for (int pos = 0; pos < total; pos += block)
    {
        if (armedAt < 0 && pos >= (int) (0.25 * sr))
        {
            CHECK (p.armResample (10.0, 1, false), "armed");
            armedAt = pos;
        }
        juce::MidiBuffer midi;
        if (noteAt >= pos && noteAt < pos + block)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), noteAt - pos);
        const float* chans[2] = { in.data() + pos, in.data() + pos };
        buf.clear();
        p.render (buf, midi, {}, chans, 2, true);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
        const auto st = p.resampleState();
        if (loopingAt < 0 && st == Player::Resample::looping)
            loopingAt = pos;
        // the message thread: the recording taken, the effects set to zero, then the chain let go
        if (loopingAt >= 0 && takenAt < 0 && pos > loopingAt + (int) (0.05 * sr) && p.resampleReady())
        {
            rec = p.takeResample();
            p.set (snd::chOn, 0.0f);
            p.set (snd::fModel, 0.0f);
            takenAt = pos;
        }
        if (takenAt >= 0 && releasedAt < 0 && pos > takenAt + (int) (0.04 * sr))
        {
            p.releaseNeutral();
            releasedAt = pos;
        }
        if (pos > noteAt && idleAt < 0 && st == Player::Resample::idle)
            idleAt = pos;
    }
    const int target = (int) (2.0 * sr), fade = (int) (0.02 * sr), tail = (int) (0.01 * sr);
    CHECK (rec.size() == 2 && (int) rec[0].size() == target, "one bar at 120: %d frames", rec.empty() ? 0 : (int) rec[0].size());
    if (rec.size() != 2 || (int) rec[0].size() != target)
        return;
    // what was recorded is what was heard (past the start, crossfaded with what followed the end)
    float worst = 0.0f;
    for (int j = tail; j < target; ++j)
        worst = std::max (worst, std::abs (rec[0][(size_t) j] - out[(size_t) (armedAt + j)]));
    CHECK (worst < 1.0e-6f, "recorded as heard: off by %.2e", worst);
    // past the handover, the loop as recorded: the chain neutral, then its settings at zero, then let go
    const int t0 = armedAt + target;
    worst = 0.0f;
    for (int t = t0 + fade; t < noteAt; ++t)
        worst = std::max (worst, std::abs (out[(size_t) t] - rec[0][(size_t) ((t - t0) % target)]));
    CHECK (worst < 1.0e-6f, "the loop heard as it was recorded: off by %.2e", worst);
    CHECK (releasedAt > 0 && releasedAt < noteAt, "let go while looping");
    // no click: at the handover and where it loops, no step larger than the sound's own
    auto maxStep = [&] (int from, int to)
    {
        float m = 0.0f;
        for (int t = from + 1; t < to; ++t)
            m = std::max (m, std::abs (out[(size_t) t] - out[(size_t) t - 1]));
        return m;
    };
    const float own = maxStep (armedAt, t0 - 2000);
    const float atHandover = maxStep (t0 - 200, t0 + fade + 200), atWrap = maxStep (t0 + target - 200, t0 + target + 200);
    CHECK (atHandover < own * 1.3f && atWrap < own * 1.3f, "steps: %.4f at the handover, %.4f where it loops, %.4f its own", atHandover, atWrap, own);
    std::printf ("  resample: steps %.4f at the handover, %.4f where it loops, %.4f its own; taken over in %.1f ms\n",
                 atHandover, atWrap, own, (idleAt - noteAt) * 1000.0 / sr);
    // a note takes over: the loop fades out (30 ms)
    CHECK (idleAt > 0 && idleAt - noteAt < (int) (0.05 * sr), "a note stops the loop: after %.1f ms", (idleAt - noteAt) * 1000.0 / sr);

    // synced: from the next bar of the transport
    {
        Player q;
        q.set (snd::engine, (float) snd::sampler);
        plain (q);
        Player::Transport tr;
        tr.playing = true;
        tr.bpm = 120.0;
        tr.ppq = 1.3;
        CHECK (q.armResample (10.0, 1, true), "armed, synced");
        int started = -1;
        for (int pos = 0; pos < (int) (2.0 * sr); pos += block)
        {
            buf.clear();
            const float* chans[2] = { in.data() + pos, in.data() + pos };
            q.render (buf, {}, tr, chans, 2, true);
            tr.ppq += block / sr * 2.0;
            const int written = (int) std::llround (q.resampleSeconds() * sr);
            if (started < 0 && written > 0)
                started = pos + block - written;
        }
        const int expected = (int) std::llround ((4.0 - 1.3) * 0.5 * sr);
        CHECK (std::abs (started - expected) <= 1, "synced: started at %d, the bar at %d", started, expected);
        q.stopResample();
    }
}

// the sound REPLIK8 opens with (every setting at its default), played as a host would: one note, then a chord, at
// the usual rates and block sizes; nothing may blow up
static void testDefaultSound()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 512, 1024 })
        {
            Player p;
            p.prepare (rate, block);
            juce::AudioBuffer<float> buf (2, block);
            const int total = (int) (2.0 * rate);
            float peak = 0.0f, single = 0.0f;
            double energy = 0.0;
            bool finite = true;
            int frames = 0;
            for (int pos = 0; pos < total; pos += block)
            {
                if (pos == (int) (0.75 * rate) / block * block)
                    single = peak;
                juce::MidiBuffer midi;
                if (pos == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                if (pos <= (int) (0.8 * rate) && pos + block > (int) (0.8 * rate))
                    for (int n : { 48, 55, 64 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), 0);
                buf.clear();
                p.render (buf, midi, {}, nullptr, 0, false);
                for (int ch = 0; ch < 2; ++ch)
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
            CHECK (finite && single > 0.15f && single < 0.4f && peak < 1.2f && rms > 0.05 && rms < 0.35,
                   "default sound at %.0f Hz, blocks of %d: a note %.2f, then a chord: peak %.2f, rms %.3f%s",
                   rate, block, single, peak, rms, finite ? "" : ", not finite");
            if (block == 512)
                std::printf ("  default sound at %.0f Hz: a note %.2f, with a chord: peak %.2f, rms %.3f\n", rate, single, peak, rms);
        }
}

static void probe()
{
    const double rate = 48000.0;
    const int block = 1024;
    Player p;
    p.prepare (rate, block);
    if (auto* e = std::getenv ("RK_PROBE_SET"))
    {
        auto parts = juce::StringArray::fromTokens (e, ",", "");
        for (int i = 0; i + 1 < parts.size(); i += 2)
            p.set (parts[i].getIntValue(), parts[i + 1].getFloatValue());
    }
    juce::AudioBuffer<float> buf (2, block);
    std::vector<float> L;
    for (int pos = 0; pos < (int) rate; pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 43, (juce::uint8) 100), 0);
        buf.clear();
        p.render (buf, midi, {}, nullptr, 0, false);
        for (int i = 0; i < block; ++i) L.push_back (buf.getSample (0, i));
    }
    float mn = 1e9f, mx = -1e9f; double dc = 0;
    for (size_t i = L.size() / 2; i < L.size(); ++i) { mn = std::min (mn, L[i]); mx = std::max (mx, L[i]); dc += L[i]; }
    dc /= (double) (L.size() / 2);
    std::printf ("min %.3f max %.3f dc %.3f\n", mn, mx, dc);
    for (int i = 24000; i < 24000 + 48; ++i) std::printf ("%.3f ", L[(size_t) i]);
    std::printf ("\n");
}

// the guard: something not a number (fed through the input, the filter on) never reaches the output, and the
// engine plays again right after
static void testGuard()
{
    Player p;
    p.prepare (48000.0, 512);
    p.set (snd::fModel, 1.0f);
    juce::AudioBuffer<float> buf (2, 512);
    std::vector<float> in (512, 0.0f);
    in[100] = std::numeric_limits<float>::quiet_NaN();
    const float* chans[2] = { in.data(), in.data() };
    bool clean = true;
    float after = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 5)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        buf.clear();
        p.render (buf, midi, {}, b == 2 ? chans : nullptr, b == 2 ? 2 : 0, b == 2);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                clean = clean && std::isfinite (buf.getSample (ch, i)) && std::abs (buf.getSample (ch, i)) <= 2.0f;
                if (b > 10)
                    after = std::max (after, std::abs (buf.getSample (ch, i)));
            }
    }
    CHECK (clean && after > 0.1f, "guard: clean %d, rescued %d, then playing %.2f", (int) clean, p.rescues.load(), after);
}

//==============================================================================
// The reading engine, measured as its design note asks: a slow pitch sweep through its phases and cutoffs (no level
// step), an impulse (linear phase: the ringing as long before as after), the feedback PM (bounded)
static void testReadingEngine()
{
    // a 2 kHz sine bent slowly up a fifth: speed 1 to 1.5, through the kernels' phases and cutoff rows
    {
        auto ins = instrumentOf (sine (2000.0, 3.0));
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::bend, 7.0f);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, 64);
        const int total = (int) (2.0 * sr);
        for (int pos = 0; pos < total; pos += 64)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
            const double t = juce::jlimit (0.0, 1.0, ((double) pos / sr - 0.2) / 1.6);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + (int) std::lround (t * 8191.0)), 0);
            p.render (buf, midi, {}, nullptr, 0, false);
            for (int i = 0; i < 64; ++i)
                out.push_back (buf.getSample (0, i));
        }
        double lo = 1.0e9, hi = -1.0e9;
        for (size_t w = (size_t) (0.25 * sr); w + 960 < (size_t) (1.75 * sr); w += 480)
        {
            const double l = db (rms (out, w, w + 960));
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        CHECK (hi - lo < 0.05, "a slow bend up a fifth: the level moves %.3f dB", hi - lo);
        std::printf ("  2 kHz bent up a fifth: level within %.4f dB\n", hi - lo);
    }
    // an impulse played a minor third up: the kernel is symmetric, so is the ringing round the peak
    {
        std::vector<float> imp (48000, 0.0f);
        imp[20000] = 1.0f;
        auto ins = instrumentOf (imp);
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        auto out = play (p, 0.6, { { 0.0, 63, 127 } });
        size_t peak = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > std::abs (out[peak]))
                peak = i;
        // where its energy is centred, against its peak found between samples: together for a symmetric response
        double e = 0.0, m = 0.0;
        for (size_t i = peak - 160; i <= peak + 160; ++i)
        {
            e += (double) out[i] * out[i];
            m += (double) out[i] * out[i] * (double) i;
        }
        const double y0 = out[peak - 1], y1 = out[peak], y2 = out[peak + 1];
        const double top = (double) peak + 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
        const double shift = m / e - top;
        CHECK (std::abs (shift) < 0.25, "an impulse: its energy centred %.3f samples from its peak (linear phase)", shift);
        std::printf ("  impulse a minor third up: energy centred %.3f samples from the peak\n", shift);
    }
    // the tuning dial at its ends: +96 semitones (256x, the eighth octave copy) and -48, the tone where it should be
    for (auto [hz, tune] : { std::pair<double, float> { 40.0, 96.0f }, { 2000.0, -48.0f }, { 300.0, 60.5f } })
    {
        // (looped on whole periods: at 256x a recording runs out in no time)
        const int period = (int) std::lround (sr / hz);
        auto ins = instrumentOf (sine (hz, 2.0), period, period * (int) (1.8 * hz));
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::tune, tune);
        auto out = play (p, 0.8, { { 0.0, 60, 127 } });
        const double want = hz * std::exp2 ((double) tune / 12.0);
        const double level = db (toneLevel (out, 4800, 32768, want) / 0.5);
        const double off = db (toneLevel (out, 4800, 32768, want * 1.26) / 0.5);
        CHECK (std::abs (level) < 0.5 && off < -60.0, "tune %+.1f: %.0f Hz at %.2f dB (a third off: %.1f dB)", tune, want, level, off);
        std::printf ("  tuning dial %+.1f st: %.0f Hz -> %.0f Hz at %.2f dB\n", tune, hz, want, level);
    }
    // the dial swept slowly from -24 to +44 (through the octave copies' handovers at +12, +24, +36): a 3 kHz tone
    // keeps its level, without a step
    {
        auto ins = instrumentOf (sine (3000.0, 2.0), 16, 16 * 5000);
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, 64);
        const int total = (int) (3.0 * sr);
        for (int pos = 0; pos < total; pos += 64)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (float) ((double) pos / sr - 0.2) / 2.6f);
            p.set (snd::tune, -24.0f + 68.0f * t);
            buf.clear();
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
            p.render (buf, midi, {}, nullptr, 0, false);
            for (int i = 0; i < 64; ++i)
                out.push_back (buf.getSample (0, i));
        }
        // (windows of 0.1 s, 30 periods and more: their own ripple stays far under what a step would make)
        double worst = 0.0, worstAt = 0.0;
        for (size_t w = (size_t) (0.3 * sr); w + 9600 < (size_t) (2.75 * sr); w += 1200)
        {
            const double d = std::abs (db (rms (out, w, w + 4800)) - db (rms (out, w + 1200, w + 6000)));
            if (d > worst) { worst = d; worstAt = (double) w / sr; }
        }
        if (worst >= 0.05)
            std::printf ("  (worst at %.2f s, tune %.1f)\n", worstAt, -24.0 + 68.0 * juce::jlimit (0.0, 1.0, (worstAt - 0.2) / 2.6));
        CHECK (worst < 0.05, "the dial swept -24 to +44 st: the level moves at most %.3f dB from one window to the next", worst);
        std::printf ("  dial swept -24 to +44 st (octave copies handing over): level steps under %.4f dB\n", worst);
    }
    // RETOUR at its deepest: chaos if it likes, but bounded and finite
    {
        auto ins = instrumentOf (sine (220.0, 2.0, 0.8f), 0, 96000 - 218);
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::pmSource, 4.0f);
        p.set (snd::pmIndex, 256.0f);
        auto out = play (p, 1.0, { { 0.0, 60, 127 } });
        float peak = 0.0f;
        bool finite = true;
        for (float x : out)
        {
            finite = finite && std::isfinite (x);
            peak = std::max (peak, std::abs (x));
        }
        CHECK (finite && peak < 1.5f, "RETOUR at 256 samples: peak %.2f", peak);
        CHECK (rms (out, 4800, out.size()) > 0.05, "RETOUR still sounds: %.3f", rms (out, 4800, out.size()));
    }
}

//==============================================================================
// ORBITE: on the note (harmonic while synced), clean far up, its timbre turning with ORBITE while the pitch stays,
// every algorithm bounded; in the SAMPLER's mix and as its phase modulator
static void testOrbite()
{
    auto orbiteOnly = [] (Player& p)
    {
        p.set (snd::engine, (float) snd::synth);
        plain (p);
        p.set (snd::fModel, 0.0f);
        p.set (snd::oaLevel, 0.0f);
        p.set (snd::obLevel, 0.0f);
        p.set (snd::orOn, 1.0f);
        p.set (snd::mxOrbite, 1.0f);
        p.set (snd::orOrbit, 0.0f);
    };
    // how much of a sound sits on the harmonics of f0 (the rest: aliases, or inharmonic partials)
    auto offHarmonic = [] (const std::vector<float>& x, size_t from, double f0, size_t n = 16384)
    {
        // (its DC aside: a ring modulation has some, the output's high pass takes it)
        double mean = 0.0;
        for (size_t i = 0; i < n; ++i)
            mean += x[from + i];
        mean /= (double) n;
        double total = 0.0, w = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n);
            total += h * ((double) x[from + i] - mean) * ((double) x[from + i] - mean);
            w += h;
        }
        total /= w;
        double harm = 0.0;
        for (int k = 1; k * f0 < sr * 0.5 - 50.0; ++k)
        {
            const double a = toneLevel (x, from, n, k * f0);
            harm += 0.5 * a * a;
        }
        return db (std::sqrt (std::max (1.0e-30, total - harm) / std::max (1.0e-30, total)));
    };
    {
        Player p;
        orbiteOnly (p);
        auto out = play (p, 0.6, { { 0.0, 57, 127 } });
        const double off = offHarmonic (out, 4800, rk::noteHz (57));
        CHECK (off < -45.0 && rms (out, 4800, 20000) > 0.02, "ORBITE on A2: off the harmonics %.1f dB (rms %.3f)", off, rms (out, 4800, 20000));
        std::printf ("  ORBITE A2, synced: off its harmonics %.1f dB\n", off);
    }
    // the loudest alias heard (below 18 kHz) of a sound on f0, against its loudest harmonic: partials up to
    // four times the rate fold back at |k f0 - j sr|
    auto aliasDb = [] (const std::vector<float>& x, size_t from, double f0)
    {
        double top = 0.0, worst = 0.0;
        for (int k = 1; k * f0 < sr * 0.5; ++k)
            top = std::max (top, toneLevel (x, from, 16384, k * f0));
        for (int k = 1; k * f0 < sr * 2.0; ++k)
        {
            const double f = k * f0, a = std::abs (f - std::round (f / sr) * sr);
            const double nearest = std::round (a / f0) * f0;
            if (f < sr * 0.5 || a > 18000.0 || std::abs (a - nearest) < 40.0)
                continue;
            worst = std::max (worst, toneLevel (x, from, 16384, a));
        }
        return db (worst / std::max (1.0e-12, top));
    };
    {
        Player p;
        orbiteOnly (p);
        p.set (snd::orShape, 1.5f);
        p.set (snd::s1Wave, 2.0f);
        p.set (snd::s1Ratio, 2.7f);
        p.set (snd::s1Ring, 1.0f);
        auto out = play (p, 0.6, { { 0.0, 96, 127 } });
        const double al = aliasDb (out, 4800, rk::noteHz (96));
        CHECK (al < -60.0, "ORBITE at C7 (square-ish carrier, square satellite synced 2.7x): loudest alias %.1f dB", al);
        std::printf ("  ORBITE C7, square x square synced: loudest alias heard %.1f dB\n", al);
    }
    {
        Player p;
        orbiteOnly (p);
        p.set (snd::s1Sync, 0.0f);
        p.set (snd::s1Ratio, 2.37f);
        p.set (snd::s1Ring, 1.0f);
        auto out = play (p, 0.6, { { 0.0, 57, 127 } });
        const double off = offHarmonic (out, 4800, rk::noteHz (57));
        CHECK (off > -20.0, "ORBITE unsynced at 2.37x: inharmonic (%.1f dB off the harmonics)", off);
    }
    {
        // ORBITE turning: the harmonics move, the pitch doesn't (480 Hz: a period of exactly 100 samples, so the
        // sound must still match itself a period later, and best there)
        Player p;
        orbiteOnly (p);
        p.set (snd::s2Ring, 1.0f);
        p.set (snd::orOrbit, 0.25f);
        p.set (snd::fine, (float) (1200.0 * std::log2 (480.0 / rk::noteHz (70))));
        auto out = play (p, 2.4, { { 0.0, 70, 127 } });
        double moved = 0.0;
        for (int k = 2; k <= 8; ++k)
            moved = std::max (moved, std::abs (db (toneLevel (out, 4800, 8192, k * 480.0)) - db (toneLevel (out, 100800, 8192, k * 480.0))));
        auto match = [&out] (int lag)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (size_t i = 52800; i < 52800 + 9600; ++i)
            {
                xy += (double) out[i] * out[i + (size_t) lag];
                xx += (double) out[i] * out[i];
                yy += (double) out[i + (size_t) lag] * out[i + (size_t) lag];
            }
            return xy / std::sqrt (xx * yy + 1.0e-30);
        };
        const double at = match (100);
        CHECK (moved > 1.0 && at > 0.98 && at > match (99) && at > match (101),
               "ORBITE at 0.25 Hz: a harmonic moved %.1f dB, the sound a period later %.4f alike", moved, at);
        std::printf ("  ORBITE turning: half a turn moves a harmonic %.1f dB, the period stays (%.4f alike)\n", moved, at);
    }
    for (int algo = 0; algo < 4; ++algo)
    {
        Player p;
        orbiteOnly (p);
        p.set (snd::orAlgo, (float) algo);
        p.set (snd::s1Ring, 1.0f);
        p.set (snd::s2Ring, 1.0f);
        p.set (snd::orOrbit, 5.0f);
        auto out = play (p, 0.5, { { 0.0, 48, 127 }, { 0.0, 55, 127 }, { 0.0, 64, 127 } });
        float peak = 0.0f;
        bool finite = true;
        for (float x : out)
        {
            finite = finite && std::isfinite (x);
            peak = std::max (peak, std::abs (x));
        }
        CHECK (finite && peak < 1.5f && rms (out, 4800, out.size()) > 0.01, "ORBITE algo %d: peak %.2f, rms %.3f", algo, peak, rms (out, 4800, out.size()));
    }
    // the SAMPLER's mix: the sample at 0, ORBITE alone; and ORBITE pushing the reading by up to INDICE samples
    {
        const int len = 96000;
        std::vector<float> ramp ((size_t) len);
        for (int i = 0; i < len; ++i)
            ramp[(size_t) i] = (float) i / (float) len;
        auto ins = instrumentOf (ramp);
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins.get());
        p.set (snd::orOn, 1.0f);
        p.set (snd::mxSample, 0.0f);
        p.set (snd::orOrbit, 0.0f);
        auto out = play (p, 0.6, { { 0.0, 60, 127 } });
        CHECK (rms (out, 2000, out.size()) > 0.02 && offHarmonic (out, 2000, rk::noteHz (60)) < -30.0,
               "SAMPLER: ORBITE alone in the mix (rms %.3f)", rms (out, 2000, out.size()));
        Player q;
        q.set (snd::engine, (float) snd::sampler);
        plain (q);
        q.setInstrument (ins.get());
        q.set (snd::pmSource, 5.0f);
        q.set (snd::pmIndex, 80.0f);
        out = play (q, 0.5, { { 0.0, 60, 127 } });
        double lo = 0.0, hi = 0.0;
        for (int i = 2000; i < 20000; ++i)
        {
            const double off = (double) out[(size_t) i] * len - (double) i;
            lo = std::min (lo, off);
            hi = std::max (hi, off);
        }
        CHECK (hi > 20.0 && lo < -20.0 && hi < 81.0 && lo > -81.0, "ORBITE as PM source: the reading moved %.1f .. %.1f samples", lo, hi);
    }
}

//==============================================================================
// SUB: a sine on the note (never under 25 Hz), the sample an octave down kept on its time, the crossover taking the
// sample's own lows away
static void testSub()
{
    auto sampler = [] (Player& p, Instrument* ins)
    {
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (ins);
        p.set (snd::subOn, 1.0f);
        p.set (snd::mxSub, 1.0f);
    };
    // SINE, an octave under the note played (C2 -> C1, 32.7 Hz)
    {
        auto ins = instrumentOf (sine (rk::noteHz (60), 3.0), 480, 480 + 183 * 2 * 100);
        Player p;
        sampler (p, ins.get());
        p.set (snd::subReplace, 0.0f);
        auto out = play (p, 1.0, { { 0.0, 48, 127 } });
        const double sub = db (toneLevel (out, 9600, 32768, rk::noteHz (36)));
        CHECK (sub > -26.0, "SUB SINE: C1 under a C2 at %.1f dB", sub);
        std::printf ("  SUB SINE under a C2: C1 at %.1f dB\n", sub);
    }
    // never under 25 Hz: two octaves under C0 is 4 Hz; it comes back up to C1 (32.7 Hz) and above
    {
        auto ins = instrumentOf (sine (rk::noteHz (60), 3.0), 480, 480 + 183 * 2 * 100);
        Player p;
        sampler (p, ins.get());
        p.set (snd::subOct, 1.0f);
        auto out = play (p, 1.0, { { 0.0, 12, 127 } });
        const double at = db (toneLevel (out, 9600, 32768, rk::noteHz (12) * 2.0)), under = db (toneLevel (out, 9600, 32768, rk::noteHz (12) / 4.0));
        CHECK (at > under + 20.0, "SUB at C0 -2 oct: at 32.7 Hz %.1f dB, at 4 Hz %.1f dB", at, under);
    }
    // OCTAVE: the sample (on its root) read an octave down, in time; the two heads don't beat
    {
        auto ins = instrumentOf (sine (rk::noteHz (60), 3.0, 0.5f), 0, (int) std::lround (sr / rk::noteHz (60) * 560));
        Player p;
        sampler (p, ins.get());
        p.set (snd::subMode, 1.0f);
        p.set (snd::subXover, 250.0f);
        p.set (snd::mxSample, 0.0f);
        auto out = play (p, 1.2, { { 0.0, 60, 127 } });
        const double f = rk::noteHz (48);
        const double sub = db (toneLevel (out, 9600, 32768, f));
        // its level steady (no beating between the grains): windows of ten periods
        const size_t period = (size_t) std::lround (sr / f);
        double lo = 1.0e9, hi = -1.0e9;
        for (size_t w = 9600; w + 10 * period < 52000; w += period)
        {
            const double l = db (rms (out, w, w + 10 * period));
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        CHECK (sub > -20.0 && hi - lo < 0.5, "SUB OCTAVE: an octave under the sample at %.1f dB, steady within %.2f dB", sub, hi - lo);
        std::printf ("  SUB OCTAVE: the sample an octave down at %.1f dB, steady within %.2f dB\n", sub, hi - lo);
    }
    // REPLACE: a 60 Hz sample, the crossover at 120, the sub silent: its lows gone
    {
        auto ins = instrumentOf (sine (60.0, 3.0, 0.5f), 0, 800 * 150);
        Player p;
        sampler (p, ins.get());
        p.set (snd::mxSub, 0.0f);
        p.set (snd::subXover, 120.0f);
        p.set (snd::subReplace, 1.0f);
        auto out = play (p, 1.0, { { 0.0, 60, 127 } });
        const double kept = db (toneLevel (out, 9600, 32768, 60.0) / 0.5);
        CHECK (kept < -18.0, "SUB REPLACE: a 60 Hz tone under a 120 Hz crossover at %.1f dB", kept);
    }
}

// FOLLOW (0.4.5): each source heard (INPUT, SELF, SIDECHAIN, a DRUM track alone), one way up or down, or both ways
static void testFollower()
{
    // a burst on DRUM 3 (0.1 .. 0.3 s), nothing elsewhere: only a follower listening to it moves
    const int total = (int) (0.6 * sr), block = 256;
    std::vector<float> burst ((size_t) total, 0.0f);
    for (int i = (int) (0.1 * sr); i < (int) (0.3 * sr); ++i)
        burst[(size_t) i] = 0.5f * (float) std::sin (2.0 * pi * 200.0 * i / sr);
    auto run = [&] (int source, float polarity, float amount, float& atPeak, float& atRest)
    {
        Player p;
        plain (p);
        p.set (snd::m1Type, 4.0f);              // FOLLOW
        p.set (snd::m1Source, (float) source);
        p.set (snd::m1Polarity, polarity);
        p.set (snd::m1Gain, 6.0f);
        p.set (snd::m1Attack, 2.0f);
        p.set (snd::m1Release, 40.0f);
        p.set (snd::cutoff, 1000.0f);
        p.setRoute (0, snd::cutoff, amount);
        juce::AudioBuffer<float> buf (2, block);
        atPeak = atRest = -1.0f;
        for (int pos = 0; pos < total; pos += block)
        {
            buf.setSize (2, std::min (block, total - pos), false, false, true);
            buf.clear();
            p.setFollowSignal (5, burst.data() + pos);           // DRUM 3
            p.render (buf, {}, {}, nullptr, 0, false);
            if (pos + block >= (int) (0.25 * sr) && atPeak < 0.0f)
                atPeak = p.modulatedNorm (snd::cutoff);
        }
        atRest = p.modulatedNorm (snd::cutoff);
    };
    const float base = snd::toNorm (snd::cutoff, 1000.0f);
    float peak = 0.0f, rest = 0.0f;
    run (5, 1.0f, 0.4f, peak, rest);
    CHECK (peak > base + 0.3f && std::abs (rest - base) < 0.02f, "DRUM 3 heard, one way up: %.3f at the burst, %.3f after (%.3f)", peak, rest, base);
    run (5, 1.0f, -0.4f, peak, rest);
    CHECK (peak < base - 0.3f && std::abs (rest - base) < 0.02f, "one way down: %.3f at the burst, %.3f after (%.3f)", peak, rest, base);
    run (5, 0.0f, 0.3f, peak, rest);
    CHECK (peak > base + 0.25f && rest < base - 0.25f, "both ways: %.3f at the burst, %.3f in silence (%.3f)", peak, rest, base);
    run (4, 1.0f, 0.4f, peak, rest);
    CHECK (std::abs (peak - base) < 0.02f, "DRUM 2 does not hear DRUM 3: %.3f (%.3f)", peak, base);

    // SELF: the player's own voices, no host signal needed
    {
        auto tone = instrumentOf (sine (440.0, 1.0, 0.5f), 24000, 24000 + 4800);
        Player p;
        p.set (snd::engine, (float) snd::sampler);
        plain (p);
        p.setInstrument (tone.get());
        p.set (snd::m1Type, 4.0f);
        p.set (snd::m1Source, 1.0f);
        p.set (snd::m1Polarity, 1.0f);
        p.set (snd::m1Gain, 6.0f);
        p.set (snd::cutoff, 1000.0f);
        p.setRoute (0, snd::cutoff, 0.4f);
        play (p, 0.3, { { 0.0, 69, 127 } });
        const float n = p.modulatedNorm (snd::cutoff);
        CHECK (n > base + 0.2f, "SELF follows the voices: %.3f (%.3f)", n, base);
    }
}

// WTF?! LOOP (0.4.5): its length (2 to 16 draws) loops, a new seed draws another loop; LIVE never repeats
static void testWtfLoop()
{
    auto draws = [] (float mode, int len, uint32_t seed, int count)
    {
        Player p;
        plain (p);
        p.set (snd::wtf, 1.0f);
        p.set (snd::wtfMode, mode);
        p.set (snd::wtfLen, (float) len);
        p.wtfSeed = seed;
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, 256);
        Player::Transport t;
        t.playing = true;
        t.bpm = 120.0;
        const int eighth = (int) (sr * 0.25);
        for (int pos = 0; pos < eighth * count; pos += 256)
        {
            t.ppq = (double) pos / sr * 2.0;
            buf.clear();
            p.render (buf, {}, t, nullptr, 0, false);
            if ((pos / 256) % (eighth / 256) == (eighth / 256) - 2)
                out.push_back (p.wtfPulls()[0]);       // (late in each eighth: glided there)
        }
        return out;
    };
    auto a = draws (1.0f, 3, 0x7f4a7c15u, 9);
    bool loops = a.size() == 9;
    for (size_t i = 3; i < a.size(); ++i)
        loops = loops && std::abs (a[i] - a[i - 3]) < 1.0e-3f;
    CHECK (loops && std::abs (a[0] - a[1]) > 1.0e-3f, "LOOP of 3 comes back every 3 eighths");
    auto b = draws (1.0f, 3, 12345u, 3);
    CHECK (b.size() == 3 && std::abs (a[0] - b[0]) + std::abs (a[1] - b[1]) > 1.0e-2f, "a new seed, another loop");
    auto c = draws (0.0f, 3, 0x7f4a7c15u, 9);
    bool repeats = true;
    for (size_t i = 3; i < c.size(); ++i)
        repeats = repeats && std::abs (c[i] - c[i - 3]) < 1.0e-3f;
    CHECK (! repeats, "LIVE draws anew");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (std::getenv ("RK_PROBE") != nullptr) { probe(); return 0; }
    if (std::getenv ("RK_MEMCHECK") != nullptr)
    {
        // (under valgrind: a short default sound, then the phase modulation)
        const juce::String which (std::getenv ("RK_MEMCHECK"));
        if (which.contains ("d")) testDefaultSound();
        if (which.contains ("g")) testGuard();
        if (which.contains ("r")) testResample();
        if (which.contains ("a")) { testAntiAliasing(); testAliasingScale(); }
        Player p;
        p.prepare (44100.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < 40; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            buf.clear();
            p.render (buf, midi, {}, nullptr, 0, false);
        }
        testPhaseModulation();
        std::printf ("%d checks, %d failed\n", checks, failures);
        return 0;
    }
    if (std::getenv ("RK_SKIP_NEW") == nullptr)
    {
        testDefaultSound();
        testGuard();
    }
    testResample();
    testAntiAliasing();
    testAliasingScale();
    testPhaseModulation();
    testReadingEngine();
    testOrbite();
    testSub();
    testReverseElasticLoops();
    testFarPitch();
    testScenes();
    testVoices();
    testFilterModsFx();
    testFollower();
    testWtfLoop();
    testSmoothing();
    testSynth();
    testStability();
    testClock();
    testCpu();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
