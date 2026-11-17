// Oversampling for the nonlinear parts (the filters, the drives): halfband FIR stages, 2x each, cascaded to 2x, 4x
// or 8x. The first stage is long (flat to 20 kHz at 44.1/48 kHz, about 90 dB down where things would fold back),
// the next ones shorter (their band is already empty above a quarter of their rate). A sample at a time, no
// allocation once prepared.

#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace dsp
{
    // a Kaiser-windowed halfband low pass, its taps (odd length 4k+3)
    std::vector<float> halfband (int taps, double beta);

    class Halfband
    {
    public:
        void init (const std::vector<float>& h);
        void reset();
        // one sample in, two out (at twice the rate)
        void up (float x, float& y0, float& y1);
        // two samples in (at twice the rate), one out
        float down (float x0, float x1);
        int latency() const          { return (int) (h.size() - 1) / 2; }   // at the high rate

    private:
        std::vector<float> h;
        std::vector<float> histUp, histDown;
        int posUp = 0, posDown = 0, centre = 0;
    };

    class Oversampler
    {
    public:
        void prepare (int factor);       // 1, 2, 4 or 8
        void reset();
        int factor() const               { return f; }

        // x up to the high rate, `fn` applied to each high-rate sample, back down
        template <typename Fn>
        inline float process (float x, Fn&& fn)
        {
            if (f == 1)
                return fn (x);
            float a[8], b[8];
            int n = 1;
            a[0] = x;
            for (int s = 0; s < stages; ++s)
            {
                for (int i = 0; i < n; ++i)
                    up_[(size_t) s].up (a[i], b[2 * i], b[2 * i + 1]);
                n *= 2;
                for (int i = 0; i < n; ++i)
                    a[i] = b[i];
            }
            for (int i = 0; i < n; ++i)
                a[i] = fn (a[i]);
            for (int s = stages - 1; s >= 0; --s)
            {
                n /= 2;
                for (int i = 0; i < n; ++i)
                    b[i] = down_[(size_t) s].down (a[2 * i], a[2 * i + 1]);
                for (int i = 0; i < n; ++i)
                    a[i] = b[i];
            }
            return a[0];
        }

    private:
        int f = 1, stages = 0;
        std::array<Halfband, 3> up_, down_;
    };
}
