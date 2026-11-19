// The modulators, as in IMPAKT (after Torso's: arm one, turn what it should move). Four of them, each of a type:
//
//   ONDE      an LFO: sine, triangle, ramps up and down, square, S&H, SLEW (a new value each cycle, reached gliding)
//   PAS       a sequence of up to 16 steps, each a division long (1/16: sixteen sixteenths), gliding with LISSAGE
//   HASARD    a loop of LEN draws, one a division, that comes back each time round; VARI redraws some of them as it
//             goes (0: the same loop forever, 100 %: never the same); LISSAGE glides from one to the next
//   ENV       an envelope (attack, decay, sustain, release) that each note fires, held while keys are down
//   SUIVEUR   follows the loudness of a sound (the audio input, or the instrument), its rise and fall
//
// On the tempo (divisions from 1/32 to 4 bars, dotted and triplets: the host's bar when it plays, REPLIKANT's tempo
// otherwise) or free (Hz). ONDE, PAS and HASARD go -1..1 (or 0..1, POLARITY +); ENV and SUIVEUR 0..1.

#pragma once

#include <array>
#include <cstdint>

namespace dsp
{
    enum class ModType { wave, steps, random, envelope, follower };
    enum class ModShape { sine, triangle, sawUp, sawDown, square, sampleHold, slew };

    struct ModSettings
    {
        ModType type = ModType::wave;
        ModShape shape = ModShape::sine;
        bool sync = true;
        int division = 6;           // index in divisions() (1/16 by default)
        float hz = 1.0f;            // free rate (a cycle, a step, a draw)
        float smooth = 0.0f;        // 0..1
        bool unipolar = false;      // 0..1 instead of -1..1 (ENV and FOLLOW too: ± sets them around the setting)
        bool retrigger = false;     // starts again at each note
        int length = 16;            // PAS: steps (1..16); HASARD: draws (1..32)
        std::array<float, 16> stepValues {};   // -1..1
        float vari = 0.0f;          // HASARD: 0..1
        float attack = 0.01f, decay = 0.3f, sustain = 0.5f, release = 0.4f;   // seconds; ENV, SUIVEUR (attack, release)
        float gain = 1.0f;          // SUIVEUR: the level that reads as 1
    };

    struct Division
    {
        const char* name;
        double beats;               // its length in quarter notes
    };
    const Division* divisions();
    int numDivisions();

    class Modulator
    {
    public:
        Modulator();
        void set (const ModSettings& s)    { settings = s; }
        const ModSettings& get() const     { return settings; }

        // the value now, synced to the host (its position in quarter notes), or moved on by dt seconds at bpm
        float atPpq (double ppq, double bpm, double dtSeconds);
        float advance (double dtSeconds, double bpm);
        void retrigger()                   { phase = 0.0; cycle = 0; fresh = true; }
        // ENV: keys down or up; SUIVEUR: the level of its sound over the last few samples
        void gate (bool on);
        void input (float level)           { inLevel = level; }
        void reseed (uint32_t seed);       // HASARD: another loop

        float value() const                { return out; }
        double getPhase() const            { return phase; }
        int64_t cycles() const             { return cycle; }
        int currentStep() const            { return stepIndex; }
        bool isUnipolar() const;

    private:
        float waveAt (double ph, bool newCycle);
        float sequenced (bool newCycle);
        float envelope (double dt);
        float follow (double dt);
        float finish (float v, double dt);
        float rnd();

        ModSettings settings;
        double phase = 0.0;          // in the cycle (ONDE) or the step (PAS, HASARD)
        int64_t cycle = 0;           // cycles (steps, draws) so far
        bool fresh = true;
        int stepIndex = 0;
        float out = 0.0f, held = 0.0f, prevHeld = 0.0f, smoothed = 0.0f, prevStep = 0.0f;
        std::array<float, 32> draws {};
        // ENV
        int stage = 0;               // 0 idle, 1 attack, 2 decay / sustain, 3 release
        float env = 0.0f;
        // SUIVEUR
        float inLevel = 0.0f, followed = 0.0f;
        uint32_t rng = 0x1234567u;
    };
}
