// The player's sound: every setting of the voices, the filter, the drive, the effects and the modulators, in one
// table. The plug-in makes its automatable parameters from it (their ids never change: projects and automation
// keep them), the player reads them, the modulators move them, an instrument keeps them as its sound.
//
// Values are kept as they are shown (Hz, ms, dB...). A modulator moves a setting in its normalised range (0..1, the
// way its row moves), by its route's amount: a route of +0.5 on CUTOFF sweeps half the cutoff's range up.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snd
{
    enum Id
    {
        // playing
        volume, voiceMode, voices, glide, transpose, fine, bend, velocity, loops,
        // reading the sample: as it is, or elastic (grains); its character (after what the Assimil8or's manual
        // describes: aliasing, bits, reverse, phase modulation); its loop moved
        playMode, start, speed, grain, spray,
        alias, bits, reverse, loopStart, loopSize, pmSource, pmRatio, pmIndex, transients,
        // amplitude envelope
        attack, decay, sustain, release,
        // filter
        fModel, fType, cutoff, reso, fDrive, sep, fm, quality,
        fEnv, fAttack, fDecay, fSustain, fRelease, keyTrack, velCutoff,
        // drive
        dModel, dAmt, dTone, dMix,
        // effects
        spOn, spVoices, spSlice, spBlur, spGlide, spCentre, spWidth, spOct, spWave, spFreeze, spFeedback, spMix,
        piOn, piA, piB, piDetune, piDelay, piLevelB, piFeedback, piCrystals, piMix,
        chOn, chMode, chRate, chDepth, chFeedback, chTone, chMix,
        crOn, crBits, crRate, crJitter, crTone, crMix,
        rvOn, rvSize, rvDecay, rvTone, rvShimmer, rvMod, rvMix,
        // the four modulators (the same seventeen settings each, in this order: see ModSetting)
        m1Type, m1Shape, m1Sync, m1Div, m1Hz, m1Smooth, m1Polarity, m1Retrig, m1Len, m1Vari, m1Attack, m1Decay, m1Sustain, m1Release, m1Source, m1Gain, m1Amt,
        m2Type, m2Shape, m2Sync, m2Div, m2Hz, m2Smooth, m2Polarity, m2Retrig, m2Len, m2Vari, m2Attack, m2Decay, m2Sustain, m2Release, m2Source, m2Gain, m2Amt,
        m3Type, m3Shape, m3Sync, m3Div, m3Hz, m3Smooth, m3Polarity, m3Retrig, m3Len, m3Vari, m3Attack, m3Decay, m3Sustain, m3Release, m3Source, m3Gain, m3Amt,
        m4Type, m4Shape, m4Sync, m4Div, m4Hz, m4Smooth, m4Polarity, m4Retrig, m4Len, m4Vari, m4Attack, m4Decay, m4Sustain, m4Release, m4Source, m4Gain, m4Amt,
        // the scenes (after the Octatrack's): the fader between scene A and scene B
        xfade, sceneA, sceneB,
        // the tempo of the modulators and the clock when the host doesn't give one
        tempo,
        // (0.3) what the voices play: the SYNTHÉ's oscillators, the DRUM's pads, the SAMPLER's instrument (0.3.5: or
        // the RADIO)
        engine,
        // SYNTHÉ: two oscillators reading banks of cycles (the position morphs through the bank), B moving A's
        // phase, a sub an octave under A, noise, up to four detuned voices spread across the stereo
        oaBank, oaPos, oaOct, oaSemi, oaFine, oaLevel,
        obBank, obPos, obOct, obSemi, obFine, obLevel,
        pmBA, subLevel, noiseLevel, unison, detune, width,
        // SAMPLER: the loop's end crossfaded into what comes before its start
        loopXfade,
        // WTF?! (after IMPAKT's): the sound pulled away from itself, a new draw every eighth note (LIVE) or a loop
        // of eight draws coming back (LOOP); at 0, the sound as it is
        wtf, wtfMode,
        // (0.3.2) ORBITE (dsp/Orbite.h): a carrier and two satellites synced to it and ringing it, wired by an
        // algorithm, satellite 2's restart circling; and the MIXEUR before the filter: the sample's level and
        // ORBITE's (the oscillators, the sub and the noise keep theirs)
        orOn, orAlgo, orShape, orSym, orOct, orSemi, orOrbit, orTrack,
        s1Wave, s1Ratio, s1Sym, s1Ring, s1Sync,
        s2Wave, s2Ratio, s2Sym, s2Ring, s2Sync, s2Phase,
        mxSample, mxOrbite,
        // SAMPLER: the tuning dial, -96..+96 semitones, continuous (the reading engine's speed: up to 256x either way)
        tune,
        // (0.3.4) SAMPLER: the SUB, a clean low end under the sample: a sine on the note (SINE) or the sample itself
        // read an octave or two down by the reading engine, in grains kept on its time (OCTAVE); a crossover that can
        // take the sample's own (stretched) lows away; never under 25 Hz
        subOn, subMode, subOct, subXover, subReplace, subDrive, subFollow, mxSub,
        // (0.3.5) RADIO (radio/RadioCore.h): the receiver listened to and how (LIVE, LOOP, DIG), its air; the takes
        // (how far back GRAB goes), how they are cut into pads, and the pads
        rdRx, rdMode, rdLen, rdSquelch, rdGrab, rdDepth, rdTune, rdStatic, rdLevel,
        rdBand, rdCentre, rdFade, rdRate, rdWhistle, rdCrackle, rdDrive,
        rdTake, rdSens, rdPads, rdPadTune, rdPadDecay, rdChoke, rdPadLevel,
        // (0.3.6) RADIO: ON AIR (off: the radio not heard, the pads still played)
        rdOn,
        // (0.4) DRUM: the swing of its sequencer (every second sixteenth late)
        drSwing,
        // (0.4.2) SPECTRE: its slices (free and their multiplier, on the grid, on the notes, on the attacks), its band
        // or notch, TRANSIENTS, FINE and WARP on its pitches, how its oscillators follow the peaks
        spClock, spMult, spDiv, spNotch, spTrans, spFine, spWarp, spAssign,
        // (0.4.4) the filter's CUBE: which one, its Y and Z (its X is the cutoff)
        fCube, fY, fZ, fSmooth, fTrim,
        // (0.4.5) ESPACE's dry, apart from its wet (MIX); how many draws WTF?!'s LOOP goes through
        rvDry, wtfLen,
        // (0.4.5) ECHO: a tape loop and its four heads
        ecOn, ecTime, ecSync, ecHeads, ecSpread, ecFeedback, ecTone, ecDir, ecMotion, ecSweep, ecRate, ecWidth, ecWear, ecMix,
        // (0.4.5) COMP, at the end of the chain (SETTINGS)
        cpOn, cpIn, cpGate, cpAmount, cpAttack, cpRelease, cpKey, cpDrive, cpMix, cpLevel,
        count
    };

    constexpr int numMods = 4, routesPerMod = 8, maxSteps = 16, numScenes = 16;
    enum Engine { synth = 0, drum = 1, sampler = 2, radio = 3 };
    // what a scene can hold (everything but the scenes themselves and a few settings of the whole)
    bool sceneLockable (int id);
    enum ModSetting { mType, mShape, mSync, mDiv, mHz, mSmooth, mPolarity, mRetrig, mLen, mVari,
                      mAttack, mDecay, mSustain, mRelease, mSource, mGain, mAmt, modSettings };
    inline int modParam (int mod, ModSetting which)   { return m1Type + mod * modSettings + which; }

    enum class Kind { linear, log, power, integer, choice, toggle };

    struct Def
    {
        const char* id;          // the parameter's id (never changes)
        const char* name;        // UTF-8, shown
        Kind kind;
        float lo, hi, def;
        float shape;             // power: the exponent of the curve (value = lo + (hi - lo) * t^shape)
        const char* choices;     // choice: "A|B|C" (UTF-8)
        bool modulatable;
        const char* unit;        // for the host
    };

    const Def& def (int id);
    int find (const std::string& id);                    // -1 when unknown
    std::vector<std::string> choicesOf (int id);

    // the row's range: the value at t (0..1) and back
    float fromNorm (int id, float t);
    float toNorm (int id, float value);
    // a value made valid (in range, whole when it must be)
    float legal (int id, float value);
    // a value as the player shows it (the row's text)
    std::string text (int id, float value);
    // its name with its section (what a host shows, the routes and the scenes list): "Espace Mix", "M2 Forme"
    std::string longName (int id);
}
