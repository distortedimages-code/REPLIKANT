// The DSP core, on its own: interpolation, formants, loops, pitch, the plan, WAV chunks, captures.

#include "../Source/core/RkCore.h"
#include "../Source/core/Sinc.h"
#include "../Source/core/Analysis.h"
#include "../Source/core/LoopFinder.h"
#include "../Source/core/Formant.h"
#include "../Source/core/Transpose.h"
#include "../Source/core/Plan.h"
#include "../Source/core/Wav.h"
#include "../Source/core/Capture.h"
#include "TestSignals.h"

#include "signalsmith-linear/fft.h"

#include <chrono>
#include <cstdio>
#include <complex>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace rk;

static double now()
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

// error of `got` against a sine at hz, best-fitting amplitude and phase, as dB below the signal
static double sineErrorDb (const float* x, int n, double sr, double hz)
{
    double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0, xx = 0, sc2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double s = std::sin (6.283185307179586 * hz * i / sr), c = std::cos (6.283185307179586 * hz * i / sr);
        ss += s * s; cc += c * c; sc2 += s * c;
        xs += x[i] * s; xc += x[i] * c; xx += (double) x[i] * x[i];
    }
    (void) sc;
    const double det = ss * cc - sc2 * sc2;
    const double a = (xs * cc - xc * sc2) / det, b = (xc * ss - xs * sc2) / det;
    double err = 0, sig = 0;
    for (int i = 0; i < n; ++i)
    {
        const double fit = a * std::sin (6.283185307179586 * hz * i / sr) + b * std::cos (6.283185307179586 * hz * i / sr);
        err += (x[i] - fit) * (x[i] - fit);
        sig += fit * fit;
    }
    return 10.0 * std::log10 (sig / std::max (1e-30, err));
}

static void testNames()
{
    CHECK (noteName (60) == "C3", "%s", noteName (60).c_str());
    CHECK (noteName (0) == "C-2", "%s", noteName (0).c_str());
    CHECK (noteName (61) == "C#3", "-");
    CHECK (parseNote ("C3") == 60, "-");
    CHECK (parseNote ("a#-1") == 22, "%d", parseNote ("a#-1"));
    CHECK (parseNote ("Eb2") == 51, "%d", parseNote ("Eb2"));
    CHECK (parseNote ("H3") == -1, "-");
}

static void testSinc()
{
    const double sr = 48000;
    const int n = 48000;
    const auto s = test::sine (sr, 1000.0, n);
    SourceView v { s.data(), n };
    for (double step : { 0.7, 1.0 / 1.0594630943592953, 1.5, 2.3 })
    {
        std::vector<float> out (8000);
        resample (v, 1000.0, step, out.data(), (int) out.size());
        const double db = sineErrorDb (out.data(), (int) out.size(), sr, 1000.0 * step);
        CHECK (db > 90.0, "step %.3f: SNR %.1f dB", step, db);
    }
    // a tone that would fold back: 15 kHz played twice as fast = 30 kHz, gone
    {
        const auto hi = test::sine (sr, 15000.0, n);
        SourceView hv { hi.data(), n };
        std::vector<float> out (8000);
        resample (hv, 1000.0, 2.0, out.data(), (int) out.size());
        double e = 0;
        for (float x : out) e += (double) x * x;
        const double db = 10.0 * std::log10 (e / out.size() / 0.125);
        CHECK (db < -85.0, "aliasing at %.1f dB", db);
    }
    // 20 kHz kept at unity-ish speed (flat top)
    {
        const auto hi = test::sine (sr, 20000.0, n);
        SourceView hv { hi.data(), n };
        std::vector<float> out (8000);
        resample (hv, 1000.3, 0.999, out.data(), (int) out.size());
        double e = 0;
        for (float x : out) e += (double) x * x;
        const double db = 10.0 * std::log10 (e / out.size() / 0.125);
        CHECK (db > -1.0, "20 kHz at %.2f dB", db);
    }
    // the live kernel too
    {
        std::vector<float> out (4000);
        for (int i = 0; i < 4000; ++i)
            out[(size_t) i] = interpolate (v, 2000.0 + i * 1.1225, 0.9 / 1.1225, SincKernel::live());
        const double db = sineErrorDb (out.data(), 4000, sr, 1000.0 * 1.1225);
        CHECK (db > 70.0, "live kernel SNR %.1f dB", db);
    }
}

// amplitudes (dB) of the harmonics of f0 up to maxHz, over the middle of the sound, levels relative to the loudest
static std::vector<double> harmonicsDb (const Channels& ch, double sr, double f0, double maxHz)
{
    const auto m = mixDown (ch);
    const int n = (int) (sr * 0.5), start = (int) m.size() / 2 - n / 2;
    std::vector<double> a;
    double top = 0.0;
    for (int h = 1; h * f0 < maxHz; ++h)
    {
        double xs = 0, xc = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (6.283185307179586 * i / n);
            const double ph = 6.283185307179586 * h * f0 * (start + i) / sr;
            xs += w * m[(size_t) (start + i)] * std::sin (ph);
            xc += w * m[(size_t) (start + i)] * std::cos (ph);
        }
        a.push_back (std::sqrt (xs * xs + xc * xc));
        top = std::max (top, a.back());
    }
    for (auto& v : a)
        v = 20.0 * std::log10 (std::max (1e-12, v / top));
    return a;
}

static double rmsDiff (const std::vector<double>& a, const std::vector<double>& b)
{
    double e = 0;
    const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        e += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt (e / std::max<size_t> (1, n));
}

static void testFormant()
{
    const double sr = 48000;
    test::Tone t;
    t.hz = noteHz (55);
    t.formantHz = 1000.0;
    t.formantQ = 6.0;
    t.seconds = 1.5;
    Sample s;
    s.root = 55;
    s.sampleRate = sr;
    s.audio = test::make (t);

    for (double st : { 5.0, -7.0, 3.0 })
    {
        // the synth itself playing that note: same resonance, other pitch
        test::Tone real = t;
        real.hz = noteHz (55 + st);
        const auto truth = harmonicsDb (test::make (real), sr, real.hz, 6000.0);

        const double t0 = now();
        auto c = transposed (s, st, RenderSettings { Algo::classic });
        auto f = transposed (s, st, RenderSettings { Algo::formant });
        const double t1 = now();
        const double errC = rmsDiff (harmonicsDb (c.audio, sr, real.hz, 6000.0), truth);
        const double errF = rmsDiff (harmonicsDb (f.audio, sr, real.hz, 6000.0), truth);
        std::printf ("  formant %+.0f st: harmonics off by %.2f dB with varispeed, %.2f dB corrected (%.2f s)\n",
                     st, errC, errF, t1 - t0);
        CHECK (errF < errC * 0.4 && errF < 1.5, "%+.0f st: corrected %.2f dB vs varispeed %.2f dB", st, errF, errC);

        const auto mono = mixDown (f.audio);
        const auto p = detectPitch (mono, sr, real.hz, 2000, (int) mono.size() - 2000);
        CHECK (p.ok && std::abs (p.cents) < 2.0, "pitch after formant render: %.2f cents ok=%d", p.cents, (int) p.ok);
        CHECK (std::abs (gainToDb (peakOf (f.audio)) - gainToDb (peakOf (s.audio))) < 9.0, "level %.1f vs %.1f",
               gainToDb (peakOf (f.audio)), gainToDb (peakOf (s.audio)));
    }

    // fixed time keeps the length and the formants
    test::Tone real = t;
    real.hz = noteHz (59);
    const auto truth = harmonicsDb (test::make (real), sr, real.hz, 6000.0);
    auto upT = transposed (s, 4.0, RenderSettings { Algo::fixedTime });
    CHECK (upT.length() == s.length(), "fixed time length %d vs %d", upT.length(), s.length());
    const auto monoT = mixDown (upT.audio);
    const auto pt = detectPitch (monoT, sr, real.hz, 4000, (int) monoT.size() - 8000);
    CHECK (pt.ok && std::abs (pt.cents) < 5.0, "fixed time pitch %.2f cents", pt.cents);
    const double errT = rmsDiff (harmonicsDb (upT.audio, sr, real.hz, 6000.0), truth);
    std::printf ("  fixed time +4 st: harmonics off by %.2f dB\n", errT);
    CHECK (errT < 2.5, "fixed time harmonics %.2f dB", errT);
}

static void testPitch()
{
    const double sr = 48000;
    test::Tone t;
    t.hz = noteHz (60 + 0.17);
    t.seconds = 1.0;
    const auto ch = test::make (t);
    const auto m = mixDown (ch);
    auto p = detectPitch (m, sr, noteHz (60), 2000, (int) m.size());
    CHECK (p.ok && std::abs (p.cents - 17.0) < 1.0, "C3 +17: %.2f cents conf %.2f", p.cents, p.confidence);
    t.hz = noteHz (24 - 0.31);   // a low C0, flat
    t.seconds = 1.5;
    const auto m2 = mixDown (test::make (t));
    p = detectPitch (m2, sr, noteHz (24), 2000, (int) m2.size());
    CHECK (p.ok && std::abs (p.cents + 31.0) < 1.5, "C0 -31: %.2f cents", p.cents);
    // noise: no pitch
    std::vector<float> noise (48000);
    std::mt19937 rng (3);
    std::normal_distribution<float> nd (0, 0.2f);
    for (auto& v : noise) v = nd (rng);
    p = detectPitch (noise, sr, 440, 0, 48000);
    CHECK (! p.ok, "noise has no pitch (conf %.2f)", p.confidence);
}

static void testLoops()
{
    const double sr = 48000;
    // steady tone: a seamless loop, long
    {
        test::Tone t;
        t.hz = noteHz (57);
        t.seconds = 3.0;
        t.noiseDb = -90;
        auto ch = test::make (t);
        const int held = (int) ch[0].size();
        const double t0 = now();
        auto rep = findLoop (ch, sr, 0, held, noteHz (57), {});
        const double t1 = now();
        CHECK (rep.loop.valid(), "no loop");
        CHECK (rep.loop.quality > 0.98f, "quality %.3f", rep.loop.quality);
        CHECK (rep.loop.length() > sr * 0.8, "length %.2f s", rep.loop.length() / sr);
        std::printf ("  loop steady: %d..%d (%.2f s) quality %.4f in %.2f s\n", rep.loop.start, rep.loop.end,
                     rep.loop.length() / sr, rep.loop.quality, t1 - t0);
    }
    // a slow wobble at 1.25 Hz: the loop is a whole number of wobbles
    {
        test::Tone t;
        t.hz = noteHz (48);
        t.seconds = 4.0;
        t.lfoHz = 1.25;
        t.lfoDepth = 0.5;
        auto ch = test::make (t);
        auto rep = findLoop (ch, sr, 0, (int) ch[0].size(), noteHz (48), {});
        CHECK (rep.loop.valid(), "no loop");
        const double wob = sr / 1.25;
        const double k = std::round (rep.loop.length() / wob);
        CHECK (k >= 1 && std::abs (rep.loop.length() - k * wob) < 0.03 * wob, "loop %d vs wobble %.0f", rep.loop.length(), wob);
        CHECK (std::abs (rep.modulationSeconds - 0.8) < 0.01, "modulation %.3f s", rep.modulationSeconds);
        CHECK (rep.loop.quality > 0.9f, "quality %.3f", rep.loop.quality);
    }
    // crossfade: seam continuity after baking
    {
        test::Tone t;
        t.hz = 333.3;
        t.seconds = 2.0;
        auto ch = test::make (t);
        Loop l;
        l.start = 30000;
        l.end = 70011;   // not a period multiple: a real jump without the blend
        bakeCrossfade (ch, l, 2400);
        CHECK (l.crossfade == 2400, "crossfade %d", l.crossfade);
        const auto& c = ch[0];
        const double jump = std::abs (c[(size_t) l.start] - c[(size_t) l.end - 1]);
        double maxStep = 0;
        for (int i = l.start + 1; i < l.end; ++i)
            maxStep = std::max (maxStep, (double) std::abs (c[(size_t) i] - c[(size_t) i - 1]));
        CHECK (jump < maxStep * 1.2, "seam jump %.4f vs max step %.4f", jump, maxStep);
    }
}

// the loop of a transposed sample has a whole-sample length and plays seamlessly
static void testTransposedLoop()
{
    const double sr = 48000;
    test::Tone t;
    t.hz = noteHz (60);
    t.seconds = 2.5;
    Sample s;
    s.root = 60;
    s.sampleRate = sr;
    s.audio = test::make (t);
    auto rep = findLoop (s.audio, sr, 0, s.length(), noteHz (60), {});
    bakeCrossfade (s.audio, rep.loop, 2000);
    s.loop = rep.loop;
    for (auto algo : { Algo::classic, Algo::formant })
        for (double st : { 2.37, -3.6, 11.0 })
        {
            auto o = transposed (s, st, RenderSettings { algo });
            CHECK (o.loop.valid(), "loop lost");
            const double expect = s.loop.length() / ratioOf (st);
            CHECK (std::abs (o.loop.length() - expect) <= 0.51, "loop %d vs %.2f", o.loop.length(), expect);
            // pitch error of the speed nudge
            const double cents = 1200.0 * std::log2 (s.loop.length() / (double) o.loop.length() / ratioOf (st));
            CHECK (std::abs (cents) < 0.1, "nudge %.4f cents", cents);
            // seam: playing ...end-2, end-1, start, start+1... is as smooth as inside the loop
            const auto& c = o.audio[0];
            const double jump = std::abs (c[(size_t) o.loop.start] - c[(size_t) o.loop.end - 1]);
            double maxStep = 0;
            for (int i = o.loop.start + 1; i < o.loop.end; ++i)
                maxStep = std::max (maxStep, (double) std::abs (c[(size_t) i] - c[(size_t) i - 1]));
            CHECK (jump < maxStep * 1.05, "%s %+.2f: seam %.4f vs step %.4f", algo == Algo::classic ? "classic" : "formant", st, jump, maxStep);
            CHECK (o.length() == o.loop.end + 512, "tail %d", o.length() - o.loop.end);
        }
}

static void testPlan()
{
    Session s;
    s.lowNote = 36;
    s.highNote = 48;
    s.interval = 3;
    auto n = plannedNotes (s);
    CHECK ((n == std::vector<int> { 36, 39, 42, 45, 48 }), "interval notes");
    s.noteMode = NoteMode::count;
    s.noteCount = 4;
    n = plannedNotes (s);
    CHECK ((n == std::vector<int> { 36, 40, 44, 48 }), "count notes %d %d %d %d", n[0], n[1], n[2], n[3]);
    s.layers = 4;
    auto v = plannedVelocities (s);
    CHECK ((v == std::vector<int> { 32, 64, 95, 127 }), "linear %d %d %d %d", v[0], v[1], v[2], v[3]);
    s.curve = VelCurve::soft;
    v = plannedVelocities (s);
    CHECK (v.size() == 4 && v[0] < 32 && v[3] == 127, "soft %d", v[0]);
    s.layers = 16;
    s.curve = VelCurve::hard;
    v = plannedVelocities (s);
    bool rising = true;
    for (size_t i = 1; i < v.size(); ++i) rising = rising && v[i] > v[i - 1];
    CHECK (rising && v.back() == 127 && v.front() >= 1, "hard 16 rising");
    s.layers = 2;
    s.curve = VelCurve::linear;
    s.roundRobins = 3;
    s.noteMode = NoteMode::interval;
    CHECK (planJobs (s).size() == 5 * 2 * 3, "jobs %zu", planJobs (s).size());

    auto z = keyZones ({ 60, 63, 66 }, Placement::centred, 48, 80);
    CHECK (z[0].lo == 48 && z[0].hi == 61 && z[1].lo == 62 && z[1].hi == 64 && z[2].lo == 65 && z[2].hi == 80, "centred");
    z = keyZones ({ 60, 64 }, Placement::up, 60, 70);
    CHECK (z[0].lo == 60 && z[0].hi == 63 && z[1].lo == 64 && z[1].hi == 70, "up");
    z = keyZones ({ 60, 64 }, Placement::down, 50, 64);
    CHECK (z[0].lo == 50 && z[0].hi == 60 && z[1].lo == 61 && z[1].hi == 64, "down");
    auto vz = velocityZones ({ 40, 90, 127 });
    CHECK (vz[0].lo == 1 && vz[0].hi == 40 && vz[1].lo == 41 && vz[1].hi == 90 && vz[2].lo == 91 && vz[2].hi == 127, "vel zones");
}

static void testWav()
{
    Channels ch (2, std::vector<float> (1000));
    for (int i = 0; i < 1000; ++i)
    {
        ch[0][(size_t) i] = (float) std::sin (i * 0.05) * 0.7f;
        ch[1][(size_t) i] = (float) std::cos (i * 0.03) * -0.4f;
    }
    WavInfo info;
    info.root = 60;
    info.detuneCents = -20.0f;
    info.loop.start = 100;
    info.loop.end = 900;
    info.hasRange = true;
    info.keyLo = 58;
    info.keyHi = 62;
    info.velLo = 65;
    info.velHi = 127;
    for (int bits : { 16, 24, 32 })
    {
        const auto bytes = encodeWav (ch, 44100, bits, info);
        Channels back;
        double sr = 0;
        WavInfo got;
        CHECK (decodeWav (bytes.data(), bytes.size(), back, sr, got), "decode %d", bits);
        CHECK (sr == 44100 && back.size() == 2 && back[0].size() == 1000, "format %d", bits);
        double err = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 1000; ++i)
                err = std::max (err, (double) std::abs (back[(size_t) c][(size_t) i] - ch[(size_t) c][(size_t) i]));
        CHECK (err < (bits == 16 ? 1e-4 : 1e-6), "error %g at %d bits", err, bits);
        CHECK (got.root == 59 && std::abs (got.detuneCents - 80.0f) < 0.01f, "root %d + %.2f", got.root, got.detuneCents);
        CHECK (got.loop.start == 100 && got.loop.end == 900, "loop %d %d", got.loop.start, got.loop.end);
        CHECK (got.hasRange && got.keyLo == 58 && got.keyHi == 62 && got.velLo == 65 && got.velHi == 127, "range");
    }
}

static void testCapture()
{
    const double sr = 48000;
    Session s;
    s.hold = 2.0;
    test::Tone t;
    t.hz = noteHz (52 + 0.08);
    t.seconds = 3.2;
    t.releaseAt = 2.0;
    t.release = 0.4;
    t.noiseDb = -88;
    t.delay = 4800 + 317;      // 100 ms of "pre-roll" + 317 samples of latency
    auto raw = test::make (t);
    // the noise floor as measured before the session
    test::Tone quiet = t;
    quiet.level = 0.0;
    quiet.delay = 0;
    quiet.seconds = 0.5;
    const auto nf = measureNoise (test::make (quiet));
    CHECK (std::abs (nf.rmsDb() + 88.0f) < 1.5f, "floor %.1f dB", nf.rmsDb());

    Job job { 52, 100, 0, 0 };
    const int noteOn = 4800, noteOff = 4800 + 317 + (int) (2.0 * sr);
    auto r = processCapture (raw, sr, noteOn, noteOff, job, s, nf);
    CHECK (! r.silent, "silent");
    CHECK (std::abs (r.latency - 317) < 30, "latency %d", r.latency);
    CHECK (r.sample.pitchConfident && std::abs (r.sample.detuneCents - 8.0f) < 1.0f, "detune %.2f", r.sample.detuneCents);
    CHECK (r.sample.loop.valid() && r.sample.loop.end <= r.sample.noteOff, "loop %d..%d note-off %d",
           r.sample.loop.start, r.sample.loop.end, r.sample.noteOff);
    const double tail = (r.sample.length() - r.sample.noteOff) / sr;
    CHECK (tail > 0.25 && tail < 1.2, "tail %.2f s", tail);
    std::printf ("  capture: latency %d, detune %.2f c, loop %.2f s (q %.3f), tail %.2f s\n", r.latency,
                 r.sample.detuneCents, r.sample.loop.length() / sr, r.sample.loop.quality, tail);

    // nothing played
    test::Tone none = quiet;
    none.seconds = 3.0;
    auto r2 = processCapture (test::make (none), sr, 4800, 100000, job, s, nf);
    CHECK (r2.silent, "silence not seen");

    // normalisation keeps the layers apart
    std::vector<Sample> v (2);
    v[0].audio = Channels (1, std::vector<float> (10, 0.25f));
    v[1].audio = Channels (1, std::vector<float> (10, 0.5f));
    normalise (v, Normalise::instrument, 0.0);
    CHECK (std::abs (v[1].audio[0][0] - 1.0f) < 1e-5f && std::abs (v[0].audio[0][0] - 0.5f) < 1e-5f, "instrument normalisation");
}

int main()
{
    testNames();
    testSinc();
    testPitch();
    testPlan();
    testWav();
    testLoops();
    testTransposedLoop();
    testFormant();
    testCapture();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
