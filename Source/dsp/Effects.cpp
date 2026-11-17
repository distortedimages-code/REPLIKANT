#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    int pow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }
    inline float polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
}

//==============================================================================
// SPECTRE
void Spectral::prepare (double rate)
{
    sr = rate;
    ring.assign ((size_t) N, 0.0f);
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const double x = kTwoPi * i / N;   // Blackman-Harris: its side lobes under the quietest partials kept
        window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
    }
    mags.assign ((size_t) N / 2 + 1, 0.0f);
    smooth.assign ((size_t) N / 2 + 1, 0.0f);
    fft.resize ((size_t) N);
    fa.assign ((size_t) N, {});
    fb.assign ((size_t) N, {});
    found.reserve ((size_t) N / 2);
    reset();
}

void Spectral::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (smooth.begin(), smooth.end(), 0.0f);
    for (auto& o : osc)
        o = Osc();
    ringPos = sinceSlice = 0;
    lastOut = level = 0.0f;
}

int Spectral::peaks (float* hz, float* lv, int max) const
{
    int n = 0;
    for (auto& o : osc)
        if (o.amp > 1.0e-4f && n < max)
        {
            hz[n] = o.hz;
            lv[n] = o.amp;
            ++n;
        }
    return n;
}

void Spectral::analyse()
{
    for (int i = 0; i < N; ++i)
        fa[(size_t) i] = ring[(size_t) ((ringPos + i) % N)] * window[(size_t) i];
    fft.fft (fa.data(), fb.data());
    const int H = N / 2;
    float total = 0.0f;
    const float blur = std::clamp (target.blur, 0.0f, 0.98f);
    for (int k = 0; k <= H; ++k)
    {
        mags[(size_t) k] = std::abs (fb[(size_t) k]) * (2.0f / (N * 0.35875f));
        smooth[(size_t) k] = blur * smooth[(size_t) k] + (1.0f - blur) * mags[(size_t) k];
        total += smooth[(size_t) k] * smooth[(size_t) k];
    }
    // the band: centre (log, 40 Hz .. 10 kHz) and its width (an octave .. everything)
    const double centreHz = 40.0 * std::pow (250.0, (double) std::clamp (target.centre, 0.0f, 1.0f));
    const double halfOct = 0.5 + 5.0 * std::clamp (target.bandwidth, 0.0f, 1.0f);
    const double lo = centreHz / std::pow (2.0, halfOct), hi = centreHz * std::pow (2.0, halfOct);
    const double binHz = sr / N;
    float loudest = 0.0f;
    for (int k = 2; k < H - 1; ++k)
        loudest = std::max (loudest, smooth[(size_t) k]);
    const float floorLevel = std::max (1.0e-5f, loudest * 0.001f);   // 60 dB under the loudest
    found.clear();
    for (int k = 2; k < H - 1; ++k)
    {
        const float m = smooth[(size_t) k];
        if (m > smooth[(size_t) k - 1] && m >= smooth[(size_t) k + 1] && m > floorLevel)
        {
            const double f = k * binHz;
            if (f < lo || f > hi)
                continue;
            // the peak between bins (parabolic, in log)
            const double a = std::log (smooth[(size_t) k - 1] + 1e-9), b = std::log (m + 1e-9), c = std::log (smooth[(size_t) k + 1] + 1e-9);
            const double den = a - 2.0 * b + c;
            const double off = std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
            found.push_back ({ m, (float) ((k + off) * binHz) });
        }
    }
    const int V = std::clamp (target.voices, 1, maxVoices);
    std::partial_sort (found.begin(), found.begin() + std::min ((int) found.size(), V), found.end(),
                       [] (auto& x, auto& y) { return x.first > y.first; });
    found.resize ((size_t) std::min ((int) found.size(), V));
    std::sort (found.begin(), found.end(), [] (auto& x, auto& y) { return x.second < y.second; });
    // the oscillators, in frequency order, take the peaks in frequency order (each glides to the nearest change)
    const float trans = std::exp2 (std::round (std::clamp (target.octave, -2.0f, 2.0f)));
    float sumAmp = 0.0f;
    for (auto& f : found)
        sumAmp += f.first;
    const float norm = sumAmp > 0.0f ? std::sqrt (total) / std::max (1.0e-6f, sumAmp) * 1.5f : 0.0f;
    std::array<int, maxVoices> order {};
    for (int i = 0; i < maxVoices; ++i)
        order[(size_t) i] = i;
    std::sort (order.begin(), order.end(), [this] (int a, int b) { return osc[(size_t) a].hzTarget < osc[(size_t) b].hzTarget; });
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& o = osc[(size_t) order[(size_t) i]];
        if (i < (int) found.size())
        {
            o.hzTarget = found[(size_t) i].second * trans;
            if (o.amp < 1.0e-4f)
                o.hz = o.hzTarget;
            o.ampTarget = found[(size_t) i].first * norm;
        }
        else
            o.ampTarget = 0.0f;
    }
}

void Spectral::process (float* L, float* R, int n)
{
    const float k = (float) std::min (1.0, 100.0 / sr);
    const int hop = (int) (sr * (0.006 + 0.5 * std::pow ((double) std::clamp (target.slice, 0.0f, 1.0f), 2.0)));
    const float glideK = (float) (1.0 - std::exp (-1.0 / (sr * (0.001 + 0.6 * std::pow ((double) target.glide, 2.0)))));
    const float ampK = (float) (1.0 - std::exp (-1.0 / (sr * 0.01)));
    const float fb = std::clamp (target.feedback, 0.0f, 0.95f);
    for (int i = 0; i < n; ++i)
    {
        mix += (target.mix - mix) * k;
        const float dl = L[i], dr = R != nullptr ? R[i] : L[i];
        ring[(size_t) ringPos] = 0.5f * (dl + dr) + fb * lastOut;
        ringPos = (ringPos + 1) % N;
        if (++sinceSlice >= hop && ! target.freeze)
        {
            sinceSlice = 0;
            analyse();
        }
        float outL = 0.0f, outR = 0.0f;
        int idx = 0;
        for (auto& o : osc)
        {
            o.hz += (o.hzTarget - o.hz) * glideK;
            o.amp += (o.ampTarget - o.amp) * ampK;
            if (o.amp < 1.0e-5f || o.hz <= 0.0f || o.hz > sr * 0.45)
            {
                ++idx;
                continue;
            }
            const double dt = o.hz / sr;
            o.phase += dt;
            if (o.phase >= 1.0) o.phase -= 1.0;
            float s;
            switch (target.wave)
            {
                case 1:  s = (float) (1.0 - 4.0 * std::abs (o.phase - 0.5)); break;
                case 2:  s = (float) (2.0 * o.phase - 1.0) - polyBlep (o.phase, dt); break;
                case 3:  s = (o.phase < 0.5 ? 1.0f : -1.0f) + polyBlep (o.phase, dt) - polyBlep (std::fmod (o.phase + 0.5, 1.0), dt); break;
                default: s = (float) std::sin (kTwoPi * o.phase); break;
            }
            s *= o.amp;
            // spread: every other oscillator leans left or right
            const float pan = (idx % 2 == 0) ? 0.3f : 0.7f;
            outL += s * (1.0f - pan) * 1.4f;
            outR += s * pan * 1.4f;
            ++idx;
        }
        lastOut = 0.5f * (outL + outR);
        L[i] = dl + mix * (outL - dl);
        if (R != nullptr)
            R[i] = dr + mix * (outR - dr);
    }
}

//==============================================================================
// PITCH
void PitchShift::prepare (double rate)
{
    sr = rate;
    const int size = pow2 ((int) (sr * 1.3));
    mask = size - 1;
    for (auto& x : v)
    {
        x.buf.assign ((size_t) size, 0.0f);
        x.st.presetDefault (1, (float) sr);   // (presetCheaper drifts ~11 cents on a pure tone)
    }
    grainLen = (float) (sr * 0.08);
    in.assign (512, 0.0f);
    out.assign (512, 0.0f);
    fbIn.assign (512, 0.0f);
    reset();
}

void PitchShift::reset()
{
    for (auto& x : v)
    {
        std::fill (x.buf.begin(), x.buf.end(), 0.0f);
        x.w = 0;
        x.grain = 0.0f;
        x.st.reset();
    }
}

float PitchShift::read (const Voice& x, float back) const
{
    back = std::max (0.0f, back);
    const int i0 = (int) back;
    const float f = back - (float) i0;
    const float a = x.buf[(size_t) ((x.w - i0) & mask)], b = x.buf[(size_t) ((x.w - i0 - 1) & mask)];
    return a + (b - a) * f;
}

// CRISTAUX: two grains half a grain apart, faded in and out (triangles summing to 1), each read backwards through
// the line at the interval's speed
float PitchShift::reverseGrain (Voice& x, float ratio)
{
    const float G = grainLen;
    x.grain += 1.0f + ratio;
    while (x.grain >= G) x.grain -= G;
    float g2 = x.grain + 0.5f * G;
    if (g2 >= G) g2 -= G;
    const float w1 = 1.0f - std::abs (2.0f * x.grain / G - 1.0f);
    return read (x, x.delay + 1.0f + x.grain) * w1 + read (x, x.delay + 1.0f + g2) * (1.0f - w1);
}

void PitchShift::chunk (float* L, float* R, int n)
{
    const float fb = std::clamp (target.feedback, 0.0f, 0.95f);
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = 0.5f * (L[i] + (R != nullptr ? R[i] : L[i]));
    float outL[512], outR[512];
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        auto& x = v[c];
        const float semis = target.semis[c] + target.fine[c] / 100.0f;
        const float wantDelay = (float) (std::clamp (target.delay[c], 0.0f, 1.0f) * sr);
        if (target.level[c] <= 0.0f && fb <= 0.0f)
        {
            x.delay = wantDelay;
            continue;
        }
        if (! target.crystals)
        {
            // the feedback: what came out of its delay a block or more ago, back in
            const float back = std::max (x.delay, (float) n);
            for (int i = 0; i < n; ++i)
                fbIn[(size_t) i] = in[(size_t) i] + fb * read (x, back - (float) i);
            x.st.setTransposeSemitones (semis);
            const float* ins[1] = { fbIn.data() };
            float* outs[1] = { out.data() };
            x.st.process (ins, n, outs, n);
            for (int i = 0; i < n; ++i)
            {
                x.delay += (wantDelay - x.delay) * 0.0005f;
                x.w = (x.w + 1) & mask;
                x.buf[(size_t) x.w] = out[(size_t) i];
                const float y = read (x, x.delay) * target.level[c];
                outL[i] += y * (c == 0 ? 0.8f : 0.4f);
                outR[i] += y * (c == 0 ? 0.4f : 0.8f);
            }
        }
        else
        {
            x.ratio += (std::exp2 (semis / 12.0f) - x.ratio) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                x.delay += (wantDelay - x.delay) * 0.0005f;
                const float y = reverseGrain (x, x.ratio);
                x.w = (x.w + 1) & mask;
                x.buf[(size_t) x.w] = in[(size_t) i] + fb * y;
                const float o = y * target.level[c];
                outL[i] += o * (c == 0 ? 0.8f : 0.4f);
                outR[i] += o * (c == 0 ? 0.4f : 0.8f);
            }
        }
    }
    const float k = (float) std::min (1.0, 100.0 / sr);
    for (int i = 0; i < n; ++i)
    {
        mix += (target.mix - mix) * k;
        L[i] += mix * (outL[i] - L[i]);
        if (R != nullptr)
            R[i] += mix * (outR[i] - R[i]);
    }
}

void PitchShift::process (float* L, float* R, int n)
{
    for (int i = 0; i < n; i += 256)
        chunk (L + i, R != nullptr ? R + i : nullptr, std::min (256, n - i));
}

//==============================================================================
// CHORUS
void Chorus::prepare (double rate)
{
    sr = rate;
    const int size = pow2 ((int) (sr * 0.06));
    mask = size - 1;
    for (auto& b : buf)
        b.assign ((size_t) size, 0.0f);
    reset();
}

void Chorus::reset()
{
    for (auto& b : buf)
        std::fill (b.begin(), b.end(), 0.0f);
    w = 0;
    ph = 0.0;
    lp[0] = lp[1] = fbState[0] = fbState[1] = 0.0f;
}

float Chorus::tap (int ch, float d) const
{
    d = std::max (1.0f, d);
    const int i0 = (int) d;
    const float f = d - (float) i0;
    const auto& b = buf[ch];
    const float a = b[(size_t) ((w - i0) & mask)], c = b[(size_t) ((w - i0 - 1) & mask)];
    return a + (c - a) * f;
}

void Chorus::process (float* L, float* R, int n)
{
    const float k = (float) std::min (1.0, 100.0 / sr);
    const double inc = std::clamp (target.rate, 0.01f, 20.0f) / sr;
    const float toneK = (float) (1.0 - std::exp (-kTwoPi * (2500.0 + 15000.0 * target.tone * target.tone) / sr));
    for (int i = 0; i < n; ++i)
    {
        mix += (target.mix - mix) * k;
        depth += (target.depth - depth) * k;
        ph += inc;
        if (ph >= 1.0) ph -= 1.0;
        const float dl = L[i], dr = R != nullptr ? R[i] : L[i];
        float wl = 0.0f, wr = 0.0f;
        w = (w + 1) & mask;
        if (target.mode == 1)
        {
            // flanger: 0.3 .. 6 ms swept, its feedback
            const float fb = std::clamp (target.feedback, -0.95f, 0.95f);
            buf[0][(size_t) w] = dl + fb * fbState[0];
            buf[1][(size_t) w] = dr + fb * fbState[1];
            const float m0 = 0.5f - 0.5f * (float) std::cos (kTwoPi * ph), m1 = 0.5f - 0.5f * (float) std::cos (kTwoPi * (ph + 0.25));
            const float d0 = (float) (sr * (0.0003 + 0.0057 * depth * m0)), d1 = (float) (sr * (0.0003 + 0.0057 * depth * m1));
            wl = tap (0, d0);
            wr = tap (1, d1);
            // the loop held (a soft limit): no runaway at full feedback
            fbState[0] = wl / (1.0f + 0.5f * std::abs (wl));
            fbState[1] = wr / (1.0f + 0.5f * std::abs (wr));
            wl *= 1.0f - 0.5f * std::abs (fb);
            wr *= 1.0f - 0.5f * std::abs (fb);
        }
        else
        {
            buf[0][(size_t) w] = dl;
            buf[1][(size_t) w] = dr;
            if (target.mode == 2)
            {
                // vibrato: one swaying delay, all wet
                const float m = (float) std::sin (kTwoPi * ph);
                wl = tap (0, (float) (sr * (0.005 + 0.004 * depth * m)));
                wr = tap (1, (float) (sr * (0.005 + 0.004 * depth * m)));
            }
            else
            {
                // chorus: three voices a third of a cycle apart, spread
                for (int v = 0; v < 3; ++v)
                {
                    const float m = (float) std::sin (kTwoPi * (ph + v / 3.0));
                    const float d = (float) (sr * (0.012 + 0.003 * v + 0.006 * depth * m));
                    const float s0 = tap (0, d), s1 = tap (1, d * 1.07f);
                    const float pan = v == 0 ? 0.5f : v == 1 ? 0.15f : 0.85f;
                    wl += (s0 * (1.0f - pan) + s1 * (1.0f - pan)) * 0.6f;
                    wr += (s0 * pan + s1 * pan) * 0.6f;
                }
            }
        }
        // dark as a bucket brigade
        lp[0] += toneK * (wl - lp[0]);
        lp[1] += toneK * (wr - lp[1]);
        wl = lp[0];
        wr = lp[1];
        const float m = target.mode == 2 ? std::max (mix, 0.0f) : mix;
        L[i] = dl + m * (wl - dl);
        if (R != nullptr)
            R[i] = dr + m * (wr - dr);
    }
}

//==============================================================================
// CRUSH
void Crush::process (float* L, float* R, int n)
{
    const float bits = std::clamp (target.bits, 1.0f, 16.0f);
    const float steps = std::exp2 (bits - 1.0f);
    const double period = sr / std::clamp ((double) target.rateHz, 200.0, sr);
    const float toneK = (float) (1.0 - std::exp (-kTwoPi * (800.0 + 20000.0 * target.tone * target.tone) / sr));
    auto rnd = [this]
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (double) rng / 4294967295.0;
    };
    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i], dr = R != nullptr ? R[i] : L[i];
        acc += 1.0;
        if (acc >= nextAt)
        {
            acc -= nextAt;
            nextAt = period * (1.0 + target.jitter * (rnd() - 0.5));
            // the depth: rounded to the bits (fractional bits blend between two depths)
            held[0] = std::round (dl * steps) / steps;
            held[1] = std::round (dr * steps) / steps;
        }
        lp[0] += toneK * (held[0] - lp[0]);
        lp[1] += toneK * (held[1] - lp[1]);
        L[i] = dl + target.mix * (lp[0] - dl);
        if (R != nullptr)
            R[i] = dr + target.mix * (lp[1] - dr);
    }
}
}
