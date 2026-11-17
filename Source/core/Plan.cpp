#include "Plan.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace rk
{
std::vector<int> plannedNotes (const Session& s)
{
    const int lo = std::clamp (std::min (s.lowNote, s.highNote), 0, 127);
    const int hi = std::clamp (std::max (s.lowNote, s.highNote), 0, 127);
    std::set<int> notes;
    switch (s.noteMode)
    {
        case NoteMode::interval:
        {
            const int step = std::clamp (s.interval, 1, 48);
            for (int n = lo; n <= hi; n += step)
                notes.insert (n);
            break;
        }
        case NoteMode::count:
        {
            const int count = std::clamp (s.noteCount, 1, 128);
            if (count == 1 || hi == lo)
                notes.insert ((lo + hi) / 2);
            else
                for (int i = 0; i < count; ++i)
                    notes.insert ((int) std::lround (lo + (double) i * (hi - lo) / (count - 1)));
            break;
        }
        case NoteMode::custom:
            for (int n : s.customNotes)
                if (n >= 0 && n <= 127)
                    notes.insert (n);
            break;
    }
    return { notes.begin(), notes.end() };
}

std::vector<int> plannedVelocities (const Session& s)
{
    std::vector<int> v;
    if (s.curve == VelCurve::custom && ! s.customVelocities.empty())
    {
        std::set<int> u;
        for (int x : s.customVelocities)
            u.insert (std::clamp (x, 1, 127));
        return { u.begin(), u.end() };
    }
    const int n = std::clamp (s.layers, 1, 16);
    const double gamma = s.curve == VelCurve::soft ? 1.6 : s.curve == VelCurve::hard ? 0.6 : 1.0;
    for (int i = 0; i < n; ++i)
        v.push_back (std::clamp ((int) std::lround (127.0 * std::pow ((double) (i + 1) / n, gamma)), 1, 127));
    // strictly rising (low layers can collide with a steep curve)
    for (int i = 1; i < n; ++i)
        if (v[(size_t) i] <= v[(size_t) i - 1])
            v[(size_t) i] = v[(size_t) i - 1] + 1;
    for (int i = n - 1; i >= 0; --i)
        if (v[(size_t) i] > 127 - (n - 1 - i))
            v[(size_t) i] = 127 - (n - 1 - i);
    return v;
}

std::vector<Job> planJobs (const Session& s)
{
    std::vector<Job> jobs;
    const auto notes = plannedNotes (s);
    const auto vels = plannedVelocities (s);
    const int rr = std::clamp (s.roundRobins, 1, 8);
    for (int n : notes)
        for (size_t l = 0; l < vels.size(); ++l)
            for (int r = 0; r < rr; ++r)
                jobs.push_back ({ n, vels[l], (int) l, r });
    return jobs;
}

double estimatedSeconds (const Session& s)
{
    const double perJob = s.hold + std::min (s.releaseMax, 1.0 + s.releaseMax * 0.4) + s.pause + 0.05;
    return 1.0 + (double) planJobs (s).size() * perJob;
}

SizeEstimate estimateSize (const Session& s, double sr, int channels)
{
    SizeEstimate e;
    const auto jobs = planJobs (s);
    e.recordings = (int) jobs.size();
    if (e.recordings == 0 || sr <= 0.0)
        return e;
    channels = std::clamp (channels, 1, 2);
    // as the recorder holds them (ONE-SHOT: short holds, the rest is its tail)
    const double hold = s.oneShot ? std::min (s.hold, 0.5) : s.hold;
    const double tailMax = s.oneShot ? std::max (s.releaseMax, s.hold) : s.releaseMax;
    const double tail = std::min (tailMax, 1.0 + tailMax * 0.4);
    const double each = s.preMs * 0.001 + hold + tail;
    e.secondsOfAudio = each * e.recordings;
    const double frames = e.secondsOfAudio * sr * channels;
    e.diskBytes = (int64_t) (frames * (s.fileBits == 32 ? 4.0 : 3.0)) + (int64_t) e.recordings * 200 + 4096;
    // in memory: floats, the octave copies (1/2 + 1/4 + 1/8), then each key of each zone rendered
    double memory = frames * 4.0 * 1.875;
    if (s.renderKeys)
    {
        const auto notes = plannedNotes (s);
        if (! notes.empty())
        {
            const int lo = std::clamp (*std::min_element (notes.begin(), notes.end()) - s.extend, 0, 127);
            const int hi = std::clamp (*std::max_element (notes.begin(), notes.end()) + s.extend, 0, 127);
            const int layers = (int) plannedVelocities (s).size();
            e.renderedKeys = std::max (0, (hi - lo + 1) * layers * std::max (1, s.roundRobins) - e.recordings);
            memory += (double) e.renderedKeys * each * sr * channels * 4.0;
        }
    }
    e.memoryBytes = (int64_t) memory;
    return e;
}

std::vector<Zone> keyZones (const std::vector<int>& rootsIn, Placement p, int kLo, int kHi)
{
    std::vector<int> R (rootsIn);
    std::sort (R.begin(), R.end());
    R.erase (std::unique (R.begin(), R.end()), R.end());
    std::vector<Zone> z;
    const int n = (int) R.size();
    if (n == 0)
        return z;
    kLo = std::clamp (std::min (kLo, R.front()), 0, 127);
    kHi = std::clamp (std::max (kHi, R.back()), 0, 127);
    for (int i = 0; i < n; ++i)
    {
        Zone zone {};
        switch (p)
        {
            case Placement::centred:
                zone.lo = i == 0 ? kLo : (R[(size_t) i - 1] + R[(size_t) i]) / 2 + 1;
                zone.hi = i == n - 1 ? kHi : (R[(size_t) i] + R[(size_t) i + 1]) / 2;
                break;
            case Placement::up:
                zone.lo = i == 0 ? kLo : R[(size_t) i];
                zone.hi = i == n - 1 ? kHi : R[(size_t) i + 1] - 1;
                break;
            case Placement::down:
                zone.lo = i == 0 ? kLo : R[(size_t) i - 1] + 1;
                zone.hi = i == n - 1 ? kHi : R[(size_t) i];
                break;
        }
        z.push_back (zone);
    }
    return z;
}

std::vector<Zone> velocityZones (const std::vector<int>& vIn)
{
    std::vector<int> v (vIn);
    std::sort (v.begin(), v.end());
    v.erase (std::unique (v.begin(), v.end()), v.end());
    std::vector<Zone> z;
    for (size_t i = 0; i < v.size(); ++i)
        z.push_back ({ i == 0 ? 1 : v[i - 1] + 1, i + 1 == v.size() ? 127 : v[i] });
    return z;
}

int mappedRoot (const Sample& x, const Session& s)
{
    return s.tuneOn() || x.manualTune || x.sent < 0 ? x.root : x.sent;
}

void assignZones (std::vector<Sample>& samples, const Session& s)
{
    std::vector<int> roots, vels;
    for (auto& x : samples)
    {
        roots.push_back (mappedRoot (x, s));
        vels.push_back (x.velocity);
    }
    std::sort (roots.begin(), roots.end());
    roots.erase (std::unique (roots.begin(), roots.end()), roots.end());
    std::sort (vels.begin(), vels.end());
    vels.erase (std::unique (vels.begin(), vels.end()), vels.end());
    if (roots.empty())
        return;
    const int lo = std::min (s.lowNote, roots.front()) - std::max (0, s.extend);
    const int hi = std::max (s.highNote, roots.back()) + std::max (0, s.extend);
    const auto kz = keyZones (roots, s.placement, lo, hi);
    const auto vz = velocityZones (vels);
    for (auto& x : samples)
    {
        const auto ri = (size_t) (std::lower_bound (roots.begin(), roots.end(), mappedRoot (x, s)) - roots.begin());
        const auto vi = (size_t) (std::lower_bound (vels.begin(), vels.end(), x.velocity) - vels.begin());
        x.keyLo = kz[ri].lo;
        x.keyHi = kz[ri].hi;
        x.velLo = vz[vi].lo;
        x.velHi = vz[vi].hi;
    }
}
}
