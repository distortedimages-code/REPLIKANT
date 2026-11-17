#include "MidiClock.h"

#include <thread>

MidiClock::MidiClock() : juce::Thread ("REPLIKANT clock")
{
    startThread (juce::Thread::Priority::highest);
}

MidiClock::~MidiClock()
{
    wanted = false;
    stopThread (1000);
    if (threadRunning)
        sendNow (0xFC);
}

void MidiClock::setDestination (const juce::String& id, const juce::String& name)
{
    if (id == destId && (id.isEmpty() || id == "host" || port.load() != nullptr))
        return;
    const juce::ScopedLock sl (portLock);
    destId = id;
    destName = name;
    port = nullptr;
    retired = std::move (portOwner);
    toHost = id == "host";
    if (id.isNotEmpty() && id != "host")
    {
        auto found = id;
        bool here = false;
        const auto devices = juce::MidiOutput::getAvailableDevices();
        for (auto& d : devices)
            here = here || d.identifier == id;
        if (! here)
            for (auto& d : devices)
                if (d.name == name)
                    found = d.identifier;
        portOwner = juce::MidiOutput::openDevice (found);
        port = portOwner.get();
    }
}

bool MidiClock::isMissing() const
{
    return destId.isNotEmpty() && destId != "host" && port.load() == nullptr;
}

void MidiClock::setMode (Mode m)
{
    mode = (int) m;
}

void MidiClock::setRunning (bool on)
{
    wanted = on;
}

void MidiClock::sendNow (juce::uint8 b)
{
    const juce::ScopedLock sl (portLock);
    if (auto* p = port.load())
        p->sendMessageNow (juce::MidiMessage (b));
}

void MidiClock::push (juce::uint8 b, double t)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        events[(size_t) scope.startIndex1] = { t, b };
}

void MidiClock::process (bool hostPlaying, double ppq, double hostBpm, int n, double sr, juce::MidiBuffer& out)
{
    if (n <= 0 || sr <= 0.0)
        return;
    // the block's start, as a steady clock (the callbacks come a little early or late)
    const double now = juce::Time::getMillisecondCounterHiRes();
    {
        static constexpr double resync = 20.0;
        if (blockTime < 0.0 || std::abs (now - blockTime) > resync)
            blockTime = now;
        else
            blockTime += 0.02 * (now - blockTime);
    }
    const double startMs = blockTime;
    blockTime += (double) n / sr * 1000.0;

    const bool plugin = toHost.load();
    const bool hasPort = port.load() != nullptr;
    auto emit = [&] (juce::uint8 b, double at)
    {
        const int s = juce::jlimit (0, n - 1, (int) at);
        if (plugin)
            out.addEvent (juce::MidiMessage (b), s);
        else if (hasPort)
            push (b, startMs + at / sr * 1000.0);
    };

    if ((Mode) mode.load() == Mode::host)
    {
        if (audioRunning && ! plugin)
            audioRunning = false;
        if (hostPlaying && ! hostWasPlaying)
            emit (0xFA, 0.0);
        if (! hostPlaying && hostWasPlaying)
            emit (0xFC, 0.0);
        if (hostPlaying && hostBpm > 0.0)
        {
            const double spb = 60.0 / hostBpm * sr;               // samples a beat
            const double endPpq = ppq + (double) n / spb;
            for (double k = std::ceil (ppq * 24.0 - 1.0e-9); k / 24.0 < endPpq; k += 1.0)
                emit (0xF8, (k / 24.0 - ppq) * spb);
        }
        hostWasPlaying = hostPlaying;
        running = hostPlaying && (plugin || hasPort);
        return;
    }
    if (hostWasPlaying)
    {
        emit (0xFC, 0.0);
        hostWasPlaying = false;
    }
    // INTERNE out of the plug-in: worked out here, to the sample (to a port: the thread does it)
    if (! plugin)
    {
        if (audioRunning)
            audioRunning = false;
        return;
    }
    const bool want = wanted.load();
    if (want && ! audioRunning)
    {
        emit (0xFA, 0.0);
        audioRunning = true;
        phase = 0.0;
    }
    else if (! want && audioRunning)
    {
        emit (0xFC, 0.0);
        audioRunning = false;
    }
    if (audioRunning)
    {
        const double spt = 60.0 / (tempoNow() * 24.0) * sr;       // samples a tick
        while (phase < (double) n)
        {
            emit (0xF8, phase);
            phase += spt;
        }
        phase -= (double) n;
    }
    running = audioRunning;
}

void MidiClock::run()
{
    while (! threadShouldExit())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        double next = now + 20.0;

        // what the audio thread worked out, at its time
        for (;;)
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (1, start1, size1, start2, size2);
            if (size1 <= 0)
                break;
            const auto e = events[(size_t) start1];
            if (e.t > now)
            {
                next = juce::jmin (next, e.t);
                break;
            }
            sendNow (e.b);
            fifo.finishedRead (1);
        }

        // INTERNE to a port of the Mac
        const bool own = (Mode) mode.load() == Mode::internal && ! toHost.load() && port.load() != nullptr;
        if (own && wanted.load() && ! threadRunning)
        {
            sendNow (0xFA);
            threadRunning = true;
            running = true;
            nextTick = now;
        }
        if (threadRunning && (! own || ! wanted.load()))
        {
            sendNow (0xFC);
            threadRunning = false;
            if ((Mode) mode.load() == Mode::internal)
                running = false;
        }
        if (threadRunning)
        {
            const double period = 2500.0 / tempoNow();     // ms a tick: 60000 / (bpm * 24)
            if (now >= nextTick)
            {
                sendNow (0xF8);
                nextTick += period;
                if (now - nextTick > 100.0)        // (the machine slept)
                    nextTick = now + period;
            }
            next = juce::jmin (next, nextTick);
        }

        const double wait = next - juce::Time::getMillisecondCounterHiRes();
        if (wait > 1.5)
            std::this_thread::sleep_for (std::chrono::microseconds ((int64_t) ((wait - 1.0) * 1000.0)));
        else if (wait > 0.0)
            std::this_thread::yield();
    }
}
