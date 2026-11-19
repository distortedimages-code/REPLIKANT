// REPLIK8 — DRUM (0.4): the tracks, their voices and their sequencer (DrumCore.h). Our own implementation, after the
// sequencer of IMPAKT.
#include "DrumCore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace drum
{
namespace
{
    constexpr float twoPi = 6.28318530717958647f;
    using K = Kind;

    const ParamDef table[numParams] = {
        { "src",   "Source",     K::choice,  0, 2, 0, "SYNTH|SAMPLE|SLICES", "" },
        { "note",  "Note",       K::integer, 24, 96, 60, nullptr, "note" },
        { "tune",  "Tune",       K::linear,  -24, 24, 0, nullptr, "st" },
        { "bank",  "Bank",       K::choice,  0, 1, 0, "BASIC|CAPTURED", "" },
        { "pos",   "Position",   K::linear,  0, 1, 0, nullptr, "%" },
        { "start", "Start",      K::linear,  0, 1, 0, nullptr, "%" },
        { "slice", "Slice",      K::integer, 0, 15, 0, nullptr, "slice" },
        { "rev",   "Reverse",    K::toggle,  0, 1, 0, nullptr, "" },
        { "sweep", "Sweep",      K::linear,  -24, 48, 0, nullptr, "st" },
        { "swt",   "Sweep time", K::log,     0.005f, 2, 0.05f, nullptr, "s" },
        { "pmod",  "Pmod",       K::linear,  0, 1, 0, nullptr, "%" },
        { "ratio", "Ratio",      K::log,     0.25f, 16, 1.4142f, nullptr, "x" },
        { "atk",   "Attack",     K::linear,  0, 1, 0, nullptr, "atk" },
        { "dec",   "Decay",      K::linear,  0, 1, 0.5f, nullptr, "dec" },
        { "body",  "Body",       K::linear,  0, 1, 1, nullptr, "%" },
        { "click", "Click",      K::linear,  0, 1, 0, nullptr, "%" },
        { "noise", "Noise",      K::linear,  0, 1, 0, nullptr, "%" },
        { "tone",  "Tone",       K::linear,  0, 1, 0.5f, nullptr, "%" },
        { "filt",  "Filter",     K::choice,  0, 2, 0, "LP|HP|BP", "" },
        { "cut",   "Cutoff",     K::log,     20, 20000, 20000, nullptr, "Hz" },
        { "reso",  "Reso",       K::linear,  0, 1, 0, nullptr, "%" },
        { "fenv",  "F.Env",      K::linear,  -1, 1, 0, nullptr, "fenv" },
        { "fatk",  "F.Attack",   K::linear,  0, 1, 0, nullptr, "atk" },
        { "fdec",  "F.Decay",    K::linear,  0, 1, 0.35f, nullptr, "dec" },
        { "drive", "Drive",      K::linear,  0, 1, 0, nullptr, "%" },
        { "lvl",   "Level",      K::linear,  0, 1, 0.8f, nullptr, "%" },
        { "pan",   "Pan",        K::linear,  -1, 1, 0, nullptr, "pan" },
        { "choke", "Choke",      K::choice,  0, 4, 0, "OFF|A|B|C|D", "" },
        { "vel",   "Velocity",   K::linear,  0, 1, 0.8f, nullptr, "%" },
        { "prob",  "Prob",       K::linear,  0, 1, 1, nullptr, "%" },
        { "micro", "Micro",      K::linear,  -0.5f, 0.5f, 0, nullptr, "micro" },
        { "rtrg",  "Retrig",     K::choice,  0, 12, 0, "OFF|1/1|1/2|1/4|1/8|1/12|1/16|1/24|1/32|1/48|1/64|1/96|1/128", "" },
        { "rcnt",  "Count",      K::integer, 1, 16, 3, nullptr, "hits" },
        { "rcrv",  "Curve",      K::linear,  -1, 1, 0, nullptr, "crv" },
        { "rvel",  "Vel curve",  K::linear,  -1, 1, 0, nullptr, "crv" },
        { "stereo", "Stereo",    K::toggle,  0, 1, 1, nullptr, "" },
    };
    const float speeds[numSpeeds] = { 0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    const float retrigDen[numRetrigs] = { 0, 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 };

    std::string fmt (const char* f, double v)
    {
        char b[64];
        std::snprintf (b, sizeof (b), f, v);
        return b;
    }
    inline float hermite (float xm1, float x0, float x1, float x2, float t)
    {
        const float c = 0.5f * (x1 - xm1), v = x0 - x1, w = c + v, a = w + v + 0.5f * (x2 - x0), b = w + a;
        return ((a * t - b) * t + c) * t + x0;
    }
    inline float clampf (float v, float lo, float hi)   { return v < lo ? lo : (v > hi ? hi : v); }
    inline int idx (float v, int n)                        { const int i = (int) std::lround (v); return i < 0 ? 0 : (i >= n ? n - 1 : i); }
}

const ParamDef& def (int p)                  { return table[p < 0 ? 0 : (p >= numParams ? numParams - 1 : p)]; }
float speedOf (int i)                        { return speeds[i < 0 ? 0 : (i >= numSpeeds ? numSpeeds - 1 : i)]; }
float retrigPpq (int i)                      { const float d = retrigDen[i < 0 ? 0 : (i >= numRetrigs ? numRetrigs - 1 : i)]; return d > 0.0f ? 4.0f / d : 0.0f; }

std::vector<std::string> choicesOf (int p)
{
    const auto& d = def (p);
    if (d.kind == K::toggle)
        return { "OFF", "ON" };
    std::vector<std::string> out;
    if (d.choices == nullptr)
        return out;
    std::string cur;
    for (const char* c = d.choices; *c != 0; ++c)
    {
        if (*c == '|') { out.push_back (cur); cur.clear(); }
        else cur += *c;
    }
    out.push_back (cur);
    return out;
}

float fromNorm (int p, float t)
{
    const auto& d = def (p);
    t = clampf (t, 0.0f, 1.0f);
    switch (d.kind)
    {
        case K::log:     return d.lo * std::pow (d.hi / d.lo, t);
        case K::integer:
        case K::choice:
        case K::toggle:  return std::round (d.lo + (d.hi - d.lo) * t);
        default:         return d.lo + (d.hi - d.lo) * t;
    }
}

float toNorm (int p, float v)
{
    const auto& d = def (p);
    if (d.hi <= d.lo)
        return 0.0f;
    v = clampf (v, d.lo, d.hi);
    if (d.kind == K::log)
        return std::log (v / d.lo) / std::log (d.hi / d.lo);
    return (v - d.lo) / (d.hi - d.lo);
}

float legal (int p, float v)
{
    const auto& d = def (p);
    if (! std::isfinite (v))
        return d.def;
    v = clampf (v, d.lo, d.hi);
    if (d.kind == K::integer || d.kind == K::choice || d.kind == K::toggle)
        v = std::round (v);
    return v;
}

std::string text (int p, float v)
{
    const auto& d = def (p);
    if (d.kind == K::choice || d.kind == K::toggle)
    {
        const auto c = choicesOf (p);
        const int i = (int) std::lround (v - d.lo);
        return i >= 0 && i < (int) c.size() ? c[(size_t) i] : std::string();
    }
    const std::string u = d.unit;
    if (u == "%")     return fmt ("%.0f %%", v * 100.0f);
    if (u == "st")    return std::abs (v) < 0.005f ? std::string ("0 ST") : fmt ("%+.2f ST", v);
    if (u == "s")     return v < 1.0f ? fmt ("%.0f MS", v * 1000.0f) : fmt ("%.2f S", v);
    if (u == "x")     return fmt ("%.2f X", v);
    if (u == "Hz")    return v >= 19900.0f ? std::string ("OPEN") : (v < 1000.0f ? fmt ("%.0f HZ", v) : fmt ("%.1f KHZ", v / 1000.0f));
    if (u == "atk")   return v < 0.005f ? std::string ("0 MS") : fmt ("%.0f MS", 200.0 * v * v);
    if (u == "dec")   return v >= 0.99f ? std::string ("HOLD") : (0.01 * std::pow (400.0, (double) v) < 1.0 ? fmt ("%.0f MS", 10.0 * std::pow (400.0, (double) v))
                                                                                                         : fmt ("%.2f S", 0.01 * std::pow (400.0, (double) v)));
    if (u == "pan")   return std::abs (v) < 0.01f ? std::string ("C") : (v < 0.0f ? fmt ("L%.0f", -v * 100.0f) : fmt ("R%.0f", v * 100.0f));
    if (u == "micro") return std::abs (v) < 0.001f ? std::string ("0") : fmt ("%+.0f/48", v * 48.0f);
    if (u == "fenv")  return std::abs (v) < 0.005f ? std::string ("0 OCT") : fmt ("%+.1f OCT", v * 6.0f);
    if (u == "crv")   return std::abs (v) < 0.005f ? std::string ("EVEN") : fmt ("%+.0f %%", v * 100.0f);
    if (u == "hits")  return fmt ("%.0f", v);
    if (u == "slice") return fmt ("%.0f", v + 1.0f);
    if (u == "note")
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = (int) std::lround (v);
        return std::string (names[((n % 12) + 12) % 12]) + std::to_string (n / 12 - 2);
    }
    return fmt ("%.2f", v);
}

//==============================================================================
void Track::reset (int index)
{
    for (int i = 0; i < numParams; ++i)
        p[i] = def (i).def;
    for (int s = 0; s < numSteps; ++s)
    {
        trig[s] = 0;
        rat[s] = 1;
        for (int i = 0; i < numParams; ++i)
            lock[s][i] = NAN;
    }
    len = 16;
    speed = 4;
    // a kit to start with, all made of the synth: a kick, a snare, two hats (choked together), a tom, a clap, a metal
    // and a zap
    switch (index)
    {
        case 0: p[pNote] = 60; p[pSweep] = 30; p[pSweepTime] = 0.035f; p[pDecay] = 0.45f; p[pClick] = 0.35f; p[pDrive] = 0.25f; break;
        case 1: p[pNote] = 74; p[pSweep] = 7; p[pSweepTime] = 0.03f; p[pDecay] = 0.3f; p[pBody] = 0.55f; p[pNoise] = 0.75f; p[pTone] = 0.65f; p[pClick] = 0.2f; break;
        case 2: p[pBody] = 0.0f; p[pNoise] = 1.0f; p[pTone] = 0.95f; p[pDecay] = 0.18f; p[pFilter] = 1; p[pCutoff] = 6500.0f; p[pChoke] = 1; p[pLevel] = 0.6f; break;
        case 3: p[pBody] = 0.0f; p[pNoise] = 1.0f; p[pTone] = 0.95f; p[pDecay] = 0.5f; p[pFilter] = 1; p[pCutoff] = 6000.0f; p[pChoke] = 1; p[pLevel] = 0.55f; break;
        case 4: p[pNote] = 67; p[pSweep] = 9; p[pSweepTime] = 0.08f; p[pDecay] = 0.48f; p[pClick] = 0.15f; break;
        case 5: p[pBody] = 0.0f; p[pNoise] = 1.0f; p[pTone] = 0.6f; p[pDecay] = 0.3f; p[pFilter] = 2; p[pCutoff] = 1400.0f; p[pReso] = 0.25f; p[pRetrig] = 0; break;
        case 6: p[pNote] = 82; p[pPmod] = 0.55f; p[pRatio] = 1.4142f; p[pDecay] = 0.42f; p[pLevel] = 0.6f; break;
        case 7: p[pNote] = 72; p[pSweep] = 48; p[pSweepTime] = 0.06f; p[pDecay] = 0.35f; p[pLevel] = 0.6f; break;
        default: break;
    }
}

bool Track::locked (int step) const
{
    if (step < 0 || step >= numSteps)
        return false;
    for (int i = 0; i < numParams; ++i)
        if (! std::isnan (lock[step][i]))
            return true;
    return false;
}

void Pattern::reset()
{
    for (int t = 0; t < numTracks; ++t)
        tr[t].reset (t);
    used = 0;
    chg = 0;
}

int Pattern::length16() const
{
    if (chg > 0)
        return chg;
    double longest = 1.0;
    for (const auto& t : tr)
        longest = std::max (longest, (double) t.len / (double) speedOf (t.speed));
    return std::max (1, (int) std::ceil (longest - 1.0e-9));
}

//==============================================================================
Engine::Engine()
{
    for (auto& m : mute) m = false;
    for (auto& s : stepNow) s = -1;
    for (auto& g : glow) g = 0.0f;
    for (auto& s : nextSound) s = nullptr;
    for (auto& s : usedSound) s = nullptr;
    banks[0] = nullptr;
    banks[1] = nullptr;
    for (auto& c : chainSlots) c = 0;
    hits.reserve (512);
}

void Engine::allocate()
{
    if (allocated())
        return;
    pats.resize ((size_t) numPatterns);
    for (auto& p : pats)
        p.reset();
    pats[0].used = 1;
    armed.store (true, std::memory_order_release);
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    for (auto& t : trackOut)
        t.assign ((size_t) std::max (maxBlock, 512), 0.0f);
    for (auto& v : voices)
        v.active = false;
    for (auto& r : run)
        r = Run {};
    wasRunning = false;
}

void Engine::queue (int i)
{
    if (i < 0 || i >= numPatterns || ! allocated())
        return;
    if (! pats[(size_t) i].used)
    {
        // (a pattern never written: the sounds of the one playing, no trigs, 16 steps at 1x)
        auto& to = pats[(size_t) i];
        const auto& from = pats[(size_t) playing.load()];
        for (int t = 0; t < numTracks; ++t)
        {
            to.tr[t].reset (t);
            std::memcpy (to.tr[t].p, from.tr[t].p, sizeof (to.tr[t].p));
        }
        to.used = 1;
    }
    if (! runningNow.load())
    {
        playing.store (i);
        queued.store (-1);
    }
    else
        queued.store (i);
}

void Engine::setChain (const std::vector<int>& c)
{
    const int n = std::min ((int) c.size(), (int) chainSlots.size());
    chainLen.store (0);
    for (int i = 0; i < n; ++i)
        chainSlots[(size_t) i].store (std::max (0, std::min (numPatterns - 1, c[(size_t) i])));
    chainAt = 0;
    chainLen.store (n);
    if (n > 0)
        queue (chainSlots[0].load());
}

std::vector<int> Engine::chain() const
{
    std::vector<int> c;
    for (int i = 0; i < chainLen.load(); ++i)
        c.push_back (chainSlots[(size_t) i].load());
    return c;
}

double Engine::stepPpq (const Track& t) const
{
    return 0.25 / (double) speedOf (t.speed);
}

//============================================================================== the sequencer
// the hits of this block: each track's steps falling in it (swing, micro-timing, probability), their retrigs and
// ratchets, a pattern change at its boundary
void Engine::schedule (int n, const Clock& c, float swing)
{
    if (! c.running)
    {
        if (wasRunning)
        {
            for (int t = 0; t < numTracks; ++t)
            {
                run[(size_t) t] = Run {};
                stepNow[(size_t) t].store (-1);
            }
            // (the notes held let go)
            for (auto& v : voices)
                if (v.active && v.hold)
                    v.fadeStep = -1.0f / (0.25f * (float) sr);
        }
        wasRunning = false;
        runningNow.store (false);
        return;
    }
    const double bpm = c.bpm > 1.0 ? c.bpm : 120.0;
    const double samplesPerPpq = sr * 60.0 / bpm;
    const double ppq0 = c.ppq, ppq1 = ppq0 + (double) n / samplesPerPpq;
    if (! wasRunning || ppq0 < lastPpq - 1.0e-6 || ppq0 > lastPpq + 0.5)
    {
        // started, or the song position jumped: the steps from where it is now, the pattern counted from 0
        patStart = 0.0;
        lastSwitch = -1.0e9;
        for (int t = 0; t < numTracks; ++t)
        {
            const auto& tk = pats[(size_t) playing.load()].tr[t];
            run[(size_t) t] = Run {};
            run[(size_t) t].lastStep = (int64_t) std::ceil ((ppq0 - patStart) / stepPpq (tk) - 1.0e-9) - 1;
        }
        chainAt = 0;
    }
    wasRunning = true;
    runningNow.store (true);
    lastPpq = ppq1;

    // a track's retrig hits still due before `before` (samples into the block)
    auto flushRetrig = [&] (int t, double before)
    {
        auto& r = run[(size_t) t];
        while (r.rtLeft > 0 && r.rtNext < before)
        {
            const int i = r.rtTotal - r.rtLeft + 1;
            const float vm = r.rtRVel > 0.0f ? 1.0f - r.rtRVel * (float) i / (float) r.rtTotal
                                             : 1.0f + r.rtRVel * (1.0f - (float) i / (float) r.rtTotal);
            if (! mute[t].load (std::memory_order_relaxed))
                hits.push_back ({ std::max (0, (int) r.rtNext), t, r.rtStep, std::max (0.0f, vm), true, -1.0f });
            r.rtGap *= r.rtRatio;
            r.rtNext += r.rtGap;
            --r.rtLeft;
        }
    };
    auto segment = [&] (double a, double b)
    {
        const auto& pat = pats[(size_t) playing.load()];
        for (int t = 0; t < numTracks; ++t)
        {
            const auto& tk = pat.tr[t];
            auto& r = run[(size_t) t];
            const double sp = stepPpq (tk);
            const int len = std::max (1, (int) tk.len);
            for (int64_t j = r.lastStep + 1;; ++j)
            {
                if (j < 0)
                {
                    r.lastStep = j;
                    continue;
                }
                const int s = (int) (j % len);
                double at = patStart + (double) j * sp;
                if (j & 1)
                    at += (double) clampf ((swing - 0.5f) * 2.0f, 0.0f, 0.6f) * sp;
                if (tk.trig[s])
                    at += (double) clampf (tk.at (s, pMicro), -0.5f, 0.5f) * sp;
                // (in samples too: a hit that rounds to the next block is that block's)
                const int off = (int) std::max (0.0, std::floor ((at - ppq0) * samplesPerPpq + 1.0e-6));
                if (at >= b || off >= n)
                    break;
                r.lastStep = j;
                if (! tk.trig[s] || mute[t].load (std::memory_order_relaxed))
                    continue;
                const float prob = modded (t, pProb, tk.at (s, pProb));
                if (prob < 0.999f && rng.uni() >= prob)
                    continue;
                // the last hit's retrigs up to this one; then its own retrig, or its ratchet: the step split evenly
                flushRetrig (t, (double) off);
                const int nr = std::max (1, (int) tk.rat[s]);
                const int ri = idx (tk.at (s, pRetrig), numRetrigs);
                const float rvel = clampf (tk.at (s, pRVel), -1.0f, 1.0f);
                r.rtLeft = 0;
                float firstVel = 1.0f;
                if (nr > 1 || ri > 0)
                {
                    const int cnt = nr > 1 ? nr - 1 : idx (tk.at (s, pCount), 17);
                    const double base = nr > 1 ? sp * samplesPerPpq / (double) nr : (double) retrigPpq (ri) * samplesPerPpq;
                    double q = 1.0, scale = 1.0;
                    const float crv = clampf (tk.at (s, pCurve), -1.0f, 1.0f);
                    if (cnt > 1 && std::abs (crv) > 0.01f)
                    {
                        // the gaps close in (right) or spread out (left), the last hit where an even roll ends: each gap
                        // the same ratio of the last, up to 16x from the first to the last
                        const double e = 4.0 * (crv > 0.0f ? -std::abs (crv) : std::abs (crv));
                        q = std::exp2 (e / (double) (cnt - 1));
                        scale = (double) cnt * (1.0 - q) / (1.0 - std::exp2 (e * (double) cnt / (double) (cnt - 1)));
                    }
                    r.rtLeft = cnt;
                    r.rtTotal = cnt;
                    r.rtStep = s;
                    r.rtGap = base * scale;
                    r.rtRatio = q;
                    r.rtNext = (double) off + r.rtGap;
                    r.rtRVel = rvel;
                    r.rtRatchet = nr > 1;
                    firstVel = rvel < 0.0f ? 1.0f + rvel : 1.0f;
                }
                hits.push_back ({ off, t, s, firstVel, false, -1.0f });
            }
        }
    };

    // a change waiting (or a chain going on): at the end of the pattern playing, counted from where it started
    const auto& now = pats[(size_t) playing.load()];
    const double L = (double) now.length16() * 0.25;
    const bool changing = queued.load() >= 0 || chainLen.load() > 0;
    double boundary = patStart + L * std::ceil ((ppq0 - patStart) / L - 1.0e-9);
    if (boundary <= lastSwitch + 1.0e-6)
        boundary += L;                       // (the pattern just started there: its end is the next one)
    if (boundary < ppq0)
        boundary = ppq0;
    if (changing && boundary < ppq1)
    {
        segment (ppq0, boundary);
        int next = queued.exchange (-1);
        if (next < 0 && chainLen.load() > 0)
        {
            chainAt = (chainAt + 1) % chainLen.load();
            next = chainSlots[(size_t) chainAt].load();
        }
        else if (next >= 0 && chainLen.load() > 0)
        {
            for (int i = 0; i < chainLen.load(); ++i)
                if (chainSlots[(size_t) i].load() == next)
                    chainAt = i;
        }
        if (next >= 0)
            playing.store (next);
        patStart = boundary;
        lastSwitch = boundary;
        for (int t = 0; t < numTracks; ++t)
        {
            const auto& tk = pats[(size_t) playing.load()].tr[t];
            run[(size_t) t].lastStep = (int64_t) std::ceil ((boundary - patStart) / stepPpq (tk) - 1.0e-9) - 1;
        }
        segment (boundary, ppq1);
    }
    else
        segment (ppq0, ppq1);
    for (int t = 0; t < numTracks; ++t)
    {
        flushRetrig (t, (double) n);
        if (run[(size_t) t].rtLeft > 0)
            run[(size_t) t].rtNext -= (double) n;
    }

    // the playheads
    for (int t = 0; t < numTracks; ++t)
    {
        const auto& tk = pats[(size_t) playing.load()].tr[t];
        const int64_t j = (int64_t) std::floor ((ppq1 - patStart) / stepPpq (tk));
        stepNow[(size_t) t].store (j >= 0 ? (int) (j % std::max (1, (int) tk.len)) : -1, std::memory_order_relaxed);
    }
}

//============================================================================== the voices
void Engine::startVoice (int track, const float* p, float vel, const float* base, int step)
{
    // CHOKE: the others of its group cut (5 ms)
    const int group = idx (p[pChoke], 5);
    for (auto& v : voices)
        if (v.active && v.track != track && group > 0 && idx (v.p[pChoke], 5) == group)
            v.fadeStep = -1.0f / (0.005f * (float) sr);
    // the track's voice playing fades out (3 ms), the other one starts (both busy: the quieter one starts again)
    Voice& a = voices[(size_t) track * 2];
    Voice& b = voices[(size_t) track * 2 + 1];
    Voice* use = ! a.active ? &a : (! b.active ? &b : (a.fade * a.env < b.fade * b.env ? &a : &b));
    Voice* old = use == &a ? &b : &a;
    if (old->active)
        old->fadeStep = -1.0f / (0.003f * (float) sr);
    Voice& v = *use;
    v = Voice {};
    v.active = true;
    v.track = track;
    std::memcpy (v.p, p, sizeof (v.p));
    std::memcpy (v.base, base != nullptr ? base : p, sizeof (v.base));
    v.fromStep = step;
    {
        const auto& tk = pats[(size_t) playing.load()].tr[track];
        for (int i = 0; i < numParams; ++i)
            v.atHit[i] = step >= 0 ? tk.at (step, i) : tk.p[i];
    }
    const int src = idx (p[pSource], 3);
    const float fsr = (float) sr;
    v.sound = src == synth ? nullptr : usedSound[(size_t) track].load (std::memory_order_acquire);
    if (src != synth && (v.sound == nullptr || v.sound->audio == nullptr || v.sound->length() < 2))
        v.sound = nullptr;
    v.stereo = v.sound != nullptr && p[pStereo] > 0.5f;
    v.ic[1][0] = v.ic[1][1] = 0.0f;
    if (v.sound != nullptr)
    {
        const int len = v.sound->length();
        int a = 0, b = len;
        if (src == slices && ! v.sound->slices.empty())
        {
            const int sl = std::min ((int) v.sound->slices.size() - 1, idx (p[pSlice], 16));
            a = std::max (0, std::min (len - 1, v.sound->slices[(size_t) sl].start));
            b = std::max (a + 1, std::min (len, v.sound->slices[(size_t) sl].end));
        }
        a += (int) (clampf (p[pStart], 0.0f, 0.98f) * (float) (b - a));
        v.from = a;
        v.to = b;
        const bool rev = p[pReverse] > 0.5f;
        v.pos = rev ? (double) (b - 1) : (double) a;
        v.step = v.sound->audio->sampleRate / sr * std::exp2 ((p[pNote] - 60.0f + p[pTune]) / 12.0f) * (rev ? -1.0 : 1.0);
    }
    v.hz = 440.0f * std::exp2 ((p[pNote] + p[pTune] - 69.0f - 24.0f) / 12.0f);
    const float atk = clampf (p[pAttack], 0.0f, 1.0f);
    const float atkSec = 0.2f * atk * atk;
    v.att = atkSec < 0.0005f ? 1.0f : 0.0f;
    v.attInc = atkSec < 0.0005f ? 1.0f : 1.0f / (atkSec * fsr);
    v.env = v.att;
    v.hold = p[pDecay] >= 0.99f;
    const float decSec = 0.01f * std::pow (400.0f, clampf (p[pDecay], 0.0f, 1.0f));
    v.decCoef = v.hold ? 1.0f : std::exp (-1.0f / (decSec * fsr));
    v.swEnv = 1.0f;
    v.swCoef = std::exp (-1.0f / (std::max (0.002f, p[pSweepTime]) * fsr));
    v.clickEnv = clampf (p[pClick], 0.0f, 1.0f);
    v.noiseEnv = clampf (p[pNoise], 0.0f, 1.0f);
    v.noiseCoef = std::exp (-1.0f / ((v.hold ? 2.0f : decSec * 0.5f) * fsr));
    // the filter's envelope: ATTACK up to 200 ms, then DECAY down (HOLD: it stays up)
    const float fatk = clampf (p[pFAttack], 0.0f, 1.0f), fatkSec = 0.2f * fatk * fatk;
    v.fatt = fatkSec < 0.0005f ? 1.0f : 0.0f;
    v.fattInc = fatkSec < 0.0005f ? 1.0f : 1.0f / (fatkSec * fsr);
    v.fenv = v.fatt;
    v.fhold = p[pFDecay] >= 0.99f;
    v.fdecCoef = v.fhold ? 1.0f : std::exp (-1.0f / (0.01f * std::pow (400.0f, clampf (p[pFDecay], 0.0f, 1.0f)) * fsr));
    v.vel = clampf (vel, 0.0f, 1.0f);
    v.fade = 1.0f;
    v.fadeStep = 0.0f;
}

float Engine::modded (int track, int param, float v, float extra) const
{
    const float o = mods[(size_t) (track * modStride + param)] + extra;
    if (! (std::abs (o) > 1.0e-5f))
        return v;
    return legal (param, fromNorm (param, clampf (toNorm (param, v) + o, 0.0f, 1.0f)));
}

void Engine::hit (int track, int step, float velMul, bool)
{
    const auto& tk = pats[(size_t) playing.load()].tr[track];
    float p[numParams];
    for (int i = 0; i < numParams; ++i)
        p[i] = tk.at (step, i);
    // WTF?!: the hit's settings pulled (each track its own share of the ten pulls)
    static const int pulled[numWtf] = { pTune, pDecay, pSweep, pPmod, pCutoff, pReso, pNoise, pStart, pDrive, pFEnv };
    static const float weight[numWtf] = { 0.4f, 1.0f, 0.6f, 1.0f, 1.0f, 0.7f, 0.8f, 0.8f, 0.8f, 0.8f };
    for (int j = 0; j < numWtf; ++j)
    {
        const float o = wtf[(size_t) ((j + 3 * track) % numWtf)] * weight[j];
        if (! (std::abs (o) > 1.0e-5f))
            continue;
        int id = pulled[j];
        if (id == pStart && idx (p[pSource], 3) == synth)
            id = pPosition;
        p[id] = legal (id, fromNorm (id, clampf (toNorm (id, p[id]) + o, 0.0f, 1.0f)));
    }
    // the mods: every setting of the hit (but its source and its choke group); the voice keeps where they started
    float base[numParams];
    std::memcpy (base, p, sizeof (base));
    for (int i = 0; i < numParams; ++i)
        if (i != pSource && i != pChoke)
            p[i] = modded (track, i, p[i]);
    startVoice (track, p, p[pVel] * velMul, base, step);
    glow[(size_t) track].store (1.0f, std::memory_order_relaxed);
}

void Engine::renderVoice (Voice& v, float* L, float* R, float* F, int n)
{
    const float fsr = (float) sr, isr = 1.0f / fsr;
    const int src = idx (v.p[pSource], 3);
    const auto* bank = banks[(size_t) idx (v.p[pBank], 2)].load (std::memory_order_relaxed);
    if (bank == nullptr || bank->count() == 0)
        bank = banks[0].load (std::memory_order_relaxed);
    const bool synthBody = src == synth && bank != nullptr && bank->count() > 0;
    const float body = clampf (v.p[pBody], 0.0f, 1.0f);
    const float pmod = clampf (v.p[pPmod], 0.0f, 1.0f), ratio = std::max (0.25f, v.p[pRatio]);
    const float sweep = v.p[pSweep];
    const float position = clampf (v.p[pPosition], 0.0f, 1.0f);
    // the track's filter (left open: nothing done), its drive
    const int ft = idx (v.p[pFilter], 3);
    const float cut = clampf (v.p[pCutoff], 20.0f, 20000.0f), reso = clampf (v.p[pReso], 0.0f, 1.0f);
    // (its envelope moves the cutoff up or down by as much as six octaves, the filter set again every 8 samples)
    const float fAmt = clampf (v.p[pFEnv], -1.0f, 1.0f) * 6.0f;
    const bool moving = std::abs (fAmt) > 0.006f;
    const bool filter = ! (ft == 0 && cut >= 19900.0f && reso < 0.01f && (! moving || fAmt > 0.0f));
    const float q = 0.5f + 11.5f * reso * reso;
    const float top = std::min (20000.0f, 0.45f * fsr);
    radio::Svf svf;
    if (filter && ! moving)
        svf.set (cut, q, fsr);
    const float drv = clampf (v.p[pDrive], 0.0f, 1.0f), dg = 1.0f + 8.0f * drv * drv, dn = 1.0f / std::tanh (dg);
    // the noise's colour: a one-pole low pass from 300 Hz (dark) to past 18 kHz, and a high pass above 2 kHz when bright
    const float tone = clampf (v.p[pTone], 0.0f, 1.0f);
    const float nA = 1.0f - std::exp (-twoPi * 300.0f * std::exp2 (6.0f * tone) * isr);
    const float hpMix = clampf ((tone - 0.6f) * 2.5f, 0.0f, 1.0f);
    const float hpA = 1.0f - std::exp (-twoPi * 2000.0f * isr);
    const float clickCoef = std::exp (-1.0f / (0.0006f * fsr));
    const float level = clampf (v.p[pLevel], 0.0f, 1.0f) * 1.2f * v.vel * v.vel;
    // PAN: a mono voice placed (equal power), a stereo one balanced (each side at 1 in the middle)
    const float pan = clampf (v.p[pPan], -1.0f, 1.0f);
    const float th = (pan + 1.0f) * 0.785398163f;
    const float gl = v.stereo ? (pan > 0.0f ? 1.0f - pan : 1.0f) : std::cos (th) * 1.41421356f;
    const float gr = v.stereo ? (pan < 0.0f ? 1.0f + pan : 1.0f) : std::sin (th) * 1.41421356f;
    const auto* A = v.sound != nullptr ? v.sound->audio.get() : nullptr;
    const int len = A != nullptr ? (int) A->l.size() : 0;
    int lvl = 0;
    for (int i = 0; i < n; ++i)
    {
        const float semis = sweep * v.swEnv;
        v.swEnv *= v.swCoef;
        const float pitch = std::exp2 (semis * (1.0f / 12.0f));
        float x = 0.0f, xr = 0.0f;       // (xr: the right side, for a voice in stereo)
        bool bodyLeft = false;
        if (synthBody && body > 0.0f)
        {
            const float f = v.hz * pitch;
            if ((i & 31) == 0)
                lvl = dsp::CycleBank::levelFor ((double) f * (1.0 + 3.0 * pmod), sr);
            double ph = v.phase;
            if (pmod > 0.0f)
            {
                ph += (double) (pmod * 1.5f * v.env * std::sin (twoPi * (float) v.mphase));
                v.mphase += (double) (f * ratio * isr);
                v.mphase -= std::floor (v.mphase);
            }
            ph -= std::floor (ph);
            x = bank->read (position, ph, lvl) * body;
            v.phase += (double) (f * isr);
            v.phase -= std::floor (v.phase);
            bodyLeft = true;
        }
        else if (A != nullptr && body > 0.0f && v.pos >= (double) v.from && v.pos < (double) v.to - 1.0)
        {
            double at = v.pos;
            if (pmod > 0.0f)
            {
                at += (double) (pmod * 0.0015f * (float) A->sampleRate * v.env * std::sin (twoPi * (float) v.mphase));
                v.mphase += (double) (220.0f * ratio * std::abs ((float) v.step) * pitch * (float) A->sampleRate / fsr * isr);
                v.mphase -= std::floor (v.mphase);
            }
            const int k = (int) std::floor (at);
            const float t = (float) (at - (double) k);
            auto s = [&] (const float* d, int j) { return d[j < v.from ? v.from : (j >= v.to ? v.to - 1 : j)]; };
            const float yl = hermite (s (A->l.data(), k - 1), s (A->l.data(), k), s (A->l.data(), k + 1), s (A->l.data(), k + 2), t);
            const float yr = hermite (s (A->r.data(), k - 1), s (A->r.data(), k), s (A->r.data(), k + 1), s (A->r.data(), k + 2), t);
            // (its ends faded: 2 ms)
            const double room = v.step >= 0.0 ? (double) v.to - v.pos : v.pos - (double) v.from;
            const float edge = (float) std::min (1.0, room / (0.002 * A->sampleRate));
            if (v.stereo)
            {
                x = yl * body * edge;
                xr = yr * body * edge;
            }
            else
                x = 0.5f * (yl + yr) * body * edge;
            v.pos += v.step * (double) pitch;
            bodyLeft = true;
            (void) len;
        }
        // the noise, the click
        float nz = 0.0f;
        if (v.noiseEnv > 1.0e-5f)
        {
            const float w = rng.bi();
            v.nz += (w - v.nz) * nA;
            v.dz[0] += (v.nz - v.dz[0]) * hpA;
            nz = (v.nz + hpMix * (v.nz - v.dz[0] - v.nz)) * v.noiseEnv;
            v.noiseEnv *= v.noiseCoef;
        }
        float ck = 0.0f;
        if (v.clickEnv > 1.0e-4f)
        {
            ck = v.clickEnv * (0.5f * rng.bi() + 0.8f * std::sin (twoPi * 3000.0f * v.t));
            v.clickEnv *= clickCoef;
        }
        // the envelope
        if (v.att < 1.0f)
        {
            v.att = std::min (1.0f, v.att + v.attInc);
            v.env = v.att;
        }
        else
            v.env *= v.decCoef;
        x = x * v.env + nz + ck;
        xr = v.stereo ? xr * v.env + nz + ck : x;
        if (moving)
        {
            if (v.fatt < 1.0f)
            {
                v.fatt = std::min (1.0f, v.fatt + v.fattInc);
                v.fenv = v.fatt;
            }
            else
                v.fenv *= v.fdecCoef;
            if (filter && (i & 7) == 0)
                svf.set (clampf (cut * std::exp2 (fAmt * v.fenv), 20.0f, top), q, fsr);
        }
        if (filter)
        {
            float lp, bp, hp;
            svf.tick (x, v.ic[0][0], v.ic[0][1], lp, bp, hp);
            x = ft == 0 ? lp : (ft == 1 ? hp : bp);
            if (v.stereo)
            {
                svf.tick (xr, v.ic[1][0], v.ic[1][1], lp, bp, hp);
                xr = ft == 0 ? lp : (ft == 1 ? hp : bp);
            }
        }
        if (drv > 0.001f)
        {
            x = std::tanh (x * dg) * dn;
            if (v.stereo)
                xr = std::tanh (xr * dg) * dn;
        }
        if (! v.stereo)
            xr = x;
        const float g = level * v.fade;
        L[i] += x * g * gl;
        R[i] += xr * g * gr;
        if (F != nullptr)
            F[i] += 0.5f * (x + xr) * g;
        v.t += isr;
        if (v.fadeStep != 0.0f)
        {
            v.fade += v.fadeStep;
            if (v.fade <= 0.0f)
            {
                v.active = false;
                return;
            }
        }
        // done: the body silent (or the sample at its end), the noise and the click gone
        if (v.att >= 1.0f && v.noiseEnv < 1.0e-5f && v.clickEnv < 1.0e-4f && (v.env < 1.0e-4f || ! bodyLeft) && ! (v.hold && bodyLeft))
        {
            v.active = false;
            return;
        }
    }
    if (! std::isfinite (v.ic[0][0]) || ! std::isfinite (v.ic[0][1]) || ! std::isfinite (v.ic[1][0]) || ! std::isfinite (v.ic[1][1]))
        v.active = false;
}

void Engine::allSoundOff()
{
    for (auto& v : voices)
        v.active = false;
}

void Engine::render (float* L, float* R, int n, const Clock& c, float swing, const Event* ev, int numEvents)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    trackFrames = n > 0 && (size_t) n <= trackOut[0].size() ? n : 0;
    for (auto& t : trackOut)
        std::fill (t.begin(), t.begin() + trackFrames, 0.0f);
    if (n <= 0 || ! allocated())
        return;
    auto F = [this] (const Voice& v, int pos) { return trackFrames > 0 ? trackOut[(size_t) v.track].data() + pos : nullptr; };
    for (int t = 0; t < numTracks; ++t)
    {
        const Sound* s = nextSound[(size_t) t].load (std::memory_order_acquire);
        if (s != usedSound[(size_t) t].load (std::memory_order_relaxed))
        {
            // (a new sound: the voices playing the old one stop)
            for (auto& v : voices)
                if (v.active && v.track == t && v.sound != nullptr)
                    v.active = false;
            usedSound[(size_t) t].store (s, std::memory_order_release);
        }
    }
    // what the mods move while a hit rings (block by block): its filter, drive, level, pan, colour
    // (and what is turned on the track while it rings: followed too)
    static const int moving[] = { pCutoff, pReso, pFEnv, pDrive, pLevel, pPan, pTone, pPmod, pNoise, pBody, pClick };
    for (auto& v : voices)
        if (v.active)
        {
            const auto& tk = pats[(size_t) playing.load()].tr[v.track];
            for (int i : moving)
            {
                const float now = v.fromStep >= 0 ? tk.at (v.fromStep, i) : tk.p[i];
                const float turned = toNorm (i, now) - toNorm (i, v.atHit[i]);
                v.p[i] = modded (v.track, i, v.base[i], turned);
            }
        }
    hits.clear();
    schedule (n, c, swing);
    // the keys: track 1 on C1
    for (int e = 0; e < numEvents; ++e)
    {
        const int t = ev[e].note - firstNote;
        if (t >= 0 && t < numTracks && ev[e].velocity > 0)
            hits.push_back ({ std::max (0, std::min (n - 1, ev[e].at)), t, -1, 1.0f, false, (float) ev[e].velocity / 127.0f });
    }
    std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.at < b.at; });
    int pos = 0;
    for (const auto& h : hits)
    {
        const int at = std::max (pos, std::min (n, h.at));
        if (at > pos)
            for (auto& v : voices)
                if (v.active)
                    renderVoice (v, L + pos, R + pos, F (v, pos), at - pos);
        pos = at;
        if (h.velAbs >= 0.0f)
        {
            const auto& tk = pats[(size_t) playing.load()].tr[h.track];
            float p[numParams];
            for (int i = 0; i < numParams; ++i)
                p[i] = i == pSource || i == pChoke ? tk.p[i] : modded (h.track, i, tk.p[i]);
            startVoice (h.track, p, h.velAbs, tk.p, -1);
            glow[(size_t) h.track].store (1.0f, std::memory_order_relaxed);
        }
        else
            hit (h.track, h.step, h.velMul, h.retrig);
    }
    if (pos < n)
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, L + pos, R + pos, F (v, pos), n - pos);
}
}
