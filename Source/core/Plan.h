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
        bool tune = true;             // measure each note's pitch and play it in tune
        bool removeDc = true;

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

    std::vector<Zone> keyZones (const std::vector<int>& roots, Placement, int lowestKey, int highestKey);
    std::vector<Zone> velocityZones (const std::vector<int>& velocities);

    // keys and velocities for each sample, from the roots and velocities present
    void assignZones (std::vector<Sample>&, const Session&);
}
