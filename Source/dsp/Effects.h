// The player's effects, after the drive, in this order (each one on or off, each with its mix):
//
//   SPECTRE  after the Rossum Panharmonium: the sound's spectrum analysed slice after slice, its strongest peaks
//            (1 to 33) given to a bank of oscillators that glide to them: the sound rebuilt from its partials, blurred,
//            frozen, transposed, in sines or other waveforms, between a centre frequency and a bandwidth
//   PITCH    after Eventide's PitchFactor: two pitch-shifted voices, each with its delay and feedback; CRISTAUX plays
//            their grains backwards (the shimmering reverse arpeggios)
//   CHORUS   after Eventide's ModFactor: a three-voice chorus (dark as a BBD), a flanger with its feedback, a vibrato
//   CRUSH    bits and rate: the samples' depth (16 to 1 bit, in between too) and their rate (with jitter), a tone after
//   ESPACE   the reverb (Space.h)
//
// Nothing here is a copy of those devices: they are what was aimed at.

#pragma once

#include "Space.h"
#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wshadow-field"
 #pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
 #pragma clang diagnostic ignored "-Wshadow-uncaptured-local"
 #pragma clang diagnostic ignored "-Wfloat-equal"
 #pragma clang diagnostic ignored "-Wsign-conversion"
 #pragma clang diagnostic ignored "-Wimplicit-int-conversion"
 #pragma clang diagnostic ignored "-Wshorten-64-to-32"
 #pragma clang diagnostic ignored "-Wconversion"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wextra-semi"
 #pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
 #pragma clang diagnostic ignored "-Wcast-align"
 #pragma clang diagnostic ignored "-Wdeprecated"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif
#include "signalsmith-linear/fft.h"
#include "signalsmith-stretch.h"
#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace dsp
{
    //==========================================================================
    struct SpectralSettings
    {
        int voices = 12;          // 1..33
        float slice = 0.4f;       // 0..1: the analysis' rate (fast .. slow)
        float blur = 0.2f;        // 0..1
        float glide = 0.3f;       // 0..1
        float centre = 0.5f;      // 0..1 (log 40 Hz .. 10 kHz)
        float bandwidth = 1.0f;   // 0..1 (an octave .. everything)
        float octave = 0.0f;      // -2..2 (semitones would be octave * 12)
        int wave = 0;             // 0 sine, 1 triangle, 2 saw, 3 square
        bool freeze = false;
        float feedback = 0.0f;    // 0..1
        float mix = 0.5f;
    };

    class Spectral
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void set (const SpectralSettings& s)   { target = s; }
        void process (float* left, float* right, int n);
        // the oscillators now (for drawing): frequency (Hz) and level
        int peaks (float* hz, float* level, int max) const;

    private:
        void analyse();
        static constexpr int N = 4096, maxVoices = 33;
        double sr = 48000.0;
        SpectralSettings target;
        float mix = 0.5f;
        std::vector<float> ring, window, mags, smooth;
        int ringPos = 0, sinceSlice = 0;
        signalsmith::linear::Pow2FFT<float> fft;
        std::vector<std::complex<float>> fa, fb;
        std::vector<std::pair<float, float>> found;   // (level, hz): the slice's peaks (kept: no allocation)
        struct Osc
        {
            double phase = 0.0;
            float hz = 0.0f, hzTarget = 0.0f, amp = 0.0f, ampTarget = 0.0f;
        };
        std::array<Osc, maxVoices> osc;
        float lastOut = 0.0f, level = 0.0f;
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
        void prepare (double sampleRate);
        void reset();
        void set (const PitchSettings& s)     { target = s; }
        void process (float* left, float* right, int n);

    private:
        struct Voice
        {
            signalsmith::stretch::SignalsmithStretch<float> st;
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
        void reset()                           { held[0] = held[1] = 0; acc = 1.0; lp[0] = lp[1] = 0; }
        void set (const CrushSettings& s)      { target = s; }
        void process (float* left, float* right, int n);

    private:
        double sr = 48000.0, acc = 1.0, nextAt = 1.0;
        CrushSettings target;
        float held[2] {}, lp[2] {};
        uint32_t rng = 0xBADC0DEu;
    };
}
