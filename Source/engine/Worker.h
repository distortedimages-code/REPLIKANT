// Everything slow, away from the audio and the editor: following a sampling session (copying each note out of
// the recorder, cleaning it, finding its loop), then building the instrument (normalised, mapped), saving it,
// and rendering its keys; also loading, re-rendering after a change, exporting.

#pragma once

#include <JuceHeader.h>
#include "Instrument.h"
#include "Recorder.h"
#include "../core/Capture.h"

#include <deque>
#include <functional>
#include <mutex>

class Worker : private juce::Thread
{
public:
    struct Status
    {
        bool sampling = false, busy = false;
        juce::String text;                    // what is happening, in a few words
        juce::String warning;                 // a problem worth telling (stays until the next session)
        float progress = -1.0f;               // 0..1, < 0 = none
        int done = 0, total = 0;              // notes recorded / planned
        double secondsLeft = 0.0;
        std::vector<rk::Job> recorded;        // the notes recorded so far in this session
        std::shared_ptr<const rk::Sample> last;   // the last one, to show it
        int latency = -1;                     // samples, measured
        float floorDb = -200.0f;
    };

    explicit Worker (Recorder&);
    ~Worker() override;

    // called (on the worker thread) with each new version of the instrument
    std::function<void (std::shared_ptr<Instrument>)> onInstrument;
    // called (on the worker thread) when the status changed
    std::function<void()> onStatus;

    // message thread
    bool startSession (const juce::String& name, const rk::Session&, const Recorder::Plan&, double sampleRate,
                       int channels, const juce::File& folder);
    void stopSession();
    // renders the keys of `ins` (as its session says) and publishes it, first as it is, then with its keys
    void renderAndPublish (std::shared_ptr<Instrument> ins, bool publishFirst = true);
    // runs a task on the worker thread (load, export...), with a label for the status
    void post (const juce::String& label, std::function<void()> task);

    Status getStatus() const;
    bool isSampling() const;

    static juce::File uniqueFolder (const juce::File& root, const juce::String& name);
    // renders the keys of an instrument in place; `progress` gets 0..1; stops early when `cancelled` says so
    static void renderKeys (Instrument&, const std::function<bool()>& cancelled, const std::function<void (float)>& progress);

private:
    void run() override;
    void drain();
    void processJob (int job, int64_t noteOn, int64_t noteOff, int64_t end);
    void finishSession (bool aborted);
    void setStatus (const std::function<void (Status&)>&);

    Recorder& recorder;
    mutable std::mutex statusLock;
    Status status;

    std::mutex taskLock;
    std::deque<std::pair<juce::String, std::function<void()>>> tasks;
    juce::WaitableEvent wake;
    std::atomic<int> renderGeneration { 0 };     // a new render (or session) makes the running one stop

    // the session being recorded (worker thread)
    struct Raw
    {
        rk::Channels audio;
        int noteOn = 0, noteOff = 0;
        rk::Job job {};
    };
    struct Session
    {
        bool active = false;
        juce::String name;
        rk::Session settings;
        Recorder::Plan plan;
        double sampleRate = 48000.0;
        juce::File folder;
        rk::NoiseFloor noise;
        std::vector<rk::Sample> samples;
        std::vector<Raw> raws;            // kept to realign everything on one latency at the end
        std::vector<int> latencies;
        int64_t noteOn = -1, noteOff = -1;
        int silentInARow = 0;
        double startedAt = 0.0;
    } session;
    std::atomic<bool> sampling { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Worker)
};
