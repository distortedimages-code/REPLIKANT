// The analog drive, on the whole player (after the voices), oversampled 4x:
//   PREAMP   after a Neve-style class-A preamp: an input transformer that fattens the lows, a transistor stage biased
//            off its centre (even harmonics, a warm second), an output transformer saturating the lows first
//   CONSOLE  a console's summing bus pushed: a smooth symmetric saturation that also squeezes a little (level-
//            dependent gain), the glue
//   TAPE     after an Ampex machine at 15 ips: record pre-emphasis, the tape's saturation with memory (hysteresis-
//            like), playback de-emphasis, the head bump in the lows, the top rolling off, a hint of wow and flutter
// DRIVE pushes it (0 = transparent), TONE tilts it, MIX blends it, the output level is kept about the same.

#pragma once

#include "Oversampler.h"

#include <cmath>
#include <cstdint>

namespace dsp
{
    enum class DriveModel { off, preamp, console, tape };

    struct DriveSettings
    {
        DriveModel model = DriveModel::off;
        float drive = 0.0f;   // 0..1
        float tone = 0.5f;    // 0 dark .. 1 bright
        float mix = 1.0f;     // 0..1
    };

    // RBJ peaking biquad, transposed direct form II (the tape's head bump)
    struct PeakEq
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        void design (double f, double q, double db, double sr)
        {
            const double A = std::pow (10.0, db / 40.0), w = 2.0 * 3.14159265358979 * f / sr, al = std::sin (w) / (2.0 * q);
            const double a0 = 1.0 + al / A;
            b0 = (float) ((1.0 + al * A) / a0);
            b1 = (float) (-2.0 * std::cos (w) / a0);
            b2 = (float) ((1.0 - al * A) / a0);
            a1 = b1;
            a2 = (float) ((1.0 - al / A) / a0);
        }
        float tick (float x, float& z1, float& z2) const
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    class Drive
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void set (const DriveSettings& s)   { target = s; }
        void process (float* left, float* right, int n);   // in place

    private:
        struct Channel
        {
            Oversampler os;
            // biquad-ish states (one-poles and a peak), hysteresis, DC
            float lfIn = 0, lfOut = 0, pre = 0, de = 0, bump1 = 0, bump2 = 0, top = 0, hyst = 0, dcx = 0, dcy = 0;
            float tilt = 0, comp = 0;
            float wowBuf[2048] {};
            int wowPos = 0;
        };
        float tick (Channel&, float x, int ch);

        double sr = 48000.0;
        DriveSettings target;
        float drive = 0.0f, tone = 0.5f, mix = 1.0f;
        Channel chans[2];
        PeakEq bump;
        double wowPhase = 0.0, flutterPhase = 0.0;
        uint32_t rng = 0x2468ACE1u;
    };
}
