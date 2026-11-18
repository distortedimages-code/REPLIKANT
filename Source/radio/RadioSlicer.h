// REPLIK8 — RADIO (0.3.5): a take cut at its attacks into pads.
//
// The attacks are found where the spectrum suddenly gains energy (the spectral flux on a log scale, a frame every
// 5.3 ms, 21 ms long), above a threshold that follows what is around (so a loud passage doesn't hide its own hits and
// a quiet one doesn't make a hit of each breath). SENSITIVITY lowers that threshold. Each attack is then placed to the
// sample: where the sound starts to rise, a millisecond before (so the click of a hit is never cut).
//
// The pads take the strongest attacks, in the take's order; each one ends where the next attack (any one, kept or
// not) begins, four seconds at most. A take with hardly any attack is cut evenly.

#pragma once

#include "RadioCore.h"

namespace radio
{
    struct Onset { int at = 0; float strength = 0.0f; };

    // every attack found (in order), with how strong it is
    std::vector<Onset> findOnsets (const float* l, const float* r, int n, double sampleRate, float sensitivity);
    // the pads: up to `count` slices
    std::vector<Slice> slice (const float* l, const float* r, int n, double sampleRate, int count, float sensitivity);
}
