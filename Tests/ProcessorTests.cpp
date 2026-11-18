// The whole plug-in as a host runs it: the processor prepared, MIDI notes in, blocks out. Run on GitHub's Macs at
// each push (Apple Silicon), so that what a host hears is checked there too: a sound that stays in range, numbers
// only, and that moves on from one block to the next (a block played over and over is a fault).

#include <JuceHeader.h>
#include "../Source/plugin/PluginProcessor.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                                                      \
    do {                                                                                      \
        ++checks;                                                                             \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } \
    } while (0)

static void playAsAHost (double rate, int block)
{
    ReplikantProcessor proc;
    proc.setRateAndBufferSizeDetails (rate, block);
    proc.prepareToPlay (rate, block);
    const int chans = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (chans, block);
    const int total = (int) (3.0 * rate);
    float peak = 0.0f, firstNote = 0.0f;
    double energy = 0.0;
    bool finite = true;
    int frames = 0, repeats = 0, blocks = 0;
    std::vector<float> previous ((size_t) block, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        const int chordAt = (int) (1.0 * rate);
        if (chordAt >= pos && chordAt < pos + block)
            for (int n : { 48, 55, 64 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), chordAt - pos);
        if (pos < chordAt)
            firstNote = peak;
        buf.clear();
        proc.processBlock (buf, midi);
        bool same = blocks > 2;
        for (int i = 0; i < block; ++i)
        {
            const float x = buf.getSample (0, i);
            same = same && x == previous[(size_t) i];
            previous[(size_t) i] = x;
        }
        if (same && peak > 0.01f)
            ++repeats;
        ++blocks;
        for (int ch = 0; ch < juce::jmin (2, proc.getTotalNumOutputChannels()); ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample (ch, i);
                finite = finite && std::isfinite (x);
                peak = std::max (peak, std::abs (x));
                energy += (double) x * x;
            }
        frames += block;
    }
    const double rms = std::sqrt (energy / (2.0 * frames));
    std::printf ("  as a host at %.0f Hz, blocks of %d: a note %.2f, with a chord %.2f, rms %.3f, %d blocks repeated\n",
                 rate, block, firstNote, peak, rms, repeats);
    CHECK (finite, "numbers only at %.0f Hz / %d", rate, block);
    CHECK (firstNote > 0.1f && firstNote < 0.5f, "a note at %.2f (%.0f Hz / %d)", firstNote, rate, block);
    CHECK (peak < 1.2f && rms > 0.03 && rms < 0.4, "with a chord: peak %.2f, rms %.3f (%.0f Hz / %d)", peak, rms, rate, block);
    CHECK (repeats == 0, "%d blocks played twice (%.0f Hz / %d)", repeats, rate, block);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    // a home of its own: no settings from before
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("replik8-processor-tests");
    home.deleteRecursively();
    home.createDirectory();
   #if JUCE_MAC || JUCE_LINUX
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
   #endif
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 512, 1024 })
            playAsAHost (rate, block);
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
