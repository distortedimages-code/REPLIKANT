// Plays an instrument, and mangles it.
//
//   the voices   POLY (up to 32), MONO (each note starts again) or LEGATO (the note glides on); each voice reads its
//                sample as it is (the rendered keys untouched, a band-limited interpolation only where the pitch is
//                moved) or ELASTIC (grains: its speed and pitch apart, from frozen to backwards), then its own
//                analog filter (oversampled) with its envelope, then its amplitude envelope
//   the chain    on everything (and on the audio input in FX mode): the drive, then SPECTRE, PITCH, CHORUS, CRUSH,
//                ESPACE, then the volume
//   the mods     four modulators on the tempo (the host's when it plays), each with up to eight routes to any
//                setting marked modulatable in Sound.h
//
// Audio thread only, apart from the settings (atomics: the plug-in's parameters, the routes, the steps).

#pragma once

#include <JuceHeader.h>
#include "Instrument.h"
#include "Sound.h"
#include "../dsp/VAFilter.h"
#include "../dsp/Drive.h"
#include "../dsp/Effects.h"
#include "../dsp/Modulators.h"
#include "../dsp/Cycles.h"
#include "../dsp/Orbite.h"

namespace dsp { class Polyphase; }

// a biquad (transposed direct form II), its coefficients set from outside
struct RkBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    inline float tick (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void clear()                   { z1 = z2 = 0.0f; }
};

#include <array>
#include <atomic>

class Player
{
public:
    Player();

    // the value of each setting: the plug-in's parameters (bind them before prepare), else the player's own
    void bind (int id, std::atomic<float>* value)    { values[(size_t) id] = value; }
    float get (int id) const                         { return values[(size_t) id]->load (std::memory_order_relaxed); }
    void set (int id, float v)                       { values[(size_t) id]->store (snd::legal (id, v)); }

    void prepare (double sampleRate, int maxBlock);
    bool safetyHighPass = true;          // the voices' DC / infrasound high pass (off: the tests read exact values)
    void setInstrument (const Instrument*);          // audio thread; the caller keeps it alive
    void allNotesOff (bool immediately);
    void resetEffects();

    struct Transport
    {
        bool playing = false;
        double ppq = 0.0, bpm = 120.0;
    };
    // adds the instrument through the chain to `out`
    // `input` is the audio input (what the followers listen to); it goes through the chain too when `inputThroughFx`
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Transport& transport,
                 const float* const* input = nullptr, int inputChannels = 0, bool inputThroughFx = false);

    //========================================================================== modulation
    struct Route
    {
        std::atomic<int> dest { -1 };
        std::atomic<float> amount { 0.0f };
    };
    std::array<std::array<Route, snd::routesPerMod>, snd::numMods> routes;
    std::array<std::array<std::atomic<float>, snd::maxSteps>, snd::numMods> steps;
    std::array<std::atomic<uint32_t>, snd::numMods> seeds;     // HASARD: its loop (a new seed, a new loop)

    //========================================================================== scenes (after the Octatrack's)
    // the value each scene holds for a setting (NaN: not held); the fader goes from scene A to scene B
    std::array<std::array<std::atomic<float>, snd::count>, snd::numScenes> scenes;
    void setSceneLock (int scene, int id, float value);        // NaN clears it
    float sceneLock (int scene, int id) const;
    bool sceneHolds (int scene) const;
    void clearScene (int scene);

    //========================================================================== the chain's order
    // the six stages (0 drive, 1 SPECTRE, 2 PITCH, 3 CHORUS, 4 CRUSH, 5 ESPACE) in the order they run; a new order
    // is taken through a 10 ms dip of the chain's output (no click)
    static constexpr int numStages = 6;
    void setFxOrder (const std::array<int, numStages>&);      // ignored unless a permutation
    std::array<int, numStages> fxOrder() const;

    // FOLLOW: what each of its sources plays, set by the host before each render (valid for that render's frames;
    // nullptr: silent). 0 INPUT (the audio input), 1 SELF (the instrument: its voices, or what the DRUM or the RADIO
    // plays), 2 SIDECHAIN, 3.. DRUM 1..8 (each track alone). Without INPUT, the followers hear `input` (render's)
    static constexpr int numFollowSources = 11;
    void setFollowSignal (int source, const float* left, const float* right = nullptr)
    {
        if (source >= 0 && source < numFollowSources)
            follow[(size_t) source] = { left, right };
    }

    // message thread: the route of mod m to `dest` set to `amount` (made if needed, removed at 0); false when the
    // mod has no room left
    bool setRoute (int mod, int dest, float amount);
    float routeAmount (int mod, int dest) const;
    void clearRoutes (int mod);
    // where a setting is now, modulated and moved by the scenes (0..1 of its range), or < 0 when nothing moves it
    float modulatedNorm (int id) const;

    //========================================================================== RÉÉCHANTILLONNER
    // What comes out of the chain (before the volume) recorded, then played back in place of what was playing: the
    // live sound fades out as the recording comes in from its start (20 ms), the chain is then made neutral (filter,
    // drive, effects, WTF?! off, their tails cleared, the voices stopped) and the recording goes on looping into it,
    // as the input would: heard the same, nothing processed twice, no gap. Its end is crossfaded into its start with
    // the 10 ms recorded past it, so it loops cleanly (in the sampler as well).
    // The message thread arms it (the room made there), takes the recording once ready, sets the sound to match
    // (effects at zero, the sampler) and then lets the chain go (releaseNeutral).
    enum class Resample { idle = 0, armed, recording, handing, looping };
    // bars: its length (0: until stopResample, or the room); synced: from the next bar when the transport plays
    bool armResample (double maxSeconds, int bars, bool synced);    // message thread; false unless idle
    void stopResample();             // armed: cancelled; recording: ends now; looping: fades out
    Resample resampleState() const   { return (Resample) rsState.load(); }
    float resampleProgress() const;  // while recording, 0..1
    double resampleSeconds() const   { return (double) rsWritten.load() / sr; }
    bool resampleReady() const       { return rsReady.load(); }
    rk::Channels takeResample();     // message thread, once ready (the loop's length, crossfaded); clears ready
    std::atomic<bool> neutral { false };            // the chain held neutral (set at the handover)
    void releaseNeutral()            { neutral = false; }
    bool loopsOnNoteOn = true;       // looping: a note played takes over (the loop fades out)

    // the output's guard: how many times the engine had to be reset (something not a number came out)
    std::atomic<int> rescues { 0 };
    static constexpr float synthLevel = 0.25f;        // the SYNTHÉ's voices, before the filter

    //========================================================================== for the editor
    std::array<std::atomic<float>, snd::numMods> modValue {};   // what each sends (after its depth)
    std::array<std::atomic<int>, snd::numMods> modStep {};
    std::array<std::atomic<float>, snd::numMods> modPhase {};   // where each wave is, over two of its cycles (0..2: the screen draws two)
    std::atomic<int> shownSample { -1 };
    std::atomic<float> shownPosition { 0.0f };       // in the sample's frames
    std::atomic<int> activeVoices { 0 };
    std::array<std::atomic<bool>, 128> keyDown {};
    std::atomic<float> filterEnv { 0.0f };            // the newest voice's filter envelope (0..1)
    static constexpr int maxGrains = 6;
    std::array<std::atomic<float>, maxGrains> grainAt {};   // the newest voice's grains (frames), < 0 = none
    static constexpr int maxPeaks = 33;
    std::array<std::atomic<float>, maxPeaks> peakHz {}, peakLevel {};
    std::atomic<float> outLevel { 0.0f };
    static constexpr int scopeSize = 4096;                       // the output's last samples (the scope)
    std::array<std::atomic<float>, scopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::array<std::atomic<float>, snd::count> shownNorm {};    // each setting as it is now (0..1)
    std::array<std::atomic<bool>, snd::count> shownMoved {};    // moved by a scene or a modulator

private:
    struct Env
    {
        enum Stage { idle, attack, decay, sustain, release };
        Stage stage = idle;
        float v = 0.0f;
        void on()        { stage = attack; }
        void off()       { if (stage != idle) stage = release; }
    };
    struct EnvRates
    {
        float attackStep = 1.0f, decayCoef = 0.0f, sustain = 1.0f, releaseCoef = 0.0f;
    };
    static float tick (Env&, const EnvRates&, bool amp);

    struct Grain
    {
        bool on = false;
        double pos = 0.0;
        float phase = 0.0f, inc = 0.0f;
    };

    // how a voice reads its sample during a chunk (its loop as moved, its direction, its character)
    struct Read
    {
        const rk::Sample* s = nullptr;
        const std::array<rk::Channels, Instrument::octaveCopies>* octaves = nullptr;   // the recording's octave copies (none for a rendered key)
        int level = 0;                                        // 0: the sample itself; 1..8: its copy that many octaves down
        float mixNext = 0.0f;                                 // how much of the next copy down (the crossfade between two)
        int len = 0;
        bool stereo = false, loop = false, moved = false;    // moved: the loop isn't the one found (splices smoothed)
        int ls = 0, le = 0;
        double step = 1.0;                                    // < 0: backwards
        float alias = 0.0f, bitsQ = 0.0f;                     // bitsQ: 2^(bits - 1), 0 = untouched
        const dsp::Polyphase* bank = nullptr;                // its kernels (null: the voices' 128 taps)
        int pm = 0;                                           // 0 none, 1 sine, 2 its own sample, 3 the input, 4 itself
        float pmIndex = 0.0f;
        double pmInc = 0.0;
    };

    struct Voice
    {
        bool active = false, held = false, fading = false;
        int note = 0, origin = 0, velocity = 0, index = -1, transposeAtOn = 0;
        const rk::Sample* s = nullptr;
        double pos = 0.0, baseShift = 0.0, rateScale = 1.0;
        float pitch = 0.0f, pitchTarget = 0.0f;          // the note it plays, gliding (semitones)
        float gain = 1.0f, fade = 1.0f, startAtOn = 0.0f;
        bool raw = false;                                // plays the recording itself (ALIASING up at its start)
        // a splice being smoothed: the old position goes on, fading out, while the new one fades in
        double ghost = 0.0;
        int ghostLeft = 0;
        // TRANSITOIRES GARDÉS: the attack read at its own speed while the pitched body takes over
        double attackPos = 0.0;
        int attackLeft = 0, attackFade = 0;
        double pmPhase = 0.0, pmPos = 0.0;                 // the modulator: a sine's phase, a second head in the sample
        double pmDisp = 0.0;                               // the modulation's push at the last sample (samples)
        float lastOut = 0.0f;                              // the voice's last sample (RETOUR)
        // ORBITE: its oscillators, and how far satellite 2's restart has circled (cycles, unwrapped)
        dsp::Orbite orb;
        double orbit = 0.0;
        // SUB: the sine's phase, the follower, the crossover (the sample split, the sub kept low, the infra taken
        // away), OCTAVE's two heads reading the sample slower
        using Biquad = RkBiquad;
        struct Sub
        {
            double phase = 0.0;
            float env = 0.0f;
            Biquad lo[2][2], hi[2][2];     // the sample's split (left, right), each a Linkwitz-Riley pair
            Biquad band[2], infra[2];      // the sub: kept under the crossover, then over 22 Hz
            struct Head { bool on = false; double pos = 0.0; float phase = 0.0f, inc = 0.0f; };
            Head heads[3];                 // (two playing, the third free as the oldest ends)
            float sinceHead = 1.0e9f;
            float xoverNow = -1.0f;
            void clear()
            {
                phase = 0.0;
                env = 0.0f;
                for (auto& c : lo) for (auto& b : c) b.clear();
                for (auto& c : hi) for (auto& b : c) b.clear();
                for (auto& b : band) b.clear();
                for (auto& b : infra) b.clear();
                for (auto& h : heads) h.on = false;
                sinceHead = 1.0e9f;
                xoverNow = -1.0f;
            }
        };
        Sub sub;
        Env amp, flt;
        // elastic
        double scan = 0.0;
        float sinceGrain = 1.0e9f;
        std::array<Grain, maxGrains> grains;
        uint32_t rng = 1;
        // filter
        dsp::VAFilter filter[2];
        dsp::Oversampler os[2];
        // where the last chunk left the cutoff, its envelope's depth and the step: the next chunk ramps from there
        // a sample at a time (no stairs every 32 samples)
        bool fresh = true;
        float cutFrom = 0.0f, envFrom = 0.0f;
        // (0.4.5) the levels of the mix (oscillators, sub, noise, ORBITE; the sample's), where the last chunk left them
        std::array<float, 5> lvFrom {};
        bool lvFresh = true;
        double stepFrom = 1.0;
        // SYNTHÉ: the oscillators' phases (each unison voice its own), the sub's; where the last chunk left their
        // increments and positions
        bool synth = false;
        std::array<double, 4> phA {}, phB {};
        double phSub = 0.0, incAFrom = 0.0, incBFrom = 0.0;
        float posAFrom = 0.0f, posBFrom = 0.0f, pmFrom = 0.0f;
        uint64_t age = 0;
        int originalLength = 0;
    };

    struct Chunk;     // the settings of a few samples (modulated)
    void noteOn (int note, int velocity, const Chunk&);
    void noteOff (int note, const Chunk&);
    void startVoice (int note, int velocity, float fromPitch, const Chunk&);
    void renderVoice (Voice&, float* L, float* R, int n, const Chunk&);
    void renderSynth (Voice&, float* L, float* R, int n, const Chunk&);
    const dsp::CycleBank& bankFor (int choice) const;
    // speed: how many samples of the recording go by per sample played, right now (the step, and the phase
    // modulation's push): the kernel's cutoff follows it
    void readFrame (const Read&, double pos, double speed, float& left, float& right) const;
    double alignGrain (const Voice&, const Read&, double target, double search) const;
    void prepareRead (Voice&, const Chunk&, Read&) const;
    Voice* freeVoice();
    void limitPolyphony (int keep);
    void modulate (int offset, int n, const Transport&, Chunk&, bool atRest);
    void runChain (float* L, float* R, int n, const Chunk&);
    void setQuality (int factor);

    static constexpr int maxVoices = 12;   // 8 playing, and room for those fading out
    std::array<Voice, maxVoices> voices;
    const Instrument* ins = nullptr;
    std::shared_ptr<const dsp::CycleBank> basics = dsp::CycleBank::basics();
    double sr = 48000.0;
    int quality = 0;                     // the voices' oversampling factor now
    double bend = 0.0;                   // -1..1
    bool sustain = false;
    std::array<bool, 128> sustained {};
    std::array<int, 128> rrCounter {};
    uint64_t clock = 0;
    float lastPitch = -1.0f;             // the last note played (the glide starts there)
    std::vector<int> stack;              // MONO / LEGATO: the notes held, the newest last

    // settings
    std::array<std::atomic<float>, snd::count> own;
    std::array<std::atomic<float>*, snd::count> values;
    // the continuous settings glide (~12 ms, in their own scale) when they move while something sounds: a knob, an
    // automation, a block's jump; at rest (nothing playing) they are taken as they are
    float smoothed (int id, float k, bool snap);
    std::array<float, snd::count> glideRaw {}, glideNorm {};
    std::array<bool, snd::count> gliding {};
    bool glidePrimed = false;
    float inCutFrom = -1.0f;             // the input filter's cutoff (FX mode), ramped like the voices'
    // WTF?!: what it pulls each setting by (drawn every eighth note, glided to), the draw's step
    static constexpr int wtfCount = 10;
    std::array<float, wtfCount> wtfTarget {}, wtfNow {};
    int64_t wtfStep = -1;
    uint32_t wtfRng = 0x7f4a7c15u;
    void applyWtf (double ppq, int n, Chunk&);
public:
    // WTF?!'s pulls now (normalised, its amount in them): the DRUM takes them for its hits
    const float* wtfPulls() const        { return wtfNow.data(); }
    static constexpr int numWtfPulls = wtfCount;
private:

    // modulators
    std::array<dsp::Modulator, snd::numMods> mods;
    std::array<double, snd::numMods> retrigPpq {};
    double freePpq = 0.0;                // the modulators' own position when the host stands still
    bool retrigPending[snd::numMods] {};
    bool gateOn[snd::numMods] {};
    std::array<uint32_t, snd::numMods> seedNow {};
    float inputLevel = 0.0f, voiceLevel = 0.0f;   // what the followers follow (the last chunk's)
    struct FollowSignal { const float* l = nullptr; const float* r = nullptr; };
    std::array<FollowSignal, numFollowSources> follow {};
    std::array<float, numFollowSources> followLevel {};   // this chunk's
    void hearFollowers (int pos, int end);

    // the chain
    dsp::VAFilter inFilter[2];
    dsp::Oversampler inOs[2];
    dsp::Drive drive;
    dsp::Spectral spectral;
    dsp::PitchShift pitch;
    dsp::Chorus chorus;
    dsp::Crush crush;
    dsp::Space space;
    float wet[6] {};                     // drive and the five effects, faded in and out
    static constexpr int defaultOrder = 0 + 1 * 6 + 2 * 36 + 3 * 216 + 4 * 1296 + 5 * 7776;   // stage at position p: digit p (base 6)
    std::atomic<int> orderWanted { defaultOrder };
    int orderNow = defaultOrder, orderPhase = 0;    // 1: fading out to change, 2: fading back in
    float orderGain = 1.0f;
    int lastDriveModel = 1;
    float volumeGain = 1.0f;
    juce::AudioBuffer<float> bus, dry;

    // RÉÉCHANTILLONNER
    void resampleChunk (float* L, float* R, int n, const Chunk&);   // in the chain, before the volume
    void loopIntoChain (float* L, float* R, int n);                   // looping: the recording as the input
    void neutralise();
    std::atomic<int> rsState { 0 };
    std::atomic<bool> rsReady { false }, rsStop { false };
    std::atomic<int> rsWritten { 0 };
    std::array<std::vector<float>, 2> rsBuf;
    int rsCapacity = 0, rsBars = 0, rsTarget = 0, rsTail = 0, rsFade = 0, rsFadePos = 0, rsRead = 0;
    bool rsSynced = false, rsHeadDone = false, wasNeutral = false;
    float rsGain = 1.0f;
    float dcState[2][2] {};              // the voices' safety high pass (DC, infrasound)
    int peakCounter = 0;
};
