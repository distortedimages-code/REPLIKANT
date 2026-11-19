// SPDX-License-Identifier: MPL-2.0
// After MIFM1's mi_metal.h (Copyright (C) 2026 Yoann): the same network, its constants at 22.05 kHz scaled to the rate.
#include "Space.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
    const int kAp[4] = { 71, 53, 191, 139 };
    const int kLen[8] = { 1103, 1249, 1427, 1583, 1693, 1823, 1979, 2111 };
    const float kRate[8] = { 0.13f, 0.17f, 0.23f, 0.29f, 0.37f, 0.43f, 0.53f, 0.71f };
    const float kOut = 0.71f;

    int pow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }
    inline float flush (float x) { return std::abs (x) < 1.0e-15f ? 0.0f : x; }
    inline float soft (float x)
    {
        const float a = std::abs (x);
        return a < 1.5f ? x : (x > 0.0f ? 1.0f : -1.0f) * (1.5f + 0.5f * (a - 1.5f) / (0.5f + (a - 1.5f)));
    }
    inline void hadamard (float* v)
    {
        float a0 = v[0] + v[1], a1 = v[0] - v[1], a2 = v[2] + v[3], a3 = v[2] - v[3];
        float a4 = v[4] + v[5], a5 = v[4] - v[5], a6 = v[6] + v[7], a7 = v[6] - v[7];
        float b0 = a0 + a2, b2 = a0 - a2, b1 = a1 + a3, b3 = a1 - a3;
        float b4 = a4 + a6, b6 = a4 - a6, b5 = a5 + a7, b7 = a5 - a7;
        v[0] = b0 + b4, v[4] = b0 - b4, v[1] = b1 + b5, v[5] = b1 - b5;
        v[2] = b2 + b6, v[6] = b2 - b6, v[3] = b3 + b7, v[7] = b3 - b7;
    }
}

void Space::prepare (double rate)
{
    sr = rate;
    scaleRate = rate / 22050.0;
    for (int i = 0; i < 4; ++i)
    {
        apLen[i] = std::max (1, (int) std::lround (kAp[i] * scaleRate));
        ap[i].buf.assign ((size_t) pow2 (apLen[i] + 4), 0.0f);
        ap[i].mask = (int) ap[i].buf.size() - 1;
    }
    for (int i = 0; i < kLines; ++i)
    {
        len[i] = (float) (kLen[i] * scaleRate);
        lines[i].buf.assign ((size_t) pow2 ((int) (len[i] * 1.1f) + 64), 0.0f);
        lines[i].mask = (int) lines[i].buf.size() - 1;
        ph[i] = (float) i * 0.125f;
    }
    shim.buf.assign ((size_t) pow2 ((int) (2048 * scaleRate) + 8), 0.0f);
    shim.mask = (int) shim.buf.size() - 1;
    reset();
}

void Space::reset()
{
    for (auto* l : { &ap[0], &ap[1], &ap[2], &ap[3] })
        std::fill (l->buf.begin(), l->buf.end(), 0.0f);
    for (auto& l : lines)
        std::fill (l.buf.begin(), l.buf.end(), 0.0f);
    std::fill (shim.buf.begin(), shim.buf.end(), 0.0f);
    for (int i = 0; i < kLines; ++i)
        ay[i] = lp[i] = hp[i] = 0.0f;
    shD = 1.0f;
    shHp1 = shHp2 = shLp = inHp1 = inHp2 = 0.0f;
    scale = scaleAim = 0.03f + 0.97f * target.size * target.size;
    counter = 0;
}

void Space::block (int m)
{
    // the settings (MIFM1's Set)
    const float decay = std::clamp (target.decay, 0.0f, 1.0f), size = std::clamp (target.size, 0.0f, 1.0f),
                tone = std::clamp (target.tone, 0.0f, 1.0f);
    freeze = decay >= 0.97f;
    t60 = 0.3f * std::exp2 (decay * (6.643856f / 0.97f));
    inLevel = 1.0f - 0.45f * decay;
    scaleAim = 0.03f + 0.97f * size * size;
    hf = 0.0625f * std::exp2 (tone * 4.0f);
    diff = tone > 0.5f ? 0.7f - 0.5f * (tone - 0.5f) : 0.7f;
    sway = (tone > 0.5f ? 1.0f - 1.6f * (tone - 0.5f) : 1.0f) * 2.0f * std::clamp (target.mod, 0.0f, 1.0f);

    // the block's taps (MIFM1's Prepare)
    const float s0 = scale;
    scale += (scaleAim - scale) * (1.0f - std::exp (-(float) m / (float) (0.1 * sr)));
    const float s1 = scale;
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        const float len1 = len[i] * s1;
        const float depth = std::min (6.0f * (float) scaleRate, 0.05f * len1) * sway;
        const float m0 = depth * std::sin (6.2831853f * ph[i]);
        ph[i] += kRate[i] * (float) m / (float) sr;
        ph[i] -= ph[i] >= 1.0f ? 1.0f : 0.0f;
        const float m1 = depth * std::sin (6.2831853f * ph[i]);
        const float d = std::max (2.0f, 0.5f * (len[i] * s0 + m0 + len1 + m1));
        int di = (int) d;
        float f = d - (float) di;
        if (f < 0.1f) f += 1.0f, --di;
        ti[i] = std::clamp (di, 1, lines[i].mask - 2);
        eta[i] = (1.0f - f) / (1.0f + f);
        const float lg = -3.0f * d / ((float) sr * t60);
        g[i] = freeze ? 1.0f : std::pow (10.0f, lg);
        const float r = freeze ? 1.0f : std::pow (10.0f, lg * (1.0f / hf - 1.0f));
        pb[i] = (1.0f - r) / (1.0f + r);
        dc[i] = (float) (0.00002 * std::sqrt (d / scaleRate) / scaleRate);
        sum += g[i] * g[i];
    }
    gp = sum / (float) kLines;
}

void Space::process (float* L, float* R, int n)
{
    const float k = (float) std::min (1.0, 100.0 / sr);
    const float cIn = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 50.0 / sr));
    const float cSh = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 150.0 / sr));
    const float cShLp = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 4000.0 / sr));
    const float P = (float) (2046.0 * scaleRate);
    for (int i = 0; i < n; ++i)
    {
        if (counter <= 0)
        {
            block (16);
            counter = 16;
        }
        --counter;
        mix += (target.mix - mix) * k;
        shimAmt += (target.shimmer - shimAmt) * k;
        const float inAim = freeze ? 0.0f : inLevel;
        inGain += (inAim - inGain) * (float) (0.001 * 22050.0 / sr);

        const float dl = L[i], dr = R != nullptr ? R[i] : L[i];
        float x = 0.5f * (dl + dr) * inGain;
        inHp1 += cIn * (x - inHp1);
        x -= inHp1;
        inHp2 += cIn * (x - inHp2);
        x -= inHp2;
        for (int a = 0; a < 4; ++a)
        {
            const float dd = ap[a].read (apLen[a] - 1);
            const float w = x + diff * dd;
            ap[a].write (w);
            x = dd - diff * w;
        }
        const float shimFeed = freeze ? 0.0f : shimAmt * 1.8f * std::sqrt (1.0f - std::min (gp, 1.0f));
        if (shimFeed > 1.0e-4f)
        {
            // the octave: two taps sliding back through the line twice as fast as it is written, crossfaded
            shD -= 1.0f;
            if (shD < 1.0f) shD += P;
            float d2 = shD + 0.5f * P;
            if (d2 >= P + 1.0f) d2 -= P;
            const float w1 = 1.0f - std::abs ((shD - 1.0f) * (2.0f / P) - 1.0f);
            auto tap = [this] (float d)
            {
                const int i0 = (int) d;
                const float fr = d - (float) i0;
                const float a = shim.read (i0), b = shim.read (i0 + 1);
                return a + (b - a) * fr;
            };
            float o = tap (d2) + (tap (shD) - tap (d2)) * w1;
            shHp1 += cSh * (o - shHp1);
            o -= shHp1;
            shHp2 += cSh * (o - shHp2);
            x += shimFeed * (o - shHp2);
        }
        float y[kLines], v[kLines];
        for (int l = 0; l < kLines; ++l)
        {
            y[l] = eta[l] * (lines[l].read (ti[l] - 1) - ay[l]) + lines[l].read (ti[l]);
            ay[l] = y[l];
            lp[l] = y[l] + pb[l] * (lp[l] - y[l]);
            v[l] = lp[l];
        }
        hadamard (v);
        const float xi = x * 0.35355339f;
        for (int l = 0; l < kLines; ++l)
        {
            const float w = g[l] * v[l] * 0.35355339f + (((0x5Au >> l) & 1u) ? -xi : xi);
            hp[l] += dc[l] * (w - hp[l]);
            lines[l].write (soft (w - hp[l]));
        }
        const float l = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.35355339f;
        const float r = (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * 0.35355339f;
        shLp += cShLp * (l + r - shLp);
        shim.write (shLp);
        const float og = kOut * std::sqrt (scale * (1.0f / 0.379f));
        const float wl = l * og, wr = r * og;
        dryNow += (std::clamp (target.dry, 0.0f, 1.0f) - dryNow) * k;
        L[i] = dl * dryNow + wl * mix * 2.6f;
        if (R != nullptr)
            R[i] = dr * dryNow + wr * mix * 2.6f;
    }
    for (int l = 0; l < kLines; ++l)
        lp[l] = flush (lp[l]), hp[l] = flush (hp[l]), ay[l] = flush (ay[l]);
    inHp1 = flush (inHp1), inHp2 = flush (inHp2), shHp1 = flush (shHp1), shHp2 = flush (shHp2), shLp = flush (shLp);
    if (! (std::abs (lp[0]) < 1.0e3f))
        reset();
}
}
