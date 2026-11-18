#include "Sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace snd
{
namespace
{
    constexpr const char* kDivisions = "4 BAR|2 BAR|1 BAR|1/2|1/4|1/8|1/16|1/32|1/2 .|1/4 .|1/8 .|1/16 .|1/2 T|1/4 T|1/8 T|1/16 T";
    constexpr const char* kShapes = "SINE|TRI|RAMP +|RAMP -|SQUARE|S&H|SLEW";

    using K = Kind;
    // id, name, kind, lo, hi, default, shape, choices, modulatable, unit
    const Def table[count] = {
        { "volume",    "Volume",                      K::linear,  -36.0f, 12.0f, 0.0f, 1, nullptr, true, "dB" },
        { "voiceMode", "Voice mode",                  K::choice,  0, 2, 0, 1, "POLY|MONO|LEGATO", false, "" },
        { "voices",    "Polyphony",                   K::integer, 1, 8, 8, 1, nullptr, false, "" },
        { "glide",     "Glide",                       K::power,   0, 2000, 0, 2.5f, nullptr, true, "ms" },
        { "transpose", "Transpose",                   K::integer, -24, 24, 0, 1, nullptr, true, "st" },
        { "fine",      "Fine tune",                   K::linear,  -100, 100, 0, 1, nullptr, true, "ct" },
        { "bend",      "Bend range",                  K::integer, 0, 12, 2, 1, nullptr, false, "st" },
        { "velocity",  "Velocity",                    K::linear,  0, 1, 0.7f, 1, nullptr, false, "%" },
        { "loops",     "Loop",                        K::toggle,  0, 1, 1, 1, nullptr, false, "" },

        { "playMode",  "Play mode",                   K::choice,  0, 1, 0, 1, "SAMPLE|STRETCH", false, "" },
        { "start",     "Start",                       K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "speed",     "Speed",                       K::linear,  -2, 2, 1, 1, nullptr, true, "x" },
        { "grain",     "Grain",                       K::log,     10, 500, 80, 1, nullptr, true, "ms" },
        { "spray",     "Spray",                       K::linear,  0, 1, 0.1f, 1, nullptr, true, "%" },
        { "alias",     "Aliasing",                    K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "bits",      "Bits",                        K::linear,  1, 32, 32, 1, nullptr, true, "bit" },
        { "reverse",   "Direction",                   K::choice,  0, 1, 0, 1, "FWD|REV", true, "" },
        { "loopStart", "Loop start",                  K::linear,  -1, 1, 0, 1, nullptr, true, "%" },
        { "loopSize",  "Loop size",                   K::log,     0.01f, 1, 1, 1, nullptr, true, "%" },
        { "pmSource",  "PM source",                   K::choice,  0, 5, 0, 1, "OFF|SINE|SAMPLE|INPUT|FEEDBACK|ORBITE", false, "" },
        { "pmRatio",   "PM ratio",                    K::log,     0.25f, 8, 1, 1, nullptr, true, "x" },
        { "pmIndex",   "PM depth",                    K::power,   0, 256, 0, 2.0f, nullptr, true, "smp" },
        { "transients","Transients",                  K::choice,  0, 1, 0, 1, "PITCHED|KEPT", false, "" },

        { "attack",    "Attack",                      K::power,   0, 4000, 1, 3.3f, nullptr, true, "ms" },
        { "decay",     "Decay",                       K::log,     1, 10000, 600, 1, nullptr, true, "ms" },
        { "sustain",   "Sustain",                     K::linear,  0, 1, 1, 1, nullptr, true, "%" },
        { "release",   "Release",                     K::log,     5, 10000, 300, 1, nullptr, true, "ms" },

        { "fModel",    "Model",                       K::choice,  0, 6, 0, 1, "OFF|MOOG|MS-20|SEM|OSCAR|WASP|POLIVOKS", false, "" },
        { "fType",     "Type",                        K::choice,  0, 3, 0, 1, "LP|BP|HP|NOTCH", false, "" },
        { "cutoff",    "Cutoff",                      K::log,     20, 20000, 2500, 1, nullptr, true, "Hz" },
        { "reso",      "Resonance",                   K::linear,  0, 1, 0.2f, 1, nullptr, true, "%" },
        { "fDrive",    "Drive",                       K::linear,  0, 1, 0.2f, 1, nullptr, true, "%" },
        { "sep",       "Spread",                      K::linear,  0, 1, 0.0f, 1, nullptr, true, "%" },
        { "fm",        "FM",                          K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "quality",   "Quality",                     K::choice,  0, 2, 2, 1, "1X|2X|4X", false, "" },
        { "fEnv",      "Env amount",                  K::linear,  -1, 1, 0, 1, nullptr, true, "oct" },
        { "fAttack",   "Attack",                      K::power,   0, 4000, 2, 3.3f, nullptr, false, "ms" },
        { "fDecay",    "Decay",                       K::log,     1, 10000, 400, 1, nullptr, true, "ms" },
        { "fSustain",  "Sustain",                     K::linear,  0, 1, 0, 1, nullptr, false, "%" },
        { "fRelease",  "Release",                     K::log,     5, 10000, 300, 1, nullptr, false, "ms" },
        { "keyTrack",  "Key track",                   K::linear,  0, 1, 0.5f, 1, nullptr, false, "%" },
        { "velCutoff", "Velocity",                    K::linear,  0, 1, 0.3f, 1, nullptr, false, "%" },

        { "dModel",    "Model",                       K::choice,  0, 3, 0, 1, "OFF|TUBE|CONSOLE|TAPE", false, "" },
        { "dAmt",      "Drive",                       K::linear,  0, 1, 0.3f, 1, nullptr, true, "%" },
        { "dTone",     "Tone",                        K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "dMix",      "Mix",                         K::linear,  0, 1, 1, 1, nullptr, true, "%" },

        { "spOn",      "Spectre",                     K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "spVoices",  "Voices",                      K::integer, 1, 33, 12, 1, nullptr, false, "" },
        { "spSlice",   "Slice",                       K::linear,  0, 1, 0.4f, 1, nullptr, true, "%" },
        { "spBlur",    "Blur",                        K::linear,  0, 1, 0.2f, 1, nullptr, true, "%" },
        { "spGlide",   "Glide",                       K::linear,  0, 1, 0.3f, 1, nullptr, true, "%" },
        { "spCentre",  "Centre",                      K::linear,  0, 1, 0.5f, 1, nullptr, true, "spc" },
        { "spWidth",   "Width",                       K::linear,  0, 1, 1, 1, nullptr, true, "%" },
        { "spOct",     "Octave",                      K::integer, -2, 2, 0, 1, nullptr, true, "oct" },
        { "spWave",    "Wave",                        K::choice,  0, 3, 0, 1, "SINE|TRI|SAW|SQR", false, "" },
        { "spFreeze",  "Freeze",                      K::toggle,  0, 1, 0, 1, nullptr, true, "" },
        { "spFeedback","Feedback",                    K::linear,  0, 0.95f, 0, 1, nullptr, true, "%" },
        { "spMix",     "Mix",                         K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },

        { "piOn",      "Pitch",                       K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "piA",       "Voice A",                     K::integer, -24, 24, 12, 1, nullptr, true, "st" },
        { "piB",       "Voice B",                     K::integer, -24, 24, -12, 1, nullptr, true, "st" },
        { "piDetune",  "Detune",                      K::linear,  0, 50, 0, 1, nullptr, true, "ct" },
        { "piDelay",   "Delay",                       K::power,   0, 1000, 0, 2.0f, nullptr, true, "ms" },
        { "piLevelB",  "Level B",                     K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "piFeedback","Feedback",                    K::linear,  0, 0.95f, 0, 1, nullptr, true, "%" },
        { "piCrystals","Crystals",                    K::toggle,  0, 1, 0, 1, nullptr, true, "" },
        { "piMix",     "Mix",                         K::linear,  0, 1, 0.4f, 1, nullptr, true, "%" },

        { "chOn",      "Chorus",                      K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "chMode",    "Mode",                        K::choice,  0, 2, 0, 1, "CHORUS|FLANGER|VIBRATO", false, "" },
        { "chRate",    "Rate",                        K::log,     0.02f, 10, 0.4f, 1, nullptr, true, "Hz" },
        { "chDepth",   "Depth",                       K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "chFeedback","Feedback",                    K::linear,  -0.95f, 0.95f, 0, 1, nullptr, true, "%" },
        { "chTone",    "Tone",                        K::linear,  0, 1, 0.6f, 1, nullptr, false, "%" },
        { "chMix",     "Mix",                         K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },

        { "crOn",      "Crush",                       K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "crBits",    "Bits",                        K::linear,  1, 16, 16, 1, nullptr, true, "bit" },
        { "crRate",    "Rate",                        K::log,     200, 48000, 48000, 1, nullptr, true, "Hz" },
        { "crJitter",  "Jitter",                      K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "crTone",    "Tone",                        K::linear,  0, 1, 1, 1, nullptr, true, "%" },
        { "crMix",     "Mix",                         K::linear,  0, 1, 1, 1, nullptr, true, "%" },

        { "rvOn",      "Espace",                      K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "rvSize",    "Size",                        K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "rvDecay",   "Decay",                       K::linear,  0, 1, 0.5f, 1, nullptr, true, "rv" },
        { "rvTone",    "Tone",                        K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "rvShimmer", "Shimmer",                     K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "rvMod",     "Motion",                      K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "rvMix",     "Mix",                         K::linear,  0, 1, 0.3f, 1, nullptr, true, "%" },

#define RK_MOD(n, type, div) \
        { "m" #n "Type",     "Type",                K::choice,  0, 4, type, 1, "LFO|STEPS|RANDOM|ENV|FOLLOW", false, "" }, \
        { "m" #n "Shape",    "Shape",               K::choice,  0, 6, 0, 1, kShapes, false, "" }, \
        { "m" #n "Sync",     "Rhythm",              K::choice,  0, 1, 0, 1, "SYNC|FREE", false, "" }, \
        { "m" #n "Div",      "Division",            K::choice,  0, 15, div, 1, kDivisions, false, "" }, \
        { "m" #n "Hz",       "Rate",                K::log,     0.02f, 40, 1, 1, nullptr, false, "Hz" }, \
        { "m" #n "Smooth",   "Smooth",             K::linear,  0, 1, 0, 1, nullptr, false, "%" }, \
        { "m" #n "Polarity", "Polarity",            K::choice,  0, 1, 0, 1, "\xc2\xb1|+", false, "" }, \
        { "m" #n "Retrig",   "Retrig",              K::choice,  0, 1, 0, 1, "FREE|NOTE", false, "" }, \
        { "m" #n "Len",      "Length",            K::integer, 1, 32, 16, 1, nullptr, false, "" }, \
        { "m" #n "Vari",     "Variation",           K::linear,  0, 1, 0, 1, nullptr, false, "%" }, \
        { "m" #n "Attack",   "Attack",             K::power,   0, 4000, 5, 3.3f, nullptr, false, "ms" }, \
        { "m" #n "Decay",    "Decay",               K::log,     1, 10000, 300, 1, nullptr, false, "ms" }, \
        { "m" #n "Sustain",  "Sustain",            K::linear,  0, 1, 0.5f, 1, nullptr, false, "%" }, \
        { "m" #n "Release",  "Release",             K::log,     5, 10000, 400, 1, nullptr, false, "ms" }, \
        { "m" #n "Source",   "Source",              K::choice,  0, 1, 0, 1, "INPUT|VOICES", false, "" }, \
        { "m" #n "Gain",     "Gain",                K::linear,  0, 36, 12, 1, nullptr, false, "dB" }, \
        { "m" #n "Amt",      "Amount",              K::linear,  0, 1, 1, 1, nullptr, false, "%" },
        RK_MOD (1, 0, 4)
        RK_MOD (2, 1, 6)
        RK_MOD (3, 2, 5)
        RK_MOD (4, 3, 6)
#undef RK_MOD

        { "xfade",     "Crossfader",                  K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "sceneA",    "Scene A",                     K::integer, 1, 16, 1, 1, nullptr, false, "" },
        { "sceneB",    "Scene B",                     K::integer, 1, 16, 2, 1, nullptr, false, "" },
        { "tempo",     "Tempo",                       K::linear,  40, 300, 120, 1, nullptr, false, "bpm" },

        { "engine",    "Engine",                      K::choice,  0, 3, 0, 1, "SYNTH|DRUM|SAMPLER|RADIO", false, "" },
        { "oaBank",    "Bank",                        K::choice,  0, 1, 0, 1, "BASIC|CAPTURED", false, "" },
        { "oaPos",     "Position",                    K::linear,  0, 1, 0.2857f, 1, nullptr, true, "%" },
        { "oaOct",     "Octave",                      K::integer, -3, 3, 0, 1, nullptr, false, "oct" },
        { "oaSemi",    "Transpose",                   K::integer, -12, 12, 0, 1, nullptr, false, "st" },
        { "oaFine",    "Fine",                        K::linear,  -100, 100, 0, 1, nullptr, true, "ct" },
        { "oaLevel",   "Level",                       K::linear,  0, 1, 0.8f, 1, nullptr, true, "%" },
        { "obBank",    "Bank",                        K::choice,  0, 1, 0, 1, "BASIC|CAPTURED", false, "" },
        { "obPos",     "Position",                    K::linear,  0, 1, 0.4286f, 1, nullptr, true, "%" },
        { "obOct",     "Octave",                      K::integer, -3, 3, -1, 1, nullptr, false, "oct" },
        { "obSemi",    "Transpose",                   K::integer, -12, 12, 0, 1, nullptr, false, "st" },
        { "obFine",    "Fine",                        K::linear,  -100, 100, 4, 1, nullptr, true, "ct" },
        { "obLevel",   "Level",                       K::linear,  0, 1, 0.45f, 1, nullptr, true, "%" },
        { "pmBA",      "PM B \xe2\x86\x92 A",         K::power,   0, 1, 0, 2.0f, nullptr, true, "%" },
        { "subLevel",  "Sub",                         K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "noiseLevel","Noise",                       K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "unison",    "Unison",                      K::integer, 1, 4, 1, 1, nullptr, false, "" },
        { "detune",    "Detune",                      K::linear,  0, 1, 0.2f, 1, nullptr, true, "%" },
        { "width",     "Width",                       K::linear,  0, 1, 0.6f, 1, nullptr, true, "%" },
        { "loopXfade", "Crossfade",                   K::linear,  0, 1, 0, 1, nullptr, false, "%" },
        { "wtf",       "WTF?!",                       K::linear,  0, 1, 0, 1, nullptr, false, "%" },
        { "wtfMode",   "WTF?! mode",                  K::choice,  0, 1, 0, 1, "LIVE|LOOP", false, "" },

        { "orOn",      "Orbite",                      K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "orAlgo",    "Algo",                        K::choice,  0, 3, 0, 1, "PARALLEL|CASCADE|FM|LOOP", false, "" },
        { "orShape",   "Shape",                       K::linear,  0, 2, 1, 1, nullptr, true, "shp" },
        { "orSym",     "Symmetry",                    K::linear,  -1, 1, 0, 1, nullptr, true, "sym" },
        { "orOct",     "Octave",                      K::integer, -3, 3, 0, 1, nullptr, false, "oct" },
        { "orSemi",    "Transpose",                   K::integer, -12, 12, 0, 1, nullptr, false, "st" },
        { "orOrbit",   "Rotation",                    K::power,   0, 12, 0.6f, 3.0f, nullptr, true, "orb" },
        { "orTrack",   "Key track",                   K::toggle,  0, 1, 1, 1, nullptr, false, "" },
        { "s1Wave",    "Wave",                        K::choice,  0, 2, 0, 1, "TRI|SAW|SQR", false, "" },
        { "s1Ratio",   "Ratio",                       K::log,     0.25f, 16, 2, 1, nullptr, true, "x" },
        { "s1Sym",     "Symmetry",                    K::linear,  -1, 1, 0, 1, nullptr, true, "sym" },
        { "s1Ring",    "Ring",                        K::linear,  0, 1, 0.6f, 1, nullptr, true, "%" },
        { "s1Sync",    "Sync",                        K::toggle,  0, 1, 1, 1, nullptr, false, "" },
        { "s2Wave",    "Wave",                        K::choice,  0, 2, 1, 1, "TRI|SAW|SQR", false, "" },
        { "s2Ratio",   "Ratio",                       K::log,     0.25f, 16, 3, 1, nullptr, true, "x" },
        { "s2Sym",     "Symmetry",                    K::linear,  -1, 1, 0, 1, nullptr, true, "sym" },
        { "s2Ring",    "Ring",                        K::linear,  0, 1, 0.35f, 1, nullptr, true, "%" },
        { "s2Sync",    "Sync",                        K::toggle,  0, 1, 1, 1, nullptr, false, "" },
        { "s2Phase",   "Phase",                       K::linear,  0, 1, 0, 1, nullptr, true, "deg" },
        { "mxSample",  "Sample",                      K::linear,  0, 1, 1, 1, nullptr, true, "%" },
        { "mxOrbite",  "Orbite",                      K::linear,  0, 1, 0.7f, 1, nullptr, true, "%" },
        { "tune",      "Tune",                        K::linear,  -96, 96, 0, 1, nullptr, true, "tun" },
        { "subOn",     "Sub",                         K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "subMode",   "Source",                      K::choice,  0, 1, 0, 1, "SINE|OCTAVE", false, "" },
        { "subOct",    "Octave",                      K::choice,  0, 1, 0, 1, "-1 OCT|-2 OCT", false, "" },
        { "subXover",  "Crossover",                   K::log,     40, 250, 100, 1, nullptr, true, "Hz" },
        { "subReplace","Replace",                     K::linear,  0, 1, 0.5f, 1, nullptr, true, "%" },
        { "subDrive",  "Drive",                       K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "subFollow", "Follow",                      K::toggle,  0, 1, 1, 1, nullptr, false, "" },
        { "mxSub",     "Sub",                         K::linear,  0, 1, 0.7f, 1, nullptr, true, "%" },
        { "rdRx",      "Receiver",                    K::choice,  0, 3, 0, 1, "RX 1|RX 2|RX 3|RX 4", false, "" },
        { "rdMode",    "Mode",                        K::choice,  0, 2, 0, 1, "LIVE|LOOP|DIG", false, "" },
        { "rdLen",     "Length",                      K::choice,  0, 9, 7, 1, "1/16|1/8|3/16|1/4|3/8|1/2|3/4|1 BAR|1.5 BAR|2 BARS", false, "" },
        { "rdSquelch", "Squelch",                     K::linear,  0, 1, 0, 1, nullptr, true, "rsq" },
        { "rdGrab",    "Grab",                        K::linear,  0, 1, 0.25f, 1, nullptr, true, "rgb" },
        { "rdDepth",   "Depth",                       K::log,     1, 20, 6, 1, nullptr, true, "s" },
        { "rdTune",    "Tune",                        K::linear,  -24, 24, 0, 1, nullptr, true, "tun" },
        { "rdStatic",  "Static",                      K::linear,  0, 1, 0.12f, 1, nullptr, true, "%" },
        { "rdLevel",   "Level",                       K::linear,  0, 1, 0.8f, 1, nullptr, true, "%" },
        { "rdBand",    "Band",                        K::linear,  0, 1, 0, 1, nullptr, true, "rbw" },
        { "rdCentre",  "Centre",                      K::log,     300, 3000, 1000, 1, nullptr, true, "Hz" },
        { "rdFade",    "Fading",                      K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "rdRate",    "Rate",                        K::log,     0.04f, 4, 0.3f, 1, nullptr, true, "Hz" },
        { "rdWhistle", "Whistle",                     K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "rdCrackle", "Crackle",                     K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "rdDrive",   "Drive",                       K::linear,  0, 1, 0, 1, nullptr, true, "%" },
        { "rdTake",    "Take",                        K::choice,  0, 5, 3, 1, "1 S|2 S|4 S|8 S|16 S|30 S", false, "" },
        { "rdSens",    "Sensitivity",                 K::linear,  0, 1, 0.5f, 1, nullptr, false, "%" },
        { "rdPads",    "Pads",                        K::choice,  0, 2, 2, 1, "4|8|16", false, "" },
        { "rdPadTune", "Tune",                        K::linear,  -24, 24, 0, 1, nullptr, true, "tun" },
        { "rdPadDecay","Decay",                       K::linear,  0, 1, 1, 1, nullptr, true, "rdc" },
        { "rdChoke",   "Choke",                       K::toggle,  0, 1, 0, 1, nullptr, false, "" },
        { "rdPadLevel","Level",                       K::linear,  0, 1, 0.8f, 1, nullptr, true, "%" },
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
        && id != voices && id != voiceMode && id != engine && id != wtfMode;
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
        return { "OFF", "ON" };
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
    if (u == "st")  return (v > 0.0f ? "+" : "") + fmt ("%.0f ST", v);
    if (u == "ct")  return (v > 0.0f ? "+" : "") + fmt ("%.0f CT", v);
    if (u == "oct") return (v > 0.0f ? "+" : "") + fmt (d.kind == Kind::integer ? "%.0f OCT" : "%.1f OCT", d.kind == Kind::integer ? v : v * 6.0f);
    if (u == "x")   return fmt ("%.2f X", v);
    if (u == "bit") return fmt (v < 10.0f ? "%.1f BITS" : "%.0f BITS", v);
    if (u == "bpm") return fmt ("%.1f BPM", v);
    if (u == "smp") return fmt ("%.0f SMP", v);
    if (u == "spc") return hzText (40.0 * std::pow (250.0, (double) v));
    if (u == "rv")  return v >= 0.97f ? std::string ("FREEZE") : msText (1000.0 * 0.3 * std::pow (100.0, (double) v / 0.97));
    if (u == "shp")
    {
        if (v < 0.02f)  return "TRI";
        if (std::abs (v - 1.0f) < 0.02f) return "SAW";
        if (v > 1.98f)  return "SQUARE";
        return v < 1.0f ? fmt ("TRI/SAW %.0f %%", v * 100.0f) : fmt ("SAW/SQR %.0f %%", (v - 1.0f) * 100.0f);
    }
    if (u == "sym") return std::abs (v) < 0.005f ? std::string ("0 %") : fmt ("%+.0f %%", v * 100.0f);
    if (u == "orb") return v < 0.005f ? std::string ("STILL") : hzText (v);
    if (u == "deg") return fmt ("%.0f\xc2\xb0", v * 360.0f);
    if (u == "tun") return std::abs (v) < 0.005f ? std::string ("0.00 ST") : fmt ("%+.2f ST", v);
    if (u == "rsq") return v < 0.005f ? std::string ("OFF") : fmt ("%.0f DB", -66.0 + 56.0 * v);
    if (u == "rgb") return v < 0.005f ? std::string ("KEEP") : fmt ("%.0f %%", v * 100.0f);
    if (u == "rbw") return v < 0.01f ? std::string ("FULL") : hzText (12000.0 * std::pow (250.0 / 12000.0, (double) v));
    if (u == "rdc") return v >= 0.99f ? std::string ("HOLD") : msText (1000.0 * 0.02 * std::pow (400.0, (double) v));
    if (u == "s")   return fmt (v < 10.0f ? "%.1f S" : "%.0f S", v);
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
    if (id >= m1Type && id < xfade)
        return "M" + std::to_string ((id - m1Type) / modSettings + 1) + " " + name;
    if (in (attack, release))      return "Amp " + name;
    if (in (fModel, quality))      return "Filter " + name;
    if (in (fEnv, velCutoff))      return "Filter env " + name;
    if (in (dModel, dMix))         return "Drive " + name;
    if (in (spVoices, spMix))      return "Spectre " + name;
    if (in (piA, piMix))           return "Pitch " + name;
    if (in (chMode, chMix))        return "Chorus " + name;
    if (in (crBits, crMix))        return "Crush " + name;
    if (in (rvSize, rvMix))        return "Espace " + name;
    if (in (pmRatio, pmIndex))     return name;
    if (in (oaBank, oaLevel))      return "Osc A " + name;
    if (in (obBank, obLevel))      return "Osc B " + name;
    if (in (subLevel, width))      return "Synth " + name;
    if (id == loopXfade)           return "Loop " + name;
    if (in (orAlgo, orTrack))      return "Orbite " + name;
    if (in (s1Wave, s1Sync))       return "Orbite sat 1 " + name;
    if (in (s2Wave, s2Phase))      return "Orbite sat 2 " + name;
    if (in (mxSample, mxOrbite))   return "Mixer " + name;
    if (id == tune)                return "Sampler " + name;
    if (in (subMode, subFollow))   return "Sub " + name;
    if (id == mxSub)               return "Mixer Sub";
    if (in (rdRx, rdDrive))        return "Radio " + name;
    if (in (rdTake, rdPads))       return "Radio " + name;
    if (in (rdPadTune, rdPadLevel)) return "Radio Pads " + name;
    return name;
}
}
