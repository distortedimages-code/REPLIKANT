// Making the keys that weren't recorded: a recorded sample moved by some semitones (and cents).
//
//  classic   varispeed with the band-limited sinc: what samplers do, without aliasing nor dulling. The timbre and
//            the envelopes move with the pitch (the "chipmunk" effect grows with the distance).
//  formant   the same varispeed, then the spectral envelope put back where it was (see Formant.h): the synth's
//            filter and body stay put, as they do when the synth itself plays that key. The default.
//  fixedTime the duration is kept as well (Signalsmith Stretch, formants compensated): attacks and decays keep
//            their length, at the cost of a little smearing. For long evolving sounds.
//
// Loops are carried over on whole samples: with varispeed the playback speed is nudged by far less than a cent so
// the loop's length stays a whole number of samples, and the loop region is rendered from a virtually endless
// loop, so it is seamless; after the time-preserving mode the seam is searched again near where it was.

#pragma once

#include "RkCore.h"
#include "Formant.h"

namespace rk
{
    enum class Algo { classic, formant, fixedTime };

    struct RenderSettings
    {
        Algo algo = Algo::formant;
        double formantAmount = 1.0;
        int tailAfterLoop = 512;        // samples kept after a loop's end (some samplers read a little past it)
    };

    // `src` moved by `semitones`. The result keeps the root, velocity and zone of `src` (the caller sets them).
    Sample transposed (const Sample& src, double semitones, const RenderSettings& = {});
}
