#include "Smear.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wfloat-equal"
 #pragma clang diagnostic ignored "-Wsign-conversion"
 #pragma clang diagnostic ignored "-Wimplicit-float-conversion"
 #pragma clang diagnostic ignored "-Wimplicit-int-conversion"
 #pragma clang diagnostic ignored "-Wshorten-64-to-32"
 #pragma clang diagnostic ignored "-Wconversion"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
 #pragma clang diagnostic ignored "-Wcast-align"
 #pragma clang diagnostic ignored "-Wdeprecated"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif
#include "signalsmith-linear/fft.h"
#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace dsp
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr int tableSize = 4096, gaussSize = 1024;
    constexpr float gaussSpan = 4.0f;            // the harmonics' bands: exp (-x²) tabled over 0 .. 4

    inline uint32_t xorshift (uint32_t& s)
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // log2 and 2^x, near enough for shaping magnitudes (TONAL), much faster than pow
    inline float fastLog2 (float x)
    {
        uint32_t i;
        std::memcpy (&i, &x, 4);
        const float e = (float) ((int) ((i >> 23) & 255) - 128);
        i = (i & 0x007FFFFFu) | 0x3F800000u;
        float m;
        std::memcpy (&m, &i, 4);
        return e + ((-1.0f / 3.0f) * m + 2.0f) * m - 2.0f / 3.0f;
    }
    inline float fastExp2 (float p)
    {
        if (p < -126.0f)
            return 0.0f;
        const float w = std::floor (p), z = p - w;
        const float f = 1.0f + z * (0.6960656f + z * (0.2244943f + z * 0.0794402f));
        const uint32_t bits = (uint32_t) ((int) w + 127) << 23;
        float s;
        std::memcpy (&s, &bits, 4);
        return s * f;
    }
    // a table read between its points (t in 0..1)
    inline float lerpTable (const std::vector<float>& tb, float t)
    {
        const float x = t * (float) tableSize;
        const int i = std::min (tableSize - 1, (int) x);
        const float f = x - (float) i;
        return tb[(size_t) i] + f * (tb[(size_t) i + 1] - tb[(size_t) i]);
    }
}

struct Smear::Size
{
    int N = 0;
    signalsmith::linear::Pow2FFT<float> fft;
};

Smear::Smear() = default;
Smear::~Smear() = default;

void Smear::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxW = std::max (2048, (int) std::lround (maxSeconds * sr)) & ~1;
    int n = 1024;
    while (n < maxW)
        n <<= 1;
    if (n != maxN)
    {
        maxN = n;
        sizes.clear();
        for (int s = 1024; s <= maxN; s <<= 1)
        {
            auto z = std::make_unique<Size>();
            z->N = s;
            z->fft.resize ((size_t) s);
            sizes.push_back (std::move (z));
        }
        for (auto* v : { &ar, &ai, &br, &bi })
            v->assign ((size_t) maxN, 0.0f);
        mL.assign ((size_t) maxN / 2 + 1, 0.0f);
        mR.assign ((size_t) maxN / 2 + 1, 0.0f);
    }
    hann.resize (tableSize + 1);
    sine.resize (tableSize + 1);
    cosT.resize (tableSize);
    sinT.resize (tableSize);
    for (int i = 0; i <= tableSize; ++i)
    {
        const double t = (double) i / tableSize;
        hann[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * t));
        sine[(size_t) i] = (float) std::sin (0.5 * kTwoPi * t);
        if (i < tableSize)
        {
            cosT[(size_t) i] = (float) std::cos (kTwoPi * t);
            sinT[(size_t) i] = (float) std::sin (kTwoPi * t);
        }
    }
    gauss.resize (gaussSize + 1);
    for (int i = 0; i <= gaussSize; ++i)
    {
        const double x = gaussSpan * (double) i / gaussSize;
        gauss[(size_t) i] = (float) std::exp (-x * x);
    }
}

void Smear::allocate (Voice& v) const
{
    for (auto& r : v.ring)
        if ((int) r.size() != maxN)
            r.assign ((size_t) maxN, 0.0f);
    v.rp = 0;
    v.hopLeft = 0;
}

void Smear::Voice::start (uint32_t seed)
{
    for (auto& r : ring)
        std::fill (r.begin(), r.end(), 0.0f);
    rp = 0;
    hopLeft = 0;
    rng = seed | 1u;
    rngR = (seed * 2654435761u) | 1u;
    started = false;
}

void Smear::frame (Voice& v, const Source& s, const Shape& sh)
{
    if (sizes.empty() || v.ring[0].empty() || s.l == nullptr || s.len <= 0)
    {
        v.hopLeft = 512;
        return;
    }
    const int W = std::max (1024, std::min (maxW, (int) std::lround (std::max (minSeconds, sh.seconds) * sr))) & ~1;
    int N = 1024, zi = 0;
    while (N < W)
    {
        N <<= 1;
        ++zi;
    }
    auto& z = *sizes[(size_t) std::min ((int) sizes.size() - 1, zi)];
    const int H = N / 2;
    const bool mono = s.r == nullptr;

    // the frame, read: the left side real, the right imaginary (one transform for both); outside the sound, nothing
    const auto first = (int64_t) std::floor (s.centre) - W / 2;
    const bool wraps = s.loop && s.le > s.ls + 1 && s.le <= s.len;
    const float invW = 1.0f / (float) W;
    double valid = 0.0, full = 0.0;
    for (int j = 0; j < W; ++j)
    {
        int64_t k = first + j;
        const float w = lerpTable (hann, (float) j * invW);
        full += (double) (w * w);
        if (wraps && k >= s.le)
            k = s.ls + (k - s.ls) % (s.le - s.ls);
        if (k < 0 || k >= s.len)
        {
            ar[(size_t) j] = ai[(size_t) j] = 0.0f;
            continue;
        }
        valid += (double) (w * w);
        ar[(size_t) j] = s.l[k] * w;
        ai[(size_t) j] = mono ? 0.0f : s.r[k] * w;
    }
    std::fill (ar.begin() + W, ar.begin() + N, 0.0f);
    std::fill (ai.begin() + W, ai.begin() + N, 0.0f);
    // (a sound shorter than the window: what there is, made up to the window's level)
    const float gValid = valid > 0.02 * full ? (float) std::min (4.0, std::sqrt (full / valid)) : 0.0f;
    if (gValid <= 0.0f)
    {
        v.hopLeft = W / 2;
        return;
    }
    z.fft.fft (ar.data(), ai.data(), br.data(), bi.data());

    // the two sides' magnitudes
    double eIn = 0.0;
    for (int k = 0; k <= H; ++k)
    {
        const int q = (N - k) & (N - 1);
        const float zr = br[(size_t) k], zim = bi[(size_t) k], cr = br[(size_t) q], ci = -bi[(size_t) q];
        const float lr = 0.5f * (zr + cr), li = 0.5f * (zim + ci);
        mL[(size_t) k] = std::sqrt (lr * lr + li * li);
        if (! mono)
        {
            const float a = zr - cr, b = zim - ci;
            mR[(size_t) k] = 0.5f * std::sqrt (a * a + b * b);
        }
    }
    if (mono)
        std::copy (mL.begin(), mL.begin() + H + 1, mR.begin());
    mL[0] = mR[0] = mL[(size_t) H] = mR[(size_t) H] = 0.0f;
    for (int k = 1; k < H; ++k)
        eIn += (double) (mL[(size_t) k] * mL[(size_t) k] + mR[(size_t) k] * mR[(size_t) k]);
    if (eIn < 1.0e-18)
    {
        v.hopLeft = W / 2;
        return;
    }

    const double binHz = sr / N;
    const float f0 = std::max (8.0f, sh.noteHz);
    const float tonal = std::clamp (sh.tonal, 0.0f, 1.0f), harm = std::clamp (sh.harmonics, 0.0f, 1.0f);
    const float shim = std::clamp (sh.shimmer, 0.0f, 1.0f), width = std::clamp (sh.width, 0.0f, 1.0f);
    const float depth = std::min (1.0f, 1.5f * harm);
    const float bw = std::max (0.35f - 0.32f * harm, (float) (0.6 * binHz / f0));       // the bands, in harmonics
    const float gaussScale = (float) gaussSize / gaussSpan / bw;
    const double inv = 1.0 / std::max (1.0e-3, std::abs (sh.step));
    auto at = [H] (const float* m, double p)
    {
        if (p >= (double) H)
            return 0.0f;
        const int i = (int) p;
        const float f = (float) (p - (double) i);
        return m[i] + f * (m[i + 1] - m[i]);
    };

    auto shape = [&] (float* m)
    {
        // the note: each frequency scaled (up: read lower, from the top down; down: the loudest of the span read,
        // from the bottom up, so a partial isn't stepped over)
        if (inv <= 1.0)
            for (int k = H - 1; k >= 1; --k)
                m[k] = at (m, (double) k * inv);
        else
            for (int k = 1; k < H; ++k)
            {
                const double a = (double) k * inv, b = (double) (k + 1) * inv;
                float mx = at (m, a);
                for (int i = (int) a + 1; i < (int) b && i < H; ++i)
                    mx = std::max (mx, m[i]);
                m[k] = mx;
            }
        // TONAL: the partials kept, what is under them pushed down
        if (tonal > 0.001f)
        {
            float mx = 0.0f;
            for (int k = 1; k < H; ++k)
                mx = std::max (mx, m[k]);
            if (mx > 0.0f)
            {
                const float e = 3.0f * tonal, im = 1.0f / mx;
                for (int k = 1; k < H; ++k)
                    if (m[k] > 0.0f)
                        m[k] *= fastExp2 (e * fastLog2 (m[k] * im));
            }
        }
        // HARMONICS: through the note's series (a band round each harmonic, nothing under the first)
        if (depth > 0.001f)
            for (int k = 1; k < H; ++k)
            {
                const float q = (float) ((double) k * binHz) / f0;
                float g = 0.0f;
                if (q > 0.5f)
                {
                    const float x = std::abs (q - std::nearbyint (q)) * gaussScale;
                    g = x < (float) gaussSize ? gauss[(size_t) x] : 0.0f;
                }
                m[k] *= 1.0f - depth + depth * g;
            }
        // SHIMMER: the octave above, and the twelfth (from the top down: what is read isn't changed yet)
        if (shim > 0.001f)
        {
            const float a = shim, b = 0.5f * shim * shim;
            for (int k = H - 1; k >= 2; --k)
                m[k] += a * at (m, (double) k * 0.5) + b * at (m, (double) k / 3.0);
        }
        m[0] = m[H] = 0.0f;
    };
    shape (mL.data());
    shape (mR.data());

    double eOut = 0.0;
    for (int k = 1; k < H; ++k)
        eOut += (double) (mL[(size_t) k] * mL[(size_t) k] + mR[(size_t) k] * mR[(size_t) k]);
    if (eOut < 1.0e-24)
    {
        v.hopLeft = W / 2;
        return;
    }
    // its loudness kept (what was taken away made up, 18 dB at most); the transform's scale; the frames adding in power
    const float g = (float) std::min (8.0, std::sqrt (eIn / eOut)) * gValid;

    // the phases, drawn (WIDTH: the right side's drawn apart from the left's)
    br[0] = bi[0] = br[(size_t) H] = bi[(size_t) H] = 0.0f;
    const float spread = width * (float) tableSize;
    for (int k = 1; k < H; ++k)
    {
        const int pl = (int) (xorshift (v.rng) >> 20);
        const int pr = (pl + (int) (((float) (xorshift (v.rngR) >> 8) * (1.0f / 16777216.0f) - 0.5f) * spread) + tableSize * 2) & (tableSize - 1);
        const float yl = mL[(size_t) k] * g, yr = mR[(size_t) k] * g;
        const float cl = cosT[(size_t) pl], sl = sinT[(size_t) pl], cr = cosT[(size_t) pr], sr2 = sinT[(size_t) pr];
        // W[k] = YL + i YR; W[N - k] = conj (YL) + i conj (YR)
        br[(size_t) k] = yl * cl - yr * sr2;
        bi[(size_t) k] = yl * sl + yr * cr;
        br[(size_t) (N - k)] = yl * cl + yr * sr2;
        bi[(size_t) (N - k)] = -yl * sl + yr * cr;
    }
    z.fft.ifft (br.data(), bi.data(), ar.data(), ai.data());

    // back through the sine window, overlap-added where the voice reads
    const float out = std::sqrt ((float) N / (0.375f * (float) W)) / (float) N;
    const int mask = (int) v.ring[0].size() - 1;
    float* r0 = v.ring[0].data();
    float* r1 = v.ring[1].data();
    for (int j = 0; j < W; ++j)
    {
        const float w = lerpTable (sine, (float) j * invW) * out;
        const int at2 = (v.rp + j) & mask;
        r0[at2] += ar[(size_t) j] * w;
        r1[at2] += ai[(size_t) j] * w;
    }
    // the next frame half a window on (the first one sooner, by chance: voices started together don't all
    // compute their frames in the same block)
    int hop = W / 2;
    if (! v.started)
    {
        v.started = true;
        hop = hop / 2 + (int) ((float) (xorshift (v.rng) >> 8) * (1.0f / 16777216.0f) * (float) (hop / 2));
    }
    v.hopLeft = std::max (64, hop);
}
}
