// REPLIK8 — RADIO (0.3.5) in the window: what the screen shows (what is heard as it scrolls by, the take cut into its
// pads), the strip under it (the sixteen pads, REC, GRAB, the take or a pad sent to the SAMPLER), and the tuner that
// takes the screen's place on the TUNE page (the stations' directory, the shortwave receivers and their frequency).

#pragma once

#include <JuceHeader.h>
#include "Look.h"
#include "Text.h"
#include "../plugin/PluginProcessor.h"
#include "../radio/RadioNet.h"

namespace te
{
    // a receiver's state as the screen says it
    juce::String radioStateText (int state);

    // the screen (the left of it): the radio as it goes by, and the take with its pads
    struct RadioScreen
    {
        const radio::Take* cachedTake = nullptr;
        juce::Path takeWave;
        float cachedW = 0.0f, cachedH = 0.0f;
        juce::Rectangle<float> cutArea;          // CUT's pattern (a click: another one)
        void paint (juce::Graphics&, juce::Rectangle<float>, ReplikantProcessor&, int selectedPad);
    };

    // under the screen: the pads, REC, GRAB, and what goes to the SAMPLER or to a DRUM track
    class RadioStrip : public juce::Component
    {
    public:
        RadioStrip (ReplikantProcessor&, std::function<void (const juce::String&, bool)> message);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int selected = -1;                       // the pad chosen (PAD -> SAMPLER sends it)

    private:
        ReplikantProcessor& proc;
        std::function<void (const juce::String&, bool)> message;
        juce::Rectangle<float> pads[radio::numPads], airKey, recKey, grabKey, takeKey, padKey, takeDrumKey, padDrumKey, takeChip, padChip;
        int held = -1;
        double lastPaint = 0.0;
        const radio::Take* cachedTake = nullptr;
        std::vector<std::vector<float>> padWaves;        // each pad's outline (a few dozen peaks)
        void layout();
    };

    // the TUNE page: the receiver listened to, tuned to a station or to a frequency on a shortwave receiver
    class RadioTuner : public juce::Component,
                       private juce::Timer,
                       private juce::ListBoxModel
    {
    public:
        RadioTuner (ReplikantProcessor&, std::function<void (const juce::String&, bool)> message);
        ~RadioTuner() override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void visibilityChanged() override;

    private:
        ReplikantProcessor& proc;
        std::function<void (const juce::String&, bool)> message;
        int view = 0;                            // 0 the stations, 1 the shortwave
        bool viewChosen = false;
        juce::TextEditor query, freq, address;   // (0.4.11) address: a receiver's own address, pasted
        juce::ListBox list;
        juce::Array<juce::var> stations, receivers;
        int asked = 0, waitingFor = -1;
        juce::String error;
        int lastVersion = -1, lastRx = -1;
        double swKhz = 9420.0;                   // the frequency and mode a receiver chosen in the list tunes to
        juce::String swMode { "am" };
        juce::Rectangle<float> tabWeb, tabSw, tabOff, searchKey, refreshKey, status;
        struct Chip { juce::Rectangle<float> r; juce::String label; juce::var what; };
        std::vector<Chip> chips;                 // the quick searches, the bands, the steps, the modes

        int rx() const;
        RadioNet::Source source() const;
        void tune (const RadioNet::Source&);
        void search (const juce::var& q);
        void askReceivers (bool fresh);
        void setFrequency (double khz, const juce::String& mode = {});
        void showView (int v);
        void tuneAddress (const juce::String&);
        void timerCallback() override;
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    };
}
