#include "Weave.h"

#include <algorithm>
#include <cmath>

namespace weave
{
namespace
{
constexpr double pi = 3.14159265358979323846;

uint32_t mix (uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// a draw in [0, 1), the same for the same four numbers
float uni (uint32_t seed, uint32_t a, uint32_t b, uint32_t c) noexcept
{
    const uint32_t h = mix (mix (seed * 0x9e3779b1u + 0x632be5abu) ^ mix (a * 0x85ebca6bu + 0x1234567u)
                            ^ mix (b * 0xc2b2ae35u + 0x2545f491u) ^ mix (c * 0x27d4eb2fu + 0x165667b1u));
    return (float) (h >> 8) * (1.0f / 16777216.0f);
}

int64_t floorDiv (int64_t a, int64_t b) noexcept
{
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int legalGrid (int g) noexcept { return g <= 8 ? 8 : (g >= 32 ? 32 : 16); }
int legalBars (int b) noexcept { return b >= 4 ? 4 : (b >= 2 ? 2 : 1); }

// MUTATE: which draw a step plays at that pass: the one of the last pass (of the 64 before) where it was drawn again
// (0: the pattern's own); kept until drawn again
uint32_t generation (const Settings& st, uint32_t step, int64_t cycle) noexcept
{
    if (st.mutate <= 0.0f)
        return 0;
    for (int64_t c = cycle; c > cycle - 64; --c)
        if (uni (st.seed, step, (uint32_t) c, 0x5eedu) < st.mutate)
            return (uint32_t) c * 2u + 1u;
    return 0;
}
}

int plan (const Settings& st, int count, int64_t cycle, int force, Step* out)
{
    const int grid = legalGrid (st.grid), bars = legalBars (st.bars), total = grid * bars, spb = grid / 4;
    for (int s = 0; s < total; ++s)
        out[s] = {};
    if (count <= 0)
        return total;
    auto valid = [count] (int t) { return t >= 0 && t < count ? t : -1; };
    const int beatT = valid (st.beat), backT = valid (st.back);
    const float move = std::clamp (st.move, 0.0f, 1.0f);
    // CALL: the sound answering (another than the beat's), drawn from the seed
    const int anchor = beatT >= 0 ? beatT : 0;
    const int answer = count > 1 ? (anchor + 1 + std::min (count - 2, (int) (uni (st.seed, 7, 0, 1) * (float) (count - 1)))) % count : anchor;
    // EUCLID: the changes spread evenly through the bar, turned by the seed
    const int hits = (int) std::lround (move * (float) grid);
    const int rot = std::min (grid - 1, (int) (uni (st.seed, 11, 0, 3) * (float) grid));
    auto euclidHit = [&] (int i) { return hits > 0 && ((((i + rot) % grid) * hits) % grid) < hits; };

    int cur = beatT >= 0 ? beatT : std::min (count - 1, (int) (uni (st.seed, 5, 0, 2) * (float) count));
    int beatStart = cur;
    bool callOn = false;
    for (int s = 0; s < total; ++s)
    {
        auto& o = out[s];
        const bool onBeat = s % spb == 0;
        const int beatNo = (s / spb) % 4;
        int fixedT = -1;
        if (onBeat)
        {
            if (backT >= 0 && (beatNo == 1 || beatNo == 3))
                fixedT = backT;
            else if (beatT >= 0)
                fixedT = beatT;
            // CALL: this beat answered or not (drawn once a beat)
            const auto bs = (uint32_t) (5000 + s / spb);
            callOn = uni (st.seed, bs, generation (st, bs, cycle), 0) < move;
        }
        if (fixedT >= 0)
        {
            cur = fixedT;
            o.thread = (int8_t) cur;
            o.fixed = true;
            beatStart = cur;
            continue;
        }
        if (onBeat)
            beatStart = cur;
        const auto gen = generation (st, (uint32_t) s, cycle);
        float d[8];
        for (int k = 0; k < 8; ++k)
            d[k] = uni (st.seed, (uint32_t) s, gen, (uint32_t) k);
        if (count > 1)
            switch (st.style)
            {
                case styleWalk:   if (d[0] < move) cur = (cur + (d[1] < 0.25f ? count - 1 : 1)) % count; break;
                case styleEuclid: if (euclidHit (s % grid)) cur = (cur + 1) % count; break;
                case styleCall:   cur = (s % spb) * 2 >= spb && callOn ? answer : beatStart; break;
                default:     if (d[0] < move) cur = (cur + 1 + std::min (count - 2, (int) (d[1] * (float) (count - 1)))) % count; break;
            }
        o.thread = (int8_t) cur;
        // the accidents
        if (d[2] < st.time)
        {
            const int opts[] = { -1, -1, -2, -std::max (1, spb / 2), -spb, std::max (1, spb / 2), 1 };
            o.shift = (int8_t) opts[std::min (6, (int) (d[3] * 7.0f))];
        }
        if (d[4] < st.stutter)
            o.ratchet = (uint8_t) (d[5] < 0.6f ? 2 : (d[5] < 0.75f ? 3 : 4));
        o.reverse = d[6] < st.reverse;
    }
    if (force >= 0 && force < count)
        for (int s = 0; s < total; ++s)
            out[s].thread = (int8_t) force;
    return total;
}

double spanBars (const Settings& st, double seconds, double bpm)
{
    static const double spans[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    if (st.span >= 1 && st.span <= 7)
        return spans[st.span - 1];
    const double r = seconds / (240.0 / std::max (20.0, bpm));
    if (! (r > 0.0))
        return 1.0;
    double best = 1.0, err = 1.0e9;
    for (double s : spans)
    {
        const double e = std::abs (std::log2 (r / s));
        if (e < err - 1.0e-9)
        {
            err = e;
            best = s;
        }
    }
    return best;
}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    held.reserve (128);
    reset();
}

void Engine::reset()
{
    cur = {};
    ghost = {};
    ghostLeft = 0;
    lastThread = -1;
    for (auto& g : grains)
        g.on = false;
    sinceSpawn = 1 << 30;
    planCycle = -1;
    plannedCount = -1;
    held.clear();
    ownRunning = false;
    gate = 0.0f;
}

void Engine::read (const Audio& a, double frame, float& l, float& r) const noexcept
{
    const int n = a.frames();
    if (n < 4)
    {
        l = r = 0.0f;
        return;
    }
    double f = std::fmod (frame, (double) n);
    if (f < 0.0)
        f += (double) n;
    const int i1 = std::min (n - 1, (int) f);
    const float t = (float) (f - (double) i1);
    const int i0 = i1 == 0 ? n - 1 : i1 - 1, i2 = i1 + 1 >= n ? i1 + 1 - n : i1 + 1, i3 = i1 + 2 >= n ? i1 + 2 - n : i1 + 2;
    auto herm = [t] (float y0, float y1, float y2, float y3)
    {
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    };
    l = herm (a.l[(size_t) i0], a.l[(size_t) i1], a.l[(size_t) i2], a.l[(size_t) i3]);
    r = herm (a.r[(size_t) i0], a.r[(size_t) i1], a.r[(size_t) i2], a.r[(size_t) i3]);
}

void Engine::spawn (int thread, double frame, double rate, bool atPeak)
{
    Grain* g = nullptr;
    for (auto& x : grains)
        if (! x.on)
        {
            g = &x;
            break;
        }
    if (g == nullptr)
    {
        g = &grains[0];
        for (auto& x : grains)
            if (x.age * g->len > g->age * x.len)
                g = &x;           // (the one furthest through its window)
    }
    const int len = std::max (64, grainLen);
    lastGrain = (int) (g - grains.data());
    g->on = true;
    g->thread = thread;
    g->pos = frame;
    g->rate = rate;
    g->len = len;
    g->age = atPeak ? len / 2 : 0;
    g->fadeIn = atPeak ? fadeN : 0;
    g->fadeOut = -1;
    g->fadeLen = fadeN;
}

double Engine::align (const Audio& a, double natural, double target, double dir) const noexcept
{
    const int n = a.frames();
    const int W = 128, range = std::min (grainLen / 4, (int) (0.012 * a.sampleRate));
    if (n < 4 * W || range < 2)
        return target;
    auto at = [&a, n] (long i) { i %= n; if (i < 0) i += n; return a.l[(size_t) i] + a.r[(size_t) i]; };
    const long nat = (long) std::floor (natural), tg = (long) std::floor (target), d = dir < 0.0 ? -1 : 1;
    float ref[W / 2];
    for (int k = 0; k < W / 2; ++k)
        ref[k] = at (nat + 2 * k * d);
    double best = -1.0e30, bestPos = target;
    for (int o = -range; o <= range; ++o)
    {
        double c = 0.0, en = 1.0e-9;
        for (int k = 0; k < W / 2; ++k)
        {
            const float x = at (tg + o + 2 * k * d);
            c += (double) (ref[k] * x);
            en += (double) (x * x);
        }
        const double score = c / std::sqrt (en);
        if (score > best)
        {
            best = score;
            bestPos = target + (double) o;
        }
    }
    return bestPos;
}

void Engine::render (float* L, float* R, int n, const Clock& ck, const Settings& st, const Event* ev, int ne)
{
    const Audio* a[maxThreads];
    int count = 0;
    for (int i = 0; i < maxThreads; ++i)
    {
        a[i] = threads[(size_t) i].load (std::memory_order_acquire);
        if (a[i] != nullptr && a[i]->frames() >= 4 && count == i)
            ++count;
    }
    fadeN = std::max (1, (int) std::lround (std::clamp (st.xfadeMs, 0.1f, 200.0f) * 0.001 * sr));
    grainLen = std::max (64, (int) std::lround (std::clamp (st.grainMs, 10.0f, 500.0f) * 0.001 * sr));
    const double bpm = ck.bpm > 1.0 ? ck.bpm : 120.0;
    const double ppqPerSample = bpm / 60.0 / sr;
    const int grid = legalGrid (st.grid), total = grid * legalBars (st.bars);
    const float gateCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
    const bool tape = st.fit != fitStretch;
    int e = 0, shown = -1;
    for (int i = 0; i < n; ++i)
    {
        // the keys, where they are played: they force a sound, and run the weave from its start while the transport
        // stands still
        while (e < ne && ev[e].pos <= i)
        {
            const auto& m = ev[e++];
            if (m.velocity > 0)
            {
                held.erase (std::remove (held.begin(), held.end(), m.note), held.end());
                if (held.size() < held.capacity())
                    held.push_back (m.note);
                if (! ck.running && ! ownRunning)
                {
                    ownRunning = true;
                    ownPpq = 0.0;
                }
            }
            else
            {
                held.erase (std::remove (held.begin(), held.end(), m.note), held.end());
                if (held.empty())
                    ownRunning = false;
            }
        }
        const bool running = (ck.running || ownRunning) && count > 0;
        const double p = ck.running ? ck.ppq + (double) i * ppqPerSample : ownPpq;
        if (! ck.running && ownRunning)
            ownPpq += ppqPerSample;
        gate += ((running ? 1.0f : 0.0f) - gate) * gateCoef;
        if (! running && gate < 1.0e-4f)
        {
            cur.thread = -1;
            ghostLeft = 0;
            lastThread = -1;
            for (auto& g : grains)
                g.on = false;
            continue;
        }
        if (running)
        {
            const double stepPos = p * (double) grid / 4.0;
            const auto S = (int64_t) std::floor (stepPos);
            const int64_t cyc = floorDiv (S, total);
            const int idx = (int) (S - cyc * total);
            const int force = held.empty() ? -1 : ((held.back() - 60) % count + count) % count;
            if (cyc != planCycle || count != plannedCount || force != plannedForce || st.beat != planned.beat || st.back != planned.back
                || st.move != planned.move || st.style != planned.style || st.time != planned.time || st.stutter != planned.stutter
                || st.reverse != planned.reverse || st.mutate != planned.mutate || st.bars != planned.bars || st.grid != planned.grid
                || st.seed != planned.seed)
            {
                numSteps = plan (st, count, cyc, force, steps.data());
                planCycle = cyc;
                plannedCount = count;
                plannedForce = force;
                planned = st;
                shownCycle.store (cyc, std::memory_order_relaxed);
                shownForce.store (force, std::memory_order_relaxed);
            }
            const auto& sp = steps[(size_t) std::min (idx, numSteps - 1)];
            shown = idx;
            if (sp.thread >= 0 && sp.thread < count)
            {
                const double seg = 1.0 / (double) std::max (1, (int) sp.ratchet);
                double l2 = std::fmod (stepPos - (double) S, seg);
                if (sp.reverse)
                    l2 = seg - l2;
                const double srcPpq = ((double) (S + sp.shift) + l2) * 4.0 / (double) grid;
                const Audio& au = *a[sp.thread];
                const double spanPpq = 4.0 * spanBars (st, (double) au.frames() / au.sampleRate, bpm);
                double u = srcPpq / spanPpq;
                u -= std::floor (u);
                const double target = u * (double) au.frames();
                const double vel = (double) au.frames() * ppqPerSample / spanPpq * (sp.reverse ? -1.0 : 1.0);
                const bool jump = sp.thread != lastThread || std::abs (target - (lastTarget + lastVel)) > std::max (2.0, 2.0 * std::abs (vel));
                lastThread = sp.thread;
                lastTarget = target;
                lastVel = vel;
                if (tape)
                {
                    if (jump || cur.thread != sp.thread)
                    {
                        if (cur.thread >= 0)
                        {
                            ghost = cur;
                            ghostLeft = fadeN;
                        }
                        cur.thread = sp.thread;
                    }
                    cur.frame = target;
                    cur.vel = vel;
                }
                else
                {
                    const double rate = au.sampleRate / sr * (sp.reverse ? -1.0 : 1.0);
                    bool any = false;
                    for (auto& g : grains)
                        any = any || g.on;
                    if (jump || ! any)
                    {
                        for (auto& g : grains)
                            if (g.on && g.fadeOut < 0)
                            {
                                g.fadeOut = fadeN;
                                g.fadeLen = fadeN;
                            }
                        spawn (sp.thread, target, rate, true);
                        sinceSpawn = 0;
                    }
                    else if (sinceSpawn >= grainLen / 2)
                    {
                        double start = target;
                        if (lastGrain >= 0 && grains[(size_t) lastGrain].on && grains[(size_t) lastGrain].thread == sp.thread)
                            start = align (au, grains[(size_t) lastGrain].pos, target, rate);
                        spawn (sp.thread, start, rate, false);
                        sinceSpawn = 0;
                    }
                    ++sinceSpawn;
                }
            }
        }
        else if (tape && cur.thread >= 0)
            cur.frame += cur.vel;                  // (stopped: the head runs on through the fade)

        float l = 0.0f, r = 0.0f;
        if (tape)
        {
            if (cur.thread >= 0 && cur.thread < count)
            {
                float cl, cr;
                read (*a[cur.thread], cur.frame, cl, cr);
                if (ghostLeft > 0 && ghost.thread >= 0 && ghost.thread < count)
                {
                    const float t = 1.0f - (float) ghostLeft / (float) fadeN;
                    const float gi = (float) std::sin (0.5 * pi * t), go = (float) std::cos (0.5 * pi * t);
                    float gl, gr;
                    read (*a[ghost.thread], ghost.frame, gl, gr);
                    l = cl * gi + gl * go;
                    r = cr * gi + gr * go;
                    ghost.frame += ghost.vel;
                    --ghostLeft;
                }
                else
                {
                    l = cl;
                    r = cr;
                }
            }
        }
        else
        {
            for (auto& g : grains)
            {
                if (! g.on)
                    continue;
                if (g.thread >= count || g.age >= g.len || g.fadeOut == 0)
                {
                    g.on = false;
                    continue;
                }
                const double s = std::sin (pi * (double) g.age / (double) g.len);
                float w = (float) (s * s);
                if (g.fadeIn > 0)
                {
                    w *= 1.0f - (float) g.fadeIn / (float) fadeN;
                    --g.fadeIn;
                }
                if (g.fadeOut > 0)
                {
                    w *= (float) g.fadeOut / (float) std::max (1, g.fadeLen);
                    --g.fadeOut;
                }
                float gl, gr;
                read (*a[g.thread], g.pos, gl, gr);
                l += gl * w;
                r += gr * w;
                g.pos += g.rate;
                ++g.age;
            }
        }
        const float k = st.gain * gate;
        L[i] += l * k;
        R[i] += r * k;
        levelSum += (l * l + r * r) * 0.5f * k * k;
        ++levelCount;
    }
    shownStep.store ((ck.running || ownRunning) && count > 0 ? shown : -1, std::memory_order_relaxed);
    if (levelCount >= 2048)
    {
        shownLevel.store (std::sqrt (levelSum / (float) levelCount), std::memory_order_relaxed);
        levelSum = 0.0f;
        levelCount = 0;
    }
}
}
