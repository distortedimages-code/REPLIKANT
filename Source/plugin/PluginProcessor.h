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
        bool monitor = true;            // the input is heard
        bool thru = false;              // notes played into REPLIKANT go to the synth instead of the instrument
    };

    ReplikantProcessor();
    ~ReplikantProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }
    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return true; }
    double getTailLengthSeconds() const override             { return 2.0; }
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
    bool startSampling (juce::String& whyNot);
    void stopSampling();
    bool isSampling() const                          { return worker.isSampling() || recorder.isRunning(); }

    InstrumentPtr getInstrument() const;
    void newInstrument();
    void openInstrument (const juce::File&);
    void exportInstrument (bool bitwig, bool allKeys, const juce::File& target);
    juce::File instrumentsRoot() const;

    // MIDI to the synth
    juce::Array<juce::MidiDeviceInfo> midiOutputs() const;
    void setMidiPort (const juce::String& identifier, const juce::String& name);
    juce::String midiPortStatus() const;            // empty when fine
    void sendTestNote();

    // the editor's keyboard
    void audition (int note, int velocity, bool on);

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

    // MIDI to the synth
    std::unique_ptr<juce::MidiOutput> portOwner, retiredPort;
    std::atomic<juce::MidiOutput*> port { nullptr };
    std::atomic<bool> hostMidiOut { false };
    std::atomic<int> sourceAudio { 0 };      // Source, for the audio thread
    std::atomic<bool> monitorAudio { true }, thruAudio { false };
    juce::MidiMessageCollector toSynth, toPlayer;   // from the editor
    juce::MidiBuffer playMidi, outMidi, synthMidi;
    juce::AudioBuffer<float> synthBuffer;
    bool wasSampling = false;

    double sampleRate = 48000.0;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pVelocity = nullptr;
    std::atomic<float>* pBend = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReplikantProcessor)
};
