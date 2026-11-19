// RADIO (0.3.5) on the network (radio/RadioNet), against local stand-ins (Tests/radio/radio_mock.py PORT MP3, after
// IMPAKT's): a station (through a redirection, its titles, its format, a playlist, an AAC one refused), the
// directory (a search filtered, a click counted), the receivers' public list, a shortwave receiver (its S-meter, tuned
// again over its connection), never two connections on one receiver, one with no channel free (not asked again), the
// standby when no one listens, the sources kept with the project. The audio runs in real time, as a host would.

#include <JuceHeader.h>
#include "../Source/radio/RadioNet.h"

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef RK_TEST_DIR
 #define RK_TEST_DIR "."
#endif

static int fails = 0;
static void check (const char* name, bool ok, const juce::String& info = {})
{
    std::printf ("%s %s %s\n", ok ? "PASS" : "FAIL", name, info.toRawUTF8());
    std::fflush (stdout);
    if (! ok)
        ++fails;
}

static float peakHz (const std::vector<float>& x, size_t from, int n, float sr, float lo, float hi)
{
    float best = 0, bf = 0;
    for (float f = lo; f <= hi; f += 5)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / (n - 1));
            re += x[from + (size_t) i] * w * std::cos (2 * juce::MathConstants<double>::pi * f * i / sr);
            im += x[from + (size_t) i] * w * std::sin (2 * juce::MathConstants<double>::pi * f * i / sr);
        }
        if (re * re + im * im > best) { best = (float) (re * re + im * im); bf = f; }
    }
    return bf;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const int port = 18400 + juce::Random::getSystemRandom().nextInt (2000);
    const juce::String base = "http://127.0.0.1:" + juce::String (port);
    const juce::File dir (RK_TEST_DIR);

    // the stand-ins
    juce::ChildProcess mock;
    if (! mock.start (juce::StringArray { "python3", dir.getChildFile ("radio_mock.py").getFullPathName(), juce::String (port),
                                          dir.getChildFile ("radio_1k.mp3").getFullPathName() }, 0))
    {
        std::printf ("SKIP no python3: the network's stand-ins can't run here\n");
        return 0;
    }
    juce::Thread::sleep (1200);
    if (! mock.isRunning())
    {
        std::printf ("SKIP the stand-ins didn't start\n");
        return 0;
    }

    auto engine = std::make_unique<radio::Engine>();
    radio::Engine& e = *engine;
    e.prepare (48000.0, 256);
    e.allocate();
    radio::Settings set;
    set.stat = 0.0f;
    set.level = 0.8f;
    int listening = 0;                  // the receiver listened to (-1: none)
    auto net = std::make_unique<RadioNet> (e.bank);
    net->dirOverride = base;
    net->listOverride = base + "/public/";
    net->standbyMs = 3000;

    // the audio, in real time
    std::vector<float> L, R, bl (256), br (256);
    double done = 0;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto run = [&] (double secs, bool keep)
    {
        const double until = juce::Time::getMillisecondCounterHiRes() + secs * 1000.0;
        while (juce::Time::getMillisecondCounterHiRes() < until)
        {
            const double due = (juce::Time::getMillisecondCounterHiRes() - t0) * 48.0;
            while (done + 256 <= due)
            {
                e.bank.use.store (listening >= 0 ? 1 << listening : 0);
                set.rx = juce::jmax (0, listening);
                e.render (bl.data(), br.data(), 256, set, nullptr, 0);
                if (keep)
                {
                    L.insert (L.end(), bl.begin(), bl.end());
                    R.insert (R.end(), br.begin(), br.end());
                }
                done += 256;
            }
            juce::Thread::sleep (2);
        }
    };
    auto waitState = [&] (int rx, int want, double secs)
    {
        const double until = juce::Time::getMillisecondCounterHiRes() + secs * 1000.0;
        while (juce::Time::getMillisecondCounterHiRes() < until)
        {
            if (net->getStatus (rx).state == want)
                return true;
            run (0.05, false);
        }
        return false;
    };
    auto stats = [&]
    {
        juce::URL u (base + "/stats");
        auto s = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (3000));
        return s ? juce::JSON::parse (s->readEntireStreamAsString()) : juce::var();
    };

    // ---- a station
    RadioNet::Source s;
    s.kind = 1;
    s.url = base + "/redir";
    s.name = "Mock";
    s.uuid = "u-1";
    net->setSource (0, s);
    check ("WEB: the station plays (through a redirection)", waitState (0, radio::live, 8), net->getStatus (0).msg);
    L.clear();
    R.clear();
    run (2.5, true);
    {
        const float hz = L.size() > 30000 ? peakHz (L, L.size() - 16384, 16384, 48000, 500, 2000) : 0;
        double ss = 0;
        for (size_t i = L.size() - 16384; i < L.size(); ++i)
            ss += L[i] * L[i];
        check ("WEB: its sound heard (1 kHz, both sides)", std::abs (hz - 1000) <= 6 && std::sqrt (ss / 16384) > 0.05,
               juce::String (hz) + " Hz, rms " + juce::String (std::sqrt (ss / 16384), 3));
    }
    run (3.0, false);
    check ("WEB: its title read (ICY)", net->getStatus (0).title.startsWith ("Mock Song"), net->getStatus (0).title);
    check ("WEB: its format", net->getStatus (0).rate == 44100 && net->getStatus (0).kbps == 128,
           juce::String (net->getStatus (0).rate) + " / " + juce::String (net->getStatus (0).kbps));
    check ("WEB: about a second in hand (two at most), no dropout", e.bank.rx[0].fill.load() > 0.5f && e.bank.rx[0].fill.load() <= 2.0f && e.bank.rx[0].dropouts.load() == 0,
           juce::String (e.bank.rx[0].fill.load(), 2) + " s, " + juce::String (e.bank.rx[0].dropouts.load()) + " dropouts");
    {
        const auto st = stats();
        bool clicked = false;
        if (auto* a = st["clicks"].getArray())
            for (auto& c : *a)
                clicked |= c.toString() == "u-1";
        check ("WEB: the click counted in the directory", clicked);
    }
    // a playlist
    s.url = base + "/playlist.pls";
    s.uuid = {};
    net->setSource (0, s);
    run (0.3, false);
    check ("WEB: a playlist's stream plays", waitState (0, radio::live, 8), net->getStatus (0).msg);
    // AAC: refused, not asked again
    s.url = base + "/aac";
    net->setSource (0, s);
    check ("WEB: an AAC station refused", waitState (0, radio::failed, 6), net->getStatus (0).msg);
    check ("WEB: ... said so", net->getStatus (0).msg.contains ("AAC"), net->getStatus (0).msg);

    // ---- the directory, the receivers' list
    juce::var q (new juce::DynamicObject());
    q.getDynamicObject()->setProperty ("name", "mock");
    net->search (7, q);
    std::vector<juce::var> ans;
    for (int i = 0; i < 100 && ans.size() < 1; ++i)
    {
        run (0.05, false);
        for (auto& a : net->takeAnswers())
            ans.push_back (a);
    }
    check ("directory: answered", ans.size() == 1 && (int) ans[0]["id"] == 7 && (bool) ans[0]["ok"]);
    if (! ans.empty())
        check ("directory: HLS and AAC left out", ans[0]["res"].size() == 1 && ans[0]["res"][0]["name"].toString() == "Mock Jazz",
               juce::JSON::toString (ans[0]["res"], true));
    net->receivers (8, true);
    ans.clear();
    for (int i = 0; i < 100 && ans.size() < 1; ++i)
    {
        run (0.05, false);
        for (auto& a : net->takeAnswers())
            ans.push_back (a);
    }
    check ("receivers: answered", ans.size() == 1 && (bool) ans[0]["ok"]);
    if (! ans.empty())
    {
        const auto& r = ans[0]["res"];
        check ("receivers: the list parsed (chunked), the offline one left out, the freest first",
               r.size() == 3 && r[0]["name"].toString() == "Mock emptiest" && (int) r[0]["max"] == 8 && r[1]["name"].toString() == "Mock receiver & antenna"
                   && (int) r[1]["port"] == port + 1 && r[1]["loc"].toString() == "Paris" && std::abs ((double) r[1]["hi"] - 30000.0) < 1,
               juce::JSON::toString (r, true).substring (0, 400));
    }

    // (0.4.11) the map's form of the list: a JavaScript array
    net->listOverride = base + "/kiwisdr_com.js";
    net->receivers (9, true);
    ans.clear();
    for (int i = 0; i < 100 && ans.size() < 1; ++i)
    {
        run (0.05, false);
        for (auto& a : net->takeAnswers())
            ans.push_back (a);
    }
    check ("receivers (array): answered", ans.size() == 1 && (bool) ans[0]["ok"]);
    if (! ans.empty())
    {
        const auto& r = ans[0]["res"];
        check ("receivers (array): parsed, the offline one left out, the freest first",
               r.size() == 3 && r[0]["name"].toString().startsWith ("Mock emptiest") && (int) r[0]["max"] == 8 && r[1]["name"].toString() == "Mock receiver & antenna"
                   && (int) r[1]["port"] == port + 1 && r[1]["loc"].toString() == "Paris" && std::abs ((double) r[1]["hi"] - 30000.0) < 1,
               juce::JSON::toString (r, true).substring (0, 400));
    }
    net->listOverride = base + "/public/";

    // ---- a shortwave receiver (RX 2)
    RadioNet::Source w;
    w.kind = 2;
    w.host = "127.0.0.1";
    w.port = port + 1;
    w.khz = 9420.0;
    w.mode = "am";
    w.rxName = "Mock receiver";
    listening = 1;
    net->setSource (1, w);
    check ("SW: the receiver plays", waitState (1, radio::live, 8), net->getStatus (1).msg);
    L.clear();
    R.clear();
    run (2.0, true);
    check ("SW: its sound heard (500 Hz)", std::abs (peakHz (R, R.size() - 16384, 16384, 48000, 200, 1500) - 500) <= 6,
           juce::String (peakHz (R, R.size() - 16384, 16384, 48000, 200, 1500)));
    check ("SW: its S-meter", std::abs ((float) e.bank.rx[1].meterC.load() * 0.01f + 73.0f) < 0.5f, juce::String (e.bank.rx[1].meterC.load()));
    check ("SW: its rate", net->getStatus (1).rate == 12001, juce::String (net->getStatus (1).rate));
    w.khz = 11850.0;
    net->setSource (1, w);
    run (1.5, false);
    L.clear();
    R.clear();
    run (1.0, true);
    check ("SW: tuned again over its connection (900 Hz)", std::abs (peakHz (R, R.size() - 16384, 16384, 48000, 200, 1500) - 900) <= 6,
           juce::String (peakHz (R, R.size() - 16384, 16384, 48000, 200, 1500)));
    {
        const auto st = stats();
        check ("SW: one connection, two frequencies", (int) st["ws"] == 1 && st["mods"].size() == 2 && std::abs ((double) st["mods"][1] - 11850.0) < 0.01,
               juce::JSON::toString (st, true));
    }
    // the same receiver on RX 4: refused
    net->setSource (3, w);
    check ("SW: never two connections on one receiver", waitState (3, radio::busy, 3), net->getStatus (3).msg);
    // a receiver with no channel free (RX 3): busy, not asked again
    RadioNet::Source b = w;
    b.port = port + 2;
    net->setSource (2, b);
    check ("SW: a busy receiver", waitState (2, radio::busy, 6), net->getStatus (2).msg);
    run (5.0, false);
    check ("SW: ... not asked again", (int) stats()["busy"] == 1, juce::String ((int) stats()["busy"]));
    // standby: no one listening to RX 2 for 3 s: let go; back when it's listened to again
    listening = -1;
    check ("standby: let go when no one listens", waitState (1, radio::standby, 8), juce::String (net->getStatus (1).state));
    listening = 1;
    check ("standby: back when listened to", waitState (1, radio::live, 8), juce::String (net->getStatus (1).state));
    check ("SW: a second connection after the standby", (int) stats()["ws"] == 2, juce::String ((int) stats()["ws"]));
    // the project: the sources kept and read back
    {
        const auto v = net->toVar();
        RadioNet n2 (e.bank);
        n2.fromVar (juce::JSON::parse (juce::JSON::toString (v)));
        check ("project: the sources kept", n2.getSource (1).kind == 2 && n2.getSource (1).khz == 11850.0 && n2.getSource (0).url == base + "/aac"
                                                && n2.getSource (2).port == port + 2);
        n2.clearAll();
    }
    net.reset();
    mock.kill();
    std::printf ("%d failed\n", fails);
    return fails ? 1 : 0;
}
