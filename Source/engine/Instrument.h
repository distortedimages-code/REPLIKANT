// An instrument: the recorded samples, the keys rendered from them, and what answers each key.
// Built once (by the worker), then only read: the audio thread plays it, the editor draws it.
//
// On disk, an instrument is a folder:
//     <Name>/<Name>.replikant      the session and the mapping (JSON)
//     <Name>/Samples/*.wav         the recordings, 24-bit, with their root, loop and range inside (smpl / inst)
// The WAV folder alone can be dropped in Logic's Sampler, Ableton, Kontakt...

#pragma once

#include <JuceHeader.h>
#include "../core/Plan.h"
#include "../core/Analysis.h"

#include <array>
#include <memory>

struct Instrument
{
    juce::String name;
    rk::Session session;
    double sampleRate = 48000.0;
    std::vector<rk::Sample> samples;          // as recorded (processed)
    std::vector<rk::Sample> keys;             // rendered keys
    std::vector<std::vector<int>> keyOf;      // per sample: index in `keys` for key keyLo + j, -1 = play the sample itself
    std::array<std::vector<int>, 128> byKey;  // samples whose zone holds the key
    juce::File folder;                        // where it is saved (empty = not yet)
    rk::NoiseFloor noise;

    // what plays for a key and velocity: the sample (or its rendered key) and the shift still to apply live
    struct Source
    {
        const rk::Sample* sample = nullptr;
        double semitones = 0.0;
        int index = -1;                        // in `samples`
    };
    // the round-robin takes answering (key, velocity), in rr order
    std::vector<Source> sourcesFor (int key, int velocity) const;

    // the live shift of sample i at key k (k - root - its measured detune when tuning is on)
    double shiftFor (int sampleIndex, int key) const;

    void rebuildIndex();                       // byKey from the zones; keyOf reset to -1 if sizes changed
    int64_t memoryBytes() const;
    int countKeysToRender() const;
    bool hasLoops() const;
};

using InstrumentPtr = std::shared_ptr<const Instrument>;

namespace instrumentio
{
    juce::File defaultRoot();                                       // ~/Music/REPLIKANT
    juce::String safeName (const juce::String&);
    juce::String sampleFileName (const juce::String& instrument, const rk::Sample&);

    juce::var sessionToVar (const rk::Session&);
    rk::Session sessionFromVar (const juce::var&);

    // writes the folder (the samples, then the description); returns the description file
    juce::Result save (Instrument&, const juce::File& folder);
    // reads a folder or its .replikant file; keys are not rendered
    std::shared_ptr<Instrument> load (const juce::File&, juce::String& error);

    // exports. allKeys: every key as its own sample (rendered with REPLIKANT's transposition), otherwise the
    // recordings, mapped
    juce::Result exportWavFolder (const Instrument&, const juce::File& folder, bool allKeys);
    juce::Result exportBitwig (const Instrument&, const juce::File& file, bool allKeys);
}
