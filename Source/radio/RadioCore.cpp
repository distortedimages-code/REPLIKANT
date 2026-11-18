// REPLIK8 — RADIO (0.3.5): the receivers' feed and history, the listening and its air, what was heard, the pads
// (RadioCore.h). After IMPAKT's RADIO (v87), our own implementation.
#include "RadioCore.h"

#include <algorithm>
#include <cstring>

namespace radio
{
namespace
{
    constexpr int fifoMask = fifoFrames - 1, histMask = histFrames - 1, guardFrames = 16;
    constexpr float twoPi = 6.28318530717958647f;

    inline float hermite (float xm1, float x0, float x1, float x2, float t)
    {
        const float c = 0.5f * (x1 - xm1), v = x0 - x1, w = c + v, a = w + v + 0.5f * (x2 - x0), b = w + a;
        return ((a * t - b) * t + c) * t + x0;
    }
    inline float loopAt (const int16_t* b, int L, float p, int ch)
    {
        const int i = (int) p;
        const float t = p - (float) i;
        auto at = [&] (int k) { k %= L; if (k < 0) k += L; return (float) b[2 * k + ch]; };
        return hermite (at (i - 1), at (i), at (i + 1), at (i + 2), t) * (1.0f / 32768.0f);
    }
    inline float semis (float st)                    { return std::exp2 (st * (1.0f / 12.0f)); }
    inline float softTanh (float x)                  { return std::tanh (x); }
    inline bool bad (float x)                        { return ! std::isfinite (x) || std::abs (x) > 1.0e6f; }

    // the shifter's allpass chains: their outputs a quarter turn apart across the band (a known pair of 4-stage chains)
    const float HA[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    const float HB[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
}

float lenSixteenths (int i)
{
    static const float L[10] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
    return L[i < 0 ? 0 : (i > 9 ? 9 : i)];
}

float takeSeconds (int i)
{
    static const float S[6] = { 1, 2, 4, 8, 16, 30 };
    return S[i < 0 ? 0 : (i > 5 ? 5 : i)];
}

void Svf::set (float hz, float q, float sr)
{
    hz = std::min (std::max (hz, 10.0f), 0.49f * sr);
    g = std::tan (3.14159265358979f * hz / sr);
    k = 1.0f / std::max (q, 0.05f);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

//==============================================================================
void Bank::allocate()
{
    if (ready())
        return;
    for (auto& r : rx)
    {
        r.fifo.assign ((size_t) fifoFrames * 2, 0);
        r.hist.assign ((size_t) histFrames * 2, 0);
    }
    reset();
    isReady.store (true, std::memory_order_release);
}

void Bank::reset()
{
    for (auto& r : rx)
    {
        std::fill (r.fifo.begin(), r.fifo.end(), (int16_t) 0);
        std::fill (r.hist.begin(), r.hist.end(), (int16_t) 0);
        r.wr = 0; r.rd = 0; r.flushReq = 0; r.flushSeen = 0;
        r.state = none; r.kind = web; r.meterC = -32768;
        r.priming = 1; r.underruns = 0; r.fpos = 0.0f; r.ratio = 1.0f; r.gain = 0.0f;
        r.hw = 0; r.hacc = 0.0; r.sig = 0.0f;
        r.level = 0.0f; r.fill = 0.0f; r.speed = 1.0f; r.dropouts = 0; r.waiting = 1;
    }
}

// ---- the network's side
int Bank::push (int k, const int16_t* x, int n)
{
    if (k < 0 || k >= numRx || n <= 0 || ! ready())
        return 0;
    Rx& r = rx[k];
    const unsigned wr = (unsigned) r.wr.load (std::memory_order_relaxed), rd = (unsigned) r.rd.load (std::memory_order_acquire);
    const int room = fifoFrames - (int) (wr - rd) - guardFrames;
    if (n > room)
        n = room;
    if (n <= 0)
        return 0;
    int16_t* f = r.fifo.data();
    for (int i = 0; i < n; ++i)
    {
        const unsigned at = ((wr + (unsigned) i) & (unsigned) fifoMask) * 2u;
        f[at] = x[2 * i];
        f[at + 1] = x[2 * i + 1];
    }
    r.wr.store ((int) (wr + (unsigned) n), std::memory_order_release);
    return n;
}

int Bank::space (int k) const
{
    if (k < 0 || k >= numRx || ! ready())
        return 0;
    const Rx& r = rx[k];
    return fifoFrames - (int) ((unsigned) r.wr.load (std::memory_order_relaxed) - (unsigned) r.rd.load (std::memory_order_acquire)) - guardFrames;
}

void Bank::flush (int k)                           { if (k >= 0 && k < numRx) rx[k].flushReq.fetch_add (1, std::memory_order_acq_rel); }
void Bank::setState (int k, int st, int kind)
{
    if (k < 0 || k >= numRx)
        return;
    rx[k].kind.store (kind, std::memory_order_release);
    rx[k].state.store (st, std::memory_order_release);
}
void Bank::setMeter (int k, float dbm)             { if (k >= 0 && k < numRx) rx[k].meterC.store (dbm < -300.0f ? -32768 : (int) (dbm * 100.0f)); }

// ---- the audio thread: each receiver's feed read into its history
void Bank::advance (int n, double sr)
{
    if (! ready())
        return;
    const double due = (double) n * (double) rate / sr;
    const float sgA = 1.0f - std::exp (-(float) n / (float) (0.03 * sr));
    for (auto& r : rx)
    {
        const int st = r.state.load (std::memory_order_acquire), kind = r.kind.load (std::memory_order_acquire);
        const int fr = r.flushReq.load (std::memory_order_acquire);
        unsigned rd = (unsigned) r.rd.load (std::memory_order_relaxed);
        const unsigned wr = (unsigned) r.wr.load (std::memory_order_acquire);
        if (fr != r.flushSeen)
        {
            r.flushSeen = fr;
            rd = wr;
            r.priming = 1;
            r.fpos = 0.0f;
        }
        r.hacc += due;
        const int m = (int) r.hacc;
        r.hacc -= (double) m;
        int avail = (int) (wr - rd);
        const int target = kind == shortwave ? rate / 2 : rate;
        if (r.priming && avail >= target && (st == live || st == buffering))
        {
            r.priming = 0;
            if (avail > target + target / 2)            // (a burst: only what is needed kept)
            {
                rd = wr - (unsigned) target;
                avail = target;
            }
        }
        if (! r.priming)
        {
            // (far too much in hand: a burst after a stall: on to the newest, faded in again)
            if (avail > 2 * target)
            {
                rd = wr - (unsigned) target;
                avail = target;
                r.fpos = 0.0f;
                r.gain = 0.0f;
            }
            // a little faster when it has too much in hand, slower when too little (+-0.6 %: a few cents at most)
            const float e = std::max (-1.0f, std::min (1.0f, 3.0f * (float) (avail - target) / (float) target));
            r.ratio += (1.0f + 0.006f * e - r.ratio) * std::min (1.0f, (float) m / (2.0f * (float) rate));
        }
        else
            r.ratio = 1.0f;
        const int16_t* f = r.fifo.data();
        int16_t* h = r.hist.data();
        float lv = 0.0f;
        const float gT = 1.0f / 480.0f;
        for (int i = 0; i < m; ++i)
        {
            float s[2] = { 0.0f, 0.0f };
            if (! r.priming)
            {
                if (avail < 3)
                {
                    r.priming = 1;
                    ++r.underruns;
                }
                else
                {
                    for (int c = 0; c < 2; ++c)
                        s[c] = hermite (f[((rd - 1u) & (unsigned) fifoMask) * 2u + (unsigned) c], f[(rd & (unsigned) fifoMask) * 2u + (unsigned) c],
                                        f[((rd + 1u) & (unsigned) fifoMask) * 2u + (unsigned) c], f[((rd + 2u) & (unsigned) fifoMask) * 2u + (unsigned) c], r.fpos);
                    r.fpos += r.ratio;
                    while (r.fpos >= 1.0f && avail > 0)
                    {
                        r.fpos -= 1.0f;
                        ++rd;
                        --avail;
                    }
                }
            }
            // (the sound fades in and out with the signal, 10 ms: no click when the network runs dry)
            const float g = r.priming || st != live ? 0.0f : 1.0f;
            r.gain += (g - r.gain) * gT;
            const size_t at = (size_t) ((r.hw + i) & histMask) * 2;
            for (int c = 0; c < 2; ++c)
            {
                const float v = s[c] * r.gain;
                h[at + (size_t) c] = (int16_t) (v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v));
                lv = std::max (lv, std::abs (v) * (1.0f / 32768.0f));
            }
        }
        r.hw += m;
        r.rd.store ((int) rd, std::memory_order_release);
        r.sig += ((r.priming || st != live ? 0.0f : 1.0f) - r.sig) * sgA;
        r.level.store (std::max (lv, r.level.load (std::memory_order_relaxed) * 0.995f), std::memory_order_relaxed);
        r.fill.store ((float) avail / (float) rate, std::memory_order_relaxed);
        r.speed.store (r.ratio, std::memory_order_relaxed);
        r.dropouts.store (r.underruns, std::memory_order_relaxed);
        r.waiting.store (r.priming, std::memory_order_relaxed);
    }
}

float Bank::at (const Rx& r, double p, int ch) const
{
    const double fl = std::floor (p);
    const int64_t i = (int64_t) fl;
    const float t = (float) (p - fl);
    if (r.hist.empty())
        return 0.0f;
    const int16_t* h = r.hist.data();
    auto x = [&] (int64_t k) { return (float) h[(size_t) (k & histMask) * 2 + (size_t) ch]; };
    return hermite (x (i - 1), x (i), x (i + 1), x (i + 2), t) * (1.0f / 32768.0f);
}

//==============================================================================
Engine::Engine()
{
    for (auto& g : padGlow) g = 0.0f;
    for (auto& a : padAt) a = -1.0f;
}

void Engine::allocate()
{
    if (allocated())
        return;
    bank.allocate();
    for (auto& l : loops)
        l.assign ((size_t) loopFrames * 2, 0);
    makeHeard();
    reset();
    armed.store (true, std::memory_order_release);
}

void Engine::makeHeard()
{
    const size_t frames = (size_t) std::ceil (heardSeconds * sr);
    heardL.assign (frames, 0.0f);
    heardR.assign (frames, 0.0f);
    heardW = 0;
    recFrom = -1;
    peaks.assign (frames / (size_t) peakFrames + 1, 0.0f);
    peakW = 0;
    peakAcc = 0.0f;
    peakN = 0;
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    if (allocated())
        makeHeard();
    reset();
}

void Engine::reset()
{
    mode = -1;
    rxNow = -1;
    cur = -1;
    loopLen[0] = loopLen[1] = 0;
    for (auto& h : heads) h = {};
    std::memset (ax, 0, sizeof (ax));
    std::memset (ay, 0, sizeof (ay));
    adl[0] = adl[1] = 0.0f;
    shPh = 0.0f;
    std::memset (bh1, 0, sizeof (bh1)); std::memset (bh2, 0, sizeof (bh2));
    std::memset (bl1, 0, sizeof (bl1)); std::memset (bl2, 0, sizeof (bl2));
    std::memset (fdN, 0, sizeof (fdN));
    fdA = fdB = 1.0f; fdPh = 1.0f; fdF = 900.0f;
    htPh = 0.0f; htF = 600.0f + 1400.0f * rng.uni(); htT = htF;
    ckE = ckLp = 0.0f; ckLeft = 0;
    std::memset (pk, 0, sizeof (pk));
    sqE = 0.0f; sqG = 1.0f;
    dLp[0] = dLp[1] = 0.0f; dPh = swPh = 0.0f;
    for (auto& v : voices) v.active = false;
}

// a loop taken from the history (LOOP: its newest frames; DIG: somewhere further back)
void Engine::takeLoop (const Rx& r, int L, bool dig, float depth)
{
    const int b = cur < 0 ? 0 : 1 - cur;
    const double behind = 8.0;
    double end = (double) r.hw - behind;
    if (dig)
        end -= (double) (rng.uni() * std::max (1.0f, depth) * (float) rate);
    double st = std::floor (end) - (double) L;
    const double oldest = (double) r.hw - (double) (histFrames - 2 * rate);
    if (st < oldest)
        st = oldest;
    const int64_t s0 = (int64_t) st;
    int16_t* dst = loops[b].data();
    const int16_t* h = r.hist.data();
    for (int i = 0; i < L; ++i)
    {
        const size_t from = (size_t) ((s0 + i) & histMask) * 2;
        dst[2 * i] = h[from];
        dst[2 * i + 1] = h[from + 1];
    }
    loopLen[b] = L;
    cur = b;
}

void Engine::listen (float* L, float* R, int n, const Settings& s)
{
    const int k = std::max (0, std::min (numRx - 1, s.rx));
    const Rx& r = bank.rx[k];
    const float fsr = (float) sr, isr = 1.0f / fsr;
    const double inc = (double) rate / sr;
    const int st = r.state.load (std::memory_order_relaxed);
    const int md = std::max (0, std::min (2, s.mode));

    // the period of a loop (LENGTH, on the tempo), and where in it the transport is
    const double bpm = s.bpm > 1.0 ? s.bpm : 120.0;
    const double sixteenth = sr * 60.0 / bpm / 4.0;
    const double period = std::max (64.0, (double) lenSixteenths (s.len) * sixteenth);
    bool turn = false;
    if (md != mode || k != rxNow)
    {
        mode = md;
        rxNow = k;
        p = (double) r.hw - (double) n * inc - 8.0;
        cur = -1;
        heads[0] = {};
        heads[1] = {};
        el = 0.0;
        turn = true;
    }
    if (md != modeLive && s.playing)
    {
        const double beats = (double) lenSixteenths (s.len) / 4.0;
        double ph = std::fmod (s.ppq, beats);
        if (ph < 0.0)
            ph += beats;
        const double target = ph / beats * period;
        if (target + 1.0 < el)
            turn = true;              // (its turn came round, or the transport jumped back)
        el = target;
    }

    // TUNE: LIVE a shift (Hz), LOOP and DIG a transposition
    const float shHz = md == modeLive ? shiftHz (s.tune) : 0.0f;
    const bool shift = std::abs (shHz) > 0.5f;
    const float pinc = md == modeLive ? 1.0f : (float) inc * semis (s.tune);
    const int L0 = (int) std::max (64.0, std::min ((double) loopFrames, period * inc));
    // (a seam's crossfade: 4 ms, a quarter of the loop at most)
    const float xf = std::max (8.0f, std::min (0.004f * fsr, std::min ((float) period * 0.25f, (float) L0 / std::max (pinc, 1.0e-3f) * 0.25f)));
    const float xfInc = 1.0f / xf;

    // the air
    const float stat = clamp01 (s.stat), hiss = 0.22f * stat * stat;
    const bool tuningNow = md == modeLive && (st == connecting || st == buffering || st == tuning);
    const float dead = tuningNow ? (0.06f + 0.25f * stat) * (1.0f - r.sig) : 0.0f;
    const float bandV = clamp01 (s.band), fade = clamp01 (s.fade), het = clamp01 (s.whistle), crk = clamp01 (s.crackle), drv = clamp01 (s.drive);
    const bool band = bandV > 0.01f;
    Svf h1, h2, l1, l2, nt;
    if (band)
    {
        const float bw = bandHz (bandV), c = std::max (300.0f, std::min (3000.0f, s.centre));
        const float lo = std::max (40.0f, c - 0.5f * bw), hi = std::min (c + 0.5f * bw, 0.45f * fsr);
        h1.set (lo, 0.5412f, fsr); h2.set (lo, 1.3066f, fsr);
        l1.set (std::max (hi, lo * 1.2f), 0.5412f, fsr); l2.set (std::max (hi, lo * 1.2f), 1.3066f, fsr);
    }
    // the fading: a new depth to fade to every 1 / RATE seconds (deep ones rarer), a notch wandering with it
    const float fdInc = std::max (0.01f, s.fadeRate) * isr;
    if (fade > 0.002f)
        nt.set (fdF, 1.2f, fsr);
    // the whistle wanders, slowly
    if (het > 0.002f)
    {
        if (rng.uni() < (float) n * isr * 0.3f)
            htT = 300.0f + 2200.0f * rng.uni();
        htF += (htT - htF) * std::min (1.0f, (float) n * isr * 0.8f);
    }
    const float htInc = htF * isr, htLvl = 0.12f * het * het;
    const float ckRate = 60.0f * crk * crk * isr;
    const float dg = 1.0f + 10.0f * drv * drv, dmix = std::min (1.0f, drv * 8.0f), dnorm = 1.0f / std::tanh (dg);
    const float dlpA = 1.0f - std::exp (-twoPi * (5200.0f - 3000.0f * drv) * isr);
    // the squelch (LIVE): its threshold, the signal's envelope (2 ms up, 25 ms down: it shuts between words)
    const bool sql = md == modeLive && s.squelch > 0.005f;
    const float sqThr = std::pow (10.0f, squelchDb (s.squelch) / 20.0f);
    const float sqUp = 1.0f - std::exp (-isr / 0.002f), sqDn = 1.0f - std::exp (-isr / 0.025f);
    const float sgOpen = 1.0f / (0.002f * fsr), sgClose = 1.0f / (0.03f * fsr);
    const float shInc = shHz * isr;
    const float level = clamp01 (s.level) * 1.25f;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float x[2] = { 0.0f, 0.0f };
        if (md == modeLive)
        {
            // (kept just behind what is written, inside the history)
            const double hi = (double) r.hw - 4.0, lo = (double) r.hw - (double) (histFrames - 4096);
            if (p > hi) p = hi;
            if (p < lo) p = lo;
            x[0] = bank.at (r, p, 0);
            x[1] = bank.at (r, p, 1);
            p += inc;
        }
        else
        {
            // at the period's turn: a new loop (LOOP: by chance, GRAB; DIG: always, from further back), from its start
            el += 1.0;
            if (el >= period)
            {
                el -= period;
                turn = true;
            }
            const int mh = heads[0].on && heads[0].dg >= 0.0f ? 0 : (heads[1].on && heads[1].dg >= 0.0f ? 1 : -1);
            bool seam = turn || mh < 0;
            if (turn)
            {
                const bool take = md == modeDig || cur < 0 || rng.uni() < s.grab;
                if (take)
                    takeLoop (r, L0, md == modeDig, s.depth);
                turn = false;
            }
            // (its end reached before the turn, played faster: from its start again)
            if (! seam && mh >= 0 && heads[mh].pos + pinc * xf >= (float) loopLen[headBuf[mh]])
                seam = true;
            if (seam && cur >= 0)
            {
                const int nw = mh >= 0 ? 1 - mh : 0;
                if (mh >= 0)
                    heads[mh].dg = -xfInc;
                heads[nw] = { 0.0f, 0.0f, xfInc, true };
                headBuf[nw] = cur;
            }
            for (int h = 0; h < 2; ++h)
            {
                Head& hd = heads[h];
                if (! hd.on)
                    continue;
                const int b = headBuf[h];
                const int Lb = loopLen[b];
                if (Lb <= 0)
                {
                    hd.on = false;
                    continue;
                }
                const float g = hd.g * hd.g * (3.0f - 2.0f * hd.g);
                x[0] += loopAt (loops[b].data(), Lb, hd.pos, 0) * g;
                x[1] += loopAt (loops[b].data(), Lb, hd.pos, 1) * g;
                hd.pos += pinc;
                if (hd.pos >= (float) Lb)
                    hd.pos -= (float) Lb;
                hd.g += hd.dg;
                if (hd.g >= 1.0f) { hd.g = 1.0f; hd.dg = 0.0f; }
                if (hd.g <= 0.0f) { hd.g = 0.0f; hd.on = false; }
            }
            const int m2 = heads[0].on && heads[0].dg >= 0.0f ? 0 : 1;
            if (heads[m2].on && loopLen[headBuf[m2]] > 0)
                loopPos.store (heads[m2].pos / (float) loopLen[headBuf[m2]], std::memory_order_relaxed);
        }
        // TUNE on LIVE: the signal moved up or down (a quarter turn apart: one chain a sample later)
        if (shift)
        {
            shPh += shInc;
            shPh -= std::floor (shPh);
            const float cs = std::cos (twoPi * shPh), sn = std::sin (twoPi * shPh);
            for (int c = 0; c < 2; ++c)
            {
                float a = x[c], b = adl[c];
                adl[c] = x[c];
                for (int j = 0; j < 4; ++j)
                {
                    const float ca = HA[j] * HA[j], cb = HB[j] * HB[j];
                    const float ya = ca * (a + ay[c][j][1]) - ax[c][j][1];
                    ax[c][j][1] = ax[c][j][0]; ax[c][j][0] = a; ay[c][j][1] = ay[c][j][0]; ay[c][j][0] = ya; a = ya;
                    const float yb = cb * (b + ay[c][4 + j][1]) - ax[c][4 + j][1];
                    ax[c][4 + j][1] = ax[c][4 + j][0]; ax[c][4 + j][0] = b; ay[c][4 + j][1] = ay[c][4 + j][0]; ay[c][4 + j][0] = yb; b = yb;
                }
                x[c] = a * cs + b * sn;
            }
        }
        // the squelch listens to the signal itself
        if (sql)
        {
            const float e = std::max (std::abs (x[0]), std::abs (x[1]));
            sqE += (e - sqE) * (e > sqE ? sqUp : sqDn);
            const float gT = sqE > sqThr ? 1.0f : 0.0f;
            sqG += gT > sqG ? std::min (sgOpen, gT - sqG) : -std::min (sgClose, sqG - gT);
        }
        else
            sqG = 1.0f;
        // the fading
        if (fade > 0.002f)
        {
            fdPh += fdInc;
            if (fdPh >= 1.0f)
            {
                fdPh -= 1.0f;
                fdA = fdB;
                const float u = rng.uni();
                fdB = 1.0f - u * u;
                fdF = 300.0f * std::pow (10.0f, rng.uni());
                nt.set (fdF, 1.2f, fsr);
            }
            const float w = fdPh * fdPh * (3.0f - 2.0f * fdPh);
            const float g = fdA + (fdB - fdA) * w;
            for (int c = 0; c < 2; ++c)
            {
                float lp, bp, hp;
                nt.tick (x[c], fdN[c][0], fdN[c][1], lp, bp, hp);
                x[c] = (x[c] - fade * nt.k * bp) * (1.0f - fade + fade * g);
            }
        }
        // the whistle, the hiss (and the dial's sweep while it tunes), the crackle
        dPh += isr * 0.37f;
        dPh -= std::floor (dPh);
        if (htLvl > 0.0f)
        {
            htPh += htInc * (1.0f + 0.003f * std::sin (twoPi * dPh));
            htPh -= std::floor (htPh);
            const float w = htLvl * std::sin (twoPi * htPh) * (0.4f + 0.6f * r.sig);
            x[0] += w;
            x[1] += w;
        }
        if (hiss > 0.0f || dead > 0.0f)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float w = rng.bi();
                pk[c][0] = 0.997f * pk[c][0] + 0.03f * w;
                pk[c][1] = 0.96f * pk[c][1] + 0.12f * w;
                pk[c][2] = 0.6f * pk[c][2] + 0.3f * w;
                x[c] += (pk[c][0] + pk[c][1] + pk[c][2] + 0.2f * w) * 1.3f * (hiss + dead);
            }
            if (dead > 0.0f)
            {
                // the dial's sweep: a whistle falling through the band, again and again
                swPh += isr * 0.7f;
                swPh -= std::floor (swPh);
                const float f = 3200.0f - 3000.0f * swPh;
                htPh += f * isr;
                htPh -= std::floor (htPh);
                const float w = dead * 0.25f * std::sin (twoPi * htPh) * (1.0f - swPh);
                x[0] += w;
                x[1] += w;
            }
        }
        if (ckRate > 0.0f)
        {
            if (ckLeft <= 0 && rng.uni() < ckRate)
            {
                const float u = rng.uni();
                const bool crash = crk > 0.7f && u < 0.05f;
                ckLeft = (int) ((crash ? 0.25f + 0.3f * rng.uni() : 0.001f + 0.015f * u * u) * fsr);
                ckE = (crash ? 0.5f : 0.15f + 0.7f * u * u * u) * (0.3f + 0.7f * crk);
            }
            if (ckLeft > 0)
            {
                --ckLeft;
                ckLp += (rng.bi() - ckLp) * 0.5f;
                x[0] += ckLp * ckE;
                x[1] += ckLp * ckE;
                ckE *= 0.9993f;
            }
        }
        // the receiver's band
        if (band)
            for (int c = 0; c < 2; ++c)
            {
                float lp, bp, hp;
                h1.tick (x[c], bh1[c][0], bh1[c][1], lp, bp, hp);
                h2.tick (hp, bh2[c][0], bh2[c][1], lp, bp, hp);
                l1.tick (hp, bl1[c][0], bl1[c][1], lp, bp, hp);
                l2.tick (lp, bl2[c][0], bl2[c][1], lp, bp, hp);
                x[c] = lp;
            }
        // the detector and the speaker pushed
        if (dmix > 0.0f)
            for (int c = 0; c < 2; ++c)
            {
                const float y = softTanh (dg * (x[c] + 0.25f * drv * x[c] * x[c])) * dnorm;
                dLp[c] += (y - dLp[c]) * dlpA;
                x[c] += (dLp[c] - x[c]) * dmix;
            }
        const float g = level * sqG;
        L[i] = x[0] * g;
        R[i] = x[1] * g;
        peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i])));
    }
    if (md == modeLive)
        loopPos.store (-1.0f, std::memory_order_relaxed);
    loopSeconds.store (md == modeLive || cur < 0 ? 0.0f : (float) loopLen[cur] / (float) rate, std::memory_order_relaxed);
    gateOpen.store (sqG, std::memory_order_relaxed);
    heardLevel.store (std::max (peak, heardLevel.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);

    // (nothing that isn't a number stays in the filters)
    bool reset = false;
    for (int c = 0; c < 2; ++c)
        reset = reset || bad (ay[c][0][0]) || bad (bh1[c][0]) || bad (bl2[c][1]) || bad (fdN[c][0]) || bad (dLp[c]);
    if (reset)
    {
        std::memset (ax, 0, sizeof (ax)); std::memset (ay, 0, sizeof (ay));
        std::memset (bh1, 0, sizeof (bh1)); std::memset (bh2, 0, sizeof (bh2));
        std::memset (bl1, 0, sizeof (bl1)); std::memset (bl2, 0, sizeof (bl2));
        std::memset (fdN, 0, sizeof (fdN));
        dLp[0] = dLp[1] = 0.0f;
        for (int i = 0; i < n; ++i)
            L[i] = R[i] = 0.0f;
    }
}

//==============================================================================
void Engine::render (float* L, float* R, int n, const Settings& s, const Event* ev, int numEvents)
{
    if (n <= 0)
        return;
    if (! allocated())
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        usedTake.store (nextTake.load (std::memory_order_acquire), std::memory_order_release);
        return;
    }
    // the pads' take (a new one: the pads playing the old one stop)
    const Take* t = nextTake.load (std::memory_order_acquire);
    if (t != take)
    {
        take = t;
        for (auto& v : voices)
            v.active = false;
    }
    usedTake.store (take, std::memory_order_release);

    bank.advance (n, sr);
    listen (L, R, n, s);

    // what was heard, kept (the takes come from it), and its peaks for the screen
    const int64_t w0 = heardW.load (std::memory_order_relaxed);
    const int64_t cap = (int64_t) heardL.size();
    for (int i = 0; i < n; ++i)
    {
        const size_t at = (size_t) ((w0 + i) % cap);
        heardL[at] = L[i];
        heardR[at] = R[i];
        peakAcc = std::max (peakAcc, std::max (std::abs (L[i]), std::abs (R[i])));
        if (++peakN >= peakFrames)
        {
            const int64_t pw = peakW.load (std::memory_order_relaxed);
            peaks[(size_t) (pw % (int64_t) peaks.size())] = peakAcc;
            peakW.store (pw + 1, std::memory_order_release);
            peakAcc = 0.0f;
            peakN = 0;
        }
    }
    heardW.store (w0 + n, std::memory_order_release);

    // the pads, struck at their events
    int pos = 0;
    for (int e = 0; e < numEvents; ++e)
    {
        const int at = std::max (pos, std::min (n, ev[e].at));
        if (at > pos)
            pads (L + pos, R + pos, at - pos, s);
        pos = at;
        if (ev[e].velocity > 0)
            padOn (ev[e].note, ev[e].velocity, s);
        else
            padOff (ev[e].note);
    }
    if (pos < n)
        pads (L + pos, R + pos, n - pos, s);
}

//==============================================================================
bool Engine::copyHeard (int64_t from, int64_t to, Take::Audio& out) const
{
    if (! allocated())
        return false;
    const int64_t cap = (int64_t) heardL.size();
    const int64_t margin = (int64_t) (2.0 * sr);
    from = std::max (from, std::max ((int64_t) 0, to - (cap - margin)));
    from = std::max (from, to - (int64_t) (longestTake * sr));
    if (to - from < 64)
        return false;
    const size_t len = (size_t) (to - from);
    out.l.resize (len);
    out.r.resize (len);
    out.sampleRate = sr;
    for (size_t i = 0; i < len; ++i)
    {
        const size_t at = (size_t) ((from + (int64_t) i) % cap);
        out.l[i] = heardL[at];
        out.r[i] = heardR[at];
    }
    return true;
}

bool Engine::grab (double seconds, Take::Audio& out) const
{
    const int64_t to = heardW.load (std::memory_order_acquire);
    return copyHeard (to - (int64_t) (seconds * sr), to, out);
}

void Engine::recStart()
{
    recFrom.store (heardW.load (std::memory_order_acquire));
}

double Engine::recSeconds() const
{
    const int64_t f = recFrom.load();
    return f < 0 ? 0.0 : (double) (heardW.load (std::memory_order_acquire) - f) / sr;
}

bool Engine::recStop (Take::Audio& out)
{
    const int64_t f = recFrom.exchange (-1);
    if (f < 0)
        return false;
    return copyHeard (f, heardW.load (std::memory_order_acquire), out);
}

//==============================================================================
void Engine::padOn (int note, int velocity, const Settings& s)
{
    const int pad = note - firstPadNote;
    if (take == nullptr || take->audio == nullptr || pad < 0 || pad >= (int) take->slices.size() || pad >= numPads)
        return;
    const auto& sl = take->slices[(size_t) pad];
    const int len = take->length();
    const int a = std::max (0, std::min (len - 1, sl.start)), b = std::max (a + 1, std::min (len, sl.end));
    if (b - a < 8)
        return;
    const float fade = 1.0f / (0.005f * (float) sr);
    if (s.choke)
        for (auto& v : voices)
            if (v.active)
                v.fadeStep = -fade;
    // a free voice, else the one nearest its end
    PadVoice* use = nullptr;
    for (auto& v : voices)
        if (! v.active)
        {
            use = &v;
            break;
        }
    if (use == nullptr)
    {
        use = &voices[0];
        for (auto& v : voices)
            if (v.env * v.fade < use->env * use->fade)
                use = &v;
    }
    PadVoice& v = *use;
    v = {};
    v.active = true;
    v.pad = pad;
    v.note = note;
    v.start = a;
    v.end = b;
    v.pos = (double) a;
    v.step = take->audio->sampleRate / sr * (double) semis (s.padTune);
    v.gain = clamp01 (s.padLevel) * 1.25f * (0.35f + 0.65f * (float) velocity / 127.0f);
    v.env = 1.0f;
    v.hold = s.padDecay >= 0.99f;
    v.coef = v.hold ? 1.0f : std::exp (-1.0f / (padDecaySeconds (s.padDecay) * (float) sr));
    v.fade = 0.0f;
    v.fadeStep = 1.0f / (0.0008f * (float) sr);
    padGlow[(size_t) pad].store (1.0f, std::memory_order_relaxed);
}

void Engine::padOff (int)
{
    // (one shots: a pad plays on until its slice ends, or its decay)
}

void Engine::pads (float* L, float* R, int n, const Settings&)
{
    if (take == nullptr || take->audio == nullptr)
        return;
    const auto& A = *take->audio;
    const float* l = A.l.data();
    const float* r = A.r.data();
    const int len = (int) A.l.size();
    const double endFade = 0.003 * A.sampleRate;
    std::array<float, numPads> where;
    where.fill (-1.0f);
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        for (int i = 0; i < n; ++i)
        {
            if (v.pos >= (double) v.end - 1.0)
            {
                v.active = false;
                break;
            }
            const int k = (int) v.pos;
            const float t = (float) (v.pos - (double) k);
            auto at = [len] (const float* x, int j) { return x[j < 0 ? 0 : (j >= len ? len - 1 : j)]; };
            const float sl = hermite (at (l, k - 1), at (l, k), at (l, k + 1), at (l, k + 2), t);
            const float sr_ = hermite (at (r, k - 1), at (r, k), at (r, k + 1), at (r, k + 2), t);
            const float tail = (float) std::min (1.0, ((double) v.end - v.pos) / endFade);
            const float g = v.gain * v.env * v.fade * tail;
            L[i] += sl * g;
            R[i] += sr_ * g;
            v.env *= v.coef;
            v.fade += v.fadeStep;
            if (v.fade >= 1.0f && v.fadeStep > 0.0f)
            {
                v.fade = 1.0f;
                v.fadeStep = 0.0f;
            }
            if ((v.fade <= 0.0f && v.fadeStep < 0.0f) || v.env < 1.0e-4f)
            {
                v.active = false;
                break;
            }
            v.pos += v.step;
        }
        if (v.active)
            where[(size_t) v.pad] = (float) ((v.pos - (double) v.start) / std::max (1.0, (double) (v.end - v.start)));
    }
    for (int p = 0; p < numPads; ++p)
        padAt[(size_t) p].store (where[(size_t) p], std::memory_order_relaxed);
}
}
