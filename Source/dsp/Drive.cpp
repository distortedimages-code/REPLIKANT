#include "Drive.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
    inline float fastTanh (float x)
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
    inline float onePole (float& s, float x, float a) { s += a * (x - s); return s; }
    inline float coef (double hz, double sr) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * hz / sr)); }

}

void Drive::prepare (double rate)
{
    sr = rate;
    for (auto& c : chans)
        c.os.prepare (4);
    bump.design (72.0, 0.9, 2.5, sr);
    reset();
    playing = DriveModel::off;
    duck = 1.0f;
    levIn = levOut = 0.0f;
    level = 1.0f;
}

void Drive::reset()
{
    for (auto& c : chans)
    {
        c.os.reset();
        c.lfIn = c.lfOut = c.pre = c.de = c.bump1 = c.bump2 = c.top = c.hyst = c.dcx = c.dcy = c.tilt = c.comp = 0.0f;
        std::fill (std::begin (c.wowBuf), std::end (c.wowBuf), 0.0f);
        c.wowPos = 0;
    }
}

float Drive::tick (Channel& c, float x, int ch)
{
    (void) ch;
    const float g = std::pow (10.0f, drive * 1.5f);           // 0 .. +30 dB into the curve
    const float makeup = 1.0f / (1.0f + 0.65f * drive) / std::max (1.0f, std::min (g, 1.6f));
    // (0.4.5) MIX blends inside, where the curve is: the clean part shares the oversampling and every filter, so a
    // half mix never combs (it did, the dry not delayed as the drive's path is); the colourings scale with it too
    const float m = mix;
    const float clean = 1.0f / (g * makeup);                  // (the curve's input back to the dry level, after makeup)
    const double srO = sr * 4.0;
    float y = x;
    switch (playing)
    {
        case DriveModel::preamp:
        {
            // the input transformer: the lows a little fatter as it is driven
            const float lf = onePole (c.lfIn, y, coef (110.0, sr));
            y += 0.3f * drive * m * lf;
            const float aLo = coef (160.0, srO);
            y = c.os.process (y * g, [&] (float a)
            {
                // class A: biased off centre (its second harmonic), slope 1 at rest
                constexpr float b = 0.28f, tb = 0.27290f, norm = 1.0f / (1.0f - tb * tb);
                float v = (fastTanh (a + b) - tb) * norm;
                // the output transformer: the lows saturate first
                const float lo = onePole (c.lfOut, v, aLo);
                v = v - lo + fastTanh (1.4f * lo) * (1.0f / 1.4f);
                return a * clean + m * (v - a * clean);
            });
            // the bias's DC out
            const float z = y - c.dcx + 0.9997f * c.dcy;
            c.dcx = y;
            c.dcy = z;
            y = z;
            break;
        }
        case DriveModel::console:
        {
            const float rel = coef (8.0, srO), att = coef (200.0, srO);
            const float sq = 0.35f * drive;
            y = c.os.process (y * g, [&] (float a)
            {
                float v = a / std::sqrt (1.0f + a * a);
                const float e = std::abs (v);
                c.comp += (e > c.comp ? att : rel) * (e - c.comp);
                v = v / (1.0f + sq * c.comp);
                return a * clean + m * (v - a * clean);
            });
            break;
        }
        case DriveModel::tape:
        {
            // wow and flutter: a delay swaying by a fraction of a millisecond, the same on both sides (a stereo image
            // that sways was heard as phasing), as deep as the tape is driven
            const int len = 2048;
            c.wowBuf[c.wowPos] = y;
            const double wow = (std::sin (wowPhase) * 0.00012 + std::sin (flutterPhase) * 0.00002) * (double) (drive * m);
            const double d = std::clamp ((0.0006 + wow) * sr, 1.0, (double) len - 3);
            const double rp = (double) c.wowPos - d;
            const int i0 = ((int) std::floor (rp) + len) % len;
            const float fr = (float) (rp - std::floor (rp));
            y = c.wowBuf[i0] + fr * (c.wowBuf[(i0 + 1) % len] - c.wowBuf[i0]);
            c.wowPos = (c.wowPos + 1) % len;
            // record pre-emphasis (the highs up), the tape, playback de-emphasis (its exact inverse: flat when clean)
            const float cp = coef (3000.0, sr), a = 1.0f;
            const float hp = y - onePole (c.pre, y, cp);
            y += a * hp;
            y = c.os.process (y * g, [&] (float u)
            {
                // a saturation with memory: the last state leans on the next (a hysteresis loop)
                const float hm = fastTanh (u + 0.35f * c.hyst);
                c.hyst = hm;
                return u * clean + m * (hm * 0.92f - u * clean);
            });
            {
                // y (1 + a) - a L(y) = x, L a one-pole low pass with its direct term: solved for y
                const float yd = (y + a * (1.0f - cp) * c.de) / (1.0f + a - a * cp);
                c.de = cp * yd + (1.0f - cp) * c.de;
                y = yd;
            }
            const float bumped = bump.tick (y, c.bump1, c.bump2);
            y += m * (onePole (c.top, bumped, coef (16000.0, sr)) - y);
            break;
        }
        case DriveModel::off:
            return x;
    }
    // tone: a tilt around 1 kHz, +-4 dB
    const float lpT = onePole (c.tilt, y, coef (1000.0, sr));
    y += m * ((tone - 0.5f) * 1.2f * (y - lpT) - (tone - 0.5f) * 0.6f * lpT);
    return y * makeup;
}

void Drive::process (float* L, float* R, int n)
{
    if (target.model == DriveModel::off)
    {
        drive = target.drive, tone = target.tone, mix = target.mix;
        playing = DriveModel::off;
        return;
    }
    if (playing == DriveModel::off)
    {
        playing = target.model;
        reset();
    }
    // (0.4.5) its knobs glide over 10 ms (the drive's gain is exponential: quick steps were heard), a model changed
    // while playing is ducked out in 2 ms and back in 4 (no click where one curve meets another), the level held
    // where it came in by a slow follower (a drive changes the tone, not the loudness)
    const float k = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    const float duckDown = (float) (1.0 / (0.002 * sr)), duckUp = (float) (1.0 / (0.004 * sr));
    const float levK = (float) (1.0 - std::exp (-1.0 / (0.4 * sr)));
    for (int i = 0; i < n; ++i)
    {
        drive += (target.drive - drive) * k;
        tone += (target.tone - tone) * k;
        mix += (target.mix - mix) * k;
        if (target.model != playing)
        {
            duck -= duckDown;
            if (duck <= 0.0f)
            {
                duck = 0.0f;
                playing = target.model;
                reset();
            }
        }
        else if (duck < 1.0f)
            duck = std::min (1.0f, duck + duckUp);
        wowPhase += 2.0 * 3.14159265358979 * 0.55 / sr;
        flutterPhase += 2.0 * 3.14159265358979 * 7.3 / sr;
        if (wowPhase > 6.283185307179586) wowPhase -= 6.283185307179586;
        if (flutterPhase > 6.283185307179586) flutterPhase -= 6.283185307179586;
        const float inL = L[i], inR = R != nullptr ? R[i] : L[i];
        float yl = tick (chans[0], inL, 0);
        float yr = R != nullptr ? tick (chans[1], inR, 1) : yl;
        levIn += levK * (inL * inL + inR * inR - levIn);
        levOut += levK * (yl * yl + yr * yr - levOut);
        if (levIn > 1.0e-6f && levOut > 1.0e-9f)
            level += 0.0005f * (std::clamp (std::sqrt (levIn / levOut), 0.5f, 2.0f) - level);
        const float gOut = level * duck;
        L[i] = yl * gOut;
        if (R != nullptr)
            R[i] = yr * gOut;
    }
}
}
