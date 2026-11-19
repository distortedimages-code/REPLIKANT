// The player's DSP on its own: oversampling, the filters (stable, resonant where they should be, tuned), the drives,
// the effects (bounded, doing what they say), the modulators.

#include "../Source/dsp/Oversampler.h"
#include "../Source/dsp/VAFilter.h"
#include "../Source/dsp/Drive.h"
#include "../Source/dsp/Effects.h"
#include "../Source/dsp/Modulators.h"
#include "../Source/dsp/Cycles.h"
#include "../Source/engine/Listener.h"
#include "../Source/core/Analysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <functional>
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
    // (0.4.9) the Wasp: never sings by itself; its resonance gives way when the sound is loud; its lopsided inverters
    // add even harmonics; NOTCH, with SEP at 0, digs a hole at the cutoff
    {
        auto run = [&] (float res, float drive, FilterType type, float sep, std::function<float (int)> in, int n)
        {
            VAFilter f;
            f.prepare (sr * 2);
            FilterSettings s;
            s.model = FilterModel::wasp;
            s.type = type;
            s.res = res;
            s.drive = drive;
            s.sep = sep;
            f.set (s);
            std::vector<float> out;
            for (int i = 0; i < n; ++i)
                out.push_back (f.tick (in (i), std::log2 (500.0f / 20.0f)));
            return out;
        };
        // a click at full resonance, then silence: it rings, then dies away
        auto ring = run (1.0f, 0.0f, FilterType::lp, 0.0f, [] (int i) { return i < 4 ? 0.5f : 0.0f; }, (int) (sr * 2 * 1.0));
        float early = 0.0f, late = 0.0f;
        for (size_t i = 0; i < ring.size(); ++i)
        {
            if (i < ring.size() / 10)
                early = std::max (early, std::abs (ring[i]));
            else if (i > ring.size() * 4 / 5)
                late = std::max (late, std::abs (ring[i]));
        }
        CHECK (early > 1.0e-3f && late < 1.0e-5f, "wasp at full resonance rings and dies (no self-oscillation): %.2g then %.2g", early, late);
        // the peak at the cutoff, quiet then loud (relative): it gives way
        auto gainAt = [&] (float amp)
        {
            auto out = run (1.0f, 0.0f, FilterType::lp, 0.0f, [&] (int i) { return amp * (float) std::sin (2 * M_PI * 500.0 * i / (sr * 2)); }, (int) (sr * 2 * 0.5));
            return rmsDb (out, (int) (sr * 2 * 0.25)) - 20.0 * std::log10 (amp / std::sqrt (2.0));
        };
        const double quiet = gainAt (0.02f), loud = gainAt (0.8f);
        CHECK (quiet > 9.0 && loud < quiet - 4.0, "wasp's resonance: +%.1f dB quiet, +%.1f dB loud", quiet, loud);
        std::printf ("  wasp at full resonance: +%.1f dB at the cutoff quiet, +%.1f dB loud; rings %.2g, then %.2g\n", quiet, loud, early, late);
        std::printf ("  wasp's peak by level:");
        for (float a : { 0.05f, 0.1f, 0.2f, 0.3f, 0.5f })
            std::printf (" %.2f: %+.1f dB", a, gainAt (a));
        std::printf ("\n");
        // even harmonics: a 100 Hz sine through the low pass at 500 Hz, its second harmonic
        {
            auto out = run (0.0f, 0.5f, FilterType::lp, 0.0f, [&] (int i) { return 0.5f * (float) std::sin (2 * M_PI * 100.0 * i / (sr * 2)); }, (int) (sr * 2 * 0.5));
            auto amp = [&] (double hz)
            {
                double re = 0.0, im = 0.0;
                const int from = (int) out.size() / 2;
                for (int i = from; i < (int) out.size(); ++i)
                {
                    re += out[(size_t) i] * std::cos (2 * M_PI * hz * i / (sr * 2));
                    im += out[(size_t) i] * std::sin (2 * M_PI * hz * i / (sr * 2));
                }
                return std::sqrt (re * re + im * im);
            };
            const double h2 = 20.0 * std::log10 (amp (200.0) / amp (100.0));
            CHECK (h2 > -40.0 && h2 < -6.0, "wasp's even harmonics: the 2nd at %.1f dB", h2);
            std::printf ("  wasp's second harmonic (DRIVE 50 %%): %.1f dB\n", h2);
        }
        // NOTCH at SEP 0: a hole at the cutoff, the lows and highs through
        auto level = [&] (double hz)
        {
            auto out = run (0.3f, 0.0f, FilterType::notch, 0.0f, [&] (int i) { return 0.05f * (float) std::sin (2 * M_PI * hz * i / (sr * 2)); }, (int) (sr * 2 * 0.5));
            return rmsDb (out, (int) (sr * 2 * 0.25));
        };
        const double atCut = level (500.0), low = level (60.0), high = level (5000.0);
        CHECK (atCut < low - 18.0 && atCut < high - 18.0, "wasp's notch: %.1f dB at the cutoff, %.1f below, %.1f above", atCut, low, high);
        std::printf ("  wasp's notch: %.1f dB at the cutoff, %.1f below, %.1f above\n", atCut, low, high);
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
        e.unipolar = true;
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
        f.unipolar = true;
        Modulator a;
        a.set (f);
        a.input (0.3f);
        float x = 0;
        for (int k = 0; k < 100; ++k) x = a.advance (0.001, 120);
        CHECK (std::abs (x - 0.6f) < 0.01f, "follower %.3f", x);
        a.input (0.0f);
        for (int k = 0; k < 700; ++k) x = a.advance (0.001, 120);
        CHECK (x < 0.01f, "follower falls %.4f", x);
        // ±: around the setting, -1 in silence, +1 at full level
        f.unipolar = false;
        Modulator b;
        b.set (f);
        b.input (0.0f);
        for (int k = 0; k < 50; ++k) x = b.advance (0.001, 120);
        CHECK (std::abs (x + 1.0f) < 0.01f, "bipolar follower in silence %.3f", x);
        b.input (0.5f);
        for (int k = 0; k < 100; ++k) x = b.advance (0.001, 120);
        CHECK (std::abs (x - 1.0f) < 0.01f, "bipolar follower full %.3f", x);
    }
}


// SPECTRE (0.4.2): its pitches (to the cent), the band and the notch, BLUR a time constant whatever the slices, FREEZE,
// the slices on the grid, on the notes, on the attacks, TRANSIENTS quicker in the highs, WARP, FINE and OCTAVE,
// TRACK, the XF waves and the feedback bounded
struct SpectreRig
{
    double sr = 48000.0;
    Spectral sp;
    SpectralSettings st;
    std::vector<float> out;
    double t = 0.0;
    SpectreRig()
    {
        sp.prepare (sr);
        st.mix = 1.0f;
        st.glide = 0.0f;
        st.blur = 0.0f;
        st.slice = 0.1f;
    }
    // `seconds` of `gen` (time -> sample); `each` called at every block start (its time)
    template <typename G, typename E>
    void play (double seconds, G&& gen, E&& each)
    {
        std::vector<float> L (256), R (256);
        const int blocks = (int) (seconds * sr / 256.0);
        for (int b = 0; b < blocks; ++b)
        {
            each (t);
            sp.set (st);
            for (int i = 0; i < 256; ++i)
                L[(size_t) i] = R[(size_t) i] = gen (t + i / sr);
            sp.process (L.data(), R.data(), 256);
            out.insert (out.end(), L.begin(), L.end());
            t += 256.0 / sr;
        }
    }
    template <typename G> void play (double seconds, G&& gen)   { play (seconds, gen, [] (double) {}); }
    std::vector<std::pair<float, float>> now() const
    {
        float hz[33], lv[33];
        const int n = sp.peaks (hz, lv, 33);
        std::vector<std::pair<float, float>> v;
        for (int i = 0; i < n; ++i)
            v.push_back ({ hz[i], lv[i] });
        std::sort (v.begin(), v.end());
        return v;
    }
};

static float sine (double t, double hz, double a = 0.4) { return (float) (a * std::sin (2.0 * M_PI * hz * t)); }
static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

static void testSpectre()
{
    // its pitches, to a few cents (55 .. 440 Hz)
    for (double hz : { 55.0, 110.0, 220.0, 440.0 })
    {
        SpectreRig r;
        r.st.voices = 1;
        r.play (1.0, [hz] (double t) { return sine (t, hz); });
        const auto v = r.now();
        CHECK (v.size() == 1 && std::abs (cents (v[0].first, hz)) < 8.0, "spectre on %.0f Hz: %.2f Hz", hz, v.empty() ? 0.0 : v[0].first);
    }
    // the band keeps 2 kHz, the notch 220 Hz
    for (bool notch : { false, true })
    {
        SpectreRig r;
        r.st.voices = 2;
        r.st.centre = (float) (std::log (2000.0 / 40.0) / std::log (250.0));
        r.st.bandwidth = 0.0f;
        r.st.notch = notch;
        r.play (1.0, [] (double t) { return sine (t, 220.0, 0.3) + sine (t, 2000.0, 0.3); });
        const auto v = r.now();
        const double want = notch ? 220.0 : 2000.0;
        CHECK (v.size() == 1 && std::abs (cents (v[0].first, want)) < 10.0, "%s: %zu peaks, %.1f Hz", notch ? "notch" : "band", v.size(), v.empty() ? 0.0 : v[0].first);
    }
    // BLUR: 220 Hz then 330; the loudest peak moves after about the same time with slices of 11 ms and of 186 ms
    double moved[2] = { 0.0, 0.0 };
    for (int s = 0; s < 2; ++s)
    {
        SpectreRig r;
        r.st.voices = 1;
        r.st.blur = 0.5f;
        r.st.slice = s == 0 ? 0.05f : 0.6f;
        r.play (1.0, [] (double t) { return sine (t, 220.0); });
        r.play (2.5, [] (double t) { return sine (t, 330.0); }, [&] (double t)
        {
            const auto v = r.now();
            if (moved[s] == 0.0 && ! v.empty() && std::abs (cents (v[0].first, 330.0)) < 50.0)
                moved[s] = t - 1.0;
        });
    }
    std::printf ("  spectre blur: the peak moves after %.2f s (slices of 11 ms), %.2f s (186 ms)\n", moved[0], moved[1]);
    CHECK (moved[0] > 0.15 && moved[1] > 0.15 && std::abs (moved[0] - moved[1]) < 0.25, "blur in seconds: %.2f vs %.2f", moved[0], moved[1]);
    // FREEZE (BLUR at its end): the sound gone, the partial still there
    {
        SpectreRig r;
        r.st.voices = 1;
        r.play (1.0, [] (double t) { return sine (t, 220.0); });
        r.st.blur = 1.0f;
        r.play (1.0, [] (double) { return 0.0f; });
        const auto v = r.now();
        const double tail = rmsDb (std::vector<float> (r.out.end() - 24000, r.out.end()));
        CHECK (v.size() == 1 && std::abs (cents (v[0].first, 220.0)) < 8.0 && tail > -20.0, "frozen: %.1f dB, %zu peaks", tail, v.size());
    }
    // the slices: on the grid (quarters at 120: four in two seconds, and one on the start), on notes, on attacks
    {
        SpectreRig r;
        r.st.clock = 1;
        r.st.division = 4;      // 1/4
        r.st.bpm = 120.0;
        r.st.playing = true;
        r.play (2.0, [] (double t) { return sine (t, 220.0); }, [&] (double t) { r.st.ppq = t * 2.0; });
        CHECK (r.sp.analyses() == 4 || r.sp.analyses() == 5, "sync: %d analyses in 2 s of quarters", r.sp.analyses());
    }
    {
        SpectreRig r;
        r.st.clock = 2;
        bool did[2] = { false, false };
        r.play (2.0, [] (double t) { return sine (t, 220.0); }, [&] (double t)
        {
            for (int k = 0; k < 2; ++k)
                if (! did[k] && t >= 0.5 + k)
                {
                    did[k] = true;
                    r.sp.trigger();
                }
        });
        CHECK (r.sp.analyses() == 2, "notes: %d analyses for 2 notes", r.sp.analyses());
    }
    {
        SpectreRig r;
        r.st.clock = 3;
        r.play (2.0, [] (double t)
        {
            const double in = std::fmod (t, 0.25);
            return in < 0.08 ? (float) (std::exp (-in * 40.0) * 0.5 * std::sin (2.0 * M_PI * 300.0 * t)) : 0.0f;
        });
        CHECK (r.sp.analyses() >= 7 && r.sp.analyses() <= 9, "attacks: %d analyses for 8 hits", r.sp.analyses());
    }
    // TRANSIENTS: a quiet 5 kHz coming in over a loud 200 Hz is heard sooner
    double seen[2] = { 0.0, 0.0 };
    for (int tr = 0; tr < 2; ++tr)
    {
        SpectreRig r;
        r.st.voices = 2;
        r.st.slice = 0.0f;
        r.st.transients = tr == 1;
        r.play (1.0, [] (double t) { return sine (t, 200.0); });
        r.play (0.3, [] (double t) { return sine (t, 200.0) + sine (t, 5000.0, 0.15); }, [&] (double t)
        {
            for (auto& p : r.now())
                if (seen[tr] == 0.0 && std::abs (cents (p.first, 5000.0)) < 30.0 && p.second > 0.05f)
                    seen[tr] = t - 1.0;
        });
    }
    std::printf ("  spectre transients: 5 kHz heard after %.1f ms (long), %.1f ms (transients)\n", seen[0] * 1000.0, seen[1] * 1000.0);
    CHECK (seen[1] > 0.0 && seen[0] > 0.0 && seen[1] < seen[0] * 0.8, "transients quicker: %.3f vs %.3f", seen[1], seen[0]);
    // WARP: 200, 400 and 800 Hz drawn to their centre (400), or pushed out (100, 400, 1600); FINE and OCTAVE
    for (float w : { -1.0f, 1.0f })
    {
        SpectreRig r;
        r.st.voices = 3;
        r.st.warp = w;
        r.play (1.0, [] (double t) { return sine (t, 200.0, 0.25) + sine (t, 400.0, 0.25) + sine (t, 800.0, 0.25); });
        const auto v = r.now();
        bool ok = v.size() == 3;
        const double want[3] = { w < 0 ? 400.0 : 100.0, 400.0, w < 0 ? 400.0 : 1600.0 };
        for (size_t i = 0; ok && i < 3; ++i)
            ok = std::abs (cents (v[i].first, want[i])) < 15.0;
        CHECK (ok, "warp %+.0f: %zu peaks, %.1f %.1f %.1f", w, v.size(), v.size() > 0 ? v[0].first : 0.0f, v.size() > 1 ? v[1].first : 0.0f, v.size() > 2 ? v[2].first : 0.0f);
    }
    {
        SpectreRig r;
        r.st.voices = 1;
        r.st.octave = 3.0f;
        r.st.fine = 7.0f;
        r.play (1.0, [] (double t) { return sine (t, 220.0); });
        const auto v = r.now();
        CHECK (v.size() == 1 && std::abs (cents (v[0].first, 220.0 * 8.0 * std::pow (2.0, 7.0 / 12.0))) < 8.0, "octave +3, fine +7: %.1f", v.empty() ? 0.0 : v[0].first);
    }
    // TRACK: a partial coming in under two others: they stay where they are (RANK: they shift up a place, gliding)
    for (int a = 0; a < 2; ++a)
    {
        SpectreRig r;
        r.st.voices = 3;
        r.st.assign = a;
        r.st.glide = 0.6f;
        r.st.slice = 0.05f;
        r.play (1.0, [] (double t) { return sine (t, 300.0, 0.3) + sine (t, 500.0, 0.3); });
        r.play (0.25, [] (double t) { return sine (t, 100.0, 0.3) + sine (t, 300.0, 0.3) + sine (t, 500.0, 0.3); });
        double worst = 0.0;
        for (auto& p : r.now())
            if (p.second > 0.02f)
            {
                double near = 1e9;
                for (double want : { 100.0, 300.0, 500.0 })
                    near = std::min (near, std::abs (cents (p.first, want)));
                worst = std::max (worst, near);
            }
        CHECK (a == 1 ? worst < 30.0 : worst > 100.0, "%s: %.0f cents off at worst", a == 1 ? "track" : "rank", worst);
    }
    // its cost at its heaviest: 33 voices cross-fading saws, both analyses every 6 ms
    {
        SpectreRig r;
        r.st.voices = 33;
        r.st.wave = 6;
        r.st.transients = true;
        r.st.slice = 0.0f;
        r.st.assign = 1;
        const double t0 = now();
        r.play (5.0, [] (double t) { return sine (t, 150.0 * std::pow (2.0, std::floor (t * 8.0) / 12.0)) + sine (t, 900.0, 0.2) + sine (t, 3100.0, 0.1); });
        const double cpu = (now() - t0) / 5.0;
        std::printf ("  spectre at its heaviest: %.1f %% of real time\n", cpu * 100.0);
        CHECK (cpu < 0.25, "spectre cpu %.3f", cpu);
    }
    // the XF waves on a pitch changing each slice, and FEEDBACK at its end on silence: numbers only, bounded
    for (int wave : { 4, 5, 6 })
    {
        SpectreRig r;
        r.st.voices = 8;
        r.st.wave = wave;
        r.st.glide = 0.2f;
        r.play (3.0, [] (double t) { return sine (t, 150.0 * std::pow (2.0, std::floor (t * 8.0) / 12.0)) + sine (t, 900.0, 0.2); });
        float peak = 0.0f;
        bool finite = true;
        for (float x : r.out) { finite = finite && std::isfinite (x); peak = std::max (peak, std::abs (x)); }
        CHECK (finite && peak < 3.0f && peak > 0.1f, "wave %d: peak %.2f", wave, peak);
    }
    {
        SpectreRig r;
        r.st.voices = 33;
        r.st.feedback = 1.0f;
        r.st.blur = 0.3f;
        r.play (0.5, [] (double t) { return sine (t, 220.0) + sine (t, 1234.0, 0.3); });
        r.play (8.0, [] (double) { return 0.0f; });
        float peak = 0.0f;
        bool finite = true;
        for (float x : r.out) { finite = finite && std::isfinite (x); peak = std::max (peak, std::abs (x)); }
        const double tail = rmsDb (std::vector<float> (r.out.end() - 48000, r.out.end()));
        std::printf ("  spectre feedback at its end: peak %.2f, the last second %.1f dB\n", peak, tail);
        CHECK (finite && peak < 3.0f, "feedback bounded: %.2f", peak);
    }
}


// CUBE (0.4.4): its corners at their level, its filter as its drawing says (sines through it, oversampled), no jump
// between two points, never running away (X swept at audio rate, full resonance, full drive), its cost
static void testCube()
{
    CHECK (cube::count() == 10, "ten cubes: %d", cube::count());
    for (int c = 0; c < cube::count(); ++c)
        for (int i = 0; i < 8; ++i)
        {
            const float x = (float) ((i >> 2) & 1), y = (float) ((i >> 1) & 1), z = (float) (i & 1);
            double power = 0.0, top = -200.0;
            for (int k = 0; k < 96; ++k)
            {
                const double db = cube::responseDb (c, x, y, z, 0.0f, 40.0 * std::pow (300.0, (k + 0.5) / 96.0));
                power += std::pow (10.0, db / 10.0);
                top = std::max (top, db);
            }
            const double mean = 10.0 * std::log10 (power / 96.0);
            CHECK (mean < 0.6 && mean > -20.0 && top < 15.6, "cube %s corner %d: mean %.1f dB, top %.1f dB", cube::def (c).name, i, mean, top);
        }
    // no jumps: along each axis, a thousandth of the way never moves the response much more than the thousandths either
    // side of it (steep near a narrow peak, but never a step)
    double worstJump = 0.0;
    for (int c = 0; c < cube::count(); ++c)
        for (int axis = 0; axis < 3; ++axis)
            for (float t = 0.002f; t < 0.99f; t += 0.0473f)
                for (double hz : { 100.0, 700.0, 2500.0, 9000.0 })
                {
                    auto r = [&] (float v)
                    {
                        float p[3] = { 0.4f, 0.3f, 0.6f };
                        p[axis] = v;
                        return cube::responseDb (c, p[0], p[1], p[2], 0.5f, hz);
                    };
                    const double h = 0.001;
                    const double before = std::abs (r (t) - r ((float) (t - h))), now1 = std::abs (r ((float) (t + h)) - r (t)),
                                 after = std::abs (r ((float) (t + 2 * h)) - r ((float) (t + h)));
                    worstJump = std::max (worstJump, now1 - 3.0 * std::max (before, after));
                }
    CHECK (worstJump < 0.05, "cube continuous: a step of %.2f dB", worstJump);
    // sines through the filter (4x 48 kHz), against its drawing
    struct Spot { int cube; float x, y, z; };
    const double rate = 192000.0;
    double worstErr = 0.0;
    for (const Spot sp : { Spot { 0, 0.3f, 0.6f, 0.2f }, Spot { 1, 0.5f, 0.2f, 0.8f }, Spot { 5, 0.7f, 0.4f, 0.0f }, Spot { 2, 0.4f, 0.5f, 0.5f }, Spot { 7, 0.6f, 0.4f, 1.0f } })
        for (double hz : { 220.0, 1000.0, 3300.0 })
        {
            VAFilter f;
            f.prepare (rate);
            FilterSettings s;
            s.model = FilterModel::cube;
            s.cube = sp.cube;
            s.y = sp.y;
            s.z = sp.z;
            f.set (s);
            const float oct = sp.x * 9.965784f;
            double e = 0.0;
            const int n = (int) (rate * 0.6), from = (int) (rate * 0.4);
            for (int i = 0; i < n; ++i)
            {
                const float y = f.tick (0.02f * (float) std::sin (2.0 * M_PI * hz * i / rate), oct);
                if (i >= from)
                    e += (double) y * y;
            }
            const double got = 20.0 * std::log10 (std::sqrt (e / (n - from)) / (0.02 / std::sqrt (2.0)));
            const double want = cube::responseDb (sp.cube, sp.x, sp.y, sp.z, 0.0f, hz);
            if (want > -40.0)
                worstErr = std::max (worstErr, std::abs (got - want));
        }
    CHECK (worstErr < 1.5, "cube as drawn: %.2f dB off at worst", worstErr);
    // never running away, at 48 kHz and at 192: noise, X swept at 30 Hz over the whole cube, Y and Z moving, full
    // resonance and drive
    for (double rate2 : { 48000.0, 192000.0 })
        for (int c = 0; c < cube::count(); ++c)
        {
            VAFilter f;
            f.prepare (rate2);
            FilterSettings s;
            s.model = FilterModel::cube;
            s.cube = c;
            s.res = 1.0f;
            s.drive = 1.0f;
            std::mt19937 rng (7 + c);
            std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            float peak = 0.0f;
            bool finite = true;
            const int n = (int) (rate2 * 1.0);
            for (int i = 0; i < n; ++i)
            {
                if (i % 64 == 0)
                {
                    s.y = 0.5f + 0.5f * (float) std::sin (2.0 * M_PI * 3.0 * i / rate2);
                    s.z = 0.5f + 0.5f * (float) std::sin (2.0 * M_PI * 1.7 * i / rate2);
                    f.set (s);
                }
                const float oct = 5.0f + 5.2f * (float) std::sin (2.0 * M_PI * 30.0 * i / rate2);
                const float y = f.tick (0.7f * u (rng), oct);
                finite = finite && std::isfinite (y);
                peak = std::max (peak, std::abs (y));
            }
            CHECK (finite && peak <= 2.0f, "cube %s at %.0f Hz swept: peak %.2f", cube::def (c).name, rate2, peak);
        }
    // its cost: a voice's filter at 4x, X sliding
    {
        VAFilter f;
        f.prepare (192000.0);
        FilterSettings s;
        s.model = FilterModel::cube;
        s.cube = 0;
        f.set (s);
        const double t0 = now();
        float acc = 0.0f;
        for (int i = 0; i < 192000; ++i)
            acc += f.tick ((float) ((i * 7919) % 200) / 100.0f - 1.0f, 2.0f + 6.0f * (float) i / 192000.0f);
        const double cpu = now() - t0;
        VAFilter m;
        m.prepare (192000.0);
        FilterSettings ms;
        ms.model = FilterModel::moog;
        ms.res = 0.5f;
        m.set (ms);
        const double t1 = now();
        for (int i = 0; i < 192000; ++i)
            acc += m.tick ((float) ((i * 7919) % 200) / 100.0f - 1.0f, 2.0f + 6.0f * (float) i / 192000.0f);
        const double moog = now() - t1;
        std::printf ("  cube: a voice's filter at 4x, X sliding: %.2f %% of real time (the ladder: %.2f %%)%s\n", cpu * 100.0, moog * 100.0, acc > 1e30f ? "!" : "");
        CHECK (cpu < 0.05, "cube cpu %.3f", cpu);
    }
}


// (0.4.5) no clicks where a knob jumps: a model, a type, a mode or a wave changed while a tone plays never makes a
// step bigger than the tone's own (its steepest slope, with room for the harmonics the change brings)
static void testNoClicks()
{
    const double sr = 48000.0;
    auto tone = [sr] (int i) { return 0.5f * (float) std::sin (2.0 * M_PI * 220.0 * i / sr); };
    auto worstStep = [] (const std::vector<float>& y, size_t from)
    {
        float w = 0.0f;
        for (size_t i = from + 1; i < y.size(); ++i)
            w = std::max (w, std::abs (y[i] - y[i - 1]));
        return w;
    };
    // the drive: every model in turn, ten times a second
    {
        Drive d;
        d.prepare (sr);
        DriveSettings s;
        s.drive = 0.4f;
        std::vector<float> L, R;
        for (int b = 0; b < 40; ++b)
        {
            s.model = (DriveModel) (1 + b % 3);
            d.set (s);
            std::vector<float> l (480), r (480);
            for (int i = 0; i < 480; ++i)
                l[(size_t) i] = r[(size_t) i] = tone (b * 480 + i);
            d.process (l.data(), r.data(), 480);
            L.insert (L.end(), l.begin(), l.end());
        }
        const float w = worstStep (L, 4800);
        CHECK (w < 0.12f, "drive models switched: a step of %.3f", w);
    }
    // the filter: its types and models in turn
    {
        VAFilter f;
        f.prepare (sr);
        FilterSettings s;
        s.res = 0.5f;
        std::vector<float> y;
        for (int b = 0; b < 48; ++b)
        {
            s.model = (FilterModel) (1 + (b / 4) % 7);
            s.type = (FilterType) (b % 4);
            s.cube = b % 10;
            f.set (s);
            for (int i = 0; i < 480; ++i)
                y.push_back (f.tick (tone (b * 480 + i), 6.5f));
        }
        const float w = worstStep (y, 4800);
        CHECK (w < 0.25f, "filter models and types switched: a step of %.3f", w);
    }
    // the chorus' modes, SPECTRE's waves
    {
        Chorus c;
        c.prepare (sr);
        ChorusSettings s;
        s.mix = 0.6f;
        std::vector<float> L;
        for (int b = 0; b < 30; ++b)
        {
            s.mode = b % 3;
            c.set (s);
            std::vector<float> l (480), r (480);
            for (int i = 0; i < 480; ++i)
                l[(size_t) i] = r[(size_t) i] = tone (b * 480 + i);
            c.process (l.data(), r.data(), 480);
            L.insert (L.end(), l.begin(), l.end());
        }
        const float w = worstStep (L, 4800);
        CHECK (w < 0.08f, "chorus modes switched: a step of %.3f", w);
    }
    {
        // (a square has its own steps: against the steepest of the waves held, not against the tone)
        auto run = [&] (int fixed)
        {
            Spectral sp;
            sp.prepare (sr);
            SpectralSettings s;
            s.mix = 1.0f;
            s.voices = 4;
            s.glide = 0.0f;
            std::vector<float> L;
            for (int b = 0; b < 60; ++b)
            {
                s.wave = fixed >= 0 ? fixed : (b < 20 ? 0 : (b % 4 == 0 ? 0 : (b % 4 == 1 ? 1 : (b % 4 == 2 ? 4 : 1))));
                sp.set (s);
                std::vector<float> l (480), r (480);
                for (int i = 0; i < 480; ++i)
                    l[(size_t) i] = r[(size_t) i] = tone (b * 480 + i);
                sp.process (l.data(), r.data(), 480);
                L.insert (L.end(), l.begin(), l.end());
            }
            return worstStep (L, 20 * 480);
        };
        const float held = std::max ({ run (0), run (1), run (4) }), switched = run (-1);
        CHECK (switched < held * 1.1f + 0.02f, "spectre's waves switched: a step of %.3f (held: %.3f)", switched, held);
    }
}


// (0.4.5) the effects keep the sound as loud as it came in (a chord of saws with noisy hits, at their default mixes):
// none thins it, none jumps over it
static void testEffectLevels()
{
    const double sr = 48000.0;
    std::vector<float> x ((size_t) (sr * 4));
    {
        std::mt19937 r (3);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        double ph[3] = { 0, 0, 0 };
        const double f[3] = { 110, 138.6, 164.8 };
        for (size_t i = 0; i < x.size(); ++i)
        {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                ph[k] += f[k] / sr;
                ph[k] -= std::floor (ph[k]);
                v += (float) (2.0 * ph[k] - 1.0) * 0.15f;
            }
            x[i] = v + 0.2f * u (r) * (float) std::exp (-std::fmod (i / sr, 0.5) * 8.0);
        }
    }
    auto levelOf = [&] (auto&& fx)
    {
        auto L = x, R = x;
        for (size_t i = 0; i < L.size(); i += 256)
            fx (L.data() + i, R.data() + i, (int) std::min<size_t> (256, L.size() - i));
        double ei = 0.0, eo = 0.0;
        for (size_t i = (size_t) sr; i < x.size(); ++i)
        {
            ei += (double) x[i] * x[i];
            eo += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
        }
        return 10.0 * std::log10 (eo / ei);
    };
    double worst = 0.0;
    const char* worstName = "";
    auto check = [&] (const char* name, double db) { if (std::abs (db) > std::abs (worst)) { worst = db; worstName = name; } };
    { Spectral e; e.prepare (sr); SpectralSettings st; e.set (st); check ("spectre", levelOf ([&] (float* l, float* r, int n) { e.process (l, r, n); })); }
    { PitchShift e; e.prepare (sr); PitchSettings st; e.set (st); check ("pitch", levelOf ([&] (float* l, float* r, int n) { e.process (l, r, n); })); }
    for (int mode : { 0, 1 }) { Chorus e; e.prepare (sr); ChorusSettings st; st.mode = mode; e.set (st); check ("chorus", levelOf ([&] (float* l, float* r, int n) { e.process (l, r, n); })); }
    { Crush e; e.prepare (sr); CrushSettings st; st.bits = 6.0f; st.rateHz = 8000.0f; e.set (st); check ("crush", levelOf ([&] (float* l, float* r, int n) { e.process (l, r, n); })); }
    { Space e; e.prepare (sr); SpaceSettings st; e.set (st); check ("espace", levelOf ([&] (float* l, float* r, int n) { e.process (l, r, n); })); }
    for (int m : { 1, 2, 3 }) { Drive e; e.prepare (sr); DriveSettings st; st.model = (DriveModel) m; st.drive = 0.7f; e.set (st); check ("drive", levelOf ([&] (float* l, float* r, int n) { e.process (l, r, n); })); }
    std::printf ("  the effects' levels at their defaults: %+.2f dB at worst (%s)\n", worst, worstName);
    CHECK (std::abs (worst) < 1.5, "an effect moves the level by %.2f dB (%s)", worst, worstName);
}

// ECHO (0.4.5): its heads where SPREAD puts them, reversed heads, SCAN's intervals, bounded past 100 % of feedback,
// the dry untouched, TIME turned without a click
static void testEcho()
{
    const double sr = 48000.0;
    auto run = [sr] (EchoSettings st, std::vector<float>& l, std::vector<float>& r)
    {
        Echo e;
        e.prepare (sr);
        e.set (st);
        e.reset();
        for (size_t at = 0; at < l.size(); at += 32)
        {
            const int n = (int) std::min<size_t> (32, l.size() - at);
            e.process (l.data() + at, r.data() + at, n);
        }
    };
    // an impulse, four heads over 100 ms: echoes at 25, 50, 75 and 100 ms
    {
        EchoSettings st;
        st.time = 0.1f; st.heads = 4; st.spread = 1.0f; st.feedback = 0.0f; st.mix = 1.0f; st.wear = 0.0f; st.width = 0.0f;
        std::vector<float> l ((size_t) sr / 2, 0.0f), r = l;
        l[100] = r[100] = 1.0f;
        run (st, l, r);
        bool all = true;
        for (int k = 1; k <= 4; ++k)
        {
            const size_t at = 100 + (size_t) (0.025 * sr * k);
            float peak = 0.0f;
            for (size_t i = at - 3; i <= at + 3; ++i)
                peak = std::max (peak, std::abs (l[i]));
            all = all && peak > 0.35f;
            if (peak <= 0.35f)
                std::printf ("  head %d: %.3f\n", k, peak);
        }
        float stray = 0.0f;
        for (size_t i = 200; i < (size_t) (0.024 * sr); ++i)
            stray = std::max (stray, std::abs (l[i]));
        CHECK (all && stray < 0.01f, "four heads at 25, 50, 75, 100 ms (stray %.4f)", stray);
        CHECK (std::abs (l[100] - 1.0f) < 1.0e-6f, "the dry untouched: %.6f", l[100]);
    }
    // SCAN, one head (it goes up), a full sweep: an octave above
    {
        EchoSettings st;
        st.time = 0.3f; st.heads = 1; st.feedback = 0.0f; st.mix = 1.0f; st.wear = 0.0f; st.motion = 2; st.sweep = 1.0f;
        std::vector<float> l ((size_t) sr * 2), r;
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 300.0 * (double) i / sr);
        r = l;
        auto dry = l;
        run (st, l, r);
        std::vector<float> wet (l.size());
        for (size_t i = 0; i < l.size(); ++i)
            wet[i] = l[i] - dry[i];
        auto tone = [&wet, sr] (double hz)
        {
            double re = 0, im = 0;
            for (size_t i = (size_t) sr; i < wet.size(); ++i)
            {
                re += wet[i] * std::cos (2.0 * 3.14159265358979 * hz * (double) i / sr);
                im += wet[i] * std::sin (2.0 * 3.14159265358979 * hz * (double) i / sr);
            }
            return std::sqrt (re * re + im * im);
        };
        CHECK (tone (600.0) > 10.0 * tone (300.0), "SCAN an octave up: 600 Hz %.1f against 300 Hz %.1f", tone (600.0), tone (300.0));
    }
    // reversed heads: the echo of a rising ramp comes back falling
    {
        EchoSettings st;
        st.time = 0.4f; st.heads = 1; st.feedback = 0.0f; st.mix = 1.0f; st.wear = 0.0f; st.direction = 1;
        std::vector<float> l ((size_t) sr * 2, 0.0f), r;
        for (size_t i = 0; i < (size_t) (sr * 1.5); ++i)
            l[i] = (float) ((double) (i % 4800) / 4800.0) * 0.5f;    // saws rising every 100 ms
        r = l;
        auto dry = l;
        run (st, l, r);
        int falling = 0, rising = 0;
        for (size_t i = (size_t) sr; i < (size_t) (sr * 1.4); ++i)
        {
            const float d = (l[i] - dry[i]) - (l[i - 1] - dry[i - 1]);
            if (d < -1.0e-5f && d > -0.01f) ++falling;
            if (d > 1.0e-5f && d < 0.01f) ++rising;
        }
        CHECK (falling > 3 * rising, "REV: the ramps come back falling (%d falling, %d rising)", falling, rising);
    }
    // 110 % of feedback, noise for 10 s, all of it moving: held by the tape
    {
        EchoSettings st;
        st.time = 0.15f; st.heads = 4; st.feedback = 1.1f; st.mix = 1.0f; st.wear = 0.5f; st.motion = 1; st.sweep = 1.0f; st.rate = 3.0f; st.direction = 2;
        std::vector<float> l ((size_t) sr * 10), r;
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        for (auto& x : l) x = u (rng);
        r = l;
        run (st, l, r);
        float peak = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
        }
        CHECK (finite && peak < 4.0f, "110 %% feedback held: peak %.2f", peak);
    }
    // TIME turned while a sine plays: the echo glides (no step bigger than the sine's own)
    {
        Echo e;
        e.prepare (sr);
        EchoSettings st;
        st.time = 0.2f; st.heads = 2; st.feedback = 0.5f; st.mix = 0.8f; st.wear = 0.0f;
        e.set (st);
        std::vector<float> l ((size_t) sr * 3), r;
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) i / sr);
        r = l;
        float worst = 0.0f;
        for (size_t at = 0; at < l.size(); at += 32)
        {
            if (at == (size_t) sr) { st.time = 0.05f; e.set (st); }
            if (at == (size_t) (sr * 2)) { st.heads = 4; st.motion = 2; st.sweep = 0.5f; st.direction = 2; e.set (st); }
            e.process (l.data() + at, r.data() + at, 32);
        }
        for (size_t i = (size_t) (sr * 0.5); i < l.size(); ++i)
            worst = std::max (worst, std::abs (l[i] - l[i - 1]));
        // (the sine moves at most 0.3 * 2π 220 / 48000 = 0.0086 a sample; its echoes, three times that)
        CHECK (worst < 0.06f, "TIME and the heads changed without a click: worst step %.4f", worst);
    }
    // the cost: four heads scanning, reversed
    {
        Echo e;
        e.prepare (sr);
        EchoSettings st;
        st.heads = 4; st.motion = 2; st.direction = 2; st.feedback = 0.6f;
        e.set (st);
        std::vector<float> l (32, 0.1f), r (32, 0.1f);
        const double t0 = now();
        for (int b = 0; b < 15000; ++b)
            e.process (l.data(), r.data(), 32);
        const double cpu = (now() - t0) / (15000.0 * 32.0 / sr) * 100.0;
        std::printf ("  ECHO, four heads scanning: %.2f %% of a core\n", cpu);
        CHECK (cpu < 8.0, "ECHO's cost %.2f %%", cpu);
    }
}

// COMP (0.4.5): nothing at 0, a limiter at the middle, past it the louder comes out quieter, a key that pumps,
// the gate
static void testComp()
{
    const double sr = 48000.0, pi = 3.14159265358979;
    auto sine = [sr, pi] (float level, double seconds)
    {
        std::vector<float> v ((size_t) (seconds * sr));
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = level * (float) std::sin (2.0 * pi * 200.0 * (double) i / sr);
        return v;
    };
    auto run = [sr] (CompSettings cs, std::vector<float> l, const std::vector<float>* key = nullptr)
    {
        Comp c;
        c.prepare (sr);
        c.set (cs);
        c.reset();
        auto r = l;
        for (size_t at = 0; at < l.size(); at += 32)
        {
            const int n = (int) std::min<size_t> (32, l.size() - at);
            c.process (l.data() + at, r.data() + at, n, key != nullptr ? key->data() + at : nullptr, nullptr);
        }
        return l;
    };
    auto peakDb = [] (const std::vector<float>& v, size_t from, size_t to)
    {
        float p = 0.0f;
        for (size_t i = from; i < to && i < v.size(); ++i) p = std::max (p, std::abs (v[i]));
        return 20.0f * std::log10 (p + 1.0e-9f);
    };
    {
        CompSettings cs;
        cs.amount = 0.0f;
        auto in = sine (0.5f, 0.5);
        auto out = run (cs, in);
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i) worst = std::max (worst, std::abs (out[i] - in[i]));
        CHECK (worst < 1.0e-4f, "AMOUNT 0: nothing done (%.2e)", worst);
    }
    {
        CompSettings cs;
        cs.amount = 0.5f;
        cs.attack = 1;
        cs.release = 1;
        // loud and soft by turns (-6 and -18 dB, 150 ms each): the middle brings them together, past it the loud
        // ones come out under the soft ones
        auto turns = sine (0.5f, 2.0);
        for (size_t i = 0; i < turns.size(); ++i)
            if ((i / 7200) % 2 == 1)
                turns[i] *= 0.25f;
        auto out = run (cs, turns);
        const float loud = peakDb (out, 7200 * 10 + 2400, 7200 * 11), soft = peakDb (out, 7200 * 11 + 2400, 7200 * 12);
        CHECK (std::abs (loud - soft) < 2.0f, "the middle limits: -6 dB in → %.1f, -18 dB in → %.1f", loud, soft);
        cs.amount = 1.0f;
        out = run (cs, turns);
        const float over = peakDb (out, 7200 * 10 + 2400, 7200 * 11), under = peakDb (out, 7200 * 11 + 2400, 7200 * 12);
        CHECK (over < under - 2.0f, "past it, louder in comes out quieter: -6 dB → %.1f, -18 dB → %.1f", over, under);
        // and the loudness kept, whatever AMOUNT
        auto level = [] (const std::vector<float>& v) { double e = 0; for (size_t i = v.size() / 2; i < v.size(); ++i) e += (double) v[i] * v[i];
                                                         return 10.0 * std::log10 (e / (double) (v.size() / 2) + 1e-30); };
        const double ref = level (turns);
        double worst = 0.0;
        for (float a : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            cs.amount = a;
            worst = std::max (worst, std::abs (level (run (cs, turns)) - ref));
        }
        CHECK (worst < 2.5, "COMP keeps the loudness (worst %.1f dB)", worst);
    }
    {
        // a key bursting every 250 ms for 60 ms: the sine ducks with it
        CompSettings cs;
        cs.amount = 0.5f;
        cs.attack = 0;
        cs.release = 1;
        auto in = sine (0.1f, 1.0);
        std::vector<float> key (in.size(), 0.0f);
        for (size_t i = 0; i < key.size(); ++i)
            if (i % 12000 < 2880)
                key[i] = 0.8f * (float) std::sin (2.0 * pi * 60.0 * (double) i / sr);
        auto out = run (cs, in, &key);
        const float during = peakDb (out, 24000 + 1500, 24000 + 2800), between = peakDb (out, 24000 + 9000, 24000 + 11900);
        CHECK (during < between - 8.0f, "the key pumps it: %.1f dB under the burst, %.1f between", during, between);
    }
    {
        CompSettings cs;
        cs.gateDb = -40.0f;
        auto out = run (cs, sine (0.001f, 0.5));
        CHECK (peakDb (out, 12000, 24000) < -100.0f, "the gate closes under its threshold: %.1f dB", peakDb (out, 12000, 24000));
        auto open = run (cs, sine (0.1f, 0.5));
        CHECK (peakDb (open, 12000, 24000) > -21.0f, "and stays open over it: %.1f dB", peakDb (open, 12000, 24000));
    }
}

// CAPTURE CYCLES, LISTEN (0.4.5): the take starts at the threshold (with what came just before), ends in the silence;
// cut into cycles that follow a sweep, at the note's pitch, lined up with each other
static void testListen()
{
    const double sr = 48000.0, pi = 3.14159265358979;
    // a note at 110 Hz, its brightness opening over 3 s (a saw through a one-pole whose cutoff climbs), 0.5 s of
    // hiss before, 2 s after
    std::vector<float> in ((size_t) (sr * 5.5), 0.0f);
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    double ph = 0.0, lp = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        in[i] = 0.0005f * u (rng);
        const double t = (double) i / sr - 0.5;
        if (t >= 0.0 && t < 3.0)
        {
            ph += 110.0 / sr;
            ph -= std::floor (ph);
            const double saw = 2.0 * ph - 1.0;
            const double fc = 150.0 * std::pow (2.0, t * 2.2);
            lp += (saw - lp) * (1.0 - std::exp (-2.0 * pi * fc / sr));
            in[i] += (float) (0.4 * lp * std::min (1.0, t / 0.01));
        }
    }
    CycleListener l;
    CHECK (l.arm (sr, -40.0f, 30.0), "LISTEN armed");
    for (size_t at = 0; at < in.size(); at += 256)
    {
        const float* chans[1] = { in.data() + at };
        l.process (chans, 1, (int) std::min<size_t> (256, in.size() - at));
    }
    CHECK (l.state.load() == CycleListener::done, "LISTEN: the take ended in the silence (state %d)", l.state.load());
    auto take = l.take();
    const double secs = (double) take.size() / sr;
    CHECK (secs > 2.9 && secs < 3.3, "LISTEN: the take holds the note, %.2f s", secs);
    CHECK (l.state.load() == CycleListener::idle, "taken: idle again");

    const double hint = findPeriod (take.data(), (int) take.size(), (int) (sr * 1.5), sr);
    CHECK (std::abs (hint - sr / 110.0) < 2.0, "the period found: %.1f frames (%.1f)", hint, sr / 110.0);
    std::vector<int> where;
    auto cycles = cyclesFromTake (take.data(), (int) take.size(), sr, 16, &where);
    CHECK (cycles.size() == 16, "16 cycles out of the take (%d)", (int) cycles.size());
    if (cycles.size() == 16)
    {
        // brighter and brighter: the energy over the 4th harmonic, first against last
        auto bright = [] (const std::vector<float>& c)
        {
            double hi = 0.0, all = 0.0;
            const int n = (int) c.size();
            for (int h = 1; h <= 40; ++h)
            {
                double re = 0.0, im = 0.0;
                for (int t = 0; t < n; ++t)
                {
                    re += c[(size_t) t] * std::cos (2.0 * 3.14159265358979 * h * t / n);
                    im += c[(size_t) t] * std::sin (2.0 * 3.14159265358979 * h * t / n);
                }
                const double m = re * re + im * im;
                all += m;
                if (h >= 4) hi += m;
            }
            return hi / (all + 1e-12);
        };
        CHECK (bright (cycles.back()) > 4.0 * bright (cycles.front()), "the sweep followed: %.3f to %.3f", bright (cycles.front()), bright (cycles.back()));
        bool ordered = true;
        for (size_t i = 1; i < where.size(); ++i)
            ordered = ordered && where[i] > where[i - 1];
        CHECK (ordered, "the cycles in their order");
        // lined up: each close to the one before
        double worst = 0.0;
        for (size_t i = 1; i < cycles.size(); ++i)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (size_t t = 0; t < cycles[i].size(); ++t)
            {
                xy += cycles[i][t] * cycles[i - 1][t];
                xx += cycles[i][t] * cycles[i][t];
                yy += cycles[i - 1][t] * cycles[i - 1][t];
            }
            worst = std::max (worst, 1.0 - xy / std::sqrt (xx * yy + 1e-30));
        }
        CHECK (worst < 0.35, "lined up (the worst neighbours %.3f apart)", worst);
    }
    // stopped while it waits: nothing taken
    CycleListener w;
    w.arm (sr, -20.0f, 30.0);
    std::vector<float> quiet (512, 0.001f);
    const float* qc[1] = { quiet.data() };
    w.process (qc, 1, 512);
    w.stop();
    w.process (qc, 1, 512);
    CHECK (w.state.load() == CycleListener::idle && w.take().empty(), "stopped before the threshold: nothing");
}

// LIMIT (0.4.7): nothing past CEILING (between the samples either), what stays under it comes out as it went in, the
// latency it says
static void testLimiter()
{
    const double sr = 48000.0, pi = 3.14159265358979;
    Limiter lim;
    lim.prepare (sr);
    LimiterSettings ls;
    ls.ceilingDb = -1.0f;
    lim.set (ls);
    lim.reset();
    const int lat = lim.latency();
    CHECK (lat >= 70 && lat <= 80, "LIMIT's latency at 48 kHz: %d frames", lat);
    // a loud mess: noise, bursts at +12 dB, a sine near Nyquist (its peaks fall between the samples)
    std::vector<float> l ((size_t) sr * 3), r;
    std::mt19937 rng (9);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const bool burst = (i / 4800) % 5 == 0;
        l[i] = (burst ? 4.0f : 0.8f) * u (rng) + 1.5f * (float) std::sin (2.0 * pi * 11025.0 * (double) i / sr + 0.4);
    }
    r = l;
    for (size_t at = 0; at < l.size(); at += 256)
        lim.process (l.data() + at, r.data() + at, (int) std::min<size_t> (256, l.size() - at));
    const float ceil = std::pow (10.0f, -1.0f / 20.0f);
    float worst = 0.0f, between = 0.0f;
    for (size_t i = 3; i < l.size(); ++i)
    {
        worst = std::max (worst, std::abs (l[i]));
        for (float f : { 0.25f, 0.5f, 0.75f })
        {
            const float p0 = l[i - 3], p1 = l[i - 2], p2 = l[i - 1], p3 = l[i];
            const float c1 = 0.5f * (p2 - p0), c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3, c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
            between = std::max (between, std::abs (((c3 * f + c2) * f + c1) * f + p1));
        }
    }
    CHECK (worst <= ceil + 1.0e-6f, "nothing over CEILING: %.4f (%.4f)", worst, ceil);
    CHECK (between <= ceil * 1.035f, "between the samples, at most 0.3 dB over: %.4f", between);
    // quiet: as it went in, only late
    Limiter q;
    q.prepare (sr);
    q.set (ls);
    q.reset();
    std::vector<float> a ((size_t) sr / 2), b;
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = 0.3f * (float) std::sin (2.0 * pi * 220.0 * (double) i / sr);
    auto in = a;
    b = a;
    q.process (a.data(), b.data(), (int) a.size());
    float diff = 0.0f;
    for (size_t i = (size_t) lat; i < a.size(); ++i)
        diff = std::max (diff, std::abs (a[i] - in[i - (size_t) lat]));
    CHECK (diff < 1.0e-6f, "under CEILING: untouched, %d frames late (%.2e)", lat, diff);
}

int main()
{
    testOversampler();
    testFilters();
    testDrive();
    testEffects();
    testSpectre();
    testCube();
    testNoClicks();
    testEffectLevels();
    testEcho();
    testComp();
    testListen();
    testLimiter();
    testModulators();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
