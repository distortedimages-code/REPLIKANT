// REPLIK8 — WEAVE (0.4.20): up to sixteen sounds laid on the same grid, and a head that jumps from one to the
// other in rhythm. Each sound is fitted to a whole span of bars (AUTO: the power of two nearest its length at the
// tempo, from a beat to sixteen bars) and read where the song is in that span: switching from one to another at a
// step keeps the time, the groove goes on through the sounds.
//
// The weave of a pattern (one, two or four bars) is drawn by rules, the same settings and seed drawing the same:
//
//   BEAT     the sound on the beats: every beat comes back to it
//   BACK     the sound on the second and fourth (the snares' place): it comes back there
//   MOVE     between them, how often the head leaves the sound it is on, and STYLE, where it goes: RANDOM (any other),
//            WALK (the next in the order, now and then the one before), EUCLID (changes spread evenly through the
//            bar, the sounds taken in turn), CALL (each beat answered, its second half, by one other sound)
//   TIME     the step reads another moment of its sound: the one before again, a beat back, half a beat ahead...
//   STUTTER  the step cut in two, three or four repeats of its start
//   REVERSE  the step read backwards
//   MUTATE   at each pass of the pattern, that share of its steps drawn again (and kept so till drawn again)
//
// The beats and the backbeats keep their sound clean (no TIME, STUTTER, REVERSE): they hold the weave together.
// Every jump of the head (a new step, a repeat, a sound's end looping) is crossfaded (XFADE, equal power). TAPE reads
// a sound at the speed its span asks (its pitch follows, like a tape); STRETCH keeps its pitch, in grains (GRAIN).
//
// Audio thread: render. The sounds are the owner's (setThread: a pointer kept alive until the next block at least).
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace weave
{
    constexpr int maxThreads = 16;
    constexpr int maxSteps = 4 * 32;        // four bars of thirty-seconds

    struct Audio
    {
        std::vector<float> l, r;
        double sampleRate = 48000.0;
        int frames() const noexcept { return (int) l.size(); }
    };

    enum Style { styleRandom = 0, styleWalk, styleEuclid, styleCall };
    enum Fit { fitTape = 0, fitStretch };

    struct Settings
    {
        int beat = 0;                       // the sound on the beats (-1: none)
        int back = -1;                      // on the second and fourth (-1: as the other beats)
        float move = 0.5f;
        int style = styleRandom;
        float time = 0.0f, stutter = 0.0f, reverse = 0.0f, mutate = 0.0f;
        int bars = 1;                       // the pattern's: 1, 2, 4
        int grid = 16;                      // steps a bar: 8, 16, 32
        uint32_t seed = 1;
        int fit = fitTape;
        int span = 0;                       // the bars a sound is fitted to: 0 AUTO, 1..7: 1/4, 1/2, 1, 2, 4, 8, 16
        float xfadeMs = 4.0f, grainMs = 60.0f, gain = 1.0f;
    };

    // a step of the weave: its sound, the step whose moment it reads (that many steps away), its repeats, backwards;
    // fixed: a beat or a backbeat
    struct Step
    {
        int8_t thread = -1;                 // -1: nothing (no sound loaded)
        int8_t shift = 0;
        uint8_t ratchet = 1;
        bool reverse = false, fixed = false;
    };

    // the steps of the pattern's pass `cycle` (bars * grid of them, returned) for `count` sounds; force >= 0: every
    // step on that sound (a key held), the rest as drawn
    int plan (const Settings&, int count, int64_t cycle, int force, Step* out);

    // the bars a sound that long is fitted to at that tempo
    double spanBars (const Settings&, double seconds, double bpm);

    struct Clock
    {
        bool running = false;
        double ppq = 0.0, bpm = 120.0;
    };
    struct Event { int pos = 0, note = 0, velocity = 0; };      // velocity 0: off

    class Engine
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setThread (int i, const Audio* a) noexcept
        {
            if (i >= 0 && i < maxThreads)
                threads[(size_t) i].store (a, std::memory_order_release);
        }
        // adds to L, R
        void render (float* L, float* R, int n, const Clock&, const Settings&, const Event* events, int numEvents);

        // for the screen: where the head is (the pattern's step, -1: stopped), the pass, the sound forced by a key
        std::atomic<int> shownStep { -1 }, shownForce { -1 };
        std::atomic<int64_t> shownCycle { 0 };
        std::atomic<float> shownLevel { 0.0f };

    private:
        struct Head
        {
            int thread = -1;
            double frame = 0.0, vel = 0.0;   // in the sound's frames, a sample played
        };
        struct Grain
        {
            bool on = false;
            int thread = 0;
            double pos = 0.0, rate = 1.0;
            int age = 0, len = 1, fadeIn = 0, fadeOut = -1, fadeLen = 1;
        };
        void read (const Audio&, double frame, float& l, float& r) const noexcept;
        void spawn (int thread, double frame, double rate, bool atPeak);
        // STRETCH: a new grain's start moved (a few ms at most) to where it best continues the grain before
        double align (const Audio&, double natural, double target, double dir) const noexcept;
        int lastGrain = -1;

        std::array<std::atomic<const Audio*>, maxThreads> threads {};
        double sr = 48000.0;
        Head cur, ghost;
        int ghostLeft = 0, fadeN = 1, grainLen = 2880;
        double lastTarget = 0.0, lastVel = 0.0;
        int lastThread = -1;
        std::array<Grain, 4> grains {};
        int sinceSpawn = 1 << 30;
        // the pattern drawn for the pass (drawn again when anything it depends on changes)
        std::array<Step, maxSteps> steps {};
        int numSteps = 0;
        int64_t planCycle = -1;
        Settings planned;
        int plannedCount = -1, plannedForce = -2;
        // the keys held (forcing a sound), the clock of their own while the transport stands still
        std::vector<int> held;
        double ownPpq = 0.0;
        bool ownRunning = false;
        float gate = 0.0f;
        float levelSum = 0.0f;
        int levelCount = 0;
    };
}
