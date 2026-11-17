#include "Player.h"
#include "../core/Sinc.h"

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
    const float* inL = nullptr;          // the audio input over this chunk (phase modulation)
    const float* inR = nullptr;
    int n = 0;
};

namespace
{
    // a gentle kernel for the middle of ALIASING (the slopes of the old samplers' interpolators)
    const rk::SincKernel& shortKernel()
    {
        static const rk::SincKernel k (6, 512, 3.0);
        return k;
    }
    constexpr int spliceLength = 144;     // 3 ms at 48 kHz: a splice smoothed
}

Player::Player()
{
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
                       + (double) c.v[snd::fine] * 0.01 + (double) c.bendSemis;
    r.step = rk::ratioOf (semis) * v.rateScale * (c.v[snd::reverse] > 0.5f ? -1.0 : 1.0);
    // far up: the nearest octave copy (a recording's), the kernel kept short and the result as clean
    r.octaves = nullptr;
    r.level = 0;
    if (ins != nullptr && v.index >= 0 && (size_t) v.index < ins->octaves.size() && v.s == &ins->samples[(size_t) v.index])
    {
        r.octaves = &ins->octaves[(size_t) v.index];
        const double astep = std::abs (r.step);
        if (astep > 1.5)
            r.level = juce::jlimit (0, 3, (int) std::ceil (std::log2 (astep / 1.5)));
    }
    r.alias = c.v[snd::alias];
    const float bits = c.v[snd::bits];
    r.bitsQ = bits < 31.5f ? std::exp2 (bits - 1.0f) : 0.0f;
    r.pm = (int) c.v[snd::pmSource];
    r.pmIndex = c.v[snd::pmIndex];
    if (r.pmIndex < 0.01f || (r.pm == 3 && c.inL == nullptr))
        r.pm = 0;
    // the sine sits on the note played; the second head reads the sample at that ratio of its speed
    const double playing = (double) v.pitch + (double) (int) c.v[snd::transpose] + (double) c.v[snd::fine] * 0.01 + (double) c.bendSemis;
    r.pmInc = rk::noteHz (playing) * (double) c.v[snd::pmRatio] / sr;
}

// One frame of the sample at `p` (both channels, one set of weights). ALIASING, after the scale the Assimil8or's
// manual describes (written here our way):
//   0             the band-limited sinc, its cutoff lowered as the pitch goes up (nothing folds back)
//   0 .. 25 %     the cutoff stops following the pitch upwards (aliasing creeps in when transposed up)
//   25 .. 36 %    a gentler, shorter kernel (the slope of the classic samplers' chips)
//   36 .. 51 %    towards linear interpolation
//   51 .. 89 %    towards drop sample (Mirage, SP-1200)
//   89 .. 100 %   past it: the interpolation turns high pass, the images louder than the sound
// Far up, it reads the recording's copy an octave (two, three) down, at half (a quarter...) the step.
void Player::readFrame (const Read& r, double p, float& left, float& right) const
{
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
    const double astep = std::abs (r.step) * scale;
    const float a = r.alias;
    auto sinc = [&] (double cutoff, const rk::SincKernel& K, float& o0, float& o1)
    {
        const double halfWidth = (double) K.zeroCrossings() / cutoff;
        const auto k0 = (int64_t) std::ceil (pp - halfWidth);
        const auto k1 = (int64_t) std::floor (pp + halfWidth);
        const bool plain = ! wraps && k0 >= 0 && k1 < len;
        double s0 = 0.0, s1 = 0.0, ws = 0.0;
        for (int64_t k = k0; k <= k1; ++k)
        {
            const float w = K (((double) k - pp) * cutoff);
            ws += w;
            s0 += (double) w * (double) (plain ? d0[k] : at (d0, k));
            if (d1 != nullptr)
                s1 += (double) w * (double) (plain ? d1[k] : at (d1, k));
        }
        const double inv = ws > 1.0e-9 ? 1.0 / ws : 0.0;
        o0 = (float) (s0 * inv);
        o1 = (float) (s1 * inv);
    };
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
            sinc (0.9 * juce::jmin (1.0, 1.0 / astep), rk::SincKernel::live(), o0, o1);
    }
    else if (a <= 0.25f)
        sinc (0.9 * std::pow (juce::jmin (1.0, 1.0 / astep), 1.0 - (double) a / 0.25), rk::SincKernel::live(), o0, o1);
    else if (a <= 0.36f)
    {
        float b0, b1;
        sinc (0.9, rk::SincKernel::live(), o0, o1);
        sinc (0.95, shortKernel(), b0, b1);
        blend (o0, o1, b0, b1, (a - 0.25f) / 0.11f);
    }
    else if (a <= 0.51f)
    {
        float b0, b1;
        sinc (0.95, shortKernel(), o0, o1);
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
        // past drop sample: less of the smooth part (drop minus linear: what is left is the images), the level held
        const float t = (a - 0.89f) / 0.11f;
        float l0, l1;
        drop (o0, o1);
        linear (l0, l1);
        const float g = 1.0f / (1.0f - 0.85f * t);
        o0 = (o0 - t * l0) * g;
        o1 = (o1 - t * l1) * g;
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
    auto frame = [&] (double p, float& l, float& rr)
    {
        if (r.pm != 0)
            p += (double) (r.pmIndex * pm);
        readFrame (r, p, l, rr);
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
            const int k = juce::jlimit (0, len - 1, (int) v.pmPos);
            pm = sm.audio[0][(size_t) k] * 1.5f;
        }
        else if (r.pm == 3)
            pm = juce::jlimit (-1.5f, 1.5f, 0.5f * (c.inL[i] + c.inR[i]));

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
                g.pos += r.step;
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
                readFrame (ra, v.attackPos, xl, xr);
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
                    readFrame (ra, v.attackPos, al, ar);
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
                    v.ghost += r.step;
                    --v.ghostLeft;
                }
                const double before = v.pos;
                v.pos += r.step;
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
                        v.ghost = before + r.step;
                        v.ghostLeft = spliceLength;
                    }
                }
            }
        }
        xl = quantise (xl);
        xr = stereo ? quantise (xr) : xl;
        const float fe = tick (v.flt, c.flt, false);
        if (c.filterOn)
        {
            const float oct = baseOct + c.envOct * fe;
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
void Player::modulate (int offset, int n, const Transport& t, Chunk& c)
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
            mod.input ((int) P (snd::mSource) == 0 ? inputLevel : voiceLevel);
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
    for (int i = 0; i < snd::count; ++i)
    {
        const float raw = get (i);
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

    c.n = n;
    c.elastic = c.v[snd::playMode] > 0.5f;
    c.loops = c.v[snd::loops] > 0.5f;
    c.oneShot = ins != nullptr && ins->session.oneShot;
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
    c.fs.type = (dsp::FilterType) (int) c.v[snd::fType];
    c.fs.res = c.v[snd::reso];
    c.fs.drive = c.v[snd::fDrive];
    c.fs.sep = c.v[snd::sep];
    c.fs.fm = c.v[snd::fm];
    c.filterOn = c.fs.model != dsp::FilterModel::off;
    c.cutoffOct = std::log2 (juce::jmax (20.0f, c.v[snd::cutoff]) / 20.0f);
    c.envOct = c.v[snd::fEnv] * 6.0f;
}

//============================================================================== the chain
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

    // drive (the model kept while it fades out)
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
        spectral.set (s);
        stage (1, c.v[snd::spOn] > 0.5f, [this] (float* l, float* r, int m) { spectral.process (l, r, m); });
    }
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
    {
        dsp::SpaceSettings s;
        s.size = c.v[snd::rvSize];
        s.decay = c.v[snd::rvDecay];
        s.tone = c.v[snd::rvTone];
        s.shimmer = c.v[snd::rvShimmer];
        s.mod = c.v[snd::rvMod];
        s.mix = c.v[snd::rvMix];
        space.set (s);
        stage (5, c.v[snd::rvOn] > 0.5f, [this] (float* l, float* r, int m) { space.process (l, r, m); });
    }

    // volume, gliding
    const float target = juce::Decibels::decibelsToGain (c.v[snd::volume], -100.0f);
    const float g0 = volumeGain;
    volumeGain = target;
    for (int i = 0; i < n; ++i)
    {
        const float g = g0 + (target - g0) * (float) (i + 1) / (float) n;
        L[i] *= g;
        R[i] *= g;
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
        modulate (pos, end - pos, transport, c);
        // the events at its start
        for (; it != midi.end(); ++it)
        {
            const auto meta = *it;
            if (juce::jlimit (0, n - 1, meta.samplePosition) > pos)
                break;
            const auto m = meta.getMessage();
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

        // the input, in FX mode: through the filter (no envelope), then into the chain with the voices
        if (inputThroughFx && input != nullptr && inputChannels > 0)
        {
            const float* a = input[0] + pos;
            const float* b = input[juce::jmin (1, inputChannels - 1)] + pos;
            if (c.filterOn)
            {
                for (int ch = 0; ch < 2; ++ch)
                    inFilter[ch].set (c.fs);
                const float oct = c.cutoffOct;
                for (int i = 0; i < m; ++i)
                {
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
        runChain (L + pos, R + pos, m, c);
        pos = end;
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
    if (last != nullptr)
    {
        shownSample = last->index;
        const int l = last->s->length();
        const double scale = l > 0 ? (double) last->originalLength / (double) l : 0.0;
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
}
