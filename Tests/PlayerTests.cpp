// The player on made-up samples, measured: the anti-aliasing that follows the pitch, the ALIASING scale (continuous,
// up to aliases louder than the sound), the phase modulation (the read address moved by so many samples), reverse,
// elastic, moved loops, bits, MONO / LEGATO, the polyphony, the filters, the modulators, the FX mode, stability, CPU.

#include <JuceHeader.h>
#include "../Source/engine/Player.h"
#include "../Source/core/Analysis.h"
#include "../Source/core/Sinc.h"

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
    plain (p);
    p.setInstrument (mid.get());
    p.set (snd::alias, 1.0f);
    auto out = play (p, 0.3, { { 0.0, 67, 127 } });
    const double f = 9000.0 * std::pow (2.0, 7.0 / 12.0);
    const double tone = toneLevel (out, 4800, 8192, f), image = toneLevel (out, 4800, 8192, 48000.0 - f);
    CHECK (image > tone, "at 100 %% the image (%.1f dB) is louder than the tone (%.1f dB)", db (image / 0.5), db (tone / 0.5));
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
    for (int i = 2000; i < 2000 + 24000; ++i)
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
    auto ins = instrumentOf (two);
    ins->prepareForPlaying();
    Player p;
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
        plain (q);
        q.setInstrument (ins.get());
        q.set (snd::fine, 1.0f);      // (moved a little: interpolated, not the exact copy)
        const auto t0 = std::chrono::steady_clock::now();
        play (q, 1.0, { { 0.0, note, 127 } });
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };
    const double flat = timeOf (60), far = timeOf (96);
    CHECK (far < flat * 1.6, "+3 octaves costs %.2fx an unshifted voice", far / flat);

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
        plain (p);
        p.setInstrument (ins.get());
        auto out = play (p, 0.5, { { 0.0, 60, 127 } });
        open = toneLevel (out, 4800, 8192, 110.0 * 40);
    }
    const char* names[] = { "", "MOOG", "MS-20", "SEM", "OSCAR", "WASP", "POLIVOKS" };
    for (int model = 1; model <= 6; ++model)
    {
        Player p;
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
        CHECK (took / 5.0 < 0.6, "CPU %.0f %%", took / 5.0 * 100.0);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    testAntiAliasing();
    testAliasingScale();
    testPhaseModulation();
    testReverseElasticLoops();
    testFarPitch();
    testScenes();
    testVoices();
    testFilterModsFx();
    testStability();
    testCpu();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
