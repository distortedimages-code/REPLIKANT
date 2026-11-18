// REPLIK8 — RADIO (0.3.5): the radio as an instrument. After IMPAKT's RADIO machine (v87): the same algorithm, ours,
// here in stereo at 48 kHz and listened to without a break (no trig opens it: it plays as long as the tab is shown).
//
//  RECEIVERS  four (RX 1 .. 4), each tuned in the TUNE page to an internet station (an MP3 stream) or to a frequency
//             on a shortwave receiver shared on the network. The network (RadioNet) fetches, decodes and resamples
//             their sound (RadioDec) and hands it to the bank from its own threads (push: a ring per receiver, the
//             network's jitter taken up in it). The audio thread reads it a little faster or a little slower so as to
//             keep about a second in hand (half a second for a shortwave receiver): the stream's clock never drifts
//             from the audio's (0.6 % at most, a few cents). It waits for that much before it plays, and again after
//             the network ran dry. Read, it goes into the receiver's history (its last 21.8 s, on the audio's time).
//  LISTEN     LIVE (what the receiver plays now; TUNE shifts it up or down as a receiver tuned beside it, +-600 Hz,
//             the duck voice of a badly tuned SSB; SQUELCH lets only what is loud enough through), LOOP (the last
//             LENGTH of it played again in time, a new one taken at each turn by chance, GRAB), DIG (LENGTH taken
//             further back at each turn, somewhere in the last DEPTH seconds); TUNE transposes the loops, as a tape
//  AIR        BAND (the receiver's width: FULL .. 250 Hz, four poles), CENTRE (where the band sits), FADING (the
//             signal going and coming back, a notch wandering through it), RATE (how fast), WHISTLE (a carrier's
//             whistle beating against it), CRACKLE (crackle, past 70 % the crashes of a storm far away), DRIVE (the
//             detector and the small speaker pushed), STATIC (the hiss and the crackle of the air, alone when
//             nothing comes in)
//  TAKES      what was heard (the air included) is kept for 32 s: a take is its last few seconds (GRAB) or what
//             was heard between REC and STOP. Cut at its attacks into up to sixteen pads (RadioSlicer), played on
//             the keys from C1, or sent whole (or one pad) to the SAMPLER.
//
// No JUCE in here: the network's side is RadioNet, the window's RadioUi.

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace radio
{
    constexpr int numRx = 4;
    constexpr int rate = 48000;                 // the receivers' rate: what the network hands over, what the history holds
    constexpr int fifoFrames = 1 << 18;         // the feed: 5.4 s at most in hand
    constexpr int histFrames = 1 << 20;         // the history: 21.8 s
    constexpr int loopFrames = 1 << 18;         // a loop (LOOP, DIG): 5.4 s at most
    constexpr int numPads = 16;
    constexpr int firstPadNote = 36;            // C1: the pads on the keys, one a semitone
    constexpr double heardSeconds = 32.0;       // what was heard, kept (the takes come from it)
    constexpr double longestTake = 30.0;
    constexpr int peakFrames = 256;             // the screen: a peak every 256 frames of what was heard

    // a receiver's state (the network sets it): nothing tuned, off, connecting, waiting for its sound (and the
    // audio waiting for enough of it), playing, retuning, the receiver busy (no channel free), failed, on standby
    // (no one listens to it: the network let the connection go)
    enum State { none = 0, off, connecting, buffering, live, tuning, busy, failed, standby };
    enum Kind { web = 0, shortwave = 1 };
    enum Mode { modeLive = 0, modeLoop, modeDig };

    // the knobs' values
    float lenSixteenths (int index);                       // LENGTH: 1/16 .. 2 bars
    float takeSeconds (int index);                          // TAKE: 1 .. 30 s
    inline float clamp01 (float v)                          { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    inline float shiftHz (float st)                         { return st * 25.0f; }           // TUNE on LIVE
    inline float squelchDb (float v)                        { return -66.0f + 56.0f * v; }
    inline float bandHz (float v)                           { return 12000.0f * std::pow (250.0f / 12000.0f, clamp01 (v)); }
    inline float padDecaySeconds (float v)                  { return 0.02f * std::pow (400.0f, clamp01 (v)); }

    //==========================================================================
    // a state-variable filter (the trapezoidal one: stable however fast it is moved)
    struct Svf
    {
        float g = 0.0f, k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
        void set (float hz, float q, float sr);
        inline void tick (float x, float& ic1, float& ic2, float& lp, float& bp, float& hp) const
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
    };

    // a little noise of its own (each voice draws its own)
    struct Rng
    {
        uint32_t s = 0x9e3779b9u;
        inline uint32_t next()           { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        inline float uni()               { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
        inline float bi()                { return uni() * 2.0f - 1.0f; }
    };

    //==========================================================================
    // the receivers: their feed (written by the network) and their history (written by the audio thread)
    struct Rx
    {
        // the feed: the network writes (wr), the audio reads (rd); both count frames on (masked into the ring)
        std::vector<int16_t> fifo;                 // stereo, interleaved
        std::atomic<int> wr { 0 }, rd { 0 };
        std::atomic<int> flushReq { 0 };           // (the network's: its sound from now on is new, the rest dropped)
        std::atomic<int> state { none }, kind { web };
        std::atomic<int> meterC { -32768 };        // the receiver's S-meter (centi-dBm; -32768: none)
        // the audio's side: how it reads the feed, how fast, waiting to fill, how often it ran dry
        int flushSeen = 0, priming = 1, underruns = 0;
        float fpos = 0.0f, ratio = 1.0f, gain = 0.0f;
        // the history (the audio's time): frames written, the fraction of a frame still due
        std::vector<int16_t> hist;                 // stereo, interleaved
        int64_t hw = 0;
        double hacc = 0.0;
        float sig = 0.0f;                          // the signal there (0..1: the air fades in where it's missing)
        // the screen
        std::atomic<float> level { 0.0f }, fill { 0.0f }, speed { 1.0f };
        std::atomic<int> dropouts { 0 }, waiting { 1 };
    };

    class Bank
    {
    public:
        Bank() = default;
        // the rings are made the first time the radio is wanted (message thread): 20 MB nobody pays for otherwise
        void allocate();
        bool ready() const                         { return isReady.load (std::memory_order_acquire); }
        void reset();
        // the network's side (its own threads): stereo frames in (as many as there is room for), a flush, its state
        int  push (int k, const int16_t* stereo, int frames);
        int  space (int k) const;
        void flush (int k);
        void setState (int k, int state, int kind);
        void setMeter (int k, float dbm);
        std::atomic<int> use { 0 };                // the receivers listened to (bits): the network keeps those connected
        // the audio thread, once a block: each receiver's feed read into its history (n frames at sr)
        void advance (int n, double sr);
        // a frame of the history, between its frames (cubic)
        float at (const Rx&, double frame, int channel) const;
        Rx rx[numRx];

    private:
        std::atomic<bool> isReady { false };
    };

    //==========================================================================
    // the takes: what was heard, cut into pads
    struct Slice { int start = 0, end = 0; float strength = 0.0f; };
    struct Take
    {
        struct Audio { std::vector<float> l, r; double sampleRate = 48000.0; };
        std::shared_ptr<const Audio> audio;
        std::vector<Slice> slices;                 // in order, up to numPads
        std::string name;
        int length() const                         { return audio != nullptr ? (int) audio->l.size() : 0; }
    };

    // what the listening is set to (the sound's settings, read once a block)
    struct Settings
    {
        int rx = 0, mode = modeLive, len = 7;
        float squelch = 0.0f, grab = 0.25f, depth = 6.0f, tune = 0.0f, stat = 0.15f, level = 0.8f;
        float band = 0.0f, centre = 1000.0f, fade = 0.0f, fadeRate = 0.3f, whistle = 0.0f, crackle = 0.0f, drive = 0.0f;
        float padTune = 0.0f, padDecay = 1.0f, padLevel = 0.8f;
        bool choke = false;
        double bpm = 120.0, ppq = 0.0;
        bool playing = false;
    };

    class Engine
    {
    public:
        Engine();
        Bank bank;

        // the radio's memory (the receivers' rings, what was heard, the loops), made the first time it's wanted
        // (message thread); until then render() is silence
        void allocate();
        bool allocated() const                     { return armed.load (std::memory_order_acquire); }
        void prepare (double sampleRate, int maxBlock);
        void reset();                              // the listening from silence (the receivers keep their history)

        struct Event { int at, note, velocity; };  // velocity 0: let go
        // n frames of the radio and the pads into L, R (written, not added)
        void render (float* L, float* R, int n, const Settings&, const Event* events, int numEvents);

        // the pads' take (message thread; the caller keeps it alive until takeInUse() moved on)
        void setTake (const Take* t)               { nextTake.store (t, std::memory_order_release); }
        const Take* takeInUse() const              { return usedTake.load (std::memory_order_acquire); }
        const Take* takeSet() const                { return nextTake.load (std::memory_order_acquire); }

        // what was heard (message thread): its last seconds; REC .. STOP
        bool grab (double seconds, Take::Audio& out) const;
        void recStart();
        void recCancel()                           { recFrom.store (-1); }
        bool recording() const                     { return recFrom.load() >= 0; }
        double recSeconds() const;
        bool recStop (Take::Audio& out);
        double sampleRate() const                  { return sr; }

        // the screen: what was heard as peaks (a ring), where the loop plays, the gate, the pads struck
        int peakCount() const                      { return (int) peaks.size(); }
        int64_t peaksWritten() const               { return peakW.load (std::memory_order_acquire); }
        float peakAt (int64_t index) const         { return peaks.empty() ? 0.0f : peaks[(size_t) (index % (int64_t) peaks.size())]; }
        std::atomic<float> loopPos { -1.0f }, loopSeconds { 0.0f }, gateOpen { 1.0f }, heardLevel { 0.0f };
        std::array<std::atomic<float>, numPads> padGlow {};
        std::array<std::atomic<float>, numPads> padAt {};     // where each pad plays (0..1 of its slice), < 0 none

    private:
        double sr = 48000.0;
        std::atomic<bool> armed { false };
        void makeHeard();
        Rng rng;

        // ---- listening
        struct Head { float pos = 0.0f, g = 0.0f, dg = 0.0f; bool on = false; };
        int mode = -1, rxNow = -1;
        double p = 0.0;                            // LIVE: where it reads in the history
        std::vector<int16_t> loops[2];             // LOOP, DIG: the loop playing and the next (stereo)
        int loopLen[2] {}, cur = -1;
        Head heads[2];
        int headBuf[2] {};                         // (the loop each head reads)
        double el = 0.0;                           // how far into the loop's period
        // the shifter (TUNE on LIVE: two allpass chains a quarter turn apart, a channel each), its oscillator
        float ax[2][8][2] {}, ay[2][8][2] {}, adl[2] {};
        float shPh = 0.0f;
        // the air
        float bh1[2][2] {}, bh2[2][2] {}, bl1[2][2] {}, bl2[2][2] {};
        float fdA = 1.0f, fdB = 1.0f, fdPh = 1.0f, fdF = 900.0f, fdN[2][2] {};
        float htPh = 0.0f, htF = 900.0f, htT = 900.0f;
        float ckE = 0.0f, ckLp = 0.0f; int ckLeft = 0;
        float pk[2][3] {};
        float sqE = 0.0f, sqG = 1.0f;
        float dLp[2] {}, dPh = 0.0f, swPh = 0.0f;
        float lvl = 0.0f;
        void takeLoop (const Rx&, int frames, bool dig, float depth);
        void listen (float* L, float* R, int n, const Settings&);

        // ---- what was heard
        std::vector<float> heardL, heardR;
        std::atomic<int64_t> heardW { 0 };
        std::atomic<int64_t> recFrom { -1 };
        std::vector<float> peaks;
        std::atomic<int64_t> peakW { 0 };
        float peakAcc = 0.0f; int peakN = 0;
        bool copyHeard (int64_t from, int64_t to, Take::Audio& out) const;

        // ---- the pads
        struct PadVoice
        {
            bool active = false;
            int pad = 0, note = 0, start = 0, end = 0;
            double pos = 0.0, step = 1.0;
            float gain = 1.0f, env = 0.0f, coef = 1.0f, fade = 1.0f, fadeStep = 0.0f;
            bool hold = true, released = false;
        };
        std::array<PadVoice, 24> voices;
        std::atomic<const Take*> nextTake { nullptr }, usedTake { nullptr };
        const Take* take = nullptr;
        void padOn (int note, int velocity, const Settings&);
        void padOff (int note);
        void pads (float* L, float* R, int n, const Settings&);
    };
}
