// The whole window, at one size (ui::designWidth x ui::designHeight), zoomed by the editor.
//
//   top      the name of the instrument, new / open / export, the zoom, the update
//   left     the settings, on three pages: RECORDING (source, notes, velocities, timing),
//            PROCESSING (loops, attack, level), TRANSPOSITION (the keys in between, and playing)
//   right    the keyboard map, the sample shown
//   bottom   the big SAMPLE button, what is happening, the input level

#pragma once

#include <JuceHeader.h>
#include "../plugin/PluginProcessor.h"
#include "Widgets.h"
#include "KeyMap.h"
#include "WaveView.h"
#include "UpdatePanel.h"

class MainView : public juce::Component,
                 private juce::Timer,
                 private juce::ChangeListener
{
public:
    explicit MainView (ReplikantProcessor&);
    ~MainView() override;

    std::function<void (float)> onZoom;
    float getZoom() const                     { return zoom; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Group
    {
        juce::String title;
        int page;
        std::vector<ui::ParamRow*> rows;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void buildRows();
    ui::ParamRow& add (Group&, std::unique_ptr<ui::ParamRow>, bool mapping = false);
    void showPage (int);
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

    ReplikantProcessor& proc;
    KeyMap keymap { proc };
    WaveView wave;
    UpdatePanel updatePanel { proc.updater };

    std::vector<std::unique_ptr<ui::ParamRow>> rows;
    std::vector<Group> groups;
    ui::ParamRow* midiRow = nullptr;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    int page = 0;
    juce::TextButton pageButtons[3];

    juce::TextEditor nameEditor;
    juce::TextButton newButton { "Nouveau" }, openButton { "Ouvrir" }, exportButton { "Exporter" },
                     folderButton { "Dossier" }, audioButton { "Carte son" }, zoomButton, updateButton;
    juce::TextButton sampleButton, testButton { "Test MIDI" }, prevButton { "<" }, nextButton { ">" }, playButton;
    std::unique_ptr<juce::FileChooser> chooser;

    Worker::Status status;
    InstrumentPtr shown;
    int selected = -1;
    juce::String message;
    bool messageIsWarning = false;
    double messageUntil = 0.0;
    float zoom = 1.0f;
    juce::Rectangle<int> header, left, mapCard, waveCard, bottom, statusArea, meterArea;
    juce::String lastWarning;
    std::optional<Updater::News> pendingNews;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
