// Banks of single cycles for the SYNTHÉ's oscillators (after the wavetable synths): each cycle 2048 points, kept as
// band-limited copies every half octave (its harmonics cut where they would fold back at the pitch it plays), so
// nothing aliases however high it goes, and what a glide loses on the way is above 17 kHz. The position morphs from
// one cycle to the next.
//
// BASIQUES: 64 cycles made here, from a sine through triangle, saw, square, narrowing pulses, a hard-synced saw and
// a formant. CAPTURÉS: the cycles of a synth (CAPTURE CYCLES), kept with its instrument.

#pragma once

#include <memory>
#include <vector>

namespace dsp
{
    class CycleBank
    {
    public:
        static constexpr int size = 2048;        // points per cycle
        static constexpr int levels = 20;        // level l keeps the harmonics up to 1024 / 2^(l/2)

        // from cycles of `size` points; each loses its DC, and the bank is brought to a peak of 1 (each cycle, if
        // `perCycle`, or all together, keeping their differences)
        explicit CycleBank (const std::vector<std::vector<float>>& cycles, bool perCycle = false);

        int count() const                         { return n; }
        static int harmonicsAt (int level);
        // the level for a frequency at a sample rate: the richest whose highest harmonic stays under Nyquist
        static int levelFor (double hz, double sampleRate);

        // one sample: `position` 0..1 through the bank (the two nearest cycles crossfaded), `phase` 0..1
        float read (float position, double phase, int level) const;
        // the cycle itself (its richest copy), for drawing
        const float* cycle (int c) const          { return table (c, 0) + 1; }

        static std::shared_ptr<const CycleBank> basics();

        // the cycles as they were given (level 0), all one after the other (to save them)
        std::vector<float> flatten() const;

    private:
        const float* table (int c, int l) const   { return data.data() + ((size_t) c * levels + (size_t) l) * stride; }
        static constexpr int stride = size + 4;   // one point before, three after (the cubic read wraps without a test)
        int n = 0;
        std::vector<float> data;
    };

    // one cycle out of a recording: the period nearest `periodHint` frames found around `at` (autocorrelation),
    // started on a rising zero crossing, resampled to CycleBank::size points; empty if nothing periodic is there
    // (`period`: the period found, in frames)
    std::vector<float> extractCycle (const float* x, int length, int at, double periodHint, double* period = nullptr);

    // the period (frames) of what plays around `at`, any pitch from 20 Hz to 2 kHz (0: nothing periodic): the
    // normalised autocorrelation of a copy brought down to about 12 kHz, its shortest lag scoring within 10 % of
    // the best (not an octave down)
    double findPeriod (const float* x, int length, int at, double sampleRate);

    // LISTEN (0.4.5): `wanted` cycles out of a take (a note held while a knob turns): the pitch followed along it, a
    // cycle every few milliseconds, then those kept spaced by how much the timbre moved (more where it changes, few
    // where it holds), each turned to line up with the one before (circular cross-correlation): they morph without
    // a jump. `where`: the frame each comes from
    std::vector<std::vector<float>> cyclesFromTake (const float* x, int length, double sampleRate, int wanted,
                                                    std::vector<int>* where = nullptr);
}
