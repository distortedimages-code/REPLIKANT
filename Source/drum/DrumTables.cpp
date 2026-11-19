#include "DrumTables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace drum
{
namespace
{
    constexpr int N = dsp::CycleBank::size, frames = 12;
    constexpr double pi = 3.14159265358979323846, twoPi = 2.0 * pi;

    // a random number of our own (the same on every machine)
    struct Lcg
    {
        uint32_t s;
        explicit Lcg (uint32_t seed) : s (seed * 2654435761u + 12345u) {}
        double next() { s = s * 1664525u + 1013904223u; return (double) (s >> 8) * (1.0 / 16777216.0); }
    };

    std::vector<float> shaped (double (*f) (double x, double k), double k)
    {
        std::vector<float> w ((size_t) N);
        for (int i = 0; i < N; ++i)
            w[(size_t) i] = (float) f (twoPi * (double) i / N, k);
        return w;
    }
    // harmonics h (1 .. N/2 - 1), their amplitudes and phases
    std::vector<float> additive (const std::vector<double>& amp, const std::vector<double>& ph)
    {
        std::vector<double> w ((size_t) N, 0.0);
        for (size_t h = 1; h < amp.size() && h < (size_t) N / 2; ++h)
        {
            if (std::abs (amp[h]) < 1.0e-9)
                continue;
            for (int i = 0; i < N; ++i)
                w[(size_t) i] += amp[h] * std::sin (twoPi * (double) h * (double) i / N + ph[h]);
        }
        return std::vector<float> (w.begin(), w.end());
    }

    std::vector<std::vector<float>> skin()
    {
        std::vector<std::vector<float>> out;
        for (int k = 0; k < frames; ++k)
        {
            const double t = (double) k / (frames - 1);
            out.push_back (shaped ([] (double x, double a)
            {
                const double bend = 2.6 * std::pow (a, 1.3);
                return std::sin (x + 0.5 * bend * std::sin (x)) + 0.15 * a * std::sin (2.0 * x + 0.4);
            }, t));
        }
        return out;
    }
    std::vector<std::vector<float>> fold()
    {
        std::vector<std::vector<float>> out;
        for (int k = 0; k < frames; ++k)
        {
            const double g = 1.0 + 7.0 * std::pow ((double) k / (frames - 1), 1.5);
            out.push_back (shaped ([] (double x, double gain) { return std::sin (0.5 * pi * gain * std::sin (x)); }, g));
        }
        return out;
    }
    std::vector<std::vector<float>> metal()
    {
        std::vector<std::vector<float>> out;
        for (int k = 0; k < frames; ++k)
        {
            Lcg r ((uint32_t) (101 + k));
            std::vector<double> amp (100, 0.0), ph (100, 0.0);
            const int count = 6 + 3 * k;
            for (int j = 0; j < count; ++j)
            {
                const int h = 5 + (int) (r.next() * 90.0);
                amp[(size_t) h] += (0.3 + 0.7 * r.next()) / std::sqrt ((double) h);
                ph[(size_t) h] = twoPi * r.next();
            }
            amp[1] = 0.35 * (1.0 - (double) k / frames);         // (a little body under it, fading)
            out.push_back (additive (amp, ph));
        }
        return out;
    }
    std::vector<std::vector<float>> vox()
    {
        // a, e, i, o, u: three formants each (Hz), sung on 110 Hz
        static const double f[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
        static const double gain[3] = { 1.0, 0.7, 0.35 }, bw[3] = { 90.0, 110.0, 160.0 };
        std::vector<std::vector<float>> out;
        for (int k = 0; k < frames; ++k)
        {
            const double at = 4.0 * (double) k / (frames - 1);
            const int a = std::min (3, (int) at);
            const double m = at - a;
            std::vector<double> amp (48, 0.0), ph (48, 0.0);
            for (int h = 1; h < 48; ++h)
            {
                const double hz = 110.0 * h;
                double v = 0.0;
                for (int j = 0; j < 3; ++j)
                {
                    const double fj = f[a][j] + m * (f[a + 1][j] - f[a][j]);
                    v += gain[j] * std::exp (-std::pow ((hz - fj) / bw[j], 2.0));
                }
                amp[(size_t) h] = v + 0.04 / h;
                ph[(size_t) h] = 0.0;
            }
            out.push_back (additive (amp, ph));
        }
        return out;
    }
    std::vector<std::vector<float>> digi()
    {
        std::vector<std::vector<float>> out;
        static const double steps[6] = { 12, 8, 6, 4, 3, 2 };
        for (int k = 0; k < 6; ++k)
            out.push_back (shaped ([] (double x, double s) { return std::round (std::sin (x) * s) / s; }, steps[k]));
        for (int k = 0; k < 6; ++k)
        {
            const double duty = 0.5 - 0.44 * (double) k / 5.0;
            out.push_back (shaped ([] (double x, double d) { return x < twoPi * d ? 1.0 : -1.0; }, duty));
        }
        return out;
    }
    std::vector<std::vector<float>> grit()
    {
        std::vector<std::vector<float>> out;
        for (int k = 0; k < frames; ++k)
        {
            Lcg r ((uint32_t) (907 + 13 * k));
            const int top = 24 + 8 * k;
            std::vector<double> amp ((size_t) top + 1, 0.0), ph ((size_t) top + 1, 0.0);
            for (int h = 1; h <= top; ++h)
            {
                const double x = r.next();
                amp[(size_t) h] = x * x / std::pow ((double) h, 0.6);
                ph[(size_t) h] = twoPi * r.next();
            }
            out.push_back (additive (amp, ph));
        }
        return out;
    }

    std::array<std::unique_ptr<dsp::CycleBank>, numOwnTables>& all()
    {
        static std::array<std::unique_ptr<dsp::CycleBank>, numOwnTables> t = []
        {
            std::array<std::unique_ptr<dsp::CycleBank>, numOwnTables> m;
            m[0] = std::make_unique<dsp::CycleBank> (skin(), true);
            m[1] = std::make_unique<dsp::CycleBank> (fold(), true);
            m[2] = std::make_unique<dsp::CycleBank> (metal(), true);
            m[3] = std::make_unique<dsp::CycleBank> (vox(), true);
            m[4] = std::make_unique<dsp::CycleBank> (digi(), true);
            m[5] = std::make_unique<dsp::CycleBank> (grit(), true);
            return m;
        }();
        return t;
    }
}

const dsp::CycleBank* ownTable (int index)
{
    auto& t = all();
    return t[(size_t) (index < 0 ? 0 : (index >= numOwnTables ? numOwnTables - 1 : index))].get();
}

void prepareTables()
{
    (void) all();
}
}
