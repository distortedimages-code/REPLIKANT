// The MIDI clock that REPLIKANT sends (24 ticks a quarter note, Start, Stop) to start analog sequencers, drum
// machines, arpeggiators: to a MIDI port of the Mac, or out of the plug-in (the DAW routes it).
//
//   INTERNE  at REPLIKANT's tempo, started and stopped by its own button (or by a sampling session, if asked)
//   HÔTE     follows the DAW: Start when it plays, its tempo, its bar (the ticks on its grid), Stop when it stops
//
// Ticks for a port of the Mac come from a high-priority thread of their own (they don't wait for the audio), or,
// when they follow the DAW, are worked out on the audio thread and sent by that thread at their time; out of the
// plug-in, they are written into the block at their sample.

#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

class MidiClock : private juce::Thread
{
public:
    enum class Mode { internal = 0, host = 1 };

    MidiClock();
    ~MidiClock() override;

    // message thread
    void setDestination (const juce::String& identifier, const juce::String& name);   // "" none, "host" the plug-in's out
    juce::String destinationId() const              { return destId; }
    juce::String destinationName() const            { return destName; }
    bool isMissing() const;                          // a port chosen but not found
    void setMode (Mode);
    Mode getMode() const                             { return (Mode) mode.load(); }
    void setRunning (bool);                          // INTERNE: start / stop
    bool isRunning() const                           { return running.load(); }   // ticking now
    bool isWanted() const                            { return wanted.load(); }    // INTERNE: started (sent somewhere or not)
    void bindTempo (std::atomic<float>* t)           { tempo = t; }

    // audio thread, each block: the DAW's transport, and the plug-in's MIDI out
    void process (bool hostPlaying, double hostPpq, double hostBpm, int numSamples, double sampleRate, juce::MidiBuffer& out);

private:
    void run() override;
    void push (juce::uint8 byte, double timeMs);
    void sendNow (juce::uint8 byte);
    double tempoNow() const                          { return tempo != nullptr ? juce::jlimit (20.0, 400.0, (double) tempo->load()) : 120.0; }

    juce::String destId, destName;
    juce::CriticalSection portLock;      // the thread sends while the message thread changes the port
    std::unique_ptr<juce::MidiOutput> portOwner, retired;
    std::atomic<juce::MidiOutput*> port { nullptr };
    std::atomic<bool> toHost { false };
    std::atomic<int> mode { (int) Mode::internal };
    std::atomic<bool> wanted { false }, running { false };
    std::atomic<float>* tempo = nullptr;

    // the thread's own clock (INTERNE to a port)
    bool threadRunning = false;
    double nextTick = 0.0;

    // what the audio thread worked out, for the port (time in ms of the hi-res counter)
    struct Event { double t; juce::uint8 b; };
    std::array<Event, 1024> events {};
    juce::AbstractFifo fifo { 1024 };

    // the audio thread's state
    bool audioRunning = false, hostWasPlaying = false;
    double phase = 0.0;                     // ticks (INTERNE out of the plug-in)
    double blockTime = -1.0;                // the smoothed time of the block's start (ms)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiClock)
};
