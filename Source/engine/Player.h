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

    // message thread: the route of mod m to `dest` set to `amount` (made if needed, removed at 0); false when the
    // mod has no room left
    bool setRoute (int mod, int dest, float amount);
    float routeAmount (int mod, int dest) const;
    void clearRoutes (int mod);
    // where a setting is now, modulated and moved by the scenes (0..1 of its range), or < 0 when nothing moves it
    float modulatedNorm (int id) const;

    //========================================================================== for the editor
    std::array<std::atomic<float>, snd::numMods> modValue {};   // what each sends (after its depth)
    std::array<std::atomic<int>, snd::numMods> modStep {};
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
        const std::array<rk::Channels, 3>* octaves = nullptr;  // the recording's octave copies (none for a rendered key)
        int level = 0;                                        // 0: the sample itself; 1..3: its copy that many octaves down
        int len = 0;
        bool stereo = false, loop = false, moved = false;    // moved: the loop isn't the one found (splices smoothed)
        int ls = 0, le = 0;
        double step = 1.0;                                    // < 0: backwards
        float alias = 0.0f, bitsQ = 0.0f;                     // bitsQ: 2^(bits - 1), 0 = untouched
        int pm = 0;                                           // 0 none, 1 sine, 2 its own sample, 3 the input
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
        Env amp, flt;
        // elastic
        double scan = 0.0;
        float sinceGrain = 1.0e9f;
        std::array<Grain, maxGrains> grains;
        uint32_t rng = 1;
        // filter
        dsp::VAFilter filter[2];
        dsp::Oversampler os[2];
        uint64_t age = 0;
        int originalLength = 0;
    };

    struct Chunk;     // the settings of a few samples (modulated)
    void noteOn (int note, int velocity, const Chunk&);
    void noteOff (int note, const Chunk&);
    void startVoice (int note, int velocity, float fromPitch, const Chunk&);
    void renderVoice (Voice&, float* L, float* R, int n, const Chunk&);
    void readFrame (const Read&, double pos, float& left, float& right) const;
    double alignGrain (const Voice&, const Read&, double target, double search) const;
    void prepareRead (Voice&, const Chunk&, Read&) const;
    Voice* freeVoice();
    void limitPolyphony (int keep);
    void modulate (int offset, int n, const Transport&, Chunk&);
    void runChain (float* L, float* R, int n, const Chunk&);
    void setQuality (int factor);

    static constexpr int maxVoices = 12;   // 8 playing, and room for those fading out
    std::array<Voice, maxVoices> voices;
    const Instrument* ins = nullptr;
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

    // modulators
    std::array<dsp::Modulator, snd::numMods> mods;
    std::array<double, snd::numMods> retrigPpq {};
    double freePpq = 0.0;                // the modulators' own position when the host stands still
    bool retrigPending[snd::numMods] {};
    bool gateOn[snd::numMods] {};
    std::array<uint32_t, snd::numMods> seedNow {};
    float inputLevel = 0.0f, voiceLevel = 0.0f;   // what the followers follow (the last chunk's)

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
    int lastDriveModel = 1;
    float volumeGain = 1.0f;
    juce::AudioBuffer<float> bus, dry;
    float dcState[2][2] {};              // the voices' safety high pass (DC, infrasound)
    int peakCounter = 0;
};
