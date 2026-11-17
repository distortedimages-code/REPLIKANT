// Band-limited interpolation: a Kaiser-windowed sinc, read from a finely sampled table.
//
// This is the "varispeed" every sampler does (play faster = higher), done without the aliasing and the dulling
// of cheap interpolators: when a sound is played higher, the kernel widens so nothing folds back below Nyquist.
// Offline renders use a long kernel (64 zero crossings, ~100 dB stopband, flat to ~21.5 kHz at 48 kHz);
// the voices that play live use a shorter one.

#pragma once

#include <vector>
#include <cstdint>

namespace rk
{
    class SincKernel
    {
    public:
        SincKernel (int zeroCrossings, int oversample, double beta);

        // the kernel at |t| (t in input samples at full bandwidth), 0 from zeroCrossings on
        inline float operator() (double t) const
        {
            if (t < 0.0) t = -t;
            const double p = t * (double) os;
            const auto i = (int64_t) p;
            if (i >= (int64_t) Z * os)
                return 0.0f;
            const float f = (float) (p - (double) i);
            return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
        }

        int zeroCrossings() const     { return Z; }

        static const SincKernel& offline();   // 64 zero crossings, beta 10
        static const SincKernel& live();      // 24 zero crossings, beta 9

    private:
        int Z, os;
        std::vector<float> table;
    };

    // Where the input is read. With a loop, the input acts as if it kept looping from loopEnd on (so a rendering
    // of the looped part is seamless, even inside the kernel's reach).
    struct SourceView
    {
        const float* data = nullptr;
        int length = 0;
        int loopStart = -1, loopEnd = -1;

        inline float at (int64_t k) const
        {
            if (k < 0)
                return 0.0f;
            if (loopEnd > loopStart + 1 && k >= loopEnd)
                k = loopStart + (k - loopStart) % (loopEnd - loopStart);
            return k < length ? data[k] : 0.0f;
        }
    };

    // out[n] = the input at position pos0 + n * step. step > 1 plays it faster (higher). `bandwidth` is the cutoff
    // as a fraction of the input's Nyquist, lowered by itself when step > 1.
    void resample (const SourceView&, double pos0, double step, float* out, int count,
                   const SincKernel& kernel = SincKernel::offline(), double bandwidth = 0.95);

    // One value at a fractional position (the live voices). cutoff = bandwidth * min (1, 1 / step).
    float interpolate (const SourceView&, double position, double cutoff, const SincKernel&);
}
