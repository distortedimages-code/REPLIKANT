// REPLIKANT core: the data that every part shares (no JUCE in here, so it can be tested on its own).
//
// A Sample is one recording of the synth: one note, one velocity, one take (round robin), with the zone
// of keys and velocities it answers, its loop and its measured tuning.

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace rk
{
    using Channels = std::vector<std::vector<float>>;

    inline double ratioOf (double semitones)    { return std::pow (2.0, semitones / 12.0); }
    inline double noteHz (double note)          { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
    inline float dbToGain (float db)            { return std::pow (10.0f, db / 20.0f); }
    inline float gainToDb (float g)             { return g > 1.0e-10f ? 20.0f * std::log10 (g) : -200.0f; }

    // Note names with C3 = 60, as in Logic, Ableton and Bitwig
    std::string noteName (int note);
    // "C3", "c#3", "Db-1"... back to a note number (-1 when it isn't a note)
    int parseNote (const std::string&);

    struct Loop
    {
        int start = -1, end = -1;     // end is exclusive: the sample at `end` is never played, `start` follows `end - 1`
        int crossfade = 0;            // samples just before `end` that were blended with the ones before `start`
        float quality = 0.0f;         // 0..1, how close the two sides of the seam were before the blend
        bool valid() const            { return start >= 0 && end > start + 1; }
        int length() const            { return end - start; }
    };

    struct Sample
    {
        int root = 60;                // MIDI note that was played
        int velocity = 127;           // velocity it was played with
        int rr = 0;                   // round-robin take
        int keyLo = 60, keyHi = 60;   // zone of keys
        int velLo = 1, velHi = 127;   // zone of velocities
        double sampleRate = 48000.0;
        Channels audio;               // trimmed recording
        int noteOff = -1;             // where the note was released (samples from the start)
        Loop loop;
        float detuneCents = 0.0f;     // measured pitch minus the note, in cents
        bool pitchConfident = false;  // the measure is trustworthy (a clear pitch was found)
        float peak = 0.0f;
        std::string file;             // file name in the instrument's folder

        int length() const            { return audio.empty() ? 0 : (int) audio[0].size(); }
        int channels() const          { return (int) audio.size(); }
        double seconds() const        { return sampleRate > 0 ? length() / sampleRate : 0.0; }
    };

    // a mono mix of all the channels
    std::vector<float> mixDown (const Channels&, int start = 0, int count = -1);
    float peakOf (const Channels&);
    void applyGain (Channels&, float);
}
