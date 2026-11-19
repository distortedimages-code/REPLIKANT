// (0.4.13) STRETCH › SPECTRAL: a moment of the sound held, and played on as long as wanted.
//
// A frame of the source (SMEAR long, a Hann window) is analysed; its phases are thrown away and drawn again at random,
// so what comes out has the frame's colour without its time: a held instant, not a loop. Its magnitudes are then
// moved to the note played (their frequencies scaled: no change of length), sharpened toward its partials (TONAL),
// drawn through the note's harmonic series (HARMONICS: a noise, a voice or the radio becomes a chord), given an octave
// above and a twelfth (SHIMMER), and the two sides are drawn apart (WIDTH: their phases drawn apart). The frame comes
// back through a sine window and is overlap-added at half its length: the two windows' powers sum to one, and the
// frames, unrelated, add in power, so the texture keeps a steady level. Its loudness is kept whatever is done to it.
//
// The FFTs, the work buffers and the tables are shared (one voice at a time, on the audio thread); each voice has its
// own ring where its frames overlap.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace dsp
{
class Smear
{
public:
    Smear();
    ~Smear();
    Smear (const Smear&) = delete;
    Smear& operator= (const Smear&) = delete;

    static constexpr float minSeconds = 0.04f, maxSeconds = 1.0f;

    // its FFTs (powers of two up to the longest window at this rate), its tables and work buffers
    void prepare (double sampleRate);
    double sampleRate() const                    { return sr; }

    // what a frame is taken from
    struct Source
    {
        const float* l = nullptr;
        const float* r = nullptr;                // nullptr: mono
        int len = 0;
        bool loop = false;
        int ls = 0, le = 0;                      // the loop (its end excluded)
        double centre = 0.0;                     // where the frame is centred, in its samples
    };
    // what is done to it
    struct Shape
    {
        float seconds = 0.25f;                   // SMEAR: the window's length
        double step = 1.0;                       // the source's samples per output sample (its rate and the pitch)
        float noteHz = 261.63f;                  // the note played (HARMONICS)
        float tonal = 0.0f, harmonics = 0.0f, shimmer = 0.0f, width = 0.6f;
    };
    // a voice's own: its ring, where it reads
    struct Voice
    {
        std::vector<float> ring[2];
        int rp = 0, hopLeft = 0;
        uint32_t rng = 1, rngR = 7;
        bool started = false;
        void start (uint32_t seed);              // the ring emptied, the first frame due now
        // its next sample (both sides), the ring emptied behind it
        inline void read (float& l, float& r)
        {
            const int mask = (int) ring[0].size() - 1;
            l = ring[0][(size_t) rp];
            r = ring[1][(size_t) rp];
            ring[0][(size_t) rp] = 0.0f;
            ring[1][(size_t) rp] = 0.0f;
            rp = (rp + 1) & mask;
            --hopLeft;
        }
    };
    void allocate (Voice&) const;               // its ring (message thread)

    // a frame added to the voice's ring where it reads; the hop until the next one (half the window) is set in it
    void frame (Voice&, const Source&, const Shape&);

private:
    struct Size;
    double sr = 48000.0;
    int maxW = 0, maxN = 0;
    std::vector<std::unique_ptr<Size>> sizes;   // 1024, 2048, ... maxN
    std::vector<float> ar, ai, br, bi, mL, mR;
    std::vector<float> hann, sine, gauss, cosT, sinT;
};
}
