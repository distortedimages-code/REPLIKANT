#include "RkCore.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace rk
{
std::string noteName (int note)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127)
        return "?";
    return std::string (names[note % 12]) + std::to_string (note / 12 - 2);
}

int parseNote (const std::string& textIn)
{
    std::string t;
    for (char c : textIn)
        if (! std::isspace ((unsigned char) c))
            t += c;
    if (t.empty())
        return -1;
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const char letter = (char) std::toupper ((unsigned char) t[0]);
    if (letter < 'A' || letter > 'G')
        return -1;
    int n = base[letter - 'A'];
    size_t i = 1;
    while (i < t.size() && (t[i] == '#' || t[i] == 'b'))
    {
        n += t[i] == '#' ? 1 : -1;
        ++i;
    }
    if (i >= t.size())
        return -1;
    try
    {
        size_t used = 0;
        const int octave = std::stoi (t.substr (i), &used);
        if (used != t.size() - i)
            return -1;
        const int note = (octave + 2) * 12 + n;
        return note >= 0 && note <= 127 ? note : -1;
    }
    catch (...)
    {
        return -1;
    }
}

std::vector<float> mixDown (const Channels& ch, int start, int count)
{
    if (ch.empty())
        return {};
    const int len = (int) ch[0].size();
    start = std::clamp (start, 0, len);
    if (count < 0 || start + count > len)
        count = len - start;
    std::vector<float> m ((size_t) count, 0.0f);
    const float k = 1.0f / (float) ch.size();
    for (auto& c : ch)
        for (int i = 0; i < count; ++i)
            m[(size_t) i] += c[(size_t) (start + i)] * k;
    return m;
}

float peakOf (const Channels& ch)
{
    float p = 0.0f;
    for (auto& c : ch)
        for (float v : c)
            p = std::max (p, std::abs (v));
    return p;
}

void applyGain (Channels& ch, float g)
{
    for (auto& c : ch)
        for (auto& v : c)
            v *= g;
}
}

namespace rk
{
int attackEndOf (const Channels& ch, double sr)
{
    if (ch.empty() || ch[0].empty() || sr <= 0.0)
        return 0;
    const int n = (int) ch[0].size();
    const int limit = std::min (n, (int) (0.08 * sr));
    // a 1 ms peak follower
    const float release = (float) std::exp (-1.0 / (0.001 * sr));
    std::vector<float> env ((size_t) std::min (n, limit + (int) (0.06 * sr)), 0.0f);
    float e = 0.0f, best = 0.0f;
    int bestAt = 0;
    for (int i = 0; i < (int) env.size(); ++i)
    {
        float a = 0.0f;
        for (auto& c : ch)
            a = std::max (a, std::abs (c[(size_t) i]));
        e = std::max (a, e * release);
        env[(size_t) i] = e;
        if (i < limit && e > best)
        {
            best = e;
            bestAt = i;
        }
    }
    if (best <= 1.0e-6f || bestAt >= limit - 1)
        return 0;
    // the attack ends where the envelope has fallen 9 dB under its peak (within 60 ms of it); a sound that doesn't
    // fall has no attack to keep apart
    for (int i = bestAt; i < (int) env.size(); ++i)
        if (env[(size_t) i] < best * 0.35f)
            return i;
    return 0;
}

Channels halfRate (const Channels& in)
{
    // a Kaiser-windowed halfband, 255 taps (beta 12): flat to 0.45, about 120 dB down from 0.55 of the old band
    static const std::vector<double> h = []
    {
        const int taps = 255, c = taps / 2;
        std::vector<double> t ((size_t) taps, 0.0);
        auto i0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k)
            {
                term *= (x * x / 4.0) / ((double) k * (double) k);
                sum += term;
                if (term < sum * 1.0e-17)
                    break;
            }
            return sum;
        };
        const double beta = 12.0, norm = i0 (beta);
        double sum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const int k = i - c;
            const double sinc = k == 0 ? 0.5 : std::sin (3.14159265358979323846 * k * 0.5) / (3.14159265358979323846 * k);
            const double r = (double) k / (double) c;
            t[(size_t) i] = sinc * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / norm;
            sum += t[(size_t) i];
        }
        for (auto& v : t)
            v /= sum;
        return t;
    }();
    const int c = (int) h.size() / 2;
    Channels out;
    for (auto& x : in)
    {
        const int n = (int) x.size();
        std::vector<float> y ((size_t) ((n + 1) / 2), 0.0f);
        for (int m = 0; m < (int) y.size(); ++m)
        {
            const int centre = 2 * m;
            double acc = h[(size_t) c] * (double) x[(size_t) centre];
            // the odd taps (the even ones but the centre are zero in a halfband)
            for (int j = 1; j <= c; j += 2)
            {
                const int a = centre - j, b = centre + j;
                const double xa = a >= 0 ? (double) x[(size_t) a] : 0.0, xb = b < n ? (double) x[(size_t) b] : 0.0;
                acc += h[(size_t) (c + j)] * (xa + xb);
            }
            y[(size_t) m] = (float) acc;
        }
        out.push_back (std::move (y));
    }
    return out;
}
}
