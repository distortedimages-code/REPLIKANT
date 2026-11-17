// One sample: its waveform in pixel columns, the held part, the note-off, the loop (and its blended seam),
// the playhead when it plays; and, at the right, a close-up of the seam as it is heard: the end of the loop
// followed by its start.

#pragma once

#include <JuceHeader.h>
#include "../core/RkCore.h"

class WaveView : public juce::Component
{
public:
    void show (std::shared_ptr<const rk::Sample>, int index);
    void setPlayhead (float position)       { if (std::abs (position - playhead) > 1.0f) { playhead = position; repaint(); } }
    std::shared_ptr<const rk::Sample> current() const   { return sample; }
    int currentIndex() const                { return index; }
    juce::String info() const;

    void paint (juce::Graphics&) override;
    void resized() override                 { computePeaks(); }

private:
    void computePeaks();
    std::shared_ptr<const rk::Sample> sample;
    int index = -1;
    std::vector<std::pair<float, float>> peaks;   // per column
    float playhead = -1.0f;
    juce::Rectangle<int> wave, seam;
};
