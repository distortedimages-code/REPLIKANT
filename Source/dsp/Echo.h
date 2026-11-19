// ECHO (0.4.5): a tape loop read by four heads.
//
// One stereo tape (about ten seconds), written at the input plus what the heads send back. Up to four heads read it,
// spread along TIME (SPREAD 1: at a quarter, half, three quarters and the whole of it; 0: all at TIME), each panned
// to its own side (WIDTH), at a level that keeps their sum where one head would be.
//
// Each head reads through two grains half a cycle apart (sin² windows, summing to one): a head that reads at the
// tape's speed reads plainly; one that reads at another speed (ρ) moves along the tape and is caught back by the
// other grain, like the old delay-line pitch shifters. That gives each head its own way of reading:
//   DIR     FWD (all ahead), REV (all backwards: ρ = -1, the grains as long as the head is far, up to 1 s), ALT (the
//           even heads backwards)
//   MOTION  STILL (the heads in their places), WOBBLE (they sway around them, odd and even heads in opposite ways:
//           the echoes bend up and down against each other), SCAN (they glide along the tape for good, odd heads
//           forward and even heads back: each echo climbs or falls, by up to an octave, SWEEP; with FEEDBACK, they
//           spiral), JUMP (at RATE, each head jumps to another place on the tape, scrubbing there in a few ms)
// FEEDBACK (up to 110 %) goes back through the tape: TONE (left darker, right thinner), WEAR (wow and flutter on the
// tape's speed, a saturation that holds the loop when it grows, the highs worn). WET is the echoes' level: the dry
// sound stays as it is.
//
// Built for REPLIK8 after the multi-head tape echoes (several playback heads over one loop) and the grain-pair
// delay readers of the open source pitch shifters; nothing taken from anyone's code.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace dsp
{
    struct EchoSettings
    {
        float time = 0.4f;          // seconds: where the last head reads (FREE), or the division's length (sync)
        int heads = 4;              // 1..4
        float spread = 1.0f;        // 0..1
        float feedback = 0.35f;     // 0..1.1
        float tone = 0.0f;          // -1 darker .. +1 thinner
        int direction = 0;          // 0 FWD, 1 REV, 2 ALT
        int motion = 0;             // 0 STILL, 1 WOBBLE, 2 SCAN, 3 JUMP
        float sweep = 0.3f;         // 0..1: how far the heads move (WOBBLE ±50 % of their place, SCAN an octave, JUMP)
        float rate = 0.5f;          // Hz (WOBBLE, JUMP)
        float width = 0.7f;         // 0..1
        float wear = 0.2f;          // 0..1
        float mix = 0.35f;          // the echoes' level
    };

    class Echo
    {
    public:
        static constexpr int maxHeads = 4;
        void prepare (double sampleRate);
        void reset();
        void set (const EchoSettings& s)    { target = s; }
        void process (float* left, float* right, int n);   // in place

        // the screen: where each head reads (its distance behind the write head, over TIME), how loud it plays
        std::array<std::atomic<float>, maxHeads> shownAt {}, shownLevel {};
        std::atomic<float> shownSeconds { 0.4f };

    private:
        double sr = 48000.0;
        EchoSettings target;
        std::vector<float> tape[2];
        int mask = 0, w = 0, written = 0;      // (written: since the last reset)
        float time = 0.4f;                     // seconds, glided (a tape slowing down or speeding up)
        float mixNow = 0.35f, fbNow = 0.35f;
        struct Head
        {
            float gain = 0.0f;                 // glided to 1/sqrt(heads), or 0
            float phase = 0.0f;                // the grains' (0..1)
            float period = 4800.0f;            // the grains' length (samples)
            float lfo = 0.0f;                  // WOBBLE's phase (cycles)
            float jump = 0.0f, jumpTo = 0.0f;  // JUMP: where it reads (a fraction of its place), and where it goes
            float place = 1.0f;                // its place along TIME, glided (SPREAD)
        };
        std::array<Head, maxHeads> heads {};
        float jumpClock = 0.0f, wow = 0.0f, flutter = 0.0f;
        float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f;
        float fbL = 0.0f, fbR = 0.0f;          // what the heads sent back, last sample
        uint32_t rng = 0x2545f491u;
        float read (int ch, float back) const;
    };
}
