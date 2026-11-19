// REPLIKANT: the plug-in (AU, VST3) and the app. It records a synth (its audio in, MIDI out to it), turns the
// recordings into an instrument, and plays that instrument.

#pragma once

#include <JuceHeader.h>
#include <array>
#include "../engine/Instrument.h"
#include "../engine/Recorder.h"
#include "../engine/Worker.h"
#include "../engine/Listener.h"
#include "../engine/Player.h"
#include "../engine/TestSynth.h"
#include "../radio/RadioCore.h"
#include "../drum/DrumCore.h"
#include "Updater.h"
#include "MidiClock.h"

class RadioNet;

class ReplikantProcessor : public juce::AudioProcessor,
                           public juce::ChangeBroadcaster,
                           private juce::Timer
{
public:
    enum class Source { input, sidechain, testSynth };

    struct Options
    {
        Source source = Source::input;
        juce::String midiPort;          // identifier of the MIDI output to the synth; "host" = the plug-in's MIDI out
        juce::String midiPortName;
        int channel = 1;
        int program = -1;
        int monitor = 0;                // the input: 0 not heard (0.3.6: the default), 1 heard as it is, 2 through the effects (FX mode)
        bool thru = false;              // notes played into REPLIKANT go to the synth instead of the instrument
    };

    ReplikantProcessor();
    ~ReplikantProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }
    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return true; }
    double getTailLengthSeconds() const override             { return 8.0; }   // (ESPACE rings on)
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //========================================================================== message thread
    rk::Session session;
    Options options;
    juce::String instrumentName;

    void settingsChanged();                         // session or options edited: estimate, state, maybe re-render
    void mappingChanged();                          // a setting of the current instrument's mapping or rendering
    // the note a recording plays and its tuning, set by hand (for all the takes of the note sent with it): applied
    // once it stops moving, the zones and the keys made again
    void retuneSample (int sampleIndex, int root, double cents);
    bool startSampling (juce::String& whyNot);
    // CAPTURE CYCLES: one note held again and again while something of the synth moves (a CC swept, the velocity,
    // or a knob by hand); a cycle from each take, the bank kept with the instrument (the SYNTHÉ's CAPTURÉS)
    struct CycleOptions
    {
        int note = 36;               // low: more points to a period
        int count = 1;               // 8, 16, 32, 64 takes
        int vary = 0;                // 0 a CC swept, 1 the velocity, 2 by hand (nothing sent), 3 LISTEN (one take, cut)
        double listenDb = -40.0;     // LISTEN: what starts the take
        int cc = 74, ccFrom = 0, ccTo = 127;
        double hold = 0.5, settle = 0.2;    // each take held; the pause before it (the synth settles)
        static int countOf (int c)        { const int n[] = { 8, 16, 32, 64 }; return n[juce::jlimit (0, 3, c)]; }
    } cycleOptions;
    bool startCycles (juce::String& whyNot);
    bool lastCaptureWasCycles = false;
    void stopSampling();
    bool isSampling() const                          { return worker.isSampling() || recorder.isRunning() || listener.busy(); }
    CycleListener listener;                          // CAPTURE CYCLES, LISTEN

    InstrumentPtr getInstrument() const;
    void newInstrument();
    void openInstrument (const juce::File&, bool withItsSound = true);
    // a pack (zip), a .replikant or an instrument folder: brought in (a pack is unpacked into the instruments' folder)
    void importAny (const juce::File&);
    // the pack of the current instrument and its sound, made (worker) into the Packs folder; `done` gets it (or an
    // empty file) on the message thread
    void buildPack (std::function<void (const juce::File&)> done);
    juce::File packFor (const Instrument&) const;
    juce::String soundSignature() const;            // changes when the sound does
    void exportInstrument (bool bitwig, bool allKeys, const juce::File& target);
    juce::File instrumentsRoot() const;

    // MIDI to the synth
    juce::Array<juce::MidiDeviceInfo> midiOutputs() const;
    void setMidiPort (const juce::String& identifier, const juce::String& name);
    juce::String midiPortStatus() const;            // empty when fine
    void sendTestNote();

    // the editor's keyboard
    void audition (int note, int velocity, bool on);

    // RÉÉCHANTILLONNER: what comes out of the effects recorded, then injected into the SAMPLER as a looped instrument
    // without a gap (the Player loops it meanwhile); the effects set to zero so nothing is processed twice
    struct ResampleOptions
    {
        int length = 1;              // 1, 2, 4, 8 bars, or free (until stopped)
        bool synced = true;          // from the next bar when the transport plays
        int root = 60;               // the key it is played on as recorded
        bool resetFx = true;         // filter, drive, effects and WTF?! to zero
        static int barsOf (int length)    { const int b[] = { 1, 2, 4, 8, 0 }; return b[juce::jlimit (0, 4, length)]; }
    } resampleOptions;
    bool startResample (juce::String& whyNot);
    void stopResample()                              { player.stopResample(); }
    juce::String lastResample;                       // the instrument it made last

    // RADIO (radio/RadioCore.h): the receivers, the listening and its air, what was heard, the takes and their pads.
    // The network (RadioNet) is made the first time it's needed (the tab opened, a project with receivers tuned).
    radio::Engine radio;
    RadioNet& radioNet();
    bool hasRadioNet() const                         { return net != nullptr; }
    // the pads' take (message thread): what GRAB or REC made, cut at its attacks
    std::shared_ptr<const radio::Take> radioTake() const    { return takeNow; }
    bool radioSlicing() const                        { return slicing.load() > 0; }
    bool radioGrab (juce::String& why);              // the last TAKE seconds of what was heard
    bool radioRec (juce::String& why);               // REC, then STOP: what was heard between them
    void radioReslice();                             // the take cut again (SENSITIVITY, PADS changed)
    bool radioToSampler (int pad, juce::String& why);      // the take (pad < 0) or one pad, played by the SAMPLER
    void radioClear();
    juce::String radioStationName (int rx) const;    // what a receiver is tuned to (its station, its frequency)

    // DRUM (drum/DrumCore.h): eight tracks and their sequencer. Their sounds (a sample, a radio take and its slices)
    // are kept here as WAV files beside the instruments (Drums/), and with the project.
    drum::Engine drum;
    void drumReady();                                // (the patterns made, the first time they're wanted)
    std::atomic<int> drumTrack { 0 };                // the track shown (where RADIO's -> DRUM puts a take)
    std::shared_ptr<const drum::Sound> drumSound (int track) const;
    bool radioToDrum (int pad, int track, juce::String& why);    // the take (pad < 0: its slices) or a pad onto a track
    bool drumLoadFile (int track, const juce::File&, juce::String& why);
    void drumClearSound (int track);

    // the MIDI clock
    MidiClock clock;
    bool clockWithSampling = false;               // a sampling session starts and stops it
    void setClockDestination (const juce::String& id, const juce::String& name);
    void setClockMode (MidiClock::Mode);
    void setClockRunning (bool);

    // the sound (the player's settings, routes and steps), as a value (saved with the instrument, in the project)
    juce::var soundToVar() const;
    void soundFromVar (const juce::var&);
    void resetSound();
    void setSoundParam (int id, float value);         // a setting of the sound, as the host sees it

    Worker::Status status() const                    { return worker.getStatus(); }
    float inputLevel() const                         { return recorder.getMeter(); }
    Recorder::Phase phase() const                    { return recorder.getPhase(); }
    int currentJob() const                           { return recorder.getJob(); }
    double currentSampleRate() const                 { return sampleRate; }

    juce::AudioProcessorValueTreeState params;
    Player player;
    Updater updater;
    juce::PropertiesFile& settingsFile()             { return *globalSettings; }
    std::function<void (const juce::String&)> onMessage;   // something to tell (editor)

private:
    struct Sink;
    void timerCallback() override;
    void listenTick();
    bool listenShown = false;
    void publish (std::shared_ptr<Instrument>);
    void collectOldInstruments();
    void loadGlobalSettings();
    juce::var modsToVar() const;
    void modsFromVar (const juce::var&);
    juce::var scenesToVar() const;
    juce::var fxOrderToVar() const;
    void fxOrderFromVar (const juce::var&);
    void scenesFromVar (const juce::var&);
    void saveGlobalSettings();
    void resampleTick();                           // the recording ready: the instrument made, the sound matched
    void injectResample (rk::Channels);
    int releaseNeutralIn = 0;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    std::unique_ptr<juce::PropertiesFile> globalSettings;
    // what the worker hands back to the message thread waits there; the processor may be gone by then (a project
    // closed while an instrument loads): it runs only while this says the processor is alive
    std::shared_ptr<std::atomic<bool>> lifeline = std::make_shared<std::atomic<bool>> (true);
    void later (std::function<void()>);
    Recorder recorder;
    Worker worker { recorder };
    TestSynth testSynth;

    // instruments: the current one (message thread), the one handed to the audio thread, and all still alive
    mutable juce::SpinLock instrumentLock;
    std::shared_ptr<Instrument> current;
    const Instrument* pending = nullptr;
    std::atomic<bool> hasPending { false };
    std::atomic<const Instrument*> audioUses { nullptr };
    std::vector<std::shared_ptr<Instrument>> alive;
    bool mappingDirty = false;
    int mappingCountdown = 0;
    struct Retune { int index = -1, root = 60; double cents = 0.0; int countdown = 0; } retune;
    juce::String savedSound;                       // the sound last written into the instrument's folder
    juce::String pendingSound;
    int soundCountdown = 0;

    // MIDI to the synth
    std::unique_ptr<juce::MidiOutput> portOwner, retiredPort;
    std::atomic<juce::MidiOutput*> port { nullptr };
    std::atomic<bool> hostMidiOut { false };
    std::atomic<int> sourceAudio { 0 };      // Source, for the audio thread
    std::atomic<int> monitorAudio { 0 };
public:
    // the capture page open in the editor (it says so at each tick): only then, or while capturing, is the input
    // heard as it is (ÉCOUTE : DIRECT). Playing, the output is REPLIK8 alone (see processBlock)
    std::atomic<bool> captureOpen { false };
private:
    std::atomic<bool> thruAudio { false };
    juce::MidiMessageCollector toSynth, toPlayer;   // from the editor
    juce::MidiBuffer playMidi, outMidi, synthMidi;
    juce::AudioBuffer<float> synthBuffer, inputCopy, silence, followCopy;
    bool wasSampling = false;
    // the start of what went out over the last blocks: an input that is that again is the host handing our own
    // output back (see processBlock)
    static constexpr int headLength = 64;
    struct Head { float x[2][headLength] {}; int n = 0, channels = 0; bool valid = false, silent = true; };
    std::array<Head, 4> sentHeads;
    int sentNext = 0;
    void rememberOutput (const juce::AudioBuffer<float>&, int n);
    bool isOwnOutput (const juce::AudioBuffer<float>&, int n, bool scaledToo) const;

    double sampleRate = 48000.0;
    bool clockStartedBySampling = false;

    // RADIO: the network, the takes kept alive while the audio may play them, the take now and where it's kept
    std::unique_ptr<RadioNet> net;
    juce::var pendingSources;                        // (a project's receivers, before the network is made)
    struct KeptTake { std::shared_ptr<const radio::Take> take; double retiredAt = 0.0; };
    std::vector<KeptTake> takes;
    std::shared_ptr<const radio::Take> takeNow;
    juce::File takeFile;
    float slicedWith = -1.0f; int slicedPads = -1;
    double sliceChangedAt = 0.0;
    std::atomic<int> slicing { 0 };
    std::unique_ptr<juce::ThreadPool> radioPool;
    juce::AudioBuffer<float> radioBuffer;
    std::vector<radio::Engine::Event> radioEvents;
    juce::MidiBuffer noMidi;
    juce::CriticalSection radioLock;
    void useTake (std::shared_ptr<const radio::Take::Audio>, const juce::String& name, bool save);
    void radioTick();
    juce::File radioFolder() const;

    // DRUM: each track's sound (and where it's kept), the ones the audio may still play, the banks of cycles
    struct DrumSlot { std::shared_ptr<const drum::Sound> sound; juce::String path; int from = 0, to = 0; };
    std::array<DrumSlot, drum::numTracks> drumSlots;
    struct KeptSound { std::shared_ptr<const drum::Sound> sound; double retiredAt = 0.0; };
    std::vector<KeptSound> drumKept;
    std::shared_ptr<const dsp::CycleBank> drumBasic, drumCaptured, drumCapturedBefore;
    double drumCapturedSince = 0.0;
    double drumPpq = 0.0;
    bool drumClockWas = false;
    void useDrumSound (int track, std::shared_ptr<const radio::Take::Audio>, std::vector<radio::Slice>, const juce::String& name,
                       const juce::String& path, int from, int to);
    void drumTick();
    juce::var drumToVar() const;
    void drumFromVar (const juce::var&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReplikantProcessor)
};
