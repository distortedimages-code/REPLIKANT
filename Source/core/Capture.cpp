#include "Capture.h"

#include <algorithm>
#include <cmath>

namespace rk
{
CaptureResult processCapture (const Channels& rawIn, double sr, int noteOn, int noteOff, const Job& job,
                              const Session& s, const NoiseFloor& nf, int fixedLatency)
{
    CaptureResult r;
    Sample& out = r.sample;
    out.root = job.note;
    out.velocity = job.velocity;
    out.rr = job.rr;
    out.sampleRate = sr;
    if (rawIn.empty() || rawIn[0].empty())
    {
        r.silent = true;
        return r;
    }
    Channels raw = rawIn;
    const int len = (int) raw[0].size();
    r.clipped = peakOf (raw) >= 0.999f;
    if (s.removeDc && ! nf.dc.empty())
        removeDc (raw, nf.dc);

    const float floorLevel = std::max (nf.rms, 1.0e-6f);
    const float threshold = std::max ({ floorLevel * dbToGain ((float) s.thresholdDb), nf.peak * 1.4f, dbToGain (-84.0f) });
    noteOn = std::clamp (noteOn, 0, len - 1);
    noteOff = std::clamp (noteOff, noteOn, len);

    int onset;
    if (s.align == Align::fixedLatency && fixedLatency >= 0)
        onset = std::min (len - 1, noteOn + fixedLatency);
    else
    {
        const int from = std::max (0, noteOn - (int) (sr * 0.002));
        onset = findOnset (raw, from, threshold, floorLevel, (int) (sr * 0.02));
        // nothing came above the threshold
        bool any = false;
        for (auto& c : raw)
            for (int i = from; i < len && ! any; ++i)
                any = std::abs (c[(size_t) i]) > threshold;
        if (! any)
        {
            r.silent = true;
            return r;
        }
    }
    r.latency = onset - noteOn;

    const int pre = (int) (sr * s.preMs / 1000.0);
    const int start = std::max (0, onset - pre);
    const int tailFrom = s.oneShot ? onset : noteOff;
    int end = findTailEnd (raw, tailFrom, floorLevel, sr);
    end = std::min (len, end + (int) (sr * 0.005));
    if (end <= start + 16)
        end = std::min (len, start + 16);

    out.audio.assign (raw.size(), {});
    for (size_t c = 0; c < raw.size(); ++c)
        out.audio[c].assign (raw[c].begin() + start, raw[c].begin() + end);
    out.noteOff = std::clamp (noteOff - start, 0, end - start);

    fadeIn (out.audio, std::max (8, std::min (pre, (int) (sr * 0.002))));
    const int tailLen = (end - start) - out.noteOff;
    fadeOutEnd (out.audio, std::clamp (tailLen / 4, 16, (int) (sr * 0.015)));

    // pitch, on the held part after the attack
    const auto mono = mixDown (out.audio);
    const int heldEnd = s.oneShot ? (int) mono.size() : out.noteOff;
    const int pitchFrom = std::min (heldEnd, (onset - start) + (int) (sr * 0.06));
    const auto p = detectPitch (mono, sr, noteHz (job.note), pitchFrom, heldEnd);
    out.pitchConfident = p.ok;
    out.detuneCents = p.ok ? (float) p.cents : 0.0f;

    if (s.loop && ! s.oneShot)
    {
        const double f0 = noteHz (job.note + (p.ok ? p.cents / 100.0 : 0.0));
        auto rep = findLoop (out.audio, sr, onset - start, out.noteOff, f0, s.loopSettings);
        if (rep.loop.valid())
        {
            bakeCrossfade (out.audio, rep.loop, (int) (sr * s.loopSettings.crossfadeSeconds));
            out.loop = rep.loop;
        }
    }
    out.peak = peakOf (out.audio);
    return r;
}

void shareTuning (std::vector<Sample>& samples)
{
    for (int root = 0; root < 128; ++root)
    {
        double sum = 0.0;
        int n = 0;
        for (auto& s : samples)
            if (s.root == root && s.pitchConfident)
            {
                sum += s.detuneCents;
                ++n;
            }
        if (n == 0)
            continue;
        const auto mean = (float) (sum / n);
        for (auto& s : samples)
            if (s.root == root)
            {
                s.detuneCents = mean;
                s.pitchConfident = true;
            }
    }
}

void normalise (std::vector<Sample>& samples, Normalise mode, double targetDb)
{
    if (mode == Normalise::off || samples.empty())
        return;
    const float target = dbToGain ((float) targetDb);
    if (mode == Normalise::instrument)
    {
        float p = 0.0f;
        for (auto& s : samples)
            p = std::max (p, peakOf (s.audio));
        if (p <= 1.0e-6f)
            return;
        for (auto& s : samples)
        {
            applyGain (s.audio, target / p);
            s.peak = peakOf (s.audio);
        }
        return;
    }
    for (auto& s : samples)
    {
        const float p = peakOf (s.audio);
        if (p > 1.0e-6f)
            applyGain (s.audio, target / p);
        s.peak = peakOf (s.audio);
    }
}
}
