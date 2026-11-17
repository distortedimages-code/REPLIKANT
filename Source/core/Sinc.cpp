#include "Sinc.h"

#include <algorithm>
#include <cmath>

namespace rk
{
namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x / 4.0;
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

SincKernel::SincKernel (int zeroCrossings, int oversample, double beta) : Z (zeroCrossings), os (oversample)
{
    const size_t n = (size_t) Z * (size_t) os + 2;
    table.resize (n, 0.0f);
    const double norm = besselI0 (beta);
    const double pi = 3.14159265358979323846;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / (double) os;
        if (t >= (double) Z)
        {
            table[i] = 0.0f;
            continue;
        }
        const double sinc = t < 1.0e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
        const double r = t / (double) Z;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / norm;
        table[i] = (float) (sinc * w);
    }
}

const SincKernel& SincKernel::offline()
{
    static const SincKernel k (64, 2048, 10.0);
    return k;
}

const SincKernel& SincKernel::live()
{
    static const SincKernel k (24, 1024, 9.0);
    return k;
}

float interpolate (const SourceView& src, double p, double cutoff, const SincKernel& K)
{
    const double halfWidth = (double) K.zeroCrossings() / cutoff;
    const auto k0 = (int64_t) std::ceil (p - halfWidth);
    const auto k1 = (int64_t) std::floor (p + halfWidth);
    double sum = 0.0, wsum = 0.0;
    const bool plain = ! (src.loopEnd > src.loopStart + 1) && k0 >= 0 && k1 < src.length;
    for (int64_t k = k0; k <= k1; ++k)
    {
        const float w = K (((double) k - p) * cutoff);
        wsum += w;
        sum += (double) w * (double) (plain ? src.data[k] : src.at (k));
    }
    return wsum > 1.0e-9 ? (float) (sum / wsum) : 0.0f;
}

void resample (const SourceView& src, double pos0, double step, float* out, int count, const SincKernel& K, double bandwidth)
{
    const double cutoff = bandwidth * std::min (1.0, 1.0 / std::max (1.0e-6, step));
    for (int n = 0; n < count; ++n)
        out[n] = interpolate (src, pos0 + (double) n * step, cutoff, K);
}
}
