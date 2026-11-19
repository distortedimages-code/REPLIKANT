// COMP (0.4.5): the dynamics at the very end of the chain, before the volume.
//
// In this order: IN (up to +18 dB, to push it), a GATE (its threshold up to 0 dB: off at the left), the compressor,
// then DRIVE (a soft saturation) and MIX (the compressed sound against the sound as it came: parallel compression),
// LEVEL. What comes out of the compressor and the drive is brought slowly (a second) back to the loudness of what came
// in: turning AMOUNT, IN or DRIVE changes how it moves, not how loud it is.
//
// The compressor is turned with one knob, AMOUNT, which sets its threshold, its ratio and its make-up together:
//   0 .. 50 %   from 1:1 (nothing) through 4:1 to ∞:1 at the middle (a limiter), the threshold coming down to -30 dB
//               and the make-up up, so the sound stays about as loud as it gets denser
//   50 .. 100 % past the limit: what goes over the threshold comes out lower than what stays under it (1:-0.3 to
//               1:-1): the hits pushed down under their own tails, the tails sucked up between them
// ATTACK (six times, 0.05 to 30 ms: a slow one lets the transients through, a fast one flattens them) and RELEASE
// (five times, 20 ms to 1 s, and AUTO: a quick release for the peaks over a slow one for what holds, at ease on pads).
// What it listens to (KEY): itself, the sidechain, the input, or one of the DRUM's tracks (a kick pumping the rest).
//
// After the one-knob warming units' compressors (threshold, ratio and make-up on a single control, then ratios past
// infinity); the design is REPLIK8's own: a feed-forward gain computer in decibels with a 6 dB soft knee, the gain
// smoothed in decibels (attack while it comes down, release while it goes back up), linked across the two channels.

#pragma once

#include <atomic>

namespace dsp
{
    struct CompSettings
    {
        float inDb = 0.0f;          // 0..18
        float gateDb = -80.0f;      // -80 (off) .. 0
        float amount = 0.0f;        // 0..1
        int attack = 2;             // 0..5: 0.05, 0.3, 1, 3, 10, 30 ms
        int release = 3;            // 0..5: 20, 60, 150, 400 ms, 1 s, AUTO
        float drive = 0.0f;         // 0..1
        float mix = 1.0f;           // 0..1
        float levelDb = 0.0f;       // -24..12
    };

    class Comp
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void set (const CompSettings& s)     { target = s; }
        // in place; `keyL`, `keyR`: what it listens to (nullptr: the sound itself)
        void process (float* left, float* right, int n, const float* keyL = nullptr, const float* keyR = nullptr);

        // the curve (output level for an input level, both in dB, before IN and LEVEL), for the screen
        static float curve (const CompSettings&, float inDb);
        std::atomic<float> shownReduction { 0.0f }, shownLevel { -100.0f }, shownGate { 1.0f };   // dB, dB, 0..1

    private:
        double sr = 48000.0;
        CompSettings target;
        float grFast = 0.0f, grSlow = 0.0f;     // the gain taken (dB, >= 0 for compression)
        float gate = 1.0f, gateHold = 0.0f;
        float inG = 1.0f, outG = 1.0f, mixNow = 1.0f, driveNow = 0.0f, amountNow = 0.0f;
        float env = 0.0f;                       // the detector (a peak, let go in 12 ms)
        float inPow = 0.0f, outPow = 0.0f, match = 1.0f;   // the loudness kept
    };
}
