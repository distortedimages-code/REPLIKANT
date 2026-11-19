// CAPTURE's assistant (0.4.8): step by step, from the DAW REPLIK8 runs in to a test that says it is ready.
//
//   LOGICIEL      the host, recognised (Live, Logic, Bitwig, Reaper, Cubase, Studio One), or chosen
//   SOURCE        a plugin in that host, a hardware synth, or the test synth
//   MIDI          (a plugin) the Mac's IAC bus: found, or how to switch it on, watched until it appears
//   BRANCHEMENTS  (hardware) the MIDI port and the audio input, by the names the Mac gives them
//   PISTE         what to set in the host: the track, its input, its monitoring, where REPLIK8 goes
//   TEST          three notes sent, their way back timed: the level, a loop, the latency (and CYCLES' CC)
//
// REPLIK8's own capture settings (source, MIDI out, channel, monitoring) are set as the steps go.

#pragma once

#include <JuceHeader.h>
#include "Look.h"
#include "../plugin/PluginProcessor.h"

namespace te
{
    class CaptureAssistant : public juce::Component, private juce::Timer
    {
    public:
        explicit CaptureAssistant (ReplikantProcessor&);
        ~CaptureAssistant() override;

        void open (int kind);                           // 0 NOTES, 1 CYCLES
        void close();
        bool isOpen() const                             { return isVisible(); }

        std::function<void()> onClose;
        std::function<void()> onStart;                  // the capture launched from the last step
        std::function<void()> onSettingsChanged;        // REPLIK8's capture settings changed here
        std::function<void (const juce::String&, bool)> say;

        // opened by itself when NOTES or CYCLES is chosen (SETTINGS › ASSISTANT)
        static bool automatic (ReplikantProcessor&);
        static void setAutomatic (ReplikantProcessor&, bool);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        // (the snapshots: a step shown at once, the clock moved by hand)
        void showStepNamed (const juce::String& name);
        void chooseForShot (int daw, int target);
        void tick (double seconds);
        void runTestForShot();
        void fakeForShot (int daw, const juce::String& hostName, const juce::String& format, const juce::String& track, bool devices);

        enum class Daw { live, logic, bitwig, reaper, cubase, studioOne, other };
        enum class Target { none, plugin, hardware, test };
        enum class Step { daw, target, iac, wiring, track, test };

    private:
        void timerCallback() override;

        ReplikantProcessor& proc;
        int kind = 0;
        ReplikantProcessor::HostInfo host;
        bool inApp = false;
        int daw = -1;                       // Daw, -1 not chosen
        bool dawDetected = false;
        Target target = Target::none;
        std::vector<Step> steps;
        int at = 0;
        void rebuildSteps();
        void go (int step);
        bool canGoOn() const;
        Step stepNow() const                            { return steps.empty() ? Step::daw : steps[(size_t) juce::jlimit (0, (int) steps.size() - 1, at)]; }

        // what the Mac has: the MIDI outputs (the IAC bus among them), its audio inputs
        juce::Array<juce::MidiDeviceInfo> midiOuts;
        juce::MidiDeviceInfo iac;
        bool iacFound = false;
        double iacFoundAt = -10.0;
        ReplikantProcessor::AudioIns ins;
        juce::String inDevice;
        int inFirst = 0;
        bool inStereo = true;
        double lastScan = -10.0;
        void scan (bool audioToo);
        juce::String hwPort;                 // (the hardware's MIDI port, its identifier)

        // REPLIK8's settings, as the steps set them
        void applyPlugin();
        void applyHardware();
        void applyTest();
        void setChannel (int);
        void applyInput();
        double appliedAt = -10.0;
        juce::String applied;                // what was just set (shown a moment)

        // the test
        enum class Test { idle, notes, cc, done };
        Test test = Test::idle;
        double testFrom = 0.0;
        ReplikantProcessor::Probe notesResult, ccResult;
        bool ccWanted() const;
        bool retried = false;
        bool ready = false;
        double readyAt = -10.0, doneAt = -10.0;
        void startTest();
        void testTick();
        void finishTest();
        juce::String fixNote;                // what the test changed by itself
        // the meters
        float level[2] {}, hold[2] {};
        double holdAt[2] {};
        float mainLed = 0.0f, sideLed = 0.0f;
        bool echoSeen = false;

        // drawing
        double now = 0.0, openedAt = 0.0, movedAt = -10.0;
        int moveDir = 1;
        float railShown = 0.0f;
        juce::Point<float> mouse { -1.0f, -1.0f };
        struct Hit { juce::Rectangle<float> r; std::function<void()> click; };
        std::vector<Hit> hits;
        bool hot (juce::Rectangle<float> r) const       { return r.contains (mouse); }
        void hit (juce::Rectangle<float> r, std::function<void()> f)   { hits.push_back ({ r, std::move (f) }); }
        bool manualClock = false, fakeMac = false;

        void paintHeader (juce::Graphics&, juce::Rectangle<float>);
        void paintFooter (juce::Graphics&, juce::Rectangle<float>);
        void paintStep (juce::Graphics&, juce::Rectangle<float> left, juce::Rectangle<float> right);
        void paintDaw (juce::Graphics&, juce::Rectangle<float>, juce::Rectangle<float>);
        void paintTarget (juce::Graphics&, juce::Rectangle<float>, juce::Rectangle<float>);
        void paintIac (juce::Graphics&, juce::Rectangle<float>, juce::Rectangle<float>);
        void paintWiring (juce::Graphics&, juce::Rectangle<float>, juce::Rectangle<float>);
        void paintTrack (juce::Graphics&, juce::Rectangle<float>, juce::Rectangle<float>);
        void paintTest (juce::Graphics&, juce::Rectangle<float>, juce::Rectangle<float>);
        void paintFlow (juce::Graphics&, juce::Rectangle<float>, bool midiLit, bool audioLit);
        void paintTrackMock (juce::Graphics&, juce::Rectangle<float>);
        float lines (juce::Graphics&, juce::Rectangle<float>, const juce::StringArray&, int done = 0);

        juce::String dawName (int d = -2) const;
        juce::String iacName() const;
        juce::String inputName (bool forHost) const;
        juce::String portName() const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureAssistant)
    };
}
