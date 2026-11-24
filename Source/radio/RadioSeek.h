// REPLIK8 — RADIO (0.4.18): SEEK, the next clean signal up or down the band. A shortwave receiver shows its band (its
// waterfall: a line of 1024 bins in dB, many a second, over a span the client chooses); RadioNet reads a few lines of
// it beside the sound, from the frequency tuned onwards, and this finds in them where the next signal worth hearing
// is, and where the dial goes for it:
//
//   AM   a carrier standing well over the band's floor (15 dB), its sidebands above the floor too (or very strong),
//        no other carrier as strong close by (they would beat): the dial on the carrier, on the broadcasters' grid
//        (5 kHz on shortwave, 9 or 10 on medium wave) when it sits that close to it
//   USB  a voice: a hump about 2.4 kHz wide over the floor at its loudest moments, and moving (its loudest well over
//        its mean: a carrier stands still), the dial 1.5 kHz under its middle
//   LSB  the same, the dial 1.5 kHz above
//   CW   a narrow carrier over the floor (12 dB): the dial 500 Hz under it (heard at 500 Hz)
//
// The floor is the band's own, bin by bin: its 20th percentile around it (crowded bands lift it; a carrier still
// stands out of it). Nothing here knows the network: RadioNet hands the lines in, the tests hand made-up ones.
#pragma once

#include <vector>

namespace radio
{
    enum class Demod { am = 0, usb, lsb, cw };

    // a stretch of the band read for a seek: its bins (the mean of the lines read, and their loudest), in dB
    struct Panorama
    {
        double startKhz = 0.0, binKhz = 1.0;    // the first bin's low edge, a bin's width
        std::vector<float> avg, peak;
        double khzAt (double bin) const noexcept { return startKhz + (bin + 0.5) * binKhz; }
        double endKhz() const noexcept { return startKhz + binKhz * (double) avg.size(); }
    };

    struct Found
    {
        double khz = 0.0;                       // where the dial goes (0: nothing clean that way)
        float snr = 0.0f;                       // how far it stands over the floor, dB
    };

    // the nearest clean signal past `fromKhz` (its own guard left out: the one heard now) the way `dir` goes (+1 up,
    // -1 down), for that demodulation
    Found nextSignal (const Panorama&, double fromKhz, int dir, Demod);

    // how a seek reads the band for that demodulation: the zoom (the span, the receiver's bandwidth / 2^zoom), the
    // lines it waits for, the stretches it walks before it gives up
    int seekZoom (Demod) noexcept;
    int seekLines (Demod) noexcept;
    int seekWindows (Demod) noexcept;
    // (the part of the band right by the frequency heard that a seek leaves out: the station itself)
    double seekGuard (Demod) noexcept;
}
