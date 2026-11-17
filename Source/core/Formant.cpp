#include "Formant.h"

#include "signalsmith-linear/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace rk
{
namespace
{
    using Cx = std::complex<float>;

    struct Fft
    {
        explicit Fft (int n) : N (n), fft ((size_t) n), a ((size_t) n), b ((size_t) n) {}
        int N;
        signalsmith::linear::Pow2FFT<float> fft;
        std::vector<Cx> a, b;
    };

    int nextPow2 (int v)
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    // smooth a log spectrum (bins 0..N/2) by keeping the first `order` cepstral coefficients
    void lifter (Fft& f, const std::vector<float>& in, std::vector<float>& out, int order)
    {
        const int N = f.N, H = N / 2;
        for (int k = 0; k <= H; ++k)
            f.a[(size_t) k] = Cx (in[(size_t) k], 0.0f);
        for (int k = 1; k < H; ++k)
            f.a[(size_t) (N - k)] = Cx (in[(size_t) k], 0.0f);
        f.fft.ifft (f.a.data(), f.b.data());        // real cepstrum (times N)
        const float scale = 1.0f / (float) N;
        for (int q = 0; q < N; ++q)
        {
            const int d = std::min (q, N - q);
            // a short taper at the cut keeps the envelope from ringing
            float w = 1.0f;
            if (d > order)
                w = 0.0f;
            else if (d > order * 3 / 4)
                w = 0.5f + 0.5f * std::cos (3.14159265f * (float) (d - order * 3 / 4) / (float) std::max (1, order - order * 3 / 4));
            f.b[(size_t) q] *= w * scale;
        }
        f.fft.fft (f.b.data(), f.a.data());
        out.resize ((size_t) H + 1);
        for (int k = 0; k <= H; ++k)
            out[(size_t) k] = f.a[(size_t) k].real();
    }

    std::vector<float> trueEnvelopeWith (Fft& f, const std::vector<float>& logMag, int order, int iterations)
    {
        std::vector<float> A = logMag, C;
        lifter (f, A, C, order);
        for (int it = 0; it < iterations; ++it)
        {
            for (size_t k = 0; k < A.size(); ++k)
                A[k] = std::max (A[k], C[k]);
            lifter (f, A, C, order);
        }
        return C;
    }
}

std::vector<float> trueEnvelope (const std::vector<float>& logMag, int order, int iterations)
{
    Fft f ((int) (logMag.size() - 1) * 2);
    return trueEnvelopeWith (f, logMag, order, iterations);
}

std::vector<float> harmonicEnvelope (const std::vector<float>& lm, double S)
{
    const int n = (int) lm.size();
    std::vector<float> env ((size_t) n, 0.0f);
    if (n < 4 || S < 1.0)
        return lm;
    // the top of each harmonic: the loudest bin within half a harmonic of where it should be, refined between
    // bins; a window per harmonic, side by side, so every peak of the spectrum is seen once
    std::vector<double> pos, val;
    for (int k = 1; k * S < n - 1; ++k)
    {
        const int lo = std::max (1, (int) std::ceil (k * S - S * 0.5)), hi = std::min (n - 2, (int) std::floor (k * S + S * 0.5));
        if (hi < lo)
            continue;
        int m = lo;
        for (int i = lo + 1; i <= hi; ++i)
            if (lm[(size_t) i] > lm[(size_t) m])
                m = i;
        const double y0 = lm[(size_t) m - 1], y1 = lm[(size_t) m], y2 = lm[(size_t) m + 1];
        const double den = y0 - 2.0 * y1 + y2;
        const double off = std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0;
        pos.push_back (m + off);
        val.push_back (y1 - 0.25 * (y0 - y2) * off);
    }
    if (pos.empty())
        return lm;
    // straight lines (in dB) from one harmonic to the next; flat after the last. Below the fundamental, the
    // slope from the first to the second harmonic goes on (per octave): a sound played lower takes its new
    // fundamental's level from there, and a sawtooth's spectrum keeps rising towards the bass
    double slope = 0.0;   // ln amplitude per octave
    if (pos.size() >= 2 && pos[1] > pos[0] && pos[0] > 0.0)
        slope = std::clamp ((val[1] - val[0]) / std::log2 (pos[1] / pos[0]), -2.1, 2.1);   // within +-18 dB/octave
    size_t j = 0;
    for (int b = 0; b < n; ++b)
    {
        while (j + 1 < pos.size() && pos[j + 1] < b)
            ++j;
        double v;
        if (b <= pos.front())
            v = val.front() + slope * std::log2 (std::max ((double) b, pos.front() * 0.25) / pos.front());
        else if (j + 1 >= pos.size())
            v = val.back();
        else
        {
            const double t = (b - pos[j]) / std::max (1.0e-9, pos[j + 1] - pos[j]);
            v = val[j] + std::clamp (t, 0.0, 1.0) * (val[j + 1] - val[j]);
        }
        env[(size_t) b] = (float) v;
    }
    return env;
}

void correctFormants (Channels& y, double sr, double ratio, double f0Out, const FormantSettings& set)
{
    if (y.empty() || y[0].empty() || std::abs (ratio - 1.0) < 1.0e-4 || set.amount <= 0.0)
        return;
    if (f0Out <= 0.0)
        f0Out = 220.0;
    const int base = sr <= 50000.0 ? 2048 : 4096;
    // at least 8 bins between harmonics, so the analysis window resolves them
    const int N = std::clamp (nextPow2 ((int) (8.0 * sr / f0Out)), base, 16384);
    const int H = N / 2, hop = N / 4;
    const int len = (int) y[0].size();
    const double maxLn = set.maxDb / 8.685889638;
    const double binsPerHarmonic = f0Out / (sr / N);

    std::vector<float> win ((size_t) N), analysis ((size_t) N);
    for (int n = 0; n < N; ++n)
    {
        const double x = 6.283185307179586 * n / N;
        win[(size_t) n] = (float) std::sqrt (0.5 - 0.5 * std::cos (x));
        // Blackman-Harris for the analysis: its leakage stays below the quietest harmonics
        analysis[(size_t) n] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
    }

    const auto mid = mixDown (y);
    const int frames = (len + N) / hop + 1;
    auto frameStart = [&] (int k) { return -N + k * hop; };

    Fft f (N);
    std::vector<std::vector<float>> logG ((size_t) frames, std::vector<float> ((size_t) H + 1, 0.0f));
    std::vector<float> frameDb ((size_t) frames, -200.0f);
    std::vector<float> logMag ((size_t) H + 1);

    // pass 1: the envelopes, and the correction for each frame
    float loudest = -200.0f;
    for (int k = 0; k < frames; ++k)
    {
        const int s = frameStart (k);
        double energy = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const int i = s + n;
            const float v = (i >= 0 && i < len) ? mid[(size_t) i] * analysis[(size_t) n] : 0.0f;
            f.a[(size_t) n] = Cx (v, 0.0f);
            energy += (double) v * v;
        }
        frameDb[(size_t) k] = (float) (10.0 * std::log10 (energy / N + 1.0e-30));
        loudest = std::max (loudest, frameDb[(size_t) k]);
        f.fft.fft (f.a.data(), f.b.data());
        float maxMag = 0.0f;
        for (int b = 0; b <= H; ++b)
            maxMag = std::max (maxMag, std::abs (f.b[(size_t) b]));
        const float floorMag = std::max (maxMag * 1.0e-6f, 1.0e-12f);
        for (int b = 0; b <= H; ++b)
            logMag[(size_t) b] = std::log (std::max (std::abs (f.b[(size_t) b]), floorMag));
        const auto env = harmonicEnvelope (logMag, binsPerHarmonic);
        auto& g = logG[(size_t) k];
        for (int b = 0; b <= H; ++b)
        {
            // where this bin's content came from: the envelope there is the one it should keep
            const double src = std::min ((double) H, (double) b * ratio);
            const int i0 = (int) src;
            const int i1 = std::min (H, i0 + 1);
            const float fr = (float) (src - i0);
            const float want = env[(size_t) i0] + fr * (env[(size_t) i1] - env[(size_t) i0]);
            g[(size_t) b] = (float) std::clamp ((double) (want - env[(size_t) b]) * set.amount, -maxLn, maxLn);
        }
        // the frame keeps its energy: the correction moves the timbre, not the level (neighbouring keys from
        // different recordings then match)
        double e0 = 0.0, e1 = 0.0;
        for (int b = 0; b <= H; ++b)
        {
            const double p = std::exp (2.0 * (double) logMag[(size_t) b]);
            e0 += p;
            e1 += p * std::exp (2.0 * (double) g[(size_t) b]);
        }
        if (e0 > 0.0 && e1 > 0.0)
        {
            const float k2 = (float) std::clamp (0.5 * std::log (e0 / e1), -maxLn, maxLn);
            for (auto& v : g)
                v += k2;
        }
    }

    // quiet frames (silence, the very end of a tail) are left alone; a short smoothing over time
    for (int k = 0; k < frames; ++k)
    {
        const float w = std::clamp ((frameDb[(size_t) k] - (loudest - 70.0f)) / 20.0f, 0.0f, 1.0f);
        if (w < 1.0f)
            for (auto& v : logG[(size_t) k])
                v *= w;
    }
    {
        auto copy = logG;
        for (int k = 0; k < frames; ++k)
        {
            const auto& p = copy[(size_t) std::max (0, k - 1)];
            const auto& c = copy[(size_t) k];
            const auto& n = copy[(size_t) std::min (frames - 1, k + 1)];
            for (int b = 0; b <= H; ++b)
                logG[(size_t) k][(size_t) b] = 0.25f * p[(size_t) b] + 0.5f * c[(size_t) b] + 0.25f * n[(size_t) b];
        }
    }

    // pass 2: each channel equalised frame by frame, overlap-added
    std::vector<float> gain ((size_t) H + 1);
    for (auto& ch : y)
    {
        std::vector<float> out ((size_t) len + (size_t) (2 * N), 0.0f);   // index i + N
        for (int k = 0; k < frames; ++k)
        {
            const int s = frameStart (k);
            for (int n = 0; n < N; ++n)
            {
                const int i = s + n;
                f.a[(size_t) n] = Cx ((i >= 0 && i < len) ? ch[(size_t) i] * win[(size_t) n] : 0.0f, 0.0f);
            }
            f.fft.fft (f.a.data(), f.b.data());
            const auto& g = logG[(size_t) k];
            for (int b = 0; b <= H; ++b)
                gain[(size_t) b] = std::exp (g[(size_t) b]);
            f.b[0] *= gain[0];
            f.b[(size_t) H] *= gain[(size_t) H];
            for (int b = 1; b < H; ++b)
            {
                f.b[(size_t) b] *= gain[(size_t) b];
                f.b[(size_t) (N - b)] *= gain[(size_t) b];
            }
            f.fft.ifft (f.b.data(), f.a.data());
            // sqrt-Hann twice = Hann; Hann every N/4 sums to 2; the inverse FFT is N times too big
            const float scale = 0.5f / (float) N;
            for (int n = 0; n < N; ++n)
            {
                const int i = s + n + N;
                out[(size_t) i] += f.a[(size_t) n].real() * win[(size_t) n] * scale;
            }
        }
        for (int i = 0; i < len; ++i)
            ch[(size_t) i] = out[(size_t) (i + N)];
    }
}
}
