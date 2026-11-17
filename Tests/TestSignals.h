// Sounds for the tests: band-limited harmonic tones through a fixed resonance (like a synth's filter that
// doesn't follow the keyboard), with an envelope, a slow wobble and a little noise.

#pragma once

#include "../Source/core/RkCore.h"

#include <cmath>
#include <random>

namespace test
{
    struct Tone
    {
        double sr = 48000.0;
        double hz = 261.63;
        double seconds = 2.0;
        double formantHz = 1000.0;   // the resonance
        double formantQ = 4.0;
        double attack = 0.01, release = 0.3, releaseAt = -1.0;   // releaseAt < 0: no release
        double lfoHz = 0.0, lfoDepth = 0.0;                      // amplitude wobble
        double noiseDb = -100.0;
        double level = 0.5;
        double tilt = 1.0;           // harmonic h at 1/h^tilt: 1 = sawtooth, 0 = flat (pulse train)
        int channels = 2;
        int delay = 0;               // samples of silence first (latency)
    };

    // harmonic amplitudes shaped by a resonance at formantHz
    inline double resonance (double f, double fc, double q)
    {
        const double x = f / fc;
        return 1.0 / std::sqrt ((1 - x * x) * (1 - x * x) + (x / q) * (x / q));
    }

    inline rk::Channels make (const Tone& t)
    {
        const int len = t.delay + (int) (t.seconds * t.sr);
        rk::Channels ch ((size_t) t.channels, std::vector<float> ((size_t) len, 0.0f));
        std::mt19937 rng (1234);
        std::normal_distribution<double> nd (0.0, 1.0);
        const double noise = std::pow (10.0, t.noiseDb / 20.0);
        const int H = std::max (1, (int) ((t.sr * 0.45) / t.hz));
        std::vector<double> amp ((size_t) H + 1, 0.0);
        double norm = 0.0;
        for (int h = 1; h <= H; ++h)
        {
            amp[(size_t) h] = resonance (h * t.hz, t.formantHz, t.formantQ) / std::pow ((double) h, t.tilt);
            norm += amp[(size_t) h];
        }
        for (int h = 1; h <= H; ++h)
            amp[(size_t) h] *= 1.0 / norm;
        const double pi2 = 6.283185307179586;
        for (int i = 0; i < len; ++i)
        {
            double v = 0.0;
            if (i >= t.delay)
            {
                const double time = (i - t.delay) / t.sr;
                double env = std::min (1.0, time / std::max (1.0e-4, t.attack));
                if (t.releaseAt >= 0.0 && time > t.releaseAt)
                    env *= std::exp (-(time - t.releaseAt) / std::max (1.0e-3, t.release / 5.0));
                if (t.lfoHz > 0.0)
                    env *= 1.0 - t.lfoDepth * 0.5 * (1.0 - std::cos (pi2 * t.lfoHz * time));
                double s = 0.0;
                for (int h = 1; h <= H; ++h)
                    s += amp[(size_t) h] * std::sin (pi2 * t.hz * h * time + 0.3 * h);
                v = t.level * env * s;
            }
            for (int c = 0; c < t.channels; ++c)
                ch[(size_t) c][(size_t) i] = (float) (v * (c == 1 ? 0.9 : 1.0) + noise * nd (rng));
        }
        return ch;
    }

    inline std::vector<float> sine (double sr, double hz, int len, double amp = 0.5)
    {
        std::vector<float> s ((size_t) len);
        for (int i = 0; i < len; ++i)
            s[(size_t) i] = (float) (amp * std::sin (6.283185307179586 * hz * i / sr));
        return s;
    }
}
