// Formant correction for a sound that was played faster or slower (varispeed).
//
// Varispeed moves everything: the harmonics (that's the point) but also the spectral envelope, the resonances
// that make the timbre (the synth's filter, its body). This puts the envelope back where it was: each frame's
// spectrum is analysed (an envelope that follows the tops of the harmonics, spaced as the known pitch says), and
// the frame is equalised by the ratio between the envelope where it should be and where varispeed moved it.
// The harmonics, their phases and the transients are not resynthesised, only gently equalised over time, which
// is why this sounds much cleaner than a phase vocoder.

#pragma once

#include "RkCore.h"

namespace rk
{
    struct FormantSettings
    {
        double amount = 1.0;        // 0 = plain varispeed, 1 = envelope fully put back
        int iterations = 3;         // true-envelope iterations
        double maxDb = 24.0;        // largest boost or cut
    };

    // `y` has been played `ratio` times faster (its pitch is now f0Out). In place.
    void correctFormants (Channels& y, double sampleRate, double ratio, double f0Out, const FormantSettings& = {});

    // The smooth envelope of a log-magnitude spectrum (bins 0..N/2) that follows the tops of the harmonics
    // (`binsPerHarmonic` apart). trueEnvelope: the classic iterated-cepstrum one, kept for comparison.
    std::vector<float> harmonicEnvelope (const std::vector<float>& logMag, double binsPerHarmonic);
    std::vector<float> trueEnvelope (const std::vector<float>& logMag, int order, int iterations);
}
