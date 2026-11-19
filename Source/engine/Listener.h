// CAPTURE CYCLES, LISTEN (0.4.5): armed, it waits for the input to go over a threshold, then records it (with the
// 50 ms before) until it has been under it for 1.5 s, STOP, or 30 s; the take is then cut into cycles (Cycles.h,
// cyclesFromTake). Time to walk to the synth, hold a note and turn a knob: the bank is made of what it played.
//
// The audio thread writes, the message thread arms, stops and takes: `state` hands the take over (release/acquire).

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

class CycleListener
{
public:
    enum State { idle = 0, waiting, recording, done };

    // message thread, when idle
    bool arm (double sampleRate, float thresholdDb, double maxSeconds)
    {
        if (state.load() != idle || sampleRate <= 0.0)
            return false;
        sr = sampleRate;
        buffer.assign ((size_t) (sampleRate * maxSeconds), 0.0f);
        pre.assign ((size_t) (sampleRate * 0.05) + 1, 0.0f);
        prePos = 0;
        threshold = std::pow (10.0f, thresholdDb / 20.0f);
        filled = 0;
        lastLoud = 0;
        below = 0;
        stopWanted = false;
        peak = 0.0f;
        state.store (waiting, std::memory_order_release);
        return true;
    }
    void stop()                          { stopWanted = true; }
    bool busy() const                    { return state.load() != idle; }

    // audio thread: what comes in (the capture's source)
    void process (const float* const* in, int channels, int n)
    {
        int s = state.load (std::memory_order_acquire);
        if (s == idle || s == done)
            return;
        int f = filled.load (std::memory_order_relaxed);
        if (stopWanted.load())
        {
            state.store (s == recording && f > 0 ? done : idle, std::memory_order_release);
            return;
        }
        float blockPeak = 0.0f;
        const int chans = std::max (1, std::min (2, channels));
        const int size = (int) buffer.size(), np = (int) pre.size();
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < chans; ++c)
                x += in[c] != nullptr ? in[c][i] : 0.0f;
            x /= (float) chans;
            blockPeak = std::max (blockPeak, std::abs (x));
            if (s == waiting)
            {
                pre[(size_t) prePos] = x;
                prePos = (prePos + 1) % np;
                if (std::abs (x) >= threshold)
                {
                    // over it: the 50 ms before first, then on
                    for (int k = 0; k < np && f < size; ++k)
                        buffer[(size_t) f++] = pre[(size_t) ((prePos + k) % np)];
                    lastLoud = f;
                    below = 0;
                    s = recording;
                }
                continue;
            }
            if (f < size)
                buffer[(size_t) f++] = x;
            if (std::abs (x) >= threshold * 0.5f)
            {
                lastLoud = f;
                below = 0;
            }
            else
                ++below;
            if (below > (int) (sr * 1.5) || f >= size)
            {
                f = std::min (f, lastLoud + (int) (sr * 0.05));
                s = done;
                break;
            }
        }
        peak = blockPeak;
        filled.store (f, std::memory_order_relaxed);
        state.store (s, std::memory_order_release);
    }

    // message thread, once done: the take (and back to idle)
    std::vector<float> take()
    {
        if (state.load (std::memory_order_acquire) != done)
            return {};
        auto v = std::move (buffer);
        v.resize ((size_t) std::max (0, std::min (filled.load(), (int) v.size())));
        buffer.clear();
        state.store (idle, std::memory_order_release);
        return v;
    }
    double seconds() const               { return sr > 0.0 ? (double) filled.load() / sr : 0.0; }
    double maxSeconds() const            { return sr > 0.0 ? (double) buffer.size() / sr : 30.0; }
    float levelNow() const               { return peak.load(); }
    float thresholdGain() const          { return threshold; }

    std::atomic<int> state { idle };

private:
    double sr = 48000.0;
    std::vector<float> buffer, pre;
    int prePos = 0, lastLoud = 0, below = 0;
    std::atomic<int> filled { 0 };
    std::atomic<bool> stopWanted { false };
    std::atomic<float> peak { 0.0f };
    float threshold = 0.01f;
};
