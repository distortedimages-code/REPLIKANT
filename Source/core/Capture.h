// From a raw recording (what came in from the note-on, with some before) to a clean Sample: DC removed, cut at
// the attack and where the tail meets the noise floor, faded, its pitch measured, its loop found and blended.

#pragma once

#include "Analysis.h"
#include "Plan.h"

namespace rk
{
    struct CaptureResult
    {
        Sample sample;
        bool silent = false;        // nothing above the floor: the synth didn't play (MIDI or audio routing)
        bool clipped = false;       // the input hit full scale
        int latency = 0;            // samples from the note-on to the attack
    };

    // raw: the recording, noteOn / noteOff: where they were sent in it. fixedLatency (samples) is used when
    // the session aligns on a fixed latency (< 0: not measured yet, the attack is detected instead).
    CaptureResult processCapture (const Channels& raw, double sampleRate, int noteOn, int noteOff, const Job&,
                                  const Session&, const NoiseFloor&, int fixedLatency = -1);

    // one tuning per recorded note: the mean of what its layers and takes measured (detuned oscillators and drift
    // make each measure wander a little; layers of one note must not play out of tune with each other)
    void shareTuning (std::vector<Sample>&);

    // the whole instrument normalised together (keeps the velocity layers' dynamics) or each sample on its own
    void normalise (std::vector<Sample>&, Normalise, double targetDb);
}
