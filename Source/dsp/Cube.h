// CUBE (0.4.4): a filter defined at the eight corners of a cube, X, Y and Z moving between them.
//
// Each corner is a chain of seven sections, each a pair of poles (their frequency and Q) and a pair of zeros (given
// as the mix of the high pass, band pass and low pass outputs of a state variable filter tuned to the poles: a
// numerator s² mh + s mb + ml over s² + s/Q + 1). Between the corners, everything is interpolated (trilinear) where it
// moves evenly to the ear: the frequencies in octaves, the Qs in their logarithm, the mixes as they are, the corner's
// level in dB; the filter is then made from what was interpolated (never by crossfading eight filters).
//
// The cubes are our own: vowels, a sweep, a phaser, a comb, bells, a tilt, two peaks, a hollow, bodies, a spread.
// Each corner's level is set once, so that it passes as much as it takes away (its mean power over the octaves from
// 40 Hz to 12 kHz), no peak over +15 dB.

#pragma once

namespace dsp
{
namespace cube
{
    constexpr int sections = 7;
    constexpr const char* kNames = "VOWELS|SWEEP|PHASER|COMB|BELLS|TILT|DUO|HOLLOW|BODY|SPREAD";

    struct Section
    {
        float lf = 10.0f;            // log2 of its poles' frequency (Hz)
        float lq = -0.35f;           // ln of their Q
        float mh = 1.0f, mb = 1.4142f, ml = 1.0f;   // the zeros: the mix of HP, BP, LP (1, 1/Q, 1: flat)
    };
    struct Corner
    {
        Section s[sections];
        float gainDb = 0.0f;
    };
    struct Def
    {
        const char* name;
        const char* x;               // what each axis does (short, for the screen)
        const char* y;
        const char* z;
        Corner c[2][2][2];           // [x][y][z]
    };

    int count();
    const Def& def (int i);

    // the filter at (x, y, z), each 0..1, its Qs raised by `resonance` (0..1: up to x4)
    struct Point
    {
        float hz[sections], q[sections], mh[sections], mb[sections], ml[sections];
        float gain = 1.0f;           // linear
    };
    void at (int cube, float x, float y, float z, float resonance, Point& out);

    // its response (the analog prototype), in dB, at hz
    double responseDb (int cube, float x, float y, float z, float resonance, double hz);
}
}
