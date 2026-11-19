#include "Cube.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace dsp
{
namespace cube
{
namespace
{
    //========================================================================== the sections a corner is made of
    Section make (double hz, double q, double mh, double mb, double ml)
    {
        Section s;
        s.lf = (float) std::log2 (std::clamp (hz, 10.0, 22000.0));
        s.lq = (float) std::log (std::clamp (q, 0.2, 90.0));
        s.mh = (float) mh;
        s.mb = (float) mb;
        s.ml = (float) ml;
        return s;
    }
    // nothing done (the numerator the denominator): kept at a frequency, so that a neighbour can grow from it
    Section flat (double hz, double q = 0.7071) { return make (hz, q, 1.0, 1.0 / q, 1.0); }
    // a peak (or a dip) of `db` at hz: the poles narrowed, the zeros widened by the same factor
    Section peak (double hz, double q, double db)
    {
        const double a = std::pow (10.0, db / 40.0);
        const double qp = q * a, qz = q / a;
        return make (hz, qp, 1.0, 1.0 / qz, 1.0);
    }
    // a notch, `depth` 0 (nothing) .. 1 (the zeros on the frequency axis)
    Section notch (double hz, double q, double depth) { return make (hz, q, 1.0, (1.0 - depth) / q, 1.0); }
    Section lp (double hz, double q)                  { return make (hz, q, 0.0, 0.0, 1.0); }
    Section hp (double hz, double q)                  { return make (hz, q, 1.0, 0.0, 0.0); }
    Section bp (double hz, double q)                  { return make (hz, q, 0.0, 1.0 / q, 0.0); }   // unity at hz
    Section lowShelf (double hz, double db)
    {
        const double g = std::pow (10.0, db / 20.0);
        return make (hz, 0.7071, 1.0, std::sqrt (2.0 * g), g);
    }
    Section highShelf (double hz, double db)
    {
        const double g = std::pow (10.0, db / 20.0);
        return make (hz, 0.7071, g, std::sqrt (2.0 * g), 1.0);
    }
    double lerp (double a, double b, double t)       { return a + (b - a) * t; }
    double glog (double a, double b, double t)       { return a * std::pow (b / a, t); }    // between, in octaves
    double clampHz (double hz)                       { return std::min (hz, 18000.0); }

    //========================================================================== the cubes
    using Build = void (*) (int x, int y, int z, Corner&);

    // VOWELS: the four corners of the vowel space (back / front, open / closed), a man's voice or a child's
    void vowels (int x, int y, int z, Corner& c)
    {
        static const double f[2][2][5] = {
            { { 730, 1090, 2440, 3400, 4500 },      // back, open: A
              { 300, 870, 2240, 3300, 4400 } },     // back, closed: U
            { { 660, 1720, 2410, 3400, 4500 },      // front, open: E
              { 270, 2290, 3010, 3600, 4600 } },    // front, closed: I
        };
        static const double bw[5] = { 90, 110, 140, 180, 200 };
        const double scale = z ? 1.32 : 1.0;
        for (int i = 0; i < 5; ++i)
        {
            const double hz = f[x][y][i] * scale, q = hz / (bw[i] * (z ? 1.15 : 1.0));
            c.s[i] = i < 2 ? lp (hz, q) : peak (hz, q, i == 2 ? 10.0 : (i == 3 ? 8.0 : 6.0));
        }
        c.s[5] = hp (z ? 160.0 : 100.0, 0.7071);
        c.s[6] = highShelf (3000.0, z ? 9.0 : 6.0);
    }

    // SWEEP: a cutoff from 60 Hz to 12 kHz, a low pass becoming a band pass, soft or sharp (a second peak, a notch)
    void sweep (int x, int y, int z, Corner& c)
    {
        const double fc = x ? 12000.0 : 60.0;
        const double q1 = z ? 2.4 : 0.54, q2 = z ? 5.5 : 1.31;
        c.s[0] = y ? bp (fc, q1 + 0.8) : lp (fc, q1);
        c.s[1] = y ? bp (fc, q2) : lp (fc, q2);
        c.s[2] = z ? peak (clampHz (fc * 2.0), 6.0, 9.0) : flat (clampHz (fc * 2.0), 6.0);
        c.s[3] = z ? notch (fc * 0.5, 2.0, 0.8) : flat (fc * 0.5, 2.0);
        for (int i = 4; i < sections; ++i)
            c.s[i] = flat (1000.0);
    }

    // PHASER: six notches in a geometric row (where it starts, how far apart), shallow or deep with a peak between
    void phaser (int x, int y, int z, Corner& c)
    {
        const double f0 = x ? 2000.0 : 80.0, r = y ? 2.6 : 1.5;
        for (int i = 0; i < 6; ++i)
        {
            const double hz = f0 * std::pow (r, i);
            c.s[i] = hz < 18000.0 ? notch (hz, z ? 2.5 : 1.2, z ? 1.0 : 0.5) : flat (18000.0, z ? 2.5 : 1.2);
        }
        const double mid = f0 * std::sqrt (r);
        c.s[6] = z ? peak (mid, 4.0, 8.0) : flat (mid, 4.0);
    }

    // COMB: peaks on the harmonics of a pitch, all of them or the odd ones, wide or narrow
    void comb (int x, int y, int z, Corner& c)
    {
        const double f0 = x ? 880.0 : 110.0;
        for (int i = 0; i < sections; ++i)
        {
            const double n = y ? (2 * i + 1) : (i + 1), hz = f0 * n;
            c.s[i] = hz < 18000.0 ? peak (hz, z ? 40.0 : 8.0, z ? 16.0 : 9.0) : flat (18000.0, z ? 40.0 : 8.0);
        }
    }

    // BELLS: the partials of a bell or of a struck bar, on a pitch, damped or ringing
    void bells (int x, int y, int z, Corner& c)
    {
        static const double bell[sections] = { 0.5, 1.0, 1.19, 1.5, 2.0, 2.51, 2.66 };
        static const double bar[sections] = { 1.0, 2.756, 5.404, 8.933, 13.34, 18.64, 24.81 };
        const double f0 = x ? 1600.0 : 200.0;
        for (int i = 0; i < sections; ++i)
        {
            const double hz = f0 * (y ? bar[i] : bell[i]);
            c.s[i] = hz < 18000.0 ? peak (hz, z ? 80.0 : 12.0, z ? 20.0 : 12.0 - i) : flat (18000.0, z ? 80.0 : 12.0);
        }
    }

    // TILT: dark or bright, the mids scooped or honking, the whole band or a telephone's
    void tilt (int x, int y, int z, Corner& c)
    {
        c.s[0] = lowShelf (250.0, x ? -9.0 : 9.0);
        c.s[1] = highShelf (3000.0, x ? 9.0 : -9.0);
        c.s[2] = peak (900.0, 0.8, y ? 10.0 : -10.0);
        c.s[3] = z ? hp (320.0, 0.54) : flat (320.0, 0.54);
        c.s[4] = z ? hp (320.0, 1.31) : flat (320.0, 1.31);
        c.s[5] = z ? lp (3400.0, 0.54) : flat (3400.0, 0.54);
        c.s[6] = z ? lp (3400.0, 1.31) : flat (3400.0, 1.31);
    }

    // DUO: two resonances, each on its axis, soft or screaming, a notch between them
    void duo (int x, int y, int z, Corner& c)
    {
        const double f1 = x ? 2400.0 : 150.0, f2 = y ? 8000.0 : 500.0;
        c.s[0] = peak (f1, z ? 20.0 : 2.0, z ? 20.0 : 8.0);
        c.s[1] = peak (f2, z ? 20.0 : 2.0, z ? 20.0 : 8.0);
        c.s[2] = notch (std::sqrt (f1 * f2), 3.0, z ? 0.9 : 0.3);
        c.s[3] = lp (12000.0, 0.7071);
        for (int i = 4; i < sections; ++i)
            c.s[i] = flat (1000.0);
    }

    // HOLLOW: a cluster of notches (its centre, how wide), and peaks on its edges or not
    void hollow (int x, int y, int z, Corner& c)
    {
        const double centre = x ? 6000.0 : 200.0, w = y ? 2.5 : 0.3;
        const double at[4] = { -0.5, -1.0 / 6.0, 1.0 / 6.0, 0.5 };
        for (int i = 0; i < 4; ++i)
            c.s[i] = notch (clampHz (centre * std::exp2 (w * at[i])), 3.0, 0.95);
        const double lo = centre * std::exp2 (-w * 0.5 - 0.3), hi = clampHz (centre * std::exp2 (w * 0.5 + 0.3));
        c.s[4] = z ? peak (lo, 6.0, 12.0) : flat (lo, 6.0);
        c.s[5] = z ? peak (hi, 6.0, 12.0) : flat (hi, 6.0);
        c.s[6] = flat (1000.0);
    }

    // BODY: the modes of a body, large or small, of wood or of metal, damped or ringing
    void body (int x, int y, int z, Corner& c)
    {
        static const double wood[sections] = { 1.0, 1.62, 2.33, 3.1, 4.05, 5.2, 6.7 };
        static const double metal[sections] = { 1.0, 2.17, 3.41, 4.83, 6.98, 9.12, 11.7 };
        const double f0 = x ? 900.0 : 90.0;
        for (int i = 0; i < sections; ++i)
        {
            const double hz = f0 * (y ? metal[i] : wood[i]);
            const double q = y ? (z ? 70.0 : 10.0) : (z ? 25.0 : 6.0);
            c.s[i] = hz < 18000.0 ? peak (hz, q, 16.0 - 1.2 * i) : flat (18000.0, q);
        }
    }

    // SPREAD: seven peaks gathered round 1 kHz or spread over the spectrum, alike or alternating with dips, wide or narrow
    void spread (int x, int y, int z, Corner& c)
    {
        for (int i = 0; i < sections; ++i)
        {
            const double hz = clampHz (1000.0 * std::exp2 ((i - 3) * (x ? 1.1 : 0.08)));
            const double db = y ? (i % 2 == 0 ? 16.0 : -12.0) : 10.0;
            c.s[i] = peak (hz, z ? 18.0 : 3.0, db);
        }
    }

    struct Entry { const char* name; const char* x; const char* y; const char* z; Build build; };
    const Entry entries[] = {
        { "VOWELS", "back > front", "open > closed", "man > child", vowels },
        { "SWEEP", "cutoff", "LP > BP", "soft > sharp", sweep },
        { "PHASER", "notches", "spacing", "depth", phaser },
        { "COMB", "pitch", "all > odd", "wide > narrow", comb },
        { "BELLS", "pitch", "bell > bar", "damped > ringing", bells },
        { "TILT", "dark > bright", "scoop > honk", "full > phone", tilt },
        { "DUO", "peak 1", "peak 2", "soft > scream", duo },
        { "HOLLOW", "centre", "width", "edges", hollow },
        { "BODY", "large > small", "wood > metal", "damp > ring", body },
        { "SPREAD", "gathered > spread", "even > alternate", "wide > narrow", spread },
    };
    constexpr int numCubes = (int) (sizeof (entries) / sizeof (entries[0]));

    // a section's (analog) response at hz
    std::complex<double> sectionAt (const Section& s, double hz, double qMul)
    {
        const double fp = std::exp2 ((double) s.lf), q = std::clamp (std::exp ((double) s.lq) * qMul, 0.2, 90.0);
        const std::complex<double> sv (0.0, hz / fp);
        const auto num = (double) s.mh * sv * sv + (double) s.mb * sv + (double) s.ml;
        const auto den = sv * sv + sv / q + 1.0;
        return num / den;
    }
    double cornerDb (const Corner& c, double hz)
    {
        std::complex<double> h (1.0);
        for (auto& s : c.s)
            h *= sectionAt (s, hz, 1.0);
        return 20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
    }

    std::vector<Def> build()
    {
        std::vector<Def> v ((size_t) numCubes);
        for (int i = 0; i < numCubes; ++i)
        {
            auto& d = v[(size_t) i];
            d.name = entries[i].name;
            d.x = entries[i].x;
            d.y = entries[i].y;
            d.z = entries[i].z;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                    for (int z = 0; z < 2; ++z)
                    {
                        auto& c = d.c[x][y][z];
                        entries[i].build (x, y, z, c);
                        // its level: as much power as it takes in, over the octaves 40 Hz .. 12 kHz, no peak over +15 dB
                        double power = 0.0, top = -200.0;
                        const int n = 96;
                        for (int k = 0; k < n; ++k)
                        {
                            const double hz = 40.0 * std::pow (300.0, (k + 0.5) / n);
                            const double db = cornerDb (c, hz);
                            power += std::pow (10.0, db / 10.0);
                            top = std::max (top, db);
                        }
                        double gain = -10.0 * std::log10 (std::max (1.0e-9, power / n));
                        gain = std::min (gain, 15.0 - top);
                        c.gainDb = (float) std::clamp (gain, -36.0, 36.0);
                    }
        }
        return v;
    }
    const std::vector<Def>& table()
    {
        static const std::vector<Def> t = build();
        return t;
    }
}

int count()                 { return numCubes; }
const Def& def (int i)      { return table()[(size_t) std::clamp (i, 0, numCubes - 1)]; }

void at (int cube, float x, float y, float z, float resonance, Point& out)
{
    const auto& d = def (cube);
    x = std::clamp (x, 0.0f, 1.0f);
    y = std::clamp (y, 0.0f, 1.0f);
    z = std::clamp (z, 0.0f, 1.0f);
    float w[8];
    for (int i = 0; i < 8; ++i)
        w[i] = ((i & 4) ? x : 1.0f - x) * ((i & 2) ? y : 1.0f - y) * ((i & 1) ? z : 1.0f - z);
    const float qMul = 1.0f + 3.0f * std::clamp (resonance, 0.0f, 1.0f) * std::clamp (resonance, 0.0f, 1.0f);
    float gain = 0.0f;
    for (int s = 0; s < sections; ++s)
    {
        float lf = 0.0f, lq = 0.0f, mh = 0.0f, mb = 0.0f, ml = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto& sec = d.c[(i >> 2) & 1][(i >> 1) & 1][i & 1].s[s];
            lf += w[i] * sec.lf;
            lq += w[i] * sec.lq;
            mh += w[i] * sec.mh;
            mb += w[i] * sec.mb;
            ml += w[i] * sec.ml;
        }
        // (RESO raises the Qs of what resonates or cuts, not of a section left flat: no bump where nothing was)
        const float q0 = std::exp (lq);
        const float shaped = std::min (1.0f, std::abs (mb * q0 - 1.0f) + std::abs (mh - 1.0f) + std::abs (ml - 1.0f));
        out.hz[s] = std::exp2 (lf);
        out.q[s] = std::clamp (q0 * (1.0f + (qMul - 1.0f) * shaped), 0.2f, 90.0f);
        out.mh[s] = mh;
        out.mb[s] = mb;
        out.ml[s] = ml;
    }
    for (int i = 0; i < 8; ++i)
        gain += w[i] * d.c[(i >> 2) & 1][(i >> 1) & 1][i & 1].gainDb;
    out.gain = std::pow (10.0f, gain / 20.0f);
}

double responseDb (int cube, float x, float y, float z, float resonance, double hz)
{
    Point p;
    at (cube, x, y, z, resonance, p);
    std::complex<double> h (p.gain);
    for (int s = 0; s < sections; ++s)
    {
        const std::complex<double> sv (0.0, hz / (double) p.hz[s]);
        h *= ((double) p.mh[s] * sv * sv + (double) p.mb[s] * sv + (double) p.ml[s]) / (sv * sv + sv / (double) p.q[s] + 1.0);
    }
    return 20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
}
}
}
