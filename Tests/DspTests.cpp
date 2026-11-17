// The player's DSP on its own: oversampling, the filters (stable, resonant where they should be, tuned), the drives,
// the effects (bounded, doing what they say), the modulators.

#include "../Source/dsp/Oversampler.h"
#include "../Source/dsp/VAFilter.h"
#include "../Source/dsp/Drive.h"
#include "../Source/dsp/Effects.h"
#include "../Source/dsp/Modulators.h"
#include "../Source/core/Analysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace dsp;

static double now() { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }

static double rmsDb (const std::vector<float>& v, size_t from = 0)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
    return 10.0 * std::log10 (s / std::max<size_t> (1, v.size() - from) + 1e-30);
}

static void testOversampler()
{
    const double sr = 48000;
    for (int f : { 2, 4, 8 })
    {
        Oversampler os;
        os.prepare (f);
        // a tone through (identity): kept, delayed
        std::vector<float> out;
        for (int i = 0; i < 24000; ++i)
            out.push_back (os.process ((float) std::sin (2 * M_PI * 1000 * i / sr), [] (float x) { return x; }));
        const double db = rmsDb (out, 4000) - 10.0 * std::log10 (0.5);
        CHECK (std::abs (db) < 0.05, "%dx: 1 kHz at %.3f dB", f, db);
        // 18 kHz still there
        os.reset();
        out.clear();
        for (int i = 0; i < 24000; ++i)
            out.push_back (os.process ((float) std::sin (2 * M_PI * 18000 * i / sr), [] (float x) { return x; }));
        const double db18 = rmsDb (out, 4000) - 10.0 * std::log10 (0.5);
        CHECK (std::abs (db18) < 0.5, "%dx: 18 kHz at %.3f dB", f, db18);
        // a hard clip at the high rate: what folds back below 20 kHz is far less than without oversampling
        os.reset();
        out.clear();
        for (int i = 0; i < 48000; ++i)
            out.push_back (os.process ((float) std::sin (2 * M_PI * 5003 * i / sr) * 4.0f, [] (float x) { return std::max (-1.0f, std::min (1.0f, x)); }));
        (void) out;
    }
}

// the frequency a filter sings at, by itself (an impulse, then its ring counted by zero crossings)
static double ringHz (FilterModel m, float cutoffHz, double sr, int factor)
{
    VAFilter f;
    f.prepare (sr * factor);
    FilterSettings s;
    s.model = m;
    s.res = 1.0f;
    f.set (s);
    Oversampler os;
    os.prepare (factor);
    const float oct = std::log2 (cutoffHz / 20.0f);
    std::vector<float> out;
    for (int i = 0; i < (int) sr; ++i)
    {
        const float x = i < 4 ? 0.5f : 0.0f;
        out.push_back (os.process (x, [&] (float v) { return f.tick (v, oct); }));
    }
    const int from = (int) (sr * 0.5);
    int crossings = 0;
    for (int i = from + 1; i < (int) out.size(); ++i)
        if (out[(size_t) i - 1] < 0 && out[(size_t) i] >= 0)
            ++crossings;
    return crossings / ((out.size() - from) / sr);
}

static void testFilters()
{
    const double sr = 48000;
    const FilterModel models[] = { FilterModel::moog, FilterModel::ms20, FilterModel::sem, FilterModel::oscar, FilterModel::wasp, FilterModel::polivoks };
    const char* names[] = { "MOOG", "MS-20", "SEM", "OSCAR", "WASP", "POLIVOKS" };
    std::mt19937 rng (7);
    std::normal_distribution<float> nd (0, 0.3f);
    for (int mi = 0; mi < 6; ++mi)
        for (auto type : { FilterType::lp, FilterType::bp, FilterType::hp, FilterType::notch })
        {
            // stable and bounded with everything at the extremes, cutoff swept, loud noise in
            VAFilter f;
            f.prepare (sr * 2);
            FilterSettings s;
            s.model = models[mi];
            s.type = type;
            float peak = 0;
            bool nan = false;
            for (float res : { 0.0f, 0.7f, 1.0f })
                for (float drive : { 0.0f, 1.0f })
                    for (float fm : { 0.0f, 1.0f })
                    {
                        s.res = res; s.drive = drive; s.fm = fm; s.sep = 0.6f;
                        f.set (s);
                        for (int i = 0; i < 20000; ++i)
                        {
                            const float oct = 5.0f + 5.0f * std::sin (i * 0.0007f);
                            const float y = f.tick (nd (rng) * 3.0f, oct);
                            nan |= ! (y == y);
                            peak = std::max (peak, std::abs (y));
                        }
                    }
            CHECK (! nan && peak <= 2.0f, "%s type %d: peak %.2f nan %d", names[mi], (int) type, peak, (int) nan);
        }
    // the low passes pass the lows and cut the highs
    for (int mi = 0; mi < 6; ++mi)
    {
        auto through = [&] (double hz)
        {
            VAFilter f;
            f.prepare (sr * 2);
            FilterSettings s;
            s.model = models[mi];
            s.res = 0.0f;
            f.set (s);
            Oversampler os;
            os.prepare (2);
            std::vector<float> out;
            for (int i = 0; i < 24000; ++i)
                out.push_back (os.process ((float) (0.1 * std::sin (2 * M_PI * hz * i / sr)), [&] (float v) { return f.tick (v, std::log2 (1000.0f / 20.0f)); }));
            return rmsDb (out, 8000) - 10.0 * std::log10 (0.005);
        };
        const double lo = through (100.0), hi = through (8000.0);
        CHECK (lo > -4.0 && hi < lo - 20.0, "%s LP at 1 kHz: 100 Hz %.1f dB, 8 kHz %.1f dB", names[mi], lo, hi);
    }
    // the ladder sings on its cutoff (MIFM1's tuning), at 2x and 4x
    for (int factor : { 2, 4 })
        for (float fc : { 220.0f, 1000.0f, 3000.0f })
        {
            const double r = ringHz (FilterModel::moog, fc, sr, factor);
            CHECK (std::abs (1200.0 * std::log2 (r / fc)) < 35.0, "moog %dx sings at %.1f for %.0f", factor, r, fc);
        }
    // the Polivoks sings by itself at full resonance, near its cutoff
    {
        const double r = ringHz (FilterModel::polivoks, 500.0f, sr, 2);
        CHECK (r > 300.0 && r < 800.0, "polivoks sings at %.1f", r);
    }
    // CPU: a voice's stereo filter at 2x
    {
        VAFilter f[2];
        Oversampler os[2];
        for (int c = 0; c < 2; ++c) { f[c].prepare (sr * 2); os[c].prepare (2); FilterSettings s; s.model = FilterModel::moog; s.res = 0.5f; s.fm = 0.3f; f[c].set (s); }
        const double t0 = now();
        float acc = 0;
        for (int i = 0; i < 48000; ++i)
            for (int c = 0; c < 2; ++c)
                acc += os[c].process (nd (rng), [&] (float v) { return f[c].tick (v, 6.0f); });
        const double t = now() - t0;
        std::printf ("  a voice's stereo ladder at 2x: %.1f %% of a core (%g)\n", t * 100.0, (double) (acc * 0));
    }
}

static void testDrive()
{
    const double sr = 48000;
    for (auto m : { DriveModel::preamp, DriveModel::console, DriveModel::tape })
    {
        Drive d;
        d.prepare (sr);
        DriveSettings s;
        s.model = m;
        s.drive = 1.0f;
        d.set (s);
        std::vector<float> L (48000), R (48000);
        for (int i = 0; i < 48000; ++i)
            L[(size_t) i] = R[(size_t) i] = (float) (0.8 * std::sin (2 * M_PI * 110 * i / sr));
        d.process (L.data(), R.data(), 48000);
        float peak = 0;
        bool nan = false;
        for (float v : L) { peak = std::max (peak, std::abs (v)); nan |= ! (v == v); }
        CHECK (! nan && peak < 1.6f && peak > 0.2f, "drive %d: peak %.2f", (int) m, peak);
        // at 0 drive, almost transparent in level
        Drive d0;
        d0.prepare (sr);
        s.drive = 0.0f;
        d0.set (s);
        for (int i = 0; i < 48000; ++i)
            L[(size_t) i] = R[(size_t) i] = (float) (0.3 * std::sin (2 * M_PI * 1000 * i / sr));
        d0.process (L.data(), R.data(), 48000);
        const double db = rmsDb (L, 10000) - 10.0 * std::log10 (0.045);
        CHECK (std::abs (db) < 2.5, "drive %d at 0: %.2f dB", (int) m, db);
    }
}

static void testEffects()
{
    const double sr = 48000;
    std::mt19937 rng (11);
    std::normal_distribution<float> nd (0, 0.3f);
    auto run = [&] (auto& fx, int seconds, bool impulse)
    {
        std::vector<float> L ((size_t) (sr * seconds)), R ((size_t) (sr * seconds));
        for (size_t i = 0; i < L.size(); ++i)
            L[i] = R[i] = impulse ? (i < 4800 ? nd (rng) : 0.0f) : (float) (0.5 * std::sin (2 * M_PI * 220 * i / sr));
        for (size_t i = 0; i < L.size(); i += 256)
            fx.process (L.data() + i, R.data() + i, (int) std::min<size_t> (256, L.size() - i));
        return std::make_pair (L, R);
    };
    // the reverb rings for its decay, and freezes
    {
        Space s;
        s.prepare (sr);
        SpaceSettings st;
        st.mix = 1.0f;
        st.decay = 0.5f;   // ~3 s
        s.set (st);
        auto [L, R] = run (s, 4, true);
        const double early = rmsDb (std::vector<float> (L.begin() + (long) (sr * 0.3), L.begin() + (long) (sr * 0.6)));
        const double late = rmsDb (std::vector<float> (L.begin() + (long) (sr * 3.3), L.begin() + (long) (sr * 3.6)));
        CHECK (early - late > 40.0 && early - late < 90.0, "reverb decays %.1f dB in 3 s", early - late);
        bool nan = false; for (float v : L) nan |= ! (v == v);
        CHECK (! nan, "reverb nan");
        st.decay = 1.0f;
        st.shimmer = 1.0f;
        s.set (st);
        auto [L2, R2] = run (s, 6, true);
        float peak = 0; for (float v : L2) peak = std::max (peak, std::abs (v));
        CHECK (peak < 4.0f, "frozen shimmer stays bounded: %.2f", peak);
    }
    // the spectral resynthesis follows a tone's pitch, an octave up when asked
    {
        Spectral sp;
        sp.prepare (sr);
        SpectralSettings st;
        st.mix = 1.0f;
        st.voices = 4;
        st.octave = 1.0f;
        st.slice = 0.1f;
        sp.set (st);
        auto [L, R] = run (sp, 2, false);
        std::vector<float> tail (L.begin() + (long) sr, L.end());
        const auto p = rk::detectPitch (tail, sr, 440.0, 0, (int) tail.size());
        CHECK (p.ok && std::abs (p.cents) < 10.0, "spectre: %.1f Hz (ok %d)", p.hz, (int) p.ok);
    }
    // the pitch shifter: a fifth up
    {
        PitchShift ps;
        ps.prepare (sr);
        PitchSettings st;
        st.mix = 1.0f;
        st.semis[0] = 7.0f;
        st.level[0] = 1.0f;
        st.level[1] = 0.0f;
        ps.set (st);
        auto [L, R] = run (ps, 2, false);
        std::vector<float> tail (L.begin() + (long) sr, L.end());
        const auto p = rk::detectPitch (tail, sr, 220.0 * std::pow (2.0, 7.0 / 12.0), 0, (int) tail.size());
        CHECK (p.ok && std::abs (p.cents) < 5.0, "pitch +7: %.1f cents (ok %d conf %.2f)", p.cents, (int) p.ok, p.confidence);
        st.crystals = true;
        st.feedback = 0.9f;
        ps.set (st);
        auto [L2, R2] = run (ps, 3, true);
        float peak = 0; for (float v : L2) peak = std::max (peak, std::abs (v));
        CHECK (peak < 3.0f, "crystals bounded %.2f", peak);
    }
    // chorus, flanger, vibrato, crush: bounded, and the crush is coarse
    {
        for (int mode : { 0, 1, 2 })
        {
            Chorus c;
            c.prepare (sr);
            ChorusSettings st;
            st.mode = mode;
            st.feedback = 0.9f;
            st.depth = 1.0f;
            st.mix = 1.0f;
            c.set (st);
            auto [L, R] = run (c, 1, false);
            float peak = 0; for (float v : L) peak = std::max (peak, std::abs (v));
            CHECK (peak < 2.5f && peak > 0.1f, "chorus %d peak %.2f", mode, peak);
        }
        Crush cr;
        cr.prepare (sr);
        CrushSettings st;
        st.bits = 3.0f;
        st.rateHz = 4000.0f;
        cr.set (st);
        auto [L, R] = run (cr, 1, false);
        // only a few levels
        std::vector<float> levels;
        for (size_t i = 1000; i < 6000; ++i)
            if (std::find (levels.begin(), levels.end(), L[i]) == levels.end()) levels.push_back (L[i]);
        CHECK (levels.size() > 2, "crush levels %zu", levels.size());
    }
}

static void testModulators()
{
    Modulator m;
    ModSettings s;
    s.shape = ModShape::square;
    s.sync = true;
    s.division = 4;   // 1/4
    m.set (s);
    // synced to the host: a quarter note is a cycle
    CHECK (m.atPpq (0.25, 120, 0.001) == 1.0f && m.atPpq (0.75, 120, 0.001) == -1.0f, "square on ppq");
    // steps: each a division long, the sequence coming back
    s.type = ModType::steps;
    s.length = 4;
    s.stepValues = { 1.0f, -1.0f, 0.5f, 0.0f };
    s.division = 4;   // a quarter note a step
    m.set (s);
    CHECK (m.atPpq (1.5, 120, 0.001) == -1.0f && m.atPpq (2.2, 120, 0.001) == 0.5f && m.atPpq (4.1, 120, 0.001) == 1.0f,
           "steps on ppq");
    // free running at the tempo: 1/8 at 120 = 4 Hz
    s.type = ModType::wave;
    s.shape = ModShape::sawUp;
    s.division = 5;
    m.set (s);
    m.retrigger();
    float v = 0;
    for (int i = 0; i < 125; ++i) v = m.advance (0.001, 120);
    CHECK (std::abs (v - 0.0f) < 0.02f, "saw at half its cycle %.3f", v);

    // random: a loop that comes back (VARI 0), one that doesn't (VARI 100 %)
    {
        ModSettings r;
        r.type = ModType::random;
        r.length = 5;
        r.division = 6;
        Modulator a;
        a.set (r);
        std::vector<float> first, second;
        for (int k = 0; k < 10; ++k)
            (k < 5 ? first : second).push_back (a.atPpq (k * 0.25 + 0.1, 120, 0.001));
        CHECK (first == second, "random loop comes back");
        bool allDifferent = false;
        for (int k = 1; k < 5; ++k) allDifferent = allDifferent || first[(size_t) k] != first[0];
        CHECK (allDifferent, "random loop varies within");
        r.vari = 1.0f;
        a.set (r);
        std::vector<float> third;
        for (int k = 10; k < 15; ++k) third.push_back (a.atPpq (k * 0.25 + 0.1, 120, 0.001));
        int same = 0;
        for (int k = 0; k < 5; ++k) same += third[(size_t) k] == first[(size_t) k];
        CHECK (same < 2, "vari 100 %% draws again (%d the same)", same);
    }
    // envelope: rises, settles on its sustain, falls once let go
    {
        ModSettings e;
        e.type = ModType::envelope;
        e.attack = 0.01f; e.decay = 0.1f; e.sustain = 0.4f; e.release = 0.2f;
        Modulator a;
        a.set (e);
        a.gate (true);
        float peak = 0, x = 0;
        for (int k = 0; k < 20; ++k) peak = std::max (peak, a.advance (0.001, 120));
        for (int k = 0; k < 500; ++k) x = a.advance (0.001, 120);
        CHECK (peak > 0.99f && std::abs (x - 0.4f) < 0.01f, "env peak %.3f sustain %.3f", peak, x);
        a.gate (false);
        for (int k = 0; k < 400; ++k) x = a.advance (0.001, 120);
        CHECK (x < 0.01f, "env released %.4f", x);
    }
    // follower: follows a level, falls back
    {
        ModSettings f;
        f.type = ModType::follower;
        f.attack = 0.005f; f.release = 0.1f; f.gain = 2.0f;
        Modulator a;
        a.set (f);
        a.input (0.3f);
        float x = 0;
        for (int k = 0; k < 100; ++k) x = a.advance (0.001, 120);
        CHECK (std::abs (x - 0.6f) < 0.01f, "follower %.3f", x);
        a.input (0.0f);
        for (int k = 0; k < 700; ++k) x = a.advance (0.001, 120);
        CHECK (x < 0.01f, "follower falls %.4f", x);
    }
}

int main()
{
    testOversampler();
    testFilters();
    testDrive();
    testEffects();
    testModulators();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
