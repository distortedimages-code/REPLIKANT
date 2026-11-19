// The player's effects, after the drive, in this order (each one on or off, each with its mix):
//
//   SPECTRE  after the Rossum Panharmonium: the sound's spectrum analysed slice after slice, its strongest peaks
//            (1 to 33) given to a bank of oscillators that glide to them: the sound rebuilt from its partials, blurred,
//            frozen, transposed and warped, in sines or other waveforms, in a band or around a notch (0.4.2: the
//            slices on the grid, the notes or the attacks; TRANSIENTS; WARP; the XF waves; TRACK; the feedback limited)
//   PITCH    after Eventide's PitchFactor: two pitch-shifted voices, each with its delay and feedback; CRISTAUX plays
//            their grains backwards (the shimmering reverse arpeggios)
//   CHORUS   after Eventide's ModFactor: a three-voice chorus (dark as a BBD), a flanger with its feedback, a vibrato
//   CRUSH    bits and rate: the samples' depth (16 to 1 bit, in between too) and their rate (with jitter), a tone after
//   ESPACE   the reverb (Space.h)
//
// Nothing here is a copy of those devices: they are what was aimed at.

#pragma once

#include "Space.h"
#include <memory>

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace dsp
{
    //==========================================================================
    // SPECTRE: the sound analysed slice after slice (a long FFT of about 85 ms at any rate; with TRANSIENTS, a short one
    // of its last quarter too, taking over above 1 kHz), its magnitudes blurred in time (a time constant, whatever the
    // slices' rate; at its end, frozen), a band or a notch, its strongest peaks (1 to 33) found between bins and given to
    // oscillators: by rank, or each to the nearest one it had (TRACK). Their pitches are moved as they play (OCTAVE,
    // FINE, WARP: drawn towards their weighted centre, or pushed away from it), they glide in semitones (HOLD: they
    // stay), or cross-fade from the old pitch to the new over the slice (the XF waves). The slices: free, on the
    // host's grid, on the notes played or on the attacks of the sound. FEEDBACK sends the result back into the
    // analysis through a limiter.
    struct SpectralSettings
    {
        int voices = 12;          // 1..33
        float slice = 0.4f;       // 0..1: FREE's period (6 ms .. 506 ms), times `mult`
        int mult = 0;             // 0..5: x1, x2, x4, x8, x16, x32
        int clock = 0;            // 0 FREE, 1 SYNC (a division of the host's grid), 2 NOTES (trigger()), 3 ATTACKS
        int division = 6;         // SYNC: index in divisions() (1/16)
        float blur = 0.2f;        // 0..1: 20 ms .. 10 s; 1 frozen
        float glide = 0.3f;       // 0..1: 1 ms .. 10 s; at the end, HOLD
        float centre = 0.5f;      // 0..1 (log 40 Hz .. 10 kHz)
        float bandwidth = 1.0f;   // 0..1 (an octave .. eleven; 1: everything)
        bool notch = false;       // the band rejected instead of kept
        bool transients = false;
        float octave = 0.0f;      // -2..3
        float fine = 0.0f;        // semitones, -7..7
        float warp = 0.0f;        // -1 (all to one pitch) .. 0 .. 1 (spread twice as wide, in octaves)
        int wave = 0;             // 0 sine, 1 triangle, 2 saw, 3 square, 4 pulse, 5 sine XF, 6 saw XF
        int assign = 0;           // 0 RANK, 1 TRACK
        bool freeze = false;
        float feedback = 0.0f;    // 0..1 (limited)
        float mix = 0.5f;
        double ppq = 0.0, bpm = 120.0;   // the host's position at this block's start, its tempo
        bool playing = false;
    };

    class Spectral
    {
    public:
        Spectral();
        ~Spectral();
        void prepare (double sampleRate);
        void reset();
        void set (const SpectralSettings& s)   { target = s; }
        void process (float* left, float* right, int n);
        void trigger()                         { triggered = true; }    // NOTES: a note played (before process)
        // the oscillators now (for drawing): frequency (Hz) and level
        int peaks (float* hz, float* level, int max) const;
        int analyses() const                   { return analysisCount; } // (for the tests)

    private:
        void slice();                          // a new analysis (or, frozen, the peaks found again) and its voices
        void analyse();
        void pick();
        void assign();
        static constexpr int maxVoices = 33;
        int N = 4096;
        double sr = 48000.0;
        SpectralSettings target;
        float mix = 0.5f;
        std::vector<float> ring, window, shortWindow, mags, smooth;
        int ringPos = 0, sinceSlice = 0, sinceAnalysis = 0, analysisCount = 0, pendingIn = -1;
        bool triggered = false, analysedOnce = false;
        long long lastGrid = -1;
        struct FFT;                                    // (Signalsmith's, kept out of this header: it brings
        std::unique_ptr<FFT> fft;                      //  Accelerate, whose names clash with JUCE's)
        std::vector<std::complex<float>> fa, fb;
        std::vector<std::pair<float, float>> found;   // (level, hz): the slice's peaks (kept: no allocation)
        struct Match { float cost; int osc, peak; };
        std::vector<Match> matches;
        float centreHz = 1000.0f;                      // the peaks' weighted geometric centre (WARP's)
        struct Tone { double phase = 0.0; float hz = 0.0f, gain = 0.0f; };
        struct Osc
        {
            double phase = 0.0;
            float hz = 0.0f, lhz = 0.0f, mul = 1.0f;   // its pitch (and in log2), the step it takes a sample
            float peak = 0.0f;                         // the partial it plays (Hz, as analysed)
            float amp = 0.0f, ampTarget = 0.0f;
            float xf = 1.0f, xfInc = 0.0f;             // XF: the new pitch coming in (0 .. 1)
            int young = 0;                             // born: it follows its partial without gliding a window long
            Tone ghost, tail;                          // XF: the old pitch going out; a tone let go quickly
        };
        std::array<Osc, maxVoices> osc;
        float lastOut = 0.0f, fbEnv = 0.0f;
        int waveNow = 0;                               // (another wave: the oscillators ducked 3 ms, then up in 5)
        float dryPow = 0.0f, wetPow = 0.0f, wetGain = 1.0f;   // (0.4.5) the resynthesis kept at the sound's level
        float waveDuck = 1.0f;
        float onsetFast = 0.0f, onsetSlow = 0.0f;      // ATTACKS: two followers of the input
        int onsetHold = 0;
        uint32_t rng = 0x2545F491u;
    };

    //==========================================================================
    struct PitchSettings
    {
        float semis[2] = { 12.0f, -12.0f };   // -24..24
        float fine[2] = { 0.0f, 0.0f };       // cents
        float delay[2] = { 0.0f, 0.0f };      // seconds 0..1
        float level[2] = { 1.0f, 0.0f };
        float feedback = 0.0f;                // 0..0.95
        bool crystals = false;
        float mix = 0.4f;
    };

    // each voice: its input (plus the feedback) pitch-shifted by Signalsmith Stretch (polyphonic, clean), then its
    // delay; CRISTAUX: grains of a delay line played backwards at the interval instead (their seams are the sound)
    class PitchShift
    {
    public:
        PitchShift();
        ~PitchShift();
        void prepare (double sampleRate);
        void reset();
        void set (const PitchSettings& s)     { target = s; }
        void process (float* left, float* right, int n);

    private:
        struct Shifter;                      // Signalsmith Stretch (kept out of this header, as the FFT)
        struct Voice
        {
            std::unique_ptr<Shifter> st;
            std::vector<float> buf;          // the delay after it (and the grains' line)
            int w = 0;
            float grain = 0.0f, delay = 0.0f, ratio = 1.0f;
        };
        void chunk (float* L, float* R, int n);
        float read (const Voice&, float back) const;
        float reverseGrain (Voice&, float ratio);
        double sr = 48000.0;
        PitchSettings target;
        float mix = 0.4f;
        Voice v[2];
        int mask = 0;
        float grainLen = 2400.0f;
        std::vector<float> in, out, fbIn;
    };

    //==========================================================================
    struct ChorusSettings
    {
        int mode = 0;             // 0 chorus, 1 flanger, 2 vibrato
        float rate = 0.4f;        // Hz
        float depth = 0.5f;       // 0..1
        float feedback = 0.0f;    // -0.95..0.95 (flanger)
        float tone = 0.6f;        // 0..1 (dark as a BBD .. clear)
        float mix = 0.5f;
    };

    class Chorus
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void set (const ChorusSettings& s)    { target = s; }
        void process (float* left, float* right, int n);

    private:
        float tap (int ch, float delaySamples) const;
        double sr = 48000.0;
        ChorusSettings target;
        float mix = 0.5f, depth = 0.5f;
        int modeNow = 0;                 // (a mode changed: the wet ducked through silence, 3 ms down, 5 up)
        float duck = 1.0f;
        std::vector<float> buf[2];
        int mask = 0, w = 0;
        double ph = 0.0;
        float lp[2] {}, fbState[2] {};
    };

    //==========================================================================
    struct CrushSettings
    {
        float bits = 16.0f;       // 1..16
        float rateHz = 48000.0f;  // the held rate
        float jitter = 0.0f;      // 0..1
        float tone = 1.0f;        // 0..1 (a low pass after)
        float mix = 1.0f;
    };

    class Crush
    {
    public:
        void prepare (double sampleRate)       { sr = sampleRate; reset(); }
        void reset()                           { held[0] = held[1] = 0; acc = 1.0; lp[0] = lp[1] = 0; mix = target.mix; }
        void set (const CrushSettings& s)      { target = s; }
        void process (float* left, float* right, int n);

    private:
        double sr = 48000.0, acc = 1.0, nextAt = 1.0;
        CrushSettings target;
        float held[2] {}, lp[2] {};
        float mix = 0.0f;                      // gliding towards target.mix (no steps when it moves)
        uint32_t rng = 0xBADC0DEu;
    };
}
