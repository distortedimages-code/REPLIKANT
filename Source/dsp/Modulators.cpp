#include "Modulators.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
const Division* divisions()
{
    static const Division d[] = {
        { "4 MES", 16.0 }, { "2 MES", 8.0 }, { "1 MES", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 },
        { "1/16", 0.25 }, { "1/32", 0.125 },
        { "1/2 .", 3.0 }, { "1/4 .", 1.5 }, { "1/8 .", 0.75 }, { "1/16 .", 0.375 },
        { "1/2 T", 4.0 / 3.0 }, { "1/4 T", 2.0 / 3.0 }, { "1/8 T", 1.0 / 3.0 }, { "1/16 T", 1.0 / 6.0 },
    };
    return d;
}

int numDivisions()   { return 16; }

Modulator::Modulator()
{
    reseed (0x1234567u);
}

void Modulator::reseed (uint32_t seed)
{
    rng = seed != 0 ? seed : 0x1234567u;
    for (auto& d : draws)
        d = rnd();
}

float Modulator::rnd()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) ((double) rng / 4294967295.0) * 2.0f - 1.0f;
}

bool Modulator::isUnipolar() const
{
    return settings.unipolar;
}

void Modulator::gate (bool on)
{
    if (on)
        stage = 1;
    else if (stage != 0)
        stage = 3;
}

float Modulator::waveAt (double ph, bool newCycle)
{
    const float p = (float) ph;
    switch (settings.shape)
    {
        case ModShape::sine:     return std::sin (6.2831853f * p);
        case ModShape::triangle: return 1.0f - 4.0f * std::abs (p - 0.5f);   // (-1 at 0, 1 at the middle)
        case ModShape::sawUp:    return 2.0f * p - 1.0f;
        case ModShape::sawDown:  return 1.0f - 2.0f * p;
        case ModShape::square:   return p < 0.5f ? 1.0f : -1.0f;
        case ModShape::sampleHold:
        case ModShape::slew:
            if (newCycle)
            {
                prevHeld = held;
                held = rnd();
            }
            if (settings.shape == ModShape::sampleHold)
                return held;
            {
                const float t = 0.5f - 0.5f * std::cos (3.14159265f * p);   // a cosine from the last value to the new
                return prevHeld + (held - prevHeld) * t;
            }
    }
    return 0.0f;
}

float Modulator::sequenced (bool newCycle)
{
    const bool steps = settings.type == ModType::steps;
    const int n = std::clamp (settings.length, 1, steps ? 16 : 32);
    const int idx = (int) (((cycle % n) + n) % n);
    const int prev = (idx + n - 1) % n;
    if (! steps && newCycle && settings.vari > 0.0f && (rnd() * 0.5f + 0.5f) < settings.vari)
        draws[(size_t) idx] = rnd();
    stepIndex = idx;
    const float v = steps ? settings.stepValues[(size_t) idx] : draws[(size_t) idx];
    if (settings.smooth <= 0.0f)
        return v;
    const float from = steps ? settings.stepValues[(size_t) prev] : draws[(size_t) prev];
    // glide into each step over its first part (all of it at 100 %)
    float t = std::min (1.0f, (float) phase / std::max (0.02f, settings.smooth));
    t = 0.5f - 0.5f * std::cos (3.14159265f * t);
    return from + (v - from) * t;
}

float Modulator::finish (float v, double dt)
{
    if (settings.type == ModType::wave && settings.shape != ModShape::slew && settings.smooth > 0.0f)
    {
        const float k = (float) std::min (1.0, dt / (0.002 + 0.3 * settings.smooth));
        smoothed += (v - smoothed) * k;
        v = smoothed;
    }
    else
        smoothed = v;
    out = settings.unipolar ? 0.5f * (v + 1.0f) : v;
    return out;
}

float Modulator::envelope (double dt)
{
    const auto& s = settings;
    switch (stage)
    {
        case 1:
            env += (float) (dt / std::max (1.0e-4f, s.attack));
            if (env >= 1.0f)
            {
                env = 1.0f;
                stage = 2;
            }
            break;
        case 2:
            env = s.sustain + (env - s.sustain) * (float) std::exp (-dt * 4.6 / std::max (1.0e-3f, s.decay));
            break;
        case 3:
            env *= (float) std::exp (-dt * 4.6 / std::max (1.0e-3f, s.release));
            if (env < 1.0e-4f)
            {
                env = 0.0f;
                stage = 0;
            }
            break;
        default:
            break;
    }
    return env;
}

float Modulator::follow (double dt)
{
    const auto& s = settings;
    const float level = std::min (4.0f, inLevel * s.gain);
    const float t = level > followed ? s.attack : s.release;
    followed += (level - followed) * (float) (1.0 - std::exp (-dt / std::max (1.0e-3f, t)));
    return std::clamp (followed, 0.0f, 1.0f);
}

float Modulator::atPpq (double ppq, double bpm, double dt)
{
    if (! settings.sync || settings.type == ModType::envelope || settings.type == ModType::follower)
        return advance (dt, bpm);
    const auto& d = divisions()[std::clamp (settings.division, 0, numDivisions() - 1)];
    const double cycles = ppq / d.beats;
    const double c = std::floor (cycles);
    const bool newCycle = fresh || (int64_t) c != cycle;
    fresh = false;
    cycle = (int64_t) c;
    phase = cycles - c;
    const float v = settings.type == ModType::wave ? waveAt (phase, newCycle) : sequenced (newCycle);
    return finish (v, dt);
}

float Modulator::advance (double dt, double bpm)
{
    // (ENV, FOLLOW: 0..1 from where the setting is; ±: -1..1 around it, the rest at -1)
    if (settings.type == ModType::envelope || settings.type == ModType::follower)
    {
        const float e = settings.type == ModType::envelope ? envelope (dt) : follow (dt);
        return out = settings.unipolar ? e : 2.0f * e - 1.0f;
    }
    double rate;
    if (settings.sync)
    {
        const auto& d = divisions()[std::clamp (settings.division, 0, numDivisions() - 1)];
        rate = (bpm / 60.0) / d.beats;
    }
    else
        rate = std::max (0.001f, settings.hz);
    bool newCycle = fresh;
    fresh = false;
    phase += rate * dt;
    if (phase >= 1.0)
    {
        const double whole = std::floor (phase);
        phase -= whole;
        cycle += (int64_t) whole;
        newCycle = true;
    }
    const float v = settings.type == ModType::wave ? waveAt (phase, newCycle) : sequenced (newCycle);
    return finish (v, dt);
}
}
