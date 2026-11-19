// REPLIK8 — DRUM (0.4.17): the drums' own wavetables, made here (no data from anywhere), twelve cycles each, to be
// scanned by a hit:
//   SKIN   a sine bent more and more (phase distortion, a touch of the octave): from a round thump to a knock
//   FOLD   a sine folded harder and harder: from soft to a buzz
//   METAL  sparse high partials, more and more of them: bells, tins, cymbals (with the second oscillator's ring)
//   VOX    a voice's formants going through a, e, i, o, u
//   DIGI   a sine on fewer and fewer steps, then narrower and narrower pulses: the old digital machines
//   GRIT   a different random spectrum in each cycle: scanned fast, a noise of its own
// No JUCE in here.

#pragma once

#include "../dsp/Cycles.h"

namespace drum
{
    constexpr int numOwnTables = 6;
    // the table (0 .. numOwnTables - 1), made the first time any is asked for (call prepareTables() off the audio thread)
    const dsp::CycleBank* ownTable (int index);
    void prepareTables();
}
