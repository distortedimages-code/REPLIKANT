#include "Instrument.h"
#include "../core/Wav.h"
#include "../core/Transpose.h"
#include "../ui/Text.h"

#include <algorithm>
#include <map>
#include <set>

//==============================================================================
double Instrument::shiftFor (int i, int key) const
{
    const auto& s = samples[(size_t) i];
    double shift = key - s.root;
    if (session.tune && s.pitchConfident)
        shift -= s.detuneCents / 100.0;
    return shift;
}

std::vector<Instrument::Source> Instrument::sourcesFor (int key, int velocity) const
{
    std::vector<Source> out;
    if (key < 0 || key > 127)
        return out;
    const auto& cand = byKey[(size_t) key];
    std::vector<int> hits;
    for (int i : cand)
    {
        const auto& s = samples[(size_t) i];
        if (velocity >= s.velLo && velocity <= s.velHi)
            hits.push_back (i);
    }
    if (hits.empty() && ! cand.empty())
    {
        // no layer for that velocity (zones edited, or a hole): the nearest one
        int bestD = 1000;
        for (int i : cand)
        {
            const auto& s = samples[(size_t) i];
            const int d = velocity < s.velLo ? s.velLo - velocity : velocity - s.velHi;
            bestD = std::min (bestD, d);
        }
        for (int i : cand)
        {
            const auto& s = samples[(size_t) i];
            const int d = velocity < s.velLo ? s.velLo - velocity : velocity - s.velHi;
            if (d == bestD)
                hits.push_back (i);
        }
    }
    std::sort (hits.begin(), hits.end(), [this] (int a, int b) { return samples[(size_t) a].rr < samples[(size_t) b].rr; });
    for (int i : hits)
    {
        const auto& s = samples[(size_t) i];
        Source src;
        src.index = i;
        const int j = key - s.keyLo;
        if ((size_t) i < keyOf.size() && j >= 0 && j < (int) keyOf[(size_t) i].size() && keyOf[(size_t) i][(size_t) j] >= 0)
        {
            src.sample = &keys[(size_t) keyOf[(size_t) i][(size_t) j]];
            src.semitones = 0.0;
        }
        else
        {
            src.sample = &s;
            src.semitones = shiftFor (i, key);
        }
        out.push_back (src);
    }
    return out;
}

void Instrument::rebuildIndex()
{
    for (auto& k : byKey)
        k.clear();
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto& s = samples[i];
        for (int k = std::max (0, s.keyLo); k <= std::min (127, s.keyHi); ++k)
            byKey[(size_t) k].push_back ((int) i);
    }
    bool ok = keyOf.size() == samples.size();
    for (size_t i = 0; ok && i < samples.size(); ++i)
        ok = (int) keyOf[i].size() == samples[i].keyHi - samples[i].keyLo + 1;
    if (! ok)
    {
        keyOf.assign (samples.size(), {});
        for (size_t i = 0; i < samples.size(); ++i)
            keyOf[i].assign ((size_t) std::max (0, samples[i].keyHi - samples[i].keyLo + 1), -1);
    }
}

int64_t Instrument::memoryBytes() const
{
    int64_t b = 0;
    for (auto* list : { &samples, &keys })
        for (auto& s : *list)
            b += (int64_t) s.length() * s.channels() * (int64_t) sizeof (float);
    return b;
}

int Instrument::countKeysToRender() const
{
    int n = 0;
    for (size_t i = 0; i < samples.size(); ++i)
        for (int k = samples[i].keyLo; k <= samples[i].keyHi; ++k)
            if (std::abs (shiftFor ((int) i, k)) > 0.005)
                ++n;
    return n;
}

bool Instrument::hasLoops() const
{
    for (auto& s : samples)
        if (s.loop.valid())
            return true;
    return false;
}

//==============================================================================
namespace instrumentio
{
namespace
{
    template <typename E>
    struct EnumNames
    {
        std::vector<std::pair<E, const char*>> items;
        juce::String name (E e) const
        {
            for (auto& [v, n] : items)
                if (v == e)
                    return n;
            return items.front().second;
        }
        E value (const juce::var& v, E fallback) const
        {
            const auto s = v.toString();
            for (auto& [e, n] : items)
                if (s == n)
                    return e;
            return fallback;
        }
    };

    const EnumNames<rk::NoteMode> noteModes { { { rk::NoteMode::interval, "interval" }, { rk::NoteMode::count, "count" }, { rk::NoteMode::custom, "custom" } } };
    const EnumNames<rk::VelCurve> curves { { { rk::VelCurve::linear, "linear" }, { rk::VelCurve::soft, "soft" }, { rk::VelCurve::hard, "hard" }, { rk::VelCurve::custom, "custom" } } };
    const EnumNames<rk::Placement> placements { { { rk::Placement::centred, "centred" }, { rk::Placement::up, "up" }, { rk::Placement::down, "down" } } };
    const EnumNames<rk::Align> aligns { { { rk::Align::perNote, "perNote" }, { rk::Align::fixedLatency, "fixedLatency" } } };
    const EnumNames<rk::Normalise> normalises { { { rk::Normalise::instrument, "instrument" }, { rk::Normalise::perSample, "perSample" }, { rk::Normalise::off, "off" } } };
    const EnumNames<rk::Algo> algos { { { rk::Algo::formant, "formant" }, { rk::Algo::classic, "classic" }, { rk::Algo::fixedTime, "fixedTime" } } };

    juce::var intList (const std::vector<int>& v)
    {
        juce::Array<juce::var> a;
        for (int x : v)
            a.add (x);
        return a;
    }

    std::vector<int> intsOf (const juce::var& v)
    {
        std::vector<int> out;
        if (auto* a = v.getArray())
            for (auto& x : *a)
                out.push_back ((int) x);
        return out;
    }

    template <typename T>
    T get (const juce::var& o, const char* key, T fallback)
    {
        const auto v = o.getProperty (key, juce::var());
        return v.isVoid() ? fallback : (T) v;
    }

    // a sample as a WAV's chunks: it announces its detune (when it was measured) so other samplers play it in tune
    rk::WavInfo infoFor (const Instrument& ins, const rk::Sample& s, int keyLo, int keyHi, int root, bool rendered)
    {
        rk::WavInfo w;
        w.root = root;
        w.detuneCents = (! rendered && ins.session.tune && s.pitchConfident) ? s.detuneCents : 0.0f;
        w.loop = s.loop;
        w.hasRange = true;
        w.keyLo = keyLo;
        w.keyHi = keyHi;
        w.velLo = s.velLo;
        w.velHi = s.velHi;
        return w;
    }

    struct ExportItem
    {
        rk::Sample sample;       // what is written
        int root, keyLo, keyHi;
        bool rendered;
        juce::String file;
    };

    // the list of files an export writes: the recordings, or one per key
    std::vector<ExportItem> exportItems (const Instrument& ins, bool allKeys)
    {
        std::vector<ExportItem> items;
        const auto base = safeName (ins.name);
        for (size_t i = 0; i < ins.samples.size(); ++i)
        {
            const auto& s = ins.samples[i];
            if (! allKeys)
            {
                items.push_back ({ s, s.root, s.keyLo, s.keyHi, false, sampleFileName (ins.name, s) });
                continue;
            }
            for (int k = s.keyLo; k <= s.keyHi; ++k)
            {
                ExportItem it;
                it.root = k;
                it.keyLo = it.keyHi = k;
                const int j = k - s.keyLo;
                const double shift = ins.shiftFor ((int) i, k);
                if (i < ins.keyOf.size() && j < (int) ins.keyOf[i].size() && ins.keyOf[i][(size_t) j] >= 0)
                {
                    it.sample = ins.keys[(size_t) ins.keyOf[i][(size_t) j]];
                    it.rendered = true;
                }
                else if (std::abs (shift) > 0.005)
                {
                    rk::RenderSettings rs;
                    rs.algo = ins.session.algo;
                    rs.formantAmount = ins.session.formant;
                    it.sample = rk::transposed (s, shift, rs);
                    it.rendered = true;
                }
                else
                {
                    it.sample = s;
                    it.rendered = false;
                }
                it.sample.velLo = s.velLo;
                it.sample.velHi = s.velHi;
                it.file = base + " " + juce::String (rk::noteName (k)) + " v" + juce::String (s.velocity)
                        + (ins.session.roundRobins > 1 ? " rr" + juce::String (s.rr + 1) : juce::String()) + ".wav";
                items.push_back (std::move (it));
            }
        }
        return items;
    }

    juce::Result writeItem (const Instrument& ins, const ExportItem& it, const juce::File& f)
    {
        const auto info = infoFor (ins, it.sample, it.keyLo, it.keyHi, it.root, it.rendered);
        if (! rk::writeWav (f.getFullPathName().toStdString(), it.sample.audio, it.sample.sampleRate, 24, info))
            return juce::Result::fail ("Impossible d'écrire "_fr + f.getFullPathName());
        return juce::Result::ok();
    }
}

juce::File defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("REPLIKANT");
}

juce::String safeName (const juce::String& n)
{
    auto s = juce::File::createLegalFileName (n.trim()).replaceCharacters ("/\\:", "---");
    return s.isEmpty() ? juce::String ("Instrument") : s;
}

juce::String sampleFileName (const juce::String& instrument, const rk::Sample& s)
{
    return safeName (instrument) + " " + juce::String (rk::noteName (s.root)) + " v" + juce::String (s.velocity)
         + " rr" + juce::String (s.rr + 1) + ".wav";
}

juce::var sessionToVar (const rk::Session& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("lowNote", s.lowNote);
    o->setProperty ("highNote", s.highNote);
    o->setProperty ("noteMode", noteModes.name (s.noteMode));
    o->setProperty ("interval", s.interval);
    o->setProperty ("noteCount", s.noteCount);
    o->setProperty ("customNotes", intList (s.customNotes));
    o->setProperty ("layers", s.layers);
    o->setProperty ("curve", curves.name (s.curve));
    o->setProperty ("customVelocities", intList (s.customVelocities));
    o->setProperty ("roundRobins", s.roundRobins);
    o->setProperty ("hold", s.hold);
    o->setProperty ("releaseMax", s.releaseMax);
    o->setProperty ("pause", s.pause);
    o->setProperty ("oneShot", s.oneShot);
    o->setProperty ("loop", s.loop);
    o->setProperty ("loopStart", s.loopSettings.searchStart);
    o->setProperty ("loopEnd", s.loopSettings.searchEnd);
    o->setProperty ("loopMin", s.loopSettings.minSeconds);
    o->setProperty ("crossfade", s.loopSettings.crossfadeSeconds);
    o->setProperty ("align", aligns.name (s.align));
    o->setProperty ("preMs", s.preMs);
    o->setProperty ("thresholdDb", s.thresholdDb);
    o->setProperty ("normalise", normalises.name (s.normalise));
    o->setProperty ("normaliseDb", s.normaliseDb);
    o->setProperty ("tune", s.tune);
    o->setProperty ("removeDc", s.removeDc);
    o->setProperty ("placement", placements.name (s.placement));
    o->setProperty ("extend", s.extend);
    o->setProperty ("algo", algos.name (s.algo));
    o->setProperty ("formant", s.formant);
    o->setProperty ("renderKeys", s.renderKeys);
    return juce::var (o);
}

rk::Session sessionFromVar (const juce::var& v)
{
    rk::Session s;
    if (! v.isObject())
        return s;
    s.lowNote = juce::jlimit (0, 127, get (v, "lowNote", s.lowNote));
    s.highNote = juce::jlimit (0, 127, get (v, "highNote", s.highNote));
    s.noteMode = noteModes.value (v["noteMode"], s.noteMode);
    s.interval = juce::jlimit (1, 48, get (v, "interval", s.interval));
    s.noteCount = juce::jlimit (1, 128, get (v, "noteCount", s.noteCount));
    s.customNotes = intsOf (v["customNotes"]);
    s.layers = juce::jlimit (1, 16, get (v, "layers", s.layers));
    s.curve = curves.value (v["curve"], s.curve);
    s.customVelocities = intsOf (v["customVelocities"]);
    s.roundRobins = juce::jlimit (1, 8, get (v, "roundRobins", s.roundRobins));
    s.hold = juce::jlimit (0.02, 60.0, get (v, "hold", s.hold));
    s.releaseMax = juce::jlimit (0.0, 60.0, get (v, "releaseMax", s.releaseMax));
    s.pause = juce::jlimit (0.0, 10.0, get (v, "pause", s.pause));
    s.oneShot = get (v, "oneShot", s.oneShot);
    s.loop = get (v, "loop", s.loop);
    s.loopSettings.searchStart = juce::jlimit (0.0, 0.9, get (v, "loopStart", s.loopSettings.searchStart));
    s.loopSettings.searchEnd = juce::jlimit (s.loopSettings.searchStart + 0.05, 1.0, get (v, "loopEnd", s.loopSettings.searchEnd));
    s.loopSettings.minSeconds = juce::jlimit (0.005, 30.0, get (v, "loopMin", s.loopSettings.minSeconds));
    s.loopSettings.crossfadeSeconds = juce::jlimit (0.0, 2.0, get (v, "crossfade", s.loopSettings.crossfadeSeconds));
    s.align = aligns.value (v["align"], s.align);
    s.preMs = juce::jlimit (0.0, 50.0, get (v, "preMs", s.preMs));
    s.thresholdDb = juce::jlimit (3.0, 40.0, get (v, "thresholdDb", s.thresholdDb));
    s.normalise = normalises.value (v["normalise"], s.normalise);
    s.normaliseDb = juce::jlimit (-24.0, 0.0, get (v, "normaliseDb", s.normaliseDb));
    s.tune = get (v, "tune", s.tune);
    s.removeDc = get (v, "removeDc", s.removeDc);
    s.placement = placements.value (v["placement"], s.placement);
    s.extend = juce::jlimit (0, 127, get (v, "extend", s.extend));
    s.algo = algos.value (v["algo"], s.algo);
    s.formant = juce::jlimit (0.0, 1.0, get (v, "formant", s.formant));
    s.renderKeys = get (v, "renderKeys", s.renderKeys);
    return s;
}

juce::Result save (Instrument& ins, const juce::File& folder)
{
    auto samplesDir = folder.getChildFile ("Samples");
    if (! samplesDir.createDirectory())
        return juce::Result::fail ("Impossible de créer "_fr + samplesDir.getFullPathName());

    juce::Array<juce::var> list;
    std::set<juce::String> written;
    for (auto& s : ins.samples)
    {
        s.file = sampleFileName (ins.name, s).toStdString();
        const auto f = samplesDir.getChildFile (juce::String (s.file));
        const auto info = infoFor (ins, s, s.keyLo, s.keyHi, s.root, false);
        if (! rk::writeWav (f.getFullPathName().toStdString(), s.audio, s.sampleRate, 24, info))
            return juce::Result::fail ("Impossible d'écrire "_fr + f.getFullPathName());
        written.insert (f.getFileName());

        auto* o = new juce::DynamicObject();
        o->setProperty ("file", "Samples/" + juce::String (s.file));
        o->setProperty ("root", s.root);
        o->setProperty ("velocity", s.velocity);
        o->setProperty ("rr", s.rr);
        o->setProperty ("keyLo", s.keyLo);
        o->setProperty ("keyHi", s.keyHi);
        o->setProperty ("velLo", s.velLo);
        o->setProperty ("velHi", s.velHi);
        o->setProperty ("noteOff", s.noteOff);
        o->setProperty ("loopStart", s.loop.start);
        o->setProperty ("loopEnd", s.loop.end);
        o->setProperty ("crossfade", s.loop.crossfade);
        o->setProperty ("loopQuality", s.loop.quality);
        o->setProperty ("detune", s.detuneCents);
        o->setProperty ("pitchConfident", s.pitchConfident);
        list.add (juce::var (o));
    }
    // recordings of an earlier version of this instrument that aren't part of it any more
    for (auto& f : samplesDir.findChildFiles (juce::File::findFiles, false, "*.wav"))
        if (written.count (f.getFileName()) == 0)
            f.deleteFile();

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "REPLIKANT instrument");
    root->setProperty ("version", 1);
    root->setProperty ("name", ins.name);
    root->setProperty ("sampleRate", ins.sampleRate);
    root->setProperty ("noiseDb", ins.noise.rmsDb());
    root->setProperty ("session", sessionToVar (ins.session));
    root->setProperty ("samples", list);
    const auto desc = folder.getChildFile (safeName (ins.name) + ".replikant");
    if (! desc.replaceWithText (juce::JSON::toString (juce::var (root))))
        return juce::Result::fail ("Impossible d'écrire "_fr + desc.getFullPathName());
    ins.folder = folder;
    return juce::Result::ok();
}

std::shared_ptr<Instrument> load (const juce::File& where, juce::String& error)
{
    juce::File desc = where;
    if (where.isDirectory())
    {
        auto found = where.findChildFiles (juce::File::findFiles, false, "*.replikant");
        if (found.isEmpty())
        {
            error = "Pas d'instrument REPLIKANT dans " + where.getFullPathName();
            return nullptr;
        }
        desc = found.getFirst();
    }
    const auto json = juce::JSON::parse (desc.loadFileAsString());
    if (! json.isObject() || ! json["samples"].isArray())
    {
        error = "Fichier illisible : " + desc.getFullPathName();
        return nullptr;
    }
    auto ins = std::make_shared<Instrument>();
    ins->name = json["name"].toString();
    if (ins->name.isEmpty())
        ins->name = desc.getFileNameWithoutExtension();
    ins->session = sessionFromVar (json["session"]);
    ins->folder = desc.getParentDirectory();
    ins->noise.rms = rk::dbToGain ((float) (double) json.getProperty ("noiseDb", -96.0));
    for (auto& v : *json["samples"].getArray())
    {
        rk::Sample s;
        const auto f = ins->folder.getChildFile (v["file"].toString());
        rk::WavInfo info;
        if (! rk::readWav (f.getFullPathName().toStdString(), s.audio, s.sampleRate, info) || s.audio.empty())
        {
            error = "Sample manquant ou illisible : " + f.getFullPathName();
            return nullptr;
        }
        s.file = f.getFileName().toStdString();
        s.root = juce::jlimit (0, 127, get (v, "root", info.root >= 0 ? info.root : 60));
        s.velocity = juce::jlimit (1, 127, get (v, "velocity", 127));
        s.rr = juce::jmax (0, get (v, "rr", 0));
        s.keyLo = juce::jlimit (0, 127, get (v, "keyLo", s.root));
        s.keyHi = juce::jlimit (s.keyLo, 127, get (v, "keyHi", s.root));
        s.velLo = juce::jlimit (1, 127, get (v, "velLo", 1));
        s.velHi = juce::jlimit (s.velLo, 127, get (v, "velHi", 127));
        s.noteOff = get (v, "noteOff", -1);
        s.loop.start = get (v, "loopStart", info.loop.start);
        s.loop.end = get (v, "loopEnd", info.loop.end);
        if (s.loop.end > s.length())
            s.loop = {};
        s.loop.crossfade = get (v, "crossfade", 0);
        s.loop.quality = (float) get (v, "loopQuality", 0.0);
        s.detuneCents = (float) get (v, "detune", 0.0);
        s.pitchConfident = get (v, "pitchConfident", false);
        s.peak = rk::peakOf (s.audio);
        ins->sampleRate = s.sampleRate;
        ins->samples.push_back (std::move (s));
    }
    ins->rebuildIndex();
    return ins;
}

juce::Result exportWavFolder (const Instrument& ins, const juce::File& folder, bool allKeys)
{
    if (! folder.createDirectory())
        return juce::Result::fail ("Impossible de créer "_fr + folder.getFullPathName());
    for (auto& it : exportItems (ins, allKeys))
    {
        auto r = writeItem (ins, it, folder.getChildFile (it.file));
        if (r.failed())
            return r;
    }
    return juce::Result::ok();
}

juce::Result exportBitwig (const Instrument& ins, const juce::File& file, bool allKeys)
{
    const auto items = exportItems (ins, allKeys);
    juce::TemporaryFile tempDir;
    const auto dir = tempDir.getFile();
    if (! dir.createDirectory())
        return juce::Result::fail ("Dossier temporaire impossible");

    juce::XmlElement xml ("multisample");
    xml.setAttribute ("name", ins.name);
    xml.createNewChildElement ("generator")->addTextElement ("REPLIKANT");
    xml.createNewChildElement ("category")->addTextElement ("Synth");
    xml.createNewChildElement ("creator")->addTextElement ("REPLIKANT");
    xml.createNewChildElement ("description")->addTextElement ("Autosampled with REPLIKANT");
    xml.createNewChildElement ("keywords");
    const bool roundRobin = ins.session.roundRobins > 1;

    juce::ZipFile::Builder zip;
    for (auto& it : items)
    {
        const auto f = dir.getChildFile (it.file);
        auto r = writeItem (ins, it, f);
        if (r.failed())
            return r;
        zip.addFile (f, 0, it.file);   // stored: Bitwig streams the samples straight from the archive

        auto* e = xml.createNewChildElement ("sample");
        e->setAttribute ("file", it.file);
        e->setAttribute ("gain", "0.000");
        e->setAttribute ("sample-start", "0.000");
        e->setAttribute ("sample-stop", juce::String (it.sample.length()) + ".000");
        e->setAttribute ("zone-logic", roundRobin ? "round-robin" : "always-play");
        auto* key = e->createNewChildElement ("key");
        key->setAttribute ("root", it.root);
        key->setAttribute ("track", "1.0000");
        const double tune = (! it.rendered && ins.session.tune && it.sample.pitchConfident) ? -it.sample.detuneCents / 100.0 : 0.0;
        key->setAttribute ("tune", juce::String (tune, 3));
        key->setAttribute ("low", it.keyLo);
        key->setAttribute ("high", it.keyHi);
        key->setAttribute ("low-fade", 0);
        key->setAttribute ("high-fade", 0);
        auto* vel = e->createNewChildElement ("velocity");
        vel->setAttribute ("low", it.sample.velLo);
        vel->setAttribute ("high", it.sample.velHi);
        vel->setAttribute ("low-fade", 0);
        vel->setAttribute ("high-fade", 0);
        auto* sel = e->createNewChildElement ("select");
        sel->setAttribute ("low", 0);
        sel->setAttribute ("high", 127);
        sel->setAttribute ("low-fade", 0);
        sel->setAttribute ("high-fade", 0);
        if (it.sample.loop.valid())
        {
            auto* l = e->createNewChildElement ("loop");
            l->setAttribute ("mode", "loop");
            l->setAttribute ("start", juce::String (it.sample.loop.start) + ".000");
            l->setAttribute ("stop", juce::String (it.sample.loop.end) + ".000");
            l->setAttribute ("fade", "0.00");   // the crossfade is already in the audio
        }
    }
    const auto xmlFile = dir.getChildFile ("multisample.xml");
    if (! xml.writeTo (xmlFile))
        return juce::Result::fail ("multisample.xml impossible");
    zip.addFile (xmlFile, 0, "multisample.xml");

    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return juce::Result::fail ("Impossible d'écrire "_fr + file.getFullPathName());
    if (! zip.writeToStream (out, nullptr))
        return juce::Result::fail ("Archive impossible");
    out.flush();
    dir.deleteRecursively();
    return juce::Result::ok();
}
}
