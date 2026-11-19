#include "Player.h"
#include "../core/Sinc.h"
#include "../dsp/Polyphase.h"

#include <cmath>
#include <limits>

namespace
{
    constexpr int chunkSize = 32;           // the settings (and the modulators) move every 32 samples
    constexpr float twoPi = 6.28318530718f;

    inline uint32_t xorshift (uint32_t& s)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    inline float uniform (uint32_t& s)      { return (float) ((double) xorshift (s) / 4294967295.0); }

    inline float coefFor (double ms, double sr)
    {
        // the per-sample factor that brings an exponential 60 dB down in `ms`
        return (float) std::exp (std::log (1.0e-3) / std::max (1.0, ms * 0.001 * sr));
    }
}

//==============================================================================
struct Player::Chunk
{
    float v[snd::count] {};
    EnvRates amp, flt;
    float glideK = 1.0f;
    float bendSemis = 0.0f;
    bool elastic = false, loops = true, oneShot = false, filterOn = false;
    dsp::FilterSettings fs;
    float cutoffOct = 7.0f, envOct = 0.0f;
    double ppq = 0.0, bpm = 120.0;       // where the chunk starts (RÉÉCHANTILLONNER waits for a bar)
    bool playing = false;
    const float* inL = nullptr;          // the audio input over this chunk (phase modulation)
    const float* inR = nullptr;
    int n = 0;
};

namespace
{
    // ORBITE over a chunk: its settings, and its frequencies for a note (semitones, as played)
    struct OrbitePlan
    {
        dsp::OrbiteSettings s;
        double incC = 0.0, inc1 = 0.0, inc2 = 0.0, orbitInc = 0.0;
    };
    OrbitePlan orbitePlan (const float* v, double played, double sr)
    {
        OrbitePlan p;
        p.s.algo = (int) v[snd::orAlgo];
        p.s.shape = v[snd::orShape];
        p.s.sym = v[snd::orSym];
        p.s.wave[0] = (int) v[snd::s1Wave];
        p.s.wave[1] = (int) v[snd::s2Wave];
        p.s.symS[0] = v[snd::s1Sym];
        p.s.symS[1] = v[snd::s2Sym];
        p.s.ring[0] = v[snd::s1Ring];
        p.s.ring[1] = v[snd::s2Ring];
        p.s.sync[0] = v[snd::s1Sync] > 0.5f;
        p.s.sync[1] = v[snd::s2Sync] > 0.5f;
        p.s.phase2 = v[snd::s2Phase];
        const double hz = rk::noteHz (played + 12.0 * (double) v[snd::orOct] + (double) v[snd::orSemi]);
        // SUIVI: the satellites at their ratio of the carrier, or of C3 whatever the key (fixed, like formants)
        const double ref = v[snd::orTrack] > 0.5f ? hz : rk::noteHz (60.0 + 12.0 * (double) v[snd::orOct] + (double) v[snd::orSemi]);
        p.incC = std::min (0.45, hz / sr);
        p.inc1 = std::min (0.45, ref * (double) v[snd::s1Ratio] / sr);
        p.inc2 = std::min (0.45, ref * (double) v[snd::s2Ratio] / sr);
        p.orbitInc = (double) v[snd::orOrbit] / sr;
        return p;
    }

    // second-order Butterworth sections (a Linkwitz-Riley crossover is two of them in a row)
    void butterworth (RkBiquad& q, double hz, double sr, bool high)
    {
        const double w = 2.0 * 3.14159265358979 * std::min (hz, sr * 0.45) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * 0.70710678);
        const double a0 = 1.0 + al;
        if (high)
        {
            q.b0 = (float) ((1.0 + c) * 0.5 / a0); q.b1 = (float) (-(1.0 + c) / a0); q.b2 = q.b0;
        }
        else
        {
            q.b0 = (float) ((1.0 - c) * 0.5 / a0); q.b1 = (float) ((1.0 - c) / a0); q.b2 = q.b0;
        }
        q.a1 = (float) (-2.0 * c / a0);
        q.a2 = (float) ((1.0 - al) / a0);
    }

    constexpr int spliceLength = 144;     // 3 ms at 48 kHz: a splice smoothed
}

Player::Player()
{
    dsp::Polyphase::prepareAll();      // the kernels' tables, before any audio
    for (int i = 0; i < snd::count; ++i)
    {
        own[(size_t) i] = snd::def (i).def;
        values[(size_t) i] = &own[(size_t) i];
    }
    for (auto& m : steps)
        for (int s = 0; s < snd::maxSteps; ++s)
            m[(size_t) s] = (s % 4 == 0) ? 1.0f : (s % 2 == 0 ? 0.0f : -0.5f);
    for (auto& g : grainAt)
        g = -1.0f;
    for (int m = 0; m < snd::numMods; ++m)
        seeds[(size_t) m] = 0x51A3u + 7919u * (uint32_t) m;
    for (auto& sc : scenes)
        for (auto& v : sc)
            v = std::numeric_limits<float>::quiet_NaN();
    stack.reserve (128);
}

void Player::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    const int size = std::max (maxBlock, 4096);
    bus.setSize (2, size, false, true, false);
    dry.setSize (2, size, false, true, false);
    quality = 0;
    setQuality (1 << (int) get (snd::quality));
    drive.prepare (sr);
    spectral.prepare (sr);
    pitch.prepare (sr);
    chorus.prepare (sr);
    crush.prepare (sr);
    space.prepare (sr);
    echo.prepare (sr);
    comp.prepare (sr);
    for (auto& w : wet)
        w = 0.0f;
    volumeGain = juce::Decibels::decibelsToGain (get (snd::volume));
    allNotesOff (true);
}

void Player::setQuality (int factor)
{
    if (factor == quality)
        return;
    quality = factor;
    for (auto& v : voices)
        for (int c = 0; c < 2; ++c)
        {
            v.os[c].prepare (factor);
            v.filter[c].prepare (sr * factor);
        }
    for (int c = 0; c < 2; ++c)
    {
        inOs[c].prepare (factor);
        inFilter[c].prepare (sr * factor);
    }
}

void Player::resetEffects()
{
    drive.reset();
    spectral.reset();
    pitch.reset();
    chorus.reset();
    crush.reset();
    space.reset();
    echo.reset();
    comp.reset();
}

void Player::setInstrument (const Instrument* i)
{
    if (i != ins)
    {
        allNotesOff (true);
        ins = i;
        rrCounter.fill (0);
    }
}

void Player::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        if (immediately)
            v.active = false;
        else if (v.active)
        {
            v.held = false;
            v.amp.off();
            v.flt.off();
        }
    }
    sustained.fill (false);
    stack.clear();
    for (auto& k : keyDown)
        k = false;
}

//============================================================================== routes
bool Player::setRoute (int mod, int dest, float amount)
{
    if (mod < 0 || mod >= snd::numMods || dest < 0 || dest >= snd::count)
        return false;
    auto& rs = routes[(size_t) mod];
    amount = juce::jlimit (-1.0f, 1.0f, amount);
    for (auto& r : rs)
        if (r.dest.load() == dest)
        {
            if (std::abs (amount) < 1.0e-4f)
            {
                r.amount = 0.0f;
                r.dest = -1;
            }
            else
                r.amount = amount;
            return true;
        }
    if (std::abs (amount) < 1.0e-4f)
        return true;
    for (auto& r : rs)
        if (r.dest.load() < 0)
        {
            r.amount = amount;
            r.dest = dest;
            return true;
        }
    return false;
}

float Player::routeAmount (int mod, int dest) const
{
    if (mod < 0 || mod >= snd::numMods)
        return 0.0f;
    for (auto& r : routes[(size_t) mod])
        if (r.dest.load() == dest)
            return r.amount.load();
    return 0.0f;
}

void Player::clearRoutes (int mod)
{
    if (mod < 0 || mod >= snd::numMods)
        return;
    for (auto& r : routes[(size_t) mod])
    {
        r.dest = -1;
        r.amount = 0.0f;
    }
}

float Player::modulatedNorm (int id) const
{
    if (id < 0 || id >= snd::count || ! shownMoved[(size_t) id].load (std::memory_order_relaxed))
        return -1.0f;
    return shownNorm[(size_t) id].load (std::memory_order_relaxed);
}

void Player::setSceneLock (int scene, int id, float value)
{
    if (scene < 0 || scene >= snd::numScenes || ! snd::sceneLockable (id))
        return;
    scenes[(size_t) scene][(size_t) id] = std::isnan (value) ? value : snd::legal (id, value);
}

float Player::sceneLock (int scene, int id) const
{
    if (scene < 0 || scene >= snd::numScenes || id < 0 || id >= snd::count)
        return std::numeric_limits<float>::quiet_NaN();
    return scenes[(size_t) scene][(size_t) id].load();
}

bool Player::sceneHolds (int scene) const
{
    if (scene < 0 || scene >= snd::numScenes)
        return false;
    for (auto& v : scenes[(size_t) scene])
        if (! std::isnan (v.load()))
            return true;
    return false;
}

void Player::clearScene (int scene)
{
    if (scene < 0 || scene >= snd::numScenes)
        return;
    for (auto& v : scenes[(size_t) scene])
        v = std::numeric_limits<float>::quiet_NaN();
}

//============================================================================== envelopes
float Player::tick (Env& e, const EnvRates& r, bool amp)
{
    switch (e.stage)
    {
        case Env::attack:
            e.v += r.attackStep;
            if (e.v >= 1.0f)
            {
                e.v = 1.0f;
                e.stage = Env::decay;
            }
            break;
        case Env::decay:
            e.v = r.sustain + (e.v - r.sustain) * r.decayCoef;
            if (std::abs (e.v - r.sustain) < 1.0e-4f)
            {
                e.v = r.sustain;
                e.stage = Env::sustain;
            }
            break;
        case Env::sustain:
            e.v = r.sustain;     // follows the knob
            break;
        case Env::release:
            e.v *= r.releaseCoef;
            if (e.v < (amp ? 1.0e-4f : 1.0e-5f))
            {
                e.v = 0.0f;
                e.stage = Env::idle;
            }
            break;
        case Env::idle:
        default:
            break;
    }
    return e.v;
}

//============================================================================== voices
Player::Voice* Player::freeVoice()
{
    for (auto& v : voices)
        if (! v.active)
            return &v;
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.fading && (best == nullptr || v.age < best->age))
            best = &v;
    if (best == nullptr)
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age)
                best = &v;
    return best;
}

void Player::limitPolyphony (int keep)
{
    for (;;)
    {
        int count = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.fading)
            {
                ++count;
                // released voices go first, then the oldest
                if (oldest == nullptr || (v.amp.stage == Env::release) > (oldest->amp.stage == Env::release)
                    || ((v.amp.stage == Env::release) == (oldest->amp.stage == Env::release) && v.age < oldest->age))
                    oldest = &v;
            }
        if (count <= keep || oldest == nullptr)
            return;
        oldest->fading = true;
    }
}

void Player::startVoice (int note, int velocity, float fromPitch, const Chunk& c)
{
    if ((int) c.v[snd::engine] == snd::synth)
    {
        // SYNTHÉ: the oscillators (the first unison voice from zero, the others anywhere: no flanging at the start)
        auto* vp = freeVoice();
        if (vp == nullptr)
            return;
        auto& v = *vp;
        v.synth = true;
        v.active = v.held = true;
        v.fading = false;
        v.fade = 1.0f;
        v.fresh = true;
    v.lvFresh = true;
        v.lvFresh = true;
        v.s = nullptr;
        v.index = -1;
        v.ghostLeft = v.attackLeft = 0;
        v.transposeAtOn = (int) c.v[snd::transpose];
        v.phA.fill (0.0);
        v.phB.fill (0.0);
        v.phSub = 0.0;
        v.orb.reset();
        v.orbit = 0.0;
        v.amp = {};
        v.flt = {};
        v.note = v.origin = note;
        v.velocity = velocity;
        v.pitch = fromPitch;
        v.pitchTarget = (float) note;
        v.age = ++clock;
        v.gain = std::pow (juce::jlimit (0.0f, 1.0f, (float) velocity / 127.0f), c.v[snd::velocity]);
        v.rng = 0x9E3779B9u ^ (uint32_t) (v.age * 2654435761u);
        for (int u = 1; u < 4; ++u)
        {
            v.phA[(size_t) u] = uniform (v.rng);
            v.phB[(size_t) u] = uniform (v.rng);
        }
        v.amp.on();
        v.flt.on();
        for (int ch = 0; ch < 2; ++ch)
        {
            v.filter[ch].set (c.fs);
            v.filter[ch].reset();
            v.os[ch].reset();
        }
        return;
    }
    if (ins == nullptr)
        return;
    const int tr = (int) c.v[snd::transpose];
    const int key = juce::jlimit (0, 127, note + tr);
    Instrument::Source srcs[16];
    // the character, and the attacks kept, need the varispeed: the recording itself
    const bool raw = c.v[snd::alias] > 0.001f || c.v[snd::transients] > 0.5f;
    const int found = ins->sourcesFor (key, velocity, srcs, 16, raw);
    if (found <= 0)
        return;
    const auto& src = srcs[rrCounter[(size_t) key]++ % found];
    if (src.sample == nullptr || src.sample->length() == 0)
        return;
    auto* vp = freeVoice();
    if (vp == nullptr)
        return;
    auto& v = *vp;
    v.synth = false;
    v.active = true;
    v.held = true;
    v.fading = false;
    v.fade = 1.0f;
    v.note = note;
    v.origin = note;
    v.velocity = velocity;
    v.index = src.index;
    v.s = src.sample;
    v.baseShift = src.semitones;
    v.transposeAtOn = tr;
    v.rateScale = src.sample->sampleRate / sr;
    v.pitch = fromPitch;
    v.pitchTarget = (float) note;
    v.age = ++clock;
    v.originalLength = ins->samples[(size_t) src.index].length();
    const float recorded = (float) juce::jmax (1, ins->samples[(size_t) src.index].velocity);
    v.gain = std::pow (juce::jlimit (0.0f, 1.0f, (float) velocity / recorded), c.v[snd::velocity]);
    v.raw = raw;
    v.startAtOn = c.v[snd::start];
    const int len = v.s->length();
    v.pos = std::floor ((double) v.startAtOn * (double) (len - 1));
    if (c.v[snd::reverse] > 0.5f)
        v.pos = (double) (len - 1) - v.pos;          // backwards: from the end
    v.scan = v.pos;
    v.ghostLeft = 0;
    v.pmPhase = 0.0;
    v.pmPos = v.pos;
    v.pmDisp = 0.0;
    v.lastOut = 0.0f;
    v.orb.reset();
    v.orbit = 0.0;
    v.sub.clear();
    v.attackLeft = 0;
    // TRANSITOIRES GARDÉS: the attack at its own speed, the pitched body taking over at its end
    if (c.v[snd::transients] > 0.5f && ! c.elastic && c.v[snd::reverse] < 0.5f && std::abs (v.baseShift) > 0.01)
    {
        // the attack alone until its end, then the pitched body from there while the attack head goes on, fading
        const int a = v.s->attackEnd;
        const double attackOut = ((double) a - v.pos) / v.rateScale;      // output samples it lasts
        const int fade = (int) (0.004 * sr);
        if (a > 0 && attackOut > 1.0)
        {
            v.attackPos = v.pos;
            v.attackLeft = (int) attackOut + fade;
            v.attackFade = fade;
            v.pos = (double) a;
        }
    }
    v.sinceGrain = 1.0e9f;
    v.fresh = true;
    for (auto& g : v.grains)
        g.on = false;
    v.rng = 0x9E3779B9u ^ (uint32_t) (v.age * 2654435761u);
    v.amp = {};
    v.flt = {};
    v.amp.on();
    v.flt.on();
    for (int ch = 0; ch < 2; ++ch)
    {
        v.filter[ch].set (c.fs);
        v.filter[ch].reset();
        v.os[ch].reset();
    }
}

void Player::noteOn (int note, int velocity, const Chunk& c)
{
    spectral.trigger();                  // (SPECTRE on NOTES: an analysis for each note)
    keyDown[(size_t) note] = true;
    sustained[(size_t) note] = false;
    const int mode = (int) c.v[snd::voiceMode];
    const bool glides = c.v[snd::glide] > 0.5f;
    for (int m = 0; m < snd::numMods; ++m)
        if (get (snd::modParam (m, snd::mRetrig)) > 0.5f || (int) get (snd::modParam (m, snd::mType)) == (int) dsp::ModType::envelope)
            retrigPending[m] = true;

    if (mode == 0)
    {
        limitPolyphony ((int) c.v[snd::voices] - 1);
        startVoice (note, velocity, glides && lastPitch >= 0.0f ? lastPitch : (float) note, c);
        lastPitch = (float) note;
        return;
    }
    // MONO / LEGATO: one voice, the newest note
    stack.erase (std::remove (stack.begin(), stack.end(), note), stack.end());
    stack.push_back (note);
    Voice* lead = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.fading && v.amp.stage != Env::release && (lead == nullptr || v.age > lead->age))
            lead = &v;
    if (mode == 2 && lead != nullptr)
    {
        lead->note = note;
        lead->pitchTarget = (float) note;
        if (! glides)
            lead->pitch = (float) note;
        lead->held = true;
    }
    else
    {
        const float from = glides ? (lead != nullptr ? lead->pitch : (lastPitch >= 0.0f ? lastPitch : (float) note)) : (float) note;
        for (auto& v : voices)
            if (v.active)
                v.fading = true;
        startVoice (note, velocity, from, c);
    }
    lastPitch = (float) note;
}

void Player::noteOff (int note, const Chunk& c)
{
    if (sustain)
    {
        sustained[(size_t) note] = true;
        return;
    }
    keyDown[(size_t) note] = false;
    const bool oneShot = c.oneShot;
    const int mode = (int) c.v[snd::voiceMode];
    if (mode != 0)
    {
        const bool wasNewest = ! stack.empty() && stack.back() == note;
        stack.erase (std::remove (stack.begin(), stack.end(), note), stack.end());
        Voice* lead = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.fading && v.held && (lead == nullptr || v.age > lead->age))
                lead = &v;
        if (lead == nullptr || ! wasNewest)
            return;
        if (! stack.empty())
        {
            // back to the note still held
            const int prev = stack.back();
            if (mode == 2)
            {
                lead->note = prev;
                lead->pitchTarget = (float) prev;
                if (c.v[snd::glide] <= 0.5f)
                    lead->pitch = (float) prev;
            }
            else
            {
                const float from = c.v[snd::glide] > 0.5f ? lead->pitch : (float) prev;
                const int vel = lead->velocity;
                lead->fading = true;
                startVoice (prev, vel, from, c);
            }
            lastPitch = (float) prev;
            return;
        }
        lead->held = false;
        if (! oneShot)
        {
            lead->amp.off();
            lead->flt.off();
        }
        return;
    }
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
        {
            v.held = false;
            if (! oneShot)
            {
                v.amp.off();
                v.flt.off();
            }
        }
}

void Player::prepareRead (Voice& v, const Chunk& c, Read& r) const
{
    const auto& s = *v.s;
    r.s = v.s;
    r.len = s.length();
    r.stereo = s.channels() > 1;
    r.loop = c.loops && s.loop.valid() && ! c.oneShot;
    r.moved = false;
    r.ls = s.loop.start;
    r.le = s.loop.end;
    if (r.loop)
    {
        // the loop moved and resized (its splice is then smoothed as it plays)
        const float size = c.v[snd::loopSize], shift = c.v[snd::loopStart];
        if (size < 0.999f || std::abs (shift) > 0.001f)
        {
            const int L0 = s.loop.length();
            const int L = juce::jlimit (64, juce::jmax (64, r.len - 1), (int) std::lround ((double) L0 * size));
            const int room = juce::jmax (0, r.len - 1 - L);
            int st = s.loop.start + (int) std::lround (shift > 0.0f ? (double) shift * (double) (room - s.loop.start)
                                                                     : (double) shift * (double) s.loop.start);
            st = juce::jlimit (0, room, st);
            r.ls = st;
            r.le = st + L;
            r.moved = true;
        }
    }
    // the pitch: glide, transposition moved since the note began, fine, bend
    const double semis = v.baseShift + (double) (v.pitch - (float) v.origin)
                       + (double) ((int) c.v[snd::transpose] - v.transposeAtOn)
                       + (double) c.v[snd::fine] * 0.01 + (double) c.bendSemis + (double) c.v[snd::tune];
    r.step = rk::ratioOf (semis) * v.rateScale * (c.v[snd::reverse] > 0.5f ? -1.0 : 1.0);
    // far up: the recording's copy an octave (two... eight) down, read at half (a quarter...) the speed. A copy
    // takes over at twice its speed, where what it keeps (flat to 0.45 of the recording's band) is what the kernel
    // would keep of the copy above; and just before, the two are crossfaded: a pitch swept up never steps
    r.octaves = nullptr;
    r.level = 0;
    r.mixNext = 0.0f;
    if (ins != nullptr && v.index >= 0 && (size_t) v.index < ins->octaves.size() && v.s == &ins->samples[(size_t) v.index])
    {
        r.octaves = &ins->octaves[(size_t) v.index];
        const double astep = std::abs (r.step);
        if (astep > 1.5)
        {
            const double x = std::log2 (astep / 2.0);               // the copies take over at whole x
            r.level = juce::jlimit (0, Instrument::octaveCopies, x <= 0.0 ? 0 : (int) std::ceil (x));
            constexpr double zone = 0.1047;                           // the crossfade: from 1.86x to 2x
            const double toNext = (double) r.level - x;
            if (r.level < Instrument::octaveCopies && toNext < zone)
                r.mixNext = (float) juce::jlimit (0.0, 1.0, 1.0 - toNext / zone);
        }
    }
    r.alias = c.v[snd::alias];
    const float bits = c.v[snd::bits];
    r.bitsQ = bits < 31.5f ? std::exp2 (bits - 1.0f) : 0.0f;
    r.pm = (int) c.v[snd::pmSource];
    r.pmIndex = c.v[snd::pmIndex];
    if (r.pmIndex < 0.01f || (r.pm == 3 && c.inL == nullptr))
        r.pm = 0;
    // the sine sits on the note played; the second head reads the sample at that ratio of its speed
    const double playing = (double) v.pitch + (double) (int) c.v[snd::transpose] + (double) c.v[snd::fine] * 0.01 + (double) c.bendSemis + (double) c.v[snd::tune];
    r.pmInc = rk::noteHz (playing) * (double) c.v[snd::pmRatio] / sr;
}

// One frame of the sample at `p` (both channels, one set of weights). ALIASING, after the scale the Assimil8or's
// manual describes (written here our way):
//   0             the band-limited sinc, its cutoff lowered as the pitch goes up (nothing folds back)
//   0 .. 25 %     the cutoff stops following the pitch upwards (aliasing creeps in when transposed up)
//   25 .. 36 %    a gentler, shorter kernel (the slope of the classic samplers' chips)
//   36 .. 51 %    towards linear interpolation
//   51 .. 89 %    towards drop sample (Mirage, SP-1200)
//   89 .. 100 %   past it: the interpolation turns high pass (nearest minus linear), the images louder than the sound
// Far up, it reads the recording's copy an octave (two, three) down, at half (a quarter...) the step.
void Player::readFrame (const Read& r, double p, double speed, float& left, float& right) const
{
    if (r.mixNext > 0.0f && r.octaves != nullptr && r.level < Instrument::octaveCopies)
    {
        // between two octave copies: both, crossfaded
        Read a = r, b = r;
        a.mixNext = b.mixNext = 0.0f;
        b.level = r.level + 1;
        float l0, r0, l1, r1;
        readFrame (a, p, speed, l0, r0);
        readFrame (b, p, speed, l1, r1);
        left = l0 + r.mixNext * (l1 - l0);
        right = r0 + r.mixNext * (r1 - r0);
        return;
    }
    const auto& s = *r.s;
    const rk::Channels& data = r.level > 0 ? (*r.octaves)[(size_t) (r.level - 1)] : s.audio;
    const double scale = r.level > 0 ? 1.0 / (double) (1 << r.level) : 1.0;
    const double pp = p * scale;
    const int len = (int) data[0].size();
    const float* d0 = data[0].data();
    const float* d1 = r.stereo && data.size() > 1 ? data[1].data() : nullptr;
    const bool viewLoop = r.loop && ! r.moved;
    const int ls = r.ls >> r.level, le = r.le >> r.level;
    const bool wraps = viewLoop && le > ls + 1;
    auto at = [&] (const float* d, int64_t k)
    {
        if (k < 0)
            return 0.0f;
        if (wraps && k >= le)
            k = ls + (k - ls) % (le - ls);
        return k < len ? d[k] : 0.0f;
    };
    const double ip = std::floor (pp);
    const double frac = pp - ip;
    // the speed met at this octave copy (the step, and what the phase modulation adds right now)
    const double astep = speed * scale;
    const float a = r.alias;
    // the band-limited read: the kernels' taps over the samples around p (gathered when they cross an edge or the
    // loop's end), one dot product per channel
    auto band = [&] (const dsp::Polyphase& B, double cutoff, float& o0, float& o1)
    {
        const int T = B.taps();
        const auto k0 = (int64_t) ip - T / 2 + 1;
        const auto k = B.pick (cutoff, frac);
        if (k0 >= 0 && k0 + T <= (wraps ? (int64_t) juce::jmin (le, len) : (int64_t) len))
        {
            if (d1 != nullptr)
                B.apply2 (d0 + k0, d1 + k0, k, o0, o1);
            else
            {
                o0 = B.apply (d0 + k0, k);
                o1 = 0.0f;
            }
            return;
        }
        float g0[dsp::Polyphase::maxTaps], g1[dsp::Polyphase::maxTaps];
        for (int j = 0; j < T; ++j)
            g0[j] = at (d0, k0 + j);
        if (d1 != nullptr)
        {
            for (int j = 0; j < T; ++j)
                g1[j] = at (d1, k0 + j);
            B.apply2 (g0, g1, k, o0, o1);
        }
        else
        {
            o0 = B.apply (g0, k);
            o1 = 0.0f;
        }
    };
    const auto& bank = r.bank != nullptr ? *r.bank : dsp::Polyphase::hq();
    const double top = bank.top();
    // the cutoff at that speed: the kernel's stop band begins where the output's Nyquist falls in the recording
    // (its transition, 1 - top wide on each side, kept under it)
    const double clean = juce::jlimit (0.05, top, 1.0 / juce::jmax (1.0e-6, astep) - (1.0 - top));
    auto linear = [&] (float& o0, float& o1)
    {
        const auto k = (int64_t) ip;
        o0 = at (d0, k) + (float) frac * (at (d0, k + 1) - at (d0, k));
        o1 = d1 != nullptr ? at (d1, k) + (float) frac * (at (d1, k + 1) - at (d1, k)) : 0.0f;
    };
    auto drop = [&] (float& o0, float& o1)
    {
        o0 = at (d0, (int64_t) ip);
        o1 = d1 != nullptr ? at (d1, (int64_t) ip) : 0.0f;
    };
    auto blend = [] (float& a0, float& a1, float b0, float b1, float t) { a0 += t * (b0 - a0); a1 += t * (b1 - a1); };
    float o0 = 0.0f, o1 = 0.0f;
    if (a <= 0.0f)
    {
        if (r.level == 0 && frac < 1.0e-9 && std::abs (astep - 1.0) < 1.0e-9)
            drop (o0, o1);
        else
            band (bank, clean, o0, o1);
    }
    else if (a <= 0.25f)
        band (bank, top * std::pow (clean / top, 1.0 - (double) a / 0.25), o0, o1);
    else if (a <= 0.36f)
    {
        float b0, b1;
        band (bank, top, o0, o1);
        band (dsp::Polyphase::soft(), 0.95, b0, b1);
        blend (o0, o1, b0, b1, (a - 0.25f) / 0.11f);
    }
    else if (a <= 0.51f)
    {
        float b0, b1;
        band (dsp::Polyphase::soft(), 0.95, o0, o1);
        linear (b0, b1);
        blend (o0, o1, b0, b1, (a - 0.36f) / 0.15f);
    }
    else if (a <= 0.89f)
    {
        float b0, b1;
        linear (o0, o1);
        drop (b0, b1);
        blend (o0, o1, b0, b1, (a - 0.51f) / 0.38f);
    }
    else
    {
        // past drop sample: towards a high pass, the nearest sample minus the linear interpolation (both centred, so
        // the smooth part cancels and the images stay: at 100 % they are louder than the sound itself)
        const float t = (a - 0.89f) / 0.11f;
        float l0, l1, n0, n1;
        drop (o0, o1);
        linear (l0, l1);
        const auto k = (int64_t) std::floor (pp + 0.5);
        n0 = at (d0, k);
        n1 = d1 != nullptr ? at (d1, k) : 0.0f;
        constexpr float g = 3.0f;
        o0 += t * ((n0 - l0) * g - o0);
        o1 += t * ((n1 - l1) * g - o1);
    }
    left = o0;
    right = d1 != nullptr ? o1 : o0;
}

// WSOLA: a new grain starts where its first few milliseconds look most like what the newest grain is about to play,
// within `search` frames of where it should start (no phasing between the grains on a pitched sound)
double Player::alignGrain (const Voice& v, const Read& r, double target, double search) const
{
    const Grain* ref = nullptr;
    for (auto& g : v.grains)
        if (g.on && (ref == nullptr || g.phase < ref->phase))
            ref = &g;
    if (ref == nullptr || search < 2.0)
        return target;
    const auto& s = *r.s;
    const float* d = s.audio[0].data();
    const int len = s.length();
    const double step = r.step;
    const int points = 48;
    const double span = juce::jmax (2.0, 0.003 * s.sampleRate * std::abs (step) / points);   // 3 ms over 48 points
    auto x = [&] (double p) { const auto k = (int64_t) p; return k >= 0 && k < len ? d[k] : 0.0f; };
    float refv[points];
    double refE = 0.0;
    for (int j = 0; j < points; ++j)
    {
        refv[j] = x (ref->pos + (double) j * span * (step < 0.0 ? -1.0 : 1.0));
        refE += (double) refv[j] * refv[j];
    }
    if (refE < 1.0e-9)
        return target;
    double best = target, bestScore = -2.0;
    const double dir = step < 0.0 ? -1.0 : 1.0;
    for (double off = -search; off <= search; off += 2.0)
    {
        double xy = 0.0, yy = 0.0;
        for (int j = 0; j < points; ++j)
        {
            const float y = x (target + off + (double) j * span * dir);
            xy += (double) refv[j] * y;
            yy += (double) y * y;
        }
        const double score = yy > 1.0e-12 ? xy / std::sqrt (yy * refE) : -1.0;
        if (score > bestScore)
        {
            bestScore = score;
            best = target + off;
        }
    }
    return best;
}

void Player::renderVoice (Voice& v, float* L, float* R, int n, const Chunk& c)
{
    if (v.synth)
    {
        renderSynth (v, L, R, n, c);
        return;
    }
    // the glide, over the chunk
    if (std::abs (v.pitch - v.pitchTarget) > 0.0f)
    {
        const float k = std::pow (1.0f - c.glideK, (float) n);
        v.pitch = v.pitchTarget + (v.pitch - v.pitchTarget) * k;
        if (std::abs (v.pitch - v.pitchTarget) < 1.0e-3f)
            v.pitch = v.pitchTarget;
    }
    Read r;
    prepareRead (v, c, r);
    const int len = r.len;
    const bool stereo = r.stereo;
    const bool back = r.step < 0.0;

    // the filter: its cutoff follows the keys, the velocity and its envelope
    if (c.filterOn)
        for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
            v.filter[ch].set (c.fs);
    const float ktOct = c.v[snd::keyTrack] * ((float) v.note - 60.0f) / 12.0f;
    const float velOct = -c.v[snd::velCutoff] * 4.0f * (1.0f - (float) v.velocity / 127.0f);
    const float baseOct = c.cutoffOct + ktOct + velOct;
    const float fadeStep = 1.0f / (float) (0.004 * sr);

    // from where the last chunk left them to where this one wants them, a sample at a time: the cutoff, the depth
    // of its envelope, the step (the pitch). A direction turned round is taken at once (no ramp through zero)
    if (v.fresh)
    {
        v.cutFrom = baseOct;
        v.envFrom = c.envOct;
        v.stepFrom = r.step;
        v.fresh = false;
    }
    const float invN = 1.0f / (float) juce::jmax (1, n);
    float cutNow = v.cutFrom, envNow = v.envFrom;
    const float cutInc = (baseOct - cutNow) * invN, envInc = (c.envOct - envNow) * invN;
    const bool turned = (v.stepFrom < 0.0) != (r.step < 0.0);
    double stepNow = turned ? r.step : v.stepFrom;
    const double stepInc = turned ? 0.0 : (r.step - stepNow) / (double) juce::jmax (1, n);
    v.cutFrom = baseOct;
    v.envFrom = c.envOct;
    v.stepFrom = r.step;

    // elastic
    const double grainLen = juce::jmax (8.0, (double) c.v[snd::grain] * 0.001 * sr);   // output samples
    // aligned grains (WSOLA) need only two at a time; scattered ones (DISPERSION) are smoother four at a time
    // (Hann windows overlapping by 1/k sum to k / 2)
    const int overlap = c.v[snd::spray] < 0.3f ? 2 : 4;
    const double hop = grainLen / (double) overlap;
    const double speed = (double) c.v[snd::speed] * v.rateScale * (back ? -1.0 : 1.0);
    const double startDelta = (double) (c.v[snd::start] - v.startAtOn) * (double) len * (back ? -1.0 : 1.0);
    const double sprayFrames = (double) c.v[snd::spray] * 0.25 * r.s->sampleRate;
    auto place = [&] (double p)
    {
        if (r.loop)
        {
            const double ls = r.ls, le = r.le, ll = le - ls;
            if (p >= le)
                p = ls + std::fmod (p - ls, ll);
            else if (p < 0.0)
                p = le - std::fmod (-p, ll);
            return p;
        }
        return juce::jlimit (0.0, (double) (len - 1), p);
    };

    // one frame of the sample at p, with the phase modulation, the bits
    float pm = 0.0f;
    // the phase modulation's push now, and the speed the reading meets (the step and how fast the push moves):
    // the kernel's cutoff follows that speed, so a deep modulation doesn't fold back either
    double disp = 0.0, speedNow = std::abs (stepNow);
    r.bank = c.elastic ? &dsp::Polyphase::grains() : &dsp::Polyphase::hq();
    // the MIXEUR: the sample's level, ORBITE's (heard when it's on; computed too when it modulates the phase)
    const float mixS = c.v[snd::mxSample];
    const float mixO = c.v[snd::orOn] > 0.5f ? c.v[snd::mxOrbite] * synthLevel : 0.0f;
    // (the sample's, ORBITE's and the sub's levels ramped a sample at a time from where the last chunk left them)
    if (v.lvFresh)
    {
        v.lvFrom[0] = mixS;
        v.lvFrom[1] = mixO;
        v.lvFrom[2] = c.v[snd::mxSub] * 0.6f;
        v.lvFrom[3] = c.v[snd::subOn] > 0.5f ? 1.0f : 0.0f;
        v.lvFresh = false;
    }
    float mS = v.lvFrom[0], mO = v.lvFrom[1], mSub = v.lvFrom[2], subGate = v.lvFrom[3];
    const float invNs = 1.0f / (float) juce::jmax (1, n);
    const float mSInc = (mixS - mS) * invNs, mOInc = (mixO - mO) * invNs, mSubInc = (c.v[snd::mxSub] * 0.6f - mSub) * invNs;
    const float subGateTo = c.v[snd::subOn] > 0.5f ? 1.0f : 0.0f, subGateInc = (subGateTo - subGate) * invNs;
    v.lvFrom[0] = mixS;
    v.lvFrom[1] = mixO;
    v.lvFrom[2] = c.v[snd::mxSub] * 0.6f;
    v.lvFrom[3] = subGateTo;
    const bool orbNeeded = mixO > 0.0f || mO > 0.0f || r.pm == 5;
    const double playedNow = (double) v.pitch + (double) (int) c.v[snd::transpose] + (double) c.v[snd::fine] * 0.01 + (double) c.bendSemis
                           + (double) c.v[snd::tune];
    const OrbitePlan op = orbNeeded ? orbitePlan (c.v, playedNow, sr) : OrbitePlan {};
    float orbNow = 0.0f;
    // the SUB: its frequency (the note one or two octaves down, back up an octave while under 25 Hz), the crossover
    const bool subOn = c.v[snd::subOn] > 0.5f || subGate > 0.0f;
    const bool subOctave = (int) c.v[snd::subMode] == 1;
    const float subKeep = 1.0f - c.v[snd::subReplace];
    const float subDrive = 1.0f + 7.0f * c.v[snd::subDrive], subNorm = 1.0f / std::tanh (subDrive);
    const bool subFollow = c.v[snd::subFollow] > 0.5f;
    double subHz = rk::noteHz (playedNow) / ((int) c.v[snd::subOct] == 1 ? 4.0 : 2.0);
    while (subHz < 25.0)
        subHz *= 2.0;
    const double subRatio = subHz / rk::noteHz (playedNow);            // the octave heads' speed, against the reading's
    // the heads' grains: two and a half periods of the sub, 20 to 120 ms; their alignment: the material's period
    const float headLen = (float) juce::jlimit (0.02 * sr, 0.12 * sr, 2.5 * sr / subHz);
    const double periodSrc = sr / rk::noteHz (playedNow) * std::abs (r.step);
    const float followUp = 1.0f - std::exp (-1.0f / (0.003f * (float) sr)), followDown = 1.0f - std::exp (-1.0f / (0.12f * (float) sr));
    if (subOn)
    {
        const float xo = c.v[snd::subXover];
        if (std::abs (xo - v.sub.xoverNow) > 0.01f)
        {
            v.sub.xoverNow = xo;
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 2; ++k)
                {
                    butterworth (v.sub.lo[ch][k], xo, sr, false);
                    butterworth (v.sub.hi[ch][k], xo, sr, true);
                }
            butterworth (v.sub.band[0], xo, sr, false);
            butterworth (v.sub.band[1], xo, sr, false);
            butterworth (v.sub.infra[0], 22.0, sr, true);
            butterworth (v.sub.infra[1], 22.0, sr, true);
        }
    }
    // CROSSFADE: the loop's end fades into what comes before its start, up to half the loop and no
    // further back than the sample's beginning
    const double xfLen = r.loop ? juce::jmin ((double) c.v[snd::loopXfade] * 0.5 * (double) (r.le - r.ls), (double) r.ls) : 0.0;
    auto frame = [&] (double p, float& l, float& rr)
    {
        p += disp;
        readFrame (r, p, speedNow, l, rr);
        if (xfLen > 1.0 && p >= (double) r.le - xfLen && p < (double) r.le)
        {
            float l2, r2;
            readFrame (r, p - (double) (r.le - r.ls), speedNow, l2, r2);
            // raised cosine: the two sum to one (a sustained tone, the two sides alike, keeps its level)
            const float t = (float) ((p - ((double) r.le - xfLen)) / xfLen);
            const float b = 0.5f - 0.5f * std::cos (3.14159265f * t), a = 1.0f - b;
            l = l * a + l2 * b;
            rr = rr * a + r2 * b;
        }
    };
    Read ra = r;                     // the attack kept: at its own speed, its own pitch
    ra.step = v.rateScale;
    ra.level = 0;
    const double grainSearch = c.v[snd::spray] < 0.3f
        ? juce::jmin (0.010 * r.s->sampleRate, 0.25 * grainLen * std::abs (r.step)) : 0.0;
    auto quantise = [&] (float x) { return r.bitsQ > 0.0f ? std::round (x * r.bitsQ) / r.bitsQ : x; };
    const double spliceInv = 1.0 / (double) spliceLength;

    for (int i = 0; i < n; ++i)
    {
        cutNow += cutInc;
        envNow += envInc;
        stepNow += stepInc;
        if (orbNeeded)
        {
            v.orbit += op.orbitInc;
            orbNow = v.orb.process (op.s, op.incC, op.inc1, op.inc2, v.orbit);
        }
        if (r.pm == 1)
        {
            v.pmPhase += r.pmInc;
            v.pmPhase -= std::floor (v.pmPhase);
            pm = std::sin (twoPi * (float) v.pmPhase);
        }
        else if (r.pm == 2)
        {
            // the sample modulating itself, read by a second head at RAPPORT times its speed (wrapping on its loop)
            const auto& sm = *r.s;
            const int a = r.loop ? r.ls : 0, b = r.loop ? r.le : len;
            v.pmPos += std::abs (r.step) * (double) c.v[snd::pmRatio];
            if (v.pmPos >= (double) b)
                v.pmPos = (double) a + std::fmod (v.pmPos - (double) a, (double) juce::jmax (1, b - a));
            // (between its samples, linearly: the second head moves at any speed)
            const double q = juce::jlimit (0.0, (double) (len - 1), v.pmPos);
            const int k = (int) q;
            const float* d = sm.audio[0].data();
            const float x0 = d[k], x1 = d[juce::jmin (len - 1, k + 1)];
            pm = (x0 + (float) (q - (double) k) * (x1 - x0)) * 1.5f;
        }
        else if (r.pm == 3)
            pm = juce::jlimit (-1.5f, 1.5f, 0.5f * (c.inL[i] + c.inR[i]));
        else if (r.pm == 4)
            pm = v.lastOut;          // RETOUR: the voice's own last sample, before its filter (a DX's feedback)
        else if (r.pm == 5)
            pm = orbNow;             // ORBITE pushing the reading
        {
            const double d = r.pm != 0 ? (double) (r.pmIndex * pm) : 0.0;
            speedNow = std::abs (stepNow + d - v.pmDisp);
            v.pmDisp = disp = d;
        }

        float xl = 0.0f, xr = 0.0f;
        if (c.elastic)
        {
            v.sinceGrain += 1.0f;
            if (v.sinceGrain >= (float) hop)
            {
                v.sinceGrain = v.sinceGrain > 1.0e8f ? 0.0f : v.sinceGrain - (float) hop;
                for (auto& g : v.grains)
                    if (! g.on)
                    {
                        double p = v.scan + startDelta;
                        if (sprayFrames > 0.0)
                            p += (double) (uniform (v.rng) * 2.0f - 1.0f) * sprayFrames;
                        p = place (p);
                        if (grainSearch > 0.0)
                            p = place (alignGrain (v, r, p, grainSearch));
                        g.on = true;
                        g.pos = std::abs (std::abs (r.step) - 1.0) < 1.0e-9 ? std::floor (p) : p;
                        g.phase = 0.0f;
                        g.inc = (float) (1.0 / grainLen);
                        break;
                    }
            }
            for (auto& g : v.grains)
            {
                if (! g.on)
                    continue;
                const float w = 0.5f - 0.5f * std::cos (twoPi * g.phase);
                float gl, gr;
                frame (g.pos, gl, gr);
                xl += gl * w;
                xr += gr * w;
                g.pos += stepNow;
                if (! r.loop && (g.pos >= (double) len || g.pos < 0.0))
                    g.on = false;
                g.phase += g.inc;
                if (g.phase >= 1.0f)
                    g.on = false;
            }
            xl *= 2.0f / (float) overlap;
            xr *= 2.0f / (float) overlap;
            v.scan = place (v.scan + speed);
            if (c.oneShot && ! r.loop && ((speed > 0.0 && v.scan >= (double) (len - 1)) || (speed < 0.0 && v.scan <= 0.0))
                && v.amp.stage != Env::release)
                v.amp.off(), v.flt.off();
        }
        else
        {
            if (v.pos >= (double) len || v.pos < 0.0)
            {
                if (! (r.loop && back))
                {
                    v.active = false;
                    return;
                }
            }
            if (v.attackLeft > v.attackFade)
            {
                // the attack alone, at its own speed; the body waits
                readFrame (ra, v.attackPos, std::abs (ra.step), xl, xr);
                v.attackPos += ra.step;
                --v.attackLeft;
            }
            else
            {
                frame (v.pos, xl, xr);
                if (v.attackLeft > 0)
                {
                    // the attack handing over to the pitched body (equal power)
                    float al, ar;
                    readFrame (ra, v.attackPos, std::abs (ra.step), al, ar);
                    v.attackPos += ra.step;
                    const float t = (float) v.attackLeft / (float) juce::jmax (1, v.attackFade);
                    const float out = std::sin (1.5707963f * t), in = std::cos (1.5707963f * t);
                    xl = xl * in + al * out;
                    xr = xr * in + ar * out;
                    --v.attackLeft;
                }
                if (v.ghostLeft > 0)
                {
                    // the splice: the old position fading out (equal power), the new fading in
                    float gl, gr;
                    frame (v.ghost, gl, gr);
                    const float t = (float) ((double) v.ghostLeft * spliceInv);
                    const float out = std::sin (1.5707963f * t), in = std::cos (1.5707963f * t);
                    xl = xl * in + gl * out;
                    xr = xr * in + gr * out;
                    v.ghost += stepNow;
                    --v.ghostLeft;
                }
                const double before = v.pos;
                v.pos += stepNow;
                if (r.loop)
                {
                    const double span = (double) (r.le - r.ls);
                    bool wrapped = false;
                    if (! back && v.pos >= (double) r.le)
                    {
                        v.pos -= span * std::floor ((v.pos - (double) r.ls) / span);
                        wrapped = true;
                    }
                    else if (back && v.pos < (double) r.ls && before >= (double) r.ls)
                    {
                        v.pos += span * std::ceil (((double) r.ls - v.pos) / span);
                        wrapped = true;
                    }
                    // a loop that isn't the one found has an unprepared seam: smoothed
                    if (wrapped && r.moved)
                    {
                        v.ghost = before + stepNow;
                        v.ghostLeft = spliceLength;
                    }
                }
            }
        }
        xl = quantise (xl);
        xr = stereo ? quantise (xr) : xl;
        float subOut = 0.0f;
        if (subOn)
        {
            // the sample split at the crossover: its highs as they are, its lows kept as far as REPLACE leaves them
            auto& S = v.sub;
            const float inMono = 0.5f * (xl + xr);
            const float lowL = S.lo[0][1].tick (S.lo[0][0].tick (xl)), highL = S.hi[0][1].tick (S.hi[0][0].tick (xl));
            float lowR = lowL, highR = highL;
            if (stereo)
            {
                lowR = S.lo[1][1].tick (S.lo[1][0].tick (xr));
                highR = S.hi[1][1].tick (S.hi[1][0].tick (xr));
            }
            const float keep = 1.0f - (1.0f - subKeep) * subGate;      // (the lows given back as the sub fades out)
            xl = highL + keep * lowL;
            xr = highR + keep * lowR;
            // the sub itself
            float s = 0.0f;
            if (! subOctave)
            {
                S.phase += subHz / sr;
                S.phase -= std::floor (S.phase);
                s = std::sin (twoPi * (float) S.phase);
                const float a = std::abs (inMono);
                S.env += (a - S.env) * (a > S.env ? followUp : followDown);
                s *= subFollow ? juce::jmin (1.0f, S.env * 2.5f) : 0.5f;
            }
            else
            {
                // OCTAVE: the sample read slower by two heads (Hann grains, half overlapping), each new one starting where
                // the reading is, on the material's period from the old one (no beating between them)
                const double srcNow = c.elastic ? v.scan : v.pos;
                S.sinceHead += 1.0f;
                if (S.sinceHead >= headLen * 0.5f)
                {
                    S.sinceHead = 0.0f;
                    const Voice::Sub::Head* old = nullptr;             // the youngest still playing
                    for (auto& h : S.heads)
                        if (h.on && (old == nullptr || h.phase < old->phase))
                            old = &h;
                    double start = srcNow;
                    if (old != nullptr && periodSrc > 1.0)
                        start = old->pos + periodSrc * std::round ((srcNow - old->pos) / periodSrc);
                    for (auto& h : S.heads)
                        if (! h.on)
                        {
                            h = { true, start, 0.0f, 1.0f / headLen };
                            break;
                        }
                }
                const double headStep = stepNow * subRatio;
                for (auto& h : S.heads)
                {
                    if (! h.on)
                        continue;
                    float hl, hr;
                    readFrame (r, h.pos, std::abs (headStep), hl, hr);
                    s += 0.5f * (hl + hr) * (0.5f - 0.5f * std::cos (twoPi * h.phase));
                    h.pos += headStep;
                    h.phase += h.inc;
                    if (h.phase >= 1.0f)
                        h.on = false;
                }
            }
            // warmed by DRIVE, then kept under the crossover (its harmonics too) and over the infra
            s = std::tanh (s * subDrive) * subNorm;
            s = S.band[1].tick (S.band[0].tick (s));
            subOut = S.infra[1].tick (S.infra[0].tick (s)) * mSub * subGate;
        }
        mS += mSInc;
        mO += mOInc;
        mSub += mSubInc;
        subGate += subGateInc;
        xl = xl * mS + orbNow * mO + subOut;
        xr = xr * mS + orbNow * mO + subOut;
        // RETOUR's next push: this sample, kept in bounds (a feedback that can't run away)
        v.lastOut = juce::jlimit (-1.5f, 1.5f, 0.5f * (xl + xr));
        const float fe = tick (v.flt, c.flt, false);
        if (c.filterOn)
        {
            const float oct = cutNow + envNow * fe;
            auto& f0 = v.filter[0];
            xl = v.os[0].process (xl, [&f0, oct] (float u) { return f0.tick (u, oct); });
            if (stereo)
            {
                auto& f1 = v.filter[1];
                xr = v.os[1].process (xr, [&f1, oct] (float u) { return f1.tick (u, oct); });
            }
            else
                xr = xl;
        }
        float g = tick (v.amp, c.amp, true) * v.gain;
        if (v.fading)
        {
            v.fade -= fadeStep;
            if (v.fade <= 0.0f)
            {
                v.active = false;
                return;
            }
            g *= v.fade;
        }
        L[i] += xl * g;
        R[i] += xr * g;
        if (v.amp.stage == Env::idle)
        {
            v.active = false;
            return;
        }
    }
}


//============================================================================== SYNTHÉ
const dsp::CycleBank& Player::bankFor (int choice) const
{
    if (choice == 1 && ins != nullptr && ins->cycles != nullptr && ins->cycles->count() > 0)
        return *ins->cycles;
    return *basics;
}

void Player::renderSynth (Voice& v, float* L, float* R, int n, const Chunk& c)
{
    if (std::abs (v.pitch - v.pitchTarget) > 0.0f)
    {
        const float k = std::pow (1.0f - c.glideK, (float) n);
        v.pitch = v.pitchTarget + (v.pitch - v.pitchTarget) * k;
        if (std::abs (v.pitch - v.pitchTarget) < 1.0e-3f)
            v.pitch = v.pitchTarget;
    }
    const auto& bankA = bankFor ((int) c.v[snd::oaBank]);
    const auto& bankB = bankFor ((int) c.v[snd::obBank]);
    const double played = (double) v.pitch + (double) (int) c.v[snd::transpose] + (double) c.v[snd::fine] * 0.01 + (double) c.bendSemis;
    const double hzA = rk::noteHz (played + 12.0 * c.v[snd::oaOct] + c.v[snd::oaSemi] + 0.01 * c.v[snd::oaFine]);
    const double hzB = rk::noteHz (played + 12.0 * c.v[snd::obOct] + c.v[snd::obSemi] + 0.01 * c.v[snd::obFine]);

    // the unison: each voice detuned (up to +-30 cents at the edges) and placed across the stereo
    const int voicesN = juce::jlimit (1, 4, (int) c.v[snd::unison]);
    const bool stereo = voicesN > 1;
    double ratio[4] = { 1.0, 1.0, 1.0, 1.0 };
    float gl[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, gr[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (int u = 0; u < voicesN && stereo; ++u)
    {
        const float spread = (float) u / (float) (voicesN - 1) * 2.0f - 1.0f;
        ratio[u] = std::exp2 ((double) (spread * c.v[snd::detune] * 0.3f) / 12.0);
        const float pan = spread * c.v[snd::width];
        gl[u] = std::cos ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        gr[u] = std::sin ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
    }
    const float norm = 1.0f / std::sqrt ((float) voicesN);
    const double top = stereo ? std::exp2 ((double) (c.v[snd::detune] * 0.3f) / 12.0) : 1.0;
    const int levA = dsp::CycleBank::levelFor (hzA * top, sr), levB = dsp::CycleBank::levelFor (hzB * top, sr);
    const float lvA = c.v[snd::oaLevel], lvB = c.v[snd::obLevel], sub = c.v[snd::subLevel], noise = c.v[snd::noiseLevel];
    const float pmTo = c.v[snd::pmBA] * c.v[snd::pmBA] * 1.5f;     // B moves A's phase by up to 1.5 cycles
    // ORBITE, in the mix when it's on (centred, one per note)
    const float lvO = c.v[snd::orOn] > 0.5f ? c.v[snd::mxOrbite] : 0.0f;
    const OrbitePlan op = lvO > 0.0f || (! v.lvFresh && v.lvFrom[4] > 0.0f) ? orbitePlan (c.v, played, sr) : OrbitePlan {};

    // the filter, its cutoff following the keys and the velocity; everything ramped from the last chunk
    if (c.filterOn)
        for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
            v.filter[ch].set (c.fs);
    const float ktOct = c.v[snd::keyTrack] * ((float) v.note - 60.0f) / 12.0f;
    const float velOct = -c.v[snd::velCutoff] * 4.0f * (1.0f - (float) v.velocity / 127.0f);
    const float baseOct = c.cutoffOct + ktOct + velOct;
    const double incA = hzA / sr, incB = hzB / sr;
    const float posA = c.v[snd::oaPos], posB = c.v[snd::obPos];
    if (v.fresh)
    {
        v.cutFrom = baseOct;
        v.envFrom = c.envOct;
        v.incAFrom = incA;
        v.incBFrom = incB;
        v.posAFrom = posA;
        v.posBFrom = posB;
        v.pmFrom = pmTo;
        v.fresh = false;
    }
    const float invN = 1.0f / (float) juce::jmax (1, n);
    float cutNow = v.cutFrom, envNow = v.envFrom, pA = v.posAFrom, pB = v.posBFrom, pmNow = v.pmFrom;
    double iA = v.incAFrom, iB = v.incBFrom;
    const float cutInc = (baseOct - cutNow) * invN, envInc = (c.envOct - envNow) * invN;
    const float pAInc = (posA - pA) * invN, pBInc = (posB - pB) * invN, pmInc = (pmTo - pmNow) * invN;
    const double iAInc = (incA - iA) / (double) juce::jmax (1, n), iBInc = (incB - iB) / (double) juce::jmax (1, n);
    v.cutFrom = baseOct;
    v.envFrom = c.envOct;
    v.incAFrom = incA;
    v.incBFrom = incB;
    v.posAFrom = posA;
    v.posBFrom = posB;
    v.pmFrom = pmTo;
    const float fadeStep = 1.0f / (float) (0.004 * sr);
    // the levels too, a sample at a time (a fader moved: no stairs)
    const float lvTo[5] = { lvA, lvB, sub, noise, lvO };
    if (v.lvFresh)
    {
        std::copy (lvTo, lvTo + 5, v.lvFrom.begin());
        v.lvFresh = false;
    }
    float lv[5], lvInc[5];
    for (int k = 0; k < 5; ++k)
    {
        lv[k] = v.lvFrom[(size_t) k];
        lvInc[k] = (lvTo[k] - lv[k]) * invN;
        v.lvFrom[(size_t) k] = lvTo[k];
    }
    const bool subHeard = sub > 0.0f || lv[2] > 0.0f, noiseHeard = noise > 0.0f || lv[3] > 0.0f, orbHeard = lvO > 0.0f || lv[4] > 0.0f;

    for (int i = 0; i < n; ++i)
    {
        cutNow += cutInc;
        envNow += envInc;
        pA += pAInc;
        pB += pBInc;
        pmNow += pmInc;
        iA += iAInc;
        iB += iBInc;
        for (int k = 0; k < 5; ++k)
            lv[k] += lvInc[k];
        float xl = 0.0f, xr = 0.0f;
        for (int u = 0; u < voicesN; ++u)
        {
            double& b = v.phB[(size_t) u];
            b += iB * ratio[u];
            b -= std::floor (b);
            const float sb = lv[1] > 0.0f || lvB > 0.0f || pmNow > 0.0f ? bankB.read (pB, b, levB) : 0.0f;
            double& a = v.phA[(size_t) u];
            a += iA * ratio[u];
            a -= std::floor (a);
            const float sa = bankA.read (pA, a + (double) (pmNow * sb), levA);
            const float x = sa * lv[0] + sb * lv[1];
            xl += x * gl[u];
            xr += x * gr[u];
        }
        xl *= norm;
        xr *= norm;
        if (subHeard)
        {
            v.phSub += iA * 0.5;
            v.phSub -= std::floor (v.phSub);
            const float s = lv[2] * std::sin (twoPi * (float) v.phSub);
            xl += s;
            xr += s;
        }
        if (noiseHeard)
        {
            const float w = lv[3] * 0.5f * (uniform (v.rng) * 2.0f - 1.0f);
            xl += w;
            xr += w;
        }
        if (orbHeard)
        {
            v.orbit += op.orbitInc;
            const float o = lv[4] * v.orb.process (op.s, op.incC, op.inc1, op.inc2, v.orbit);
            xl += o;
            xr += o;
        }
        if (! stereo)
            xr = xl;
        // a note at about -10 dB (both oscillators at their default levels): room for chords before the output
        xl *= synthLevel;
        xr *= synthLevel;

        const float fe = tick (v.flt, c.flt, false);
        if (c.filterOn)
        {
            const float oct = cutNow + envNow * fe;
            auto& f0 = v.filter[0];
            xl = v.os[0].process (xl, [&f0, oct] (float u) { return f0.tick (u, oct); });
            if (stereo)
            {
                auto& f1 = v.filter[1];
                xr = v.os[1].process (xr, [&f1, oct] (float u) { return f1.tick (u, oct); });
            }
            else
                xr = xl;
        }
        float g = tick (v.amp, c.amp, true) * v.gain;
        if (v.fading)
        {
            v.fade -= fadeStep;
            if (v.fade <= 0.0f)
            {
                v.active = false;
                return;
            }
            g *= v.fade;
        }
        L[i] += xl * g;
        R[i] += xr * g;
        if (v.amp.stage == Env::idle)
        {
            v.active = false;
            return;
        }
    }
}

//============================================================================== modulators, settings
float Player::smoothed (int i, float k, bool snap)
{
    const float target = get (i);
    const auto kind = snd::def (i).kind;
    if (kind == snd::Kind::integer || kind == snd::Kind::choice || kind == snd::Kind::toggle)
        return target;
    if (snap || ! glidePrimed)
    {
        glideRaw[(size_t) i] = target;
        gliding[(size_t) i] = false;
        return target;
    }
    if (std::abs (target - glideRaw[(size_t) i]) > 0.0f)
    {
        if (! gliding[(size_t) i])
            glideNorm[(size_t) i] = snd::toNorm (i, glideRaw[(size_t) i]);
        glideRaw[(size_t) i] = target;
        gliding[(size_t) i] = true;
    }
    if (! gliding[(size_t) i])
        return target;
    const float goal = snd::toNorm (i, target);
    float& g = glideNorm[(size_t) i];
    g += (goal - g) * k;
    if (std::abs (goal - g) < 2.0e-4f)
    {
        gliding[(size_t) i] = false;
        return target;
    }
    return snd::fromNorm (i, g);
}

// what each follower hears over this chunk (only the sources a FOLLOW listens to): the level as a peak would read it
void Player::hearFollowers (int pos, int end)
{
    bool wanted[numFollowSources] {};
    for (int m = 0; m < snd::numMods; ++m)
        if ((int) get (snd::modParam (m, snd::mType)) == (int) dsp::ModType::follower)
            wanted[juce::jlimit (0, numFollowSources - 1, (int) get (snd::modParam (m, snd::mSource)))] = true;
    auto rms = [pos, end] (const FollowSignal& f)
    {
        if (f.l == nullptr || end <= pos)
            return 0.0f;
        float sum = 0.0f;
        for (int i = pos; i < end; ++i)
            sum += f.l[i] * f.l[i];
        int count = end - pos;
        if (f.r != nullptr)
        {
            for (int i = pos; i < end; ++i)
                sum += f.r[i] * f.r[i];
            count *= 2;
        }
        const float v = std::sqrt (sum / (float) count) * 1.41f;
        return std::isfinite (v) ? v : 0.0f;
    };
    for (int s = 0; s < numFollowSources; ++s)
    {
        if (! wanted[s])
        {
            followLevel[(size_t) s] = 0.0f;
            continue;
        }
        const auto& f = follow[(size_t) s];
        if (s == 0)
            followLevel[0] = f.l != nullptr ? rms (f) : inputLevel;
        else if (s == 1)
            followLevel[1] = std::max (voiceLevel, rms (f));   // (the voices: the last chunk's)
        else
            followLevel[(size_t) s] = rms (f);
    }
}

void Player::modulate (int offset, int n, const Transport& t, Chunk& c, bool atRest)
{
    const double bpm = t.playing && t.bpm > 0.0 ? t.bpm : (double) get (snd::tempo);
    const double ppq = t.playing ? t.ppq + (double) offset / sr * bpm / 60.0 : freePpq;
    const double dt = (double) n / sr;
    if (! t.playing)
        freePpq += dt * bpm / 60.0;

    // ENV: keys down (each new note fires it again)
    bool anyKey = sustain;
    for (auto& k : keyDown)
        anyKey = anyKey || k.load (std::memory_order_relaxed);

    float mv[snd::numMods];
    for (int m = 0; m < snd::numMods; ++m)
    {
        auto P = [this, m] (snd::ModSetting w) { return get (snd::modParam (m, w)); };
        dsp::ModSettings ms;
        ms.type = (dsp::ModType) juce::jlimit (0, 4, (int) P (snd::mType));
        ms.shape = (dsp::ModShape) juce::jlimit (0, 6, (int) P (snd::mShape));
        ms.sync = P (snd::mSync) < 0.5f;
        ms.division = (int) P (snd::mDiv);
        ms.hz = P (snd::mHz);
        ms.smooth = P (snd::mSmooth);
        ms.unipolar = P (snd::mPolarity) > 0.5f;
        ms.retrigger = P (snd::mRetrig) > 0.5f;
        ms.length = (int) P (snd::mLen);
        ms.vari = P (snd::mVari);
        ms.attack = P (snd::mAttack) * 0.001f;
        ms.decay = P (snd::mDecay) * 0.001f;
        ms.sustain = P (snd::mSustain);
        ms.release = P (snd::mRelease) * 0.001f;
        ms.gain = juce::Decibels::decibelsToGain (P (snd::mGain));
        for (int s = 0; s < snd::maxSteps; ++s)
            ms.stepValues[(size_t) s] = steps[(size_t) m][(size_t) s].load (std::memory_order_relaxed);
        auto& mod = mods[(size_t) m];
        mod.set (ms);
        const uint32_t seed = seeds[(size_t) m].load (std::memory_order_relaxed);
        if (seed != seedNow[(size_t) m])
        {
            seedNow[(size_t) m] = seed;
            mod.reseed (seed);
        }
        if (ms.type == dsp::ModType::envelope)
        {
            if (retrigPending[m])
                mod.gate (true);
            else if (! anyKey && gateOn[m])
                mod.gate (false);
            gateOn[m] = anyKey;
            retrigPending[m] = false;
        }
        else if (ms.type == dsp::ModType::follower)
            mod.input (followLevel[(size_t) juce::jlimit (0, numFollowSources - 1, (int) P (snd::mSource))]);
        const bool onHost = ms.sync && t.playing;
        if (retrigPending[m])
        {
            retrigPending[m] = false;
            if (onHost)
                retrigPpq[(size_t) m] = ppq;
            else
                mod.retrigger();
        }
        if (! ms.retrigger)
            retrigPpq[(size_t) m] = 0.0;
        const float value = onHost ? mod.atPpq (ppq - retrigPpq[(size_t) m], bpm, dt) : mod.advance (dt, bpm);
        mv[m] = value * P (snd::mAmt);
        modValue[(size_t) m].store (mv[m], std::memory_order_relaxed);
        modStep[(size_t) m].store (mod.currentStep(), std::memory_order_relaxed);
        modPhase[(size_t) m].store ((float) ((double) (mod.cycles() & 1) + mod.getPhase()), std::memory_order_relaxed);
    }

    float off[snd::count] {};
    bool moved[snd::count] {};
    for (int m = 0; m < snd::numMods; ++m)
        for (auto& r : routes[(size_t) m])
        {
            const int d = r.dest.load (std::memory_order_relaxed);
            if (d >= 0 && d < snd::count)
            {
                off[d] += r.amount.load (std::memory_order_relaxed) * mv[m];
                moved[d] = true;
            }
        }
    // the scenes: each setting held by scene A or B goes from one to the other as the fader goes (a setting held
    // by one only, from where it is); then the modulators on top
    const int sa = juce::jlimit (1, snd::numScenes, (int) get (snd::sceneA)) - 1;
    const int sb = juce::jlimit (1, snd::numScenes, (int) get (snd::sceneB)) - 1;
    const float xf = juce::jlimit (0.0f, 1.0f, get (snd::xfade) + (moved[snd::xfade] ? off[snd::xfade] : 0.0f));
    const float glideK = 1.0f - std::exp (-(float) n / (float) (0.012 * sr));
    for (int i = 0; i < snd::count; ++i)
    {
        const float raw = smoothed (i, glideK, atRest);
        const float la = scenes[(size_t) sa][(size_t) i].load (std::memory_order_relaxed);
        const float lb = scenes[(size_t) sb][(size_t) i].load (std::memory_order_relaxed);
        const bool scened = ! (std::isnan (la) && std::isnan (lb));
        const bool modded = moved[i] && snd::def (i).modulatable;
        if (! scened && ! modded)
        {
            c.v[i] = raw;
            shownMoved[(size_t) i].store (false, std::memory_order_relaxed);
            continue;
        }
        float norm = snd::toNorm (i, raw);
        if (scened)
        {
            const float na = std::isnan (la) ? norm : snd::toNorm (i, la);
            const float nb = std::isnan (lb) ? norm : snd::toNorm (i, lb);
            norm = na + (nb - na) * xf;
        }
        if (modded)
            norm += off[i];
        norm = juce::jlimit (0.0f, 1.0f, norm);
        c.v[i] = snd::fromNorm (i, norm);
        shownNorm[(size_t) i].store (norm, std::memory_order_relaxed);
        shownMoved[(size_t) i].store (true, std::memory_order_relaxed);
    }

    glidePrimed = true;
    c.ppq = ppq;
    c.bpm = bpm;
    c.playing = t.playing;
    // RÉÉCHANTILLONNER: the chain neutral, while the sound's settings are being set to match
    const bool neutralNow = neutral.load (std::memory_order_relaxed);
    if (neutralNow)
        for (int id : { (int) snd::fModel, (int) snd::dModel, (int) snd::spOn, (int) snd::piOn, (int) snd::chOn, (int) snd::crOn,
                        (int) snd::rvOn, (int) snd::ecOn, (int) snd::cpOn, (int) snd::wtf })
            c.v[id] = 0.0f;
    if (wasNeutral && ! neutralNow)
        glidePrimed = false;        // let go: the settings as they are now, not glided to from before
    wasNeutral = neutralNow;
    applyWtf (ppq, n, c);
    c.n = n;
    c.elastic = c.v[snd::playMode] > 0.5f;
    c.loops = c.v[snd::loops] > 0.5f;
    c.oneShot = ins != nullptr && ins->session.oneShot && (int) c.v[snd::engine] != snd::synth;
    c.bendSemis = (float) (bend * c.v[snd::bend]);
    c.glideK = c.v[snd::glide] > 0.5f ? (float) (1.0 - std::exp (-1.0 / (c.v[snd::glide] * 0.001 * sr * 0.25))) : 1.0f;
    auto rates = [this] (float a, float d, float s, float r)
    {
        EnvRates e;
        const double att = (double) a * 0.001 * sr;
        e.attackStep = att < 1.0 ? 1.0f : (float) (1.0 / att);
        e.decayCoef = coefFor (d, sr);
        e.sustain = juce::jlimit (0.0f, 1.0f, s);
        e.releaseCoef = coefFor (r, sr);
        return e;
    };
    c.amp = rates (c.v[snd::attack], c.v[snd::decay], c.v[snd::sustain], c.v[snd::release]);
    c.flt = rates (c.v[snd::fAttack], c.v[snd::fDecay], c.v[snd::fSustain], c.v[snd::fRelease]);
    c.fs.model = (dsp::FilterModel) (int) c.v[snd::fModel];
    c.fs.cube = (int) c.v[snd::fCube];
    c.fs.y = c.v[snd::fY];
    c.fs.z = c.v[snd::fZ];
    c.fs.smoothMs = c.v[snd::fSmooth];
    c.fs.trimDb = c.v[snd::fTrim];
    c.fs.type = (dsp::FilterType) (int) c.v[snd::fType];
    c.fs.res = c.v[snd::reso];
    c.fs.drive = c.v[snd::fDrive];
    c.fs.sep = c.v[snd::sep];
    c.fs.fm = c.v[snd::fm];
    c.filterOn = c.fs.model != dsp::FilterModel::off;
    c.cutoffOct = std::log2 (juce::jmax (20.0f, c.v[snd::cutoff]) / 20.0f);
    c.envOct = c.v[snd::fEnv] * 6.0f;
}


// WTF?!: each of the engine's most telling settings pulled from where it is by up to the whole knob's amount, a new
// pull every eighth note, glided to (30 ms); LOOP draws the same ones (2 to 16, from its seed) again and again
void Player::applyWtf (double ppq, int n, Chunk& c)
{
    const float amount = c.v[snd::wtf];
    const int engineNow = (int) c.v[snd::engine];
    const bool synthEngine = engineNow == snd::synth;
    static const int synthIds[wtfCount] = { snd::oaPos, snd::obPos, snd::pmBA, snd::cutoff, snd::reso, snd::fEnv, snd::detune,
                                            snd::decay, snd::dAmt, snd::subLevel };
    static const int samplerIds[wtfCount] = { snd::start, snd::spray, snd::alias, snd::loopStart, snd::loopSize, snd::pmIndex,
                                              snd::cutoff, snd::reso, snd::decay, snd::speed };
    // (DRUM: the pulls go to its hits, DrumCore.h; here only the filter and the drive its tracks pass through)
    static const int drumIds[wtfCount] = { -1, -1, -1, -1, -1, -1, -1, -1, snd::cutoff, snd::reso };
    const int* ids = synthEngine ? synthIds : (engineNow == snd::drum ? drumIds : samplerIds);
    const auto step = (int64_t) std::floor (ppq * 2.0);
    const uint32_t seed = wtfSeed.load (std::memory_order_relaxed);
    const bool loop = (int) c.v[snd::wtfMode] == 1;
    if (step != wtfStep || seed != wtfSeedNow)
    {
        wtfStep = step;
        wtfSeedNow = seed;
        if (loop)
        {
            // LOOP: the draw of its place in the loop (the first seed, eight long: as before 0.4.5)
            const int len = juce::jlimit (2, 16, (int) c.v[snd::wtfLen]);
            const int at = (int) (((step % len) + len) % len);
            wtfRng = seed + (uint32_t) at * 2654435761u;
            if (wtfRng == 0)
                wtfRng = 1;
            wtfLoopStep.store (at, std::memory_order_relaxed);
        }
        else
            wtfLoopStep.store (-1, std::memory_order_relaxed);
        for (auto& t : wtfTarget)
            t = uniform (wtfRng) * 2.0f - 1.0f;
    }
    const float k = 1.0f - std::exp (-(float) n / (float) (0.03 * sr));
    for (int j = 0; j < wtfCount; ++j)
    {
        wtfNow[(size_t) j] += (wtfTarget[(size_t) j] * amount - wtfNow[(size_t) j]) * k;
        if (amount <= 0.0f && std::abs (wtfNow[(size_t) j]) < 1.0e-4f)
            wtfNow[(size_t) j] = 0.0f;
        if (! (std::abs (wtfNow[(size_t) j]) > 0.0f))
            continue;
        const int id = ids[j];
        if (id < 0)
            continue;
        const float norm = juce::jlimit (0.0f, 1.0f, snd::toNorm (id, c.v[id]) + wtfNow[(size_t) j]);
        c.v[id] = snd::fromNorm (id, norm);
        shownNorm[(size_t) id].store (norm, std::memory_order_relaxed);
        shownMoved[(size_t) id].store (true, std::memory_order_relaxed);
    }
}

//============================================================================== the chain
void Player::setFxOrder (const std::array<int, numStages>& order)
{
    int seen = 0, code = 0, mul = 1;
    for (int p = 0; p < numStages; ++p)
    {
        const int st = order[(size_t) p];
        if (st < 0 || st >= numStages || (seen & (1 << st)) != 0)
            return;
        seen |= 1 << st;
        code += st * mul;
        mul *= numStages;
    }
    orderWanted = code;
}

std::array<int, Player::numStages> Player::fxOrder() const
{
    std::array<int, numStages> o {};
    int code = orderWanted.load();
    for (int p = 0; p < numStages; ++p)
    {
        o[(size_t) p] = code % numStages;
        code /= numStages;
    }
    return o;
}

void Player::runChain (float* L, float* R, int n, const Chunk& c)
{
    const float rampStep = (float) n / (float) (0.02 * sr);     // on / off in 20 ms
    auto stage = [&] (int i, bool on, auto&& process)
    {
        float& w = wet[i];
        if (! on && w <= 0.0f)
            return;
        const float w0 = w;
        w = juce::jlimit (0.0f, 1.0f, w + (on ? rampStep : -rampStep));
        if (w0 >= 1.0f && w >= 1.0f)
        {
            process (L, R, n);
            return;
        }
        auto* dl = dry.getWritePointer (0);
        auto* dr = dry.getWritePointer (1);
        std::copy (L, L + n, dl);
        std::copy (R, R + n, dr);
        process (L, R, n);
        for (int i2 = 0; i2 < n; ++i2)
        {
            const float k = w0 + (w - w0) * (float) (i2 + 1) / (float) n;
            L[i2] = dl[i2] + k * (L[i2] - dl[i2]);
            R[i2] = dr[i2] + k * (R[i2] - dr[i2]);
        }
    };

    // the stages, in the order chosen (0 drive, the model kept while it fades out; 1..6 the effects)
    int code = orderNow;
    for (int pos = 0; pos < numStages; ++pos, code /= numStages)
    {
        switch (code % numStages)
        {
            case 0:
            {
                const int model = (int) c.v[snd::dModel];
                dsp::DriveSettings d;
                if (model != 0)
                    lastDriveModel = model;
                d.model = (dsp::DriveModel) lastDriveModel;
                d.drive = c.v[snd::dAmt];
                d.tone = c.v[snd::dTone];
                d.mix = c.v[snd::dMix];
                drive.set (d);
                stage (0, model != 0, [this] (float* l, float* r, int m) { drive.process (l, r, m); });
            }
                break;
            case 1:
            {
                dsp::SpectralSettings s;
                s.voices = (int) c.v[snd::spVoices];
                s.slice = c.v[snd::spSlice];
                s.blur = c.v[snd::spBlur];
                s.glide = c.v[snd::spGlide];
                s.centre = c.v[snd::spCentre];
                s.bandwidth = c.v[snd::spWidth];
                s.octave = c.v[snd::spOct];
                s.wave = (int) c.v[snd::spWave];
                s.freeze = c.v[snd::spFreeze] > 0.5f;
                s.feedback = c.v[snd::spFeedback];
                s.mix = c.v[snd::spMix];
                s.clock = (int) c.v[snd::spClock];
                s.mult = (int) c.v[snd::spMult];
                s.division = (int) c.v[snd::spDiv];
                s.notch = c.v[snd::spNotch] > 0.5f;
                s.transients = c.v[snd::spTrans] > 0.5f;
                s.fine = c.v[snd::spFine];
                s.warp = c.v[snd::spWarp];
                s.assign = (int) c.v[snd::spAssign];
                s.ppq = c.ppq;
                s.bpm = c.bpm;
                s.playing = c.playing;
                spectral.set (s);
                stage (1, c.v[snd::spOn] > 0.5f, [this] (float* l, float* r, int m) { spectral.process (l, r, m); });
            }
                break;
            case 2:
            {
                dsp::PitchSettings s;
                s.semis[0] = c.v[snd::piA];
                s.semis[1] = c.v[snd::piB];
                s.fine[0] = c.v[snd::piDetune];
                s.fine[1] = -c.v[snd::piDetune];
                s.delay[0] = c.v[snd::piDelay] * 0.001f;
                s.delay[1] = juce::jmin (1.0f, c.v[snd::piDelay] * 0.0015f);
                s.level[0] = 1.0f;
                s.level[1] = c.v[snd::piLevelB];
                s.feedback = c.v[snd::piFeedback];
                s.crystals = c.v[snd::piCrystals] > 0.5f;
                s.mix = c.v[snd::piMix];
                pitch.set (s);
                stage (2, c.v[snd::piOn] > 0.5f, [this] (float* l, float* r, int m) { pitch.process (l, r, m); });
            }
                break;
            case 3:
            {
                dsp::ChorusSettings s;
                s.mode = (int) c.v[snd::chMode];
                s.rate = c.v[snd::chRate];
                s.depth = c.v[snd::chDepth];
                s.feedback = c.v[snd::chFeedback];
                s.tone = c.v[snd::chTone];
                s.mix = c.v[snd::chMix];
                chorus.set (s);
                stage (3, c.v[snd::chOn] > 0.5f, [this] (float* l, float* r, int m) { chorus.process (l, r, m); });
            }
                break;
            case 4:
            {
                dsp::CrushSettings s;
                s.bits = c.v[snd::crBits];
                s.rateHz = (float) juce::jmin ((double) c.v[snd::crRate], sr);
                s.jitter = c.v[snd::crJitter];
                s.tone = c.v[snd::crTone];
                s.mix = c.v[snd::crMix];
                crush.set (s);
                stage (4, c.v[snd::crOn] > 0.5f, [this] (float* l, float* r, int m) { crush.process (l, r, m); });
            }
                break;
            case 5:
            {
                dsp::SpaceSettings s;
                s.size = c.v[snd::rvSize];
                s.decay = c.v[snd::rvDecay];
                s.tone = c.v[snd::rvTone];
                s.shimmer = c.v[snd::rvShimmer];
                s.mod = c.v[snd::rvMod];
                s.mix = c.v[snd::rvMix];
                s.dry = c.v[snd::rvDry];
                space.set (s);
                stage (5, c.v[snd::rvOn] > 0.5f, [this] (float* l, float* r, int m) { space.process (l, r, m); });
            }
                break;
            case 6:
            {
                // ECHO: TIME, or the division at the tempo (at most 5 s)
                static const double beats[] = { 0.0, 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
                const int sync = juce::jlimit (0, 10, (int) c.v[snd::ecSync]);
                dsp::EchoSettings s;
                s.time = sync == 0 ? c.v[snd::ecTime] * 0.001f
                                   : (float) juce::jmin (5.0, beats[sync] * 60.0 / juce::jmax (20.0, c.bpm));
                s.heads = (int) c.v[snd::ecHeads];
                s.spread = c.v[snd::ecSpread];
                s.feedback = c.v[snd::ecFeedback];
                s.tone = c.v[snd::ecTone];
                s.direction = (int) c.v[snd::ecDir];
                s.motion = (int) c.v[snd::ecMotion];
                s.sweep = c.v[snd::ecSweep];
                s.rate = c.v[snd::ecRate];
                s.width = c.v[snd::ecWidth];
                s.wear = c.v[snd::ecWear];
                s.mix = c.v[snd::ecMix];
                echo.set (s);
                const bool on = c.v[snd::ecOn] > 0.5f;
                if (on && wet[6] <= 0.0f)
                    echo.reset();            // (switched on: no echo of what played before)
                stage (6, on, [this] (float* l, float* r, int m) { echo.process (l, r, m); });
            }
                break;
            default:
                break;
        }
    }

    // COMP (SETTINGS), at the end: what it listens to, the host's signal at this chunk (when there is none, itself)
    {
        dsp::CompSettings cs;
        cs.inDb = c.v[snd::cpIn];
        cs.gateDb = c.v[snd::cpGate];
        cs.amount = c.v[snd::cpAmount];
        cs.attack = (int) c.v[snd::cpAttack];
        cs.release = (int) c.v[snd::cpRelease];
        cs.drive = c.v[snd::cpDrive];
        cs.mix = c.v[snd::cpMix];
        cs.levelDb = c.v[snd::cpLevel];
        comp.set (cs);
        const int key = juce::jlimit (0, 10, (int) c.v[snd::cpKey]);
        const int source = key == 0 ? -1 : (key == 1 ? 2 : (key == 2 ? 0 : key));   // SIDECHAIN 2, INPUT 0, DRUM n: 3 + n
        const float* kl = source >= 0 && follow[(size_t) source].l != nullptr ? follow[(size_t) source].l + chunkPos : nullptr;
        const float* kr = kl != nullptr && follow[(size_t) source].r != nullptr ? follow[(size_t) source].r + chunkPos : nullptr;
        stage (numStages, c.v[snd::cpOn] > 0.5f, [this, kl, kr] (float* l, float* r, int m) { comp.process (l, r, m, kl, kr); });
    }

    resampleChunk (L, R, n, c);

    // volume, gliding; a new order of the stages taken through a dip of 10 ms down and 10 back up
    const float target = juce::Decibels::decibelsToGain (c.v[snd::volume], -100.0f);
    const float g0 = volumeGain;
    volumeGain = target;
    const int wanted = orderWanted.load (std::memory_order_relaxed);
    if (orderPhase == 0 && wanted != orderNow)
        orderPhase = 1;
    const float dip = 1.0f / (float) (0.01 * sr);
    for (int i = 0; i < n; ++i)
    {
        float g = g0 + (target - g0) * (float) (i + 1) / (float) n;
        if (orderPhase == 1)
            orderGain = juce::jmax (0.0f, orderGain - dip);
        else if (orderPhase == 2)
        {
            orderGain = juce::jmin (1.0f, orderGain + dip);
            if (orderGain >= 1.0f)
                orderPhase = 0;
        }
        g *= orderGain;
        L[i] *= g;
        R[i] *= g;
    }
    if (orderPhase == 1 && orderGain <= 0.0f)
    {
        orderNow = wanted;          // silent: the next chunk runs in the new order, coming back up
        orderPhase = 2;
    }
}

//============================================================================== RÉÉCHANTILLONNER
bool Player::armResample (double maxSeconds, int bars, bool synced)
{
    if (rsState.load() != (int) Resample::idle || sr <= 0.0)
        return false;
    rsReady = false;
    rsStop = false;
    rsWritten = 0;
    rsTail = (int) (0.010 * sr);
    rsFade = (int) (0.020 * sr);
    rsCapacity = (int) (juce::jlimit (1.0, 600.0, maxSeconds) * sr) + rsTail;
    for (auto& b : rsBuf)
        b.assign ((size_t) rsCapacity, 0.0f);
    rsBars = juce::jmax (0, bars);
    rsSynced = synced;
    rsTarget = 0;
    rsHeadDone = false;
    rsState = (int) Resample::armed;
    return true;
}

void Player::stopResample()
{
    if (rsState.load() != (int) Resample::idle)
        rsStop = true;
}

float Player::resampleProgress() const
{
    const int st = rsState.load();
    if (st == (int) Resample::recording)
    {
        const int of = rsTarget > 0 ? rsTarget : rsCapacity - rsTail;
        return of > 0 ? juce::jlimit (0.0f, 1.0f, (float) rsWritten.load() / (float) of) : 0.0f;
    }
    return st >= (int) Resample::handing ? 1.0f : 0.0f;
}

rk::Channels Player::takeResample()
{
    rk::Channels out;
    if (! rsReady.load())
        return out;
    for (auto& b : rsBuf)
        out.emplace_back (b.begin(), b.begin() + rsTarget);
    rsReady = false;
    return out;
}

void Player::neutralise()
{
    neutral = true;
    resetEffects();
    for (auto& w : wet)
        w = 0.0f;
    for (auto& v : voices)
        v.active = false;
    for (int ch = 0; ch < 2; ++ch)
        inFilter[ch].reset();
}

void Player::resampleChunk (float* L, float* R, int n, const Chunk& c)
{
    int st = rsState.load (std::memory_order_relaxed);
    if (st == (int) Resample::idle || st == (int) Resample::looping)
        return;
    int i = 0;
    if (st == (int) Resample::armed)
    {
        if (rsStop.exchange (false))
        {
            rsState = (int) Resample::idle;
            return;
        }
        const double bpm = c.bpm > 0.0 ? c.bpm : 120.0;
        if (rsSynced && c.playing)
        {
            // from the next bar (4/4)
            const double next = std::ceil (c.ppq / 4.0 - 1.0e-9) * 4.0;
            i = (int) std::llround ((next - c.ppq) * 60.0 / bpm * sr);
            if (i >= n)
                return;
            i = juce::jmax (0, i);
        }
        const int room = rsCapacity - rsTail;
        rsTarget = rsBars > 0 ? juce::jlimit (1, room, (int) std::llround (rsBars * 4.0 * 60.0 / bpm * sr)) : 0;
        rsWritten = 0;
        st = (int) Resample::recording;
        rsState = st;
    }
    int written = rsWritten.load (std::memory_order_relaxed);
    bool postOnly = false;                       // the handover over in this chunk: the recording alone, after it
    for (; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        // the recording (its last 10 ms during the handover: what follows the loop's end, crossfaded into its start)
        if (st == (int) Resample::recording || (st == (int) Resample::handing && written < rsTarget + rsTail))
        {
            rsBuf[0][(size_t) written] = l;
            rsBuf[1][(size_t) written] = r;
            ++written;
            if (st == (int) Resample::recording && written >= rsCapacity - rsTail)
                rsStop = true;                   // (out of room)
        }
        if (st == (int) Resample::recording && ((rsTarget > 0 && written >= rsTarget) || rsStop.load (std::memory_order_relaxed)))
        {
            rsStop = false;
            if (rsTarget <= 0 || written < rsTarget)
                rsTarget = written;
            if (rsTarget < rsTail * 2)
            {
                rsState = (int) Resample::idle;      // too short to be anything: dropped
                rsWritten = 0;
                return;
            }
            st = (int) Resample::handing;
            rsState = st;
            rsFadePos = 0;
            rsRead = 0;
            rsHeadDone = false;
            continue;               // this frame is the loop's last, heard live; its start comes in on the next
        }
        if (st == (int) Resample::handing && ! rsHeadDone && written >= rsTarget + rsTail)
        {
            // the loop's start crossfaded from what followed its end (equal power)
            for (int j = 0; j < rsTail; ++j)
            {
                const float u = ((float) j + 0.5f) / (float) rsTail * juce::MathConstants<float>::halfPi;
                const float in = std::sin (u), out = std::cos (u);
                for (auto& b : rsBuf)
                    b[(size_t) j] = b[(size_t) (rsTarget + j)] * out + b[(size_t) j] * in;
            }
            rsHeadDone = true;
            rsReady = true;
        }
        if (st == (int) Resample::handing || postOnly)
        {
            const float a = rsBuf[0][(size_t) rsRead], b = rsBuf[1][(size_t) rsRead];
            rsRead = rsRead + 1 >= rsTarget ? 0 : rsRead + 1;
            if (postOnly)
            {
                L[i] = a;
                R[i] = b;
                continue;
            }
            const float u = ((float) rsFadePos + 0.5f) / (float) rsFade * juce::MathConstants<float>::halfPi;
            const float gNew = std::sin (u), gOld = std::cos (u);
            L[i] = l * gOld + a * gNew;
            R[i] = r * gOld + b * gNew;
            if (++rsFadePos >= rsFade && rsHeadDone)
            {
                // the live sound gone: the chain made neutral, the recording carried on into it from the next chunk
                neutralise();
                postOnly = true;
                rsGain = 1.0f;
                rsState = (int) Resample::looping;
                st = (int) Resample::looping;
            }
        }
    }
    rsWritten.store (written, std::memory_order_relaxed);
}

void Player::loopIntoChain (float* L, float* R, int n)
{
    const float step = 1.0f / (float) (0.03 * sr);
    const bool stopping = rsStop.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        if (stopping)
            rsGain = juce::jmax (0.0f, rsGain - step);
        L[i] += rsBuf[0][(size_t) rsRead] * rsGain;
        R[i] += rsBuf[1][(size_t) rsRead] * rsGain;
        rsRead = rsRead + 1 >= rsTarget ? 0 : rsRead + 1;
    }
    if (stopping && rsGain <= 0.0f)
    {
        rsStop = false;
        rsState = (int) Resample::idle;
    }
}

//============================================================================== render
void Player::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Transport& transport,
                     const float* const* input, int inputChannels, bool inputThroughFx)
{
    const int n = out.getNumSamples();
    if (n <= 0)
        return;
    if (n > bus.getNumSamples())
    {
        bus.setSize (2, n, false, true, true);        // (a host giving more than it said)
        dry.setSize (2, n, false, true, true);
    }
    setQuality (1 << juce::jlimit (0, 2, (int) get (snd::quality)));
    bus.clear (0, n);
    auto* L = bus.getWritePointer (0);
    auto* R = bus.getWritePointer (1);

    Chunk c;
    auto it = midi.begin();
    int pos = 0;
    while (pos < n)
    {
        // the chunk stops at the next event
        int end = juce::jmin (n, pos + chunkSize);
        for (auto p = it; p != midi.end(); ++p)
        {
            const int at = juce::jlimit (0, n - 1, (*p).samplePosition);
            if (at > pos)
            {
                end = juce::jmin (end, at);
                break;
            }
        }
        // what the followers hear: the input over this chunk
        if (input != nullptr && inputChannels > 0)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < juce::jmin (2, inputChannels); ++ch)
                for (int i = pos; i < end; ++i)
                    sum += input[ch][i] * input[ch][i];
            inputLevel = std::sqrt (sum / (float) ((end - pos) * juce::jmin (2, inputChannels))) * 1.41f;
        }
        else
            inputLevel = 0.0f;
        hearFollowers (pos, end);
        const int rs = rsState.load (std::memory_order_relaxed);
        bool sounding = inputThroughFx || rs >= (int) Resample::recording;
        for (auto& v : voices)
            sounding = sounding || v.active;
        modulate (pos, end - pos, transport, c, ! sounding);
        // the events at its start
        for (; it != midi.end(); ++it)
        {
            const auto meta = *it;
            if (juce::jlimit (0, n - 1, meta.samplePosition) > pos)
                break;
            const auto m = meta.getMessage();
            if (m.isNoteOn() && loopsOnNoteOn && rsState.load (std::memory_order_relaxed) == (int) Resample::looping)
                rsStop = true;          // a note takes over from the loop
            if (m.isNoteOn())
                noteOn (m.getNoteNumber(), m.getVelocity(), c);
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber(), c);
            else if (m.isPitchWheel())
            {
                bend = (m.getPitchWheelValue() - 8192) / 8192.0;
                c.bendSemis = (float) (bend * c.v[snd::bend]);
            }
            else if (m.isSustainPedalOn())
                sustain = true;
            else if (m.isSustainPedalOff())
            {
                sustain = false;
                for (int k = 0; k < 128; ++k)
                    if (sustained[(size_t) k])
                    {
                        sustained[(size_t) k] = false;
                        noteOff (k, c);
                    }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                allNotesOff (m.isAllSoundOff());
        }
        const int m = end - pos;
        c.n = m;
        c.inL = input != nullptr && inputChannels > 0 ? input[0] + pos : nullptr;
        c.inR = input != nullptr && inputChannels > 0 ? input[juce::jmin (1, inputChannels - 1)] + pos : nullptr;
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, L + pos, R + pos, m, c);
        {
            // the voices' safety high pass: two one-poles at 5 Hz (DC and infrasound off, the audible lows untouched)
            const float k = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 5.0 / sr));
            float sum = 0.0f;
            for (int i = pos; i < end; ++i)
            {
                float* chans[2] = { L, R };
                for (int ch = 0; ch < 2 && safetyHighPass; ++ch)
                {
                    float x = chans[ch][i];
                    dcState[ch][0] += k * (x - dcState[ch][0]);
                    x -= dcState[ch][0];
                    dcState[ch][1] += k * (x - dcState[ch][1]);
                    x -= dcState[ch][1];
                    chans[ch][i] = x;
                }
                sum += L[i] * L[i] + R[i] * R[i];
            }
            voiceLevel = std::sqrt (sum / (float) (2 * m)) * 1.41f;
        }

        // RÉÉCHANTILLONNER: the recording in place of the input
        const bool looping = rsState.load (std::memory_order_relaxed) == (int) Resample::looping;
        if (looping)
            loopIntoChain (L + pos, R + pos, m);
        // the input, in FX mode: through the filter (no envelope), then into the chain with the voices
        if (inputThroughFx && ! looping && input != nullptr && inputChannels > 0)
        {
            const float* a = input[0] + pos;
            const float* b = input[juce::jmin (1, inputChannels - 1)] + pos;
            if (c.filterOn)
            {
                for (int ch = 0; ch < 2; ++ch)
                    inFilter[ch].set (c.fs);
                float oct = inCutFrom < 0.0f ? c.cutoffOct : inCutFrom;
                const float inc = (c.cutoffOct - oct) / (float) m;
                inCutFrom = c.cutoffOct;
                for (int i = 0; i < m; ++i)
                {
                    oct += inc;
                    auto& f0 = inFilter[0];
                    auto& f1 = inFilter[1];
                    L[pos + i] += inOs[0].process (a[i], [&f0, oct] (float u) { return f0.tick (u, oct); });
                    R[pos + i] += inOs[1].process (b[i], [&f1, oct] (float u) { return f1.tick (u, oct); });
                }
            }
            else
                for (int i = 0; i < m; ++i)
                {
                    L[pos + i] += a[i];
                    R[pos + i] += b[i];
                }
        }
        chunkPos = pos;
        runChain (L + pos, R + pos, m, c);
        pos = end;
    }

    // the guard at the door: nothing that isn't a number gets out (the engine is reset instead), nothing over +6 dB
    // (a runaway stopped short; below, nothing is touched: the host does the rest)
    {
        bool sane = true;
        for (int ch = 0; ch < 2 && sane; ++ch)
        {
            const auto* x = bus.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (x[i]) || std::abs (x[i]) > 1000.0f)
                {
                    sane = false;
                    break;
                }
        }
        if (! sane)
        {
            bus.clear (0, n);
            for (auto& v : voices)
                v.active = false;
            for (auto& v : voices)
                for (int ch = 0; ch < 2; ++ch)
                {
                    v.filter[ch].reset();
                    v.os[ch].reset();
                }
            for (int ch = 0; ch < 2; ++ch)
            {
                inFilter[ch].reset();
                inOs[ch].reset();
            }
            resetEffects();
            for (auto& w : wet)
                w = 0.0f;
            dcState[0][0] = dcState[0][1] = dcState[1][0] = dcState[1][1] = 0.0f;
            rescues.fetch_add (1, std::memory_order_relaxed);
        }
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::clip (bus.getWritePointer (ch), bus.getReadPointer (ch), -2.0f, 2.0f, n);
    }

    // out
    const int numOut = out.getNumChannels();
    float peak = 0.0f;
    if (numOut == 1)
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (L[i] + R[i]);
            out.getWritePointer (0)[i] += x;
            peak = juce::jmax (peak, std::abs (x));
        }
    else if (numOut >= 2)
    {
        out.addFrom (0, 0, L, n);
        out.addFrom (1, 0, R, n);
        peak = juce::jmax (bus.getMagnitude (0, 0, n), bus.getMagnitude (1, 0, n));
    }
    outLevel = juce::jmax (peak, outLevel.load() * 0.9f);
    {
        int w = scopeWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            scope[(size_t) (w++ & (scopeSize - 1))].store (0.5f * (L[i] + R[i]), std::memory_order_relaxed);
        scopeWrite.store (w & (scopeSize - 1), std::memory_order_relaxed);
    }

    // what the editor shows: the newest voice
    int count = 0;
    const Voice* last = nullptr;
    for (auto& v : voices)
        if (v.active)
        {
            ++count;
            if (! v.fading && (last == nullptr || v.age > last->age))
                last = &v;
        }
    activeVoices = count;
    if (last != nullptr && last->synth)
    {
        // SYNTHÉ: no sample to show, the voice's phase (the oscillator's cycle drawn turning)
        shownSample = -1;
        shownPosition = (float) last->phA[0];
        for (auto& g : grainAt)
            g = -1.0f;
        filterEnv = last->flt.v;
    }
    else if (last != nullptr)
    {
        shownSample = last->index;
        // from the frames it reads to the recording's: a rendered key is the recording at another speed (its loop
        // tells by how much: it is cut after it)
        const int l = last->s->length();
        const auto& orig = ins->samples[(size_t) last->index];
        double scale = l > 0 ? (double) last->originalLength / (double) l : 0.0;
        if (last->s != &orig && last->s->loop.valid() && orig.loop.valid())
            scale = (double) orig.loop.length() / (double) last->s->loop.length();
        const bool elastic = get (snd::playMode) > 0.5f;
        shownPosition = (float) ((elastic ? last->scan : last->pos) * scale);
        for (int g = 0; g < maxGrains; ++g)
            grainAt[(size_t) g] = elastic && last->grains[(size_t) g].on ? (float) (last->grains[(size_t) g].pos * scale) : -1.0f;
        filterEnv = last->flt.v;
    }
    else
    {
        shownSample = -1;
        for (auto& g : grainAt)
            g = -1.0f;
        filterEnv = 0.0f;
    }
    if (wet[1] > 0.0f && ++peakCounter % 4 == 0)
    {
        float hz[maxPeaks], lv[maxPeaks];
        const int np = spectral.peaks (hz, lv, maxPeaks);
        for (int i = 0; i < maxPeaks; ++i)
        {
            peakHz[(size_t) i] = i < np ? hz[i] : 0.0f;
            peakLevel[(size_t) i] = i < np ? lv[i] : 0.0f;
        }
    }
    follow.fill ({});        // (the host's signals were this block's)
}
