// REPLIK8 — RADIO (0.3.5): a take cut at its attacks (RadioSlicer.h). Our own.
#include "RadioSlicer.h"

#include <algorithm>
#include <array>
#include <complex>
#include <numeric>

namespace radio
{
namespace
{
    // an in-place radix-2 FFT (n a power of two)
    void fft (std::vector<std::complex<float>>& a)
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
            const float ang = -6.28318530717958647f / (float) len;
            const std::complex<float> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }
}

std::vector<Onset> findOnsets (const float* l, const float* r, int n, double sr, float sensitivity)
{
    std::vector<Onset> out;
    if (l == nullptr || n < 256 || sr < 1000.0)
        return out;
    // the take in mono, at its own level (the threshold must not depend on how loud the radio was)
    std::vector<float> m ((size_t) n);
    float peak = 1.0e-9f;
    for (int i = 0; i < n; ++i)
    {
        m[(size_t) i] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
        peak = std::max (peak, std::abs (m[(size_t) i]));
    }
    if (peak < 1.0e-5f)
        return out;
    for (auto& v : m)
        v /= peak;

    // the spectral flux: a frame every 5.3 ms, its window about 21 ms; the spectrum in sixth-octave bands (a noise
    // isn't an attack at each frame: bands of several bins hold still), each band against the louder of its
    // neighbours two frames back (a vibrato or a glide isn't one either)
    const int hop = std::max (32, (int) std::lround (sr / 187.5));
    int N = 256;
    while (N < hop * 4)
        N <<= 1;
    const int frames = n / hop + 1;
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i)
        win[(size_t) i] = 0.5f - 0.5f * std::cos (6.28318530717958647f * (float) i / (float) N);
    std::vector<int> edges;
    for (double f = 30.0; f < std::min (16000.0, sr * 0.45); f *= std::pow (2.0, 1.0 / 6.0))
    {
        const int k = std::max (1, (int) std::lround (f * N / sr));
        if (edges.empty() || k > edges.back())
            edges.push_back (k);
    }
    const int bands = (int) edges.size() - 1;
    if (bands < 4)
        return out;
    const int kLow = (int) std::lround (250.0 * N / sr), kHigh = (int) std::lround (2500.0 * N / sr);
    std::vector<std::complex<float>> buf ((size_t) N);
    std::vector<std::vector<float>> Y ((size_t) frames, std::vector<float> ((size_t) bands, 0.0f));
    std::vector<std::array<float, 3>> E ((size_t) frames);        // low, middle, high: their energy (dB)
    std::vector<float> flux ((size_t) frames, 0.0f), level ((size_t) frames, 0.0f);
    const float norm = 4.0f / (float) N;
    for (int f = 0; f < frames; ++f)
    {
        const int c = f * hop;
        for (int i = 0; i < N; ++i)
        {
            // (mirrored past its ends: a take cut out of a stream doesn't start with an attack)
            int k = c - N / 2 + i;
            k = k < 0 ? std::min (n - 1, -k) : (k >= n ? std::max (0, 2 * (n - 1) - k) : k);
            buf[(size_t) i] = { m[(size_t) k] * win[(size_t) i], 0.0f };
        }
        fft (buf);
        double g3[3] = { 1.0e-12, 1.0e-12, 1.0e-12 };
        for (int b = 0; b < bands; ++b)
        {
            double e = 0.0;
            for (int k = edges[(size_t) b]; k < edges[(size_t) b + 1]; ++k)
            {
                const double a = (double) std::norm (buf[(size_t) k]) * (double) (norm * norm);
                e += a;
                g3[k < kLow ? 0 : (k < kHigh ? 1 : 2)] += a;
            }
            Y[(size_t) f][(size_t) b] = std::log1p (1000.0f * (float) std::sqrt (e / (double) (edges[(size_t) b + 1] - edges[(size_t) b])));
        }
        for (int g = 0; g < 3; ++g)
            E[(size_t) f][(size_t) g] = 10.0f * (float) std::log10 (g3[g]);
        level[(size_t) f] = (float) std::sqrt (g3[0] + g3[1] + g3[2]);
        if (f >= 2)
        {
            float sum = 0.0f;
            const auto& before = Y[(size_t) f - 2];
            for (int b = 0; b < bands; ++b)
            {
                const float ref = std::max (before[(size_t) b], std::max (before[(size_t) std::max (0, b - 1)], before[(size_t) std::min (bands - 1, b + 1)]));
                sum += std::max (0.0f, Y[(size_t) f][(size_t) b] - ref);
            }
            flux[(size_t) f] = sum / (float) bands;
        }
    }

    // the peaks over a threshold that follows the surroundings, where the sound really rises (in its lows, its
    // middle or its highs: a hi-hat over a loud bass is one)
    std::vector<float> sorted (flux);
    std::nth_element (sorted.begin(), sorted.begin() + (long) (sorted.size() * 95 / 100), sorted.end());
    const float top = *std::max_element (flux.begin(), flux.end());
    const float scale = std::max (1.0e-6f, std::max (sorted[sorted.size() * 95 / 100], top * 0.05f));
    for (auto& v : flux)
        v /= scale;
    const float s = clamp01 (sensitivity);
    const float delta = 0.6f * std::pow (0.06f / 0.6f, s);           // 0.6 .. 0.06
    const float riseMin = 9.0f - 6.0f * s;                            // 9 .. 3 dB
    const int preMax = std::max (1, (int) (0.030 * sr / hop)), postMax = std::max (1, (int) (0.030 * sr / hop));
    const int preAvg = std::max (1, (int) (0.100 * sr / hop)), postAvg = std::max (1, (int) (0.070 * sr / hop));
    const int combine = std::max (1, (int) (0.045 * sr / hop));
    int last = -combine - 1;
    std::vector<int> picked;
    std::vector<float> strength;
    // (not where the window hangs past the take's ends: the mirror there may click)
    const int firstFrame = std::max (2, (N / 2 + hop - 1) / hop), lastFrame = std::min (frames - 1, (n - N / 2) / hop);
    for (int f = firstFrame; f <= lastFrame; ++f)
    {
        const float v = flux[(size_t) f];
        bool isMax = true;
        for (int j = std::max (0, f - preMax); j <= std::min (frames - 1, f + postMax) && isMax; ++j)
            isMax = flux[(size_t) j] <= v;
        if (! isMax)
            continue;
        float avg = 0.0f;
        int cnt = 0;
        for (int j = std::max (0, f - preAvg); j <= std::min (frames - 1, f + postAvg); ++j)
        {
            avg += flux[(size_t) j];
            ++cnt;
        }
        avg /= (float) std::max (1, cnt);
        if (v < avg + delta || f - last < combine)
            continue;
        float rise = -100.0f;
        for (int g = 0; g < 3; ++g)
        {
            float after = -200.0f, before = 0.0f;
            int nb = 0;
            for (int j = f - 1; j <= std::min (frames - 1, f + 2); ++j)
                after = std::max (after, E[(size_t) j][(size_t) g]);
            for (int j = std::max (0, f - 7); j <= f - 3; ++j)
            {
                before += E[(size_t) j][(size_t) g];
                ++nb;
            }
            before = nb > 0 ? before / (float) nb : -120.0f;
            rise = std::max (rise, after - before);
        }
        if (rise < riseMin)
            continue;
        // how strong: how loud it is just after
        float loud = 0.0f;
        for (int j = f; j <= std::min (frames - 1, f + (int) (0.040 * sr / hop)); ++j)
            loud = std::max (loud, level[(size_t) j]);
        picked.push_back (f);
        strength.push_back (loud);
        last = f;
    }

    // each one to the sample: where the sound starts to rise (an envelope of 0.5 ms windows), a millisecond before
    const int w = std::max (8, (int) (0.0005 * sr));
    for (size_t q = 0; q < picked.size(); ++q)
    {
        const int c = picked[q] * hop;
        const int from = std::max (0, c - N / 2 - hop), to = std::min (n, c + N / 2);
        // the envelope's peak within the frame, then back to where it was under a fifth of it
        float top = 0.0f;
        int topAt = from;
        for (int i = from; i < to; i += w)
        {
            float e = 0.0f;
            for (int j = i; j < std::min (n, i + w); ++j)
                e = std::max (e, std::abs (m[(size_t) j]));
            if (e > top)
            {
                top = e;
                topAt = i;
            }
        }
        int at = topAt;
        for (int i = topAt; i >= from; i -= w)
        {
            float e = 0.0f;
            for (int j = i; j < std::min (n, i + w); ++j)
                e = std::max (e, std::abs (m[(size_t) j]));
            at = i;
            if (e < top * 0.2f)
                break;
        }
        at = std::max (0, at - (int) (0.001 * sr));
        if (! out.empty() && at <= out.back().at + w)
            continue;
        out.push_back ({ at, strength[q] });
    }
    return out;
}

std::vector<Slice> slice (const float* l, const float* r, int n, double sr, int count, float sensitivity)
{
    std::vector<Slice> out;
    count = std::max (1, std::min (numPads, count));
    if (l == nullptr || n < 64)
        return out;
    const auto on = findOnsets (l, r, n, sr, sensitivity);
    const int longest = (int) (4.0 * sr);
    if (on.size() < 2)
    {
        // hardly any attack: cut evenly
        for (int i = 0; i < count; ++i)
        {
            const int a = (int) ((int64_t) n * i / count), b = (int) ((int64_t) n * (i + 1) / count);
            if (b - a >= 64)
                out.push_back ({ a, std::min (b, a + longest), 0.0f });
        }
        return out;
    }
    // the strongest, in the take's order
    std::vector<size_t> idx (on.size());
    std::iota (idx.begin(), idx.end(), (size_t) 0);
    std::stable_sort (idx.begin(), idx.end(), [&on] (size_t a, size_t b) { return on[a].strength > on[b].strength; });
    if ((int) idx.size() > count)
        idx.resize ((size_t) count);
    std::sort (idx.begin(), idx.end());
    for (size_t q = 0; q < idx.size(); ++q)
    {
        const size_t i = idx[q];
        const int a = on[i].at;
        const int b = i + 1 < on.size() ? on[i + 1].at : n;
        out.push_back ({ a, std::min (b, a + longest), on[i].strength });
    }
    return out;
}
}
