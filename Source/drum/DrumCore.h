// REPLIK8 — DRUM (0.4): eight tracks of drums and their sequencer, after the one of IMPAKT (itself after the Elektron
// machines): 64 steps in four pages, each track its length and speed, a p-lock of any setting on any step, ratchets,
// probability, micro-timing, retrigs in a curve, 128 patterns (A01 .. H16) changing at the end of the one playing, in
// chains. Our own implementation.
//
//  A TRACK    plays one sound at a time (a new hit cuts the last in 3 ms), from its SOURCE:
//               SYNTH    an oscillator reading the synth's cycles (BASIC or CAPTURED, POSITION through the bank),
//                        two octaves under its key (C3: 65 Hz, for skins and kicks)
//               SAMPLE   a sound of its own (dropped on its key, a pad of a radio take), from START, or backwards
//               SLICES   a radio take and its slices: SLICE says which one a hit plays (to lock on each step)
//             then the drum's settings: BODY (the oscillator or the sample's level), TUNE, SWEEP (the pitch falling from that far above, in SWEEP TIME), PMOD (a
//             sine at RATIO times the pitch pushing its phase, fading with the hit), CLICK (the attack's burst),
//             NOISE (white, its TONE from dark to bright, fading faster than the body), ATTACK, DECAY (HOLD at its
//             top: a sample to its end, a synth until the next hit), the track's filter (LP, HP, BP), DRIVE, LEVEL,
//             PAN, CHOKE (a group: a hit cuts the others of its group)
//  A TRIG     its NOTE, VELOCITY, PROBABILITY, MICRO-timing (half a step either way), RETRIG (its rate, how many, the
//             curve of their gaps, of their velocities) or its RATCHET (the step split in 2 .. 16 even hits)
//  THE CLOCK  the host's when it plays (the steps where its song position says), else REPLIK8's own (the play key);
//             SWING delays every second sixteenth
//
// No JUCE in here: the window's part is DrumUi, the project's the processor.

#pragma once

#include "../radio/RadioCore.h"
#include "../dsp/Cycles.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace drum
{
    constexpr int numTracks = 8, numSteps = 64, pageSteps = 16, numPatterns = 128, numSpeeds = 7, numRetrigs = 13;
    constexpr int firstNote = 36;               // C1: track 1 on the keys (C1 .. G1)

    enum Source { synth = 0, sample, slices };

    // a track's settings, each one lockable on a step
    enum Param
    {
        pSource, pNote, pTune, pBank, pPosition, pStart, pSlice, pReverse,
        pSweep, pSweepTime, pPmod, pRatio,
        pAttack, pDecay, pBody, pClick, pNoise, pTone,
        pFilter, pCutoff, pReso, pFEnv, pFAttack, pFDecay, pDrive,
        pLevel, pPan, pChoke,
        pVel, pProb, pMicro, pRetrig, pCount, pCurve, pRVel,
        pStereo,                                  // (0.4.12) a sound in stereo (PAN its balance), else mono (PAN places it)
        numParams
    };
    enum class Kind { linear, log, integer, choice, toggle };
    struct ParamDef
    {
        const char* id;
        const char* name;
        Kind kind;
        float lo, hi, def;
        const char* choices;          // "A|B|C"
        const char* unit;             // how it reads (text())
    };
    const ParamDef& def (int p);
    std::vector<std::string> choicesOf (int p);
    float fromNorm (int p, float t);
    float toNorm (int p, float v);
    float legal (int p, float v);
    std::string text (int p, float v);
    float speedOf (int index);                    // 1/8x .. 2x
    float retrigPpq (int index);                  // the retrig's gap (quarter notes), 0: off

    struct Track
    {
        float p[numParams];
        uint8_t trig[numSteps];
        uint8_t rat[numSteps];                    // a ratchet: the step split in n even hits (1: one)
        float lock[numSteps][numParams];          // NaN: not locked
        uint8_t len = 16, speed = 4;
        void reset (int index);
        float at (int step, int param) const      { const float l = step >= 0 ? lock[step][param] : NAN; return std::isnan (l) ? p[param] : l; }
        bool locked (int step) const;
    };

    struct Pattern
    {
        Track tr[numTracks];
        uint8_t used = 0;
        uint8_t chg = 0;                          // its length for a change (16ths); 0: AUTO, its longest track
        void reset();
        int length16() const;                     // in 16ths at 1x
    };

    // a track's sound when it isn't a synth: a sample, or a take and its slices
    struct Sound
    {
        std::shared_ptr<const radio::Take::Audio> audio;
        std::vector<radio::Slice> slices;
        std::string name;
        int length() const                        { return audio != nullptr ? (int) audio->l.size() : 0; }
    };

    class Engine
    {
    public:
        Engine();
        // the 128 patterns (about 8 MB), made the first time the drums are wanted (message thread)
        void allocate();
        bool allocated() const                    { return armed.load (std::memory_order_acquire); }
        void prepare (double sampleRate, int maxBlock);

        // ---- the message thread: what is edited (single numbers, read by the audio thread as they come)
        Pattern& pattern (int i)                  { return pats[(size_t) (i < 0 ? 0 : (i >= numPatterns ? numPatterns - 1 : i))]; }
        int playingPattern() const                { return playing.load(); }
        int queuedPattern() const                 { return queued.load(); }
        void queue (int i);                       // the next pattern (at once when stopped)
        void setChain (const std::vector<int>&);  // empty: none
        std::vector<int> chain() const;
        std::atomic<bool> mute[numTracks];
        void setSound (int track, const Sound* s) { if (track >= 0 && track < numTracks) nextSound[(size_t) track].store (s, std::memory_order_release); }
        const Sound* soundInUse (int track) const { return track >= 0 && track < numTracks ? usedSound[(size_t) track].load (std::memory_order_acquire) : nullptr; }
        const Sound* soundSet (int track) const   { return track >= 0 && track < numTracks ? nextSound[(size_t) track].load (std::memory_order_acquire) : nullptr; }
        void setBanks (const dsp::CycleBank* basic, const dsp::CycleBank* captured) { banks[0].store (basic); banks[1].store (captured); }

        // ---- the audio thread
        using Event = radio::Engine::Event;      // (the keys: the same as the radio's pads)
        struct Clock { bool running = false; double ppq = 0.0, bpm = 120.0; };
        // n frames of the eight tracks into L, R (written, not added)
        void render (float* L, float* R, int n, const Clock&, float swing, const Event* events, int numEvents);
        void allSoundOff();
        // each track alone (mono, before its pan) over the last render, for FOLLOW; nullptr when it did not fit
        const float* trackSignal (int t) const    { return trackFrames > 0 && t >= 0 && t < numTracks ? trackOut[(size_t) t].data() : nullptr; }
        // WTF?!: the player's ten pulls (normalised, its amount in them), set before render: each hit's settings are
        // pulled by them (TUNE, DECAY, SWEEP, PMOD, CUTOFF, RESO, NOISE, START or POSITION, DRIVE, F.ENV), each
        // track taking them in its own order
        static constexpr int numWtf = 10;
        void setWtf (const float* pulls)          { for (int j = 0; j < numWtf; ++j) wtf[(size_t) j] = pulls[j]; }
        // (0.4.7) what the mods add to each track's settings (normalised; track * modStride + setting), once a
        // block: taken at each hit, and followed while a hit rings by the settings that can move under it
        static constexpr int modStride = 48;
        void setMods (const float* offsets)       { std::copy (offsets, offsets + numTracks * modStride, mods.begin()); }

        // ---- the screen
        std::array<std::atomic<int>, numTracks> stepNow {};      // the step each track plays, -1: stopped
        std::array<std::atomic<float>, numTracks> glow {};        // struck (1, fades on the screen)
        std::atomic<bool> runningNow { false };

    private:
        double sr = 48000.0;
        std::atomic<bool> armed { false };
        std::vector<Pattern> pats;
        std::atomic<int> playing { 0 }, queued { -1 };
        std::array<float, numWtf> wtf {};
        std::array<float, numTracks * modStride> mods {};
        float modded (int track, int param, float v, float extra = 0.0f) const;   // a setting with the mods' share
        std::array<std::atomic<int>, 64> chainSlots {};
        std::atomic<int> chainLen { 0 };
        int chainAt = 0;
        std::array<std::atomic<const Sound*>, numTracks> nextSound {};
        std::array<std::atomic<const Sound*>, numTracks> usedSound {};
        std::array<std::atomic<const dsp::CycleBank*>, 2> banks {};
        radio::Rng rng;

        // ---- the clock
        bool wasRunning = false;
        double patStart = 0.0;                    // where the pattern playing started (quarter notes)
        double lastPpq = 0.0, lastSwitch = -1.0e9;

        // ---- a voice (each track: the one playing and the one fading out)
        struct Voice
        {
            bool active = false;
            int track = 0;
            const Sound* sound = nullptr;
            float p[numParams];
            float base[numParams];                // (p before the mods: they keep moving the settings that can)
            float atHit[numParams];               // (the track's settings when it was hit: turned since, they follow)
            int fromStep = -1;                    // (the step it came from; -1: a key)
            double pos = 0.0, step = 1.0;         // SAMPLE / SLICES: where it reads, how fast (frames)
            int from = 0, to = 0;                 // the frames it plays
            double phase = 0.0, mphase = 0.0;     // SYNTH: the oscillator; PMOD's sine
            float hz = 65.0f, t = 0.0f;           // its pitch; the time since the hit (seconds)
            float env = 0.0f, att = 0.0f, attInc = 1.0f, decCoef = 1.0f, swEnv = 1.0f, swCoef = 1.0f;
            float clickEnv = 0.0f, noiseEnv = 0.0f, noiseCoef = 1.0f, nz = 0.0f;
            float vel = 1.0f, fade = 1.0f, fadeStep = 0.0f;
            float fenv = 0.0f, fatt = 1.0f, fattInc = 1.0f, fdecCoef = 1.0f;   // the filter's envelope
            bool fhold = false;
            float ic[2][2] {}, dz[2] {};
            bool stereo = false;                  // (its sound's two sides kept apart)
            bool hold = false;
        };
        std::array<Voice, numTracks * 2> voices;
        void hit (int track, int step, float velMul, bool retrig);
        void startVoice (int track, const float* p, float vel, const float* base = nullptr, int step = -1);
        void renderVoice (Voice&, float* L, float* R, float* F, int n);   // F: its track's own (mono, added)
        std::array<std::vector<float>, numTracks> trackOut;
        int trackFrames = 0;

        // ---- a track's playing state
        struct Run
        {
            int64_t lastStep = INT64_MIN;          // the last step index handled (absolute, at the track's speed)
            int rtLeft = 0, rtTotal = 0, rtStep = -1;
            double rtNext = 0.0, rtGap = 0.0, rtRatio = 1.0;   // in samples, from the block's start
            float rtRVel = 0.0f;
            bool rtRatchet = false;
        };
        std::array<Run, numTracks> run;
        struct Hit { int at, track, step; float velMul; bool retrig; float velAbs; };   // velAbs >= 0: a key's (no trig)
        std::vector<Hit> hits;
        void schedule (int n, const Clock&, float swing);
        double stepPpq (const Track&) const;
    };
}
