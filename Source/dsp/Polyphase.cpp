#include "Polyphase.h"

#include <algorithm>

namespace dsp
{
    namespace
    {
        double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            const double q = x * x * 0.25;
            for (int k = 1; k < 64; ++k)
            {
                term *= q / ((double) k * (double) k);
                sum += term;
                if (term < sum * 1.0e-17)
                    break;
            }
            return sum;
        }
    }

    Polyphase::Polyphase (int taps, int phases, int rows, double beta, double top, double lowest)
        : T (std::max (2, taps & ~1)), P (std::max (1, phases)), R (std::max (1, rows)), topCut (top)
    {
        const double octaves = R > 1 ? std::log2 (top / lowest) : 1.0;
        rowsPerOctave = R > 1 ? (double) (R - 1) / octaves : 0.0;
        table.assign ((size_t) R * (size_t) (P + 1) * (size_t) T, 0.0f);
        const double norm = besselI0 (beta), half = (double) T * 0.5;
        constexpr double pi = 3.14159265358979323846;
        std::vector<double> h ((size_t) T);
        for (int row = 0; row < R; ++row)
        {
            const double fc = R > 1 ? top * std::exp2 (-(double) row / rowsPerOctave) : top;
            for (int j = 0; j <= P; ++j)
            {
                // the position read sits `frac` past tap T/2 - 1; tap k is t samples from it
                const double frac = (double) j / (double) P;
                double sum = 0.0;
                for (int k = 0; k < T; ++k)
                {
                    const double t = (double) k - (half - 1.0) - frac;
                    const double x = pi * fc * t;
                    const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (x) / x;
                    const double r = t / half;
                    const double w = std::abs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / norm;
                    h[(size_t) k] = fc * sinc * w;
                    sum += h[(size_t) k];
                }
                float* out = table.data() + ((size_t) row * (size_t) (P + 1) + (size_t) j) * (size_t) T;
                for (int k = 0; k < T; ++k)
                    out[k] = (float) (h[(size_t) k] / sum);
            }
        }
    }

    const Polyphase& Polyphase::hq()
    {
        static const Polyphase p (128, 128, 40, 8.6, 0.955, 0.955 / 4.0);
        return p;
    }
    const Polyphase& Polyphase::grains()
    {
        static const Polyphase p (64, 64, 40, 8.6, 0.92, 0.92 / 4.0);
        return p;
    }
    const Polyphase& Polyphase::soft()
    {
        static const Polyphase p (12, 64, 1, 3.0, 0.95, 0.95);
        return p;
    }
    void Polyphase::prepareAll()
    {
        hq();
        grains();
        soft();
    }
}
