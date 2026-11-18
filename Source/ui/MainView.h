// The whole window, at one size (ui::designWidth x ui::designHeight), zoomed by the editor.
//
//   top      the mascot, the name of the instrument, new / open / export / the pack to share, the zoom, the update
//   left     SAMPLER or PLAYER, then their pages:
//              SAMPLER  ENREGISTREMENT (source, notes, velocities, timing), TRAITEMENT (loops, attack, level,
//                       tuning, files), TRANSPOSITION (the keys in between)
//              PLAYER   SON, LECTURE, FILTRE, SPECTRE, ESPACE, MODS, SCÈNES
//   right    the keyboard map; under it the sample (SAMPLER, with its tuning by hand), or what the player's page
//            shows (its waveform and grains, the filter, the chain, the modulators, the scenes)
//   bottom   SAMPLER: the big SAMPLE button, what is happening, the size it will take; PLAYER: M1..M4, the
//            crossfader; both: the MIDI clock, the level
//
// A pack (zip), a .replikant or an instrument folder dropped on the window is brought in.

#pragma once

#include <JuceHeader.h>
#include "../plugin/PluginProcessor.h"
#include "Widgets.h"
#include "KeyMap.h"
#include "WaveView.h"
#include "UpdatePanel.h"
#include "PlayerViews.h"

class MainView : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer,
                 private juce::ChangeListener
{
public:
    explicit MainView (ReplikantProcessor&);
    ~MainView() override;

    std::function<void (float)> onZoom;
    float getZoom() const                     { return zoom; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override       { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override                  { dropping = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    // for the snapshots: show a mode and a page
    void showPage (int mode, int page);

private:
    struct Group
    {
        juce::String title;
        int mode, page;
        std::vector<ui::ParamRow*> rows;
        juce::Rectangle<int> bounds, band;
        int toggle = -1;                  // a sound setting the title band turns on and off (the effects)
    };

    // the pack to share: click to make it, then drag it out (to the Finder, AirDrop, a mail)
    class PackTile : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit PackTile (MainView& v) : view (v) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        juce::File ready;
        juce::String readyFor;            // the instrument and sound it was made of
        bool making = false;
    private:
        MainView& view;
        bool dragStarted = false;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void buildSamplerRows();
    void buildPlayerRows();
    Group& group (const juce::String& title, int mode, int page, int toggle = -1);
    ui::ParamRow& add (Group&, std::unique_ptr<ui::ParamRow>, bool mapping = false);
    ui::ParamRow& sound (Group&, int id, std::function<bool()> shown = nullptr, const char* label = nullptr);
    juce::Colour pageColour (int mode, int page) const;
    void refreshMidiChoices();
    void refreshAll();
    void selectSample (int);
    void step (int dir);
    void playShown();
    void toggleSampling();
    void showExportMenu();
    void openInstrument();
    void showZoomMenu();
    void showMessage (const juce::String&, bool warning = false);
    void refreshUpdate();
    void maybeShowUpdate();
    void armChanged();
    juce::String packSignature() const;

    ReplikantProcessor& proc;
    ui::Arming arm;
    KeyMap keymap { proc };
    WaveView wave;
    ui::FilterView filterView { proc };
    ui::ChainView chainView { proc };
    ui::ModView modView { proc, arm };
    ui::SceneView sceneView { proc, arm };
    ui::PlayerStrip strip { proc, arm };
    ui::PlayerStrip clockStrip { proc, arm };              // the clock, in both modes
    UpdatePanel updatePanel { proc.updater };
    PackTile packTile { *this };

    std::vector<std::unique_ptr<ui::ParamRow>> rows;
    std::vector<Group> groups;
    ui::ParamRow* midiRow = nullptr;
    ui::ParamRow* clockRow = nullptr;
    std::unique_ptr<ui::ParamRow> tuneNote, tuneCents;      // the sample shown, retuned by hand
    int tuneNoteValue = -1;
    double tuneCentsValue = 0.0;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    int mode = 0;
    int page[2] = { 0, 0 };
    int modShown = 0;                                        // MODS: the modulator whose settings are shown
    juce::TextButton modeButtons[2];
    juce::OwnedArray<juce::TextButton> pageButtons;
    juce::TextButton modTabs[snd::numMods];

    juce::TextEditor nameEditor;
    juce::TextButton newButton { "Nouveau" }, openButton { "Ouvrir" }, exportButton { "Exporter" },
                     audioButton { "Carte son" }, zoomButton, updateButton;
    juce::TextButton sampleButton, testButton { "Test MIDI" }, prevButton { "<" }, nextButton { ">" }, playButton;
    std::unique_ptr<juce::FileChooser> chooser;

    Worker::Status status;
    InstrumentPtr shown;
    int selected = -1;
    juce::String message;
    bool messageIsWarning = false;
    double messageUntil = 0.0;
    float zoom = 1.0f;
    bool dropping = false;
    juce::Rectangle<int> header, left, mapCard, waveCard, bottom, statusArea, meterArea, banner;
    int lastMeterShown = -100000, lastVoicesShown = -1;     // what the bottom bar last drew (repainted on change)
    std::optional<Updater::News> pendingNews;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
