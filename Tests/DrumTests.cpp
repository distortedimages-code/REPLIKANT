// DRUM (0.4) on its own: the sequencer to the sample (steps, swing, micro-timing, probability, ratchets, retrigs and
// their curve, speeds and lengths, p-locks, a pattern change at the boundary, a chain), the keys, the choke, the
// sources (a sample as it is, a take's slices chosen by step, the synth), and a storm of random settings.

#include <tuple>
#include "../Source/drum/DrumCore.h"
#include "../Source/drum/DrumTables.h"
#include "../Source/radio/RadioSlicer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include <numeric>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <string>

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
    // (0.4.12) STEREO (on by default): a sound's two sides kept apart; off, the two summed (the middle of both)
    {
        auto st = std::make_shared<radio::Take::Audio>();
        st->sampleRate = sr;
        st->l = a->l;
        st->r.assign (a->l.size(), 0.0f);
        Sound ss;
        ss.audio = st;
        g.e.setSound (1, &ss);
        auto& t1 = g.e.pattern (0).tr[1];
        t1.p[pLevel] = 0.5f;
        CHECK (t1.p[pStereo] > 0.5f, "stereo by default: %g", (double) t1.p[pStereo]);
        g.play (0.1, 120.0, 0.5f, { { 0, firstNote + 1, 127 } }, false);
        double el = 0, pr = 0;
        for (int i = 10; i < 4700; ++i)
        {
            el = std::max (el, (double) std::abs (g.L[(size_t) i] - st->l[(size_t) i] * 0.5f * 1.2f));
            pr = std::max (pr, (double) std::abs (g.R[(size_t) i]));
        }
        CHECK (el < 1.0e-4 && pr < 1.0e-6, "stereo: left as it is (%g), right silent (%g)", el, pr);
        t1.p[pStereo] = 0.0f;
        g.play (0.1, 120.0, 0.5f, { { 0, firstNote + 1, 127 } }, false);
        double em = 0;
        for (int i = 10; i < 4700; ++i)
            em = std::max (em, std::max (std::abs ((double) g.L[(size_t) i] - st->l[(size_t) i] * 0.25 * 1.2),
                                         std::abs ((double) g.R[(size_t) i] - st->l[(size_t) i] * 0.25 * 1.2)));
        CHECK (em < 1.0e-4, "mono: both sides the middle of the two: %g", em);
        // PAN, in stereo, a balance: hard left, the right side gone, the left as it was
        t1.p[pStereo] = 1.0f;
        st->r = st->l;
        t1.p[pPan] = -1.0f;
        g.play (0.1, 120.0, 0.5f, { { 0, firstNote + 1, 127 } }, false);
        double bl = 0, br = 0;
        for (int i = 10; i < 4700; ++i)
        {
            bl = std::max (bl, (double) std::abs (g.L[(size_t) i] - st->l[(size_t) i] * 0.5f * 1.2f));
            br = std::max (br, (double) std::abs (g.R[(size_t) i]));
        }
        CHECK (bl < 1.0e-4 && br < 1.0e-6, "stereo balance: %g %g", bl, br);
        t1.p[pPan] = 0.0f;
        g.e.setSound (1, &g.click);
    }
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


// the filter's envelope: a bright noise through a low pass at 200 Hz, opened six octaves at the hit and closing in
// tens of milliseconds (bright at first, dark after); the other way, an open filter closed at the hit (dark, then bright)
static void testFilterEnvelope()
{
    auto run = [] (float cutoff, float amount, float fdec)
    {
        Engine e;
        e.allocate();
        e.prepare (sr, 512);
        e.setBanks (dsp::CycleBank::basics().get(), nullptr);
        auto& tk = e.pattern (0).tr[0];
        tk.reset (0);
        tk.p[pSweep] = 0.0f; tk.p[pClick] = 0.0f; tk.p[pBody] = 0.0f; tk.p[pDrive] = 0.0f;
        tk.p[pNoise] = 1.0f; tk.p[pTone] = 1.0f; tk.p[pDecay] = 1.0f;
        tk.p[pFilter] = 0; tk.p[pCutoff] = cutoff; tk.p[pReso] = 0.0f;
        tk.p[pFEnv] = amount; tk.p[pFAttack] = 0.0f; tk.p[pFDecay] = fdec;
        std::vector<float> l (512), r (512), out;
        for (int b = 0; b < 40; ++b)
        {
            Engine::Event k { 0, firstNote, 127 };
            e.render (l.data(), r.data(), 512, {}, 0.5f, &k, b == 0 ? 1 : 0);
            out.insert (out.end(), l.begin(), l.end());
        }
        // the brightness of a stretch: its slope's level against its own
        auto bright = [&out] (int from, int len)
        {
            double d = 0.0, a = 0.0;
            for (int i = from; i < from + len; ++i)
            {
                const double x = out[(size_t) i], dx = x - out[(size_t) i - 1];
                d += dx * dx;
                a += x * x;
            }
            return std::sqrt (d / std::max (1.0e-12, a));
        };
        bool finite = true;
        for (float x : out)
            finite = finite && std::isfinite (x);
        return std::make_tuple (bright (24, 480), bright (14400, 4800), finite);
    };
    const auto [upEarly, upLate, upOk] = run (200.0f, 1.0f, 0.3f);
    const auto [stillEarly, stillLate, stillOk] = run (200.0f, 0.0f, 0.3f);
    const auto [downEarly, downLate, downOk] = run (20000.0f, -1.0f, 0.3f);
    std::printf ("  filter envelope: up %.3f -> %.3f (none %.3f -> %.3f), down %.3f -> %.3f\n", upEarly, upLate, stillEarly, stillLate, downEarly, downLate);
    CHECK (upOk && stillOk && downOk, "numbers only");
    CHECK (upEarly > 4.0 * stillEarly && upLate < 1.5 * stillLate, "opened at the hit, closed after: %.3f / %.3f, %.3f / %.3f", upEarly, stillEarly, upLate, stillLate);
    CHECK (downEarly * 4.0 < downLate, "closed at the hit, opened after: %.3f -> %.3f", downEarly, downLate);
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
        p.tr[t].p[pFEnv] = 0.5f;
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
    std::printf ("  cpu: eight synth tracks on every sixteenth, all their settings on (filter envelopes too): %.2f %% of real time\n", secs / 10.0 * 100.0);
    CHECK (secs / 10.0 < 0.08, "%.3f", secs / 10.0);
}

// (0.4.7) the mods on a track: taken at the hit (LEVEL down to nothing: silence), followed while it rings (the
// cutoff closed by them halfway through a long noise), and what is turned on the track while it rings, followed too
static void testMods()
{
    auto run = [] (float levelMod, float cutMod, float turnedCutoff)
    {
        Engine e;
        e.allocate();
        e.prepare (sr, 512);
        e.setBanks (dsp::CycleBank::basics().get(), nullptr);
        auto& tk = e.pattern (0).tr[0];
        tk.reset (0);
        tk.p[pSweep] = 0.0f; tk.p[pClick] = 0.0f; tk.p[pBody] = 0.0f; tk.p[pDrive] = 0.0f;
        tk.p[pNoise] = 1.0f; tk.p[pTone] = 1.0f; tk.p[pDecay] = 1.0f;
        tk.p[pFilter] = 0; tk.p[pCutoff] = 20000.0f; tk.p[pReso] = 0.0f; tk.p[pFEnv] = 0.0f;
        std::vector<float> mods ((size_t) numTracks * Engine::modStride, 0.0f);
        mods[(size_t) pLevel] = levelMod;
        e.setMods (mods.data());
        std::vector<float> l (512), r (512), out;
        for (int b = 0; b < 40; ++b)
        {
            if (b == 20)
            {
                mods[(size_t) pCutoff] = cutMod;
                e.setMods (mods.data());
                if (turnedCutoff > 0.0f)
                    tk.p[pCutoff] = turnedCutoff;
            }
            Engine::Event k { 0, firstNote, 127 };
            e.render (l.data(), r.data(), 512, {}, 0.5f, &k, b == 0 ? 1 : 0);
            out.insert (out.end(), l.begin(), l.end());
        }
        auto bright = [&out] (int from, int len)
        {
            double d = 0.0, a = 0.0;
            for (int i = from; i < from + len; ++i)
            {
                const double x = out[(size_t) i], dx = x - out[(size_t) i - 1];
                d += dx * dx;
                a += x * x;
            }
            return std::sqrt (d / std::max (1.0e-12, a));
        };
        double e2 = 0.0;
        for (int i = 0; i < 4096; ++i)
            e2 += (double) out[(size_t) i] * out[(size_t) i];
        return std::make_tuple (std::sqrt (e2 / 4096.0), bright (8192, 2048), bright (16384, 2048));
    };
    const auto [loud, b0, b1] = run (0.0f, 0.0f, 0.0f);
    const auto [quiet, q0, q1] = run (-1.0f, 0.0f, 0.0f);
    CHECK (loud > 0.05 && quiet < 1.0e-4, "LEVEL's mod taken at the hit: %.4f, down: %.6f", loud, quiet);
    const auto [l2, c0, c1] = run (0.0f, -0.6f, 0.0f);
    CHECK (c1 < 0.5 * c0 && std::abs (c0 - b0) < 0.2 * b0, "the cutoff closed by a mod while it rings: %.3f -> %.3f", c0, c1);
    const auto [l3, t0, t1] = run (0.0f, 0.0f, 400.0f);
    CHECK (t1 < 0.5 * t0, "the cutoff turned while it rings: %.3f -> %.3f", t0, t1);
    (void) b1; (void) q0; (void) q1; (void) l2; (void) l3;
}

// (0.4.17) the sound of the settings that were there before: the same, frame for frame. DRUM_GOLDEN=write <file> keeps
// it, DRUM_GOLDEN=<file> compares (for the change of the voice; not a test of the suite unless asked)
static void testGolden()
{
    const char* g = std::getenv ("DRUM_GOLDEN");
    if (g == nullptr)
        return;
    const bool writing = std::strncmp (g, "write ", 6) == 0;
    const std::string path = writing ? std::string (g + 6) : std::string (g);
    std::vector<float> all;
    auto take = [&all] (Rig& r) { all.insert (all.end(), r.L.begin(), r.L.end()); all.insert (all.end(), r.R.begin(), r.R.end()); };
    auto tone = std::make_shared<radio::Take::Audio>();
    tone->sampleRate = sr;
    for (int i = 0; i < 24000; ++i)
    {
        tone->l.push_back ((float) (0.5 * std::sin (6.2831853 * 300.0 * i / sr) * std::exp (-i / 9000.0)));
        tone->r.push_back ((float) (0.4 * std::sin (6.2831853 * 450.0 * i / sr)));
    }
    Sound snd;
    snd.audio = tone;
    struct Cfg { int src; float bank, pos, sweep, swt, pmod, ratio, tune, decay, attack, start, rev, stereo, filt, cut, reso, fenv, drive; };
    const Cfg cfgs[] = {
        { 0, 0, 0.0f, 0, 0.05f, 0, 1.41f, 0, 0.5f, 0, 0, 0, 1, 0, 20000, 0, 0, 0 },
        { 0, 0, 0.4f, 24, 0.04f, 0, 1.41f, -5, 0.4f, 0, 0, 0, 1, 0, 20000, 0, 0, 0 },
        { 0, 0, 0.7f, -12, 0.2f, 0.6f, 2.3f, 3, 0.6f, 0.2f, 0, 0, 1, 0, 3000, 0.4f, 0.5f, 0.3f },
        { 0, 0, 0.2f, 48, 0.01f, 1.0f, 0.5f, 0, 0.3f, 0, 0, 0, 1, 2, 800, 0.7f, -0.3f, 0.8f },
        { 1, 0, 0, 0, 0.05f, 0, 1.41f, 0, 1.0f, 0, 0.1f, 0, 1, 0, 20000, 0, 0, 0 },
        { 1, 0, 0, 12, 0.08f, 0.3f, 1.41f, -7, 0.7f, 0, 0.3f, 1, 0, 1, 500, 0.2f, 0, 0.2f },
        { 1, 0, 0, 0, 0.05f, 0, 1.41f, 5, 0.5f, 0.1f, 0, 0, 1, 0, 2000, 0, 0.6f, 0 },
    };
    for (const auto& c : cfgs)
    {
        Rig r;
        r.e.setSound (0, &snd);
        auto& tk = r.e.pattern (0).tr[0];
        tk.p[pSource] = (float) c.src; tk.p[pBank] = c.bank; tk.p[pPosition] = c.pos; tk.p[pSweep] = c.sweep; tk.p[pSweepTime] = c.swt;
        tk.p[pPmod] = c.pmod; tk.p[pRatio] = c.ratio; tk.p[pTune] = c.tune; tk.p[pDecay] = c.decay; tk.p[pAttack] = c.attack;
        tk.p[pStart] = c.start; tk.p[pReverse] = c.rev; tk.p[pStereo] = c.stereo; tk.p[pFilter] = c.filt; tk.p[pCutoff] = c.cut;
        tk.p[pReso] = c.reso; tk.p[pFEnv] = c.fenv; tk.p[pDrive] = c.drive; tk.p[pLevel] = 0.7f;
        r.play (0.5, 120.0, 0.5f, { { 100, firstNote, 110 } }, false);
        take (r);
    }
    if (writing)
    {
        if (auto* f = std::fopen (path.c_str(), "wb")) { std::fwrite (all.data(), sizeof (float), all.size(), f); std::fclose (f); }
        std::printf ("  golden: %zu frames kept\n", all.size());
        return;
    }
    std::vector<float> was (all.size(), 0.0f);
    size_t got = 0;
    if (auto* f = std::fopen (path.c_str(), "rb")) { got = std::fread (was.data(), sizeof (float), was.size(), f); std::fclose (f); }
    double worst = 0.0;
    size_t at = 0;
    for (size_t i = 0; i < all.size(); ++i)
        if (std::abs ((double) all[i] - was[i]) > worst) { worst = std::abs ((double) all[i] - was[i]); at = i; }
    std::printf ("  golden: worst difference %.3g at %zu\n", worst, at);
    CHECK (got == all.size() && worst < 1.0e-4, "the old settings sound the same: %.3g (frame %zu of %zu)", worst, at, all.size());
}

//==============================================================================
// (0.4.17) the drums' wavetables and their scans, the second oscillator, BURST, STUTTER, a sound read as a wave or
// in grains, VEL > SCAN
static double rms (const float* x, int n)
{
    double e = 0;
    for (int i = 0; i < n; ++i) e += (double) x[i] * x[i];
    return std::sqrt (e / n);
}
static double toneAt (const std::vector<float>& x, size_t from, size_t n, double hz)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (6.283185307 * (double) i / (double) n);
        re += w * x[from + i] * std::cos (6.283185307 * hz * (double) i / sr);
        im += w * x[from + i] * std::sin (6.283185307 * hz * (double) i / sr);
    }
    return std::sqrt (re * re + im * im) / (double) n;
}
// how bright: the share of the energy in the first difference (a high pass, crude)
static double brightness (const std::vector<float>& x, size_t from, size_t n)
{
    double d = 0, e = 0;
    for (size_t i = from + 1; i < from + n; ++i) { d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]); e += (double) x[i] * x[i]; }
    return e > 0 ? d / e : 0.0;
}
static void testWavetables()
{
    // the six tables: twelve cycles each, bounded, each one unlike its neighbour
    for (int t = 0; t < 6; ++t)
    {
        const auto* b = ownTable (t);
        CHECK (b != nullptr && b->count() == 12, "table %d: %d cycles", t, b != nullptr ? b->count() : 0);
        double diff = 0, peak = 0;
        for (int c = 0; c + 1 < b->count(); ++c)
            for (int i = 0; i < dsp::CycleBank::size; ++i)
            {
                diff = std::max (diff, (double) std::abs (b->cycle (c)[i] - b->cycle (c + 1)[i]));
                peak = std::max (peak, (double) std::abs (b->cycle (c)[i]));
            }
        CHECK (peak > 0.5 && peak < 1.01 && diff > 0.01, "table %d: peak %.3f, change %.3f", t, peak, diff);
    }
    auto synthTrack = [] (Rig& g)
    {
        auto& tk = g.e.pattern (0).tr[0];
        tk.p[pSource] = synth;
        tk.p[pNote] = 60;
        tk.p[pDecay] = 1.0f;
        tk.p[pLevel] = 0.5f;
        return std::ref (tk);
    };
    // SCAN: FOLD from its softest to its hardest in 0.3 s: brighter as it goes; without the scan, it stays
    {
        Rig g;
        Track& tk = synthTrack (g);
        tk.p[pBank] = 3;
        tk.p[pPosition] = 0.0f;
        tk.p[pScan] = 1.0f;
        tk.p[pScanTime] = 0.3f;
        g.play (0.6, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        const double early = brightness (g.L, 200, 2400), late = brightness (g.L, 21600, 2400);
        Rig h;
        Track& tk2 = synthTrack (h);
        tk2.p[pBank] = 3;
        h.play (0.6, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        const double still = brightness (h.L, 21600, 2400), stillEarly = brightness (h.L, 200, 2400);
        std::printf ("  scan: brightness %.4f -> %.4f (no scan: %.4f -> %.4f)\n", early, late, stillEarly, still);
        CHECK (late > 4.0 * early && std::abs (still - stillEarly) < 0.2 * stillEarly, "scanned: %.4f -> %.4f, still %.4f / %.4f", early, late, stillEarly, still);
        // VEL > SCAN at full: a soft hit scans less
        tk.p[pVelMod] = 1.0f;
        g.play (0.6, 120.0, 0.5f, { { 0, firstNote, 40 } }, false);
        CHECK (brightness (g.L, 21600, 2400) < 0.5 * late, "a soft hit scans less: %.4f", brightness (g.L, 21600, 2400));
    }
    // the second oscillator: MIX alone (no first) sounds at RATIO; RING gives the sum and the difference
    {
        Rig g;
        Track& tk = synthTrack (g);
        tk.p[pBody] = 0.0f;
        tk.p[pO2Mode] = mix;
        tk.p[pPmod] = 0.8f;
        tk.p[pRatio] = 2.0f;
        g.play (0.3, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        const double f0 = 440.0 * std::exp2 ((60.0 - 69.0 - 24.0) / 12.0);
        const double at2 = toneAt (g.L, 2400, 8192, 2.0 * f0), at1 = toneAt (g.L, 2400, 8192, f0);
        CHECK (at2 > 10.0 * at1 && at2 > 0.01, "MIX alone at its ratio: %.4f (%.4f at the note)", at2, at1);
        tk.p[pBody] = 1.0f;
        tk.p[pO2Mode] = ring;
        tk.p[pPmod] = 1.0f;
        tk.p[pRatio] = 3.0f;
        g.play (0.3, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        const double sum = toneAt (g.L, 2400, 8192, 4.0 * f0), dif = toneAt (g.L, 2400, 8192, 2.0 * f0), plain = toneAt (g.L, 2400, 8192, f0);
        CHECK (sum > 5.0 * plain && dif > 5.0 * plain, "RING: %.4f %.4f over %.4f", sum, dif, plain);
    }
    // BURST: four taps of noise, 12 ms apart (a clap)
    {
        Rig g;
        Track& tk = synthTrack (g);
        tk.p[pBody] = 0.0f;
        tk.p[pNoise] = 1.0f;
        tk.p[pDecay] = 0.3f;
        tk.p[pBurst] = 4.0f;
        tk.p[pBurstGap] = 0.012f;
        g.play (0.15, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        int taps = 0;
        double last = 0;
        for (size_t w = 0; w + 96 < 4 * 576 + 400; w += 96)
        {
            const double e = std::sqrt (std::inner_product (g.L.begin() + (long) w, g.L.begin() + (long) w + 96, g.L.begin() + (long) w, 0.0) / 96.0);
            if (e > 2.0 * last && e > 0.02)
                ++taps;
            last = e;
        }
        CHECK (taps == 4, "BURST: %d taps", taps);
    }
    // STUTTER: the click struck again three times, 50 ms apart (the volume going on)
    {
        Rig g;
        Track& tk = synthTrack (g);
        tk.p[pBody] = 0.0f;
        tk.p[pClick] = 1.0f;
        tk.p[pStutter] = 3.0f;
        tk.p[pStutLen] = 0.05f;
        g.play (0.3, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        int clicks = 0;
        for (int k = 0; k < 5; ++k)
            if (rms (g.L.data() + (size_t) (k * 2400), 200) > 0.01)
                ++clicks;
        CHECK (clicks == 4, "STUTTER: %d clicks (the hit and three)", clicks);
    }
    // a sound read as a WAVE: at the key's pitch, whatever it was recorded at
    auto noisy = std::make_shared<radio::Take::Audio>();
    noisy->sampleRate = sr;
    {
        std::mt19937 r (5);
        std::uniform_real_distribution<float> u (-0.4f, 0.4f);
        for (int i = 0; i < 48000; ++i)
            noisy->l.push_back (u (r) * 0.3f + 0.4f * (float) std::sin (6.283185307 * 97.0 * i / sr));
        noisy->r = noisy->l;
    }
    Sound ns;
    ns.audio = noisy;
    {
        Rig g;
        g.e.setSound (0, &ns);
        auto& tk = g.e.pattern (0).tr[0];
        tk.p[pPlay] = wave;
        tk.p[pDecay] = 1.0f;
        tk.p[pLevel] = 0.5f;
        tk.p[pNote] = 60;
        g.play (0.8, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        const double key = 261.6256;
        // (its cycle repeats at the key: alike a period apart, not beside it; and nothing under it)
        auto ac = [&g] (double lag)
        {
            const int l = (int) std::lround (lag);
            double a = 0, e = 0;
            for (size_t i = 12000; i < 30000; ++i) { a += (double) g.L[i] * g.L[i + (size_t) l]; e += (double) g.L[i] * g.L[i]; }
            return a / e;
        };
        double mean = 0;
        for (size_t i = 12000; i < 30000; ++i) mean += g.L[i];
        mean /= 18000.0;
        const double onKey = ac (sr / key), off = ac (sr / key / 1.06);
        std::printf ("  wave: alike a period apart %.3f, beside %.3f, offset %.4f\n", onKey, off, mean);
        CHECK (onKey > 0.8 && off < onKey - 0.3 && std::abs (mean) < 0.01, "WAVE on the key: %.3f vs %.3f, offset %.4f", onKey, off, mean);
        // GRAINS: frozen in place (no scan), an octave up (TUNE +12): 600 Hz from a 300 Hz tone, held
        auto t300 = std::make_shared<radio::Take::Audio>();
        t300->sampleRate = sr;
        for (int i = 0; i < 24000; ++i)
            t300->l.push_back (0.4f * (float) std::sin (6.283185307 * 300.0 * i / sr));
        t300->r = t300->l;
        Sound ts;
        ts.audio = t300;
        g.e.setSound (0, &ts);
        tk.p[pPlay] = grains;
        tk.p[pTune] = 12.0f;
        tk.p[pStart] = 0.3f;
        g.play (2.0, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        const double up = toneAt (g.L, 72000, 16384, 600.0), was = toneAt (g.L, 72000, 16384, 300.0);
        CHECK (up > 10.0 * was && rms (g.L.data() + 90000, 4800) > 0.05, "GRAINS: 600 Hz %.4f (300: %.4f), still sounding after 1.9 s", up, was);
        // STUTTER on a sound as it is: its jumps without a click
        tk.p[pPlay] = oneShot;
        tk.p[pTune] = 0.0f;
        tk.p[pStart] = 0.0f;
        tk.p[pStutter] = 4.0f;
        tk.p[pStutLen] = 0.037f;
        g.play (0.3, 120.0, 0.5f, { { 0, firstNote, 127 } }, false);
        float jump = 0.0f;
        for (size_t i = 1; i < g.L.size(); ++i)
            jump = std::max (jump, std::abs (g.L[i] - g.L[i - 1]));
        CHECK (jump < 0.06f, "STUTTER on a sound, no click: %.4f", (double) jump);
    }
}

int main()
{
    testGolden();
    testWavetables();
    testParams();
    testSteps();
    testRolls();
    testPatterns();
    testKeysAndChoke();
    testSources();
    testFilterEnvelope();
    testMods();
    testStorm();
    testCpu();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
