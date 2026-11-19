#include "Comp.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
    inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
    inline float dbToGain (float db)                   { return std::exp (db * 0.11512925464970229f); }
    inline float coef (double seconds, double sr)      { return (float) (1.0 - std::exp (-1.0 / std::max (1.0, seconds * sr))); }

    // AMOUNT: the threshold (dB), how much of what goes over it is taken (1: ∞:1, 2: 1:-1), the make-up (dB)
    struct Law { float threshold, slope, makeup; };
    Law lawOf (float amount)
    {
        const float a = clampf (amount, 0.0f, 1.0f);
        Law l;
        if (a <= 0.5f)
        {
            const float u = a / 0.5f;
            l.slope = 1.0f - (1.0f - u) * (1.0f - u);
            l.threshold = -30.0f * u;
        }
        else
        {
            // (the threshold back up as it goes past: only the hits pushed under, the rest left where it is)
            const float v = (a - 0.5f) / 0.5f;
            l.slope = 1.3f + 0.7f * v;
            l.threshold = -30.0f + 16.0f * v;
        }
        l.makeup = 0.45f * std::min (1.0f, l.slope) * -l.threshold;
        return l;
    }

    constexpr float kKnee = 6.0f;
    // what is taken (dB) from a level `over` dB above the threshold
    inline float taken (const Law& l, float over)
    {
        if (over <= -0.5f * kKnee)
            return 0.0f;
        if (over < 0.5f * kKnee)
        {
            const float k = over + 0.5f * kKnee;
            return l.slope * k * k / (2.0f * kKnee);
        }
        return l.slope * over;
    }

    constexpr float attacks[6] = { 0.00005f, 0.0003f, 0.001f, 0.003f, 0.01f, 0.03f };
    constexpr float releases[5] = { 0.02f, 0.06f, 0.15f, 0.4f, 1.0f };
}

float Comp::curve (const CompSettings& s, float inDb)
{
    const auto l = lawOf (s.amount);
    return inDb - std::min (60.0f, taken (l, inDb - l.threshold)) + l.makeup;
}

void Comp::prepare (double sampleRate)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
}

void Comp::reset()
{
    grFast = grSlow = 0.0f;
    gate = 1.0f;
    gateHold = 0.0f;
    inG = dbToGain (clampf (target.inDb, 0.0f, 18.0f));
    outG = dbToGain (clampf (target.levelDb, -24.0f, 12.0f));
    mixNow = clampf (target.mix, 0.0f, 1.0f);
    driveNow = clampf (target.drive, 0.0f, 1.0f);
    amountNow = clampf (target.amount, 0.0f, 1.0f);
    env = 0.0f;
    inPow = outPow = 0.0f;
    match = 1.0f;
}

void Comp::process (float* L, float* R, int n, const float* keyL, const float* keyR)
{
    const auto& s = target;
    const float kGlide = coef (0.02, sr);
    const float aAtt = coef (attacks[std::clamp (s.attack, 0, 5)], sr);
    const bool autoRelease = s.release >= 5;
    const float aRel = coef (autoRelease ? 0.06f : releases[std::clamp (s.release, 0, 4)], sr);
    const float aSlowAtt = coef (0.08, sr), aSlowRel = coef (1.2, sr);
    const float envRel = (float) std::exp (-1.0 / (0.012 * sr));          // the detector: a peak, let go in 12 ms
    const float gateOpen = coef (0.0003, sr), gateClose = coef (0.06 / 6.9, sr);   // (closed: 60 dB down in 60 ms)
    const float holdLen = (float) (0.03 * sr);
    const float inAim = dbToGain (clampf (s.inDb, 0.0f, 18.0f)), outAim = dbToGain (clampf (s.levelDb, -24.0f, 12.0f));
    const float gateDb = clampf (s.gateDb, -80.0f, 0.0f);
    const float kPow = coef (0.4, sr), kMatch = coef (1.0, sr);
    const bool gating = gateDb > -79.5f;
    float peak = -100.0f, most = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        inG += (inAim - inG) * kGlide;
        outG += (outAim - outG) * kGlide;
        mixNow += (clampf (s.mix, 0.0f, 1.0f) - mixNow) * kGlide;
        driveNow += (clampf (s.drive, 0.0f, 1.0f) - driveNow) * kGlide;
        amountNow += (clampf (s.amount, 0.0f, 1.0f) - amountNow) * kGlide;
        const float xl = L[i], xr = R[i];
        // what it listens to (after IN), its level
        const float kl = (keyL != nullptr ? keyL[i] : xl) * inG;
        const float kr = (keyR != nullptr ? keyR[i] : (keyL != nullptr ? keyL[i] : xr)) * inG;
        const float a = std::max (std::abs (kl), std::abs (kr));
        env = a > env ? a : env * envRel;
        const float levelDb = 20.0f * std::log10 (env + 1.0e-9f);
        peak = std::max (peak, levelDb);
        // the gate (held 30 ms open after the level falls under it)
        if (gating)
        {
            if (levelDb >= gateDb)
            {
                gateHold = holdLen;
                gate += (1.0f - gate) * gateOpen;
            }
            else if (gateHold > 0.0f)
                gateHold -= 1.0f;
            else
            {
                gate += (0.0f - gate) * gateClose;
                if (gate < 1.0e-5f)
                    gate = 0.0f;
            }
        }
        else
            gate += (1.0f - gate) * gateOpen;
        // the gain taken: quick (ATTACK, RELEASE), and with AUTO a slow one under it
        const auto law = lawOf (amountNow);
        const float gr = std::min (60.0f, taken (law, levelDb - law.threshold));
        grFast += (gr - grFast) * (gr > grFast ? aAtt : aRel);
        float g = grFast;
        if (autoRelease)
        {
            grSlow += (gr - grSlow) * (gr > grSlow ? aSlowAtt : aSlowRel);
            g = std::max (grFast, std::min (gr + 6.0f, grSlow));
        }
        most = std::max (most, g);
        const float gain = dbToGain (law.makeup - g) * gate * inG;
        float yl = xl * gain, yr = xr * gain;
        // DRIVE: tanh, the small signals brought up as it pushes (up to +9 dB)
        if (driveNow > 1.0e-3f)
        {
            const float gd = dbToGain (18.0f * driveNow), back = 1.0f / std::sqrt (gd);
            yl = std::tanh (yl * gd) * back;
            yr = std::tanh (yr * gd) * back;
        }
        // the loudness kept: what comes out brought slowly back to the sound's own (the pumping stays, the level
        // doesn't run off; held in the silences)
        inPow += (xl * xl + xr * xr - inPow) * kPow;
        outPow += (yl * yl + yr * yr - outPow) * kPow;
        if (inPow > 1.0e-7f && outPow > 1.0e-9f)
            match += (clampf (std::sqrt (inPow / outPow), 0.25f, 8.0f) - match) * kMatch;
        yl *= match;
        yr *= match;
        L[i] = (xl + mixNow * (yl - xl)) * outG;
        R[i] = (xr + mixNow * (yr - xr)) * outG;
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i]))
        {
            L[i] = R[i] = 0.0f;
            reset();
        }
    }
    shownReduction.store (most, std::memory_order_relaxed);
    shownLevel.store (peak, std::memory_order_relaxed);
    shownGate.store (gate, std::memory_order_relaxed);
}
}
