// ORBITE: three oscillators bound together, a carrier and two satellites (after the Trident's carrier and its two
// modulation oscillators, taken our own way).
//
// Each satellite is hard-synced to the carrier (a satellite tuned above it adds bright overtones instead of beating)
// and multiplies it: ANNEAU, a ring modulation that stays harmonic since both restart together each cycle (synchro
// off: the clang of a free ring modulator). An algorithm wires them:
//   PARALLÈLE  the carrier times each satellite
//   CASCADE    satellite 2 rings satellite 1, which rings the carrier
//   FM         satellite 1 pushes the carrier's phase (through zero), satellite 2 rings the result
//   BOUCLE     as PARALLÈLE, the output fed back into the satellites' phase (grit that stays on the note)
// The second satellite restarts at its PHASE, and that phase can circle (ORBITE, in Hz): the timbre turns while the
// pitch stays put.
//
// The carrier morphs triangle > saw > square; each wave has its SYMÉTRIE (a skewed triangle, a kinked saw, the
// pulse width). Computed at four times the rate, every step (a wrap, a pulse edge, a sync reset) smoothed by a polyBLEP
// placed where it falls between two samples, then brought down by a halfband filter: clean up the keyboard.

#pragma once

#include "Oversampler.h"

namespace dsp
{
    struct OrbiteSettings
    {
        int algo = 0;                    // 0 parallèle, 1 cascade, 2 fm, 3 boucle
        float shape = 1.0f;              // the carrier: 0 triangle, 1 saw, 2 square (in between: blends)
        float sym = 0.0f;                // -1..1
        int wave[2] { 0, 1 };            // the satellites: 0 triangle, 1 saw, 2 square
        float symS[2] { 0.0f, 0.0f };
        float ring[2] { 0.5f, 0.0f };    // ANNEAU (FM: satellite 1's index)
        bool sync[2] { true, true };
        float phase2 = 0.0f;             // where satellite 2 restarts (0..1 of a cycle)
    };

    class Orbite
    {
    public:
        Orbite();
        void reset();
        // one sample at the host's rate. inc: cycles per host sample of the carrier and the satellites; orbit: how
        // far satellite 2's restart has circled (0..1)
        float process (const OrbiteSettings&, double incC, double inc1, double inc2, double orbit);

        // the shapes (p in 0..1), for the screen too
        static float carrierWave (double p, float shape, float sym);
        static float satelliteWave (double p, int wave, float sym);

    private:
        struct Osc { double ph = 0.0, eff = 0.0, offPrev = 0.0; float pend = 0.0f; };
        float tick (const OrbiteSettings&, double incC, double inc1, double inc2, double orbit);
        Osc car, sat[2];
        float fb = 0.0f, lastA = 0.0f;
        double orbitWas = 0.0;
        bool fresh = true;
        Halfband down4, down;        // 4x to 2x, 2x to 1x
    };
}
