// The part of sampling that runs in the audio thread: it plays the notes (MIDI to the synth), listens to the
// input, and records everything into a ring. It decides when a note is over (its tail has faded into the noise
// floor, or the longest release is reached) and when the next can start (silence again, after the pause).
//
// It allocates nothing and takes no lock in the audio thread: a session's memory is made by start(), on the
// message thread; what happens is told to the worker through a lock-free queue of events, with the positions
// in the ring, and the worker copies the recordings out of the ring.

#pragma once

#include <JuceHeader.h>
#include "../core/Plan.h"

#include <array>
#include <atomic>
#include <memory>

class Recorder
{
public:
    struct MidiSink
    {
        virtual ~MidiSink() = default;
        virtual void send (const juce::MidiMessage&) = 0;
    };

    struct Event
    {
        enum Type { noiseDone, noteOn, noteOff, jobDone, finished, aborted };
        Type type;
        int job = -1;
        int64_t at = 0;          // position in the recording (samples since the session started)
        int64_t from = 0;        // noiseDone: where the measure began
    };

    struct Plan
    {
        std::vector<rk::Job> jobs;
        double hold = 3.0, releaseMax = 3.0, pause = 0.25;
        int channel = 1;         // 1..16
        int program = -1;        // program change sent first (-1 = none)
    };

    enum class Phase { idle, program, noise, gap, hold, release };

    Recorder();
    ~Recorder();

    // message thread
    bool start (const Plan&, double sampleRate, int numChannels);   // false if a session is running
    void stop();                                                       // ends the session (note-off, all notes off)
    bool isRunning() const                 { return running.load(); }
    Phase getPhase() const                 { return (Phase) phaseShown.load(); }
    int getJob() const                     { return jobShown.load(); }
    float getMeter() const                 { return meter.load(); }   // input peak, decaying
    float getFloorDb() const               { return floorDb.load(); }
    int getNumChannels() const;

    // worker thread
    int popEvents (Event* dest, int max);
    // copies [from, to) of the recording; false when part of it was overwritten (or not written yet)
    bool copy (int64_t from, int64_t to, std::vector<std::vector<float>>& dest) const;
    int64_t getWritten() const             { return written.load (std::memory_order_acquire); }

    // audio thread: once per block, before capture (sends what is due at the start of the block) ...
    void beginBlock (MidiSink&);
    // ... then the block of input
    void capture (const float* const* input, int numChannels, int numSamples);

private:
    struct Run
    {
        Plan plan;
        double sampleRate = 48000.0;
        int channels = 2;
        int64_t mask = 0;
        std::vector<std::vector<float>> ring;
        int holdS = 0, releaseMaxS = 0, pauseS = 0, noiseS = 0, programS = 0, maxGapS = 0, quietS = 0, minReleaseS = 0;
    };

    void push (Event::Type, int job, int64_t at, int64_t from = 0);
    void enter (Phase);
    void finish (bool aborted, MidiSink&);

    std::shared_ptr<Run> keep[2];               // the current run and the one before (the audio thread may still read it)
    std::atomic<Run*> incoming { nullptr };
    Run* run = nullptr;                          // audio thread's

    // audio thread state
    Phase phase = Phase::idle;
    int job = 0;
    int64_t phaseSamples = 0;
    int64_t quietSamples = 0;
    float env = 0.0f, envDecay = 0.999f, silence = 1.0e-5f;
    double noiseSum = 0.0;
    int64_t noiseCount = 0, noiseFrom = 0;
    bool noteDown = false;
    int noteNumber = 0;

    std::atomic<bool> running { false }, abortRequested { false };
    std::atomic<int64_t> written { 0 };
    std::atomic<int> phaseShown { 0 }, jobShown { -1 };
    std::atomic<float> meter { 0.0f }, floorDb { -200.0f };

    juce::AbstractFifo fifo { 1024 };
    std::array<Event, 1024> events;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};
