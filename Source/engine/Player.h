// Plays an instrument: velocity layers, round robins, sustain loops, the rendered keys as they are (no
// interpolation at all), a live band-limited interpolation only where a key is shifted (pitch bend, keys that
// weren't rendered). Audio thread only, apart from the parameters (atomics).

#pragma once

#include <JuceHeader.h>
#include "Instrument.h"

#include <array>
#include <atomic>

class Player
{
public:
    struct Params
    {
        std::atomic<float> attackMs { 1.0f }, releaseMs { 300.0f }, velocity { 0.7f }, gainDb { 0.0f }, bendRange { 2.0f };
        std::atomic<bool> loops { true };
    };

    void prepare (double sampleRate);
    void setInstrument (const Instrument*);       // audio thread; the caller keeps it alive
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi);   // adds to `out`
    void allNotesOff (bool immediately);

    Params params;

    // for the editor: the voice heard last (sample index and where it is, in that sample's frames)
    std::atomic<int> shownSample { -1 };
    std::atomic<float> shownPosition { 0.0f };
    std::atomic<int> activeVoices { 0 };
    std::array<std::atomic<bool>, 128> keyDown {};    // keys sounding (for the keyboard)

private:
    struct Voice
    {
        bool active = false, held = false, releasing = false;
        int note = 0, velocity = 0, index = -1;
        const rk::Sample* s = nullptr;
        double pos = 0.0, shift = 0.0, rateScale = 1.0;
        float gain = 1.0f, env = 0.0f, attackStep = 1.0f, releaseCoef = 0.999f;
        uint64_t age = 0;
        int originalLength = 0;
    };

    void noteOn (int note, int velocity);
    void noteOff (int note);
    void renderVoice (Voice&, float* const* out, int numOut, int start, int n);
    Voice& freeVoice();

    static constexpr int maxVoices = 48;
    std::array<Voice, maxVoices> voices;
    const Instrument* ins = nullptr;
    double sr = 48000.0;
    double bend = 0.0;                  // -1..1
    bool sustain = false;
    std::array<bool, 128> sustained {};
    std::array<int, 128> rrCounter {};
    uint64_t clock = 0;
};
