#include "Effects.h"
#include "Modulators.h"

#include <algorithm>
#include <cmath>

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wshadow-field"
 #pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
 #pragma clang diagnostic ignored "-Wshadow-uncaptured-local"
 #pragma clang diagnostic ignored "-Wfloat-equal"
 #pragma clang diagnostic ignored "-Wsign-conversion"
 #pragma clang diagnostic ignored "-Wimplicit-int-conversion"
 #pragma clang diagnostic ignored "-Wshorten-64-to-32"
 #pragma clang diagnostic ignored "-Wconversion"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wextra-semi"
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
#include "signalsmith-stretch.h"
#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace dsp
{
struct Spectral::FFT { signalsmith::linear::Pow2FFT<float> f; };
struct PitchShift::Shifter { signalsmith::stretch::SignalsmithStretch<float> s; };

Spectral::Spectral() : fft (std::make_unique<FFT>()) {}
Spectral::~Spectral() = default;
PitchShift::PitchShift()
{
    for (auto& x : v)
        x.st = std::make_unique<Shifter>();
}
PitchShift::~PitchShift() = default;

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
namespace
{
    // a waveform at its phase (0..1), its step `dt` for the band limiting (polyBLEP); the XF waves are a sine and a saw
    inline float spectreWave (int w, double ph, double dt)
    {
        switch (w)
        {
            case 1:  return (float) (1.0 - 4.0 * std::abs (ph - 0.5));
            case 2:
            case 6:  return (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
            case 3:  return (ph < 0.5 ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (std::fmod (ph + 0.5, 1.0), dt);
            case 4:  return (ph < 0.25 ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (std::fmod (ph + 0.75, 1.0), dt) + 0.5f;
            default: return (float) std::sin (kTwoPi * ph);
        }
    }
}

void Spectral::prepare (double rate)
{
    sr = rate;
    // about 85 ms of sound whatever the rate (4096 at 44.1 and 48 kHz, 8192 at 88.2 and 96)
    N = std::clamp (pow2 ((int) (sr * 0.08)), 1024, 32768);
    ring.assign ((size_t) N, 0.0f);
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const double x = kTwoPi * i / N;   // Blackman-Harris: its side lobes under the quietest partials kept
        window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
    }
    // TRANSIENTS: the last quarter, in a Hann window, the rest of the transform left at zero (the same bins)
    const int Q = N / 4;
    shortWindow.resize ((size_t) Q);
    for (int i = 0; i < Q; ++i)
        shortWindow[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / Q));
    mags.assign ((size_t) N / 2 + 1, 0.0f);
    smooth.assign ((size_t) N / 2 + 1, 0.0f);
    fft->f.resize ((size_t) N);
    fa.assign ((size_t) N, {});
    fb.assign ((size_t) N, {});
    found.reserve ((size_t) N / 2);
    matches.reserve ((size_t) maxVoices * maxVoices);
    reset();
}

void Spectral::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (smooth.begin(), smooth.end(), 0.0f);
    for (auto& o : osc)
        o = Osc();
    ringPos = sinceSlice = sinceAnalysis = analysisCount = 0;
    pendingIn = -1;
    triggered = analysedOnce = false;
    lastGrid = -1;
    lastOut = fbEnv = onsetFast = onsetSlow = 0.0f;
    onsetHold = 0;
    centreHz = 1000.0f;
    found.clear();
}

int Spectral::peaks (float* hz, float* lv, int max) const
{
    int n = 0;
    for (auto& o : osc)
        if (o.amp > 1.0e-4f && o.hz > 0.0f && n < max)
        {
            hz[n] = o.hz;
            lv[n] = o.amp;
            ++n;
        }
    return n;
}

void Spectral::analyse()
{
    const int H = N / 2;
    for (int i = 0; i < N; ++i)
        fa[(size_t) i] = ring[(size_t) ((ringPos + i) % N)] * window[(size_t) i];
    fft->f.fft (fa.data(), fb.data());
    const float gLong = 2.0f / ((float) N * 0.35875f);
    for (int k = 0; k <= H; ++k)
        mags[(size_t) k] = std::abs (fb[(size_t) k]) * gLong;
    if (target.transients)
    {
        // the last 21 ms (at 48 kHz) above 1.3 kHz, the long one under 700 Hz, mixed in between
        const int Q = N / 4;
        for (int i = 0; i < N; ++i)
            fa[(size_t) i] = i < Q ? ring[(size_t) ((ringPos + N - Q + i) % N)] * shortWindow[(size_t) i] : 0.0f;
        fft->f.fft (fa.data(), fb.data());
        const float gShort = 2.0f / ((float) Q * 0.5f);
        const double binHz = sr / N;
        for (int k = 0; k <= H; ++k)
        {
            const float u = (float) std::clamp ((k * binHz - 700.0) / 600.0, 0.0, 1.0);
            if (u > 0.0f)
                mags[(size_t) k] += u * (std::abs (fb[(size_t) k]) * gShort - mags[(size_t) k]);
        }
    }
    // BLUR: each bin follows the analyses with a time constant (20 ms .. 10 s), whatever the time between them; at
    // its end the spectrum stays as it is
    const float b = std::clamp (target.blur, 0.0f, 1.0f);
    float alpha = 1.0f;
    if (analysedOnce && b >= 0.995f)
        alpha = 0.0f;
    else if (analysedOnce && b > 0.005f)
        alpha = (float) (1.0 - std::exp (-(double) sinceAnalysis / sr / (0.02 * std::pow (500.0, (double) b))));
    for (int k = 0; k <= H; ++k)
    {
        smooth[(size_t) k] += alpha * (mags[(size_t) k] - smooth[(size_t) k]);
        if (! std::isfinite (smooth[(size_t) k]))
            smooth[(size_t) k] = 0.0f;
    }
    sinceAnalysis = 0;
    analysedOnce = true;
    ++analysisCount;
}

void Spectral::pick()
{
    const int H = N / 2;
    const double binHz = sr / N;
    found.clear();
    if (! analysedOnce)
        return;
    // the band: centre (log, 40 Hz .. 10 kHz) and its width (an octave .. eleven; at its end, everything), kept or
    // rejected (NOTCH)
    const double centre = 40.0 * std::pow (250.0, (double) std::clamp (target.centre, 0.0f, 1.0f));
    const bool all = target.bandwidth >= 0.999f;
    const double halfOct = 0.5 + 5.0 * std::clamp (target.bandwidth, 0.0f, 1.0f);
    const double lo = centre / std::pow (2.0, halfOct), hi = centre * std::pow (2.0, halfOct);
    float loudest = 0.0f;
    for (int k = 2; k < H - 1; ++k)
        loudest = std::max (loudest, smooth[(size_t) k]);
    const float floorLevel = std::max (1.0e-5f, loudest * 0.001f);   // 60 dB under the loudest
    for (int k = 2; k < H - 1; ++k)
    {
        const float m = smooth[(size_t) k];
        if (m > smooth[(size_t) k - 1] && m >= smooth[(size_t) k + 1] && m > floorLevel)
        {
            const double f = k * binHz;
            const bool inside = all || (f >= lo && f <= hi);
            if (inside == target.notch)
                continue;
            // the peak between bins (parabolic, in log)
            const double a = std::log (smooth[(size_t) k - 1] + 1e-9), bb = std::log (m + 1e-9), c = std::log (smooth[(size_t) k + 1] + 1e-9);
            const double den = a - 2.0 * bb + c;
            const double off = std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
            found.push_back ({ m, (float) ((k + off) * binHz) });
        }
    }
    const int V = std::clamp (target.voices, 1, maxVoices);
    std::partial_sort (found.begin(), found.begin() + std::min ((int) found.size(), V), found.end(),
                       [] (auto& x, auto& y) { return x.first > y.first; });
    found.resize ((size_t) std::min ((int) found.size(), V));
    std::sort (found.begin(), found.end(), [] (auto& x, auto& y) { return x.second < y.second; });
    // WARP's centre: the peaks' geometric mean, weighted by their levels
    double sw = 0.0, sl = 0.0;
    for (auto& f : found)
    {
        sw += f.first;
        sl += f.first * std::log ((double) f.second);
    }
    if (sw > 0.0)
        centreHz = (float) std::exp (sl / sw);
}

void Spectral::assign()
{
    const int H = N / 2;
    float total = 0.0f, sumAmp = 0.0f;
    for (int k = 0; k <= H; ++k)
        total += smooth[(size_t) k] * smooth[(size_t) k];
    for (auto& f : found)
        sumAmp += f.first;
    const float norm = sumAmp > 0.0f ? std::sqrt (total) / std::max (1.0e-6f, sumAmp) * 1.5f : 0.0f;
    const int F = (int) found.size();
    std::array<bool, maxVoices> oscUsed {}, peakUsed {};
    std::array<int, maxVoices> peakOf;
    peakOf.fill (-1);
    auto silent = [] (const Osc& o) { return o.amp < 1.0e-4f && o.ampTarget <= 0.0f; };
    if (target.assign == 1)
    {
        // TRACK: each sounding oscillator to the nearest new peak (in octaves, under half an octave), the nearest
        // pairs first; the peaks left over to silent oscillators
        matches.clear();
        for (int i = 0; i < maxVoices; ++i)
            if (! silent (osc[(size_t) i]) && osc[(size_t) i].peak > 0.0f)
                for (int j = 0; j < F; ++j)
                    matches.push_back ({ std::abs (std::log2 (found[(size_t) j].second / osc[(size_t) i].peak)), i, j });
        std::sort (matches.begin(), matches.end(), [] (const Match& x, const Match& y) { return x.cost < y.cost; });
        for (auto& m : matches)
        {
            if (m.cost > 0.5f)
                break;
            if (oscUsed[(size_t) m.osc] || peakUsed[(size_t) m.peak])
                continue;
            oscUsed[(size_t) m.osc] = peakUsed[(size_t) m.peak] = true;
            peakOf[(size_t) m.osc] = m.peak;
        }
        for (int j = 0; j < F; ++j)
        {
            if (peakUsed[(size_t) j])
                continue;
            int best = -1;
            for (int pass = 0; pass < 2 && best < 0; ++pass)
                for (int i = 0; i < maxVoices && best < 0; ++i)
                    if (! oscUsed[(size_t) i] && (pass == 1 || silent (osc[(size_t) i])))
                        best = i;
            if (best >= 0)
            {
                oscUsed[(size_t) best] = peakUsed[(size_t) j] = true;
                peakOf[(size_t) best] = j;
            }
        }
    }
    else
    {
        // RANK: the oscillators sounding, in the order of their partials (then the silent ones), take the peaks in
        // frequency order: a partial coming in under the others moves them all up a place
        std::array<int, maxVoices> order {};
        for (int i = 0; i < maxVoices; ++i)
            order[(size_t) i] = i;
        std::stable_sort (order.begin(), order.end(), [this, &silent] (int a, int b)
        {
            const bool sa = silent (osc[(size_t) a]), sb = silent (osc[(size_t) b]);
            if (sa != sb)
                return ! sa;
            return osc[(size_t) a].peak < osc[(size_t) b].peak;
        });
        for (int i = 0; i < F; ++i)
            peakOf[(size_t) order[(size_t) i]] = i;
    }
    // XF: the old pitch fades out over the slice while the new one comes in
    const bool xfMode = target.wave >= 5;
    const float fade = (float) std::clamp ((double) sinceSlice, 0.005 * sr, 4.0 * sr);
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& o = osc[(size_t) i];
        if (peakOf[(size_t) i] < 0)
        {
            o.ampTarget = 0.0f;
            continue;
        }
        const auto& f = found[(size_t) peakOf[(size_t) i]];
        const bool born = silent (o) || o.peak <= 0.0f;
        const bool moved = ! born && std::abs (std::log2 (f.second / o.peak)) > 5.0f / 1200.0f;
        if (xfMode && moved && o.amp > 1.0e-4f)
        {
            const float gIn = o.xf < 1.0f ? std::sqrt (o.xf) : 1.0f, gOut = o.xf < 1.0f ? std::sqrt (1.0f - o.xf) : 0.0f;
            if (o.xf < 1.0f && gOut > gIn)
            {
                // (mid-fade, the old one louder: the new one let go quickly, the old one fading from where it is)
                o.tail = { o.phase, o.hz, o.amp * gIn };
                o.ghost.gain *= gOut;
            }
            else
            {
                if (o.xf < 1.0f)
                    o.tail = { o.ghost.phase, o.ghost.hz, o.ghost.gain * gOut };
                o.ghost = { o.phase, o.hz, o.amp * gIn };
            }
            o.xf = 0.0f;
            o.xfInc = 1.0f / fade;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            o.phase = (double) rng / 4294967296.0;      // (a new oscillator: its own phase)
        }
        o.peak = f.second;
        o.ampTarget = f.first * norm;
        if (born)
        {
            o.hz = 0.0f;                                  // (silent: it starts on its pitch, and follows the estimate
            o.young = N;                                  //  as it settles, the partial filling the window)
        }
    }
}

void Spectral::slice()
{
    if (! target.freeze)
        analyse();
    pick();
    assign();
    sinceSlice = 0;
}

void Spectral::process (float* L, float* R, int n)
{
    const float k = (float) std::min (1.0, 100.0 / sr);
    const float ampK = (float) (1.0 - std::exp (-1.0 / (sr * 0.01)));
    // GLIDE: in semitones, 1 ms .. 10 s (the first half as before 0.4.2), at its end HOLD
    const double g = std::clamp ((double) target.glide, 0.0, 1.0);
    const bool hold = g >= 0.995;
    const double tau = 0.001 + 0.6 * g * g + 9.4 * std::pow (g, 8.0);
    const float k8 = hold ? 0.0f : (float) (1.0 - std::exp (-8.0 / (sr * tau)));
    const float fb = std::clamp (target.feedback, 0.0f, 1.0f);
    const float fbRel = (float) std::exp (-1.0 / (0.3 * sr));
    const float tailK = (float) std::exp (-1.0 / (0.002 * sr));
    const int waveWanted = std::clamp (target.wave, 0, 6);
    // the slices
    const int clock = std::clamp (target.clock, 0, 3);
    const double freeHop = sr * (0.006 + 0.5 * std::pow ((double) std::clamp (target.slice, 0.0f, 1.0f), 2.0)) * (double) (1 << std::clamp (target.mult, 0, 5));
    const double beats = divisions()[std::clamp (target.division, 0, numDivisions() - 1)].beats;
    const double bpm = target.bpm > 1.0 ? target.bpm : 120.0;
    const double syncHop = beats * 60.0 / bpm * sr;
    const int triggerDelay = (int) (sr * (target.transients ? 0.03 : 0.055));
    if (clock != 1 || ! target.playing)
        lastGrid = -1;
    if (clock == 2 && triggered)
        pendingIn = triggerDelay;
    triggered = false;
    // the pitches the oscillators go to: their partials, warped around the centre (q 0 .. 2), transposed
    const float trans = std::exp2 (std::round (std::clamp (target.octave, -2.0f, 3.0f)) + std::clamp (target.fine, -7.0f, 7.0f) / 12.0f);
    const float q = 1.0f + std::clamp (target.warp, -1.0f, 1.0f);
    const float top = (float) (sr * 0.49);
    std::array<float, maxVoices> goal {};
    auto goals = [&]
    {
        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& o = osc[(size_t) i];
            float f = o.peak;
            if (f > 0.0f && std::abs (q - 1.0f) > 1.0e-4f)
                f = centreHz * std::pow (f / centreHz, q);
            goal[(size_t) i] = std::clamp (f * trans, 5.0f, top);
        }
    };
    goals();
    const float dryG0 = 0.0f;
    (void) dryG0;
    for (int i = 0; i < n; ++i)
    {
        mix += (target.mix - mix) * k;
        const float dl = L[i], dr = R != nullptr ? R[i] : L[i];
        const float mono = 0.5f * (dl + dr);
        // FEEDBACK: the result back into the analysis, held under 0.5 by a limiter (instant up, 300 ms down)
        const float a = std::abs (lastOut);
        fbEnv = a > fbEnv ? a : fbEnv * fbRel;
        const float lim = fbEnv > 0.5f ? 0.5f / fbEnv : 1.0f;
        ring[(size_t) ringPos] = mono + fb * lastOut * lim;
        ringPos = (ringPos + 1) % N;
        ++sinceSlice;
        ++sinceAnalysis;
        // ATTACKS: a fast follower jumping over a slow one
        if (clock == 3)
        {
            const float x = std::abs (mono);
            onsetFast += (x - onsetFast) * (x > onsetFast ? 0.02f : 0.0015f) * (float) (48000.0 / sr);
            onsetSlow += (x - onsetSlow) * (x > onsetSlow ? 0.0004f : 0.0001f) * (float) (48000.0 / sr);
            if (onsetHold > 0)
                --onsetHold;
            else if (onsetFast > 2.0f * onsetSlow + 0.003f)
            {
                pendingIn = triggerDelay;
                onsetHold = (int) (0.05 * sr);
            }
        }
        bool due = false;
        if (clock == 0)
            due = sinceSlice >= freeHop;
        else if (clock == 1)
        {
            if (target.playing)
            {
                const double at = target.ppq + (double) i * bpm / (60.0 * sr);
                const auto grid = (long long) std::floor (at / beats + 1.0e-9);
                due = grid != lastGrid;
                lastGrid = grid;
            }
            else
                due = sinceSlice >= syncHop;
        }
        if (pendingIn >= 0 && pendingIn-- == 0)
            due = true;
        if (due)
        {
            slice();
            goals();
        }
        // the pitches: every 8 samples a step in log2 towards their goal, spread evenly over the 8
        if ((i & 7) == 0)
            for (int v = 0; v < maxVoices; ++v)
            {
                auto& o = osc[(size_t) v];
                const float gl = std::log2 (goal[(size_t) v]);
                if (o.hz <= 0.0f || o.young > 0)
                {
                    o.young = std::max (0, o.young - 8);
                    o.lhz = gl;
                    o.hz = goal[(size_t) v];
                    o.mul = 1.0f;
                    continue;
                }
                o.hz = std::exp2 (o.lhz);
                const float d = (gl - o.lhz) * k8;
                o.lhz += d;
                o.mul = std::exp2 (d * 0.125f);
            }
        if (waveWanted != waveNow)
        {
            waveDuck -= (float) (1.0 / (0.003 * sr));
            if (waveDuck <= 0.0f)
            {
                waveDuck = 0.0f;
                waveNow = waveWanted;
            }
        }
        else if (waveDuck < 1.0f)
            waveDuck = std::min (1.0f, waveDuck + (float) (1.0 / (0.005 * sr)));
        const int wave = waveNow;
        float outL = 0.0f, outR = 0.0f;
        int idx = 0;
        for (auto& o : osc)
        {
            o.amp += (o.ampTarget - o.amp) * ampK;
            o.hz *= o.mul;
            float s = 0.0f;
            const bool fading = o.xf < 1.0f;
            if (o.amp > 1.0e-5f && o.hz > 0.0f && o.hz < top)
            {
                const double dt = o.hz / sr;
                o.phase += dt;
                if (o.phase >= 1.0) o.phase -= 1.0;
                s = spectreWave (wave, o.phase, dt) * o.amp * (fading ? std::sqrt (o.xf) : 1.0f);
            }
            if (fading)
            {
                if (o.ghost.gain > 1.0e-5f && o.ghost.hz < top)
                {
                    const double dt = o.ghost.hz / sr;
                    o.ghost.phase += dt;
                    if (o.ghost.phase >= 1.0) o.ghost.phase -= 1.0;
                    s += spectreWave (wave, o.ghost.phase, dt) * o.ghost.gain * std::sqrt (1.0f - o.xf);
                }
                o.xf = std::min (1.0f, o.xf + o.xfInc);
                if (o.xf >= 1.0f)
                    o.ghost.gain = 0.0f;
            }
            if (o.tail.gain > 1.0e-5f)
            {
                const double dt = o.tail.hz / sr;
                o.tail.phase += dt;
                if (o.tail.phase >= 1.0) o.tail.phase -= 1.0;
                if (o.tail.hz < top)
                    s += spectreWave (wave, o.tail.phase, dt) * o.tail.gain;
                o.tail.gain *= tailK;
            }
            // spread: every other oscillator leans left or right
            const float pan = (idx % 2 == 0) ? 0.3f : 0.7f;
            outL += s * (1.0f - pan) * 1.4f;
            outR += s * pan * 1.4f;
            ++idx;
        }
        outL *= waveDuck;
        outR *= waveDuck;
        lastOut = 0.5f * (outL + outR);
        if (! std::isfinite (lastOut))
        {
            // (a number gone wrong: everything let go, nothing sent on)
            for (auto& o : osc)
                o = Osc();
            std::fill (ring.begin(), ring.end(), 0.0f);
            std::fill (smooth.begin(), smooth.end(), 0.0f);
            lastOut = fbEnv = outL = outR = 0.0f;
        }
        // (0.4.5) the resynthesis brought to the sound's level (followers over half a second; held through silence,
        // so a frozen spectrum or a drone keeps its level), within +-12 dB
        {
            const float pk = (float) (1.0 / (0.5 * sr));
            dryPow += pk * (mono * mono - dryPow);
            wetPow += pk * (0.25f * (outL + outR) * (outL + outR) - wetPow);
            if (dryPow > 1.0e-6f && wetPow > 1.0e-9f)
                wetGain += 0.0004f * (std::clamp (std::sqrt (dryPow / wetPow), 0.25f, 4.0f) - wetGain);
        }
        // MIX: at constant power
        const float th = std::clamp (mix, 0.0f, 1.0f) * 1.5707963f;
        const float dg = std::cos (th), wg = std::sin (th) * wetGain;
        L[i] = dl * dg + outL * wg;
        if (R != nullptr)
            R[i] = dr * dg + outR * wg;
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
        x.st->s.presetDefault (1, (float) sr);   // (presetCheaper drifts ~11 cents on a pure tone)
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
        x.st->s.reset();
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
            x.st->s.setTransposeSemitones (semis);
            const float* ins[1] = { fbIn.data() };
            float* outs[1] = { out.data() };
            x.st->s.process (ins, n, outs, n);
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
        // (0.4.5) at constant power, the shifted voices brought up to the dry's level: the sound kept as loud
        const float th = std::clamp (mix, 0.0f, 1.0f) * 1.5707963f;
        const float dg = std::cos (th), wg = std::sin (th) * 1.65f;
        L[i] = L[i] * dg + outL[i] * wg;
        if (R != nullptr)
            R[i] = R[i] * dg + outR[i] * wg;
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
        if (target.mode != modeNow)
        {
            duck -= (float) (1.0 / (0.003 * sr));
            if (duck <= 0.0f)
            {
                duck = 0.0f;
                modeNow = target.mode;
                fbState[0] = fbState[1] = 0.0f;
            }
        }
        else if (duck < 1.0f)
            duck = std::min (1.0f, duck + (float) (1.0 / (0.005 * sr)));
        const float dl = L[i], dr = R != nullptr ? R[i] : L[i];
        float wl = 0.0f, wr = 0.0f;
        w = (w + 1) & mask;
        if (modeNow == 1)
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
            if (modeNow == 2)
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
        const float m = modeNow == 2 ? std::max (mix, 0.0f) : mix;
        float ol, orr;
        if (modeNow == 2)
        {
            ol = dl + m * (wl - dl);
            orr = dr + m * (wr - dr);
        }
        else
        {
            // (0.4.5) at constant power: the voices beside the dry, not instead of it
            const float th = std::clamp (m, 0.0f, 1.0f) * 1.5707963f;
            const float dg = std::cos (th), wg = std::sin (th);
            ol = dl * dg + wl * wg;
            orr = dr * dg + wr * wg;
        }
        // (through a mode change: the dry alone, for a few ms)
        L[i] = dl + duck * (ol - dl);
        if (R != nullptr)
            R[i] = dr + duck * (orr - dr);
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
    const float mixK = (float) std::min (1.0, 330.0 / sr);
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
        mix += (target.mix - mix) * mixK;
        L[i] = dl + mix * (lp[0] - dl);
        if (R != nullptr)
            R[i] = dr + mix * (lp[1] - dr);
    }
}
}
