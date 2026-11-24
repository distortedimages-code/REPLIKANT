#include "RadioSeek.h"

#include <algorithm>
#include <cmath>

namespace radio
{
namespace
{
// each bin's floor: the 20th percentile of the bins around it (32 each side), never more than 8 dB over the whole
// stretch's own (a band full of stations lifts its floor to their sidebands: their carriers still stand out of it)
std::vector<float> floorOf (const std::vector<float>& x)
{
    const int n = (int) x.size();
    std::vector<float> f ((size_t) n), w;
    if (n == 0)
        return f;
    w = x;
    std::nth_element (w.begin(), w.begin() + n / 5, w.end());
    const float whole = w[(size_t) (n / 5)];
    const int half = 32;
    for (int i = 0; i < n; ++i)
    {
        const int a = std::max (0, i - half), b = std::min (n - 1, i + half);
        w.assign (x.begin() + a, x.begin() + b + 1);
        const auto k = w.size() / 5;
        std::nth_element (w.begin(), w.begin() + (long) k, w.end());
        f[(size_t) i] = std::min (w[k], whole + 8.0f);
    }
    return f;
}

bool localMax (const std::vector<float>& x, int i, int reach)
{
    const int n = (int) x.size();
    for (int d = 1; d <= reach; ++d)
    {
        if (i - d >= 0 && ! (x[(size_t) i] > x[(size_t) (i - d)]))
            return false;
        if (i + d < n && x[(size_t) (i + d)] > x[(size_t) i])
            return false;
    }
    return true;
}

// where the top of a peak is, between bins (a parabola through three)
double vertex (const std::vector<float>& x, int i)
{
    if (i <= 0 || i + 1 >= (int) x.size())
        return i;
    const double a = x[(size_t) (i - 1)], b = x[(size_t) i], c = x[(size_t) (i + 1)];
    const double den = a - 2.0 * b + c;
    return std::abs (den) < 1.0e-9 ? (double) i : (double) i + std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
}

// a broadcaster's place: on the grid if it's that close to it (5 kHz on shortwave; 9 or 10 kHz on medium wave)
double onGrid (double khz, double binKhz)
{
    const double tol = std::max (1.0, binKhz * 1.3);
    double best = std::round (khz * 10.0) / 10.0, err = tol;
    for (double g : khz < 1710.0 ? std::vector<double> { 9.0, 10.0 } : std::vector<double> { 5.0 })
    {
        const double at = std::round (khz / g) * g;
        if (std::abs (at - khz) <= err)
        {
            err = std::abs (at - khz);
            best = at;
        }
    }
    return best;
}
}

int seekZoom (Demod d) noexcept    { return d == Demod::am ? 5 : d == Demod::cw ? 7 : 6; }
int seekLines (Demod d) noexcept   { return d == Demod::am ? 8 : d == Demod::cw ? 16 : 30; }
int seekWindows (Demod d) noexcept { return d == Demod::am ? 12 : d == Demod::cw ? 12 : 14; }
double seekGuard (Demod d) noexcept { return d == Demod::am ? 3.0 : d == Demod::cw ? 0.4 : 2.5; }

Found nextSignal (const Panorama& p, double fromKhz, int dir, Demod d)
{
    const int n = (int) p.avg.size();
    Found best;
    if (n < 32 || (int) p.peak.size() != n || p.binKhz <= 0.0 || dir == 0)
        return best;
    const double guard = seekGuard (d);
    auto better = [&] (double khz, float snr)
    {
        if (dir > 0 ? khz <= fromKhz + guard : khz >= fromKhz - guard)
            return;
        if (best.khz <= 0.0 || (dir > 0 ? khz < best.khz : khz > best.khz))
            best = { khz, snr };
    };
    auto bins = [&] (double khz) { return std::max (1, (int) std::lround (khz / p.binKhz)); };

    if (d == Demod::am)
    {
        // the carriers: over the floor, their sidebands too, no other one as strong close by
        const auto fl = floorOf (p.avg);
        std::vector<int> tops;
        const int edge = bins (5.0) + 1;
        for (int i = 1; i + 1 < n; ++i)
            if (p.avg[(size_t) i] - fl[(size_t) i] >= 10.0f && localMax (p.avg, i, 2))
                tops.push_back (i);
        for (int i : tops)
        {
            if (i < edge || i >= n - edge)
                continue;
            const float snr = p.avg[(size_t) i] - fl[(size_t) i];
            if (snr < 15.0f)
                continue;
            // its sidebands: the median of the bins from 1.5 to 4.5 kHz each side (a bare carrier's skirt spills
            // into the nearest of them: one bin a side, the median doesn't see it)
            std::vector<float> sides;
            for (int j = std::max (0, i - bins (4.5)); j <= std::min (n - 1, i + bins (4.5)); ++j)
                if (std::abs (j - i) * p.binKhz >= 1.5)
                    sides.push_back (p.avg[(size_t) j] - fl[(size_t) j]);
            float sb = 0.0f;
            if (! sides.empty())
            {
                std::nth_element (sides.begin(), sides.begin() + (long) (sides.size() / 2), sides.end());
                sb = sides[sides.size() / 2];
            }
            if (sb < 2.0f && snr < 30.0f)
                continue;                                  // (a bare carrier, a receiver's own birdie: not a station)
            bool alone = true;
            for (int j : tops)
            {
                const double apart = std::abs (j - i) * p.binKhz;
                if (j != i && apart >= 1.5 && apart <= 6.0 && p.avg[(size_t) j] - fl[(size_t) j] >= snr - 6.0f)
                    alone = false;                         // (two stations on top of each other: they beat)
            }
            if (alone)
                better (onGrid (p.khzAt (vertex (p.avg, i)), p.binKhz), snr);
        }
        return best;
    }

    const auto fl = floorOf (p.peak);
    std::vector<float> e ((size_t) n);
    for (int i = 0; i < n; ++i)
        e[(size_t) i] = p.peak[(size_t) i] - fl[(size_t) i];

    if (d == Demod::cw)
    {
        // a narrow carrier: over the floor, the bins 700 Hz away well under it
        const int side = bins (0.7);
        for (int i = side; i < n - side; ++i)
        {
            const float snr = e[(size_t) i];
            if (snr < 14.0f || ! localMax (e, i, 1))
                continue;
            if (e[(size_t) (i - side)] > snr - 6.0f || e[(size_t) (i + side)] > snr - 6.0f)
                continue;
            better (p.khzAt (vertex (p.peak, i)) - 0.5, snr);
        }
        return best;
    }

    // a voice in a sideband: a hump about 2.4 kHz wide over the floor at its loudest moments, and moving (its
    // loudest well over its mean: a carrier stands still)
    const int box = bins (2.4);
    std::vector<float> s ((size_t) n, -100.0f);
    for (int i = 0; i + box <= n; ++i)
    {
        float sum = 0.0f, top = -100.0f, moves = 0.0f, weight = 0.0f;
        int lit = 0;
        for (int j = i; j < i + box; ++j)
        {
            sum += e[(size_t) j];
            top = std::max (top, e[(size_t) j]);
            if (e[(size_t) j] >= 3.0f)
            {
                ++lit;
                moves += e[(size_t) j] * (p.peak[(size_t) j] - p.avg[(size_t) j]);     // (the loud bins count the most)
                weight += e[(size_t) j];
            }
        }
        const float mean = sum / (float) box;
        if ((float) lit >= 0.6f * (float) box && top - mean <= 18.0f && moves >= 3.0f * weight)
            s[(size_t) i] = mean;
    }
    const int reach = std::max (1, box / 2);
    for (int i = 1; i + box < n; ++i)
    {
        if (s[(size_t) i] < 8.0f || ! localMax (s, i, reach))
            continue;
        const double mid = p.khzAt ((double) i + 0.5 * (double) (box - 1));
        const double dial = d == Demod::usb ? mid - 1.5 : mid + 1.5;
        better (std::round (dial * 10.0) / 10.0, s[(size_t) i]);
    }
    return best;
}
}
