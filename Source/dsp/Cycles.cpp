#include "Cycles.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace dsp
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    using cpx = std::complex<double>;

    // in place, radix 2 (n a power of two); inverse unscaled
    void fft (std::vector<cpx>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
            const cpx wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                cpx w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const cpx u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    // cubic (Hermite) through four points
    inline float hermite (float xm1, float x0, float x1, float x2, float t)
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    // Lanczos (a = 4) at a fractional position, outside the recording read as silence
    double lanczos (const float* x, int length, double p)
    {
        constexpr int a = 4;
        const int i0 = (int) std::floor (p);
        double s = 0.0, ws = 0.0;
        for (int k = i0 - a + 1; k <= i0 + a; ++k)
        {
            const double d = p - (double) k;
            double w = 1.0;
            if (std::abs (d) > 1.0e-9)
                w = (std::sin (pi * d) / (pi * d)) * (std::sin (pi * d / a) / (pi * d / a));
            if (std::abs (d) >= a)
                w = 0.0;
            ws += w;
            if (k >= 0 && k < length)
                s += w * (double) x[k];
        }
        return ws > 1.0e-9 ? s / ws : 0.0;
    }
}

int CycleBank::harmonicsAt (int level)
{
    return std::max (1, (int) std::floor (1024.0 * std::pow (2.0, -0.5 * (double) level)));
}

int CycleBank::levelFor (double hz, double sampleRate)
{
    const double allowed = 0.5 * sampleRate / std::max (1.0e-3, std::abs (hz));
    for (int l = 0; l < levels; ++l)
        if ((double) harmonicsAt (l) <= allowed)
            return l;
    return levels - 1;
}

CycleBank::CycleBank (const std::vector<std::vector<float>>& cycles, bool perCycle)
{
    n = (int) cycles.size();
    data.assign ((size_t) std::max (1, n) * levels * stride, 0.0f);
    std::vector<cpx> spec ((size_t) size), work ((size_t) size);
    // the spectra first (DC out), to find the peak that brings them all to 1
    std::vector<std::vector<cpx>> spectra ((size_t) n);
    for (int c = 0; c < n; ++c)
    {
        for (int i = 0; i < size; ++i)
            spec[(size_t) i] = cpx (i < (int) cycles[(size_t) c].size() ? (double) cycles[(size_t) c][(size_t) i] : 0.0, 0.0);
        fft (spec, false);
        spec[0] = 0.0;
        spec[(size_t) size / 2] = 0.0;
        spectra[(size_t) c] = spec;
    }
    std::vector<double> peak ((size_t) n, 0.0);
    for (int c = 0; c < n; ++c)
        for (int l = 0; l < levels; ++l)
        {
            const int h = harmonicsAt (l);
            work = spectra[(size_t) c];
            for (int k = h + 1; k < size - h; ++k)
                work[(size_t) k] = 0.0;
            fft (work, true);
            float* t = data.data() + ((size_t) c * levels + (size_t) l) * stride;
            for (int i = 0; i < size; ++i)
                t[i + 1] = (float) (work[(size_t) i].real() / (double) size);
            if (l == 0)
                for (int i = 0; i < size; ++i)
                    peak[(size_t) c] = std::max (peak[(size_t) c], (double) std::abs (t[i + 1]));
        }
    double all = 0.0;
    for (auto p : peak)
        all = std::max (all, p);
    for (int c = 0; c < n; ++c)
    {
        const double p = perCycle ? peak[(size_t) c] : all;
        const float g = p > 1.0e-9 ? (float) (1.0 / p) : 0.0f;
        for (int l = 0; l < levels; ++l)
        {
            float* t = data.data() + ((size_t) c * levels + (size_t) l) * stride;
            for (int i = 1; i <= size; ++i)
                t[i] *= g;
            // the wrap: one point before, three after
            t[0] = t[size];
            t[size + 1] = t[1];
            t[size + 2] = t[2];
            t[size + 3] = t[3];
        }
    }
}

float CycleBank::read (float position, double phase, int level) const
{
    if (n <= 0)
        return 0.0f;
    level = std::clamp (level, 0, levels - 1);
    const float p = std::clamp (position, 0.0f, 1.0f) * (float) (n - 1);
    const int c0 = std::min ((int) p, n - 1), c1 = std::min (c0 + 1, n - 1);
    const float cf = p - (float) c0;
    phase -= std::floor (phase);
    const double x = phase * (double) size;
    const int i = std::min ((int) x, size - 1);
    const float t = (float) (x - (double) i);
    const float* a = table (c0, level) + 1 + i;
    float y = hermite (a[-1], a[0], a[1], a[2], t);
    if (cf > 1.0e-4f && c1 != c0)
    {
        const float* b = table (c1, level) + 1 + i;
        y += cf * (hermite (b[-1], b[0], b[1], b[2], t) - y);
    }
    return y;
}

std::vector<float> CycleBank::flatten() const
{
    std::vector<float> out;
    out.reserve ((size_t) n * size);
    for (int c = 0; c < n; ++c)
        out.insert (out.end(), cycle (c), cycle (c) + size);
    return out;
}

std::shared_ptr<const CycleBank> CycleBank::basics()
{
    static const std::shared_ptr<const CycleBank> bank = []
    {
        // the anchors, additive (every harmonic up to the table's Nyquist), the cycles between them crossfaded
        auto additive = [] (auto&& amp)
        {
            std::vector<float> w ((size_t) size, 0.0f);
            for (int k = 1; k < size / 2; ++k)
            {
                double a = 0.0, ph = 0.0;
                amp (k, a, ph);
                if (std::abs (a) < 1.0e-9)
                    continue;
                for (int i = 0; i < size; ++i)
                    w[(size_t) i] += (float) (a * std::sin (2.0 * pi * k * (double) i / size + ph));
            }
            return w;
        };
        auto pulse = [&] (double width)
        {
            return additive ([width] (int k, double& a, double& ph) { a = 2.0 / (pi * k) * std::sin (pi * k * width); ph = pi / 2.0 - pi * k * width; });
        };
        std::vector<std::vector<float>> anchors;
        anchors.push_back (additive ([] (int k, double& a, double&) { a = k == 1 ? 1.0 : 0.0; }));                                   // sine
        anchors.push_back (additive ([] (int k, double& a, double&) { a = k % 2 ? 8.0 / (pi * pi * k * k) * (((k - 1) / 2) % 2 ? -1.0 : 1.0) : 0.0; }));   // triangle
        anchors.push_back (additive ([] (int k, double& a, double&) { a = 2.0 / (pi * k) * (k % 2 ? 1.0 : -1.0); }));               // saw
        anchors.push_back (pulse (0.5));                                                                                             // square
        anchors.push_back (pulse (0.25));
        anchors.push_back (pulse (0.1));
        // a saw hard-synced at 2.6 times its period (the classic sync sweep, frozen)
        {
            std::vector<float> w ((size_t) size);
            for (int i = 0; i < size; ++i)
            {
                const double ph = std::fmod ((double) i / size * 2.6, 1.0);
                w[(size_t) i] = (float) (2.0 * ph - 1.0);
            }
            anchors.push_back (w);
        }
        // a formant: a sine burst around the 7th harmonic, under a raised cosine
        anchors.push_back (additive ([] (int k, double& a, double&) { const double d = (double) k - 7.0; a = std::exp (-d * d / 6.0) * (k % 2 ? 1.0 : -0.7); }));
        const int total = 64, segments = (int) anchors.size() - 1;
        std::vector<std::vector<float>> cycles;
        for (int c = 0; c < total; ++c)
        {
            const double pos = (double) c / (total - 1) * segments;
            const int s = std::min ((int) pos, segments - 1);
            const double t = pos - s;
            std::vector<float> w ((size_t) size);
            // each anchor at the same loudness before mixing
            auto peakOf = [] (const std::vector<float>& x) { float p = 0.0f; for (auto v : x) p = std::max (p, std::abs (v)); return p > 0.0f ? p : 1.0f; };
            const float pa = peakOf (anchors[(size_t) s]), pb = peakOf (anchors[(size_t) s + 1]);
            for (int i = 0; i < size; ++i)
                w[(size_t) i] = (float) ((1.0 - t) * anchors[(size_t) s][(size_t) i] / pa + t * anchors[(size_t) s + 1][(size_t) i] / pb);
            cycles.push_back (std::move (w));
        }
        return std::make_shared<const CycleBank> (cycles, true);
    }();
    return bank;
}

std::vector<float> extractCycle (const float* x, int length, int at, double periodHint)
{
    if (x == nullptr || length <= 0 || periodHint < 2.0)
        return {};
    // the period: the best normalised autocorrelation within 15 % of the hint, over about four periods
    const int lo = std::max (2, (int) std::floor (periodHint * 0.85)), hi = (int) std::ceil (periodHint * 1.15);
    const int win = std::max (64, (int) (periodHint * 4.0));
    if (at < 0 || at + win + hi + 2 >= length)
        return {};
    double best = -2.0;
    int bestLag = (int) std::lround (periodHint);
    std::vector<double> score ((size_t) hi + 2, -2.0);
    for (int lag = lo; lag <= hi; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < win; ++i)
        {
            const double a = x[at + i], b = x[at + i + lag];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }
        const double s = xx > 1.0e-12 && yy > 1.0e-12 ? xy / std::sqrt (xx * yy) : -2.0;
        score[(size_t) lag] = s;
        if (s > best)
        {
            best = s;
            bestLag = lag;
        }
    }
    if (best < 0.8)
        return {};
    double period = (double) bestLag;
    if (bestLag > lo && bestLag < hi)
    {
        const double a = score[(size_t) bestLag - 1], b = score[(size_t) bestLag], c = score[(size_t) bestLag + 1];
        const double d = a - 2.0 * b + c;
        if (std::abs (d) > 1.0e-12)
            period += std::clamp (0.5 * (a - c) / d, -0.5, 0.5);
    }
    // a rising zero crossing to start on
    double start = (double) at;
    for (int i = at + 1; i < at + (int) period + 2 && i < length; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            start = (double) (i - 1) + (double) (-x[i - 1] / (x[i] - x[i - 1]));
            break;
        }
    if (start + period + 8.0 >= (double) length)
        return {};
    std::vector<float> c ((size_t) CycleBank::size);
    for (int i = 0; i < CycleBank::size; ++i)
        c[(size_t) i] = (float) lanczos (x, length, start + period * (double) i / CycleBank::size);
    // what drifted over the period (pitch, level) leaves a seam: spread it over the cycle
    const float seam = (float) lanczos (x, length, start + period) - c[0];
    for (int i = 0; i < CycleBank::size; ++i)
        c[(size_t) i] -= seam * (float) i / (float) CycleBank::size;
    return c;
}
}
