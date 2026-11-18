// Reading a sample between its samples, as cleanly as can be heard: Kaiser-windowed sinc kernels, tabulated once
// (no sinc or Bessel function while playing), applied as plain dot products the compiler turns into vector code.
//
// A bank holds `rows` cutoffs (from `top`, a fraction of Nyquist, down to `lowest`, spaced evenly in octaves), and for
// each cutoff `phases + 1` kernels of `taps` taps, one every 1/phases of a sample (the last one closes the interval).
// A position falling between two tabulated phases gets the two dot products blended (the error of that blend is
// under -100 dB at 128 phases, where 32 would leave it near -70 dB at the top of the band).
//
// Each kernel's window is centred on the position read (not on the middle of the taps), and each kernel sums to
// exactly one: the response is the same, and the level too, whatever fraction of a sample the position falls on, so
// a slow pitch sweep has no ripple of level or tone.
//
// The cutoff follows the playing speed (its caller lowers it as the pitch goes up, so nothing folds back) and the
// speeds far up are read from the sample's copies an octave down: the speed a kernel meets stays under 1.5 or so,
// and its taps keep their length in time at any transposition.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace dsp
{
    class Polyphase
    {
    public:
        Polyphase (int taps, int phases, int rows, double beta, double top, double lowest);

        int taps() const                { return T; }
        double top() const              { return topCut; }

        // the kernels around `frac` (0..1) and `cutoff`: two phases at the row at or under the cutoff (a, b), the
        // same two at the next row down (c, d), and how far between them (t across the phases, u across the rows: the cutoff
        // glides between its rows too, so a pitch bend or a sweep of ALIASING never steps)
        struct Pick { const float* a; const float* b; const float* c; const float* d; float t, u; };
        inline Pick pick (double cutoff, double frac) const
        {
            int row = 0;
            float u = 0.0f;
            if (R > 1 && cutoff < topCut)
            {
                double x = std::log2 (topCut / cutoff) * rowsPerOctave;
                // one row further down (two blended rows reject only as well as the higher of them: it must sit at
                // or under the cutoff asked), reached gradually within the first row so nothing steps at the top
                x = x < 1.0 ? 2.0 * x : x + 1.0;
                x = x > (double) (R - 1) ? (double) (R - 1) : x;
                row = (int) x;
                u = (float) (x - (double) row);
                if (row >= R - 1)
                {
                    row = R - 1;
                    u = 0.0f;
                }
            }
            const double y = frac * (double) P;
            int j = (int) y;
            j = j < 0 ? 0 : (j >= P ? P - 1 : j);
            const size_t setsPerRow = (size_t) (P + 1);
            const float* base = table.data() + ((size_t) row * setsPerRow + (size_t) j) * (size_t) T;
            const float* next = u > 1.0e-4f ? base + setsPerRow * (size_t) T : nullptr;
            return { base, base + T, next, next != nullptr ? next + T : nullptr, (float) (y - (double) j), next != nullptr ? u : 0.0f };
        }

        // the frame at a position whose first tap reads x[0] (x[0] is the sample floor(p) - taps/2 + 1)
        inline float apply (const float* x, const Pick& k) const
        {
            float s0 = 0.0f, s1 = 0.0f;
            for (int i = 0; i < T; ++i)
            {
                s0 += x[i] * k.a[i];
                s1 += x[i] * k.b[i];
            }
            float y = s0 + k.t * (s1 - s0);
            if (k.c != nullptr)
            {
                float s2 = 0.0f, s3 = 0.0f;
                for (int i = 0; i < T; ++i)
                {
                    s2 += x[i] * k.c[i];
                    s3 += x[i] * k.d[i];
                }
                y += k.u * (s2 + k.t * (s3 - s2) - y);
            }
            return y;
        }
        inline void apply2 (const float* x0, const float* x1, const Pick& k, float& o0, float& o1) const
        {
            float a0 = 0.0f, b0 = 0.0f, a1 = 0.0f, b1 = 0.0f;
            for (int i = 0; i < T; ++i)
            {
                a0 += x0[i] * k.a[i];
                b0 += x0[i] * k.b[i];
                a1 += x1[i] * k.a[i];
                b1 += x1[i] * k.b[i];
            }
            o0 = a0 + k.t * (b0 - a0);
            o1 = a1 + k.t * (b1 - a1);
            if (k.c != nullptr)
            {
                float c0 = 0.0f, d0 = 0.0f, c1 = 0.0f, d1 = 0.0f;
                for (int i = 0; i < T; ++i)
                {
                    c0 += x0[i] * k.c[i];
                    d0 += x0[i] * k.d[i];
                    c1 += x1[i] * k.c[i];
                    d1 += x1[i] * k.d[i];
                }
                o0 += k.u * (c0 + k.t * (d0 - c0) - o0);
                o1 += k.u * (c1 + k.t * (d1 - c1) - o1);
            }
        }

        // the voices' reader: 128 taps, beta 8.6 (about 87 dB down where things would fold), flat to ~21.5 kHz
        // at 48 kHz (top 0.955 of Nyquist), 128 phases, cutoffs down to a quarter of the top
        static const Polyphase& hq();
        // the elastic mode's grains (several at once): 64 taps, the same law
        static const Polyphase& grains();
        // the middle of ALIASING: a short, soft kernel (the slope of the old samplers' interpolators)
        static const Polyphase& soft();
        // made before the audio starts (the tables take a few tens of milliseconds to compute)
        static void prepareAll();

        static constexpr int maxTaps = 128;

    private:
        int T, P, R;
        double topCut, rowsPerOctave;
        std::vector<float> table;
    };
}
