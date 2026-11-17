// The keyboard and its map: one column per key, velocity going up. What will be recorded is drawn as empty
// frames, what is recorded is filled (its recorded key solid), the note being recorded flashes red, the zone
// playing lights up. Under it, a linear pixel keyboard to play the instrument (higher on a key = softer), and
// the range to record with its two handles.

#pragma once

#include <JuceHeader.h>
#include "../plugin/PluginProcessor.h"

class KeyMap : public juce::Component
{
public:
    explicit KeyMap (ReplikantProcessor&);

    void refresh (InstrumentPtr, const Worker::Status&, bool sampling, int currentJob);
    void setSelected (int sampleIndex)        { selected = sampleIndex; repaint(); }
    std::function<void (int)> onSelect;       // a sample was clicked
    std::function<void()> onRangeChanged;     // the handles moved (or a custom note was toggled)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int keyAt (int x) const;
    int velocityAt (int y) const;
    juce::Rectangle<int> zoneRect (int keyLo, int keyHi, int velLo, int velHi) const;
    int columnX (int key) const;
    void updateRange();
    void playKey (int key, int velocity);
    void releaseKey();

    ReplikantProcessor& proc;
    InstrumentPtr ins;
    Worker::Status status;
    bool sampling = false;
    int job = -1;
    int selected = -1;
    int first = 24, last = 107;               // keys shown
    float colW = 10.0f;
    juce::Rectangle<int> map, keys, handles, labels;
    int dragHandle = -1;                      // 0 low, 1 high
    int heldKey = -1;
    int hoverKey = -1, hoverVel = -1;
    uint32_t blink = 0;
};
