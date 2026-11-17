#include "RkCore.h"

#include <algorithm>
#include <cctype>

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
