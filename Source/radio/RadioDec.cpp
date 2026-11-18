// REPLIK8 — RADIO (0.3.5): the network's decoder (RadioDec.h). Our own, but the MP3 decoder itself: minimp3 (lieff,
// CC0 1.0: radio/ext/minimp3.h).
#include "RadioDec.h"
#include "RadioCore.h"

#include <cmath>
#include <cstring>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wcast-align"
#endif
#include "ext/minimp3.h"
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace radio
{
static_assert (sizeof (mp3dec_t) <= sizeof (((Dec*) nullptr)->mp3st), "minimp3's state fits");

namespace
{
    // the IMA ADPCM tables (the standard's)
    const short imaStep[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
        4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
        24623, 27086, 29794, 32767 };
    const signed char imaIdx[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
    constexpr float pi = 3.14159265358979f;
    inline int16_t toShort (float y)
    {
        return (int16_t) (y > 32767.0f ? 32767 : (y < -32767.0f ? -32767 : (int) (y + (y >= 0.0f ? 0.5f : -0.5f))));
    }
}

void Dec::reset()
{
    // the kernel: a sinc under a Blackman window, nz zero crossings each side, res points between two
    for (int k = 0; k <= nz * res + 1; ++k)
    {
        const float u = (float) k / (float) res;
        const float x = std::min (u / (float) nz, 1.0f);
        const float w = 0.42f + 0.5f * std::cos (pi * x) + 0.08f * std::cos (2.0f * pi * x);
        const float s = k == 0 ? 1.0f : std::sin (pi * u) / (pi * u);
        proto[k] = u >= (float) nz ? 0.0f : s * w;
    }
    gain = 0.9f;
    restart();
}

void Dec::restart()
{
    mp3dec_init ((mp3dec_t*) mp3st);
    inN = 0;
    outN = 0;
    std::memset (ringL, 0, sizeof (ringL));
    std::memset (ringR, 0, sizeof (ringR));
    srcN = 0; T = 0.0; srcRate = 0.0f; step = 1.0; c = 0.9f; D = (float) nz / c;
    adIdx = 0; adPrev = 0;
    rate = 0; chans = 0; kbps = 0; frames = 0; skipped = 0;
}

void Dec::setRate (float r)
{
    if (r == srcRate)
        return;
    srcRate = r;
    step = (double) r / (double) radio::rate;
    c = 0.9f * std::min (1.0f, (float) radio::rate / r);
    D = (float) nz / c;
    T = (double) srcN;                                // (a new rate: its first output from here)
}

// a source frame in; every output it completes out
void Dec::put (float l, float r)
{
    ringL[srcN & (ring - 1)] = l;
    ringR[srcN & (ring - 1)] = r;
    ++srcN;
    if (srcN >= (1 << 30))
    {
        srcN -= 1 << 29;
        T -= (double) (1 << 29);
    }
    while (T + (double) D < (double) srcN && outN < outCap)
    {
        const int j0 = (int) std::floor (T - (double) D) + 1, j1 = (int) std::floor (T + (double) D);
        float yl = 0.0f, yr = 0.0f;
        for (int j = j0; j <= j1; ++j)
        {
            const float u = std::abs ((float) (T - (double) j)) * c * (float) res;
            const int i = (int) u;
            if (i >= nz * res || j < 0)
                continue;
            const float f = u - (float) i, h = proto[i] + (proto[i + 1] - proto[i]) * f;
            yl += ringL[j & (ring - 1)] * h;
            yr += ringR[j & (ring - 1)] * h;
        }
        out[2 * outN] = toShort (yl * c * gain);
        out[2 * outN + 1] = toShort (yr * c * gain);
        ++outN;
        T += step;
    }
}

void Dec::pcmFeed (const int16_t* x, int n, int ch, float r)
{
    if (n <= 0 || ch <= 0 || r < 4000.0f)
        return;
    setRate (r);
    for (int i = 0; i < n; ++i)
    {
        if (ch == 1)
            put ((float) x[i], (float) x[i]);
        else
            put ((float) x[i * ch], (float) x[i * ch + 1]);
    }
}

void Dec::adpcmFeed (const unsigned char* b, int n, float r)
{
    if (n <= 0 || r < 4000.0f)
        return;
    setRate (r);
    for (int i = 0; i < n; ++i)
        for (int h = 0; h < 2; ++h)
        {
            const int code = h ? b[i] >> 4 : b[i] & 15;
            const int stp = imaStep[adIdx];
            adIdx += imaIdx[code];
            adIdx = adIdx < 0 ? 0 : (adIdx > 88 ? 88 : adIdx);
            int d = stp >> 3;
            if (code & 1) d += stp >> 2;
            if (code & 2) d += stp >> 1;
            if (code & 4) d += stp;
            if (code & 8) d = -d;
            adPrev += d;
            adPrev = adPrev < -32768 ? -32768 : (adPrev > 32767 ? 32767 : adPrev);
            put ((float) adPrev, (float) adPrev);
        }
}

void Dec::mp3Feed (const unsigned char* b, int n)
{
    if (n > inRoom())
        n = inRoom();
    if (n > 0)
        std::memcpy (in + inN, b, (size_t) n);
    inN += n;
    while (mp3Work()) {}
}

bool Dec::mp3Work()
{
    // (a frame makes at most 1152 frames: as many as 6912 outputs from 8 kHz). The decoder wants the next frame's
    // header in hand too (else it takes the frame for noise and looks again): 4 KB at least (two of the largest
    // frames), 16 KB to find the first one
    if (inN < (frames ? 4096 : 16384) || outRoom() < 7000)
        return false;
    mp3dec_frame_info_t info;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    const int ns = mp3dec_decode_frame ((mp3dec_t*) mp3st, in, inN, pcm, &info);
    int used = info.frame_bytes;
    if (used <= 0)
    {
        // nothing found in what is in hand: wait for more, unless it is full of something else (half of it dropped)
        if (inN < inCap)
            return false;
        used = inCap / 2;
        skipped += (uint32_t) used;
    }
    if (used > inN)
        used = inN;
    std::memmove (in, in + used, (size_t) (inN - used));
    inN -= used;
    if (ns > 0 && info.hz >= 8000 && info.channels > 0)
    {
        rate = info.hz;
        chans = info.channels;
        kbps = info.bitrate_kbps;
        ++frames;
        pcmFeed (pcm, ns, info.channels, (float) info.hz);
    }
    else if (ns == 0)
        skipped += (uint32_t) used;
    return true;
}

int Dec::take (int16_t* dst, int max)
{
    const int n = outN < max ? outN : max;
    std::memcpy (dst, out, (size_t) n * 2 * sizeof (int16_t));
    std::memmove (out, out + 2 * n, (size_t) (outN - n) * 2 * sizeof (int16_t));
    outN -= n;
    return n;
}
}
