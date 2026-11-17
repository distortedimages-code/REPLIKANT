#include "Player.h"
#include "../core/Sinc.h"

void Player::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    allNotesOff (true);
}

void Player::setInstrument (const Instrument* i)
{
    if (i != ins)
    {
        allNotesOff (true);
        ins = i;
        rrCounter.fill (0);
    }
}

void Player::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        if (immediately)
            v.active = false;
        else if (v.active)
            v.held = false, v.releasing = true;
    }
    sustained.fill (false);
    for (auto& k : keyDown)
        k = false;
}

Player::Voice& Player::freeVoice()
{
    for (auto& v : voices)
        if (! v.active)
            return v;
    // steal: the oldest releasing voice, else the oldest
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.releasing && (best == nullptr || v.age < best->age))
            best = &v;
    if (best == nullptr)
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age)
                best = &v;
    return *best;
}

void Player::noteOn (int note, int velocity)
{
    if (ins == nullptr)
        return;
    const auto srcs = ins->sourcesFor (note, velocity);
    if (srcs.empty())
        return;
    const auto& src = srcs[(size_t) (rrCounter[(size_t) note]++ % (int) srcs.size())];
    if (src.sample == nullptr || src.sample->length() == 0)
        return;
    auto& v = freeVoice();
    v = Voice();
    v.active = true;
    v.held = true;
    v.note = note;
    v.velocity = velocity;
    v.index = src.index;
    v.s = src.sample;
    v.shift = src.semitones;
    v.rateScale = src.sample->sampleRate / sr;
    v.age = ++clock;
    v.originalLength = ins->samples[(size_t) src.index].length();
    const float recorded = (float) juce::jmax (1, ins->samples[(size_t) src.index].velocity);
    v.gain = std::pow (juce::jlimit (0.0f, 1.0f, (float) velocity / recorded), params.velocity.load());
    const float attack = juce::jmax (0.0f, params.attackMs.load()) * 0.001f * (float) sr;
    v.env = attack < 1.0f ? 1.0f : 0.0f;
    v.attackStep = attack < 1.0f ? 1.0f : 1.0f / attack;
    keyDown[(size_t) note] = true;
}

void Player::noteOff (int note)
{
    if (sustain)
    {
        sustained[(size_t) note] = true;
        return;
    }
    const bool oneShot = ins != nullptr && ins->session.oneShot;
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
        {
            v.held = false;
            if (! oneShot)
                v.releasing = true;
        }
    keyDown[(size_t) note] = false;
}

void Player::renderVoice (Voice& v, float* const* out, int numOut, int start, int n)
{
    const auto& s = *v.s;
    const int len = s.length();
    const int nch = s.channels();
    const bool loop = params.loops.load() && s.loop.valid() && ! (ins != nullptr && ins->session.oneShot);
    const double step = rk::ratioOf (v.shift + bend * params.bendRange.load()) * v.rateScale;
    const bool exact = std::abs (step - 1.0) < 1.0e-9 && std::abs (v.pos - std::round (v.pos)) < 1.0e-9;
    const double cutoff = 0.9 * juce::jmin (1.0, 1.0 / step);
    const float level = v.gain * juce::Decibels::decibelsToGain (params.gainDb.load());
    const float relCoef = (float) std::exp (std::log (1.0e-4) / (juce::jmax (1.0f, params.releaseMs.load()) * 0.001 * sr));

    for (int i = 0; i < n; ++i)
    {
        if (v.pos >= (double) len)
        {
            v.active = false;
            break;
        }
        // envelope
        if (v.releasing)
        {
            v.env *= relCoef;
            if (v.env < 1.0e-4f)
            {
                v.active = false;
                break;
            }
        }
        else if (v.env < 1.0f)
            v.env = juce::jmin (1.0f, v.env + v.attackStep);
        const float g = v.env * level;

        for (int c = 0; c < numOut; ++c)
        {
            const int sc = juce::jmin (c, nch - 1);
            float x;
            if (exact)
                x = s.audio[(size_t) sc][(size_t) (int64_t) v.pos];
            else
            {
                rk::SourceView view { s.audio[(size_t) sc].data(), len, loop ? s.loop.start : -1, loop ? s.loop.end : -1 };
                x = rk::interpolate (view, v.pos, cutoff, rk::SincKernel::live());
            }
            out[c][start + i] += x * g;
        }
        v.pos += step;
        if (loop && v.pos >= (double) s.loop.end)
            v.pos -= (double) s.loop.length();
    }
}

void Player::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int n = out.getNumSamples();
    const int numOut = out.getNumChannels();
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos)
            return;
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, out.getArrayOfWritePointers(), numOut, pos, end - pos);
        pos = end;
    };
    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())
            bend = (m.getPitchWheelValue() - 8192) / 8192.0;
        else if (m.isSustainPedalOn())
            sustain = true;
        else if (m.isSustainPedalOff())
        {
            sustain = false;
            for (int k = 0; k < 128; ++k)
                if (sustained[(size_t) k])
                {
                    sustained[(size_t) k] = false;
                    noteOff (k);
                }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allNotesOff (m.isAllSoundOff());
    }
    renderTo (n);

    // what the editor shows
    int count = 0;
    const Voice* last = nullptr;
    for (auto& v : voices)
        if (v.active)
        {
            ++count;
            if (last == nullptr || v.age > last->age)
                last = &v;
        }
    activeVoices = count;
    if (last != nullptr)
    {
        shownSample = last->index;
        const int l = last->s->length();
        shownPosition = (float) (l > 0 ? last->pos * (double) last->originalLength / (double) l : 0.0);
    }
    else
        shownSample = -1;
}
