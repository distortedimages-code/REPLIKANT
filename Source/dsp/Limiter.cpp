#include "Limiter.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    // a Catmull-Rom between p1 and p2 at f
    inline float cubic (float p0, float p1, float p2, float p3, float f)
    {
        const float c1 = 0.5f * (p2 - p0);
        const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
        return ((c3 * f + c2) * f + c1) * f + p1;
    }
}

void Limiter::prepare (double sampleRate)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    look = std::max (8, (int) std::lround (0.0015 * sr));
    // (the sound waits the window and two frames; the smallest is taken over the window and three: a peak and the
    // ones between its neighbours are all inside every minimum the average takes for it)
    dl.assign ((size_t) look + 2, 0.0f);
    dr.assign ((size_t) look + 2, 0.0f);
    need.assign ((size_t) look + 3, 1.0f);
    minQ.assign ((size_t) look + 4, 0);
    avg.assign ((size_t) look, 1.0f);
    reset();
}

void Limiter::reset()
{
    std::fill (dl.begin(), dl.end(), 0.0f);
    std::fill (dr.begin(), dr.end(), 0.0f);
    std::fill (need.begin(), need.end(), 1.0f);
    std::fill (avg.begin(), avg.end(), 1.0f);
    avgSum = (double) look;
    qHead = qTail = 0;
    pos = 0;
    t = 0;
    held = 1.0f;
    for (auto& p : prevL) p = 0.0f;
    for (auto& p : prevR) p = 0.0f;
    driveG = dbToGain (std::clamp (target.driveDb, 0.0f, 12.0f));
    ceilG = dbToGain (std::clamp (target.ceilingDb, -12.0f, 0.0f));
    onG = target.on ? 1.0f : 0.0f;
}

void Limiter::process (float* L, float* R, int n)
{
    if (dl.empty() || n <= 0)
        return;
    const auto& s = target;
    const float kGlide = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    const float driveAim = dbToGain (std::clamp (s.driveDb, 0.0f, 12.0f));
    const float ceilAim = dbToGain (std::clamp (s.ceilingDb, -12.0f, 0.0f));
    const float rel = 1.0f - std::exp (-1.0f / (std::clamp (s.releaseMs, 10.0f, 1000.0f) * 0.001f * (float) sr));
    const int W = look, M = (int) need.size(), D = (int) dl.size(), Q = (int) minQ.size();
    float most = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        driveG += (driveAim - driveG) * kGlide;
        ceilG += (ceilAim - ceilG) * kGlide;
        onG += ((s.on ? 1.0f : 0.0f) - onG) * kGlide;
        const float push = 1.0f + onG * (driveG - 1.0f);
        const float xl = L[i] * push, xr = R[i] * push;
        // the peak: this sample, and between the last two (three points on the cubic through four)
        float peak = std::max (std::abs (xl), std::abs (xr));
        for (float f : { 0.25f, 0.5f, 0.75f })
        {
            peak = std::max (peak, std::abs (cubic (prevL[0], prevL[1], prevL[2], xl, f)));
            peak = std::max (peak, std::abs (cubic (prevR[0], prevR[1], prevR[2], xr, f)));
        }
        prevL[0] = prevL[1]; prevL[1] = prevL[2]; prevL[2] = xl;
        prevR[0] = prevR[1]; prevR[1] = prevR[2]; prevR[2] = xr;
        const float g0 = peak > ceilG ? ceilG / peak : 1.0f;
        // the smallest needed over the window (a queue of the candidates, oldest first)
        const long long span = (long long) M * 1024;           // (positions kept modulo M * 1024)
        need[(size_t) (t % M)] = g0;
        const int now = (int) (t % span);
        while (qTail != qHead && need[(size_t) (minQ[(size_t) ((qTail - 1 + Q) % Q)] % M)] >= g0)
            qTail = (qTail - 1 + Q) % Q;
        minQ[(size_t) qTail] = now;
        qTail = (qTail + 1) % Q;
        auto age = [now, span] (int at) { long long d = (long long) now - at; if (d < 0) d += span; return d; };
        while (qHead != qTail && age (minQ[(size_t) qHead]) >= M)
            qHead = (qHead + 1) % Q;
        const float lowest = qHead != qTail ? need[(size_t) (minQ[(size_t) qHead] % M)] : 1.0f;
        const int slot = (int) (t % W);
        // the release, then the average over the window
        held = lowest < held ? lowest : held + (lowest - held) * rel;
        avgSum += (double) held - (double) avg[(size_t) slot];
        avg[(size_t) slot] = held;
        const float g = std::min (1.0f, (float) (avgSum / (double) W));
        most = std::min (most, g);
        ++t;
        // the sound, the window late, at that gain (off: as it was, only late)
        const float ol = dl[(size_t) pos], orr = dr[(size_t) pos];
        dl[(size_t) pos] = xl;
        dr[(size_t) pos] = xr;
        pos = (pos + 1) % D;
        const float ge = 1.0f + onG * (g - 1.0f);
        float yl = ol * ge, yr = orr * ge;
        if (onG > 0.999f)
        {
            yl = std::clamp (yl, -ceilG, ceilG);
            yr = std::clamp (yr, -ceilG, ceilG);
        }
        L[i] = std::isfinite (yl) ? yl : 0.0f;
        R[i] = std::isfinite (yr) ? yr : 0.0f;
    }
    shownReduction.store (s.on ? -20.0f * std::log10 (std::max (1.0e-6f, most)) : 0.0f, std::memory_order_relaxed);
}
}
