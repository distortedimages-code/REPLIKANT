#include "Oversampler.h"

namespace dsp
{
namespace
{
    double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x / 4.0;
        for (int k = 1; k < 60; ++k)
        {
            term *= q / ((double) k * k);
            sum += term;
            if (term < sum * 1e-16)
                break;
        }
        return sum;
    }
}

std::vector<float> halfband (int taps, double beta)
{
    std::vector<float> h ((size_t) taps, 0.0f);
    const int c = (taps - 1) / 2;
    const double pi = 3.14159265358979323846;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        const int k = i - c;
        double v;
        if (k == 0)
            v = 0.5;
        else if (k % 2 == 0)
            v = 0.0;
        else
            v = std::sin (pi * k / 2.0) / (pi * k);
        const double r = (double) k / (double) c;
        v *= bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        h[(size_t) i] = (float) v;
        sum += v;
    }
    for (auto& v : h)
        v = (float) (v / sum);   // unity at DC
    return h;
}

void Halfband::init (const std::vector<float>& taps)
{
    h = taps;
    centre = (int) (h.size() - 1) / 2;
    histUp.assign (h.size() * 2, 0.0f);
    histDown.assign (h.size() * 2, 0.0f);
    posUp = posDown = 0;
}

void Halfband::reset()
{
    std::fill (histUp.begin(), histUp.end(), 0.0f);
    std::fill (histDown.begin(), histDown.end(), 0.0f);
    posUp = posDown = 0;
}

// The zero-stuffed input filtered by 2h. With x[n-k] = hist[pos + L - k] (a doubled ring: no wrap in the loops):
//   y[2n]   = 2 sum_{i even} h[i] x[n - i/2]          (the odd offsets from the centre)
//   y[2n+1] = 2 h[c] x[n - (c-1)/2]                   (the centre tap alone: c is odd)
void Halfband::up (float x, float& y0, float& y1)
{
    const int N = (int) h.size();
    const int L = (N + 1) / 2;
    histUp[(size_t) posUp] = histUp[(size_t) (posUp + L)] = x;
    const float* base = histUp.data() + posUp + L;      // base[-k] = x[n-k]
    float a = 0.0f;
    for (int i = 0; i < N; i += 2)
        a += h[(size_t) i] * base[-(i / 2)];
    y0 = 2.0f * a;
    y1 = 2.0f * h[(size_t) centre] * base[-((centre - 1) / 2)];
    posUp = (posUp + 1) % L;
}

// y[n] = sum_i h[i] v[2n+1 - i], v the high-rate input (doubled ring of N)
float Halfband::down (float x0, float x1)
{
    const int N = (int) h.size();
    histDown[(size_t) posDown] = histDown[(size_t) (posDown + N)] = x0;
    posDown = (posDown + 1) % N;
    histDown[(size_t) posDown] = histDown[(size_t) (posDown + N)] = x1;
    const float* base = histDown.data() + posDown + N;   // base[-k] = v[m-k]
    float y = h[(size_t) centre] * base[-centre];
    for (int i = 0; i < N; i += 2)
        y += h[(size_t) i] * base[-i];
    posDown = (posDown + 1) % N;
    return y;
}

void Oversampler::prepare (int factor)
{
    f = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    stages = f == 8 ? 3 : f == 4 ? 2 : f == 2 ? 1 : 0;
    static const auto first = halfband (63, 9.0);
    static const auto second = halfband (23, 7.0);
    static const auto third = halfband (15, 6.0);
    for (int s = 0; s < 3; ++s)
    {
        const auto& t = s == 0 ? first : s == 1 ? second : third;
        up_[(size_t) s].init (t);
        down_[(size_t) s].init (t);
    }
}

void Oversampler::reset()
{
    for (auto& s : up_) s.reset();
    for (auto& s : down_) s.reset();
}
}
