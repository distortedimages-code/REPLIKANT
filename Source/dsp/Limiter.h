// LIMIT (0.4.7): the very last thing before the outputs, so nothing that comes out goes over CEILING, whatever the
// filters, the drives and the effects did before it.
//
// A look-ahead limiter: the sound waits 1.5 ms (the plug-in tells the host, which makes up for it), long enough for
// the gain to come down before a peak arrives rather than after it. The peaks are measured between the samples too
// (three points between each two, on a cubic through four: what a converter would draw), on both channels together.
// The gain needed at each sample (CEILING over the peak) goes through the smallest of the last 1.5 ms (it is down
// before the peak), a release (RELEASE, one-pole: how fast it comes back up), then an average over the same 1.5 ms (a
// smooth ramp, never above what the peak needs). A last clamp at CEILING catches what rounding might leave. DRIVE
// pushes the sound into it first (up to +12 dB).
//
// Original: the sliding minimum and the moving average are the textbook look-ahead limiter; nothing taken from
// anyone's code.

#pragma once

#include <atomic>
#include <vector>

namespace dsp
{
    struct LimiterSettings
    {
        bool on = true;
        float ceilingDb = -0.3f;    // -12..0
        float releaseMs = 120.0f;   // 10..1000
        float driveDb = 0.0f;       // 0..12
    };

    class Limiter
    {
    public:
        void prepare (double sampleRate);
        void reset();
        int latency() const                  { return look + 2; }
        void set (const LimiterSettings& s)  { target = s; }
        void process (float* left, float* right, int n);     // in place, `latency()` frames late (on or off)
        std::atomic<float> shownReduction { 0.0f };           // dB, the most in the last block

    private:
        double sr = 48000.0;
        LimiterSettings target;
        int look = 72;
        std::vector<float> dl, dr;          // the delay (look frames)
        std::vector<float> need;            // the gains needed, the window's (a ring)
        std::vector<int> minQ;              // the sliding minimum's candidates (positions, a ring of indices)
        int qHead = 0, qTail = 0;
        std::vector<float> avg;             // the average's ring
        double avgSum = 0.0;
        int pos = 0;
        long long t = 0;
        float held = 1.0f;                  // after the release
        float prevL[3] {}, prevR[3] {};     // the last samples (for the peaks between)
        float driveG = 1.0f, ceilG = 0.966f, onG = 1.0f;
    };
}
