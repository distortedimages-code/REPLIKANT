// The plug-in window: the machine, laid out at one size and zoomed as a whole (its size follows the zoom).

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "../ui/Machine.h"

class ReplikantEditor : public juce::AudioProcessorEditor
{
public:
    explicit ReplikantEditor (ReplikantProcessor& p) : AudioProcessorEditor (p), main (p)
    {
        look.applyColours();
        setLookAndFeel (&look);
        juce::LookAndFeel::setDefaultLookAndFeel (&look);
        addAndMakeVisible (main);
        main.onZoom = [this] (float z) { applyZoom (z); };
        setResizable (false, false);
        applyZoom (main.getZoom());
    }

    ~ReplikantEditor() override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    void resized() override   { main.setBounds (0, 0, te::width, te::height); }

private:
    void applyZoom (float z)
    {
        z = juce::jlimit (0.5f, 2.0f, z);
        main.setTransform (juce::AffineTransform::scale (z));
        setSize (juce::roundToInt ((float) te::width * z), juce::roundToInt ((float) te::height * z));
    }

    te::Look look;          // before the view: it outlives it
    te::Machine main;
    juce::TooltipWindow tips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReplikantEditor)
};
