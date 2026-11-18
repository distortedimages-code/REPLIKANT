#include "Worker.h"
#include "../core/Transpose.h"
#include "../ui/Text.h"

Worker::Worker (Recorder& r) : juce::Thread ("REPLIK8 worker"), recorder (r)
{
    startThread (juce::Thread::Priority::normal);
}

Worker::~Worker()
{
    ++renderGeneration;
    signalThreadShouldExit();
    wake.signal();
    stopThread (20000);
}

Worker::Status Worker::getStatus() const
{
    std::lock_guard<std::mutex> l (statusLock);
    return status;
}

bool Worker::isSampling() const
{
    return sampling.load();
}

void Worker::setStatus (const std::function<void (Status&)>& f)
{
    {
        std::lock_guard<std::mutex> l (statusLock);
        f (status);
    }
    if (onStatus)
        onStatus();
}

juce::File Worker::uniqueFolder (const juce::File& root, const juce::String& name)
{
    const auto base = instrumentio::safeName (name);
    auto f = root.getChildFile (base);
    for (int n = 2; f.exists(); ++n)
        f = root.getChildFile (base + " " + juce::String (n));
    return f;
}

bool Worker::startSession (const juce::String& name, const rk::Session& s, const Recorder::Plan& plan, double sr,
                           int channels, const juce::File& folder)
{
    if (sampling.load() || recorder.isRunning())
        return false;
    ++renderGeneration;   // a render of the previous instrument is pointless now
    {
        std::lock_guard<std::mutex> l (taskLock);
        session = Session();
        session.active = true;
        session.name = name;
        session.settings = s;
        session.plan = plan;
        session.sampleRate = sr;
        session.folder = folder;
        session.startedAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }
    if (! recorder.start (plan, sr, channels))
        return false;
    sampling = true;
    setStatus ([&] (Status& st)
    {
        st = Status();
        st.sampling = true;
        st.busy = true;
        st.total = (int) plan.jobs.size();
        st.text = "MESURE DU BRUIT DE FOND";
        st.progress = 0.0f;
        st.secondsLeft = rk::estimatedSeconds (s);
    });
    wake.signal();
    return true;
}

bool Worker::startCycleSession (const juce::String& name, const rk::Session& s, const Recorder::Plan& plan, double sr,
                                int channels, const juce::File& folder, std::shared_ptr<const Instrument> base)
{
    if (! startSession (name, s, plan, sr, channels, folder))
        return false;
    std::lock_guard<std::mutex> l (taskLock);
    session.cycles = true;
    session.base = std::move (base);
    return true;
}

void Worker::stopSession()
{
    recorder.stop();
    wake.signal();
}

void Worker::post (const juce::String& label, std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> l (taskLock);
        tasks.emplace_back (label, std::move (task));
    }
    wake.signal();
}

void Worker::renderAndPublish (std::shared_ptr<Instrument> ins, bool publishFirst)
{
    const int generation = ++renderGeneration;   // the previous render, if any, stops at its next key
    post ("RENDU DES TOUCHES", [this, ins, publishFirst, generation]
    {
        auto cancelled = [this, generation] { return renderGeneration.load() != generation || threadShouldExit(); };
        if (cancelled())
            return;
        ins->rebuildIndex();
        ins->prepareForPlaying();
        if (publishFirst && onInstrument)
            onInstrument (ins);
        if (! ins->session.renderKeys || ins->countKeysToRender() == 0)
        {
            ins->keys.clear();
            for (auto& k : ins->keyOf)
                std::fill (k.begin(), k.end(), -1);
            if (! publishFirst && onInstrument)
                onInstrument (ins);
            return;
        }
        // a copy is rendered, so the one playing is never touched
        auto next = std::make_shared<Instrument> (*ins);
        next->keys.clear();
        for (auto& k : next->keyOf)
            std::fill (k.begin(), k.end(), -1);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        renderKeys (*next, cancelled, [this] (float p)
        {
            setStatus ([p] (Status& st) { st.progress = p; st.text = "RENDU HQ DES TOUCHES " + juce::String (juce::roundToInt (p * 100)) + " %"; });
        });
        if (cancelled())
            return;
        next->prepareForPlaying();
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
        if (onInstrument)
            onInstrument (next);
        setStatus ([&] (Status& st)
        {
            st.text = "PRÊT · "_fr + juce::String ((int) next->keys.size()) + " TOUCHES RENDUES EN " + juce::String (secs, 1) + " S";
            st.progress = -1.0f;
        });
    });
}

void Worker::renderKeys (Instrument& ins, const std::function<bool()>& cancelled, const std::function<void (float)>& progress)
{
    struct Item { int sample, key; };
    std::vector<Item> items;
    for (size_t i = 0; i < ins.samples.size(); ++i)
        for (int k = ins.samples[i].keyLo; k <= ins.samples[i].keyHi; ++k)
            if (std::abs (ins.shiftFor ((int) i, k)) > 0.005)
                items.push_back ({ (int) i, k });
    ins.rebuildIndex();
    ins.keys.assign (items.size(), {});
    rk::RenderSettings rs;
    rs.algo = ins.session.algo;
    rs.formantAmount = ins.session.formant;

    std::atomic<int> next { 0 }, done { 0 };
    const int threads = juce::jlimit (1, 12, juce::SystemStats::getNumCpus() - 1);
    {
        juce::ThreadPool pool (juce::ThreadPoolOptions{}.withThreadName ("REPLIK8 render").withNumberOfThreads (threads));
        for (int t = 0; t < threads; ++t)
            pool.addJob ([&]
            {
                for (;;)
                {
                    const int n = next++;
                    if (n >= (int) items.size() || cancelled())
                        return;
                    const auto& it = items[(size_t) n];
                    const auto& src = ins.samples[(size_t) it.sample];
                    auto out = rk::transposed (src, ins.shiftFor (it.sample, it.key), rs);
                    out.root = it.key;
                    out.keyLo = out.keyHi = it.key;
                    out.velLo = src.velLo;
                    out.velHi = src.velHi;
                    ins.keys[(size_t) n] = std::move (out);
                    ++done;
                }
            });
        while (pool.getNumJobs() > 0)
        {
            progress (items.empty() ? 1.0f : (float) done.load() / (float) items.size());
            juce::Thread::sleep (80);
        }
    }
    for (size_t n = 0; n < items.size(); ++n)
    {
        const auto& it = items[n];
        ins.keyOf[(size_t) it.sample][(size_t) (it.key - ins.samples[(size_t) it.sample].keyLo)] = (int) n;
    }
    progress (1.0f);
}

void Worker::run()
{
    while (! threadShouldExit())
    {
        wake.wait (25);
        if (threadShouldExit())
            break;
        if (sampling.load())
        {
            drain();
            continue;
        }
        std::pair<juce::String, std::function<void()>> task;
        {
            std::lock_guard<std::mutex> l (taskLock);
            if (tasks.empty())
                continue;
            task = std::move (tasks.front());
            tasks.pop_front();
        }
        // (a task without a label is a quiet one: a small save, nothing to show)
        if (task.first.isNotEmpty())
            setStatus ([&] (Status& st) { st.busy = true; st.text = task.first; st.progress = 0.0f; });
        task.second();
        bool more;
        {
            std::lock_guard<std::mutex> l (taskLock);
            more = ! tasks.empty();
        }
        setStatus ([more] (Status& st) { st.busy = more; if (! more) st.progress = -1.0f; });
        if (more)
            wake.signal();
    }
}

void Worker::drain()
{
    Recorder::Event ev[64];
    for (;;)
    {
        const int n = recorder.popEvents (ev, 64);
        if (n == 0)
            break;
        for (int i = 0; i < n; ++i)
        {
            const auto& e = ev[i];
            switch (e.type)
            {
                case Recorder::Event::noiseDone:
                {
                    rk::Channels noise;
                    if (recorder.copy (e.from, e.at, noise))
                        session.noise = rk::measureNoise (noise);
                    const float db = session.noise.rmsDb();
                    setStatus ([db] (Status& st)
                    {
                        st.floorDb = db;
                        st.text = "BRUIT DE FOND " + juce::String (db, 1) + " DB";
                        if (db > -50.0f)
                            st.warning = "Bruit de fond élevé ("_fr + juce::String (db, 1) + " dB) : vérifie le gain et que le synthé est bien silencieux."_fr;
                    });
                    break;
                }
                case Recorder::Event::noteOn:
                    session.noteOn = e.at;
                    if (e.job >= 0 && e.job < (int) session.plan.jobs.size())
                    {
                        const auto& j = session.plan.jobs[(size_t) e.job];
                        const auto done = (int) session.samples.size();
                        setStatus ([&] (Status& st)
                        {
                            st.text = juce::String (rk::noteName (j.note)) + " · VÉLOCITÉ "_fr + juce::String (j.velocity)
                                    + (session.settings.roundRobins > 1 ? " · PRISE "_fr + juce::String (j.rr + 1) : juce::String());
                            const double el = juce::Time::getMillisecondCounterHiRes() * 0.001 - session.startedAt;
                            if (done > 0)
                                st.secondsLeft = el / (double) (e.job) * (double) ((int) session.plan.jobs.size() - e.job);
                        });
                    }
                    break;
                case Recorder::Event::noteOff:
                    session.noteOff = e.at;
                    break;
                case Recorder::Event::jobDone:
                    processJob (e.job, session.noteOn, session.noteOff, e.at);
                    break;
                case Recorder::Event::finished:
                    finishSession (false);
                    return;
                case Recorder::Event::aborted:
                    finishSession (true);
                    return;
            }
        }
    }
}

void Worker::processJob (int jobIndex, int64_t noteOn, int64_t noteOff, int64_t end)
{
    if (jobIndex < 0 || jobIndex >= (int) session.plan.jobs.size() || noteOn < 0)
        return;
    const auto& job = session.plan.jobs[(size_t) jobIndex];
    const int64_t pre = (int64_t) (session.sampleRate * 0.05);
    const int64_t from = juce::jmax ((int64_t) 0, noteOn - pre);
    Raw raw;
    raw.job = job;
    if (! recorder.copy (from, end, raw.audio))
    {
        setStatus ([] (Status& st) { st.warning = "Un enregistrement a été perdu (mémoire tampon dépassée)."_fr; });
        return;
    }
    raw.noteOn = (int) (noteOn - from);
    raw.noteOff = (int) (juce::jmax (noteOn, noteOff) - from);

    auto r = rk::processCapture (raw.audio, session.sampleRate, raw.noteOn, raw.noteOff, job, session.settings, session.noise);
    if (r.silent)
    {
        ++session.silentInARow;
        const auto note = rk::noteName (job.note);
        setStatus ([&] (Status& st) { st.warning = "Rien reçu pour "_fr + juce::String (note) + " : vérifie la sortie MIDI, le canal et l'entrée audio."_fr; });
        if (session.silentInARow >= 3 && session.samples.empty())
        {
            setStatus ([] (Status& st) { st.warning = "Aucun son reçu sur les 3 premières notes : session arrêtée. Vérifie la sortie MIDI, le canal et l'entrée audio (bouton TEST)."_fr; });
            recorder.stop();
        }
        return;
    }
    session.silentInARow = 0;
    if (session.cycles)
    {
        // one cycle, where the take holds steady (past its attack, before its release)
        const auto mono = rk::mixDown (r.sample.audio);
        const double hz = 440.0 * std::pow (2.0, ((double) job.note - 69.0) / 12.0
                                                  + (r.sample.pitchConfident ? (double) (r.sample.root - job.note) / 12.0 + r.sample.detuneCents / 1200.0 : 0.0));
        const double period = session.sampleRate / hz;
        const int held = r.sample.noteOff > 0 ? r.sample.noteOff : (int) mono.size();
        const int at = juce::jmax (0, juce::jmin ((int) (held * 0.6), held - (int) (period * 6.0) - 8));
        auto cycle = dsp::extractCycle (mono.data(), (int) mono.size(), at, period);
        if (cycle.empty())      // a sub an octave down: the sound repeats every two periods
            cycle = dsp::extractCycle (mono.data(), (int) mono.size(), juce::jmax (0, at - (int) (period * 6.0)), period * 2.0);
        auto last = std::make_shared<rk::Sample> (std::move (r.sample));
        if (cycle.empty())
        {
            setStatus ([&] (Status& st) { st.warning = "Pas de cycle net sur la prise "_fr + juce::String (jobIndex + 1) + " : le son ne se répète pas (bruit, attaque trop longue ?)."_fr; });
            return;
        }
        session.cycleList.push_back (std::move (cycle));
        setStatus ([&] (Status& st)
        {
            st.recorded.push_back (job);
            st.done = jobIndex + 1;
            st.progress = (float) (jobIndex + 1) / (float) juce::jmax (1, st.total);
            st.last = last;
            st.text = "CYCLE "_fr + juce::String ((int) session.cycleList.size()) + " / " + juce::String (st.total);
        });
        return;
    }
    session.latencies.push_back (r.latency);
    if (session.settings.align == rk::Align::fixedLatency)
        session.raws.push_back (std::move (raw));
    session.samples.push_back (std::move (r.sample));
    auto last = std::make_shared<rk::Sample> (session.samples.back());
    const int lat = r.latency;
    const bool clipped = r.clipped;
    setStatus ([&] (Status& st)
    {
        st.recorded.push_back (job);
        st.done = jobIndex + 1;
        st.progress = (float) (jobIndex + 1) / (float) juce::jmax (1, st.total);
        st.last = last;
        st.latency = lat;
        if (clipped)
            st.warning = "Saturation sur " + juce::String (rk::noteName (job.note)) + " v" + juce::String (job.velocity) + " : baisse le niveau du synthé ou le gain d'entrée."_fr;
    });
}

void Worker::finishSession (bool aborted)
{
    sampling = false;
    if (session.cycles)
    {
        finishCycles (aborted);
        return;
    }
    if (session.samples.empty())
    {
        setStatus ([aborted] (Status& st) { st.sampling = false; st.busy = false; st.progress = -1.0f; st.text = aborted ? "SESSION ARRÊTÉE"_fr : "AUCUN SAMPLE"; });
        session.active = false;
        return;
    }
    setStatus ([] (Status& st) { st.sampling = false; st.text = "ASSEMBLAGE DE L'INSTRUMENT"; st.progress = 0.0f; });

    // one latency for all (the shortest seen: slow attacks only ever make it look longer)
    if (session.settings.align == rk::Align::fixedLatency && ! session.raws.empty())
    {
        const int lat = *std::min_element (session.latencies.begin(), session.latencies.end());
        session.samples.clear();
        for (auto& raw : session.raws)
        {
            auto r = rk::processCapture (raw.audio, session.sampleRate, raw.noteOn, raw.noteOff, raw.job, session.settings, session.noise, lat);
            if (! r.silent)
                session.samples.push_back (std::move (r.sample));
        }
        session.raws.clear();
    }

    rk::shareTuning (session.samples);
    rk::normalise (session.samples, session.settings.normalise, session.settings.normaliseDb);
    rk::assignZones (session.samples, session.settings);

    auto ins = std::make_shared<Instrument>();
    ins->name = session.name;
    ins->session = session.settings;
    ins->sampleRate = session.sampleRate;
    ins->noise = session.noise;
    ins->samples = std::move (session.samples);
    ins->rebuildIndex();
    session.active = false;

    const auto folder = session.folder;
    const auto res = instrumentio::save (*ins, folder);
    setStatus ([&] (Status& st)
    {
        st.text = (aborted ? "SESSION ARRÊTÉE · "_fr : "") + juce::String ((int) ins->samples.size()) + " SAMPLES · ENREGISTRÉ"_fr;
        if (res.failed())
            st.warning = res.getErrorMessage();
        st.busy = false;
        st.progress = -1.0f;
    });
    renderAndPublish (ins, true);
}

void Worker::finishCycles (bool aborted)
{
    auto list = std::move (session.cycleList);
    auto base = std::move (session.base);
    session.active = false;
    session.cycles = false;
    if (list.size() < 2)
    {
        setStatus ([aborted] (Status& st) { st.sampling = false; st.busy = false; st.progress = -1.0f; st.text = aborted ? "SESSION ARRÊTÉE"_fr : "PAS ASSEZ DE CYCLES"; });
        return;
    }
    // the bank (each cycle brought to the same peak: a table plays evenly), kept with the instrument
    auto ins = base != nullptr ? std::make_shared<Instrument> (*base) : std::make_shared<Instrument>();
    if (base == nullptr)
    {
        ins->name = session.name;
        ins->session = session.settings;
        ins->sampleRate = session.sampleRate;
        ins->rebuildIndex();
    }
    ins->cycles = std::make_shared<const dsp::CycleBank> (list, true);
    const auto folder = base != nullptr && base->folder.isDirectory() ? base->folder : session.folder;
    const auto res = instrumentio::save (*ins, folder);
    const int count = (int) list.size();
    setStatus ([&] (Status& st)
    {
        st.sampling = false;
        st.text = (aborted ? "SESSION ARRÊTÉE · "_fr : "") + juce::String (count) + " CYCLES · ENREGISTRÉS"_fr;
        if (res.failed())
            st.warning = res.getErrorMessage();
        st.busy = false;
        st.progress = -1.0f;
    });
    // the samples and their keys as they were: published as it is
    if (onInstrument)
        onInstrument (ins);
}
