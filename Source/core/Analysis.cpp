#include "Analysis.h"

#include "signalsmith-linear/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace rk
{
NoiseFloor measureNoise (const Channels& ch)
{
    NoiseFloor n;
    if (ch.empty() || ch[0].empty())
        return n;
    const size_t len = ch[0].size();
    double sq = 0.0;
    for (auto& c : ch)
    {
        double sum = 0.0;
        for (float v : c)
            sum += v;
        const float dc = (float) (sum / (double) len);
        n.dc.push_back (dc);
        for (float v : c)
        {
            const float x = v - dc;
            sq += (double) x * x;
            n.peak = std::max (n.peak, std::abs (x));
        }
    }
    n.rms = (float) std::sqrt (sq / (double) (len * ch.size()));
    return n;
}

std::vector<float> rmsEnvelope (const std::vector<float>& m, int hop, int window)
{
    std::vector<float> env;
    if (m.empty() || hop <= 0)
        return env;
    const int len = (int) m.size();
    // running sum of squares
    std::vector<double> cum ((size_t) len + 1, 0.0);
    for (int i = 0; i < len; ++i)
        cum[(size_t) i + 1] = cum[(size_t) i] + (double) m[(size_t) i] * m[(size_t) i];
    for (int c = 0; c < len; c += hop)
    {
        const int a = std::max (0, c - window / 2), b = std::min (len, c + window / 2 + 1);
        env.push_back ((float) std::sqrt ((cum[(size_t) b] - cum[(size_t) a]) / (double) std::max (1, b - a)));
    }
    return env;
}

namespace
{
    float absMax (const Channels& ch, int i)
    {
        float m = 0.0f;
        for (auto& c : ch)
            m = std::max (m, std::abs (c[(size_t) i]));
        return m;
    }
}

int findOnset (const Channels& ch, int from, float threshold, float floor, int lookback)
{
    if (ch.empty())
        return from;
    const int len = (int) ch[0].size();
    from = std::clamp (from, 0, len);
    // the loudest channel, sample by sample
    auto level = [&] (int i) { return absMax (ch, i); };
    // RMS of that over [i, i + W)
    const int W = 24;
    auto rms = [&] (int i)
    {
        double s = 0.0;
        const int b = std::min (len, i + W);
        for (int k = std::max (0, i); k < b; ++k)
            s += (double) level (k) * level (k);
        return (float) std::sqrt (s / (double) std::max (1, b - std::max (0, i)));
    };
    int hit = -1;
    for (int i = from; i < len; ++i)
        if (level (i) > threshold && rms (i) > threshold * 0.3f)   // a lone click of noise doesn't count
        {
            hit = i;
            break;
        }
    if (hit < 0)
        return from;
    // walk back to where the sound leaves the floor: the first window (going back) as quiet as the noise
    const float quiet = std::max (floor * 2.0f, threshold * 0.02f);
    const int limit = std::max (from, hit - lookback);
    for (int i = hit; i > limit; --i)
        if (rms (i - W) < quiet)
            return std::max (limit, i - W / 2);
    return limit;
}

int findTailEnd (const Channels& ch, int from, float floor, double sr)
{
    if (ch.empty())
        return from;
    const auto m = mixDown (ch);
    const int len = (int) m.size();
    const int win = std::max (16, (int) (sr * 0.010));
    const float limit = std::max (floor * 2.0f, 1.0e-6f);
    int end = from;
    for (int a = std::max (0, from); a < len; a += win)
    {
        const int b = std::min (len, a + win);
        double sq = 0.0;
        for (int i = a; i < b; ++i)
        {
            // the loudest channel counts, not the mix (a sound panned hard to one side)
            float v = 0.0f;
            for (auto& c : ch)
                v = std::max (v, std::abs (c[(size_t) i]));
            sq += (double) v * v;
        }
        if (std::sqrt (sq / (double) (b - a)) > limit)
            end = b;
    }
    return std::max (from, end);
}

namespace
{
    // The normalised squared difference between the window and itself `t` samples later, for t in [tMin, tMax]
    // (as YIN's, but normalised by the energy of both windows, so only the lags searched are computed):
    // 0 = perfectly periodic at that lag. Best lag (refined between samples) and its value.
    bool periodWindow (const std::vector<float>& m, int start, int W, int tMin, int tMax, double& lag, float& value)
    {
        if (start < 0 || start + W + tMax + 2 >= (int) m.size() || tMin < 2)
            return false;
        std::vector<double> dn ((size_t) tMax + 2, 1.0);
        for (int t = tMin - 1; t <= tMax + 1; ++t)
        {
            double r = 0.0, e = 0.0;
            const float* a = m.data() + start;
            const float* b = a + t;
            for (int j = 0; j < W; ++j)
            {
                r += (double) a[j] * (double) b[j];
                e += (double) a[j] * (double) a[j] + (double) b[j] * (double) b[j];
            }
            dn[(size_t) t] = e > 1.0e-20 ? (e - 2.0 * r) / e : 1.0;
        }
        int best = tMin;
        for (int t = tMin; t <= tMax; ++t)
            if (dn[(size_t) t] < dn[(size_t) best])
                best = t;
        double off = 0.0;
        const double y0 = dn[(size_t) best - 1], y1 = dn[(size_t) best], y2 = dn[(size_t) best + 1];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::abs (den) > 1.0e-12)
            off = std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
        lag = (double) best + off;
        value = (float) std::max (0.0, y1 - 0.25 * (y0 - y2) * off);
        return true;
    }
}

Pitch detectPitch (const std::vector<float>& m, double sr, double expectedHz, int from, int to)
{
    Pitch best;
    if (expectedHz <= 0.0 || m.empty())
        return best;
    to = std::min (to, (int) m.size());
    from = std::max (0, from);
    float bestMeanV = 2.0f;
    // the period of the note, or two or four of them: a sub-oscillator an octave or two down makes the true
    // period longer (a patch an octave up is periodic at the note's period anyway)
    for (int k : { 1, 2, 4 })
    {
        const double period = k * sr / expectedHz;
        const int tMin = std::max (2, (int) std::floor (period / 1.0595));
        const int tMax = (int) std::ceil (period * 1.0595);
        const int W = std::max ((int) (sr * 0.02), (int) (period * 3.0));
        const int span = to - from - W - tMax - 4;
        if (span <= 0)
            break;
        std::vector<double> lags;
        std::vector<float> values;
        const int windows = 7;
        for (int w = 0; w < windows; ++w)
        {
            const int start = from + (int) ((double) span * (w + 0.5) / windows);
            double lag;
            float v;
            if (periodWindow (m, start, W, tMin, tMax, lag, v) && v < 0.25f)
            {
                lags.push_back (lag);
                values.push_back (v);
            }
        }
        if (lags.size() < 3)
            continue;
        auto sorted = lags;
        std::sort (sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        // the windows have to agree (a few cents): an unstable pitch (vibrato, a noisy sound) isn't corrected
        double spreadCents = 0.0;
        for (double l : lags)
            spreadCents = std::max (spreadCents, std::abs (1200.0 * std::log2 (l / median)));
        float meanV = 0.0f;
        for (float v : values)
            meanV += v;
        meanV /= (float) values.size();
        if (meanV >= bestMeanV * 0.8f)    // a longer period has to be clearly better to be taken
            continue;
        bestMeanV = meanV;
        // the average period over the whole held part, in one long window: detuned oscillators and slow drift
        // average out to the pitch heard (the short windows above can lock onto one oscillator or the other)
        double lagAll = median;
        {
            const int longW = std::min ((int) (sr * 1.5), to - from - tMax - 4);
            const int lo = std::max (tMin, (int) std::floor (median * 0.99)), hi = std::min (tMax, (int) std::ceil (median * 1.01));
            double lag;
            float v;
            if (longW > W && hi > lo && periodWindow (m, from, longW, lo, hi, lag, v) && v < 0.5f)
                lagAll = lag;
        }
        Pitch p;
        p.hz = k * sr / lagAll;            // as a frequency of the note itself
        p.cents = 1200.0 * std::log2 (p.hz / expectedHz);
        p.confidence = std::clamp (1.0f - meanV * 4.0f, 0.0f, 1.0f) * (spreadCents < 10.0 ? 1.0f : 0.3f);
        p.ok = p.confidence > 0.5f && std::abs (p.cents) < 60.0;
        best = p;
        if (meanV < 0.02f)
            break;                          // clearly periodic already
    }
    return best;
}

Pitch detectPitchWide (const std::vector<float>& m, double sr, double sentHz, int from, int to)
{
    Pitch none;
    if (sentHz <= 0.0 || m.empty())
        return none;
    to = std::min (to, (int) m.size());
    from = std::max (0, from);
    const double P = sr / sentHz;
    const int minLag = std::max (2, (int) std::floor (P * 0.69));       // half an octave up (and a little)
    int maxLag = (int) std::ceil (P * 4.4);                             // half an octave down, two octaves under (subs)
    const int avail = to - from;
    if (avail < (int) (P * 3.0) || avail < 256)
        return detectPitch (m, sr, sentHz, from, to);
    const int W = std::min (avail, std::max ((int) (sr * 0.6), 6 * maxLag));
    maxLag = std::min (maxLag, W / 2);
    if (maxLag <= minLag + 2)
        return detectPitch (m, sr, sentHz, from, to);
    const int start = from + (avail - W) / 2;

    // autocorrelation by FFT (zero-padded: linear, not circular)
    int N = 1;
    while (N < 2 * W)
        N <<= 1;
    signalsmith::linear::Pow2FFT<float> fft ((size_t) N);
    std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);
    double mean = 0.0;
    for (int i = 0; i < W; ++i)
        mean += m[(size_t) (start + i)];
    mean /= W;
    for (int i = 0; i < W; ++i)
        a[(size_t) i] = (float) (m[(size_t) (start + i)] - mean);
    fft.fft (a.data(), b.data());
    for (auto& v : b)
        v = std::complex<float> (std::norm (v), 0.0f);
    fft.ifft (b.data(), a.data());
    std::vector<double> cum ((size_t) W + 1, 0.0);
    for (int i = 0; i < W; ++i)
    {
        const double x = m[(size_t) (start + i)] - mean;
        cum[(size_t) i + 1] = cum[(size_t) i] + x * x;
    }
    auto nsdf = [&] (int t)
    {
        const double e = cum[(size_t) (W - t)] + (cum[(size_t) W] - cum[(size_t) t]);
        return e > 1.0e-20 ? 2.0 * (double) a[(size_t) t].real() / (double) N / e : 0.0;
    };
    // the key maxima: the highest point of each positive lobe after the first negative stretch
    std::vector<std::pair<int, double>> keys;
    int t = 1;
    while (t <= maxLag && nsdf (t) > 0.0)
        ++t;
    int bestT = -1;
    double bestV = -1.0;
    for (; t <= maxLag + 1; ++t)
    {
        const double v = t <= maxLag ? nsdf (t) : -1.0;
        if (v > 0.0)
        {
            if (v > bestV)
                bestV = v, bestT = t;
        }
        else if (bestT >= 0)
        {
            if (bestT >= minLag)
                keys.push_back ({ bestT, bestV });
            bestT = -1;
            bestV = -1.0;
        }
    }
    double top = 0.0;
    for (auto& k : keys)
        top = std::max (top, k.second);
    if (keys.empty() || top < 0.4)
        return detectPitch (m, sr, sentHz, from, to);
    int lag = keys.front().first;
    for (auto& k : keys)
        if (k.second >= 0.85 * top)
        {
            lag = k.first;
            break;
        }
    double off = 0.0;
    {
        const double y0 = nsdf (lag - 1), y1 = nsdf (lag), y2 = nsdf (lag + 1), den = y0 - 2.0 * y1 + y2;
        if (std::abs (den) > 1.0e-12)
            off = std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
    }
    // its pitch class against the note sent, in the octave closest to it
    double semis = 12.0 * std::log2 (P / ((double) lag + off));
    semis -= 12.0 * std::round (semis / 12.0);
    // refined around there (the narrow search also sees a sub an octave or two down)
    auto p = detectPitch (m, sr, sentHz * std::pow (2.0, semis / 12.0), from, to);
    if (! p.ok)
        return p;
    p.cents = 1200.0 * std::log2 (p.hz / sentHz);
    p.cents -= 1200.0 * std::round (p.cents / 1200.0);   // (the refined pitch's octave folded back next to the note sent)
    p.ok = std::abs (p.cents) <= 660.0;
    return p;
}

void fadeIn (Channels& ch, int n)
{
    for (auto& c : ch)
    {
        const int len = std::min (n, (int) c.size());
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) (i + 1) / (float) (len + 1);
            c[(size_t) i] *= 0.5f - 0.5f * std::cos (t * 3.14159265f);
        }
    }
}

void fadeOutEnd (Channels& ch, int n)
{
    for (auto& c : ch)
    {
        const int len = (int) c.size();
        const int k = std::min (n, len);
        for (int i = 0; i < k; ++i)
        {
            const float t = (float) (i + 1) / (float) (k + 1);
            c[(size_t) (len - 1 - i)] *= 0.5f - 0.5f * std::cos (t * 3.14159265f);
        }
    }
}

void removeDc (Channels& ch, const std::vector<float>& dc)
{
    for (size_t c = 0; c < ch.size() && c < dc.size(); ++c)
        for (auto& v : ch[c])
            v -= dc[c];
}
}
