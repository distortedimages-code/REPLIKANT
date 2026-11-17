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
        int root = 60;                // the note it plays (the note sent, or the one found when tuning is RECALER)
        int sent = -1;                // MIDI note that was sent to the synth (-1: unknown, the root)
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
        bool manualTune = false;      // root and detune set by hand: never measured again
        float peak = 0.0f;
        int attackEnd = -1;           // where its attack ends (frames; 0 = no sharp attack, -1 = not measured yet)
        std::string file;             // file name in the instrument's folder

        int length() const            { return audio.empty() ? 0 : (int) audio[0].size(); }
        int channels() const          { return (int) audio.size(); }
        double seconds() const        { return sampleRate > 0 ? length() / sampleRate : 0.0; }
    };

    // a mono mix of all the channels
    std::vector<float> mixDown (const Channels&, int start = 0, int count = -1);
    float peakOf (const Channels&);

    // The end of a sample's attack: its envelope's first peak (a few ms after it), when it comes within 80 ms of the
    // start; 0 for a slow attack (nothing to keep apart)
    int attackEndOf (const Channels&, double sampleRate);

    // The same sound an octave lower in rate (half the frames), through a long halfband low pass (Kaiser, about
    // 120 dB down above the new band), centred: frame m of the result is at frame 2m of the input
    Channels halfRate (const Channels&);
    void applyGain (Channels&, float);
}
