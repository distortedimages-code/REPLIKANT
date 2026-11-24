// RADIO (0.3.5) on its own, no network: the decoder (an MP3 at its pitch and length, the shortwave ADPCM), the
// receivers' feed (it waits to fill, keeps its clock on the audio's, survives a dry network), the listening (TUNE's
// shift, the squelch, LOOP's grab, DIG digging back, the air bounded), what was heard (GRAB, REC), the slicer (attacks
// found to the millisecond, the strongest kept), the pads, and a storm of random settings.

#include "../Source/radio/RadioCore.h"
#include "../Source/radio/RadioDec.h"
#include "../Source/radio/RadioSlicer.h"
#include "../Source/radio/RadioSeek.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <tuple>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

#ifndef RK_TEST_DIR
 #define RK_TEST_DIR "."
#endif

static const double twoPi = 6.283185307179586;

// the loudest frequency in [lo, hi] (a scan of Goertzel-like correlations, then refined)
static double peakHz (const float* x, int n, double sr, double lo, double hi, double step = 5.0)
{
    auto power = [&] (double f)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (twoPi * i / (n - 1));
            re += x[i] * w * std::cos (twoPi * f * i / sr);
            im += x[i] * w * std::sin (twoPi * f * i / sr);
        }
        return re * re + im * im;
    };
    double best = 0, bf = lo;
    for (double f = lo; f <= hi; f += step)
    {
        const double p = power (f);
        if (p > best) { best = p; bf = f; }
    }
    for (double f = bf - step; f <= bf + step; f += step / 20.0)
    {
        const double p = power (f);
        if (p > best) { best = p; bf = f; }
    }
    return bf;
}

static double rms (const float* x, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max (1, n));
}

static std::vector<unsigned char> readFile (const char* name)
{
    std::ifstream f (std::string (RK_TEST_DIR) + "/" + name, std::ios::binary);
    return std::vector<unsigned char> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

//==============================================================================
static void decodeAll (const std::vector<unsigned char>& bytes, std::vector<float>& L, std::vector<float>& R, radio::Dec& d)
{
    int16_t buf[4096 * 2];
    size_t at = 0;
    for (;;)
    {
        const int m = (int) std::min ((size_t) d.inRoom(), bytes.size() - at);
        if (m > 0) { d.mp3Feed (bytes.data() + at, m); at += (size_t) m; }
        bool more = true;
        while (more)
        {
            const int k = d.take (buf, 4096);
            for (int i = 0; i < k; ++i) { L.push_back (buf[2 * i] / 32768.0f); R.push_back (buf[2 * i + 1] / 32768.0f); }
            more = d.mp3Work() || k > 0;
        }
        if (at >= bytes.size())
        {
            // (the last frames: padding so the decoder sees their end)
            std::vector<unsigned char> pad (20000, 0);
            d.mp3Feed (pad.data(), (int) pad.size());
            int k;
            while ((k = d.take (buf, 4096)) > 0 || d.mp3Work())
                for (int i = 0; i < k; ++i) { L.push_back (buf[2 * i] / 32768.0f); R.push_back (buf[2 * i + 1] / 32768.0f); }
            break;
        }
    }
}

static void testDecoder()
{
    auto d = std::make_unique<radio::Dec>();
    d->reset();
    for (const char* name : { "radio_1k.mp3", "radio_440_22k.mp3" })
    {
        const auto bytes = readFile (name);
        CHECK (bytes.size() > 1000, "%s missing", name);
        if (bytes.size() < 1000)
            continue;
        d->restart();
        std::vector<float> L, R;
        decodeAll (bytes, L, R, *d);
        const bool one = name[6] == '1';
        const double secs = (double) L.size() / radio::rate;
        CHECK (d->frames > 10, "%s: %u frames", name, d->frames);
        CHECK (L.size() > 48000, "%s: %zu frames out", name, L.size());
        if (L.size() < 48000)
            continue;
        const int n = 16384;
        const double f = peakHz (L.data() + L.size() / 2 - n / 2, n, radio::rate, one ? 800 : 300, one ? 1200 : 600);
        CHECK (std::abs (f - (one ? 1000.0 : 440.0)) < 1.5, "%s: %.2f Hz (rate %d, %d ch, %.2f s)", name, f, d->rate, d->chans, secs);
        // both sides carry it (a mono stream too)
        CHECK (rms (R.data() + R.size() / 2, n) > 0.05, "%s: right side %.3f", name, rms (R.data() + R.size() / 2, n));
        std::printf ("  decoded %s: %d Hz %d ch -> %.2f s at 48 kHz, peak %.2f Hz\n", name, d->rate, d->chans, secs, f);
    }
    // a shortwave receiver's IMA ADPCM, at its odd rate: a 500 Hz tone comes out at 500 Hz
    {
        d->restart();
        const double fr = 12001.135;
        // encode a tone the standard way
        std::vector<unsigned char> code;
        int idx = 0, prev = 0;
        static const short steps[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
            73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
            4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
            24623, 27086, 29794, 32767 };
        static const int idxs[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
        unsigned char byte = 0;
        for (int i = 0; i < (int) (fr * 2.0); ++i)
        {
            const int s = (int) (12000.0 * std::sin (twoPi * 500.0 * i / fr));
            int diff = s - prev, nib = 0, st = steps[idx];
            if (diff < 0) { nib = 8; diff = -diff; }
            int delta = st >> 3;
            if (diff >= st) { nib |= 4; diff -= st; delta += st; }
            if (diff >= st >> 1) { nib |= 2; diff -= st >> 1; delta += st >> 1; }
            if (diff >= st >> 2) { nib |= 1; delta += st >> 2; }
            prev += (nib & 8) ? -delta : delta;
            prev = std::max (-32768, std::min (32767, prev));
            idx = std::max (0, std::min (88, idx + idxs[nib]));
            if (i & 1) { byte |= (unsigned char) (nib << 4); code.push_back (byte); }
            else byte = (unsigned char) nib;
        }
        std::vector<float> L;
        int16_t buf[4096 * 2];
        for (size_t at = 0; at < code.size(); at += 512)
        {
            d->adpcmFeed (code.data() + at, (int) std::min ((size_t) 512, code.size() - at), (float) fr);
            int k;
            while ((k = d->take (buf, 4096)) > 0)
                for (int i = 0; i < k; ++i) L.push_back (buf[2 * i] / 32768.0f);
        }
        const double secs = (double) L.size() / radio::rate;
        CHECK (std::abs (secs - 2.0) < 0.01, "adpcm: %.3f s", secs);
        const double f = L.size() > 30000 ? peakHz (L.data() + 20000, 8192, radio::rate, 400, 600) : 0;
        CHECK (std::abs (f - 500.0) < 1.0, "adpcm: %.2f Hz", f);
    }
}

//==============================================================================
// a receiver fed in "real time": `fps` frames a second of a tone at hz, the audio read at sr in blocks
struct Feeder
{
    radio::Bank& b;
    int k;
    double hz, ph = 0.0, acc = 0.0;
    std::vector<int16_t> buf;
    Feeder (radio::Bank& bb, int kk, double h) : b (bb), k (kk), hz (h) {}
    void feed (double seconds, double speed = 1.0)
    {
        acc += seconds * radio::rate * speed;
        const int n = (int) acc;
        acc -= n;
        buf.resize ((size_t) n * 2);
        for (int i = 0; i < n; ++i)
        {
            const auto v = (int16_t) (16000.0 * std::sin (ph));
            ph += twoPi * hz / radio::rate;
            buf[(size_t) (2 * i)] = v;
            buf[(size_t) (2 * i + 1)] = v;
        }
        b.push (k, buf.data(), n);
    }
};

static void testFeed()
{
    auto e = std::make_unique<radio::Engine>();
    const double sr = 44100.0;
    e->prepare (sr, 512);
    e->allocate();
    radio::Bank& b = e->bank;
    b.setState (0, radio::buffering, radio::web);
    Feeder f (b, 0, 1000.0);
    radio::Settings s;
    s.stat = 0.0f;
    s.level = 0.8f;
    std::vector<float> L (512), R (512);
    // half a second in hand: it waits (silence), at a second: it plays
    for (int blk = 0; blk < (int) (0.5 * sr / 512); ++blk)
    {
        f.feed (512.0 / sr);
        e->render (L.data(), R.data(), 512, s, nullptr, 0);
    }
    CHECK (b.rx[0].waiting.load() == 1, "still waiting at 0.5 s");
    CHECK (b.rx[0].level.load() < 1.0e-4, "nothing played while waiting: %g", (double) b.rx[0].level.load());
    b.setState (0, radio::live, radio::web);
    for (int blk = 0; blk < (int) (1.0 * sr / 512); ++blk)
    {
        f.feed (512.0 / sr);
        e->render (L.data(), R.data(), 512, s, nullptr, 0);
    }
    CHECK (b.rx[0].waiting.load() == 0, "plays once a second is in hand");
    // a minute of a feed whose clock runs 0.3 % fast, then 0.3 % slow: the fill stays near a second, no dropout
    std::vector<float> out;
    float lo = 10.0f, hi = 0.0f;
    for (double speed : { 1.003, 0.997 })
        for (int blk = 0; blk < (int) (60.0 * sr / 512); ++blk)
        {
            f.feed (512.0 / sr, speed);
            e->render (L.data(), R.data(), 512, s, nullptr, 0);
            if (blk > (int) (20.0 * sr / 512))
            {
                lo = std::min (lo, b.rx[0].fill.load());
                hi = std::max (hi, b.rx[0].fill.load());
            }
            if (blk % 50 == 0 && out.size() < 44100 * 4)
                out.insert (out.end(), L.begin(), L.end());
        }
    CHECK (b.rx[0].dropouts.load() == 0, "dropouts: %d", b.rx[0].dropouts.load());
    CHECK (lo > 0.85f && hi < 1.15f, "fill %.3f .. %.3f s", lo, hi);
    // its pitch, read a little faster or slower: within a few cents
    {
        std::vector<float> one;
        for (int blk = 0; blk < 32; ++blk)
        {
            f.feed (512.0 / sr, 1.003);
            e->render (L.data(), R.data(), 512, s, nullptr, 0);
            one.insert (one.end(), L.begin(), L.end());
        }
        const double hz = peakHz (one.data(), (int) one.size(), sr, 950, 1050, 1.0);
        CHECK (std::abs (1200.0 * std::log2 (hz / 1003.0)) < 12.0, "a fast feed heard at %.2f Hz (sent 1003)", hz);
    }
    // the network dry for three seconds: silence (faded), then it waits, fills and plays again
    for (int blk = 0; blk < (int) (3.0 * sr / 512); ++blk)
        e->render (L.data(), R.data(), 512, s, nullptr, 0);
    CHECK (rms (L.data(), 512) < 1.0e-3, "dry network: %g", rms (L.data(), 512));
    CHECK (b.rx[0].dropouts.load() >= 1, "the dropout counted");
    for (int blk = 0; blk < (int) (2.0 * sr / 512); ++blk)
    {
        f.feed (512.0 / sr);
        e->render (L.data(), R.data(), 512, s, nullptr, 0);
    }
    CHECK (rms (L.data(), 512) > 0.2, "back: %g", rms (L.data(), 512));
    // a flush (a new station): what was in hand is dropped, it waits again
    b.flush (0);
    e->render (L.data(), R.data(), 512, s, nullptr, 0);
    CHECK (b.rx[0].waiting.load() == 1, "a flush: waiting again");
}

//==============================================================================
// an engine whose receiver 0 already holds a few seconds of a tone at hz, playing
static std::unique_ptr<radio::Engine> playing (double sr, double hz, Feeder*& feeder)
{
    auto e = std::make_unique<radio::Engine>();
    e->prepare (sr, 512);
    e->allocate();
    e->bank.setState (0, radio::live, radio::web);
    feeder = new Feeder (e->bank, 0, hz);
    radio::Settings s;
    s.stat = 0.0f;
    std::vector<float> L (512), R (512);
    for (int blk = 0; blk < (int) (4.0 * sr / 512); ++blk)
    {
        feeder->feed (512.0 / sr);
        e->render (L.data(), R.data(), 512, s, nullptr, 0);
    }
    return e;
}

static std::vector<float> run (radio::Engine& e, Feeder& f, const radio::Settings& s, double seconds, std::vector<float>* right = nullptr)
{
    const double sr = e.sampleRate();
    std::vector<float> out, L (512), R (512);
    radio::Settings t = s;
    for (int blk = 0; blk < (int) (seconds * sr / 512); ++blk)
    {
        f.feed (512.0 / sr);
        e.render (L.data(), R.data(), 512, t, nullptr, 0);
        out.insert (out.end(), L.begin(), L.end());
        if (right != nullptr)
            right->insert (right->end(), R.begin(), R.end());
        t.ppq += 512.0 / sr * t.bpm / 60.0;
    }
    return out;
}

static void testListen()
{
    const double sr = 48000.0;
    Feeder* f = nullptr;
    auto e = playing (sr, 1000.0, f);
    radio::Settings s;
    s.stat = 0.0f;
    // LIVE: as it is; TUNE +8: 200 Hz up; -8: 200 Hz down (a receiver tuned beside it)
    for (float tune : { 0.0f, 8.0f, -8.0f })
    {
        s.tune = tune;
        auto out = run (*e, *f, s, 0.6);
        const double hz = peakHz (out.data() + out.size() - 16384, 16384, sr, 700, 1300, 2.0);
        CHECK (std::abs (hz - (1000.0 + 25.0 * tune)) < 2.0, "TUNE %+.0f: %.1f Hz", tune, hz);
    }
    s.tune = 0.0f;
    // BAND at its narrowest, centred far from the tone: it is gone (four poles)
    {
        s.band = 1.0f;
        s.centre = 300.0f;
        auto out = run (*e, *f, s, 0.5);
        const double r = rms (out.data() + out.size() - 8192, 8192);
        CHECK (r < 0.02, "a narrow band at 300 Hz takes 1 kHz away: %.4f", r);
        s.band = 0.0f;
    }
    // the squelch: a tone chopped between loud and 45 dB under (300 ms each), SQUELCH at -35 dB shuts the quiet parts
    {
        delete f;
        auto shutWindows = [&] (float squelch)
        {
            auto e2 = std::make_unique<radio::Engine>();
            e2->prepare (sr, 512);
            e2->allocate();
            e2->bank.setState (0, radio::live, radio::web);
            std::vector<int16_t> chunk;
            double ph = 0;
            int t = 0;
            radio::Settings q;
            q.stat = 0.0f;
            q.squelch = squelch;
            std::vector<float> L (512), R (512), out;
            for (int blk = 0; blk < (int) (7.0 * sr / 512); ++blk)
            {
                chunk.resize (512 * 2);
                for (int i = 0; i < 512; ++i, ++t)
                {
                    const bool loud = (t / 14400) % 2 == 0;
                    const auto v = (int16_t) ((loud ? 16000.0 : 90.0) * std::sin (ph));
                    ph += twoPi * 700.0 / radio::rate;
                    chunk[(size_t) (2 * i)] = v;
                    chunk[(size_t) (2 * i + 1)] = v;
                }
                e2->bank.push (0, chunk.data(), 512);
                e2->render (L.data(), R.data(), 512, q, nullptr, 0);
                if (blk > (int) (3.0 * sr / 512))
                    out.insert (out.end(), L.begin(), L.end());
            }
            int shut = 0, all = 0, open = 0;
            for (size_t w = 0; w + 2400 <= out.size(); w += 2400, ++all)
            {
                const double r = rms (out.data() + w, 2400);
                shut += r < 0.0005 ? 1 : 0;
                open += r > 0.1 ? 1 : 0;
            }
            return std::make_tuple (shut, open, all);
        };
        auto [shut, open, all] = shutWindows ((66.0f - 35.0f) / 56.0f);
        CHECK (shut >= all / 5 && open >= all / 3, "squelch: %d shut, %d open of %d windows", shut, open, all);
        auto [shut0, open0, all0] = shutWindows (0.0f);
        CHECK (shut0 == 0, "no squelch: %d shut of %d", shut0, all0);
        (void) open0;
        f = nullptr;
    }
    // LOOP: a bar taken and played again; the receiver moves on (now 1500 Hz): GRAB at 0 keeps the first, at 1 takes it
    {
        auto e3 = playing (sr, 1000.0, f);
        radio::Settings q;
        q.stat = 0.0f;
        q.mode = radio::modeLoop;
        q.len = 7;                  // a bar
        q.grab = 0.0f;
        q.playing = true;
        q.bpm = 120.0;
        q.ppq = 0.0;
        run (*e3, *f, q, 0.3);
        f->hz = 1500.0;
        auto kept = run (*e3, *f, q, 4.5);
        double hz = peakHz (kept.data() + kept.size() - 16384, 16384, sr, 800, 1700, 5.0);
        CHECK (std::abs (hz - 1000.0) < 5.0, "GRAB 0 keeps the first loop: %.1f Hz", hz);
        CHECK (e3->loopPos.load() >= 0.0f, "the loop's place shown");
        q.grab = 1.0f;
        auto fresh = run (*e3, *f, q, 4.5);
        hz = peakHz (fresh.data() + fresh.size() - 16384, 16384, sr, 800, 1700, 5.0);
        CHECK (std::abs (hz - 1500.0) < 5.0, "GRAB 100 %% takes a new one: %.1f Hz", hz);
        // TUNE +12: an octave up (the loop as a tape)
        q.tune = 12.0f;
        auto up = run (*e3, *f, q, 1.0);
        hz = peakHz (up.data() + up.size() - 8192, 8192, sr, 2500, 3500, 5.0);
        CHECK (std::abs (hz - 3000.0) < 10.0, "LOOP TUNE +12: %.1f Hz", hz);
        delete f;
        f = nullptr;
    }
    // DIG: each turn from somewhere further back (the history holds a tone that steps up every second)
    {
        auto e4 = std::make_unique<radio::Engine>();
        e4->prepare (sr, 512);
        e4->allocate();
        e4->bank.setState (0, radio::live, radio::web);
        Feeder g (e4->bank, 0, 400.0);
        radio::Settings q;
        q.stat = 0.0f;
        std::vector<float> L (512), R (512);
        for (int sec = 0; sec < 12; ++sec)
        {
            g.hz = 400.0 + 100.0 * sec;
            for (int blk = 0; blk < (int) (sr / 512); ++blk)
            {
                g.feed (512.0 / sr);
                e4->render (L.data(), R.data(), 512, q, nullptr, 0);
            }
        }
        g.hz = 1700.0;
        q.mode = radio::modeDig;
        q.len = 5;                   // half a bar
        q.depth = 10.0f;
        q.playing = true;
        int older = 0, turns = 0;
        for (int t = 0; t < 12; ++t)
        {
            auto out = run (*e4, g, q, 1.0);
            q.ppq += 2.0;
            const double hz = peakHz (out.data() + out.size() / 2, 8192, sr, 300, 1800, 10.0);
            ++turns;
            if (hz < 1550.0) ++older;
        }
        CHECK (older >= turns * 2 / 3, "DIG found older sound %d times of %d", older, turns);
    }
    // the air at its top, every receiver silent or playing: bounded
    {
        auto e5 = playing (sr, 800.0, f);
        radio::Settings q;
        q.stat = 1.0f; q.fade = 1.0f; q.fadeRate = 4.0f; q.whistle = 1.0f; q.crackle = 1.0f; q.drive = 1.0f; q.band = 0.6f; q.level = 1.0f;
        auto out = run (*e5, *f, q, 6.0);
        float top = 0;
        bool finite = true;
        for (auto v : out) { top = std::max (top, std::abs (v)); finite = finite && std::isfinite (v); }
        CHECK (finite && top < 2.5f, "the air at its top: peak %.2f", top);
        // (0.4.12) the station lost: MORPH holds it a while (half: about a second), then nothing, no static whatever
        // STAT says (it's gone)
        e5->bank.setState (0, radio::tuning, radio::web);
        radio::Settings z;
        z.stat = 0.6f;
        z.morph = 0.5f;
        auto lost = run (*e5, *f, z, 3.0);
        const double heldEarly = rms (lost.data() + 2400, 4800), heldLate = rms (lost.data() + lost.size() - 4800, 4800);
        CHECK (heldEarly > 0.01, "the station left held: %g", heldEarly);
        CHECK (heldLate < 1.0e-4, "then nothing, no static: %g", heldLate);
        // back, then lost again, then back: no click as it goes (the held one in, the new one over it)
        auto stepsOf = [] (const std::vector<float>& v, float& top)
        {
            float w = 0.0f;
            for (size_t i = 1; i < v.size(); ++i) { w = std::max (w, std::abs (v[i] - v[i - 1])); top = std::max (top, std::abs (v[i])); }
            return w;
        };
        e5->bank.setState (0, radio::live, radio::web);
        auto back = run (*e5, *f, z, 2.0);
        e5->bank.setState (0, radio::tuning, radio::web);
        auto gone = run (*e5, *f, z, 0.4);
        e5->bank.setState (0, radio::live, radio::web);
        auto again = run (*e5, *f, z, 1.5);
        float pk = 0.0f;
        const float w1 = stepsOf (back, pk), w2 = stepsOf (gone, pk), w3 = stepsOf (again, pk);
        const double backLate = rms (back.data() + back.size() - 4800, 4800), mid = rms (gone.data() + 9600, 4800);
        std::printf ("  morph: held %.3f, steps %.3f %.3f %.3f of a peak %.3f, back %.3f\n", mid, w1, w2, w3, pk, backLate);
        CHECK (backLate > 0.05 && mid > 0.3 * backLate, "back, and held while lost: %g %g", backLate, mid);
        CHECK (std::max (w1, std::max (w2, w3)) < 0.25f * pk, "no click: %g %g %g of %g", w1, w2, w3, pk);
        // MORPH 0: gone at once (a fade)
        z.morph = 0.0f;
        e5->bank.setState (0, radio::tuning, radio::web);
        auto cut = run (*e5, *f, z, 0.5);
        CHECK (rms (cut.data() + cut.size() - 4800, 4800) < 1.0e-3, "MORPH 0: gone: %g", rms (cut.data() + cut.size() - 4800, 4800));
        delete f;
        f = nullptr;
    }
}

//==============================================================================
static void testHeard()
{
    const double sr = 48000.0;
    Feeder* f = nullptr;
    auto e = playing (sr, 600.0, f);
    radio::Settings s;
    s.stat = 0.0f;
    std::vector<float> R;
    auto out = run (*e, *f, s, 3.0, &R);
    radio::Take::Audio a;
    CHECK (e->grab (2.0, a), "grab");
    CHECK (std::abs ((double) a.l.size() - 2.0 * sr) < 2.0, "grab length %zu", a.l.size());
    // the very same frames as were heard (kept in 16 bits, ±2 full scale: within one step)
    double diff = 0;
    for (size_t i = 0; i < a.l.size(); ++i)
        diff = std::max (diff, (double) std::abs (a.l[i] - out[out.size() - a.l.size() + i]));
    CHECK (diff <= 0.5 / 16384.0 + 1.0e-7, "grab is what was heard: %g", diff);
    // REC .. STOP
    e->recStart();
    run (*e, *f, s, 1.5);
    CHECK (std::abs (e->recSeconds() - 1.5) < 0.05, "rec %.3f s", e->recSeconds());
    radio::Take::Audio b;
    CHECK (e->recStop (b), "rec stop");
    CHECK (std::abs ((double) b.l.size() / sr - 1.5) < 0.05, "rec length %.3f", (double) b.l.size() / sr);
    CHECK (! e->recording(), "stopped");
    // longer than it keeps: the last 30 s
    radio::Take::Audio c;
    e->recStart();
    run (*e, *f, s, 33.0);
    CHECK (e->recStop (c) && std::abs ((double) c.l.size() / sr - radio::longestTake) < 0.05, "a long take: %.2f s", (double) c.l.size() / sr);

    // (0.4.16) two minutes kept: 125 s heard (a tone, then quiet for the last 5 s), a passage of the full two minutes
    // taken from the screen, the tone at its start
    run (*e, *f, s, 120.0);
    e->bank.setState (0, radio::failed, radio::web);
    run (*e, *f, s, 5.0);
    const int64_t now = e->heardNow();
    CHECK (e->heardOldest() <= now - (int64_t) (radio::memorySeconds * sr), "two minutes kept: %.1f s", (double) (now - e->heardOldest()) / sr);
    radio::Take::Audio d;
    CHECK (e->takeRange (now - (int64_t) (radio::memorySeconds * sr), now, d) && std::abs ((double) d.l.size() / sr - radio::memorySeconds) < 0.05,
           "a passage of two minutes: %.2f s", (double) d.l.size() / sr);
    CHECK (rms (d.l.data(), 4800) > 0.2 && rms (d.l.data() + d.l.size() - 4800, 4800) < 1.0e-3, "its start heard, its end quiet: %g %g",
           rms (d.l.data(), 4800), rms (d.l.data() + d.l.size() - 4800, 4800));
    // replayed from 10 s back: the tone heard again (instead of the quiet) up to where it was asked, then live again
    e->replayFrom (now - (int64_t) (10.0 * sr));
    auto replayed = run (*e, *f, s, 2.0);
    CHECK (rms (replayed.data() + 4800, 9600) > 0.2 && e->replaying() >= 0, "replayed: %g", rms (replayed.data() + 4800, 9600));
    float jump = 0.0f;
    for (size_t i = 1; i < 2400; ++i)
        jump = std::max (jump, std::abs (replayed[i] - replayed[i - 1]));
    CHECK (jump < 0.2f, "in without a click: %g", (double) jump);
    auto caught = run (*e, *f, s, 10.0);
    CHECK (e->replaying() < 0 && rms (caught.data() + caught.size() - 4800, 4800) < 1.0e-3, "caught up, live again");
    // stopped: out at once (10 ms)
    e->replayFrom (e->heardNow() - (int64_t) (8.0 * sr));
    run (*e, *f, s, 0.5);
    e->replayStop();
    auto stopped = run (*e, *f, s, 0.2);
    CHECK (rms (stopped.data() + 4800, 4800) < 1.0e-3, "stopped: %g", rms (stopped.data() + 4800, 4800));
    delete f;
}

//==============================================================================
// a take: hits (decaying noise bursts) at known places, over a quiet tone
static radio::Take::Audio hits (double sr, const std::vector<double>& at, const std::vector<float>& amp, double seconds)
{
    radio::Take::Audio a;
    a.sampleRate = sr;
    const int n = (int) (seconds * sr);
    a.l.assign ((size_t) n, 0.0f);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (int i = 0; i < n; ++i)
        a.l[(size_t) i] = 0.02f * (float) std::sin (twoPi * 220.0 * i / sr);
    for (size_t h = 0; h < at.size(); ++h)
    {
        const int s0 = (int) (at[h] * sr);
        for (int i = 0; i < (int) (0.15 * sr) && s0 + i < n; ++i)
            a.l[(size_t) (s0 + i)] += amp[h] * u (rng) * std::exp (-(float) i / (float) (0.03 * sr));
    }
    a.r = a.l;
    return a;
}

static void testSlicer()
{
    const double sr = 48000.0;
    std::vector<double> at { 0.30, 0.71, 1.05, 1.62, 2.00, 2.48, 2.90, 3.41 };
    std::vector<float> amp { 0.9f, 0.3f, 0.7f, 0.25f, 0.95f, 0.35f, 0.6f, 0.2f };
    auto a = hits (sr, at, amp, 4.0);
    auto on = radio::findOnsets (a.l.data(), a.r.data(), (int) a.l.size(), sr, 0.5f);
    CHECK (on.size() == at.size(), "%zu onsets of %zu", on.size(), at.size());
    if (on.size() != at.size())
        for (auto& o : on) std::printf ("    onset %.4f s  %.3f\n", o.at / sr, o.strength);
    int close = 0;
    for (size_t h = 0; h < at.size(); ++h)
        for (auto& o : on)
        {
            const double d = (double) o.at / sr - at[h];
            if (d <= 0.0005 && d > -0.004)
                ++close;
        }
    CHECK (close == (int) at.size(), "%d placed within (-4 ms, +0.5 ms]", close);
    // sixteen pads, eight hits: eight pads, each from its hit to the next
    auto sl = radio::slice (a.l.data(), a.r.data(), (int) a.l.size(), sr, 16, 0.5f);
    CHECK (sl.size() == 8, "%zu pads", sl.size());
    if (sl.size() == 8)
        CHECK (std::abs ((double) sl[1].start / sr - 0.71) < 0.005 && sl[0].end == sl[1].start, "pad 2 at %.4f", sl[1].start / sr);
    // four pads: the four strongest, in order (0.30, 1.05, 2.00, 2.90)
    auto four = radio::slice (a.l.data(), a.r.data(), (int) a.l.size(), sr, 4, 0.5f);
    CHECK (four.size() == 4, "%zu", four.size());
    if (four.size() == 4)
    {
        const double want[4] = { 0.30, 1.05, 2.00, 2.90 };
        bool ok = true;
        for (int i = 0; i < 4; ++i)
            ok = ok && std::abs ((double) four[(size_t) i].start / sr - want[i]) < 0.005;
        CHECK (ok, "strongest four: %.3f %.3f %.3f %.3f", four[0].start / sr, four[1].start / sr, four[2].start / sr, four[3].start / sr);
        // each one ends at the next attack, kept or not (0.71 after 0.30)
        CHECK (std::abs ((double) four[0].end / sr - 0.71) < 0.005, "pad 1 ends at %.3f", four[0].end / sr);
    }
    // a louder passage doesn't hide its hits: the same hits at a tenth of the level, then ten times: all found
    {
        std::vector<float> soft (amp);
        for (auto& v : soft) v *= 0.1f;
        auto b = hits (sr, at, soft, 4.0);
        auto o2 = radio::findOnsets (b.l.data(), b.r.data(), (int) b.l.size(), sr, 0.5f);
        CHECK (o2.size() == at.size(), "quiet take: %zu onsets", o2.size());
        if (o2.size() != at.size())
            for (auto& o : o2) std::printf ("    onset %.4f s  %.3f\n", o.at / sr, o.strength);
    }
    // a steady tone (no attack): cut evenly
    {
        radio::Take::Audio t;
        t.l.resize ((size_t) (2.0 * sr));
        for (size_t i = 0; i < t.l.size(); ++i) t.l[i] = 0.5f * (float) std::sin (twoPi * 330.0 * (double) i / sr);
        t.r = t.l;
        auto ev = radio::slice (t.l.data(), t.r.data(), (int) t.l.size(), sr, 8, 0.5f);
        CHECK (ev.size() == 8 && ev[1].start == (int) t.l.size() / 8, "even: %zu", ev.size());
    }
    // sensitivity: at 0 only the strong hits, at 1 all of them
    {
        auto lo = radio::findOnsets (a.l.data(), a.r.data(), (int) a.l.size(), sr, 0.0f);
        auto hi = radio::findOnsets (a.l.data(), a.r.data(), (int) a.l.size(), sr, 1.0f);
        CHECK (lo.size() <= on.size() && hi.size() >= on.size(), "sensitivity %zu <= %zu <= %zu", lo.size(), on.size(), hi.size());
    }
    // speed: thirty seconds cut in well under a second
    {
        std::vector<double> many;
        std::vector<float> amps;
        for (int i = 0; i < 60; ++i) { many.push_back (0.25 + i * 0.49); amps.push_back (0.3f + 0.6f * (float) ((i * 37) % 10) / 10.0f); }
        auto big = hits (sr, many, amps, 30.0);
        const auto t0 = std::chrono::steady_clock::now();
        auto o3 = radio::findOnsets (big.l.data(), big.r.data(), (int) big.l.size(), sr, 0.5f);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (secs < 1.0, "30 s cut in %.3f s", secs);
        CHECK (o3.size() == many.size(), "30 s: %zu onsets of %zu", o3.size(), many.size());
        std::printf ("  slicer: 30 s take in %.0f ms, %zu attacks\n", secs * 1000.0, o3.size());
    }
}

//==============================================================================
static void testPads()
{
    const double sr = 48000.0;
    radio::Engine e;
    e.prepare (sr, 512);
    e.allocate();
    radio::Take take;
    auto audio = std::make_shared<radio::Take::Audio> (hits (sr, { 0.1, 0.6 }, { 0.8f, 0.5f }, 1.0));
    take.audio = audio;
    take.slices = radio::slice (audio->l.data(), audio->r.data(), (int) audio->l.size(), sr, 8, 0.5f);
    CHECK (take.slices.size() == 2, "%zu slices", take.slices.size());
    e.setTake (&take);
    radio::Settings s;
    s.stat = 0.0f;
    s.padLevel = 0.8f;
    std::vector<float> L (512), R (512);
    e.render (L.data(), R.data(), 512, s, nullptr, 0);
    CHECK (e.takeInUse() == &take, "the take in use");
    // C1 + 1: the second pad, from its attack, as it is (velocity 127: the level's gain)
    radio::Engine::Event ev { 0, radio::firstPadNote + 1, 127 };
    e.render (L.data(), R.data(), 512, s, &ev, 1);
    const int st = take.slices[1].start;
    const float g = 0.8f * 1.25f;
    double err = 0;
    for (int i = 64; i < 512; ++i)      // (past its 0.8 ms fade in)
        err = std::max (err, (double) std::abs (L[(size_t) i] - audio->l[(size_t) (st + i)] * g));
    CHECK (err < 1.0e-4, "pad 2 plays its slice: %g", err);
    CHECK (e.padGlow[1].load() > 0.5f, "pad 2 lit");
    // a pad with nothing in it, a note under C1: nothing
    radio::Engine::Event none[2] { { 0, radio::firstPadNote + 9, 100 }, { 0, radio::firstPadNote - 1, 100 } };
    e.reset();
    e.render (L.data(), R.data(), 512, s, none, 2);
    CHECK (rms (L.data(), 512) < 1.0e-6, "empty pads silent");
    // CHOKE: a new pad cuts the one playing (5 ms)
    s.choke = true;
    radio::Engine::Event a { 0, radio::firstPadNote, 127 }, b { 0, radio::firstPadNote + 1, 127 };
    e.render (L.data(), R.data(), 512, s, &a, 1);
    e.render (L.data(), R.data(), 512, s, &b, 1);
    int active = 0;
    for (int i = 0; i < radio::numPads; ++i) active += e.padAt[(size_t) i].load() >= 0.0f ? 1 : 0;
    e.render (L.data(), R.data(), 512, s, nullptr, 0);
    active = 0;
    for (int i = 0; i < radio::numPads; ++i) active += e.padAt[(size_t) i].load() >= 0.0f ? 1 : 0;
    CHECK (active == 1, "choke: %d pads playing", active);
    // a new take: the pads playing the old one stop at once
    radio::Take other = take;
    e.setTake (&other);
    e.render (L.data(), R.data(), 512, s, nullptr, 0);
    active = 0;
    for (int i = 0; i < radio::numPads; ++i) active += e.padAt[(size_t) i].load() >= 0.0f ? 1 : 0;
    CHECK (active == 0 && e.takeInUse() == &other, "a new take: %d playing", active);
    // DECAY short: gone within its time; TUNE +12: twice as fast (the slice played in half its length)
    s.choke = false;
    s.padDecay = 0.0f;
    e.reset();
    radio::Engine::Event c { 0, radio::firstPadNote, 127 };
    e.render (L.data(), R.data(), 512, s, &c, 1);
    for (int k = 0; k < 20; ++k)
        e.render (L.data(), R.data(), 512, s, nullptr, 0);
    CHECK (rms (L.data(), 512) < 1.0e-4, "short decay gone: %g", rms (L.data(), 512));
}

//==============================================================================
static void testStorm()
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    float top = 0.0f;
    bool finite = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        auto e = std::make_unique<radio::Engine>();
        e->prepare (sr, 512);
        e->allocate();
        Feeder f (e->bank, 0, 300.0);
        e->bank.setState (0, radio::live, radio::web);
        radio::Take take;
        auto audio = std::make_shared<radio::Take::Audio> (hits (sr, { 0.1, 0.3, 0.5, 0.7 }, { 0.9f, 0.8f, 0.9f, 0.7f }, 1.0));
        take.audio = audio;
        take.slices = radio::slice (audio->l.data(), audio->r.data(), (int) audio->l.size(), sr, 16, 0.6f);
        e->setTake (&take);
        std::vector<float> L (512), R (512);
        double ppq = 0.0;
        for (int round = 0; round < 30; ++round)
        {
            radio::Settings s;
            s.rx = 0; s.mode = (int) (u (rng) * 3.0f); s.len = (int) (u (rng) * 10.0f);
            s.squelch = u (rng); s.grab = u (rng); s.depth = 1.0f + 19.0f * u (rng); s.tune = -24.0f + 48.0f * u (rng);
            s.stat = u (rng); s.level = u (rng); s.band = u (rng); s.centre = 300.0f + 2700.0f * u (rng); s.fade = u (rng);
            s.fadeRate = 0.04f + 4.0f * u (rng); s.whistle = u (rng); s.crackle = u (rng); s.drive = u (rng);
            s.padTune = -24.0f + 48.0f * u (rng); s.padDecay = u (rng); s.padLevel = u (rng); s.choke = u (rng) > 0.5f;
            s.playing = u (rng) > 0.3f; s.bpm = 60.0 + 140.0 * u (rng);
            const bool dry = u (rng) < 0.25f;
            for (int blk = 0; blk < (int) (0.4 * sr / 512); ++blk)
            {
                if (! dry)
                    f.feed (512.0 / sr, 0.99 + 0.02 * u (rng));
                radio::Engine::Event ev { (int) (u (rng) * 511.0f), radio::firstPadNote + (int) (u (rng) * 6.0f), 1 + (int) (u (rng) * 126.0f) };
                s.ppq = ppq;
                e->render (L.data(), R.data(), 512, s, &ev, u (rng) < 0.3f ? 1 : 0);
                ppq += 512.0 / sr * s.bpm / 60.0;
                for (int i = 0; i < 512; ++i)
                {
                    finite = finite && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]);
                    top = std::max (top, std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
                }
            }
        }
    }
    CHECK (finite && top < 3.0f, "random settings, dropouts, pads: peak %.2f", top);
    std::printf ("  storm: peak %.2f\n", top);
}

static void testCpu()
{
    const double sr = 48000.0;
    Feeder* f = nullptr;
    auto e = playing (sr, 900.0, f);
    radio::Settings s;
    s.tune = 5.0f; s.squelch = 0.2f; s.band = 0.5f; s.fade = 0.5f; s.whistle = 0.4f; s.crackle = 0.4f; s.drive = 0.5f; s.stat = 0.3f;
    std::vector<float> L (512), R (512);
    const auto t0 = std::chrono::steady_clock::now();
    const int blocks = (int) (10.0 * sr / 512);
    for (int blk = 0; blk < blocks; ++blk)
    {
        f->feed (512.0 / sr);
        e->render (L.data(), R.data(), 512, s, nullptr, 0);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  cpu: LIVE with all its air, %.2f %% of real time\n", secs / 10.0 * 100.0);
    CHECK (secs / 10.0 < 0.05, "%.3f of real time", secs / 10.0);
    delete f;
}

// CUT (0.4.7): a rhythm cut out of two receivers at once (one at 440 Hz, the other at 1200 Hz, each struck every
// eighth): both heard, the steps drawn from both, DENSITY 0 leaves only the first step; a switch of receiver in LIVE
// (a mod stepping through them) is ducked, not cut
static void testCut()
{
    const double sr = 48000.0;
    auto e = std::make_unique<radio::Engine>();
    e->prepare (sr, 512);
    e->allocate();
    e->bank.setState (0, radio::live, radio::web);
    e->bank.setState (1, radio::live, radio::web);
    std::vector<int16_t> c0 (1024), c1 (1024);
    double ph0 = 0, ph1 = 0;
    int t = 0;
    auto feed = [&] ()
    {
        for (int i = 0; i < 512; ++i, ++t)
        {
            const double env = std::exp (-(double) (t % 6000) / 1500.0);
            const auto a = (int16_t) (14000.0 * env * std::sin (ph0));
            const auto b = (int16_t) (14000.0 * env * std::sin (ph1));
            ph0 += twoPi * 440.0 / radio::rate;
            ph1 += twoPi * 1200.0 / radio::rate;
            c0[(size_t) (2 * i)] = c0[(size_t) (2 * i + 1)] = a;
            c1[(size_t) (2 * i)] = c1[(size_t) (2 * i + 1)] = b;
        }
        e->bank.push (0, c0.data(), 512);
        e->bank.push (1, c1.data(), 512);
    };
    radio::Settings q;
    q.stat = 0.0f;
    q.mode = radio::modeLive;
    std::vector<float> L (512), R (512), out;
    double ppq = 0.0;
    auto block = [&] (bool keep)
    {
        feed();
        q.ppq = ppq;
        e->render (L.data(), R.data(), 512, q, nullptr, 0);
        ppq += 512.0 / sr * 2.0;            // (120 bpm)
        if (keep)
            out.insert (out.end(), L.begin(), L.end());
    };
    // three seconds of LIVE first: the receivers' histories fill (and a switch from RX 1 to RX 2 at 2.5 s)
    float worst = 0.0f;
    for (int b = 0; b < (int) (3.0 * sr / 512); ++b)
    {
        q.rx = b < (int) (2.5 * sr / 512) ? 0 : 1;
        block (false);
        if (b > (int) (2.4 * sr / 512))
            for (int i = 1; i < 512; ++i)
                worst = std::max (worst, std::abs (L[(size_t) i] - L[(size_t) i - 1]));
    }
    // (a 1200 Hz tone at 0.43 moves 0.07 a sample at most)
    CHECK (worst < 0.1f, "LIVE, RX 1 to RX 2: ducked, no click (worst step %.3f)", worst);
    q.mode = radio::modeCut;
    q.playing = true;
    q.bpm = 120.0;
    q.len = 7;                  // sixteen steps
    q.density = 1.0f;
    q.vary = 0.0f;
    q.grab = 0.0f;
    q.depth = 2.0f;
    for (int b = 0; b < (int) (4.0 * sr / 512); ++b)
        block (true);
    int fromA = 0, fromB = 0;
    for (int j = 0; j < 16; ++j)
    {
        const int v = e->cutShown[(size_t) j].load();
        fromA += v >= 0 && (v & 15) == 0 ? 1 : 0;
        fromB += v >= 0 && (v & 15) == 1 ? 1 : 0;
    }
    CHECK (fromA > 0 && fromB > 0 && fromA + fromB == 16, "CUT draws from both receivers: %d and %d of 16", fromA, fromB);
    CHECK (e->cutCount.load() == 16 && e->cutNow.load() >= 0, "sixteen steps, one playing (%d)", e->cutNow.load());
    const int n = (int) out.size();
    const double a = peakHz (out.data() + n / 2, 32768, sr, 380, 500, 2.0), bz = peakHz (out.data() + n / 2, 32768, sr, 1100, 1300, 2.0);
    const double level = rms (out.data() + n / 2, n / 2);
    CHECK (std::abs (a - 440.0) < 6.0 && std::abs (bz - 1200.0) < 8.0 && level > 0.02, "both heard: %.0f Hz and %.0f Hz (rms %.3f)", a, bz, level);
    // DENSITY 0, drawn again: only the first step plays
    q.density = 0.0f;
    e->cutRoll.store (true);
    out.clear();
    for (int b = 0; b < (int) (2.0 * sr / 512); ++b)
        block (true);
    int rests = 0;
    for (int j = 1; j < 16; ++j)
        rests += e->cutShown[(size_t) j].load() < 0 ? 1 : 0;
    CHECK (rests == 15 && e->cutShown[0].load() >= 0, "DENSITY 0: the first step alone (%d rests)", rests);
}

// (0.4.18) SEEK: the next clean signal in a receiver's panorama (made up here: a floor, its noise, carriers, sidebands,
// a receiver's birdie, two stations on top of each other, a voice in a sideband, a morse carrier)
static void testSeek()
{
    std::mt19937 rng (7);
    std::normal_distribution<float> jitter (0.0f, 1.0f);
    auto addPow = [] (float a, float b) { return 10.0f * std::log10 (std::pow (10.0f, a / 10.0f) + std::pow (10.0f, b / 10.0f)); };
    struct Sig { double khz; float carrier, sides, width; };       // a carrier's level over the floor, its sidebands', how wide
    auto make = [&] (double start, int zoom, float floorDb, float peakOver, const std::vector<Sig>& sigs)
    {
        radio::Panorama p;
        p.startKhz = start;
        p.binKhz = 30000.0 / (double) (1 << zoom) / 1024.0;
        p.avg.resize (1024);
        p.peak.resize (1024);
        for (int i = 0; i < 1024; ++i)
        {
            const double f = p.khzAt (i);
            float a = floorDb + 0.7f * jitter (rng), pk = floorDb + peakOver + 1.5f * jitter (rng);
            for (const auto& s : sigs)
            {
                const double d = std::abs (f - s.khz) / p.binKhz;
                if (s.carrier > 0.0f && d < 2.0)
                {
                    const float c = floorDb + s.carrier - 6.0f * (float) (d * d);
                    a = addPow (a, c);
                    pk = addPow (pk, c + 1.0f);
                }
                const double off = (f - s.khz);
                if (s.sides > 0.0f && std::abs (off) >= 0.4 && std::abs (off) <= s.width && (s.carrier > 0.0f || off > 0.0))
                {
                    a = addPow (a, floorDb + s.sides - 6.0f);
                    pk = addPow (pk, floorDb + s.sides);
                }
            }
            p.avg[(size_t) i] = a;
            p.peak[(size_t) i] = pk;
        }
        return p;
    };
    // AM, over the 31 m band (zoom 5: 937 kHz, a bin 0.92 kHz)
    const auto am = make (9000.0, 5, -110.0f, 4.0f, {
        { 9420.0, 35.0f, 12.0f, 4.5f },        // the one heard
        { 9500.0, 32.0f, 10.0f, 4.5f },        // the next one up
        { 9580.0, 40.0f, 15.0f, 4.5f },
        { 9640.0, 12.0f, 4.0f, 4.5f },         // too weak
        { 9655.0, 30.0f, 10.0f, 4.5f },        // two on top of each other: they beat
        { 9660.0, 30.0f, 10.0f, 4.5f },
        { 9700.0, 25.0f, 0.0f, 0.0f },         // a bare carrier: the receiver's own birdie
        { 9750.0, 28.0f, 8.0f, 4.5f },
        { 9313.0, 30.0f, 10.0f, 4.5f } });     // off the grid
    auto up = [&] (const radio::Panorama& p, double from, radio::Demod d) { return radio::nextSignal (p, from, 1, d); };
    auto dn = [&] (const radio::Panorama& p, double from, radio::Demod d) { return radio::nextSignal (p, from, -1, d); };
    auto f = up (am, 9420.0, radio::Demod::am);
    CHECK (std::abs (f.khz - 9500.0) < 0.01 && f.snr > 25.0f, "AM: up from 9420, the next station on the grid (%.2f, %.1f dB)", f.khz, f.snr);
    f = up (am, 9500.0, radio::Demod::am);
    CHECK (std::abs (f.khz - 9580.0) < 0.01, "AM: up again (%.2f)", f.khz);
    f = up (am, 9580.0, radio::Demod::am);
    CHECK (std::abs (f.khz - 9750.0) < 0.01, "AM: the weak one, the two beating, the birdie passed by (%.2f)", f.khz);
    f = dn (am, 9500.0, radio::Demod::am);
    CHECK (std::abs (f.khz - 9420.0) < 0.01, "AM: down (%.2f)", f.khz);
    f = dn (am, 9420.0, radio::Demod::am);
    CHECK (std::abs (f.khz - 9313.0) < 0.7, "AM: down, off the grid, where it is (%.2f)", f.khz);
    f = up (am, 9750.0, radio::Demod::am);
    CHECK (f.khz == 0.0, "AM: nothing further up in this stretch (%.2f)", f.khz);
    f = up (am, 9418.0, radio::Demod::am);
    CHECK (std::abs (f.khz - 9500.0) < 0.01, "AM: a little off the station heard: still the next one (%.2f)", f.khz);

    // USB, in the 40 m band (zoom 6: 469 kHz, a bin 0.46 kHz): voices, the dial 1.5 kHz under their middle
    const auto ssb = make (7000.0, 6, -115.0f, 5.0f, {
        { 7150.0, 0.0f, 16.0f, 2.7f },
        { 7200.0, 0.0f, 13.0f, 2.7f },
        { 7180.0, 30.0f, 0.0f, 0.0f },         // a morse carrier: not a voice
        { 7260.0, 0.0f, 5.0f, 2.7f } });       // too weak
    f = up (ssb, 7100.0, radio::Demod::usb);
    CHECK (std::abs (f.khz - 7150.0) <= 0.6, "USB: the next voice up (%.2f, %.1f dB)", f.khz, f.snr);
    f = up (ssb, 7150.0, radio::Demod::usb);
    CHECK (std::abs (f.khz - 7200.0) <= 0.6, "USB: the next one, the morse carrier passed by (%.2f)", f.khz);
    f = up (ssb, 7200.0, radio::Demod::usb);
    CHECK (f.khz == 0.0, "USB: no voice loud enough further up (%.2f)", f.khz);

    // CW (zoom 7: 234 kHz, a bin 0.23 kHz): a narrow carrier, the dial 500 Hz under it
    const auto cw = make (7000.0, 7, -118.0f, 4.0f, { { 7010.0, 25.0f, 0.0f, 0.0f }, { 7020.0, 9.0f, 0.0f, 0.0f } });
    f = up (cw, 7005.0, radio::Demod::cw);
    CHECK (std::abs (f.khz - 7009.5) <= 0.15, "CW: the carrier, the dial 500 Hz under it (%.2f)", f.khz);
    f = up (cw, 7009.5, radio::Demod::cw);
    CHECK (f.khz == 0.0, "CW: the weak one left (%.2f)", f.khz);
    CHECK (radio::seekZoom (radio::Demod::am) == 5 && radio::seekZoom (radio::Demod::cw) == 7, "the zooms");
}

int main()
{
    testDecoder();
    testFeed();
    testListen();
    testCut();
    testHeard();
    testSlicer();
    testPads();
    testStorm();
    testCpu();
    testSeek();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
