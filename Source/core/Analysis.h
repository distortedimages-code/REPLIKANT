// Listening to the recordings: the noise floor of the line, where a note starts and where its tail fades into
// that floor, and the pitch it really plays (analog synths are rarely spot on).

#pragma once

#include "RkCore.h"

namespace rk
{
    struct NoiseFloor
    {
        float rms = 0.0f;           // linear
        float peak = 0.0f;
        std::vector<float> dc;      // per channel
        float rmsDb() const         { return gainToDb (rms); }
    };

    NoiseFloor measureNoise (const Channels&);

    // RMS of a mono signal on `window`-sample windows every `hop` samples (window centred on i * hop)
    std::vector<float> rmsEnvelope (const std::vector<float>& mono, int hop, int window);

    // First sample, at or after `from`, where the sound clearly rises above the floor: the first one over
    // `threshold` (linear), walked back to where it left the floor (at most `lookback` samples).
    int findOnset (const Channels&, int from, float threshold, float floor, int lookback);

    // Where the tail has faded into the floor: the end of the last 10 ms window, from `from` on, louder than
    // `floor` * 2 (+6 dB). Returns at least `from`.
    int findTailEnd (const Channels&, int from, float floor, double sampleRate);

    struct Pitch
    {
        double hz = 0.0;
        double cents = 0.0;         // against the expected note
        float confidence = 0.0f;    // 0..1
        bool ok = false;
    };

    // The pitch played around `expectedHz` (searched within a semitone of it, so octave errors can't happen),
    // measured with YIN on a few windows of [from, to) and kept when they agree.
    Pitch detectPitch (const std::vector<float>& mono, double sampleRate, double expectedHz, int from, int to);

    // Fades, in place
    void fadeIn (Channels&, int samples);
    void fadeOutEnd (Channels&, int samples);
    void removeDc (Channels&, const std::vector<float>& dc);
}
