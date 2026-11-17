#include "Sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace snd
{
namespace
{
    constexpr const char* kDivisions = "4 MES|2 MES|1 MES|1/2|1/4|1/8|1/16|1/32|1/2 .|1/4 .|1/8 .|1/16 .|1/2 T|1/4 T|1/8 T|1/16 T";
    constexpr const char* kShapes = "SINUS|TRIANGLE|RAMPE +|RAMPE -|CARR\xc3\x89|S&H|SLEW";

    using K = Kind;
    // id, name, kind, lo, hi, default, shape, choices, modulatable, unit
    const Def table[count] = {
        { "volume",    "Volume",                    K::linear,  -36.0f, 12.0f, 0.0f, 1, nullptr, true, "dB" },
        { "voiceMode", "Voix",                      K::choice,  0, 2, 0, 1, "POLY|MONO|LEGATO", false, "" },
        { "voices",    "Polyphonie",                K::integer, 1, 8, 8, 1, nullptr, false, "" },
        { "glide",     "Glisse",                    K::power,   0, 2000, 0, 2.5f, nullptr, true, "ms" },
        { "transpose", "Transpo",                   K::integer, -24, 24, 0, 1, nullptr, true, "st" },
        { "fine",      "Accord fin",                K::linear,  -100, 100, 0, 1, nullptr, true, "ct" },
        { "bend",      "Pitch bend",                K::integer, 0, 12, 2, 1, nullptr, false, "st" },
        { "velocity",  "V\xc3\xa9locit\xc3\xa9",    K::linear,  0, 1, 0.7f, 1, nullptr, false, "%" },
        { "loops",     "Boucles",                   K::toggle,  0, 1, 1, 1, nullptr, false, "" },

        { "playMode",  "Lecture",                   K::choice,  0, 1, 0, 1, "SAMPLE|\xc3\x89LASTIQUE", false, "" },
        { "start",     "D\xc3\xa9part",             K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "speed",     "Vitesse",                   K::linear,  -2, 2, 1, 1, nullptr, true, "x" },
        { "grain",     "Grain",                     K::log,     10, 500, 80, 1, nullptr, true, "ms" },
        { "spray",     "Dispersion",                K::linear,  0, 1, 0.1f, 1, nullptr, true, "%" },
        { "alias",     "Aliasing",                  K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "bits",      "Bits",                      K::linear,  1, 32, 32, 1, nullptr, true, "bit" },
        { "reverse",   "Sens",                      K::choice,  0, 1, 0, 1, "AVANT|ARRI\xc3\x88RE", true, "" },
        { "loopStart", "D\xc3\xa9" "but boucle",     K::linear,  -1, 1, 0, 1, nullptr, true, "%" },
        { "loopSize",  "Taille boucle",             K::log,     0.01f, 1, 1, 1, nullptr, true, "%" },
        { "pmSource",  "Modulation de phase",       K::choice,  0, 3, 0, 1, "AUCUNE|SINUS|SAMPLE|ENTR\xc3\x89" "E", false, "" },
        { "pmRatio",   "Rapport",                   K::log,     0.25f, 8, 1, 1, nullptr, true, "x" },
        { "pmIndex",   "Indice",                    K::power,   0, 256, 0, 2.0f, nullptr, true, "smp" },
        { "transients","Transitoires",              K::choice,  0, 1, 0, 1, "SUIVENT LA HAUTEUR|GARD\xc3\x89S", false, "" },

        { "attack",    "Attaque",                   K::power,   0, 4000, 1, 3.3f, nullptr, true, "ms" },
        { "decay",     "D\xc3\xa9" "croissance",    K::log,     1, 10000, 600, 1, nullptr, true, "ms" },
        { "sustain",   "Maintien",                  K::linear,  0, 1, 1, 1, nullptr, true, "%" },
        { "release",   "Rel\xc3\xa2" "che",         K::log,     5, 10000, 300, 1, nullptr, true, "ms" },

        { "fModel",    "Filtre",                    K::choice,  0, 6, 0, 1, "AUCUN|MOOG|MS-20|SEM|OSCAR|WASP|POLIVOKS", false, "" },
        { "fType",     "Type",                      K::choice,  0, 3, 0, 1, "PASSE-BAS|PASSE-BANDE|PASSE-HAUT|R\xc3\x89J" "ECTEUR", false, "" },
        { "cutoff",    "Coupure",                   K::log,     20, 20000, 2500, 1, nullptr, true, "Hz" },
        { "reso",      "R\xc3\xa9sonance",          K::linear,  0, 1, 0.2f, 1, nullptr, true, "%" },
        { "fDrive",    "Saturation",                K::linear,  0, 1, 0.2f, 1, nullptr, true, "%" },
        { "sep",       "S\xc3\xa9paration",         K::linear,  0, 1, 0.0f, 1, nullptr, true, "%" },
        { "fm",        "FM (r\xc3\xa9injection)",   K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "quality",   "Qualit\xc3\xa9",            K::choice,  0, 2, 1, 1, "1X|2X|4X", false, "" },
        { "fEnv",      "Enveloppe",                 K::linear,  -1, 1, 0, 1, nullptr, true, "oct" },
        { "fAttack",   "Attaque",                   K::power,   0, 4000, 2, 3.3f, nullptr, false, "ms" },
        { "fDecay",    "D\xc3\xa9" "croissance",    K::log,     1, 10000, 400, 1, nullptr, true, "ms" },
        { "fSustain",  "Maintien",                  K::linear,  0, 1, 0, 1, nullptr, false, "%" },
        { "fRelease",  "Rel\xc3\xa2" "che",         K::log,     5, 10000, 300, 1, nullptr, false, "ms" },
        { "keyTrack",  "Suivi clavier",             K::linear,  0, 1, 0.5f, 1, nullptr, false, "%" },
        { "velCutoff", "V\xc3\xa9locit\xc3\xa9",    K::linear,  0, 1, 0.3f, 1, nullptr, false, "%" },

        { "dModel",    "Drive",                     K::choice,  0, 3, 0, 1, "AUCUN|PR\xc3\x89" "AMPLI|CONSOLE|BANDE", false, "" },
        { "dAmt",      "Pouss\xc3\xa9" "e",         K::linear,  0, 1, 0.3f, 1, nullptr, true, "%" },
        { "dTone",     "Couleur",                   K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "dMix",      "Mix",                       K::linear,  0, 1, 1, 1, nullptr, true, "%" },

        { "spOn",      "Spectre",                   K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "spVoices",  "Oscillateurs",              K::integer, 1, 33, 12, 1, nullptr, false, "" },
        { "spSlice",   "Tranche",                   K::linear,  0, 1, 0.4f, 1, nullptr, true, "%" },
        { "spBlur",    "Flou",                      K::linear,  0, 1, 0.2f, 1, nullptr, true, "%" },
        { "spGlide",   "Glisse",                    K::linear,  0, 1, 0.3f, 1, nullptr, true, "%" },
        { "spCentre",  "Centre",                    K::linear,  0, 1, 0.5f, 1, nullptr, true, "spc" },
        { "spWidth",   "Largeur",                   K::linear,  0, 1, 1, 1, nullptr, true, "%" },
        { "spOct",     "Octave",                    K::integer, -2, 2, 0, 1, nullptr, true, "oct" },
        { "spWave",    "Onde",                      K::choice,  0, 3, 0, 1, "SINUS|TRIANGLE|DENT|CARR\xc3\x89", false, "" },
        { "spFreeze",  "Gel",                       K::toggle,  0, 1, 0, 1, nullptr, true, "" },
        { "spFeedback","R\xc3\xa9injection",        K::linear,  0, 0.95f, 0, 1, nullptr, true, "%" },
        { "spMix",     "Mix",                       K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },

        { "piOn",      "Pitch",                     K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "piA",       "Voix A",                    K::integer, -24, 24, 12, 1, nullptr, true, "st" },
        { "piB",       "Voix B",                    K::integer, -24, 24, -12, 1, nullptr, true, "st" },
        { "piDetune",  "D\xc3\xa9sacc" "ord",       K::linear,  0, 50, 0, 1, nullptr, true, "ct" },
        { "piDelay",   "Retard",                    K::power,   0, 1000, 0, 2.0f, nullptr, true, "ms" },
        { "piLevelB",  "Niveau B",                  K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "piFeedback","R\xc3\xa9injection",        K::linear,  0, 0.95f, 0, 1, nullptr, true, "%" },
        { "piCrystals","Cristaux",                  K::toggle,  0, 1, 0, 1, nullptr, true, "" },
        { "piMix",     "Mix",                       K::linear,  0, 1, 0.4f, 1, nullptr, true, "%" },

        { "chOn",      "Chorus",                    K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "chMode",    "Mode",                      K::choice,  0, 2, 0, 1, "CHORUS|FLANGER|VIBRATO", false, "" },
        { "chRate",    "Vitesse",                   K::log,     0.02f, 10, 0.4f, 1, nullptr, true, "Hz" },
        { "chDepth",   "Profondeur",                K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "chFeedback","R\xc3\xa9injection",        K::linear,  -0.95f, 0.95f, 0, 1, nullptr, true, "%" },
        { "chTone",    "Couleur",                   K::linear,  0, 1, 0.6f, 1, nullptr, false, "%" },
        { "chMix",     "Mix",                       K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },

        { "crOn",      "Crush",                     K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "crBits",    "Bits",                      K::linear,  1, 16, 16, 1, nullptr, true, "bit" },
        { "crRate",    "Fr\xc3\xa9quence",          K::log,     200, 48000, 48000, 1, nullptr, true, "Hz" },
        { "crJitter",  "Instabilit\xc3\xa9",        K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "crTone",    "Couleur",                   K::linear,  0, 1, 1, 1, nullptr, true, "%" },
        { "crMix",     "Mix",                       K::linear,  0, 1, 1, 1, nullptr, true, "%" },

        { "rvOn",      "Espace",                    K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "rvSize",    "Taille",                    K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "rvDecay",   "Dur\xc3\xa9" "e",           K::linear,  0, 1, 0.5f, 1, nullptr, true, "rv" },
        { "rvTone",    "Couleur",                   K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "rvShimmer", "Shimmer",                   K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "rvMod",     "Mouvement",                 K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "rvMix",     "Mix",                       K::linear,  0, 1, 0.3f, 1, nullptr, true, "%" },

#define RK_MOD(n, type, div) \
        { "m" #n "Type",     "Type",                K::choice,  0, 4, type, 1, "ONDE|PAS|HASARD|ENV|SUIVEUR", false, "" }, \
        { "m" #n "Shape",    "Forme",               K::choice,  0, 6, 0, 1, kShapes, false, "" }, \
        { "m" #n "Sync",     "Rythme",              K::choice,  0, 1, 0, 1, "TEMPO|LIBRE", false, "" }, \
        { "m" #n "Div",      "Division",            K::choice,  0, 15, div, 1, kDivisions, false, "" }, \
        { "m" #n "Hz",       "Fr\xc3\xa9quence",    K::log,     0.02f, 40, 1, 1, nullptr, false, "Hz" }, \
        { "m" #n "Smooth",   "Lissage",             K::linear,  0, 1, 0, 1, nullptr, false, "%" }, \
        { "m" #n "Polarity", "Polarit\xc3\xa9",     K::choice,  0, 1, 0, 1, "\xc2\xb1|+", false, "" }, \
        { "m" #n "Retrig",   "Relance",             K::choice,  0, 1, 0, 1, "LIBRE|\xc3\x80 CHAQUE NOTE", false, "" }, \
        { "m" #n "Len",      "Longueur",            K::integer, 1, 32, 16, 1, nullptr, false, "" }, \
        { "m" #n "Vari",     "Variation",           K::linear,  0, 1, 0, 1, nullptr, false, "%" }, \
        { "m" #n "Attack",   "Attaque",             K::power,   0, 4000, 5, 3.3f, nullptr, false, "ms" }, \
        { "m" #n "Decay",    "D\xc3\xa9" "croissance", K::log,  1, 10000, 300, 1, nullptr, false, "ms" }, \
        { "m" #n "Sustain",  "Maintien",            K::linear,  0, 1, 0.5f, 1, nullptr, false, "%" }, \
        { "m" #n "Release",  "Rel\xc3\xa2" "che",      K::log,     5, 10000, 400, 1, nullptr, false, "ms" }, \
        { "m" #n "Source",   "Source",              K::choice,  0, 1, 0, 1, "ENTR\xc3\x89" "E|INSTRUMENT", false, "" }, \
        { "m" #n "Gain",     "Sensibilit\xc3\xa9",  K::linear,  0, 36, 12, 1, nullptr, false, "dB" }, \
        { "m" #n "Amt",      "Profondeur",          K::linear,  0, 1, 1, 1, nullptr, false, "%" },
        RK_MOD (1, 0, 4)
        RK_MOD (2, 1, 6)
        RK_MOD (3, 2, 5)
        RK_MOD (4, 3, 6)
#undef RK_MOD

        { "xfade",     "Fader",                     K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "sceneA",    "Sc\xc3\xa8ne A",            K::integer, 1, 16, 1, 1, nullptr, false, "" },
        { "sceneB",    "Sc\xc3\xa8ne B",            K::integer, 1, 16, 2, 1, nullptr, false, "" },
        { "tempo",     "Tempo",                     K::linear,  40, 300, 120, 1, nullptr, false, "bpm" },
    };

    std::string fmt (const char* f, double v)
    {
        char b[64];
        std::snprintf (b, sizeof (b), f, v);
        return b;
    }

    std::string msText (double ms)
    {
        if (ms < 1000.0)
            return fmt (ms < 10.0 ? "%.1f MS" : "%.0f MS", ms);
        return fmt (ms < 10000.0 ? "%.2f S" : "%.1f S", ms / 1000.0);
    }

    std::string hzText (double hz)
    {
        if (hz < 1.0)
            return fmt ("%.2f HZ", hz);
        if (hz < 100.0)
            return fmt ("%.1f HZ", hz);
        if (hz < 1000.0)
            return fmt ("%.0f HZ", hz);
        return fmt (hz < 10000.0 ? "%.2f KHZ" : "%.1f KHZ", hz / 1000.0);
    }
}

bool sceneLockable (int id)
{
    return id >= 0 && id < count && id != xfade && id != sceneA && id != sceneB && id != tempo && id != quality
        && id != voices && id != voiceMode;
}

const Def& def (int id)
{
    return table[std::clamp (id, 0, count - 1)];
}

int find (const std::string& id)
{
    for (int i = 0; i < count; ++i)
        if (id == table[i].id)
            return i;
    return -1;
}

std::vector<std::string> choicesOf (int id)
{
    std::vector<std::string> out;
    const auto& d = def (id);
    if (d.kind == Kind::toggle)
        return { "NON", "OUI" };
    if (d.choices == nullptr)
        return out;
    std::string cur;
    for (const char* p = d.choices; *p != 0; ++p)
    {
        if (*p == '|')
        {
            out.push_back (cur);
            cur.clear();
        }
        else
            cur += *p;
    }
    out.push_back (cur);
    return out;
}

float fromNorm (int id, float t)
{
    const auto& d = def (id);
    t = std::clamp (t, 0.0f, 1.0f);
    switch (d.kind)
    {
        case Kind::log:     return d.lo * std::pow (d.hi / d.lo, t);
        case Kind::power:   return d.lo + (d.hi - d.lo) * std::pow (t, d.shape);
        case Kind::integer:
        case Kind::choice:
        case Kind::toggle:  return std::round (d.lo + (d.hi - d.lo) * t);
        case Kind::linear:
        default:            return d.lo + (d.hi - d.lo) * t;
    }
}

float toNorm (int id, float v)
{
    const auto& d = def (id);
    if (d.hi <= d.lo)
        return 0.0f;
    v = std::clamp (v, d.lo, d.hi);
    switch (d.kind)
    {
        case Kind::log:     return std::log (v / d.lo) / std::log (d.hi / d.lo);
        case Kind::power:   return std::pow ((v - d.lo) / (d.hi - d.lo), 1.0f / d.shape);
        default:            return (v - d.lo) / (d.hi - d.lo);
    }
}

float legal (int id, float v)
{
    const auto& d = def (id);
    if (! std::isfinite (v))
        return d.def;
    v = std::clamp (v, d.lo, d.hi);
    if (d.kind == Kind::integer || d.kind == Kind::choice || d.kind == Kind::toggle)
        v = std::round (v);
    return v;
}

std::string text (int id, float v)
{
    const auto& d = def (id);
    if (d.kind == Kind::choice || d.kind == Kind::toggle)
    {
        const auto c = choicesOf (id);
        const int i = (int) std::lround (v - d.lo);
        return i >= 0 && i < (int) c.size() ? c[(size_t) i] : std::string();
    }
    const std::string u = d.unit;
    if (u == "dB")  return (v > 0.0f ? "+" : "") + fmt ("%.1f DB", v);
    if (u == "ms")  return msText (v);
    if (u == "Hz")  return hzText (v);
    if (u == "st")  return (v > 0.0f ? "+" : "") + fmt ("%.0f DT", v);
    if (u == "ct")  return (v > 0.0f ? "+" : "") + fmt ("%.0f CT", v);
    if (u == "oct") return (v > 0.0f ? "+" : "") + fmt (d.kind == Kind::integer ? "%.0f OCT" : "%.1f OCT", d.kind == Kind::integer ? v : v * 6.0f);
    if (u == "x")   return fmt ("%.2f X", v);
    if (u == "bit") return fmt (v < 10.0f ? "%.1f BITS" : "%.0f BITS", v);
    if (u == "bpm") return fmt ("%.1f BPM", v);
    if (u == "smp") return fmt ("%.0f \xc3\x89" "CH", v);
    if (u == "spc") return hzText (40.0 * std::pow (250.0, (double) v));
    if (u == "rv")  return v >= 0.97f ? std::string ("GEL") : msText (1000.0 * 0.3 * std::pow (100.0, (double) v / 0.97));
    if (u == "%")   return fmt ("%.0f %%", v * 100.0f);
    return fmt (d.kind == Kind::integer ? "%.0f" : "%.2f", v);
}
}

namespace snd
{
std::string longName (int id)
{
    const std::string name = def (id).name;
    auto in = [id] (int a, int b) { return id >= a && id <= b; };
    if (id >= m1Type)
        return "M" + std::to_string ((id - m1Type) / modSettings + 1) + " " + name;
    if (in (attack, release))      return "Ampli " + name;
    if (in (fType, quality))       return "Filtre " + name;
    if (in (fEnv, velCutoff))      return "Env. filtre " + name;
    if (in (dAmt, dMix))           return "Drive " + name;
    if (in (spVoices, spMix))      return "Spectre " + name;
    if (in (piA, piMix))           return "Pitch " + name;
    if (in (chMode, chMix))        return "Chorus " + name;
    if (in (crBits, crMix))        return "Crush " + name;
    if (in (rvSize, rvMix))        return "Espace " + name;
    if (in (pmRatio, pmIndex))     return "PM " + name;
    return name;
}
}
