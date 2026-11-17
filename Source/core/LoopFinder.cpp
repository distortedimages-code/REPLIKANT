#include "LoopFinder.h"
#include "Analysis.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace rk
{
double seamDistance (const std::vector<float>& m, int s, int e, int W)
{
    const int half = std::max (1, W / 2);
    const int len = (int) m.size();
    if (s - half < 0 || e - half < 0 || s + half >= len || e + half >= len)
        return 2.0;
    double d = 0.0, en = 0.0;
    const float* a = m.data() + s - half;
    const float* b = m.data() + e - half;
    for (int i = 0; i < 2 * half; ++i)
    {
        const double x = a[i], y = b[i];
        d += (x - y) * (x - y);
        en += x * x + y * y;
    }
    return en > 1.0e-20 ? d / en : 0.0;
}

namespace
{
    struct Candidate
    {
        int s, e;
        double score;
    };

    // Search on a decimated copy first (about 11 kHz), then around the best few at full rate.
    struct Searcher
    {
        const std::vector<float>& m;
        int q = 1;
        std::vector<float> md;
        std::vector<double> cum;     // running sum of squares at full rate
        int levelWindow = 1;
        double lengthBonus = 0.08;   // per unit of the longest possible loop

        Searcher (const std::vector<float>& mono, double sr) : m (mono)
        {
            q = std::max (1, (int) std::lround (sr / 11025.0));
            md.resize (m.size() / (size_t) q);
            for (size_t i = 0; i < md.size(); ++i)
            {
                float s = 0.0f;
                for (int k = 0; k < q; ++k)
                    s += m[i * (size_t) q + (size_t) k];
                md[i] = s / (float) q;
            }
            cum.assign (m.size() + 1, 0.0);
            for (size_t i = 0; i < m.size(); ++i)
                cum[i + 1] = cum[i] + (double) m[i] * m[i];
            levelWindow = std::max (32, (int) (sr * 0.05));
        }

        double rmsAt (int pos) const
        {
            const int len = (int) m.size();
            const int a = std::clamp (pos - levelWindow / 2, 0, len), b = std::clamp (pos + levelWindow / 2, 0, len);
            return b > a ? std::sqrt ((cum[(size_t) b] - cum[(size_t) a]) / (double) (b - a)) : 0.0;
        }

        double levelDiff (int s, int e) const
        {
            const double x = rmsAt (s), y = rmsAt (e);
            return x + y > 1.0e-12 ? std::abs (x - y) / (x + y) : 0.0;
        }

        double score (const std::vector<float>& sig, int s, int e, int W, int fullS, int fullE, double span) const
        {
            const double seam = seamDistance (sig, s, e, W);
            const double level = levelDiff (fullS, fullE);
            return seam + 2.5 * level - lengthBonus * (double) (fullE - fullS) / std::max (1.0, span);
        }

        // ranges at full rate; `lengthOk` filters loop lengths (full-rate samples)
        Loop search (int sLo, int sHi, int eLo, int eHi, int minLength, int Wfull, double span,
                     const std::function<bool (int)>& lengthOk, int eCandidates) const
        {
            Loop best;
            const int W = std::max (8, Wfull / q);
            const int sL = sLo / q, sH = sHi / q, eL = eLo / q, eH = eHi / q;
            if (sH < sL || eH < eL)
                return best;
            const int eStep = std::max (1, (eH - eL) / std::max (1, eCandidates));
            const int sStep = (sH - sL) > 40000 ? 2 : 1;
            std::vector<Candidate> top;
            const size_t keep = 6;
            for (int e = eH; e >= eL; e -= eStep)
            {
                for (int s = sL; s <= sH; s += sStep)
                {
                    const int L = (e - s) * q;
                    if (L < minLength || (lengthOk && ! lengthOk (L)))
                        continue;
                    const double sc = score (md, s, e, W, s * q, e * q, span);
                    if (top.size() < keep || sc < top.back().score)
                    {
                        // keep the list distinct: a neighbour of a kept candidate replaces it only when better
                        bool merged = false;
                        for (auto& c : top)
                            if (std::abs (c.s - s) < W && std::abs (c.e - e) < W)
                            {
                                if (sc < c.score)
                                    c = { s, e, sc };
                                merged = true;
                                break;
                            }
                        if (! merged)
                            top.push_back ({ s, e, sc });
                        std::sort (top.begin(), top.end(), [] (auto& x, auto& y) { return x.score < y.score; });
                        if (top.size() > keep)
                            top.resize (keep);
                    }
                }
            }
            double bestScore = 1.0e9;
            const int r = 2 * q;
            for (auto& c : top)
            {
                const int s0 = c.s * q, e0 = c.e * q;
                for (int e = e0 - r; e <= e0 + r; ++e)
                    for (int s = s0 - r; s <= s0 + r; ++s)
                    {
                        if (e - s < minLength || s < sLo || e > eHi)
                            continue;
                        const double sc = score (m, s, e, Wfull, s, e, span);
                        if (sc < bestScore)
                        {
                            bestScore = sc;
                            best.start = s;
                            best.end = e;
                        }
                    }
            }
            if (best.valid())
                best.quality = (float) std::clamp (1.0 - seamDistance (m, best.start, best.end, Wfull), 0.0, 1.0);
            return best;
        }
    };

    // the period of a slow modulation of the level (seconds), 0 when there isn't a clear one
    double modulationPeriod (const std::vector<float>& m, double sr, int a, int b)
    {
        const int hop = std::max (1, (int) (sr * 0.005));
        std::vector<float> part (m.begin() + a, m.begin() + b);
        auto env = rmsEnvelope (part, hop, hop * 8);
        const int n = (int) env.size();
        if (n < 80)
            return 0.0;
        double mean = 0.0;
        for (float v : env)
            mean += v;
        mean /= n;
        if (mean <= 1.0e-9)
            return 0.0;
        // remove the slow trend (a fade, a decay) with a straight line
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = 0; i < n; ++i)
        {
            sx += i;
            sy += env[(size_t) i];
            sxx += (double) i * i;
            sxy += (double) i * env[(size_t) i];
        }
        const double slope = (n * sxy - sx * sy) / std::max (1.0e-12, n * sxx - sx * sx);
        const double icpt = (sy - slope * sx) / n;
        std::vector<double> d ((size_t) n);
        double var = 0.0;
        for (int i = 0; i < n; ++i)
        {
            d[(size_t) i] = env[(size_t) i] - (icpt + slope * i);
            var += d[(size_t) i] * d[(size_t) i];
        }
        const double depth = std::sqrt (var / n) / mean;
        if (depth < 0.02)
            return 0.0;
        const int lagMin = std::max (2, (int) (0.10 / 0.005)), lagMax = n / 2;
        std::vector<double> r ((size_t) lagMax + 2, 0.0);
        for (int lag = lagMin - 1; lag <= lagMax + 1 && lag < n; ++lag)
        {
            double xy = 0, xx = 0, yy = 0;
            for (int i = 0; i + lag < n; ++i)
            {
                xy += d[(size_t) i] * d[(size_t) i + (size_t) lag];
                xx += d[(size_t) i] * d[(size_t) i];
                yy += d[(size_t) i + (size_t) lag] * d[(size_t) i + (size_t) lag];
            }
            r[(size_t) lag] = xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : 0.0;
        }
        // past the first dip (the envelope stops resembling itself), the strongest return; the first peak
        // close to it is the period (not one of its multiples)
        int lag = lagMin;
        while (lag <= lagMax && r[(size_t) lag] > 0.2)
            ++lag;
        double best = 0.0;
        for (int l = lag; l <= lagMax; ++l)
            best = std::max (best, r[(size_t) l]);
        if (best < 0.55)
            return 0.0;
        for (int l = lag; l <= lagMax; ++l)
            if (r[(size_t) l] >= 0.9 * best)
            {
                // the top of that hump (ripple can put a small bump on its rising side), refined between steps
                int top = l;
                for (int k = l; k <= std::min (lagMax, l + l / 5); ++k)
                    if (r[(size_t) k] > r[(size_t) top])
                        top = k;
                double off = 0.0;
                const double y0 = r[(size_t) top - 1], y1 = r[(size_t) top], y2 = r[(size_t) top + 1];
                const double den = y0 - 2.0 * y1 + y2;
                if (std::abs (den) > 1.0e-12)
                    off = std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
                return (top + off) * 0.005;
            }
        return 0.0;
    }
}

LoopReport findLoop (const Channels& ch, double sr, int h0, int h1, double f0, const LoopSettings& set)
{
    LoopReport rep;
    if (ch.empty() || ch[0].empty())
        return rep;
    const auto m = mixDown (ch);
    const int len = (int) m.size();
    h0 = std::clamp (h0, 0, len);
    h1 = std::clamp (h1, h0, len);
    const int held = h1 - h0;
    int a = h0 + (int) (set.searchStart * held);
    int b = h0 + (int) (set.searchEnd * held);
    const int C = (int) (set.crossfadeSeconds * sr);
    a = std::max (a, std::min (C, b / 3));                   // room for the crossfade before the loop start
    b = std::min (b, len - (int) (sr * 0.01) - 1);
    int minL = (int) (set.minSeconds * sr);
    if (b - a < 64)
        return rep;
    if (b - a < minL * 5 / 4)
        minL = (b - a) * 3 / 5;

    // pitch
    if (f0 > 0.0)
    {
        const auto p = detectPitch (m, sr, f0, a, b);
        if (p.ok)
            rep.periodSamples = sr / p.hz;
    }
    const int Wfull = std::clamp (rep.periodSamples > 0 ? (int) (rep.periodSamples * 2.5) : (int) (sr * 0.006),
                                  (int) (sr * 0.004), (int) (sr * 0.04));

    // slow modulation
    rep.modulationSeconds = modulationPeriod (m, sr, a, b);
    std::function<bool (int)> lengthOk;
    if (rep.modulationSeconds > 0.0)
    {
        const double M = rep.modulationSeconds * sr;
        lengthOk = [M] (int L)
        {
            const double k = std::round ((double) L / M);
            return k >= 1.0 && std::abs ((double) L - k * M) <= 0.04 * M;
        };
    }

    Searcher search (m, sr);
    const int span = b - a;
    // the loop starts in the first half of the window and ends in its last third
    const int sHi = a + span / 2;
    const int eLo = b - std::min (span / 3, (int) (sr * 0.6));
    rep.loop = search.search (a, sHi, eLo, b, minL, Wfull, (double) span, lengthOk, 48);
    if (! rep.loop.valid() && lengthOk)        // the modulation filter left nothing: search freely
    {
        rep.modulationSeconds = 0.0;
        rep.loop = search.search (a, sHi, eLo, b, minL, Wfull, (double) span, {}, 48);
    }
    return rep;
}

Loop refineLoop (const Channels& ch, double sr, Loop approx, int radius, double f0)
{
    if (! approx.valid() || ch.empty())
        return approx;
    const auto m = mixDown (ch);
    const int len = (int) m.size();
    int Wfull = f0 > 0 ? (int) (2.5 * sr / f0) : (int) (sr * 0.006);
    Wfull = std::clamp (Wfull, (int) (sr * 0.004), (int) (sr * 0.04));
    const int sLo = std::max (Wfull, approx.start - radius), sHi = std::min (len - Wfull - 1, approx.start + radius);
    const int eLo = std::max (sHi + 2, approx.end - radius), eHi = std::min (len - Wfull - 1, approx.end + radius);
    Searcher search (m, sr);
    search.lengthBonus = 0.0;
    auto l = search.search (sLo, sHi, eLo, eHi, std::max (2, approx.length() / 2), Wfull, (double) approx.length(), {}, 40);
    return l.valid() ? l : approx;
}

void bakeCrossfade (Channels& ch, Loop& loop, int C)
{
    if (! loop.valid() || ch.empty())
        return;
    const int len = (int) ch[0].size();
    if (loop.end > len)
        return;
    C = std::min ({ C, loop.length() / 2, loop.start });
    if (C < 8)
    {
        loop.crossfade = 0;
        return;
    }
    const int s = loop.start, e = loop.end;
    double xy = 0, xx = 0, yy = 0;
    for (auto& c : ch)
        for (int i = 0; i < C; ++i)
        {
            const double A = c[(size_t) (e - C + i)], B = c[(size_t) (s - C + i)];
            xy += A * B;
            xx += A * A;
            yy += B * B;
        }
    const double rho = xx > 0 && yy > 0 ? std::clamp (xy / std::sqrt (xx * yy), -0.3, 1.0) : 0.0;
    for (auto& c : ch)
        for (int i = 0; i < C; ++i)
        {
            const double t = ((double) i + 0.5) / (double) C;
            const double ga = std::cos (t * 1.5707963267948966), gb = std::sin (t * 1.5707963267948966);
            const double norm = 1.0 / std::sqrt (std::max (0.25, 1.0 + 2.0 * rho * ga * gb));
            const double A = c[(size_t) (e - C + i)], B = c[(size_t) (s - C + i)];
            c[(size_t) (e - C + i)] = (float) ((ga * A + gb * B) * norm);
        }
    loop.crossfade = C;
}
}
