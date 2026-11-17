// A small analog-like synth inside REPLIKANT, to try sampling without any gear (and for the tests): two
// detuned saws and a sub through a resonant low-pass with its envelope, a slow drift per voice, slightly out of
// tune as a real one, a noise floor and a few milliseconds of latency, like a line input.

#pragma once

#include <JuceHeader.h>

#include <array>

class TestSynth
{
public:
    void prepare (double sampleRate);
    void handle (const juce::MidiMessage&);
    void render (float* const* out, int numChannels, int numSamples);   // replaces the buffer
    void reset();

    float tuningCents = 7.0f;     // the whole synth is a little sharp

private:
    struct Voice
    {
        bool active = false, held = false;
        int note = 0;
        float vel = 0.0f;
        double ph1 = 0, ph2 = 0, ph3 = 0, drift = 0, driftRate = 0;
        float amp = 0, fenv = 0;
        int stageA = 0, stageF = 0;       // 0 attack, 1 decay/sustain, 2 release
        float lp = 0, bp = 0;             // filter state
        float lp2 = 0, bp2 = 0;
        uint64_t age = 0;
    };
    float poly (double t, double dt) const;
    std::array<Voice, 12> voices;
    double sr = 48000.0;
    juce::Random rng { 42 };
    std::vector<float> delayL, delayR;
    int delayPos = 0, delaySize = 0;
    uint64_t clock = 0;
};
