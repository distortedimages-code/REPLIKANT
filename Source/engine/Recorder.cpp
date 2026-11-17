#include "Recorder.h"

Recorder::Recorder() = default;
Recorder::~Recorder() = default;   // the runs belong to keep[]

int Recorder::getNumChannels() const
{
    return keep[0] != nullptr ? keep[0]->channels : 2;
}

bool Recorder::start (const Plan& plan, double sampleRate, int numChannels)
{
    if (running.load() || plan.jobs.empty() || sampleRate <= 0.0)
        return false;
    auto r = std::make_shared<Run>();
    r->plan = plan;
    r->sampleRate = sampleRate;
    r->channels = juce::jlimit (1, 2, numChannels);
    auto secs = [sampleRate] (double s) { return (int) std::lround (s * sampleRate); };
    r->holdS = juce::jmax (1, secs (plan.hold));
    r->releaseMaxS = juce::jmax (0, secs (plan.releaseMax));
    r->pauseS = secs (plan.pause);
    r->noiseS = secs (0.5);
    r->programS = plan.program >= 0 ? secs (0.4) : 0;
    r->maxGapS = r->pauseS + secs (3.0);
    r->quietS = secs (0.10);
    r->minReleaseS = secs (0.03);
    // the ring holds a whole note (and its gap) several times over
    const double longest = plan.hold + plan.releaseMax + plan.pause + 8.0;
    int64_t size = 1;
    while ((double) size < longest * sampleRate * 2.0)
        size <<= 1;
    r->mask = size - 1;
    r->ring.assign ((size_t) r->channels, std::vector<float> ((size_t) size, 0.0f));

    keep[1] = keep[0];
    keep[0] = r;
    abortRequested = false;
    fifo.reset();
    written.store (0, std::memory_order_release);
    jobShown = 0;
    running = true;
    incoming.store (r.get(), std::memory_order_release);
    return true;
}

void Recorder::stop()
{
    if (running.load())
        abortRequested = true;
}

void Recorder::push (Event::Type t, int j, int64_t at, int64_t from)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        events[(size_t) scope.startIndex1] = Event { t, j, at, from };
    else if (scope.blockSize2 > 0)
        events[(size_t) scope.startIndex2] = Event { t, j, at, from };
}

int Recorder::popEvents (Event* dest, int max)
{
    const auto scope = fifo.read (juce::jmin (max, fifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i)
        dest[n++] = events[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        dest[n++] = events[(size_t) (scope.startIndex2 + i)];
    return n;
}

bool Recorder::copy (int64_t from, int64_t to, std::vector<std::vector<float>>& dest) const
{
    auto r = keep[0];
    if (r == nullptr || to < from)
        return false;
    const int64_t w = getWritten();
    const int64_t size = r->mask + 1;
    if (to > w || from < w - size + 256 || from < 0)
        return false;
    dest.assign ((size_t) r->channels, std::vector<float> ((size_t) (to - from)));
    for (int c = 0; c < r->channels; ++c)
        for (int64_t i = from; i < to; ++i)
            dest[(size_t) c][(size_t) (i - from)] = r->ring[(size_t) c][(size_t) (i & r->mask)];
    // the audio thread may have lapped us while copying
    return getWritten() - size + 256 <= from;
}

void Recorder::enter (Phase p)
{
    phase = p;
    phaseSamples = 0;
    quietSamples = 0;
    phaseShown = (int) p;
}

void Recorder::finish (bool aborted, MidiSink& sink)
{
    const int ch = run != nullptr ? run->plan.channel : 1;
    if (noteDown)
    {
        sink.send (juce::MidiMessage::noteOff (ch, noteNumber));
        noteDown = false;
    }
    sink.send (juce::MidiMessage::allNotesOff (ch));
    push (aborted ? Event::aborted : Event::finished, job, written.load (std::memory_order_relaxed));
    enter (Phase::idle);
    running = false;
}

void Recorder::beginBlock (MidiSink& sink)
{
    if (auto* r = incoming.exchange (nullptr, std::memory_order_acq_rel))
    {
        run = r;
        job = 0;
        noteDown = false;
        env = 0.0f;
        envDecay = (float) std::exp (-1.0 / (0.015 * r->sampleRate));
        noiseSum = 0.0;
        noiseCount = 0;
        if (r->plan.program >= 0)
        {
            sink.send (juce::MidiMessage::programChange (r->plan.channel, juce::jlimit (0, 127, r->plan.program)));
            enter (Phase::program);
        }
        else
            enter (Phase::noise);
        sink.send (juce::MidiMessage::allNotesOff (r->plan.channel));
        noiseFrom = written.load (std::memory_order_relaxed);
    }
    if (phase == Phase::idle || run == nullptr)
        return;
    if (abortRequested.exchange (false))
    {
        finish (true, sink);
        return;
    }

    auto& R = *run;
    const int64_t now = written.load (std::memory_order_relaxed);
    switch (phase)
    {
        case Phase::program:
            if (phaseSamples >= R.programS)
            {
                enter (Phase::noise);
                noiseFrom = now;
                noiseSum = 0.0;
                noiseCount = 0;
            }
            break;

        case Phase::noise:
            if (phaseSamples >= R.noiseS)
            {
                const float rms = noiseCount > 0 ? (float) std::sqrt (noiseSum / (double) noiseCount) : 0.0f;
                floorDb = juce::Decibels::gainToDecibels (rms, -200.0f);
                // a tail is over when its peak follower falls this low: +8 dB over the floor, never above -60 dBFS
                silence = juce::jlimit (1.0e-5f, 1.0e-3f, rms * 2.5f);
                push (Event::noiseDone, -1, now, noiseFrom);
                enter (Phase::gap);
            }
            break;

        case Phase::gap:
            if (phaseSamples >= R.pauseS && (quietSamples >= R.quietS / 2 || phaseSamples >= R.maxGapS))
            {
                if (job >= (int) R.plan.jobs.size())
                {
                    finish (false, sink);
                    return;
                }
                const auto& j = R.plan.jobs[(size_t) job];
                noteNumber = j.note;
                sink.send (juce::MidiMessage::noteOn (R.plan.channel, j.note, (juce::uint8) juce::jlimit (1, 127, j.velocity)));
                noteDown = true;
                jobShown = job;
                push (Event::noteOn, job, now);
                enter (Phase::hold);
            }
            break;

        case Phase::hold:
            if (phaseSamples >= R.holdS)
            {
                sink.send (juce::MidiMessage::noteOff (R.plan.channel, noteNumber));
                noteDown = false;
                push (Event::noteOff, job, now);
                enter (Phase::release);
            }
            break;

        case Phase::release:
            if ((phaseSamples >= R.minReleaseS && quietSamples >= R.quietS) || phaseSamples >= R.releaseMaxS)
            {
                push (Event::jobDone, job, now);
                ++job;
                enter (Phase::gap);
            }
            break;

        case Phase::idle:
            break;
    }
}

void Recorder::capture (const float* const* in, int numChannels, int n)
{
    // the meter runs even when not sampling
    float peak = 0.0f;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < n; ++i)
            peak = juce::jmax (peak, std::abs (in[c][i]));
    meter = juce::jmax (peak, meter.load() * 0.8f);

    if (phase == Phase::idle || run == nullptr)
        return;
    auto& R = *run;
    const int64_t w0 = written.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        float a = 0.0f;
        for (int c = 0; c < R.channels; ++c)
        {
            const float v = numChannels > 0 ? in[juce::jmin (c, numChannels - 1)][i] : 0.0f;
            R.ring[(size_t) c][(size_t) ((w0 + i) & R.mask)] = v;
            a = juce::jmax (a, std::abs (v));
            if (phase == Phase::noise)
                noiseSum += (double) v * v;
        }
        if (phase == Phase::noise)
            noiseCount += R.channels;
        env = juce::jmax (a, env * envDecay);
        if (env < silence)
            ++quietSamples;
        else
            quietSamples = 0;
    }
    phaseSamples += n;
    written.store (w0 + n, std::memory_order_release);
}
