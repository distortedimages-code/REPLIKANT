// The whole plug-in as a host runs it: the processor prepared, MIDI notes in, blocks out. Run on GitHub's Macs at
// each push (Apple Silicon), so that what a host hears is checked there too: a sound that stays in range, numbers
// only, and that moves on from one block to the next (a block played over and over is a fault).

#include <JuceHeader.h>
#include "../Source/plugin/PluginProcessor.h"

#include <tuple>

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

// A host that hands the plug-in the same buffer each time without clearing it (in place, nothing on the input):
// what comes in is what went out the block before. Notes must still end: the sound back to silence after the
// release, never piling up.
static void playAsALazyHost (double rate, int block, int lag = 1, float hostGain = 1.0f)
{
    ReplikantProcessor proc;
    proc.setRateAndBufferSizeDetails (rate, block);
    proc.prepareToPlay (rate, block);
    const int chans = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    // (lag 2: two buffers taking turns, so what comes in went out two blocks before)
    std::vector<juce::AudioBuffer<float>> bufs ((size_t) lag, juce::AudioBuffer<float> (chans, block));
    for (auto& b : bufs)
        b.clear();
    int turn = 0;
    const int total = (int) (3.0 * rate);
    float peak = 0.0f, during = 0.0f;
    double tail = 0.0;
    int tailFrames = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        for (auto [at, note, on] : { std::tuple<double, int, bool> { 0.0, 60, true }, { 0.2, 64, true }, { 0.4, 67, true },
                                     { 0.6, 60, false }, { 0.6, 64, false }, { 0.6, 67, false } })
        {
            const int s = (int) (at * rate);
            if (s >= pos && s < pos + block)
                midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), s - pos);
        }
        auto& buf = bufs[(size_t) turn];
        turn = (turn + 1) % lag;
        proc.processBlock (buf, midi);          // (not cleared: an earlier block's output is this block's input)
        if (hostGain != 1.0f)
            buf.applyGain (hostGain);           // (a host that touches it on the way: not the same numbers any more)
        for (int ch = 0; ch < juce::jmin (2, proc.getTotalNumOutputChannels()); ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample (ch, i);
                peak = std::max (peak, std::isfinite (x) ? std::abs (x) : 1.0e9f);
                if (pos < (int) (0.6 * rate))
                    during = std::max (during, std::abs (x));
                if (pos >= (int) (2.5 * rate))
                {
                    tail += (double) x * x;
                    ++tailFrames;
                }
            }
    }
    const double tailRms = std::sqrt (tail / juce::jmax (1, tailFrames));
    std::printf ("  a lazy host at %.0f Hz, blocks of %d (input = output %d block%s before): peak %.2f, after the release %.5f rms\n",
                 rate, block, lag, lag > 1 ? "s" : "", peak, tailRms);
    CHECK (during > 0.05f && peak < 1.2f, "lazy host: notes at %.2f, peak %.2f (%.0f Hz / %d)", during, peak, rate, block);
    CHECK (tailRms < 1.0e-3, "lazy host: the notes end (%.5f rms left, %.0f Hz / %d)", tailRms, rate, block);
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
        {
            playAsAHost (rate, block);
            playAsALazyHost (rate, block);
        }
    playAsALazyHost (48000.0, 512, 2);
    playAsALazyHost (48000.0, 1024, 1, 0.9999f);
    playAsALazyHost (44100.0, 256, 3, 1.0001f);

    // the capture page open, a real input: ÉCOUTE DIRECT lets it through (and closed, it doesn't)
    for (bool open : { true, false })
    {
        ReplikantProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        proc.captureOpen = open;
        juce::AudioBuffer<float> buf (juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels()), 512);
        double phase = 0.0, energy = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i, phase += 440.0 / 48000.0)
                for (int ch = 0; ch < juce::jmin (2, proc.getTotalNumInputChannels()); ++ch)
                    buf.setSample (ch, i, 0.3f * (float) std::sin (6.283185307 * phase));
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            if (b >= 20)
                for (int i = 0; i < 512; ++i)
                    energy += (double) buf.getSample (0, i) * buf.getSample (0, i);
        }
        const double rms = std::sqrt (energy / (20.0 * 512.0));
        std::printf ("  the input with the capture page %s: %.3f rms out\n", open ? "open" : "closed", rms);
        CHECK (open ? rms > 0.15 : rms < 1.0e-4, "the input heard only with the capture page open (%s: %.4f rms)", open ? "open" : "closed", rms);
    }
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
