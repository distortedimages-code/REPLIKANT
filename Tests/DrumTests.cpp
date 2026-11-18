// DRUM (0.4) on its own: the sequencer to the sample (steps, swing, micro-timing, probability, ratchets, retrigs and
// their curve, speeds and lengths, p-locks, a pattern change at the boundary, a chain), the keys, the choke, the
// sources (a sample as it is, a take's slices chosen by step, the synth), and a storm of random settings.

#include "../Source/drum/DrumCore.h"
#include "../Source/radio/RadioSlicer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace drum;
static const double sr = 48000.0;

// a sound that is one click: each hit of a track playing it shows as a single sample, where the hit falls
static std::shared_ptr<radio::Take::Audio> clickAudio()
{
    auto a = std::make_shared<radio::Take::Audio>();
    a->sampleRate = sr;
    a->l.assign (2400, 0.0f);
    a->l[0] = 1.0f;
    a->r = a->l;
    return a;
}

struct Rig
{
    Engine e;
    Sound click;
    std::vector<float> L, R;
    Rig()
    {
        e.allocate();
        e.prepare (sr, 512);
        e.setBanks (dsp::CycleBank::basics().get(), nullptr);
        click.audio = clickAudio();
        for (int t = 0; t < numTracks; ++t)
            e.setSound (t, &click);
        auto& p = e.pattern (0);
        for (int t = 0; t < numTracks; ++t)
        {
            auto& tk = p.tr[t];
            tk.reset (t);
            tk.p[pSource] = sample;
            tk.p[pNoise] = 0.0f;
            tk.p[pClick] = 0.0f;
            tk.p[pBody] = 1.0f;
            tk.p[pDecay] = 1.0f;
            tk.p[pFilter] = 0; tk.p[pCutoff] = 20000.0f; tk.p[pReso] = 0.0f;
            tk.p[pDrive] = 0.0f;
            tk.p[pPan] = 0.0f;
            tk.p[pChoke] = 0;
            tk.p[pNote] = 60; tk.p[pTune] = 0.0f; tk.p[pSweep] = 0.0f; tk.p[pPmod] = 0.0f; tk.p[pAttack] = 0.0f; tk.p[pStart] = 0.0f;
        }
    }
    // seconds of playing from the start of the song (bpm), keys at their sample
    void play (double seconds, double bpm = 120.0, float swing = 0.5f, std::vector<Engine::Event> keys = {}, bool running = true, int block = 512)
    {
        L.clear();
        R.clear();
        std::vector<float> l ((size_t) block), r ((size_t) block);
        double ppq = 0.0;
        const int total = (int) (seconds * sr);
        for (int pos = 0; pos < total; pos += block)
        {
            Engine::Clock c;
            c.running = running;
            c.ppq = ppq;
            c.bpm = bpm;
            std::vector<Engine::Event> ev;
            for (auto k : keys)
                if (k.at >= pos && k.at < pos + block)
                    ev.push_back ({ k.at - pos, k.note, k.velocity });
            e.render (l.data(), r.data(), block, c, swing, ev.data(), (int) ev.size());
            L.insert (L.end(), l.begin(), l.end());
            R.insert (R.end(), r.begin(), r.end());
            ppq += (double) block / sr * bpm / 60.0;
        }
    }
    // where the clicks are (samples)
    std::vector<int> clicks (float above = 1.0e-3f) const
    {
        std::vector<int> at;
        for (size_t i = 0; i < L.size(); ++i)
            if (std::abs (L[i]) > above)
                at.push_back ((int) i);
        return at;
    }
};

static int near (const std::vector<int>& at, double want, int tol = 1)
{
    for (int a : at)
        if (std::abs (a - want) <= tol)
            return a;
    return -1;
}

//==============================================================================
static void testParams()
{
    CHECK (text (pDecay, 1.0f) == "HOLD", "%s", text (pDecay, 1.0f).c_str());
    CHECK (text (pRetrig, 6.0f) == "1/16", "%s", text (pRetrig, 6.0f).c_str());
    CHECK (text (pNote, 60.0f) == "C3", "%s", text (pNote, 60.0f).c_str());
    CHECK (std::abs (retrigPpq (6) - 0.25f) < 1.0e-6f, "1/16 = a quarter of a quarter note");
    CHECK (std::abs (fromNorm (pCutoff, toNorm (pCutoff, 1000.0f)) - 1000.0f) < 0.5f, "cutoff round trip");
    Pattern p;
    p.reset();
    CHECK (p.length16() == 16, "a fresh pattern: %d", p.length16());
    p.tr[3].len = 64;
    CHECK (p.length16() == 64, "AUTO: its longest track: %d", p.length16());
    p.tr[3].speed = 6;     // 2x
    p.tr[5].len = 48;
    CHECK (p.length16() == 48, "%d", p.length16());
}

static void testSteps()
{
    // 120 bpm: a sixteenth is 0.125 s (6000 samples)
    Rig g;
    auto& tk = g.e.pattern (0).tr[0];
    for (int s : { 0, 4, 8, 12 })
        tk.trig[s] = 1;
    g.play (4.0);
    auto at = g.clicks();
    CHECK (at.size() == 8, "%zu hits in two bars", at.size());
    bool exact = true;
    for (int k = 0; k < 8 && k < (int) at.size(); ++k)
        exact = exact && at[(size_t) k] == k * 24000;
    CHECK (exact, "on the beat, to the sample: %d %d %d", at.size() > 1 ? at[1] : -1, at.size() > 2 ? at[2] : -1, at.size() > 7 ? at[7] : -1);
    // the same in blocks of 64 and 1000 (the sample doesn't move with the block)
    for (int block : { 64, 1000 })
    {
        g.play (2.0, 120.0, 0.5f, {}, true, block);
        auto b = g.clicks();
        CHECK (b.size() == 4 && b[1] == 24000 && b[3] == 72000, "blocks of %d: %zu hits, %d", block, b.size(), b.size() > 1 ? b[1] : -1);
    }
    // stopped: nothing
    g.play (1.0, 120.0, 0.5f, {}, false);
    CHECK (g.clicks().empty(), "stopped: silent");
    // MICRO: half a step late on step 4
    tk.lock[4][pMicro] = 0.5f;
    g.play (1.0);
    at = g.clicks();
    CHECK (near (at, 24000 + 3000) >= 0 && near (at, 24000) < 0, "micro +1/2: %d", at.size() > 1 ? at[1] : -1);
    tk.lock[4][pMicro] = NAN;
    // SWING 75 %: every second sixteenth half a step late
    tk.trig[1] = 1;
    g.play (0.5, 120.0, 0.75f);
    at = g.clicks();
    CHECK (near (at, 6000 + 3000) >= 0, "swing: the second sixteenth at %d", at.size() > 1 ? at[1] : -1);
    tk.trig[1] = 0;
    // PROB: 0 never, 0.5 about half
    tk.lock[0][pProb] = 0.0f;
    g.play (4.0);
    at = g.clicks();
    CHECK (near (at, 0) < 0 && near (at, 96000) < 0 && near (at, 24000) >= 0, "prob 0: never");
    for (int s : { 0, 4, 8, 12 })
        tk.lock[s][pProb] = 0.5f;
    g.play (32.0);
    const int n = (int) g.clicks().size();
    CHECK (n > 20 && n < 44, "prob 50 %%: %d of 64", n);
    for (int s : { 0, 4, 8, 12 })
        tk.lock[s][pProb] = NAN;
    // a p-lock: LEVEL 0 on step 8 (silent there only)
    tk.lock[8][pLevel] = 0.0f;
    g.play (2.0);
    at = g.clicks();
    CHECK (near (at, 24000) >= 0 && near (at, 72000) >= 0 && near (at, 48000) < 0, "level locked to 0 on step 8: %zu hits", at.size());
    tk.lock[8][pLevel] = NAN;
    CHECK (! tk.locked (8), "the lock gone");
    // SPEED 2x, LENGTH 3: steps 0 and 2 of a 3-step track, twice as fast (an eighth of a sixteenth: 3000 samples)
    for (int s : { 0, 4, 8, 12 })
        tk.trig[s] = 0;
    tk.trig[0] = 1;
    tk.trig[2] = 1;
    tk.len = 3;
    tk.speed = 6;
    g.play (0.5);
    at = g.clicks();
    // steps 0, 2, (3=0) 3, 5, (6=0) 6, 8 ...: at 0, 2, 3, 5, 6, 8 x 3000
    CHECK (at.size() >= 6 && at[1] == 6000 && at[2] == 9000 && at[3] == 15000, "polymetric: %d %d %d", at.size() > 1 ? at[1] : -1, at.size() > 2 ? at[2] : -1, at.size() > 3 ? at[3] : -1);
}

static void testRolls()
{
    Rig g;
    auto& tk = g.e.pattern (0).tr[0];
    tk.trig[0] = 1;
    // a ratchet of 4: four hits in the step (1500 samples apart)
    tk.rat[0] = 4;
    g.play (0.3);
    auto at = g.clicks();
    CHECK (at.size() == 4 && at[1] == 1500 && at[3] == 4500, "ratchet 4: %zu hits, %d %d", at.size(), at.size() > 1 ? at[1] : -1, at.size() > 3 ? at[3] : -1);
    // across blocks of 64 too
    g.play (0.3, 120.0, 0.5f, {}, true, 64);
    at = g.clicks();
    CHECK (at.size() == 4 && at[3] == 4500, "ratchet in blocks of 64: %zu", at.size());
    tk.rat[0] = 1;
    // a retrig of 1/32, three more (3000 samples apart)
    tk.p[pRetrig] = 8;
    tk.p[pCount] = 3;
    g.play (0.5);
    at = g.clicks();
    CHECK (at.size() == 4 && at[1] == 3000 && at[3] == 9000, "retrig 1/32 x3: %zu, %d %d", at.size(), at.size() > 1 ? at[1] : -1, at.size() > 3 ? at[3] : -1);
    // its curve: the gaps close in, the last hit where the even roll ends
    tk.p[pCurve] = 1.0f;
    g.play (0.5);
    at = g.clicks();
    CHECK (at.size() == 4 && std::abs (at[3] - 9000) <= 2 && at[1] > 3000 && (at[3] - at[2]) < (at[1] - at[0]), "curve: %d %d %d", at.size() > 1 ? at[1] : -1,
           at.size() > 2 ? at[2] : -1, at.size() > 3 ? at[3] : -1);
    tk.p[pCurve] = 0.0f;
    // its velocities: falling (each hit quieter)
    tk.p[pRVel] = 0.75f;
    g.play (0.5);
    at = g.clicks();
    if (at.size() == 4)
        CHECK (std::abs (g.L[(size_t) at[3]]) < std::abs (g.L[(size_t) at[1]]) && std::abs (g.L[(size_t) at[1]]) < std::abs (g.L[(size_t) at[0]]), "falling velocities");
    tk.p[pRVel] = 0.0f;
    // a retrig longer than a block, a step later: the roll stops where the next hit comes
    tk.p[pRetrig] = 6;      // 1/16
    tk.p[pCount] = 16;
    tk.trig[2] = 1;
    g.play (0.5);
    at = g.clicks();
    CHECK (near (at, 6000) >= 0 && near (at, 12000) >= 0 && near (at, 18000) >= 0, "a roll and the next step: %zu hits", at.size());
}

static void testPatterns()
{
    Rig g;
    auto& e = g.e;
    e.pattern (0).tr[0].trig[0] = 1;
    // pattern 2 (A02): step 8
    e.queue (1);              // (stopped: at once)
    CHECK (e.playingPattern() == 1 && e.pattern (1).used, "stopped: the pattern at once");
    for (int t = 0; t < numTracks; ++t)
    {
        e.pattern (1).tr[t].p[pSource] = sample;
        e.pattern (1).tr[t].p[pNoise] = 0.0f;
        e.pattern (1).tr[t].p[pClick] = 0.0f;
        e.pattern (1).tr[t].p[pDecay] = 1.0f;
        e.pattern (1).tr[t].p[pFilter] = 0; e.pattern (1).tr[t].p[pCutoff] = 20000.0f; e.pattern (1).tr[t].p[pReso] = 0.0f;
        e.pattern (1).tr[t].p[pDrive] = 0.0f; e.pattern (1).tr[t].p[pChoke] = 0;
        e.pattern (1).tr[t].p[pNote] = 60; e.pattern (1).tr[t].p[pSweep] = 0.0f; e.pattern (1).tr[t].p[pPmod] = 0.0f;
    }
    e.pattern (1).tr[0].trig[8] = 1;
    e.queue (0);
    // playing A01; A02 queued at 1 s: it comes at the end of A01 (2 s), its step 8 at 3 s
    std::vector<float> l (512), r (512);
    std::vector<int> at;
    double ppq = 0.0;
    for (int pos = 0; pos < (int) (5.0 * sr); pos += 512)
    {
        if (pos == 48128)
            e.queue (1);
        Engine::Clock c { true, ppq, 120.0 };
        e.render (l.data(), r.data(), 512, c, 0.5f, nullptr, 0);
        for (int i = 0; i < 512; ++i)
            if (std::abs (l[(size_t) i]) > 1.0e-3f)
                at.push_back (pos + i);
        ppq += 512.0 / sr * 2.0;
    }
    // A01 at 0; A02 from 2 s: its step 8 at 3 s, and again at 5 s (out of the window); A01's step 0 not at 2 s
    CHECK (near (at, 0) >= 0 && near (at, 96000) < 0 && near (at, 144000) >= 0, "the change at the boundary: %zu hits (%d %d)", at.size(),
           at.size() > 0 ? at[0] : -1, at.size() > 1 ? at[1] : -1);
    CHECK (e.playingPattern() == 1, "A02 playing");
    // a chain A01, A02: they take turns
    e.queue (0);
    e.setChain ({ 0, 1 });
    g.play (8.0);
    at = g.clicks();
    // (from the song's start: A01 0..2 s (hit at 0), A02 2..4 (hit at 3), A01 4..6 (hit at 4), A02 6..8 (hit at 7))
    CHECK (near (at, 0) >= 0 && near (at, 144000) >= 0 && near (at, 192000) >= 0 && near (at, 336000) >= 0 && at.size() == 4,
           "a chain: %zu hits", at.size());
    e.setChain ({});
}

static void testKeysAndChoke()
{
    Rig g;
    // C1 + 2: track 3, at its sample, at its velocity
    g.play (0.2, 120.0, 0.5f, { { 1234, firstNote + 2, 127 }, { 5000, firstNote - 1, 127 }, { 6000, firstNote + 8, 127 } }, false);
    auto at = g.clicks();
    CHECK (at.size() == 1 && at[0] == 1234, "a key: %zu hits at %d", at.size(), at.empty() ? -1 : at[0]);
    CHECK (g.e.glow[2].load() > 0.5f, "its track lit");
    // CHOKE: tracks 3 and 4 in group A, a long sound; 4 struck after 3 cuts it within 5 ms
    auto tone = std::make_shared<radio::Take::Audio>();
    tone->sampleRate = sr;
    tone->l.resize (48000);
    for (size_t i = 0; i < tone->l.size(); ++i) tone->l[i] = 0.5f * (float) std::sin (6.283185307 * 300.0 * (double) i / sr);
    tone->r = tone->l;
    Sound s;
    s.audio = tone;
    g.e.setSound (2, &s);
    auto& p = g.e.pattern (0);
    p.tr[2].p[pChoke] = 1;
    p.tr[3].p[pChoke] = 1;
    p.tr[3].p[pBody] = 0.0f;          // (track 4 itself silent: only what's left of track 3 is heard)
    g.play (0.3, 120.0, 0.5f, { { 0, firstNote + 2, 127 }, { 4800, firstNote + 3, 127 } }, false);
    double before = 0, after = 0;
    for (int i = 2400; i < 4800; ++i) before = std::max (before, (double) std::abs (g.L[(size_t) i]));
    for (int i = 4800 + 300; i < 9600; ++i) after = std::max (after, (double) std::abs (g.L[(size_t) i]));
    CHECK (before > 0.1 && after < 1.0e-4, "choked: %.3f -> %.6f", before, after);
}

static void testSources()
{
    Rig g;
    // SAMPLE: the sound as it is (velocity 127: the level's gain), from START, backwards
    auto a = std::make_shared<radio::Take::Audio>();
    a->sampleRate = sr;
    a->l.resize (4800);
    for (size_t i = 0; i < a->l.size(); ++i) a->l[i] = 0.4f * (float) std::sin (6.283185307 * 440.0 * (double) i / sr);
    a->r = a->l;
    Sound s;
    s.audio = a;
    g.e.setSound (0, &s);
    auto& tk = g.e.pattern (0).tr[0];
    tk.p[pLevel] = 0.5f;
    g.play (0.1, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
    double err = 0;
    for (int i = 10; i < 4700; ++i)
        err = std::max (err, (double) std::abs (g.L[(size_t) i] - a->l[(size_t) i] * 0.5f * 1.2f));
    CHECK (err < 1.0e-4, "a sample as it is: %g", err);
    // TUNE +12: twice as fast (its end at half the time)
    tk.p[pTune] = 12.0f;
    g.play (0.1, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
    int last = 0;
    for (int i = 0; i < 4800; ++i) if (std::abs (g.L[(size_t) i]) > 1.0e-4f) last = i;
    CHECK (std::abs (last - 2400) < 30, "tune +12: ends at %d", last);
    tk.p[pTune] = 0.0f;
    // SLICES: a take of three hits, SLICE locked per step
    std::vector<double> hitsAt { 0.1, 0.4, 0.7 };
    auto take = std::make_shared<radio::Take::Audio>();
    take->sampleRate = sr;
    take->l.assign ((size_t) sr, 0.0f);
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (size_t h = 0; h < hitsAt.size(); ++h)
        for (int i = 0; i < 4000; ++i)
            take->l[(size_t) (hitsAt[h] * sr) + (size_t) i] = (0.3f + 0.3f * (float) h) * u (rng) * std::exp (-(float) i / 600.0f);
    take->r = take->l;
    Sound sl;
    sl.audio = take;
    sl.slices = radio::slice (take->l.data(), take->r.data(), (int) take->l.size(), sr, 16, 0.5f);
    CHECK (sl.slices.size() == 3, "%zu slices", sl.slices.size());
    g.e.setSound (1, &sl);
    auto& t2 = g.e.pattern (0).tr[1];
    t2.p[pSource] = slices;
    t2.trig[0] = 1; t2.trig[4] = 1; t2.trig[8] = 1;
    t2.lock[4][pSlice] = 2;
    t2.lock[8][pSlice] = 1;
    g.play (1.5);
    auto peakAround = [&] (int at) { double m = 0; for (int i = at; i < at + 3000; ++i) m = std::max (m, (double) std::abs (g.L[(size_t) i])); return m; };
    const double p0 = peakAround (0), p1 = peakAround (24000), p2 = peakAround (48000);
    CHECK (p0 < p2 && p2 < p1, "slices 1, 3, 2 by step: %.2f %.2f %.2f", p0, p1, p2);
    // SYNTH: the kit's kick, its sweep falling to its note (65 Hz two octaves under C3)
    Engine e2;
    e2.allocate();
    e2.prepare (sr, 512);
    e2.setBanks (dsp::CycleBank::basics().get(), nullptr);
    std::vector<float> l (512), r (512), out;
    for (int b = 0; b < 60; ++b)
    {
        Engine::Event k { 0, firstNote, 120 };
        e2.render (l.data(), r.data(), 512, {}, 0.5f, &k, b == 0 ? 1 : 0);
        out.insert (out.end(), l.begin(), l.end());
    }
    double first = 0, late = 0;
    for (int i = 0; i < 4800; ++i) first = std::max (first, (double) std::abs (out[(size_t) i]));
    for (int i = (int) out.size() - 4800; i < (int) out.size(); ++i) late = std::max (late, (double) std::abs (out[(size_t) i]));
    CHECK (first > 0.2 && late < first * 0.05, "the kick hits and dies: %.3f -> %.4f", first, late);
    // its pitch at the end of the sweep: zero crossings between 0.2 and 0.3 s, about 65 Hz
    int zc = 0;
    for (int i = 9600; i < 14400; ++i) zc += (out[(size_t) i - 1] < 0.0f) != (out[(size_t) i] < 0.0f) ? 1 : 0;
    const double hz = zc / 2.0 / 0.1;
    CHECK (std::abs (hz - 65.4) < 8.0, "the kick settles near 65 Hz: %.1f", hz);
}

static void testStorm()
{
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    float top = 0.0f;
    bool finite = true;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Engine e;
        e.allocate();
        e.prepare (rate, 512);
        e.setBanks (dsp::CycleBank::basics().get(), nullptr);
        auto a = std::make_shared<radio::Take::Audio>();
        a->sampleRate = 44100.0;
        a->l.resize (20000);
        for (size_t i = 0; i < a->l.size(); ++i) a->l[i] = 0.7f * u (rng) * std::exp (-(float) i / 3000.0f);
        a->r = a->l;
        Sound s;
        s.audio = a;
        s.slices = { { 0, 5000, 1.0f }, { 5000, 12000, 1.0f }, { 12000, 20000, 1.0f } };
        for (int t = 0; t < numTracks; ++t)
            e.setSound (t, &s);
        std::vector<float> l (512), r (512);
        double ppq = 0.0;
        for (int round = 0; round < 12; ++round)
        {
            auto& pat = e.pattern (round % 3);
            for (int t = 0; t < numTracks; ++t)
            {
                auto& tk = pat.tr[t];
                for (int p = 0; p < numParams; ++p)
                    tk.p[p] = fromNorm (p, u (rng));
                tk.p[pLevel] = 0.3f + 0.4f * u (rng);
                tk.len = (uint8_t) (1 + (int) (u (rng) * 63.0f));
                tk.speed = (uint8_t) (u (rng) * 6.99f);
                for (int st = 0; st < numSteps; ++st)
                {
                    tk.trig[st] = u (rng) < 0.3f ? 1 : 0;
                    tk.rat[st] = (uint8_t) (u (rng) < 0.1f ? 2 + (int) (u (rng) * 14.0f) : 1);
                    if (u (rng) < 0.1f)
                        tk.lock[st][(int) (u (rng) * (numParams - 0.01f))] = 0.5f;
                }
            }
            e.queue (round % 3);
            const double bpm = 60.0 + 200.0 * u (rng);
            for (int b = 0; b < (int) (0.5 * rate / 512); ++b)
            {
                Engine::Event k { (int) (u (rng) * 500.0f), firstNote + (int) (u (rng) * 8.0f), 100 };
                e.render (l.data(), r.data(), 512, { u (rng) > 0.1f, ppq, bpm }, 0.5f + 0.25f * u (rng), &k, u (rng) < 0.2f ? 1 : 0);
                ppq += 512.0 / rate * bpm / 60.0;
                for (int i = 0; i < 512; ++i)
                {
                    finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                    top = std::max (top, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
                }
            }
        }
    }
    CHECK (finite && top < 12.0f, "a storm of settings: peak %.2f", top);
    std::printf ("  storm: peak %.2f\n", top);
}

static void testCpu()
{
    Engine e;
    e.allocate();
    e.prepare (sr, 512);
    e.setBanks (dsp::CycleBank::basics().get(), nullptr);
    auto& p = e.pattern (0);
    for (int t = 0; t < numTracks; ++t)
    {
        for (int s = 0; s < 16; ++s) p.tr[t].trig[s] = 1;
        p.tr[t].p[pPmod] = 0.4f;
        p.tr[t].p[pNoise] = 0.5f;
        p.tr[t].p[pFilter] = 0; p.tr[t].p[pCutoff] = 3000.0f; p.tr[t].p[pReso] = 0.4f; p.tr[t].p[pDrive] = 0.5f;
        p.tr[t].p[pDecay] = 0.7f;
    }
    std::vector<float> l (512), r (512);
    double ppq = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < (int) (10.0 * sr / 512); ++b)
    {
        e.render (l.data(), r.data(), 512, { true, ppq, 140.0 }, 0.5f, nullptr, 0);
        ppq += 512.0 / sr * 140.0 / 60.0;
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  cpu: eight synth tracks on every sixteenth, all their settings on: %.2f %% of real time\n", secs / 10.0 * 100.0);
    CHECK (secs / 10.0 < 0.08, "%.3f", secs / 10.0);
}

int main()
{
    testParams();
    testSteps();
    testRolls();
    testPatterns();
    testKeysAndChoke();
    testSources();
    testStorm();
    testCpu();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
