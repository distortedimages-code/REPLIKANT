// The whole window (0.3), after the mockup A · MACHINE: a black screen, eight page keys, eight encoders, and below
// the keyboard (or the capture's keys), the mods and scenes, WTF?!.
//
//   SYNTHÉ    the oscillators of cycles, played at once
//   DRUM      (0.4) eight tracks (a sample, a radio take's slices, a voice of the synth's cycles) and their sequencer
//   SAMPLER   the instrument (captured, or a sound dropped on the window)
//   RADIO     (0.3.5) the radio: a station or a shortwave receiver, its air, the takes cut into pads
//   CAPTURE   over any mode: the synth sampled (notes), its cycles, or what comes out of the effects (resampling)
//
// Each page holds one or more banks of up to eight controls; pressing the page's key again goes to its next bank.

#pragma once

#include <JuceHeader.h>
#include "Controls.h"
#include "UpdatePanel.h"

namespace te
{
    struct DrumEdit;
    class CaptureAssistant;

    // a key of the body (a button drawn with the look)
    class Key : public juce::Button
    {
    public:
        explicit Key (const juce::String& label = {}) : juce::Button (label) {}
        std::function<KeyState()> look;            // how it looks now (else: on when toggled)
        juce::Colour dot = juce::Colours::transparentBlack;
        Picto icon = Picto::none;                  // drawn before the label
        float fontSize = 12.5f;
        bool selector = false;                     // (0.4.19) a mode selector: a liseret inside, the type in bold
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    class Machine : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer,
                    private juce::ChangeListener
    {
    public:
        explicit Machine (ReplikantProcessor&);
        ~Machine() override;

        float getZoom() const                      { return zoom; }
        std::function<void (float)> onZoom;

        enum Mode { synthMode = 0, drumMode = 1, samplerMode = 2, radioMode = 3, weaveMode = 4 };
        static constexpr int numModes = 5;
        // (0.4.20) the header's keys: WEAVE is reached by SAMPLER's (the screen switches between the two)
        static constexpr int numModeKeys = 4;
        int samplerSide = samplerMode;             // where SAMPLER's key goes: the sampler or WEAVE (the last one shown)
        void openWeave();                          // a few sounds chosen for WEAVE
        void showMode (int mode);                  // sets the engine (what plays) and shows its pages
        void showCapture (bool);
        enum CaptureKind { notesKind = 0, cyclesKind = 1, resampleKind = 2 };
        void setCaptureKind (int);
        int captureKindShown() const               { return captureKind; }
        void showPage (int page);                  // in the mode shown; the same page again: its next bank
        int modeShown() const                      { return mode; }
        bool captureShown() const                  { return capture; }
        void touch (const Ctl&);                   // the screen's readout follows
        void setKeyStyle (int style);              // CLASSIC, PADS, FLAT
        void setArmedMod (int mod);                // a mod armed (-1: none), as its key does
        void openAssistant();                      // CAPTURE's assistant, over the screen and the encoders (0.4.8)
        CaptureAssistant* captureAssistant()       { return assistant.get(); }

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override              { dropping = false; repaint(); }
        void filesDropped (const juce::StringArray&, int, int) override;

        // for the snapshots: what a timer tick would refresh; a bank of the page shown
        void refreshNow()                          { timerCallback(); }
        void showBank (int bank);
        void drumLift (std::vector<int> steps);    // (DRUM: steps lifted for p-locks)
        void drumShowPatterns (bool);              // (DRUM: the patterns on the strip)

        // (0.4.16) the desk's silkscreen for a bank: liserets round controls that go together (their places in the
        // bank, from .. to, and a name printed in the line), arrows where the sound goes from one to the next (after
        // the place given), and where it all goes on to, printed after the last control
        struct Frame { int from = 0, to = 0; juce::String label; Picto picto = Picto::none; };
        struct Bank
        {
            juce::String name;
            std::vector<Ctl> ctls;
            std::vector<Frame> frames = {};
            std::vector<int> arrows = {};
            juce::String exit = {};
            Picto exitPicto = Picto::none;
            std::function<bool()> when = {};     // (0.4.17) shown only when (DRUM: a synth's banks, a sound's)
            bool shown() const                    { return ! when || when(); }
        };
        struct Page { juce::String name; juce::String viz; std::vector<Bank> banks; };

    private:
        friend class Screen;
        friend class Keyboard;
        friend class ModScenes;
        friend class WtfPanel;
        friend class CaptureStrip;

        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void buildPages();
        void buildCapturePages();
        std::vector<Page>& pagesShown();
        int& pageIndex();
        void layoutEncoders();
        void armChanged();
        void showMessage (const juce::String&, bool warning = false);
        void showFileMenu();
        void openInstrument();
        void exportMenu (juce::PopupMenu&);
        void toggleSampling();
        void renameInstrument();
        void refreshUpdate();
        void maybeShowUpdate();
        juce::String packSignature() const;

        ReplikantProcessor& proc;
        Arming arm;
        int mode = synthMode;
        bool capture = false;
        std::vector<Page> pages[numModes], capturePages, resamplePages, cyclePages;
        int page[numModes] {}, capturePage = 0, resamplePage = 0, cyclePage = 0;
        void buildCyclePages();
        int captureKind = notesKind;
        int keyStyle = 0;                          // the keyboard's look: CLASSIC, PADS, FLAT
        juce::String shownResample;                // the last instrument RÉÉCHANTILLONNER made, as shown
        void buildResamplePages();
        void toggleResample();
        std::map<juce::String, int> bankOf;        // page name (per mode) -> bank shown
        const Ctl* focus = nullptr;
        juce::String focusName;
        float zoom = 1.0f;
        bool dropping = false;
        int dropSounds = 0;                        // (0.4.20) the sounds among the files dragged over
        bool wasSampling = false;
        juce::String message;
        bool messageWarning = false;
        double messageUntil = 0.0;
        Worker::Status status;
        juce::Array<juce::MidiDeviceInfo> midiDevices;

        // the header
        juce::Rectangle<int> header, logo, presetBox, bpmBox, meterBox;
        Key modeKeys[numModeKeys], captureKey, playKey, fileKey, packKey, updateKey;
        juce::TextEditor nameEditor;
        // the pack: made on a click, dragged out once ready
        juce::File packReady;
        juce::String packFor;
        bool packMaking = false, packDragging = false;
        struct PackDrag : juce::MouseListener
        {
            Machine& m;
            explicit PackDrag (Machine& mm) : m (mm) {}
            void mouseDrag (const juce::MouseEvent&) override;
        } packDrag { *this };

        std::unique_ptr<juce::Component> screen;
        static constexpr int numPageKeys = 10;
        Key pageKeys[numPageKeys];
        std::unique_ptr<Encoder> encoders[8];
        int slotCtl[8] { -1, -1, -1, -1, -1, -1, -1, -1 };   // (0.4.16) each encoder's control: its place in the bank shown
        const Bank* bankShown = nullptr;
        void placeEncoders();                      // their widths: a switch narrower, next to what it switches
        void paintSilkscreen (juce::Graphics&);
        std::unique_ptr<juce::Component> keyboard, modScenes, wtfPanel, captureStrip;
        std::unique_ptr<juce::Component> radioStrip, radioTuner;   // (RADIO: the pads in the keyboard's place, the tuner over the screen)
        std::unique_ptr<juce::Component> drumStrip;                // (DRUM: its tracks and steps in the keyboard's place)
        std::unique_ptr<DrumEdit> drumEdit;                         // (what the drum's screen, strip and encoders edit)
        std::unique_ptr<CaptureAssistant> assistant;
        bool assistantSeen = false;                                 // (opened by itself once, entering CAPTURE)
        void captureSettingsChanged();                              // the assistant set the source, the port...
        bool tunerPage() const;
        int radioPad() const;
        juce::Rectangle<int> screenArea, keysArea, encArea, bottomArea, deckArea, inMeter, outMeter;

        UpdatePanel updatePanel;
        std::optional<Updater::News> pendingNews;
        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Machine)
    };
}
