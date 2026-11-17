// One sample: its waveform in pixel columns, the held part, the note-off, the loop (and its blended seam),
// the playhead when it plays; and, at the right, a close-up of the seam as it is heard: the end of the loop
// followed by its start.

#pragma once

#include <JuceHeader.h>
#include <array>
#include "../core/RkCore.h"

class WaveView : public juce::Component
{
public:
    void show (std::shared_ptr<const rk::Sample>, int index);
    void setPlayhead (float position)       { if (std::abs (position - playhead) > 1.0f) { playhead = position; repaint(); } }
    // the player's marks: its grains (frames, < 0 = none), the loop as it is moved (-1 = the sample's own)
    void setPlayerMarks (const std::array<float, 6>& grainsAt, int loopStart, int loopEnd)
    {
        if (grainsAt != grains || loopStart != movedStart || loopEnd != movedEnd)
        {
            grains = grainsAt;
            movedStart = loopStart;
            movedEnd = loopEnd;
            repaint();
        }
    }
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
    std::array<float, 6> grains { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    int movedStart = -1, movedEnd = -1;
    juce::Rectangle<int> wave, seam;
};
