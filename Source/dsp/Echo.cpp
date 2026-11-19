#include "Echo.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
    constexpr float kTwoPi = 6.28318530717958647692f;
    inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
    inline float uniform (uint32_t& s)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) ((double) s / 4294967295.0);
    }
}

void Echo::prepare (double sampleRate)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    int size = 1;
    while ((double) size < sr * 9.0)
        size <<= 1;
    for (auto& t : tape)
        t.assign ((size_t) size, 0.0f);
    mask = size - 1;
    w = 0;
    reset();
}

void Echo::reset()
{
    // (nothing cleared: what was written before is out of reach until written over)
    written = 0;
    time = clampf (target.time, 0.005f, 5.0f);
    mixNow = target.mix;
    fbNow = target.feedback;
    // (the heads straight to their places and levels: nothing glides from before)
    const int count = std::clamp (target.heads, 1, maxHeads);
    const float spread = clampf (target.spread, 0.0f, 1.0f);
    for (int k = 0; k < maxHeads; ++k)
    {
        auto& h = heads[(size_t) k];
        h = Head {};
        h.lfo = 0.25f * (float) k;
        h.jump = h.jumpTo = 1.0f;
        h.place = count == 1 ? 1.0f : 1.0f - spread * (float) (count - 1 - k) / (float) count;
        h.gain = k < count ? 1.0f / std::sqrt ((float) count) : 0.0f;
    }
    jumpClock = 0.0f;
    lpL = lpR = hpL = hpR = fbL = fbR = 0.0f;
}

// the tape `back` samples behind the write head (Catmull-Rom between the samples)
float Echo::read (int ch, float back) const
{
    if (back > (float) written - 3.0f)
        return 0.0f;                                   // (before the last reset: silence)
    const auto& t = tape[ch];
    const float pos = (float) w - back;
    const float fl = std::floor (pos);
    const float f = pos - fl;
    const int i = (int) fl;
    const float y0 = t[(size_t) ((i - 1) & mask)], y1 = t[(size_t) (i & mask)];
    const float y2 = t[(size_t) ((i + 1) & mask)], y3 = t[(size_t) ((i + 2) & mask)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

void Echo::process (float* L, float* R, int n)
{
    if (tape[0].empty() || n <= 0)
        return;
    const float fsr = (float) sr;
    const auto& s = target;
    const int count = std::clamp (s.heads, 1, maxHeads);
    const float spread = clampf (s.spread, 0.0f, 1.0f);
    const float sweep = clampf (s.sweep, 0.0f, 1.0f);
    const float wear = clampf (s.wear, 0.0f, 1.0f);
    const float width = clampf (s.width, 0.0f, 1.0f);
    const float timeAim = clampf (s.time, 0.005f, 5.0f);
    const float maxBack = (float) (mask - 8);

    // the block's: glides (the tape's speed in 0.25 s; places, levels, mix and feedback in 20 ms)
    const float kTime = 1.0f - std::exp (-1.0f / (0.25f * fsr));
    const float kFast = 1.0f - std::exp (-1.0f / (0.02f * fsr));
    const float kJump = 1.0f - std::exp (-1.0f / (0.006f * fsr));
    // the loop's filters: one-poles (TONE, WEAR), and how hard the tape saturates
    const float tone = clampf (s.tone, -1.0f, 1.0f);
    const float lpHz = std::min (0.45f * fsr, 14000.0f * std::exp2 (std::min (0.0f, tone) * 3.3f - wear * 1.6f));
    const float hpHz = 30.0f * std::exp2 (std::max (0.0f, tone) * 4.7f);
    const float aLp = 1.0f - std::exp (-kTwoPi * lpHz / fsr);
    const float aHp = 1.0f - std::exp (-kTwoPi * hpHz / fsr);
    const float drive = 1.0f + 2.0f * wear;
    const float fbAim = clampf (s.feedback, 0.0f, 1.1f);

    // each head: its place, its level, its pan, how fast it reads (ρ)
    float lastBack[maxHeads] {};
    float placeAim[maxHeads], gainAim[maxHeads], panL[maxHeads], panR[maxHeads], rho[maxHeads], side[maxHeads];
    const float pans[maxHeads] = { -1.0f, 1.0f, -0.45f, 0.45f };
    for (int k = 0; k < maxHeads; ++k)
    {
        placeAim[k] = count == 1 ? 1.0f : 1.0f - spread * (float) (count - 1 - k) / (float) count;
        gainAim[k] = k < count ? 1.0f / std::sqrt ((float) count) : 0.0f;
        const float p = count == 1 ? 0.0f : pans[k] * width;
        panL[k] = std::min (1.0f, 1.0f - p);
        panR[k] = std::min (1.0f, 1.0f + p);
        side[k] = (k % 2 == 0) ? 1.0f : -1.0f;
        const bool backwards = s.direction == 1 || (s.direction == 2 && k % 2 == 1);
        float r = 1.0f;
        if (s.motion == 2)
            r = std::exp2 (side[k] * sweep);            // SCAN: odd heads up, even heads down, an octave at most
        rho[k] = backwards ? -r : r;
    }
    const float wobbleStep = std::max (0.01f, s.rate) / fsr;
    const float jumpStep = std::max (0.01f, s.rate) / fsr;

    for (int i = 0; i < n; ++i)
    {
        time += (timeAim - time) * kTime;
        mixNow += (s.mix - mixNow) * kFast;
        fbNow += (fbAim - fbNow) * kFast;
        // the tape's speed, unsteady with WEAR (wow and flutter): every head the same
        wow += 0.55f / fsr;
        flutter += 7.3f / fsr;
        if (wow >= 1.0f) wow -= 1.0f;
        if (flutter >= 1.0f) flutter -= 1.0f;
        const float speed = 1.0f + wear * (0.0045f * std::sin (kTwoPi * wow) + 0.0012f * std::sin (kTwoPi * flutter));
        // JUMP: at RATE, every head somewhere else
        if (s.motion == 3)
        {
            jumpClock += jumpStep;
            if (jumpClock >= 1.0f)
            {
                jumpClock -= 1.0f;
                for (auto& h : heads)
                    h.jumpTo = 1.0f - 0.85f * sweep * uniform (rng);
            }
        }
        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < maxHeads; ++k)
        {
            auto& h = heads[(size_t) k];
            h.gain += (gainAim[k] - h.gain) * kFast;
            h.place += (placeAim[k] - h.place) * kFast;
            if (h.gain < 1.0e-5f && gainAim[k] <= 0.0f)
            {
                h.gain = 0.0f;
                continue;
            }
            // how far behind the write head it reads
            float back = h.place * time * fsr * speed;
            if (s.motion == 1)
            {
                h.lfo += wobbleStep;
                if (h.lfo >= 1.0f) h.lfo -= 1.0f;
                back *= 1.0f + 0.5f * sweep * side[k] * std::sin (kTwoPi * h.lfo);
            }
            if (s.motion == 3)
            {
                h.jump += (h.jumpTo - h.jump) * kJump;
                back *= h.jump;
            }
            else
                h.jump += (1.0f - h.jump) * kJump;
            back = clampf (back, 4.0f, maxBack);
            float l, r;
            const float travel = 1.0f - rho[k];          // how fast a grain drifts along the tape
            if (std::abs (travel) < 1.0e-4f)
            {
                l = read (0, back);
                r = read (1, back);
                h.phase = 0.0f;
            }
            else
            {
                // two grains: the distance swept by each stays within the tape (at least 4 samples behind)
                const float reach = std::abs (travel) * 0.5f;
                const float longest = (rho[k] < 0.0f ? 1.0f : 0.1f) * fsr;
                const float aim = clampf (std::min (longest, (back - 4.0f) / std::max (1.0e-3f, reach) * 0.95f), 0.01f * fsr, longest);
                h.period += (aim - h.period) * kFast;
                h.phase += 1.0f / std::max (16.0f, h.period);
                if (h.phase >= 1.0f) h.phase -= 1.0f;
                l = r = 0.0f;
                for (int gidx = 0; gidx < 2; ++gidx)
                {
                    float ph = h.phase + 0.5f * (float) gidx;
                    if (ph >= 1.0f) ph -= 1.0f;
                    const float win = std::sin (3.14159265f * ph);
                    const float d = clampf (back + travel * (ph - 0.5f) * h.period, 4.0f, maxBack);
                    l += win * win * read (0, d);
                    r += win * win * read (1, d);
                }
            }
            const float g = h.gain;
            outL += g * panL[k] * l;
            outR += g * panR[k] * r;
            lastBack[k] = back;
        }
        // back into the tape: TONE and WEAR, then the tape's saturation (it holds a loop past 100 %)
        lpL += (outL - lpL) * aLp;
        lpR += (outR - lpR) * aLp;
        hpL += (lpL - hpL) * aHp;
        hpR += (lpR - hpR) * aHp;
        fbL = std::tanh ((lpL - hpL) * fbNow * drive) / drive;
        fbR = std::tanh ((lpR - hpR) * fbNow * drive) / drive;
        const float inL = L[i], inR = R[i];
        w = (w + 1) & mask;
        written = std::min (written + 1, mask);
        tape[0][(size_t) w] = inL + fbL;
        tape[1][(size_t) w] = inR + fbR;
        if (! std::isfinite (tape[0][(size_t) w]) || ! std::isfinite (tape[1][(size_t) w]))
        {
            tape[0][(size_t) w] = tape[1][(size_t) w] = 0.0f;
            lpL = lpR = hpL = hpR = 0.0f;
        }
        L[i] = inL + mixNow * outL;
        R[i] = inR + mixNow * outR;
    }
    for (int k = 0; k < maxHeads; ++k)
    {
        shownLevel[(size_t) k].store (heads[(size_t) k].gain, std::memory_order_relaxed);
        shownAt[(size_t) k].store (lastBack[k] / std::max (1.0f, time * fsr), std::memory_order_relaxed);
    }
    shownSeconds.store (time, std::memory_order_relaxed);
}
}
