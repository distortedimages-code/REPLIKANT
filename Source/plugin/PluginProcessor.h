// REPLIKANT: the plug-in (AU, VST3) and the app. It records a synth (its audio in, MIDI out to it), turns the
// recordings into an instrument, and plays that instrument.

#pragma once

#include <JuceHeader.h>
#include "../engine/Instrument.h"
#include "../engine/Recorder.h"
#include "../engine/Worker.h"
#include "../engine/Player.h"
#include "../engine/TestSynth.h"
#include "Updater.h"
#include "MidiClock.h"

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
        int monitor = 1;                // the input: 0 not heard, 1 heard as it is, 2 through the effects (FX mode)
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
    void stopSampling();
    bool isSampling() const                          { return worker.isSampling() || recorder.isRunning(); }

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
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    std::unique_ptr<juce::PropertiesFile> globalSettings;
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
    std::atomic<int> monitorAudio { 1 };
    std::atomic<bool> thruAudio { false };
    juce::MidiMessageCollector toSynth, toPlayer;   // from the editor
    juce::MidiBuffer playMidi, outMidi, synthMidi;
    juce::AudioBuffer<float> synthBuffer, inputCopy;
    bool wasSampling = false;

    double sampleRate = 48000.0;
    bool clockStartedBySampling = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReplikantProcessor)
};
