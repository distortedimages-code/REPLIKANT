// WEAVE (0.4.20) on its own: the rules drawing the pattern (the beats and backbeats fixed and clean, MOVE and its
// styles between them, the accidents, MUTATE, the seed, a key forcing a sound), the spans the sounds are fitted to,
// the head reading each where the song is (the right sound, the right moment), its jumps crossfaded (no click),
// STRETCH keeping the pitch where TAPE moves it, the keys running it with the transport stopped, and a storm.

#include "../Source/weave/Weave.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace weave;
static const double sr = 48000.0;

static std::vector<Step> planOf (const Settings& st, int count, int64_t cycle, int force = -1)
{
    std::vector<Step> v (maxSteps);
    v.resize ((size_t) plan (st, count, cycle, force, v.data()));
    return v;
}

static void testRules()
{
    Settings st;
    st.beat = 0;
    st.back = 3;
    st.move = 0.6f;
    st.time = 0.5f;
    st.stutter = 0.5f;
    st.reverse = 0.5f;
    st.bars = 2;
    st.seed = 42;
    const auto p = planOf (st, 10, 0);
    CHECK (p.size() == 32, "two bars of sixteenths (%d)", (int) p.size());
    bool beats = true, clean = true, between = false;
    for (int s = 0; s < 32; ++s)
    {
        const int beatNo = (s / 4) % 4;
        if (s % 4 == 0)
        {
            beats = beats && p[(size_t) s].fixed && p[(size_t) s].thread == (beatNo == 1 || beatNo == 3 ? 3 : 0);
            clean = clean && p[(size_t) s].shift == 0 && p[(size_t) s].ratchet == 1 && ! p[(size_t) s].reverse;
        }
        else
            between = between || (p[(size_t) s].thread != 0 && p[(size_t) s].thread != 3);
    }
    CHECK (beats, "BEAT on 1 and 3, BACK on 2 and 4");
    CHECK (clean, "the fixed steps clean (no accident)");
    CHECK (between, "between them, the head moves to other sounds");
    int shifts = 0, ratchets = 0, revs = 0;
    for (const auto& s : p)
    {
        shifts += s.shift != 0;
        ratchets += s.ratchet > 1;
        revs += s.reverse;
    }
    CHECK (shifts > 4 && ratchets > 4 && revs > 4, "the accidents drawn (%d %d %d)", shifts, ratchets, revs);
    // the same settings, the same pattern; another seed, another
    const auto q = planOf (st, 10, 0);
    bool same = true;
    for (size_t i = 0; i < p.size(); ++i)
        same = same && p[i].thread == q[i].thread && p[i].shift == q[i].shift && p[i].ratchet == q[i].ratchet && p[i].reverse == q[i].reverse;
    CHECK (same, "the same seed, the same weave");
    st.seed = 43;
    const auto r = planOf (st, 10, 0);
    int diff = 0;
    for (size_t i = 0; i < p.size(); ++i)
        diff += p[i].thread != r[i].thread;
    CHECK (diff > 4, "another seed, another weave (%d steps differ)", diff);
    // MOVE at 0: the beat's sound held through
    st.move = 0.0f;
    st.back = -1;
    bool held = true;
    for (const auto& s : planOf (st, 10, 0))
        held = held && s.thread == 0;
    CHECK (held, "MOVE at 0: every step on BEAT's sound");
    // no sound loaded: nothing
    CHECK (planOf (st, 0, 0)[0].thread == -1, "no sound: silence");
    // a key forcing a sound
    bool forced = true;
    for (const auto& s : planOf (st, 10, 0, 7))
        forced = forced && s.thread == 7;
    CHECK (forced, "a key held: its sound everywhere");
    // a BEAT past the sounds loaded: as if none
    st.beat = 12;
    st.move = 0.5f;
    CHECK (! planOf (st, 4, 0)[0].fixed, "BEAT on a sound not loaded: no fixed beat");
}

static void testStyles()
{
    Settings st;
    st.beat = -1;
    st.back = -1;
    st.seed = 9;
    st.move = 1.0f;
    st.style = styleWalk;
    const auto w = planOf (st, 6, 0);
    int ups = 0;
    for (size_t i = 1; i < w.size(); ++i)
        ups += w[i].thread == (w[i - 1].thread + 1) % 6;
    CHECK (ups >= 9, "WALK: mostly on to the next (%d of 15)", ups);
    st.style = styleEuclid;
    st.move = 0.25f;
    const auto eu = planOf (st, 6, 0);
    int changes = 0;
    for (size_t i = 1; i < eu.size(); ++i)
        changes += eu[i].thread != eu[i - 1].thread;
    CHECK (changes >= 3 && changes <= 4, "EUCLID: four changes a bar at 25 %% (%d)", changes);
    st.style = styleCall;
    st.beat = 2;
    st.move = 1.0f;
    const auto c = planOf (st, 6, 0);
    bool calls = true;
    for (size_t s = 0; s < c.size(); ++s)
        calls = calls && (s % 4 < 2 ? c[s].thread == 2 : c[s].thread != 2);
    CHECK (calls, "CALL: each beat's second half answered by another sound");
    // MUTATE: 0, the pattern comes back; some, it moves pass after pass, a little at a time
    st.style = styleRandom;
    st.beat = 0;
    st.move = 0.5f;
    st.mutate = 0.0f;
    const auto a0 = planOf (st, 8, 0), a5 = planOf (st, 8, 5);
    bool back = true;
    for (size_t i = 0; i < a0.size(); ++i)
        back = back && a0[i].thread == a5[i].thread;
    CHECK (back, "MUTATE at 0: the same pattern every pass");
    st.mutate = 0.15f;
    int moved = 0, step1 = 0;
    for (int cyc = 1; cyc < 40; ++cyc)
    {
        const auto x = planOf (st, 8, cyc - 1), y = planOf (st, 8, cyc);
        int d = 0;
        for (size_t i = 0; i < x.size(); ++i)
            d += x[i].thread != y[i].thread;
        moved += d > 0;
        step1 = std::max (step1, d);
    }
    CHECK (moved > 10 && step1 < 12, "MUTATE: it evolves, a few steps at a time (%d passes moved, at most %d steps)", moved, step1);
}

static void testSpans()
{
    Settings st;
    CHECK (spanBars (st, 2.0, 120.0) == 1.0, "a bar long at 120: one bar");
    CHECK (spanBars (st, 3.5, 120.0) == 2.0, "3.5 s: two bars");
    CHECK (spanBars (st, 0.45, 120.0) == 0.25, "0.45 s: a beat");
    CHECK (spanBars (st, 70.0, 120.0) == 16.0, "longer than sixteen bars: sixteen");
    st.span = 5;
    CHECK (spanBars (st, 0.45, 120.0) == 4.0, "SPAN set: four bars whatever the length");
}

// a sound whose every sample says which it is and where: (k + 1) * 10 + where (0..1)
static std::shared_ptr<Audio> ramp (int k, int frames)
{
    auto a = std::make_shared<Audio>();
    a->sampleRate = sr;
    a->l.resize ((size_t) frames);
    for (int i = 0; i < frames; ++i)
        a->l[(size_t) i] = (float) (k + 1) * 10.0f + (float) i / (float) frames;
    a->r = a->l;
    return a;
}

static std::shared_ptr<Audio> sine (double hz, double seconds, double amp = 0.5, double phase = 0.0)
{
    auto a = std::make_shared<Audio>();
    a->sampleRate = sr;
    const int n = (int) (seconds * sr);
    a->l.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        a->l[(size_t) i] = (float) (amp * std::sin (2.0 * 3.14159265358979 * hz * i / sr + phase));
    a->r = a->l;
    return a;
}

static void testReading()
{
    // eight sounds a bar long each at 120: the head on the right sound, at the right moment, mid-step
    std::vector<std::shared_ptr<Audio>> keep;
    Engine e;
    e.prepare (sr);
    for (int k = 0; k < 8; ++k)
    {
        keep.push_back (ramp (k, 96000));
        e.setThread (k, keep.back().get());
    }
    Settings st;
    st.beat = 0;
    st.back = 3;
    st.move = 0.7f;
    st.time = 0.4f;
    st.stutter = 0.3f;
    st.reverse = 0.3f;
    st.seed = 5;
    st.xfadeMs = 1.0f;
    st.bars = 2;
    const int bar = 96000, total = 3 * bar;          // three bars: a pass and a half
    std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
    Clock ck;
    ck.running = true;
    ck.bpm = 120.0;
    for (int at = 0; at < total; at += 512)
    {
        const int n = std::min (512, total - at);
        ck.ppq = at / sr * 2.0;
        e.render (L.data() + at, R.data() + at, n, ck, st, nullptr, 0);
    }
    const auto p = planOf (st, 8, 0);
    int wrongSound = 0, wrongPlace = 0, tested = 0;
    const int stepLen = bar / 16;
    for (int s = 0; s < 48; ++s)
    {
        const auto& sp = p[(size_t) (s % 32)];
        // a moment in the step clear of its repeats' fades
        const int segLen = stepLen / sp.ratchet;
        const int at = s * stepLen + segLen / 2;
        const float v = L[(size_t) at];
        const int k = (int) std::floor (v / 10.0f) - 1;
        if (k != sp.thread)
        {
            ++wrongSound;
            continue;
        }
        double local = (double) (segLen / 2) / stepLen;
        if (sp.reverse)
            local = 1.0 / sp.ratchet - local;
        double u = ((double) (s + sp.shift) + local) / 16.0;
        u -= std::floor (u);
        const float where = v - (float) (k + 1) * 10.0f;
        if (std::abs (where - (float) u) > 0.002f)
            ++wrongPlace;
        ++tested;
    }
    CHECK (wrongSound == 0, "each step on its sound (%d wrong)", wrongSound);
    CHECK (wrongPlace == 0 && tested == 48, "each step at its moment (%d wrong of %d)", wrongPlace, tested);
}

static float worstJump (const std::vector<float>& x, int from = 1)
{
    float w = 0.0f;
    for (size_t i = (size_t) from; i < x.size(); ++i)
        w = std::max (w, std::abs (x[i] - x[i - 1]));
    return w;
}

static void testNoClicks()
{
    // sounds that are sines, each its own pitch and phase: a hard cut would jump by up to 1
    std::vector<std::shared_ptr<Audio>> keep;
    Engine e;
    e.prepare (sr);
    for (int k = 0; k < 6; ++k)
    {
        keep.push_back (sine (180.0 + 70.0 * k, 1.7 + 0.2 * k, 0.5, 0.9 * k));
        e.setThread (k, keep.back().get());
    }
    for (int fit = 0; fit < 2; ++fit)
    {
        Settings st;
        st.fit = fit;
        st.move = 1.0f;
        st.time = 0.6f;
        st.stutter = 0.6f;
        st.reverse = 0.5f;
        st.grid = 32;
        st.xfadeMs = 4.0f;
        e.reset();
        const int total = (int) (sr * 4.0);
        std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
        Clock ck;
        ck.running = true;
        ck.bpm = 133.0;
        for (int at = 0; at < total; at += 256)
        {
            ck.ppq = 3.7 + at / sr * ck.bpm / 60.0;
            e.render (L.data() + at, R.data() + at, std::min (256, total - at), ck, st, nullptr, 0);
        }
        const float w = worstJump (L, 400);
        bool finite = true;
        for (float v : L)
            finite = finite && std::isfinite (v) && std::abs (v) < 2.0f;
        CHECK (w < 0.13f && finite, "%s: every jump crossfaded (worst step between samples %.3f)", fit == 0 ? "TAPE" : "STRETCH", w);
    }
}

static double pitchOf (const std::vector<float>& x, int from, int to)
{
    int ups = 0;
    for (int i = from + 1; i < to; ++i)
        ups += x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f;
    return ups * sr / (double) (to - from);
}

static void testFit()
{
    // a 440 Hz sound 1.5 s long, at 120: fitted to a bar (2 s). TAPE reads it slower (330 Hz), STRETCH keeps 440
    auto a = sine (440.0, 1.5);
    for (int fit = 0; fit < 2; ++fit)
    {
        Engine e;
        e.prepare (sr);
        e.setThread (0, a.get());
        Settings st;
        st.fit = fit;
        st.move = 0.0f;
        const int total = (int) sr * 2;
        std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
        Clock ck;
        ck.running = true;
        ck.bpm = 120.0;
        for (int at = 0; at < total; at += 512)
        {
            ck.ppq = at / sr * 2.0;
            e.render (L.data() + at, R.data() + at, std::min (512, total - at), ck, st, nullptr, 0);
        }
        const double hz = pitchOf (L, 4800, total - 4800);
        CHECK (std::abs (hz - (fit == 0 ? 330.0 : 440.0)) < 6.0, "%s: %.1f Hz", fit == 0 ? "TAPE moves the pitch with the speed" : "STRETCH keeps the pitch", hz);
    }
}

static void testKeys()
{
    std::vector<std::shared_ptr<Audio>> keep;
    Engine e;
    e.prepare (sr);
    for (int k = 0; k < 4; ++k)
    {
        keep.push_back (ramp (k, 96000));
        e.setThread (k, keep.back().get());
    }
    Settings st;
    st.move = 1.0f;
    Clock ck;                                       // the transport stopped
    std::vector<float> L (4096, 0.0f), R (4096, 0.0f);
    e.render (L.data(), R.data(), 4096, ck, st, nullptr, 0);
    float most = 0.0f;
    for (float v : L)
        most = std::max (most, std::abs (v));
    CHECK (most == 0.0f, "the transport stopped, no key: silence");
    // a key (C#3: the second sound) runs it, that sound everywhere
    Event on { 0, 61, 100 };
    std::fill (L.begin(), L.end(), 0.0f);
    e.render (L.data(), R.data(), 4096, ck, st, &on, 1);
    const int k = (int) std::floor (L[3000] / 10.0f) - 1;
    CHECK (k == 1 && L[3000] > 0.0f, "a key held: the weave runs, on its sound (%d)", k);
    CHECK (e.shownForce.load() == 1 && e.shownStep.load() == 0, "the screen told");
    Event off { 0, 61, 0 };
    std::fill (L.begin(), L.end(), 0.0f);
    e.render (L.data(), R.data(), 4096, ck, st, &off, 1);
    CHECK (std::abs (L[4000]) < 1.0e-3f, "the key let go: it fades out (%.4f)", L[4000]);
}

static void testStorm()
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::vector<std::shared_ptr<Audio>> keep;
    Engine e;
    e.prepare (44100.0);
    for (int k = 0; k < 16; ++k)
    {
        keep.push_back (sine (60.0 + 50.0 * k, 0.05 + 0.6 * u (rng), 0.8));
        e.setThread (k, keep.back().get());
    }
    bool ok = true;
    std::vector<float> L (777), R (777);
    Clock ck;
    ck.running = true;
    for (int b = 0; b < 3000 && ok; ++b)
    {
        if (b % 40 == 0)
        {
            Settings st;
            st.beat = (int) (u (rng) * 18.0f) - 1;
            st.back = (int) (u (rng) * 18.0f) - 1;
            st.move = u (rng);
            st.style = (int) (u (rng) * 4.0f);
            st.time = u (rng);
            st.stutter = u (rng);
            st.reverse = u (rng);
            st.mutate = u (rng);
            st.bars = 1 << (int) (u (rng) * 3.0f);
            st.grid = 8 << (int) (u (rng) * 3.0f);
            st.seed = (uint32_t) (u (rng) * 999.0f);
            st.fit = u (rng) < 0.5f ? fitTape : fitStretch;
            st.span = (int) (u (rng) * 8.0f);
            st.xfadeMs = 0.5f + 30.0f * u (rng);
            st.grainMs = 20.0f + 180.0f * u (rng);
            ck.bpm = 40.0 + 260.0 * u (rng);
            ck.ppq = -2.0 + 100.0 * u (rng);
            static Settings now;
            now = st;
        }
        static Settings now;
        if (b % 97 == 0)
            e.setThread ((int) (u (rng) * 16.0f), u (rng) < 0.3f ? nullptr : keep[(size_t) (u (rng) * 16.0f)].get());
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        Settings st;
        st.move = u (rng);
        st.fit = b % 2;
        st.stutter = 0.5f;
        st.time = 0.5f;
        e.render (L.data(), R.data(), 777, ck, st, nullptr, 0);
        ck.ppq += 777.0 / 44100.0 * ck.bpm / 60.0;
        for (float v : L)
            ok = ok && std::isfinite (v) && std::abs (v) < 4.0f;
    }
    CHECK (ok, "a storm of settings and sounds: finite and bounded");
}

int main()
{
    testRules();
    testStyles();
    testSpans();
    testReading();
    testNoClicks();
    testFit();
    testKeys();
    testStorm();
    std::printf ("WeaveTests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
