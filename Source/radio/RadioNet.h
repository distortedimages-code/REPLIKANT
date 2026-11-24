// REPLIK8 — RADIO (0.3.5): the receivers on the network (after IMPAKT's, v87). Each of the four has its own thread:
// tuned to an internet station (WEB: an MP3 stream over HTTP or HTTPS, its titles read as they come) or to a shortwave
// receiver on the network (SW: its WebSocket, AM / SSB / CW at a frequency), it fetches the sound, decodes it
// (radio::Dec) and hands it to the bank (push), as long as the RADIO tab listens to that receiver. The lists the tuner
// browses (the stations' directory, the receivers' public list) are fetched on a worker thread, their answers handed
// to the window.
//
// Manners: one connection a receiver, never two on the same shortwave receiver; let go 45 s after no one listens to it;
// a receiver that says it is busy, refuses or ends the connection is not asked again until the page tunes it again;
// a network error is retried three times (3, 10, 30 s), then left; the frequency changed at most five times a second;
// the client's name sent to the receivers (REPLIK8); each station tuned counted in the directory (as it asks).
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <deque>
#include <memory>
#include "RadioCore.h"

class RadioNet
{
public:
    // what a receiver is tuned to
    struct Source
    {
        int kind = 0;                      // 0 nothing, 1 WEB, 2 SW
        bool on = true;
        juce::String name, url, uuid, country, codec;   // WEB
        int kbps = 0;
        juce::String host, rxName;         // SW: the receiver (host, port, its own name)
        int port = 8073;
        double khz = 9420.0;               // SW: the frequency, kHz, and the mode (am, usb, lsb, cw)
        juce::String mode { "am" };
        juce::var toVar() const;
        static Source fromVar (const juce::var& v);
        bool sameStream (const Source& o) const;   // (the same connection: for SW the same receiver, whatever its frequency)
    };
    // what the page shows of a receiver
    struct Status
    {
        int state = radio::none;
        juce::String msg, title;           // why it failed / what it says; the station's title now
        int rate = 0, kbps = 0, users = -1, usersMax = -1;
        juce::String seekMsg;              // (0.4.18) what a SEEK is doing, or what it found (empty: no seek)
    };

    explicit RadioNet (radio::Bank& b);
    ~RadioNet();

    // the message thread (the tuner, a project)
    void   setSource (int rx, const Source& s);
    Source getSource (int rx) const;
    Status getStatus (int rx) const;
    int    version() const noexcept { return ver.load(); }        // (moves when a source or a status changes)
    // the tuner's questions (their answers in takeAnswers): the stations' directory, the receivers' list
    void   search (int id, const juce::var& q);
    void   receivers (int id, bool fresh);
    std::vector<juce::var> takeAnswers();
    // (0.4.18) SEEK: the next clean signal up (+1) or down (-1) the band from a shortwave receiver's frequency, found
    // on the receiver's own panorama (its waterfall, read beside the sound, then let go); the frequency set there, the
    // seek ends. 0 stops it; so does a new frequency or another receiver tuned meanwhile
    void   seek (int rx, int dir);
    int    seeking (int rx) const;         // the way it goes (0: not seeking)
    // the project: the four receivers' sources
    juce::var toVar() const;
    void   fromVar (const juce::var&);
    void   clearAll();

    // (for the tests: a directory or a receivers' list elsewhere, a shorter standby)
    juce::String dirOverride, listOverride;
    int standbyMs = 45000;

private:
    struct Rx;
    struct Watch;
    struct Seek;
    radio::Bank& bank;
    std::unique_ptr<Rx> rx[radio::numRx];
    std::unique_ptr<Watch> sup;            // (ends the sessions that should end: a blocking HTTPS read among them)
    std::unique_ptr<Seek> seeker;          // (0.4.18) reads a receiver's panorama for SEEK, one seek at a time
    std::atomic<int> ver { 0 };
    std::unique_ptr<juce::ThreadPool> pool;
    juce::CriticalSection ansLock;
    std::vector<juce::var> answers;
    juce::CriticalSection dirLock;
    juce::String dirHost;                  // the directory's server (picked once)
    juce::var    rxList; juce::int64 rxListAt = 0;
    void   answer (const juce::var& v);
    juce::String directoryHost();
    void   countClick (const juce::String& uuid);
    friend struct Rx;
    friend struct Watch;
    friend struct Seek;
    JUCE_DECLARE_NON_COPYABLE (RadioNet)
};
