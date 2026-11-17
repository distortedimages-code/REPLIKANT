#include "Wav.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rk
{
namespace
{
    void put16 (std::vector<uint8_t>& b, uint32_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); }
    void put32 (std::vector<uint8_t>& b, uint32_t v) { put16 (b, v & 0xffff); put16 (b, v >> 16); }
    void putTag (std::vector<uint8_t>& b, const char* t) { b.insert (b.end(), t, t + 4); }
    uint32_t get16 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); }
    uint32_t get32 (const uint8_t* p) { return get16 (p) | (get16 (p + 2) << 16); }
}

std::vector<uint8_t> encodeWav (const Channels& ch, double sr, int bits, const WavInfo& info)
{
    std::vector<uint8_t> b;
    const int nch = (int) ch.size();
    const size_t frames = nch > 0 ? ch[0].size() : 0;
    const bool isFloat = bits == 32;
    if (bits != 16 && bits != 24 && bits != 32)
        bits = 24;
    const int bytes = bits / 8;

    putTag (b, "RIFF");
    put32 (b, 0);                               // set at the end
    putTag (b, "WAVE");

    putTag (b, "fmt ");
    put32 (b, 16);
    put16 (b, isFloat ? 3 : 1);
    put16 (b, (uint32_t) nch);
    put32 (b, (uint32_t) std::lround (sr));
    put32 (b, (uint32_t) (std::lround (sr) * nch * bytes));
    put16 (b, (uint32_t) (nch * bytes));
    put16 (b, (uint32_t) bits);

    if (info.root >= 0)
    {
        // the sample is detuneCents above its root: root + fraction of a semitone (a negative detune = the note below)
        int root = info.root;
        double frac = info.detuneCents / 100.0;
        while (frac < 0.0) { frac += 1.0; --root; }
        while (frac >= 1.0) { frac -= 1.0; ++root; }
        root = std::clamp (root, 0, 127);
        const bool loop = info.loop.valid();
        putTag (b, "smpl");
        put32 (b, 36 + (loop ? 24 : 0));
        put32 (b, 0);                                                 // manufacturer
        put32 (b, 0);                                                 // product
        put32 (b, (uint32_t) std::lround (1.0e9 / sr));               // sample period, ns
        put32 (b, (uint32_t) root);
        put32 (b, (uint32_t) std::min (4294967295.0, frac * 4294967296.0));
        put32 (b, 0);                                                 // SMPTE format
        put32 (b, 0);                                                 // SMPTE offset
        put32 (b, loop ? 1 : 0);
        put32 (b, 0);                                                 // sampler data
        if (loop)
        {
            put32 (b, 0);                                             // cue id
            put32 (b, 0);                                             // forward
            put32 (b, (uint32_t) info.loop.start);
            put32 (b, (uint32_t) (info.loop.end - 1));                // smpl's end is the last sample played
            put32 (b, 0);
            put32 (b, 0);                                             // endless
        }
    }
    if (info.hasRange)
    {
        putTag (b, "inst");
        put32 (b, 7);
        const int cents = (int) std::lround (info.detuneCents);
        b.push_back ((uint8_t) std::clamp (info.root, 0, 127));
        b.push_back ((uint8_t) (int8_t) std::clamp (-cents, -50, 50));
        b.push_back (0);                                              // gain, dB
        b.push_back ((uint8_t) std::clamp (info.keyLo, 0, 127));
        b.push_back ((uint8_t) std::clamp (info.keyHi, 0, 127));
        b.push_back ((uint8_t) std::clamp (info.velLo, 1, 127));
        b.push_back ((uint8_t) std::clamp (info.velHi, 1, 127));
        b.push_back (0);                                              // pad to an even size
    }

    putTag (b, "data");
    const size_t dataBytes = frames * (size_t) nch * (size_t) bytes;
    put32 (b, (uint32_t) dataBytes);
    const size_t at = b.size();
    b.resize (at + dataBytes);
    uint8_t* p = b.data() + at;
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < nch; ++c)
        {
            const float v = ch[(size_t) c][i];
            if (isFloat)
            {
                std::memcpy (p, &v, 4);
                p += 4;
                continue;
            }
            const double x = std::clamp ((double) v, -1.0, 1.0);
            if (bits == 16)
            {
                const auto s = (int32_t) std::lround (x * 32767.0);
                p[0] = (uint8_t) s; p[1] = (uint8_t) (s >> 8);
                p += 2;
            }
            else
            {
                const auto s = (int32_t) std::lround (x * 8388607.0);
                p[0] = (uint8_t) s; p[1] = (uint8_t) (s >> 8); p[2] = (uint8_t) (s >> 16);
                p += 3;
            }
        }
    if (dataBytes & 1)
        b.push_back (0);
    const uint32_t riff = (uint32_t) (b.size() - 8);
    b[4] = (uint8_t) riff; b[5] = (uint8_t) (riff >> 8); b[6] = (uint8_t) (riff >> 16); b[7] = (uint8_t) (riff >> 24);
    return b;
}

bool decodeWav (const uint8_t* d, size_t size, Channels& ch, double& sr, WavInfo& info)
{
    info = {};
    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        return false;
    int format = 0, nch = 0, bits = 0;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    size_t pos = 12;
    while (pos + 8 <= size)
    {
        const uint8_t* c = d + pos;
        const size_t len = get32 (c + 4);
        const uint8_t* body = c + 8;
        const size_t avail = std::min (len, size - pos - 8);
        if (std::memcmp (c, "fmt ", 4) == 0 && avail >= 16)
        {
            format = (int) get16 (body);
            nch = (int) get16 (body + 2);
            sr = (double) get32 (body + 4);
            bits = (int) get16 (body + 14);
            if (format == 0xFFFE && avail >= 26)          // WAVE_FORMAT_EXTENSIBLE: the real format is in the GUID
                format = (int) get16 (body + 24);
        }
        else if (std::memcmp (c, "data", 4) == 0)
        {
            data = body;
            dataSize = avail;
        }
        else if (std::memcmp (c, "smpl", 4) == 0 && avail >= 36)
        {
            info.root = (int) get32 (body + 12);
            info.detuneCents = (float) ((double) get32 (body + 16) / 4294967296.0 * 100.0);
            const uint32_t loops = get32 (body + 28);
            if (loops > 0 && avail >= 36 + 24)
            {
                const uint8_t* l = body + 36;
                info.loop.start = (int) get32 (l + 8);
                info.loop.end = (int) get32 (l + 12) + 1;
            }
        }
        else if (std::memcmp (c, "inst", 4) == 0 && avail >= 7)
        {
            info.hasRange = true;
            info.keyLo = body[3];
            info.keyHi = body[4];
            info.velLo = body[5];
            info.velHi = body[6];
            if (info.root < 0)
                info.root = body[0];
        }
        pos += 8 + len + (len & 1);
    }
    if (data == nullptr || nch <= 0 || bits <= 0)
        return false;
    const int bytes = bits / 8;
    const size_t frames = dataSize / (size_t) (bytes * nch);
    ch.assign ((size_t) nch, std::vector<float> (frames, 0.0f));
    const uint8_t* p = data;
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < nch; ++c)
        {
            float v = 0.0f;
            if (format == 3 && bits == 32)
                std::memcpy (&v, p, 4);
            else if (format == 3 && bits == 64)
            {
                double x;
                std::memcpy (&x, p, 8);
                v = (float) x;
            }
            else if (bits == 8)
                v = ((float) p[0] - 128.0f) / 128.0f;
            else if (bits == 16)
                v = (float) (int16_t) get16 (p) / 32768.0f;
            else if (bits == 24)
            {
                int32_t s = (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16));
                if (s & 0x800000) s |= ~0xffffff;
                v = (float) s / 8388608.0f;
            }
            else if (bits == 32)
                v = (float) ((double) (int32_t) get32 (p) / 2147483648.0);
            ch[(size_t) c][i] = v;
            p += bytes;
        }
    return true;
}

bool writeWav (const std::string& path, const Channels& ch, double sr, int bits, const WavInfo& info)
{
    const auto b = encodeWav (ch, sr, bits, info);
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const bool ok = std::fwrite (b.data(), 1, b.size(), f) == b.size();
    return std::fclose (f) == 0 && ok;
}

bool readWav (const std::string& path, Channels& ch, double& sr, WavInfo& info)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread (buf, 1, sizeof (buf), f)) > 0)
        b.insert (b.end(), buf, buf + n);
    std::fclose (f);
    return decodeWav (b.data(), b.size(), ch, sr, info);
}
}
