// WAV files that carry their mapping: the "smpl" chunk (root note, tuning, sustain loop), read by Logic's
// Sampler, Ableton's Sampler and Simpler, Kontakt, Bitwig, the Elektron and Akai machines...; and the "inst"
// chunk (key and velocity range). Writing 16/24-bit PCM or 32-bit float; reading those and 8/32-bit PCM.

#pragma once

#include "RkCore.h"

namespace rk
{
    struct WavInfo
    {
        int root = -1;                 // -1 = no smpl chunk
        float detuneCents = 0.0f;      // how far the recording is above the root
        Loop loop;
        bool hasRange = false;         // inst chunk
        int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127;
    };

    bool writeWav (const std::string& path, const Channels&, double sampleRate, int bits, const WavInfo&);
    bool readWav (const std::string& path, Channels&, double& sampleRate, WavInfo&);

    // the same, to and from memory
    std::vector<uint8_t> encodeWav (const Channels&, double sampleRate, int bits, const WavInfo&);
    bool decodeWav (const uint8_t* data, size_t size, Channels&, double& sampleRate, WavInfo&);
}
