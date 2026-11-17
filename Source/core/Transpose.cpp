#include "Transpose.h"
#include "Sinc.h"
#include "LoopFinder.h"

#include "signalsmith-stretch.h"

#include <algorithm>
#include <cmath>

namespace rk
{
namespace
{
    Sample varispeed (const Sample& src, double semitones, const RenderSettings& set)
    {
        Sample out = src;
        double step = ratioOf (semitones);
        const int len = src.length();
        const bool looped = src.loop.valid() && src.loop.end <= len;

        int outLen = 0;
        double pos0 = 0.0;
        if (looped)
        {
            const int L = src.loop.length();
            const int Lout = std::max (2, (int) std::lround ((double) L / step));
            step = (double) L / (double) Lout;                            // a hair off, < 0.1 cent for any real loop
            const int N0 = (int) std::lround ((double) src.loop.start / step);
            pos0 = (double) src.loop.start - (double) N0 * step;          // position N0 lands exactly on the loop start
            out.loop.start = N0;
            out.loop.end = N0 + Lout;
            out.loop.crossfade = (int) std::lround (src.loop.crossfade / step);
            outLen = out.loop.end + set.tailAfterLoop;
        }
        else
        {
            outLen = (int) std::ceil ((double) len / step);
        }
        out.noteOff = src.noteOff >= 0 ? (int) std::lround (src.noteOff / step) : -1;

        // for the formant pass, the loop is rendered a little further, so its frames see the loop continue
        const int extra = (looped && set.algo == Algo::formant) ? 8192 : 0;
        for (size_t c = 0; c < src.audio.size(); ++c)
        {
            SourceView v { src.audio[c].data(), len, looped ? src.loop.start : -1, looped ? src.loop.end : -1 };
            out.audio[c].assign ((size_t) (outLen + extra), 0.0f);
            resample (v, pos0, step, out.audio[c].data(), outLen + extra);
        }

        if (set.algo == Algo::formant)
        {
            FormantSettings fs;
            fs.amount = set.formantAmount;
            const double f0 = noteHz (src.root + src.detuneCents / 100.0) * step;   // the pitch it plays now
            correctFormants (out.audio, src.sampleRate, step, f0, fs);
            for (auto& ch : out.audio)
                ch.resize ((size_t) outLen);
            if (looped)
            {
                // the equalisation follows the sound over time: a very short blend hides what moved at the seam
                const int C = std::max (out.loop.crossfade / 2, (int) (src.sampleRate * 0.004));
                bakeCrossfade (out.audio, out.loop, std::min (C, (int) (src.sampleRate * 0.02)));
                out.loop.crossfade = std::max (out.loop.crossfade, (int) std::lround (src.loop.crossfade / step));
            }
        }
        return out;
    }

    Sample keepingTime (const Sample& src, double semitones, const RenderSettings& set)
    {
        Sample out = src;
        const int len = src.length();
        const int nch = src.channels();
        if (len < 4096 || nch == 0)
            return varispeed (src, semitones, RenderSettings { Algo::formant, 1.0 });
        signalsmith::stretch::SignalsmithStretch<float> st;
        st.presetDefault (nch, (float) src.sampleRate);
        // a plain pitch shift (the envelope moves with it, as with varispeed), then the same formant
        // correction as the formant mode: measured much closer to the real thing than the stretcher's own
        st.setTransposeSemitones ((float) semitones);
        const double f0 = noteHz (src.root + src.detuneCents / 100.0);

        std::vector<const float*> in ((size_t) nch);
        std::vector<float*> outp ((size_t) nch);
        for (int c = 0; c < nch; ++c)
        {
            in[(size_t) c] = src.audio[(size_t) c].data();
            out.audio[(size_t) c].assign ((size_t) len, 0.0f);
            outp[(size_t) c] = out.audio[(size_t) c].data();
        }
        st.exact (in.data(), len, outp.data(), len);
        FormantSettings fs;
        fs.amount = set.formantAmount;
        correctFormants (out.audio, src.sampleRate, ratioOf (semitones), f0 * ratioOf (semitones), fs);

        if (src.loop.valid())
        {
            const double newF0 = f0 * ratioOf (semitones);
            out.loop = refineLoop (out.audio, src.sampleRate, src.loop, (int) (src.sampleRate * 0.03), newF0);
            bakeCrossfade (out.audio, out.loop, std::max (src.loop.crossfade, (int) (src.sampleRate * 0.03)));
        }
        return out;
    }
}

Sample transposed (const Sample& src, double semitones, const RenderSettings& set)
{
    if (std::abs (semitones) < 1.0e-6 || src.length() == 0)
        return src;
    Sample out = set.algo == Algo::fixedTime ? keepingTime (src, semitones, set) : varispeed (src, semitones, set);
    out.peak = peakOf (out.audio);
    out.detuneCents = 0.0f;
    return out;
}
}
