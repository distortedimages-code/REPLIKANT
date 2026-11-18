#include "TestSynth.h"

void TestSynth::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    delaySize = (int) (sr * 0.0035);              // 3.5 ms, like a round trip through an interface
    delayL.assign ((size_t) delaySize + 1, 0.0f);
    delayR.assign ((size_t) delaySize + 1, 0.0f);
    delayPos = 0;
    reset();
}

void TestSynth::reset()
{
    for (auto& v : voices)
        v = Voice();
}

void TestSynth::handle (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        Voice* v = nullptr;
        for (auto& x : voices)
            if (! x.active)
            {
                v = &x;
                break;
            }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& x : voices)
                if (x.age < v->age)
                    v = &x;
        }
        *v = Voice();
        v->active = v->held = true;
        v->note = m.getNoteNumber();
        v->vel = m.getFloatVelocity();
        v->ph2 = 0.37;
        v->ph3 = 0.11;
        v->driftRate = 0.08 + 0.1 * rng.nextDouble();
        v->drift = rng.nextDouble();
        v->age = ++clock;
    }
    else if (m.isNoteOff())
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == m.getNoteNumber())
            {
                v.held = false;
                v.stageA = v.stageF = 2;
            }
    }
    else if (m.isController() && m.getControllerNumber() == 74)
        brightness = (float) m.getControllerValue() / 127.0f;
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices)
            if (v.active)
                v.held = false, v.stageA = v.stageF = 2;
    }
}

// a band-limited step for the saw (PolyBLEP)
float TestSynth::poly (double t, double dt) const
{
    if (t < dt)
    {
        t /= dt;
        return (float) (t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return (float) (t * t + t + t + 1.0);
    }
    return 0.0f;
}

void TestSynth::render (float* const* out, int numChannels, int n)
{
    const float noise = juce::Decibels::decibelsToGain (-86.0f);
    const float aAtt = 1.0f / (0.004f * (float) sr), aDec = (float) std::exp (-1.0 / (0.6 * sr)), aRel = (float) std::exp (-1.0 / (0.25 * sr));
    const float fAtt = 1.0f / (0.002f * (float) sr), fDec = (float) std::exp (-1.0 / (0.45 * sr));
    const float sustainA = 0.75f, sustainF = 0.25f;
    for (int i = 0; i < n; ++i)
    {
        float mix = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            // envelopes
            if (v.stageA == 0)
            {
                v.amp += aAtt;
                if (v.amp >= 1.0f) { v.amp = 1.0f; v.stageA = 1; }
            }
            else if (v.stageA == 1)
                v.amp = sustainA + (v.amp - sustainA) * aDec;
            else
            {
                v.amp *= aRel;
                if (v.amp < 1.0e-5f) { v.active = false; continue; }
            }
            if (v.stageF == 0)
            {
                v.fenv += fAtt;
                if (v.fenv >= 1.0f) { v.fenv = 1.0f; v.stageF = 1; }
            }
            else if (v.stageF == 1)
                v.fenv = sustainF + (v.fenv - sustainF) * fDec;
            else
                v.fenv *= aRel;

            // oscillators: a slow drift of a few cents, as the real thing
            v.drift += v.driftRate / sr;
            const double cents = tuningCents + 3.0 * std::sin (6.283185307 * v.drift);
            const double hz = 440.0 * std::pow (2.0, (v.note - 69 + cents / 100.0) / 12.0);
            const double d1 = hz / sr, d2 = hz * 1.0045 / sr, d3 = hz * 0.5 / sr;
            const float s1 = (float) (2.0 * v.ph1 - 1.0) - poly (v.ph1, d1);
            const float s2 = (float) (2.0 * v.ph2 - 1.0) - poly (v.ph2, d2);
            const float sq = (v.ph3 < 0.5 ? 1.0f : -1.0f) + poly (v.ph3, d3) - poly (std::fmod (v.ph3 + 0.5, 1.0), d3);
            v.ph1 += d1; if (v.ph1 >= 1.0) v.ph1 -= 1.0;
            v.ph2 += d2; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
            v.ph3 += d3; if (v.ph3 >= 1.0) v.ph3 -= 1.0;
            const float osc = 0.45f * (s1 + s2) + 0.3f * sq;

            // filter: a fixed cutoff that opens with the envelope and the velocity (little keyboard tracking)
            const float cutoff = juce::jlimit (60.0f, (float) sr * 0.2f,
                                              (500.0f + 4500.0f * v.fenv * (0.35f + 0.65f * v.vel) + (float) hz * 0.5f)
                                                  * std::exp2 ((brightness - 0.5f) * 4.0f));
            const float g = std::tan (juce::MathConstants<float>::pi * cutoff / (float) sr);
            const float k = 0.7f;   // resonance
            // two state-variable stages (24 dB/oct), trapezoidal (Simper)
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            auto svf = [a1, a2, a3] (float x, float& ic1, float& ic2)
            {
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                return v2;
            };
            float y = svf (osc, v.lp, v.bp);
            y = svf (y, v.lp2, v.bp2);
            mix += y * v.amp * (0.25f + 0.75f * v.vel) * 0.3f;
        }
        mix = std::tanh (mix);
        // the line: a little latency and a noise floor
        const int rd = delayPos;
        const float l = delayL[(size_t) rd], r = delayR[(size_t) rd];
        delayL[(size_t) rd] = mix + noise * (rng.nextFloat() * 2.0f - 1.0f);
        delayR[(size_t) rd] = mix * 0.97f + noise * (rng.nextFloat() * 2.0f - 1.0f);
        delayPos = (delayPos + 1) % (delaySize + 1);
        if (numChannels > 0) out[0][i] = l;
        if (numChannels > 1) out[1][i] = r;
        for (int c = 2; c < numChannels; ++c)
            out[c][i] = 0.0f;
    }
}
