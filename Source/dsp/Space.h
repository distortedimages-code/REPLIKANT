// SPDX-License-Identifier: MPL-2.0
// ESPACE: MIFM1's METAL / SHIMMER reverb (Copyright (C) 2026 Yoann, mi_metal.h: after what Make Noise's Erbe-Verb
// does), moved off the FM-1: in float, at the full rate, stereo in.
//   A feedback delay network of eight lines, an orthogonal (Hadamard) mix between them, each line read through a slow
//   swaying tap (Thiran all-pass for the fraction: no darkening), its loss a gain for its length (every line dies at
//   the same rate) and Jot's one-pole (the highs die faster than the lows, by as much whatever the size). Before it,
//   a low cut and four all-passes thicken the input.
//   DECAY 0.3 s .. 30 s, then FREEZE at its end. SIZE: from a pipe (metallic) to a vast hall; turned, the taps slide
//   (the pitch bends as on a tape). TONE: left, the highs die faster (a cave); right, less diffusion and sway (metal).
//   SHIMMER: the tail an octave up fed back in (two taps sliding through a 93 ms line), the octaves climbing; its loop
//   gain held under 1 at any decay. MOD: the sway's depth. MIX: dry / wet.

#pragma once

#include <cstdint>
#include <vector>

namespace dsp
{
    struct SpaceSettings
    {
        float size = 0.5f, decay = 0.5f, tone = 0.5f, shimmer = 0.0f, mod = 0.5f, mix = 0.3f;
    };

    class Space
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void set (const SpaceSettings& s)   { target = s; }
        void process (float* left, float* right, int n);   // in place (mix applied)

    private:
        static constexpr int kLines = 8;
        void block (int m);

        struct Line
        {
            std::vector<float> buf;
            int mask = 0, w = 0;
            float read (int back) const { return buf[(size_t) ((w - back) & mask)]; }
            void write (float v)       { w = (w + 1) & mask; buf[(size_t) w] = v; }
        };

        double sr = 48000.0, scaleRate = 48000.0 / 22050.0;
        SpaceSettings target;
        float mix = 0.3f;
        Line ap[4], lines[kLines], shim;
        int apLen[4] {};
        float len[kLines] {};
        // per line: Thiran state, Jot's one-pole, DC blocker, sway phase
        float ay[kLines] {}, lp[kLines] {}, hp[kLines] {}, ph[kLines] {};
        // the block's
        int ti[kLines] {};
        float eta[kLines] {}, g[kLines] {}, pb[kLines] {}, dc[kLines] {}, gp = 0.0f;
        float scale = 0.5f, scaleAim = 0.5f, t60 = 2.0f, hf = 0.25f, diff = 0.7f, sway = 1.0f, inLevel = 1.0f, inGain = 1.0f;
        bool freeze = false;
        float shimAmt = 0.0f, shD = 1.0f, shHp1 = 0.0f, shHp2 = 0.0f, shLp = 0.0f, inHp1 = 0.0f, inHp2 = 0.0f;
        int counter = 0;
    };
}
