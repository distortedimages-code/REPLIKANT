#include "Orbite.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
    namespace
    {
        inline double wrap01 (double p)      { return p - std::floor (p); }
        inline float ringOf (float x, float r) { return (1.0f - r) + r * x; }

        // the steps a wave takes on its way from phase a to phase b (unwrapped: b < a when the phase runs back), over
        // a stretch of the sample that ends `dEnd` samples before the sample's end and lasts `span` of it: each step's
        // polyBLEP split between the sample before it (`before`) and the sample after (`after`)
        template <typename W>
        void steps (const W& w, double edge, double a, double b, double dEnd, double span, float& before, float& after)
        {
            if (! (std::abs (b - a) > 1.0e-12))
                return;
            const double lo = std::min (a, b), hi = std::max (a, b), dir = b > a ? 1.0 : -1.0;
            int guard = 0;
            for (double base = std::floor (lo); base <= hi && guard < 8; base += 1.0, ++guard)
            {
                const double xs[2] = { base, base + edge };
                for (double x : xs)
                {
                    if (! (x > lo && x <= hi))
                        continue;
                    const float J = (float) dir * (w (wrap01 (x + 1.0e-9)) - w (wrap01 (x - 1.0e-9)));
                    if (std::abs (J) < 1.0e-4f)
                        continue;
                    const double d = dEnd + span * (b - x) / (b - a);       // from the step to the sample's end
                    before += 0.5f * J * (float) (d * d);
                    after -= 0.5f * J * (float) ((1.0 - d) * (1.0 - d));
                }
            }
        }
        inline void step1 (float J, double d, float& before, float& after)
        {
            before += 0.5f * J * (float) (d * d);
            after -= 0.5f * J * (float) ((1.0 - d) * (1.0 - d));
        }
    }

    float Orbite::satelliteWave (double p, int wave, float s)
    {
        switch (wave)
        {
            case 0:
            {
                // a triangle leaning from one saw to the other
                const double q = 0.5 + 0.49 * (double) s;
                return p < q ? (float) (-1.0 + 2.0 * p / q) : (float) (1.0 - 2.0 * (p - q) / (1.0 - q));
            }
            case 1:
            {
                // a saw with a kink half way (down, none, up)
                const double k = 0.5 + 0.4 * (double) s;
                const double y = p < 0.5 ? p * 2.0 * k : k + (p - 0.5) * 2.0 * (1.0 - k);
                return (float) (2.0 * y - 1.0);
            }
            default:
                return p < 0.5 + 0.48 * (double) s ? 1.0f : -1.0f;
        }
    }

    float Orbite::carrierWave (double p, float shape, float sym)
    {
        if (shape <= 1.0f)
        {
            const float t = std::max (0.0f, shape);
            return (1.0f - t) * satelliteWave (p, 0, sym) + t * satelliteWave (p, 1, sym);
        }
        const float t = std::min (1.0f, shape - 1.0f);
        return (1.0f - t) * satelliteWave (p, 1, sym) + t * satelliteWave (p, 2, sym);
    }

    Orbite::Orbite()
    {
        down4.init (halfband (19, 8.0));
        down.init (halfband (47, 9.0));
        reset();
    }

    void Orbite::reset()
    {
        car = {};
        sat[0] = {};
        sat[1] = {};
        fb = lastA = 0.0f;
        orbitWas = 0.0;
        fresh = true;
        down4.reset();
        down.reset();
    }

    float Orbite::tick (const OrbiteSettings& s, double incC, double inc1, double inc2, double orbit)
    {
        // the carrier's clock: where it wraps in this sample (the satellites restart there)
        double dm = -1.0;
        car.ph += incC;
        if (car.ph >= 1.0)
        {
            dm = incC > 0.0 ? std::clamp ((car.ph - 1.0) / incC, 0.0, 0.999999) : 0.0;
            car.ph -= std::floor (car.ph);
        }

        float sats[2];
        const double push = s.algo == 3 ? 0.4 * (double) fb : 0.0;     // BOUCLE: the output back into their phase
        for (int k = 0; k < 2; ++k)
        {
            auto& o = sat[k];
            const double inc = k == 0 ? inc1 : inc2;
            const double off = (k == 1 ? (double) s.phase2 + orbit : 0.0) + push;
            const int wave = s.wave[k];
            const float sym = s.symS[k];
            auto w = [wave, sym] (double p) { return satelliteWave (p, wave, sym); };
            const double edge = wave == 2 ? 0.5 + 0.48 * (double) sym : 0.5;
            float before = 0.0f, after = 0.0f;
            const double a = o.eff;
            double b;
            if (s.sync[k] && dm >= 0.0)
            {
                // on to the carrier's wrap, then back to its restart phase, then on
                const double pre = a + inc * (1.0 - dm) + (off - o.offPrev);
                steps (w, edge, a, pre, dm, 1.0 - dm, before, after);
                step1 (w (wrap01 (off)) - w (wrap01 (pre)), dm, before, after);
                b = off + inc * dm;
                steps (w, edge, off, b, 0.0, dm, before, after);
            }
            else
            {
                b = a + inc + (off - o.offPrev);
                steps (w, edge, a, b, 0.0, 1.0, before, after);
            }
            // (the phase kept within a cycle; the offset as it is: next time only its change counts)
            o.offPrev = off;
            o.eff = b - std::floor (b);
            o.pend += before;
            sats[k] = o.pend;
            o.pend = w (o.eff) + after;
        }

        // the carrier (FM: satellite 1 pushes its phase, through zero)
        {
            const double off = s.algo == 2 ? 1.5 * (double) s.ring[0] * (double) lastA : 0.0;
            auto w = [&s] (double p) { return carrierWave (p, s.shape, s.sym); };
            const double edge = 0.5 + 0.48 * (double) s.sym;
            float before = 0.0f, after = 0.0f;
            const double a = car.eff;
            const double b = a + incC + (off - car.offPrev);
            steps (w, edge, a, b, 0.0, 1.0, before, after);
            car.offPrev = off;
            car.eff = b - std::floor (b);
            car.pend += before;
            const float c = car.pend;
            car.pend = w (car.eff) + after;
            lastA = sats[0];

            const float A = sats[0], B = sats[1], r1 = s.ring[0], r2 = s.ring[1];
            float y;
            switch (s.algo)
            {
                case 1:  y = c * ringOf (A * ringOf (B, r2), r1); break;
                case 2:  y = c * ringOf (B, r2); break;
                default: y = c * ringOf (A, r1) * ringOf (B, r2); break;
            }
            fb = y;
            return y;
        }
    }

    float Orbite::process (const OrbiteSettings& s, double incC, double inc1, double inc2, double orbit)
    {
        if (fresh)
        {
            orbitWas = orbit;
            fresh = false;
        }
        // four steps at four times the rate, brought down in two halfband stages
        const double q = 0.25, dOrbit = orbit - orbitWas;
        float y[4];
        for (int k = 0; k < 4; ++k)
            y[k] = tick (s, incC * q, inc1 * q, inc2 * q, orbitWas + dOrbit * (double) (k + 1) * q);
        orbitWas = orbit;
        const float a = down4.down (y[0], y[1]);     // (in this order: two statements, not two arguments)
        const float b = down4.down (y[2], y[3]);
        return down.down (a, b);
    }
}
