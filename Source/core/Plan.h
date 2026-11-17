// What a sampling session plays, in what order, and how the recordings are mapped on the keyboard.

#pragma once

#include "RkCore.h"
#include "LoopFinder.h"
#include "Transpose.h"

namespace rk
{
    enum class NoteMode { interval, count, custom };
    enum class VelCurve { linear, soft, hard, custom };   // soft: more layers in the soft playing; hard: in the loud
    enum class Placement { centred, up, down };           // where the recorded note sits in its zone
    enum class Align { perNote, fixedLatency };            // where each recording starts
    enum class Normalise { instrument, perSample, off };
    // RECALER: the note each recording really plays is found (anywhere within half an octave of the note sent) and
    // mapped on its key, then played in tune; FIN: only the cents (within a semitone); NON: the notes as sent
    enum class Tuning { snap, fine, off };

    struct Session
    {
        // the notes
        int lowNote = 36, highNote = 84;
        NoteMode noteMode = NoteMode::interval;
        int interval = 3;
        int noteCount = 8;
        std::vector<int> customNotes;

        // velocities and takes
        int layers = 1;
        VelCurve curve = VelCurve::linear;
        std::vector<int> customVelocities;
        int roundRobins = 1;

        // timing (seconds)
        double hold = 3.0;            // key held down
        double releaseMax = 3.0;      // longest tail recorded after the note-off
        double pause = 0.25;          // silence between two notes
        bool oneShot = false;         // plays to the end whatever the key does (drums, hits)

        // processing
        bool loop = true;
        LoopSettings loopSettings;
        Align align = Align::perNote;
        double preMs = 1.0;           // kept before the attack
        double thresholdDb = 12.0;    // attack detected this far above the noise floor
        Normalise normalise = Normalise::instrument;
        double normaliseDb = -0.3;
        Tuning tuning = Tuning::snap;
        double tuneCents = 0.0;       // the whole instrument retuned by hand (cents, + = higher)
        bool tuneOn() const           { return tuning != Tuning::off; }
        bool removeDc = true;
        int fileBits = 24;            // the WAV files: 24-bit PCM or 32-bit float

        // mapping and the keys in between
        Placement placement = Placement::centred;
        int extend = 12;              // semitones played beyond the recorded range, each side
        Algo algo = Algo::formant;
        double formant = 1.0;
        bool renderKeys = true;       // every key rendered in advance with the chosen algorithm
    };

    struct Job
    {
        int note, velocity, layer, rr;
    };

    struct Zone
    {
        int lo, hi;
    };

    std::vector<int> plannedNotes (const Session&);
    std::vector<int> plannedVelocities (const Session&);     // ascending
    std::vector<Job> planJobs (const Session&);              // note by note, soft to loud, take by take
    double estimatedSeconds (const Session&);

    // What the instrument will weigh, before it is recorded: on disk (the WAV files, the zip shared about the same),
    // and in memory once open (the recordings in float, their octave copies, the keys rendered). The tails are
    // counted as they usually end (before RELACHE MAX, in the noise), as the duration estimate does.
    struct SizeEstimate
    {
        int recordings = 0, renderedKeys = 0;
        double secondsOfAudio = 0.0;          // all the recordings end to end
        int64_t diskBytes = 0, memoryBytes = 0;
    };
    SizeEstimate estimateSize (const Session&, double sampleRate, int channels);

    std::vector<Zone> keyZones (const std::vector<int>& roots, Placement, int lowestKey, int highestKey);
    std::vector<Zone> velocityZones (const std::vector<int>& velocities);

    // the note a sample is mapped on: what it plays (tuning on) or the note sent (tuning off)
    int mappedRoot (const Sample&, const Session&);

    // keys and velocities for each sample, from the roots and velocities present
    void assignZones (std::vector<Sample>&, const Session&);
}
