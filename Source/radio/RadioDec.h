// REPLIK8 — RADIO (0.3.5): what the network decodes a receiver's sound with, before it hands it to the bank: an MP3
// stream (minimp3, CC0: radio/ext), plain 16-bit sound, or the IMA ADPCM a shortwave receiver sends; kept in stereo
// (a mono source on both sides) and resampled to radio::rate (a windowed sinc: 12 zero crossings, cut at 45 % of the
// lower rate). The network's threads run it, never the audio's. After IMPAKT's (v87), our own.
#pragma once

#include <cstdint>

namespace radio
{
    struct Dec
    {
        enum { inCap = 32768, outCap = 16384, ring = 1024, nz = 12, res = 256 };
        // MP3: the decoder's state (minimp3's, opaque here), the bytes not decoded yet
        alignas (16) unsigned char mp3st[8192];
        unsigned char in[inCap];
        int inN = 0;
        // the resampler: the source's last frames, how many came, where the next output falls (in source frames),
        // the step (source frames an output), the cut (zero crossings a source frame), the kernel's half length
        float ringL[ring], ringR[ring];
        int srcN = 0;
        double T = 0.0, step = 1.0;
        float c = 0.9f, D = 13.3f, srcRate = 0.0f;
        float proto[nz * res + 2];
        // the output (radio::rate, stereo interleaved): what the network takes
        int16_t out[outCap * 2];
        int outN = 0;                                   // (frames)
        // IMA ADPCM: its index and last value
        int adIdx = 0, adPrev = 0;
        // what came: its rate, channels, kbit/s; frames decoded, bytes skipped
        int rate = 0, chans = 0, kbps = 0;
        uint32_t frames = 0, skipped = 0;
        float gain = 0.9f;

        void reset();                                   // (the kernel's table too: once, before the first use)
        void restart();                                 // a new stream: the decoder and the resampler from silence
        int  inRoom() const                             { return inCap - inN; }
        int  outRoom() const                            { return outCap - outN; }
        // the bytes of an MP3 stream (as many as inRoom); then decodes what it can (as long as the output has room)
        void mp3Feed (const unsigned char* b, int n);
        bool mp3Work();                                 // (more decoded, from what is in hand: false when it needs more)
        // n frames of 16-bit sound, ch channels interleaved, at rate
        void pcmFeed (const int16_t* x, int n, int ch, float rate);
        // a shortwave receiver's IMA ADPCM (4 bits a sample, the low nibble first), at rate
        void adpcmFeed (const unsigned char* b, int n, float rate);
        // what is decoded: up to max frames into dst (stereo interleaved): how many
        int  take (int16_t* dst, int max);

    private:
        void setRate (float r);
        void put (float l, float r);
    };
}
