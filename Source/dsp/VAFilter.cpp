// SPDX-License-Identifier: MPL-2.0
// The MOOG, MS-20 and SEM models are MIFM1's (Copyright (C) 2026 Yoann, MPL-2.0: mi_filter.cc), moved to float at
// any rate, a sample at a time; OSCAR, WASP and POLIVOKS are new here, on the same building blocks.
#include "VAFilter.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace dsp
{
namespace
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kInvVt2 = 1.0f / (2.0f * 0.312f);   // the ladder's transistors' thermal voltage, twice

    inline float tanPade (float x)          // tan on 0 .. 1.32 (a cutoff up to 0.42 of the rate)
    {
        const float x2 = x * x;
        return x * (15.0f - x2) / (15.0f - 6.0f * x2);
    }
    // a transistor pair's curve: a cubic with tanh's slope at 0, flat from 1.5 (MIFM1's)
    inline float sat (float x)
    {
        if (x > 1.5f) return 1.0f;
        if (x < -1.5f) return -1.0f;
        return x - (4.0f / 27.0f) * x * x * x;
    }
    inline float fastTanh (float x)
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
    inline float soft (float y)             // the output within +-2: as it is under 1, then bending
    {
        const float a = std::abs (y);
        if (a <= 1.0f) return y;
        const float b = 2.0f - 1.0f / a;
        return y < 0.0f ? -b : b;
    }
    // an OTA's gain at what it gets (sat(v) / v): 1 while small
    inline float otaGain (float v)
    {
        const float v2 = v * v;
        return v2 <= 2.25f ? 1.0f - (4.0f / 27.0f) * v2 : 1.0f / std::sqrt (v2);
    }
    // a CMOS inverter's gain (the Wasp): lopsided (biased off its centre), its slope 1 at 0
    inline float cmosGain (float v)
    {
        constexpr float b = 0.3f, t0 = 0.29131261f, norm = 1.0f / (1.6f * (1.0f - t0 * t0));
        if (std::abs (v) < 1.0e-4f) return 1.0f;
        return (fastTanh (1.6f * v + b) - t0) * norm / v;
    }

    // Zavalishin's zero-delay feedback state variable filter: band pass returned, low and high pass out
    inline float svf (float* s, float x, float g, float a0, float rho, float& lp, float& hp)
    {
        const float h = a0 * (x - rho * s[0] - s[1]);
        const float b = g * h + s[0];
        const float l = g * b + s[1];
        s[0] = g * h + b;
        s[1] = g * b + l;
        lp = l;
        hp = h;
        return b;
    }
    // the same with its two integrators saturating (each at its gain for what it got the sample before)
    template <typename Gain>
    inline float svfNl (float* s, float* p, float x, float g, float rho, float k, Gain gain, float& lp, float& hp)
    {
        const float g1 = g * gain (k * p[0]), g2 = g * gain (k * p[1]), r = rho - g + g2;
        const float h = (x - r * s[0] - s[1]) / (1.0f + g1 * r);
        const float b = g1 * h + s[0];
        const float l = g2 * b + s[1];
        s[0] = g1 * h + b;
        s[1] = g2 * b + l;
        p[0] = h;
        p[1] = b;
        lp = l;
        hp = h;
        return b;
    }

    // the ladder's stages' g at w = pi f / fs, and in kc its feedback's scale (MIFM1's ladder_g: its loop rings at
    // its cutoff, within a cent from 15 Hz to 4 kHz at 44.1 kHz)
    inline float ladderG (float w, float& kc)
    {
        const float wk = 0.284952f, ak = 0.250230f;
        const float a = w <= wk ? w * (0.999615f + w * (-0.973237f + w * (2.37433f - 1.59619f * w)))
                                : 0.3f - (0.3f - ak) * std::exp2 (-25.3813f * (w - wk));
        kc = 0.998467f + a * (-1.84811f + a * (-10.4595f + a * (32.3894f - 21.0662f * a)));
        return a * (2.0f * 0.312f);
    }

    // one Korg 35 (MS-20) section: hp = high pass, else low pass; the loop's saturation by st
    struct K35
    {
        float G, a0, b2, b3, K;
    };
    inline K35 k35coefs (float g, float K, bool hp)
    {
        K35 c;
        const float G = g / (1.0f + g), ig = 1.0f / (1.0f + g);
        c.G = G;
        c.K = K;
        c.a0 = 1.0f / (1.0f - K * G + K * G * G);
        if (! hp) { c.b2 = (K - K * G) * ig; c.b3 = -ig; }
        else      { c.b2 = -G * ig;          c.b3 = ig; }
        return c;
    }
    inline float k35tick (const K35& c, float* s, bool hp, float in, float st, float ist)
    {
        const float G = c.G;
        float v1 = (in - s[0]) * G, l1 = v1 + s[0];
        s[0] = l1 + v1;
        float u = c.a0 * ((hp ? in - l1 : l1) + c.b2 * s[1] + c.b3 * s[2]);
        u = sat (st * u) * ist;
        float y, y2;
        if (hp)
        {
            y = c.K * u;
            const float v2 = (y - s[1]) * G, l2 = v2 + s[1];
            s[1] = l2 + v2;
            y2 = y - l2;
            y = u;                          // (its output: the high pass before the loop's gain)
        }
        else
        {
            const float v2 = (u - s[1]) * G;
            y2 = v2 + s[1];
            s[1] = y2 + v2;
            y = y2;                         // (its output, K y2 over K)
            y2 = c.K * y2;
        }
        const float v3 = (y2 - s[2]) * G, l3 = v3 + s[2];
        s[2] = l3 + v3;
        return y;
    }
}

float VAFilter::fmDepth (float k)
{
    const float a = std::abs (k), d = (std::exp (4.0f * a) - 1.0f) * (1.0f / 53.59815f);
    return k < 0.0f ? -d : d;
}

void VAFilter::prepare (double rate)
{
    sr = rate;
    invSr = 1.0 / rate;
    reset();
}

void VAFilter::reset()
{
    for (int i = 0; i < 4; ++i)
        V[i] = dV[i] = tV[i] = sv[i] = nl[i] = 0.0f;
    for (int i = 0; i < 3; ++i)
        k35a[i] = k35b[i] = 0.0f;
    du1 = dcx = dcy = y1 = 0.0f;
    dstale = true;
    playingOn = false;
    for (auto& c : cs)
        c.s1 = c.s2 = 0.0f;
    madeCube = -1;
    cubeWait = 0;
    cx = -1.0f;
}

void VAFilter::set (const FilterSettings& s)
{
    // (0.4.5) a jump while it plays (another model, type or cube): ducked through silence, 1 ms down, 3 ms up,
    // instead of a click; the continuous settings keep moving meanwhile
    const bool jump = s.model != target.model || s.type != target.type || (s.model == FilterModel::cube && s.cube != target.cube);
    if (jump && playingOn && target.model != FilterModel::off && s.model != FilterModel::off)
    {
        pending = s;
        hasPending = true;
        target.res = s.res;
        target.drive = s.drive;
        target.sep = s.sep;
        target.fm = s.fm;
        target.y = s.y;
        target.z = s.z;
        target.smoothMs = s.smoothMs;
        target.trimDb = s.trimDb;
        return;
    }
    hasPending = false;
    apply (s);
}

void VAFilter::apply (const FilterSettings& s)
{
    if (s.model != target.model)
    {
        reset();
        res = s.res, drive = s.drive, sep = s.sep, fmK = s.fm;
        cy = s.y, cz = s.z, cx = -1.0f;
        trim = std::pow (10.0f, std::clamp (s.trimDb, -24.0f, 12.0f) / 20.0f);
    }
    target = s;
}

float VAFilter::tick (float x, float oct)
{
    if (target.model == FilterModel::off)
        return x;
    playingOn = true;
    if (hasPending)
    {
        duck -= (float) (1000.0 * invSr);
        if (duck <= 0.0f)
        {
            duck = 0.0f;
            hasPending = false;
            apply (pending);
            if (target.model == FilterModel::off)
                return 0.0f;
        }
    }
    else if (duck < 1.0f)
        duck = std::min (1.0f, duck + (float) (333.0 * invSr));
    if (duck < 1.0f)
        return duck * tickNow (x, oct);
    return tickNow (x, oct);
}

float VAFilter::tickNow (float x, float oct)
{
    // the knobs glide (~3 ms at any rate)
    const float k = (float) std::min (1.0, 330.0 * invSr);
    res += (target.res - res) * k;
    drive += (target.drive - drive) * k;
    sep += (target.sep - sep) * k;
    fmK += (target.fm - fmK) * k;
    if (target.model == FilterModel::cube)
    {
        // (X, Y, Z glide by SMOOTH; X from the cutoff's octaves, as they come)
        const float ks = target.smoothMs < 0.3f ? 1.0f : 1.0f - std::exp (-1000.0f / (target.smoothMs * (float) sr));
        const float xNow = std::clamp (oct * (1.0f / 9.965784f), 0.0f, 1.0f);
        cx = cx < 0.0f ? xNow : cx + (xNow - cx) * ks;
        cy += (target.y - cy) * std::max (ks, 0.0f);
        cz += (target.z - cz) * std::max (ks, 0.0f);
        trim += (std::pow (10.0f, std::clamp (target.trimDb, -24.0f, 12.0f) / 20.0f) - trim) * k;
        float y = cubeTick (x, cx) * trim;
        if (! (y == y) || std::abs (y) > 1.0e6f)
        {
            reset();
            y = 0.0f;
        }
        y = soft (y);
        y1 = y;
        return y;
    }
    if (fmK > 1.0e-4f)
        oct += fmDepth (fmK) * 5.0f * y1;
    oct = std::clamp (oct, -0.5f, 10.3f);
    const float hz = std::min (20.0f * std::exp2 (oct), 0.42f * (float) sr);
    float y = 0.0f;
    switch (target.model)
    {
        case FilterModel::moog:     y = moog (x, hz); break;
        case FilterModel::ms20:     y = ms20 (x, hz); break;
        case FilterModel::sem:      y = sem (x, hz); break;
        case FilterModel::oscar:    y = oscar (x, hz); break;
        case FilterModel::wasp:     y = wasp (x, hz); break;
        case FilterModel::polivoks: y = polivoks (x, hz); break;
        case FilterModel::off:
        case FilterModel::cube:     y = x; break;
    }
    if (! (y == y) || std::abs (y) > 1.0e6f)
    {
        reset();
        y = 0.0f;
    }
    y = soft (y);
    y1 = y;
    return y;
}

float VAFilter::moog (float in, float hz)
{
    float kc;
    const float g = ladderG (kPi * hz * (float) invSr, kc);
    const float r4 = 4.2f * res, dk = 1.0f + 15.0f * drive * drive;
    const float lc = 1.0f / (1.0f + 1.4f * drive + 1.6f * drive * drive);
    const float u = dk * in - r4 * kc * V[3];
    float d = g * (sat (u * kInvVt2) - tV[0]);
    V[0] += d + dV[0]; dV[0] = d; tV[0] = sat (V[0] * kInvVt2);
    d = g * (tV[0] - tV[1]);
    V[1] += d + dV[1]; dV[1] = d; tV[1] = sat (V[1] * kInvVt2);
    d = g * (tV[1] - tV[2]);
    V[2] += d + dV[2]; dV[2] = d; tV[2] = sat (V[2] * kInvVt2);
    d = g * (tV[2] - tV[3]);
    V[3] += d + dV[3]; dV[3] = d; tV[3] = sat (V[3] * kInvVt2);
    // the outputs mixed from its stages (the Oberheim Xpander's)
    switch (target.type)
    {
        case FilterType::lp:    return lc * (1.0f + 0.35f * r4 * kc) * V[3];
        case FilterType::hp:    return lc * (u - 4.0f * V[0] + 6.0f * V[1] - 4.0f * V[2] + V[3]);
        case FilterType::bp:    return lc * (4.0f * V[1] - 8.0f * V[2] + 4.0f * V[3]);
        case FilterType::notch: return lc * (u - 4.0f * V[0] + 6.0f * V[1] - 4.0f * V[2] + 2.0f * V[3]);
    }
    return 0.0f;
}

// both of the MS-20's filters in series, a high pass then a low pass
float VAFilter::ms20 (float in, float hz)
{
    const float top = 0.42f * (float) sr;
    float hpHz, lpHz;
    switch (target.type)
    {
        case FilterType::hp:    hpHz = hz; lpHz = std::min (top, hz * std::exp2 ((1.0f - sep) * 8.0f)); break;
        case FilterType::bp:
        case FilterType::notch: hpHz = hz * std::exp2 (-sep * 2.0f); lpHz = std::min (top, hz * std::exp2 (sep * 2.0f)); break;
        case FilterType::lp:
        default:                hpHz = 20.0f * std::pow (std::max (1.0f, hz / 20.0f), sep); lpHz = hz; break;
    }
    const float st = 1.0f + 7.0f * drive, ist = 1.0f / st, mk = 1.0f + 0.6f * (st - 1.0f);
    const float Kl = 0.01f + 2.2f * res, Kh = 0.01f + 1.9f * res;
    const auto ch = k35coefs (tanPade (kPi * std::min (hpHz, top) * (float) invSr), Kh, true);
    const auto cl = k35coefs (tanPade (kPi * lpHz * (float) invSr), Kl, false);
    const float h = k35tick (ch, k35a, true, in, st, ist);
    const float l = k35tick (cl, k35b, false, h, st, ist);
    return (target.type == FilterType::notch ? in - l : l) * mk;
}

float VAFilter::sem (float in, float hz)
{
    const float g = tanPade (kPi * hz * (float) invSr);
    const float r = 0.5f / (0.5f + 12.0f * res * res * res), r2 = 2.0f * r;
    const float a0 = 1.0f / (1.0f + r2 * g + g * g), rho = r2 + g;
    const float kk = 6.0f * drive;
    if (kk > 0.0f)
    {
        // the input stage: a transistor a little off centre (even harmonics with the odd), its DC taken out
        const float dg = 1.0f + 7.0f * drive, bias = 0.3f * drive, sb = sat (bias), lvl = 1.0f / (1.0f + 0.08f * (dg - 1.0f));
        const float u = dg * in + bias;
        float y = dstale ? sat (u) : sat (0.5f * (u + du1));
        dstale = false;
        du1 = u;
        y -= sb;
        const float z = y - dcx + 0.9995f * dcy;
        dcx = y;
        dcy = z;
        in = z * lvl;
    }
    else
        dstale = true;
    float lp, hp;
    const float bp = kk > 0.0f ? svfNl (sv, nl, in, g, rho, kk, otaGain, lp, hp) : svf (sv, in, g, a0, rho, lp, hp);
    switch (target.type)
    {
        case FilterType::lp:    return lp;
        case FilterType::hp:    return hp;
        case FilterType::bp:    return bp * r2 * (1.0f + 2.0f * res);
        case FilterType::notch: return lp + hp;
    }
    return lp;
}

float VAFilter::oscar (float in, float hz)
{
    const float top = 0.42f * (float) sr;
    const float spread = std::exp2 (sep * 2.0f);
    const float f1 = std::max (10.0f, hz / spread), f2 = std::min (top, hz * spread);
    const float g1 = tanPade (kPi * f1 * (float) invSr), g2 = tanPade (kPi * f2 * (float) invSr);
    const float q = 0.5f + 28.0f * res * res;
    const float r2 = 1.0f / q;
    const float rho1 = r2 + g1, rho2 = r2 + g2;
    const float kk = 1.2f + 5.0f * drive;
    in *= 1.0f + 2.0f * drive;
    float l1, h1, l2, h2, y;
    switch (target.type)
    {
        case FilterType::hp:
            svfNl (sv, nl, in, g1, rho1, kk, otaGain, l1, h1);
            svfNl (sv + 2, nl + 2, h1, g2, rho2, kk, otaGain, l2, h2);
            y = h2;
            break;
        case FilterType::bp:
            svfNl (sv, nl, in, g1, rho1, kk, otaGain, l1, h1);
            svfNl (sv + 2, nl + 2, h1, g2, rho2, kk, otaGain, l2, h2);
            y = l2;
            break;
        case FilterType::notch:
            svfNl (sv, nl, in, g1, rho1, kk, otaGain, l1, h1);
            svfNl (sv + 2, nl + 2, in, g2, rho2, kk, otaGain, l2, h2);
            y = l1 + h2;
            break;
        case FilterType::lp:
        default:
            svfNl (sv, nl, in, g1, rho1, kk, otaGain, l1, h1);
            svfNl (sv + 2, nl + 2, l1, g2, rho2, kk, otaGain, l2, h2);
            y = l2;
            break;
    }
    return y / (1.0f + 1.2f * drive);
}

float VAFilter::wasp (float in, float hz)
{
    const float g = tanPade (kPi * hz * (float) invSr);
    const float q = 0.7f + 22.0f * res * res;
    const float rho = 1.0f / q + g;
    // the inverters: pushed a little even at rest (their character), much more with DRIVE
    const float kk = 1.4f + 6.0f * drive;
    const float pre = 1.0f + 3.0f * drive;
    // the input through an inverter too (its lopsided curve: even harmonics), its DC out
    const float u = pre * in;
    float y0 = u * cmosGain (u);
    const float z = y0 - dcx + 0.9995f * dcy;
    dcx = y0;
    dcy = z;
    float lp, hp;
    const float bp = svfNl (sv, nl, z, g, rho, kk, cmosGain, lp, hp);
    const float lvl = 1.0f / (1.0f + 0.5f * drive);
    switch (target.type)
    {
        case FilterType::lp:    return lp * lvl;
        case FilterType::hp:    return hp * lvl;
        case FilterType::bp:    return bp * lvl * (1.0f / q) * 2.0f;
        case FilterType::notch: return (lp + hp) * lvl;
    }
    return lp;
}

float VAFilter::polivoks (float in, float hz)
{
    const float g = tanPade (kPi * hz * (float) invSr);
    // its damping goes past zero: it sings by itself, its integrators' clipping holding it
    const float r = 1.0f - 1.08f * std::pow (res, 1.3f);
    const float r2 = 2.0f * r;
    const float a0 = 1.0f / std::max (0.05f, 1.0f + r2 * g + g * g), rho = r2 + g;
    float lp, hp;
    const float bp = svf (sv, in * (1.0f + 3.0f * drive), g, a0, rho, lp, hp);
    // the op-amps' rails: the integrators' states clipped (a hard knee, a little give past it)
    auto rail = [] (float v)
    {
        const float a = std::abs (v);
        if (a <= 1.0f) return v;
        const float c = 1.0f + 0.25f * fastTanh ((a - 1.0f) * 4.0f);
        return v < 0.0f ? -c : c;
    };
    sv[0] = rail (sv[0]);
    sv[1] = rail (sv[1]);
    const float lvl = 0.8f / (1.0f + 0.6f * drive);
    switch (target.type)
    {
        case FilterType::lp:    return lp * lvl;
        case FilterType::hp:    return hp * lvl;
        case FilterType::bp:    return bp * lvl * std::max (0.2f, std::abs (r2));
        case FilterType::notch: return (lp + hp) * lvl;
    }
    return lp;
}

// CUBE: the sections made again (twice every 48 kHz sample's time ... at most, when X, Y, Z, RESO or the cube moved) from the point in the cube,
// each a state variable filter (Zavalishin's, its frequency prewarped) whose three outputs are mixed into its zeros;
// DRIVE before them, a soft clip after the third and the sixth
float VAFilter::cubeTick (float in, float px)
{
    if (--cubeWait <= 0)
    {
        cubeWait = std::clamp ((int) (sr / 24000.0), 2, 16);     // (every 2 samples at 48 kHz, 8 at 192)
        if (target.cube != madeCube || std::abs (px - madeX) > 1.0e-5f || std::abs (cy - madeY) > 1.0e-5f || std::abs (cz - madeZ) > 1.0e-5f
            || std::abs (res - madeRes) > 1.0e-5f)
        {
            madeCube = target.cube;
            madeX = px;
            madeY = cy;
            madeZ = cz;
            madeRes = res;
            cube::Point p;
            cube::at (madeCube, px, cy, cz, res, p);
            const float top = std::min (20000.0f, 0.45f * (float) sr);
            for (int s = 0; s < cube::sections; ++s)
            {
                auto& c = cs[s];
                const float g = std::tan (kPi * std::min (p.hz[s], top) * (float) invSr);
                c.k = 1.0f / p.q[s];
                c.a1 = 1.0f / (1.0f + g * (g + c.k));
                c.a2 = g * c.a1;
                c.a3 = g * c.a2;
                c.mh = p.mh[s];
                c.mb = p.mb[s];
                c.ml = p.ml[s];
            }
            cubeGain = p.gain;
        }
    }
    const float d = 1.0f + 6.0f * drive * drive;
    float x = in * d * cubeGain;
    for (int s = 0; s < cube::sections; ++s)
    {
        auto& c = cs[s];
        const float v3 = x - c.s2;
        const float v1 = c.a1 * c.s1 + c.a2 * v3;
        const float v2 = c.s2 + c.a2 * c.s1 + c.a3 * v3;
        c.s1 = 2.0f * v1 - c.s1;
        c.s2 = 2.0f * v2 - c.s2;
        x = c.mh * (x - c.k * v1 - v2) + c.mb * v1 + c.ml * v2;
        if (s == 2 || s == 5)
            x = 3.0f * fastTanh (x * (1.0f / 3.0f));        // (bending only past +-1.5: the resonances held)
    }
    return x / std::sqrt (d);
}

float VAFilter::responseDb (const FilterSettings& s, float fc, float f)
{
    if (s.model == FilterModel::cube)
        return (float) cube::responseDb (s.cube, std::log2 (std::max (20.0f, fc) / 20.0f) / 9.965784f, s.y, s.z, s.res, f);
    using C = std::complex<double>;
    auto sec = [] (double fcut, double hz, double q, FilterType t)
    {
        const C sv (0.0, hz / std::max (1.0, fcut));
        const C den = sv * sv + sv / q + 1.0;
        switch (t)
        {
            case FilterType::hp:    return sv * sv / den;
            case FilterType::bp:    return (sv / q) / den;
            case FilterType::notch: return (sv * sv + 1.0) / den;
            case FilterType::lp:
            default:                return C (1.0) / den;
        }
    };
    C h (1.0);
    switch (s.model)
    {
        case FilterModel::off:
            return 0.0f;
        case FilterModel::moog:
        {
            const C sv (0.0, f / std::max (1.0f, fc));
            const C one (1.0);
            const double k = 4.0 * 0.98 * s.res;
            const C p = (one + sv) * (one + sv) * (one + sv) * (one + sv);
            h = s.type == FilterType::lp ? C (1.0 + 0.35 * k) / (p + k)
              : s.type == FilterType::hp ? (sv * sv * sv * sv) / (p + k)
              : s.type == FilterType::bp ? (4.0 * sv * sv) / (p + k)
              : (sv * sv * sv * sv + 1.0) / (p + k);
            break;
        }
        case FilterModel::ms20:
        {
            const double q = 0.5 + 6.0 * s.res;
            if (s.type == FilterType::hp)
                h = sec (fc, f, q, FilterType::hp) * sec (fc * std::exp2 ((1.0 - s.sep) * 8.0), f, 0.7, FilterType::lp);
            else if (s.type == FilterType::bp || s.type == FilterType::notch)
            {
                h = sec (fc * std::exp2 (-s.sep * 2.0), f, q, FilterType::hp) * sec (fc * std::exp2 (s.sep * 2.0), f, q, FilterType::lp);
                if (s.type == FilterType::notch) h = 1.0 - h;
            }
            else
                h = sec (20.0 * std::pow (std::max (1.0, fc / 20.0), (double) s.sep), f, 0.5 + 4.0 * s.res, FilterType::hp) * sec (fc, f, q, FilterType::lp);
            break;
        }
        case FilterModel::oscar:
        {
            const double q = 0.5 + 28.0 * s.res * s.res, spread = std::exp2 (s.sep * 2.0);
            if (s.type == FilterType::notch)
                h = sec (fc / spread, f, q, FilterType::lp) + sec (fc * spread, f, q, FilterType::hp);
            else if (s.type == FilterType::bp)
                h = sec (fc / spread, f, q, FilterType::hp) * sec (fc * spread, f, q, FilterType::lp);
            else
                h = sec (fc / spread, f, q, s.type) * sec (fc * spread, f, q, s.type);
            break;
        }
        case FilterModel::sem:
            h = sec (fc, f, 1.0 / (2.0 * 0.5 / (0.5 + 12.0 * s.res * s.res * s.res)), s.type);
            break;
        case FilterModel::wasp:
            h = sec (fc, f, 0.7 + 22.0 * s.res * s.res, s.type);
            break;
        case FilterModel::polivoks:
            h = sec (fc, f, 0.5 / std::max (0.02, 1.0 - 1.08 * std::pow ((double) s.res, 1.3)), s.type);
            break;
    }
    return (float) (20.0 * std::log10 (std::max (1.0e-6, std::abs (h))));
}
}
