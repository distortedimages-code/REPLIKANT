// The player's filters: models of analog filters, a sample at a time at the oversampled rate, each with its own
// way of saturating (what makes them sound like themselves when pushed), and their own output fed back into their
// cutoff (FM: from roughness to chaos).
//
//   MOOG      transistor ladder, 24 dB (D'Angelo and Valimaki's improved model, tuned on its loop: as in MIFM1)
//   MS-20     Korg 35, both filters of the MS-20 in series: a high pass then a low pass, each with its peak, the
//             saturation inside the resonance loop (Pirkle / Zavalishin; as in MIFM1). SEP places the second one
//   SEM       Oberheim SEM 12 dB state variable: LP, BP, HP, notch; its OTAs pushed by DRIVE (as in MIFM1)
//   OSCAR     the OSCar's two 12 dB filters in series, SEP pulling their cutoffs apart (up to two octaves each way):
//             LP and HP at 24 dB with two knees, BP as wide as the separation, notch from the two in parallel
//   WASP      the EDP Wasp's state variable filter built of CMOS inverters: their lopsided curve in the integrators
//             and the damping (the grit, the hollow resonance)
//   POLIVOKS  the Polivoks' integrators clipping on their op-amps' rails: the resonance past self-oscillation,
//             held by the clipping (its rasp)
//   CUBE      (0.4.4) seven sections of poles and zeros defined at the corners of a cube (Cube.h), CUTOFF its X
//             (so the envelope, the keys and the mods move through it), Y and Z its own; RESO sharpens what
//             resonates, DRIVE pushes it into soft saturation between its sections
//
// Models and their character are "after" the circuits: no measurements of the hardware were made.

#pragma once

#include "Cube.h"
#include <cstdint>

namespace dsp
{
    enum class FilterModel { off, moog, ms20, sem, oscar, wasp, polivoks, cube };
    enum class FilterType { lp, bp, hp, notch };

    struct FilterSettings
    {
        FilterModel model = FilterModel::off;
        FilterType type = FilterType::lp;
        float res = 0.0f;      // 0..1
        float drive = 0.0f;    // 0..1
        float sep = 0.0f;      // 0..1 (MS-20: where the other filter is; OSCAR: the separation)
        float fm = 0.0f;       // 0..1, the knob (its depth on an exponential curve)
        int cube = 0;          // CUBE: which (Cube.h), and its Y and Z (0..1; X is the cutoff)
        float y = 0.0f, z = 0.0f;
        float smoothMs = 10.0f;   // CUBE: X, Y and Z glide (0 .. 200 ms)
        float trimDb = 0.0f;      // CUBE: its output (-24 .. +12 dB)
    };

    class VAFilter
    {
    public:
        void prepare (double sampleRate);     // the rate it runs at (oversampled)
        void reset();
        void set (const FilterSettings&);
        // a sample; cutoff in octaves over 20 Hz (0 = 20 Hz, 10 = 20.5 kHz)
        float tick (float x, float cutoffOct);
        FilterModel model() const           { return target.model; }

        static float fmDepth (float knob);
        // magnitude of the linear part of the response (for drawing), at hz, for a cutoff in Hz
        static float responseDb (const FilterSettings&, float cutoffHz, float hz);

    private:
        float moog (float x, float hz);
        float ms20 (float x, float hz);
        float sem (float x, float hz);
        float oscar (float x, float hz);
        float wasp (float x, float hz);
        float polivoks (float x, float hz);
        float cubeTick (float in, float x);

        double sr = 96000.0, invSr = 1.0 / 96000.0;
        FilterSettings target;
        float res = 0.0f, drive = 0.0f, sep = 0.0f, fmK = 0.0f;   // gliding towards target
        float y1 = 0.0f;                                           // last output (FM)
        // states
        float V[4] {}, dV[4] {}, tV[4] {};             // ladder
        float k35a[3] {}, k35b[3] {};                   // MS-20: high pass, low pass
        float sv[4] {}, nl[4] {};                       // state variable filters (two of them), their last inputs
        float du1 = 0.0f, dcx = 0.0f, dcy = 0.0f;       // SEM's input stage
        bool dstale = true;
        // CUBE: its seven sections (their coefficients, their states), what they were made for, Y and Z gliding
        struct CubeSection { float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4f, mh = 1.0f, mb = 1.4f, ml = 1.0f, s1 = 0.0f, s2 = 0.0f; };
        CubeSection cs[cube::sections];
        float cubeGain = 1.0f, cx = -1.0f, cy = 0.0f, cz = 0.0f, trim = 1.0f;
        float madeX = -1.0f, madeY = -1.0f, madeZ = -1.0f, madeRes = -1.0f;
        int madeCube = -1, cubeWait = 0;
        uint32_t rng = 0x12345678u;
    };
}
