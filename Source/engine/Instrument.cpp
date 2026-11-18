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
    double shift = key - rk::mappedRoot (s, session) - session.tuneCents / 100.0;
    if ((session.tuneOn() || s.manualTune) && s.pitchConfident)
        shift -= s.detuneCents / 100.0;
    return shift;
}

int Instrument::sourcesFor (int key, int velocity, Source* out, int maxOut, bool recordedOnly) const
{
    // (audio thread: nothing allocated)
    if (key < 0 || key > 127 || maxOut <= 0)
        return 0;
    const auto& cand = byKey[(size_t) key];
    int hits[maxSources];
    int n = 0;
    for (int i : cand)
    {
        const auto& s = samples[(size_t) i];
        if (velocity >= s.velLo && velocity <= s.velHi && n < maxSources)
            hits[n++] = i;
    }
    if (n == 0 && ! cand.empty())
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
            if (d == bestD && n < maxSources)
                hits[n++] = i;
        }
    }
    std::sort (hits, hits + n, [this] (int a, int b) { return samples[(size_t) a].rr < samples[(size_t) b].rr; });
    n = std::min (n, maxOut);
    for (int h = 0; h < n; ++h)
    {
        const int i = hits[h];
        const auto& s = samples[(size_t) i];
        Source src;
        src.index = i;
        const int j = key - s.keyLo;
        if (! recordedOnly && (size_t) i < keyOf.size() && j >= 0 && j < (int) keyOf[(size_t) i].size() && keyOf[(size_t) i][(size_t) j] >= 0)
        {
            src.sample = &keys[(size_t) keyOf[(size_t) i][(size_t) j]];
            src.semitones = 0.0;
        }
        else
        {
            src.sample = &s;
            src.semitones = shiftFor (i, key);
        }
        out[h] = src;
    }
    return n;
}

void Instrument::prepareForPlaying()
{
    bool same = octaves.size() == samples.size();
    for (size_t i = 0; same && i < samples.size(); ++i)
        same = ! octaves[i][0].empty() && (int) octaves[i][0][0].size() == (samples[i].length() + 1) / 2;
    if (! same)
    {
        octaves.assign (samples.size(), {});
        for (size_t i = 0; i < samples.size(); ++i)
        {
            octaves[i][0] = rk::halfRate (samples[i].audio);
            octaves[i][1] = rk::halfRate (octaves[i][0]);
            octaves[i][2] = rk::halfRate (octaves[i][1]);
        }
    }
    for (auto* list : { &samples, &keys })
        for (auto& s : *list)
            if (s.attackEnd < 0)
                s.attackEnd = rk::attackEndOf (s.audio, s.sampleRate);
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
    for (auto& o : octaves)
        for (auto& level : o)
            for (auto& c : level)
                b += (int64_t) c.size() * (int64_t) sizeof (float);
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
    const EnumNames<rk::Tuning> tunings { { { rk::Tuning::snap, "snap" }, { rk::Tuning::fine, "fine" }, { rk::Tuning::off, "off" } } };
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
        w.detuneCents = rendered ? 0.0f : ((ins.session.tuneOn() || s.manualTune) && s.pitchConfident ? s.detuneCents : 0.0f)
                                           + (float) ins.session.tuneCents;
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
                items.push_back ({ s, rk::mappedRoot (s, ins.session), s.keyLo, s.keyHi, false, sampleFileName (ins.name, s) });
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
        if (! rk::writeWav (f.getFullPathName().toStdString(), it.sample.audio, it.sample.sampleRate, ins.session.fileBits, info))
            return juce::Result::fail ("Impossible d'écrire "_fr + f.getFullPathName());
        return juce::Result::ok();
    }
}

juce::File defaultRoot()
{
    // ~/Music/REPLIK8; the folder of before (REPLIKANT, until 0.3.0) renamed the first time, if it can be
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto now = music.getChildFile ("REPLIK8"), before = music.getChildFile ("REPLIKANT");
    if (! now.exists() && before.isDirectory() && ! before.moveFileTo (now))
        return before;
    return now;
}

juce::File movedFolder (const juce::File& f)
{
    // a path saved in a project before the instruments' folder was renamed
    if (f.isDirectory())
        return f;
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto before = music.getChildFile ("REPLIKANT");
    if (f.isAChildOf (before))
    {
        const auto moved = defaultRoot().getChildFile (f.getRelativePathFrom (before));
        if (moved.isDirectory())
            return moved;
    }
    return f;
}

juce::String safeName (const juce::String& n)
{
    auto s = juce::File::createLegalFileName (n.trim()).replaceCharacters ("/\\:", "---");
    return s.isEmpty() ? juce::String ("Instrument") : s;
}

juce::String sampleFileName (const juce::String& instrument, const rk::Sample& s)
{
    return safeName (instrument) + " " + juce::String (rk::noteName (s.sent >= 0 ? s.sent : s.root)) + " v" + juce::String (s.velocity)
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
    o->setProperty ("tuning", tunings.name (s.tuning));
    o->setProperty ("tuneCents", s.tuneCents);
    o->setProperty ("removeDc", s.removeDc);
    o->setProperty ("fileBits", s.fileBits);
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
    if (v.hasProperty ("tuning"))
        s.tuning = tunings.value (v["tuning"], s.tuning);
    else if (v.hasProperty ("tune"))   // (0.1: on or off; on was the cents only)
        s.tuning = (bool) v["tune"] ? rk::Tuning::fine : rk::Tuning::off;
    s.tuneCents = juce::jlimit (-1200.0, 1200.0, get (v, "tuneCents", 0.0));
    s.removeDc = get (v, "removeDc", s.removeDc);
    s.fileBits = (int) get (v, "fileBits", (double) s.fileBits) == 32 ? 32 : 24;
    s.placement = placements.value (v["placement"], s.placement);
    s.extend = juce::jlimit (0, 127, get (v, "extend", s.extend));
    s.algo = algos.value (v["algo"], s.algo);
    s.formant = juce::jlimit (0.0, 1.0, get (v, "formant", s.formant));
    s.renderKeys = get (v, "renderKeys", s.renderKeys);
    return s;
}

namespace
{
    // writes an instrument's files into `folder` (nothing of the instrument changes)
    juce::Result writeInstrument (const Instrument& ins, const juce::File& folder, const juce::var& sound)
    {
        auto samplesDir = folder.getChildFile ("Samples");
        if (! samplesDir.createDirectory())
            return juce::Result::fail ("Impossible de créer "_fr + samplesDir.getFullPathName());

        juce::Array<juce::var> list;
        std::set<juce::String> written;
        for (auto& s : ins.samples)
        {
            const auto name = sampleFileName (ins.name, s);
            const auto f = samplesDir.getChildFile (name);
            const auto info = infoFor (ins, s, s.keyLo, s.keyHi, rk::mappedRoot (s, ins.session), false);
            if (! rk::writeWav (f.getFullPathName().toStdString(), s.audio, s.sampleRate, ins.session.fileBits, info))
                return juce::Result::fail ("Impossible d'écrire "_fr + f.getFullPathName());
            written.insert (f.getFileName());

            auto* o = new juce::DynamicObject();
            o->setProperty ("file", "Samples/" + name);
            o->setProperty ("root", s.root);
            o->setProperty ("sent", s.sent);
            o->setProperty ("manualTune", s.manualTune);
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

        // the cycles (CAPTURE CYCLES): one wavetable, 2048 points a cycle, 32-bit float (Serum, Vital, Bitwig read it)
        juce::var cyclesInfo;
        if (ins.cycles != nullptr && ins.cycles->count() > 0)
        {
            const auto dir = folder.getChildFile ("Cycles");
            const auto f = dir.getChildFile (safeName (ins.name) + " - cycles " + juce::String (dsp::CycleBank::size) + ".wav");
            dir.createDirectory();
            for (auto& old : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
                if (old != f)
                    old.deleteFile();
            rk::Channels table { ins.cycles->flatten() };
            if (! rk::writeWav (f.getFullPathName().toStdString(), table, 48000.0, 32, rk::WavInfo()))
                return juce::Result::fail ("Impossible d'écrire "_fr + f.getFullPathName());
            auto* c = new juce::DynamicObject();
            c->setProperty ("file", "Cycles/" + f.getFileName());
            c->setProperty ("size", dsp::CycleBank::size);
            c->setProperty ("count", ins.cycles->count());
            cyclesInfo = juce::var (c);
        }

        const auto desc = folder.getChildFile (safeName (ins.name) + ".replikant");
        // the sound already there stays, unless one is given
        auto keep = sound;
        if (keep.isVoid() && desc.existsAsFile())
            keep = juce::JSON::parse (desc.loadFileAsString())["sound"];

        auto* root = new juce::DynamicObject();
        root->setProperty ("format", "REPLIKANT instrument");
        root->setProperty ("version", 2);
        root->setProperty ("name", ins.name);
        root->setProperty ("sampleRate", ins.sampleRate);
        root->setProperty ("noiseDb", ins.noise.rmsDb());
        root->setProperty ("session", sessionToVar (ins.session));
        root->setProperty ("samples", list);
        if (! cyclesInfo.isVoid())
            root->setProperty ("cycles", cyclesInfo);
        if (! keep.isVoid())
            root->setProperty ("sound", keep);
        if (! desc.replaceWithText (juce::JSON::toString (juce::var (root))))
            return juce::Result::fail ("Impossible d'écrire "_fr + desc.getFullPathName());
        return juce::Result::ok();
    }
}

juce::Result save (Instrument& ins, const juce::File& folder)
{
    const auto r = writeInstrument (ins, folder, ins.sound);
    if (r.failed())
        return r;
    for (auto& s : ins.samples)
        s.file = sampleFileName (ins.name, s).toStdString();
    ins.folder = folder;
    return r;
}

juce::Result saveSound (const juce::File& folder, const juce::var& sound)
{
    auto found = folder.findChildFiles (juce::File::findFiles, false, "*.replikant");
    if (found.isEmpty())
        return juce::Result::fail ("Pas d'instrument dans "_fr + folder.getFullPathName());
    auto json = juce::JSON::parse (found.getFirst().loadFileAsString());
    auto* o = json.getDynamicObject();
    if (o == nullptr)
        return juce::Result::fail ("Fichier illisible : "_fr + found.getFirst().getFullPathName());
    o->setProperty ("sound", sound);
    if (! found.getFirst().replaceWithText (juce::JSON::toString (json)))
        return juce::Result::fail ("Impossible d'écrire "_fr + found.getFirst().getFullPathName());
    return juce::Result::ok();
}

juce::Result exportPack (const Instrument& ins, const juce::var& sound, const juce::File& zip)
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("replikant-pack-" + juce::Uuid().toString());
    const auto base = safeName (ins.name);
    const auto dir = temp.getChildFile (base);
    auto r = writeInstrument (ins, dir, sound);
    if (r.wasOk())
    {
        juce::ZipFile::Builder b;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, true))
            b.addFile (f, f.hasFileExtension ("wav") ? 0 : 9, base + "/" + f.getRelativePathFrom (dir).replaceCharacter ('\\', '/'));
        zip.getParentDirectory().createDirectory();
        const auto part = zip.getSiblingFile (zip.getFileName() + ".part");
        part.deleteFile();
        {
            juce::FileOutputStream out (part);
            if (! out.openedOk() || ! b.writeToStream (out, nullptr))
                r = juce::Result::fail ("Impossible d'écrire "_fr + zip.getFullPathName());
        }
        if (r.wasOk() && ! part.moveFileTo (zip))
            r = juce::Result::fail ("Impossible d'écrire "_fr + zip.getFullPathName());
        part.deleteFile();
    }
    temp.deleteRecursively();
    return r;
}

bool isImportable (const juce::File& f)
{
    if (f.isDirectory())
        return ! f.findChildFiles (juce::File::findFiles, false, "*.replikant").isEmpty();
    return f.hasFileExtension ("zip;replikant") || isSound (f);
}

bool isSound (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension ("wav;wave;aif;aiff;aifc;flac;ogg;mp3;m4a;caf");
}

std::shared_ptr<Instrument> soundInstrument (rk::Channels audio, double sampleRate, const juce::String& name,
                                             int root, float detuneCents, bool pitchFound, rk::Loop loop)
{
    auto ins = std::make_shared<Instrument>();
    ins->name = name;
    ins->sampleRate = sampleRate;
    auto& ss = ins->session;
    ss.lowNote = ss.highNote = root;
    ss.renderKeys = false;                      // varispeed: the classic sampler
    ss.loop = loop.valid();
    ss.tuning = rk::Tuning::snap;
    ss.normalise = rk::Normalise::off;
    rk::Sample x;
    x.root = x.sent = juce::jlimit (0, 127, root);
    x.velocity = 127;
    x.keyLo = 0;
    x.keyHi = 127;
    x.velLo = 1;
    x.velHi = 127;
    x.sampleRate = sampleRate;
    x.audio = std::move (audio);
    x.loop = loop;
    x.detuneCents = pitchFound ? detuneCents : 0.0f;
    x.pitchConfident = pitchFound;
    x.manualTune = true;                        // never measured again
    x.peak = rk::peakOf (x.audio);
    ins->samples.push_back (std::move (x));
    ins->rebuildIndex();
    return ins;
}

std::shared_ptr<Instrument> readSound (const juce::File& f, juce::String& error)
{
    constexpr double maxSeconds = 180.0;
    rk::Channels audio;
    double sr = 0.0;
    rk::WavInfo info;
    bool read = f.hasFileExtension ("wav;wave") && rk::readWav (f.getFullPathName().toStdString(), audio, sr, info) && ! audio.empty();
    if (! read)
    {
        audio.clear();
        info = {};
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0.0)
        {
            error = "Je ne sais pas lire ce son : "_fr + f.getFileName();
            return nullptr;
        }
        sr = r->sampleRate;
        const int frames = (int) juce::jmin ((juce::int64) (maxSeconds * sr), r->lengthInSamples);
        const int chans = juce::jlimit (1, 2, (int) r->numChannels);
        juce::AudioBuffer<float> buf (chans, frames);
        r->read (&buf, 0, frames, 0, true, chans > 1);
        audio.assign ((size_t) chans, std::vector<float> ((size_t) frames));
        for (int c = 0; c < chans; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + frames, audio[(size_t) c].begin());
        const auto unity = r->metadataValues.getValue ("MidiUnityNote", {});
        if (unity.isNotEmpty())
            info.root = juce::jlimit (0, 127, unity.getIntValue());
        const auto ls = r->metadataValues.getValue ("Loop0Start", {}), le = r->metadataValues.getValue ("Loop0End", {});
        if (ls.isNotEmpty() && le.isNotEmpty())
        {
            info.loop.start = ls.getIntValue();
            info.loop.end = le.getIntValue() + 1;
        }
    }
    if (audio.size() > 2)
        audio.resize (2);
    const int maxFrames = (int) (maxSeconds * sr);
    for (auto& ch : audio)
        if ((int) ch.size() > maxFrames)
            ch.resize ((size_t) maxFrames);
    const int length = audio.empty() ? 0 : (int) audio[0].size();
    if (length < 16)
    {
        error = "Ce son est vide : "_fr + f.getFileName();
        return nullptr;
    }
    if (info.loop.end > length || info.loop.start < 0 || info.loop.end - info.loop.start < 16)
        info.loop = {};

    // its note: the file's, its name's, its pitch's
    int root = info.root;
    float detune = info.root >= 0 ? info.detuneCents : 0.0f;
    bool found = info.root >= 0 && std::abs (info.detuneCents) > 1.0e-3f;
    if (root < 0)
        for (auto& word : juce::StringArray::fromTokens (f.getFileNameWithoutExtension(), " _.()[]{}", ""))
        {
            const int n = rk::parseNote (word.toStdString());
            if (n >= 0)
                root = n;              // (the last one: "Pad 2 C3")
        }
    if (root < 0)
    {
        const auto mono = rk::mixDown (audio);
        const int from = juce::jmin (length - 1, (int) (0.05 * sr));
        const int to = juce::jmin (length, from + (int) (1.0 * sr));
        const auto p = rk::detectPitchWide (mono, sr, 261.6255653, from, to);
        if (p.ok && p.confidence > 0.7f)
        {
            const int semis = (int) std::lround (p.cents / 100.0);
            root = 60 + semis;
            detune = (float) (p.cents - 100.0 * semis);
            found = true;
        }
        else
            root = 60;
    }
    return soundInstrument (std::move (audio), sr, f.getFileNameWithoutExtension(), root, detune, found, info.loop);
}

juce::File importPack (const juce::File& zip, const juce::File& root, juce::String& error)
{
    juce::ZipFile z (zip);
    if (z.getNumEntries() == 0)
    {
        error = "Ce n'est pas un pack REPLIK8 : "_fr + zip.getFileName();
        return {};
    }
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("replikant-import-" + juce::Uuid().toString());
    // (JUCE refuses entries that would land outside the folder)
    const auto r = z.uncompressTo (temp, true);
    auto found = temp.findChildFiles (juce::File::findFiles, true, "*.replikant");
    if (r.failed() || found.isEmpty())
    {
        temp.deleteRecursively();
        error = r.failed() ? r.getErrorMessage() : "Pas d'instrument REPLIK8 dans "_fr + zip.getFileName();
        return {};
    }
    const auto desc = found.getFirst();
    auto name = juce::JSON::parse (desc.loadFileAsString())["name"].toString();
    if (name.isEmpty())
        name = desc.getFileNameWithoutExtension();
    root.createDirectory();
    auto target = root.getChildFile (safeName (name));
    for (int n = 2; target.exists(); ++n)
        target = root.getChildFile (safeName (name) + " " + juce::String (n));
    const auto src = desc.getParentDirectory();
    if (! src.moveFileTo (target) && ! src.copyDirectoryTo (target))
    {
        temp.deleteRecursively();
        error = "Impossible de créer "_fr + target.getFullPathName();
        return {};
    }
    temp.deleteRecursively();
    return target;
}

std::shared_ptr<Instrument> load (const juce::File& where, juce::String& error)
{
    juce::File desc = where;
    if (where.isDirectory())
    {
        auto found = where.findChildFiles (juce::File::findFiles, false, "*.replikant");
        if (found.isEmpty())
        {
            error = "Pas d'instrument REPLIK8 dans " + where.getFullPathName();
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
    ins->sound = json["sound"];
    for (auto& v : *json["samples"].getArray())
    {
        rk::Sample s;
        const auto f = ins->folder.getChildFile (v["file"].toString());
        if (! f.isAChildOf (ins->folder))
        {
            error = "Chemin refusé dans "_fr + desc.getFileName() + " : " + v["file"].toString();
            return nullptr;
        }
        rk::WavInfo info;
        if (! rk::readWav (f.getFullPathName().toStdString(), s.audio, s.sampleRate, info) || s.audio.empty())
        {
            error = "Sample manquant ou illisible : " + f.getFullPathName();
            return nullptr;
        }
        s.file = f.getFileName().toStdString();
        s.root = juce::jlimit (0, 127, get (v, "root", info.root >= 0 ? info.root : 60));
        s.sent = juce::jlimit (-1, 127, get (v, "sent", s.root));
        s.manualTune = get (v, "manualTune", false);
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
    // its cycles
    if (const auto c = json["cycles"]; c.isObject())
    {
        const auto f = ins->folder.getChildFile (c["file"].toString());
        rk::Channels table;
        double tableRate = 0.0;
        rk::WavInfo info;
        const int size = (int) c.getProperty ("size", dsp::CycleBank::size);
        if (f.isAChildOf (ins->folder) && size == dsp::CycleBank::size
            && rk::readWav (f.getFullPathName().toStdString(), table, tableRate, info) && ! table.empty())
        {
            const int count = (int) table[0].size() / size;
            std::vector<std::vector<float>> cycles;
            for (int i = 0; i < count; ++i)
                cycles.emplace_back (table[0].begin() + i * size, table[0].begin() + (i + 1) * size);
            if (count > 0)
                ins->cycles = std::make_shared<const dsp::CycleBank> (cycles, true);
        }
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
    xml.createNewChildElement ("generator")->addTextElement ("REPLIK8");
    xml.createNewChildElement ("category")->addTextElement ("Synth");
    xml.createNewChildElement ("creator")->addTextElement ("REPLIK8");
    xml.createNewChildElement ("description")->addTextElement ("Autosampled with REPLIK8");
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
        const double tune = it.rendered ? 0.0
                          : -(((ins.session.tuneOn() || it.sample.manualTune) && it.sample.pitchConfident ? it.sample.detuneCents : 0.0)
                              + ins.session.tuneCents) / 100.0;
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
