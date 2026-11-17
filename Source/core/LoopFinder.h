// Finding a sustain loop in a held note, and blending its seam.
//
// The search looks at the held part of the note (between the end of the attack and the note-off), prefers long
// loops (an analog synth drifts and breathes, a short loop freezes it), follows a slow modulation when there is
// one (an LFO, a PWM sweep: the loop is then a whole number of its periods), and scores each pair of points on
// how alike the waveform is on both sides of the seam and how close the level is. The seam is then crossfaded
// with gains that keep the power constant for the correlation found (no dip, no bump).

#pragma once

#include "RkCore.h"

#include <functional>

namespace rk
{
    struct LoopSettings
    {
        double searchStart = 0.30;   // where the loop may start, as a fraction of the held part
        double searchEnd = 0.95;     // where it may end, idem
        double minSeconds = 0.20;    // shortest loop
        double crossfadeSeconds = 0.06;
    };

    struct LoopReport
    {
        Loop loop;
        double modulationSeconds = 0.0;   // slow modulation period found (0 = none)
        double periodSamples = 0.0;       // pitch period used (0 = unpitched)
    };

    // heldStart..heldEnd: the part where the key is down (after trimming). f0: the pitch played (0 = unknown).
    LoopReport findLoop (const Channels&, double sampleRate, int heldStart, int heldEnd, double f0, const LoopSettings&);

    // Looks for the best seam near a known loop (after a time-preserving transposition moved the waveform):
    // start and end each within `radius` samples of the given ones.
    Loop refineLoop (const Channels&, double sampleRate, Loop approx, int radius, double f0);

    // Blends the `crossfade` samples before loop.end with the ones before loop.start, in place. The crossfade is
    // shortened if the loop is too short or starts too early for it. Sets loop.crossfade.
    void bakeCrossfade (Channels&, Loop& loop, int crossfade);

    // How alike the two sides of a seam are (0 = identical, 1 = unrelated), on a window of `window` samples
    double seamDistance (const std::vector<float>& mono, int s, int e, int window);
}
