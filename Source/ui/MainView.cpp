#include "MainView.h"
#include "Pixel.h"
#include "Text.h"
#include "../dsp/Modulators.h"

#include <RkNotesData.h>

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using ui::ParamRow;

namespace
{
    juce::String noteText (double v)        { return rk::noteName ((int) v); }
    juce::String intText (double v)         { return juce::String ((int) std::lround (v)); }
    juce::String pct (double v)             { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }
    juce::String db (double v)              { return (v > 0 ? "+" : "") + juce::String (v, 1) + " DB"; }
    juce::String semis (double v)           { return juce::String ((int) std::lround (v)) + " DT"; }
    juce::String megabytes (int64_t b)
    {
        const double mb = (double) b / 1048576.0;
        if (mb >= 1000.0)
            return juce::String (mb / 1024.0, 1) + " GO";
        return (mb < 10.0 ? juce::String (mb, 1) : juce::String (juce::roundToInt (mb))) + " MO";
    }

    bool standalone (const juce::AudioProcessor& p)
    {
        return p.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    }

    const char* samplerPages[] = { "ENREGISTREMENT", "TRAITEMENT", "TRANSPOSITION" };
    const char* playerPages[] = { "SON", "LECTURE", "FILTRE", "SPECTRE", "ESPACE", "MODS", "SCÈNES" };
    constexpr int numPages[2] = { 3, 7 };
    constexpr int rowH = 25;
}

MainView::MainView (ReplikantProcessor& p) : proc (p)
{
    setSize (ui::designWidth, ui::designHeight);
    setWantsKeyboardFocus (true);
    arm.player = &proc.player;
    arm.changed = [this] { armChanged(); };

    const char* modes[2] = { "SAMPLER", "PLAYER" };
    for (int i = 0; i < 2; ++i)
    {
        modeButtons[i].setButtonText (modes[i]);
        modeButtons[i].onClick = [this, i] { showPage (i, page[i]); };
        addAndMakeVisible (modeButtons[i]);
    }
    for (int m = 0; m < snd::numMods; ++m)
    {
        modTabs[m].setButtonText ("M" + juce::String (m + 1));
        modTabs[m].onClick = [this, m] { modShown = m; modView.selected = m; showPage (mode, page[mode]); };
        addChildComponent (modTabs[m]);
    }
    buildSamplerRows();
    buildPlayerRows();

    nameEditor.setText (proc.instrumentName, false);
    nameEditor.setFont (px::text (26.0f));
    nameEditor.setIndents (8, 4);
    nameEditor.onTextChange = [this] { proc.instrumentName = nameEditor.getText(); };
    nameEditor.onReturnKey = [] { juce::Component::unfocusAllComponents(); };
    nameEditor.setTooltip ("Le nom de l'instrument : celui de son dossier, de ses fichiers et de son pack."_fr);
    addAndMakeVisible (nameEditor);

    for (auto* b : { &newButton, &openButton, &exportButton, &audioButton, &zoomButton, &updateButton })
        addAndMakeVisible (b);
    addAndMakeVisible (packTile);
    packTile.setTooltip ("Le pack à partager (l'instrument, ses samples et son son, en un zip) : clique pour le faire, puis glisse-le vers le Finder, AirDrop ou un mail. Chez un pote : il le glisse sur la fenêtre de REPLIKANT."_fr);
    audioButton.setVisible (standalone (proc));
    updateButton.setVisible (false);
    newButton.onClick = [this] { proc.newInstrument(); selectSample (-1); };
    openButton.onClick = [this] { openInstrument(); };
    exportButton.onClick = [this] { showExportMenu(); };
    audioButton.onClick = []
    {
       #if JucePlugin_Build_Standalone
        if (auto* h = juce::StandalonePluginHolder::getInstance())
            h->showAudioSettingsDialog();
       #endif
    };
    zoom = (float) proc.settingsFile().getDoubleValue ("zoom", 1.0);
    zoomButton.setButtonText (juce::String (juce::roundToInt (zoom * 100)) + " %");
    zoomButton.onClick = [this] { showZoomMenu(); };
    updateButton.onClick = [this] { updatePanel.showAvailable (proc.updater.getNews (3), (int) proc.updater.getNewer().size()); };
    newButton.setTooltip ("Repartir d'un instrument vide (l'actuel reste dans son dossier)."_fr);
    openButton.setTooltip ("Ouvrir un instrument, ou un pack reçu (zip). On peut aussi les glisser sur la fenêtre."_fr);
    exportButton.setTooltip ("Exporter en dossier de WAV (Logic, Ableton, Kontakt...), en .multisample Bitwig, ou en pack à partager."_fr);

    sampleButton.setButtonText ("SAMPLER");
    sampleButton.setColour (juce::TextButton::buttonOnColourId, px::red);
    ui::styleBig (sampleButton, true);
    sampleButton.onClick = [this] { toggleSampling(); };
    addAndMakeVisible (sampleButton);
    testButton.onClick = [this] { proc.sendTestNote(); showMessage ("Note de test envoyée : C3 vélocité 100."_fr); };
    testButton.setTooltip ("Envoie un C3 au synthé : tu dois l'entendre et voir le niveau bouger."_fr);
    addAndMakeVisible (testButton);
    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    playButton.setButtonText ("Écouter"_fr);
    playButton.onClick = [this] { playShown(); };
    for (auto* b : { &prevButton, &nextButton, &playButton })
        addAndMakeVisible (b);

    // the sample shown, retuned by hand (all the takes of its note)
    tuneNote = std::make_unique<ParamRow> ("Note", 0, 127, 1, [this]
    {
        return shown != nullptr && selected >= 0 && selected < (int) shown->samples.size() ? (double) shown->samples[(size_t) selected].root : 60.0;
    }, [this] (double v)
    {
        if (shown != nullptr && selected >= 0 && selected < (int) shown->samples.size())
            proc.retuneSample (selected, (int) v, tuneCents != nullptr ? tuneCentsValue : shown->samples[(size_t) selected].detuneCents);
        tuneNoteValue = (int) v;
    }, noteText);
    tuneCents = std::make_unique<ParamRow> ("Accord", -100, 100, 1, [this]
    {
        return shown != nullptr && selected >= 0 && selected < (int) shown->samples.size() ? (double) shown->samples[(size_t) selected].detuneCents : 0.0;
    }, [this] (double v)
    {
        tuneCentsValue = v;
        if (shown != nullptr && selected >= 0 && selected < (int) shown->samples.size())
            proc.retuneSample (selected, tuneNoteValue >= 0 ? tuneNoteValue : shown->samples[(size_t) selected].root, v);
    }, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) std::lround (v)) + " CT"; });
    tuneNote->setTooltip ("La note que ce sample joue vraiment (toutes les prises de la même note envoyée suivent). Réglée à la main, elle n'est plus mesurée."_fr);
    tuneCents->setTooltip ("Son accord en cents : de combien il est au-dessus de sa note (corrigé à la lecture)."_fr);
    for (auto* r : { tuneNote.get(), tuneCents.get() })
    {
        r->locked = [this] { return proc.isSampling(); };
        r->onChange = [this] { repaint(); };
        addChildComponent (r);
    }

    addAndMakeVisible (keymap);
    addAndMakeVisible (wave);
    for (juce::Component* c : { (juce::Component*) &filterView, (juce::Component*) &chainView, (juce::Component*) &modView,
                                (juce::Component*) &sceneView, (juce::Component*) &strip, (juce::Component*) &clockStrip })
        addChildComponent (c);
    clockStrip.clockOnly = true;
    clockStrip.setVisible (true);
    modView.onSelect = [this] (int m) { modShown = m; showPage (mode, page[mode]); };
    keymap.onSelect = [this] (int i) { selectSample (i); };
    keymap.onRangeChanged = [this] { proc.settingsChanged(); repaint(); };

    addChildComponent (updatePanel);
    updatePanel.onClose = [this] { updatePanel.setVisible (false); };

    proc.onMessage = [this] (const juce::String& m) { showMessage (m); };
    proc.addChangeListener (this);
    proc.updater.changes.addChangeListener (this);

    // what this version brought, the first time it opens
    {
        auto& set = proc.settingsFile();
        const auto nowVersion = Updater::currentVersion();
        const auto before = set.getValue ("lastRunVersion");
        if (before.isNotEmpty() && before != nowVersion)
            pendingNews = Updater::summarise (nowVersion, juce::String::fromUTF8 (RkNotesData::RELEASE_NOTES_md, RkNotesData::RELEASE_NOTES_mdSize));
        if (before != nowVersion)
        {
            set.setValue ("lastRunVersion", nowVersion);
            set.saveIfNeeded();
        }
    }

    mode = juce::jlimit (0, 1, proc.settingsFile().getIntValue ("mode", 0));
    showPage (mode, juce::jlimit (0, numPages[mode] - 1, proc.settingsFile().getIntValue (mode == 0 ? "samplerPage" : "playerPage", 0)));
    refreshMidiChoices();
    refreshAll();
    refreshUpdate();
    proc.updater.checkIfDue();
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    proc.onMessage = nullptr;
    proc.removeChangeListener (this);
    proc.updater.changes.removeChangeListener (this);
}

//==============================================================================
MainView::Group& MainView::group (const juce::String& title, int m, int pg, int toggle)
{
    groups.push_back ({ title, m, pg, {}, {}, {}, toggle });
    return groups.back();
}

ParamRow& MainView::add (Group& g, std::unique_ptr<ParamRow> r, bool mapping)
{
    auto* row = r.get();
    row->locked = [this] { return proc.isSampling(); };
    row->onChange = mapping ? std::function<void()> ([this] { proc.mappingChanged(); repaint(); })
                            : std::function<void()> ([this] { proc.settingsChanged(); repaint(); });
    addChildComponent (row);
    g.rows.push_back (row);
    rows.push_back (std::move (r));
    return *row;
}

ParamRow& MainView::sound (Group& g, int id, std::function<bool()> shownWhen, const char* label)
{
    const auto& d = snd::def (id);
    auto* param = proc.params.getParameter (d.id);
    std::unique_ptr<ParamRow> r;
    const auto name = juce::String::fromUTF8 (label != nullptr ? label : d.name);
    if (d.kind == snd::Kind::choice || d.kind == snd::Kind::toggle)
    {
        juce::StringArray c;
        for (auto& t : snd::choicesOf (id))
            c.add (juce::String::fromUTF8 (t.c_str()));
        const float lo = d.lo;
        r = std::make_unique<ParamRow> (name, c, [this, id, lo] { return (int) std::lround (proc.player.get (id) - lo); },
                                        [param, lo] (int v) { param->setValueNotifyingHost (param->convertTo0to1 ((float) v + lo)); });
    }
    else
    {
        r = std::make_unique<ParamRow> (name, d.lo, d.hi, d.kind == snd::Kind::integer ? 1.0 : 0.0,
                                        [this, id] { return (double) proc.player.get (id); },
                                        [param, id] (double v) { param->setValueNotifyingHost (param->convertTo0to1 (snd::legal (id, (float) v))); },
                                        [id] (double v) { return ui::soundText (id, (float) v); });
        r->toNormFn = [id] (double v) { return (double) snd::toNorm (id, (float) v); };
        r->fromNormFn = [id] (double t) { return (double) snd::fromNorm (id, (float) t); };
    }
    r->param = id;
    r->arm = &arm;
    r->shown = std::move (shownWhen);
    r->onGestureStart = [param] { param->beginChangeGesture(); };
    r->onGestureEnd = [param] { param->endChangeGesture(); };
    auto& row = add (g, std::move (r));
    row.locked = nullptr;                    // the sound stays free during a session
    row.onChange = [this] { repaint(); };
    return row;
}

juce::Colour MainView::pageColour (int m, int pg) const
{
    // one accent; the modulators and the scenes keep their own
    if (m == 1 && pg == 5)
        return px::mod[modShown];
    if (m == 1 && pg == 6)
        return px::violet;
    return px::accent;
}

void MainView::buildSamplerRows()
{
    auto& s = proc.session;
    auto& o = proc.options;
    auto num = [] (const juce::String& n, double lo, double hi, double st, std::function<double()> g, std::function<void (double)> set,
                   std::function<juce::String (double)> f, bool log = false)
    {
        return std::make_unique<ParamRow> (n, lo, hi, st, std::move (g), std::move (set), std::move (f), log);
    };
    auto choice = [] (const juce::String& n, juce::StringArray c, std::function<int()> g, std::function<void (int)> set)
    {
        return std::make_unique<ParamRow> (n, std::move (c), std::move (g), std::move (set));
    };
    groups.reserve (40);
    //------------------------------------------------------------------ recording
    {
        auto& g = group ("Source", 0, 0);
        add (g, choice ("Son", { "ENTRÉE"_fr, "SIDECHAIN", "SYNTHÉ TEST"_fr }, [&o] { return (int) o.source; },
                        [&o] (int v) { o.source = (ReplikantProcessor::Source) v; }))
            .setTooltip ("D'où vient le son du synthé : l'entrée du plug-in (ou de la carte son), le sidechain, ou le petit synthé intégré pour essayer."_fr);
        auto& m = add (g, choice ("Sortie MIDI", { "AUCUNE" }, [this]
        {
            if (proc.options.midiPort == "host") return 1;
            for (int i = 0; i < midiDevices.size(); ++i)
                if (midiDevices[i].identifier == proc.options.midiPort) return i + 2;
            return 0;
        }, [this] (int v)
        {
            if (v == 0) proc.setMidiPort ({}, {});
            else if (v == 1) proc.setMidiPort ("host", "DAW");
            else if (v - 2 < midiDevices.size()) proc.setMidiPort (midiDevices[v - 2].identifier, midiDevices[v - 2].name);
        }));
        m.enabled = [this] { return proc.options.source != ReplikantProcessor::Source::testSynth; };
        m.setTooltip ("Le port MIDI qui va au synthé. HOTE : la sortie MIDI du plug-in (si ton DAW la route)."_fr);
        midiRow = &m;
        add (g, num ("Canal", 1, 16, 1, [&o] { return (double) o.channel; }, [&o] (double v) { o.channel = (int) v; }, intText));
        add (g, num ("Programme", -1, 127, 1, [&o] { return (double) o.program; }, [&o] (double v) { o.program = (int) v; },
                     [] (double v) { return v < 0 ? juce::String ("AUCUN") : juce::String ((int) v + 1); }))
            .setTooltip ("Un changement de programme envoyé avant la session (AUCUN : le son réglé sur le synthé)."_fr);
        add (g, choice ("Écoute"_fr, { "NON", "DIRECTE", "PAR LES EFFETS" }, [&o] { return o.monitor; }, [&o] (int v) { o.monitor = v; }))
            .setTooltip ("Entendre le synthé (ou la piste) à travers REPLIKANT : tel quel, ou passé dans le filtre et les effets du player (multi-effet). Pendant une session, toujours tel quel."_fr);
    }
    {
        auto& g = group ("Notes", 0, 0);
        add (g, choice ("Mode", { "INTERVALLE", "NOMBRE", "PERSO" }, [&s] { return (int) s.noteMode; },
                        [&s] (int v) { s.noteMode = (rk::NoteMode) v; if (s.noteMode == rk::NoteMode::custom && s.customNotes.empty()) s.customNotes = rk::plannedNotes (rk::Session { s }); }))
            .setTooltip ("INTERVALLE : une note tous les N demi-tons. NOMBRE : N notes réparties. PERSO : Alt-clic sur les touches du clavier."_fr);
        add (g, num ("De", 0, 127, 1, [&s] { return (double) s.lowNote; }, [&s] (double v) { s.lowNote = (int) v; if (s.highNote < s.lowNote) s.highNote = s.lowNote; }, noteText));
        add (g, num ("À"_fr, 0, 127, 1, [&s] { return (double) s.highNote; }, [&s] (double v) { s.highNote = (int) v; if (s.lowNote > s.highNote) s.lowNote = s.highNote; }, noteText));
        auto& iv = add (g, num ("Tous les", 1, 24, 1, [&s] { return (double) s.interval; }, [&s] (double v) { s.interval = (int) v; }, semis));
        iv.enabled = [&s] { return s.noteMode == rk::NoteMode::interval; };
        auto& nc = add (g, num ("Nombre", 1, 64, 1, [&s] { return (double) s.noteCount; }, [&s] (double v) { s.noteCount = (int) v; }, intText));
        nc.enabled = [&s] { return s.noteMode == rk::NoteMode::count; };
    }
    {
        auto& g = group ("Vélocités"_fr, 0, 0);
        add (g, num ("Couches", 1, 16, 1, [&s] { return (double) s.layers; }, [&s] (double v) { s.layers = (int) v; }, intText));
        add (g, choice ("Répartition"_fr, { "LINÉAIRE"_fr, "DOUCES", "FORTES" }, [&s] { return juce::jmin (2, (int) s.curve); },
                        [&s] (int v) { s.curve = (rk::VelCurve) v; }))
            .setTooltip ("Où mettre les couches : réparties également, plus nombreuses dans le jeu doux, ou dans le fort."_fr);
        add (g, num ("Round robin", 1, 8, 1, [&s] { return (double) s.roundRobins; }, [&s] (double v) { s.roundRobins = (int) v; },
                     [] (double v) { return juce::String ((int) v) + ((int) v > 1 ? " PRISES" : " PRISE"); }))
            .setTooltip ("Plusieurs prises de chaque note, jouées à tour de rôle (vie des synthés analogiques)."_fr);
    }
    {
        auto& g = group ("Durées"_fr, 0, 0);
        add (g, choice ("Jeu", { "TENUE", "ONE-SHOT" }, [&s] { return s.oneShot ? 1 : 0; }, [&s] (int v) { s.oneShot = v == 1; }))
            .setTooltip ("TENUE : la note tient tant que la touche est enfoncée (boucle). ONE-SHOT : joue jusqu'au bout (percussions)."_fr);
        add (g, num ("Tenue", 0.05, 30.0, 0.0, [&s] { return s.hold; }, [&s] (double v) { s.hold = v; }, ui::ms, true));
        add (g, num ("Relâche max"_fr, 0.0, 30.0, 0.05, [&s] { return s.releaseMax; }, [&s] (double v) { s.releaseMax = v; }, ui::ms))
            .setTooltip ("La plus longue queue enregistrée après le relâchement ; elle s'arrête avant si le son retombe dans le bruit."_fr);
        add (g, num ("Pause", 0.0, 5.0, 0.05, [&s] { return s.pause; }, [&s] (double v) { s.pause = v; }, ui::ms));
    }
    //------------------------------------------------------------------ processing
    {
        auto& g = group ("Boucles", 0, 1);
        add (g, choice ("Boucle", { "AUTO", "AUCUNE" }, [&s] { return s.loop ? 0 : 1; }, [&s] (int v) { s.loop = v == 0; }));
        auto loopOn = [&s] { return s.loop && ! s.oneShot; };
        auto& a = add (g, num ("Chercher de", 0.05, 0.9, 0.01, [&s] { return s.loopSettings.searchStart; },
                               [&s] (double v) { s.loopSettings.searchStart = v; if (s.loopSettings.searchEnd < v + 0.05) s.loopSettings.searchEnd = v + 0.05; }, pct));
        a.enabled = loopOn;
        a.setTooltip ("Où la boucle peut commencer, en % de la partie tenue (après l'attaque)."_fr);
        auto& b = add (g, num ("Jusqu'à"_fr, 0.1, 1.0, 0.01, [&s] { return s.loopSettings.searchEnd; },
                               [&s] (double v) { s.loopSettings.searchEnd = v; if (s.loopSettings.searchStart > v - 0.05) s.loopSettings.searchStart = v - 0.05; }, pct));
        b.enabled = loopOn;
        auto& c = add (g, num ("Longueur min", 0.01, 10.0, 0.0, [&s] { return s.loopSettings.minSeconds; }, [&s] (double v) { s.loopSettings.minSeconds = v; }, ui::ms, true));
        c.enabled = loopOn;
        auto& d = add (g, num ("Fondu", 0.0, 1.0, 0.005, [&s] { return s.loopSettings.crossfadeSeconds; }, [&s] (double v) { s.loopSettings.crossfadeSeconds = v; }, ui::ms));
        d.enabled = loopOn;
        d.setTooltip ("Le fondu du raccord de boucle, calculé pour garder la puissance (ni creux ni bosse)."_fr);
    }
    {
        auto& g = group ("Attaque", 0, 1);
        add (g, choice ("Calage", { "PAR NOTE", "LATENCE FIXE" }, [&s] { return (int) s.align; }, [&s] (int v) { s.align = (rk::Align) v; }))
            .setTooltip ("PAR NOTE : chaque sample commence à son attaque. LATENCE FIXE : la même latence mesurée pour tous (garde la forme exacte des attaques lentes)."_fr);
        add (g, num ("Seuil", 3.0, 40.0, 1.0, [&s] { return s.thresholdDb; }, [&s] (double v) { s.thresholdDb = v; },
                     [] (double v) { return "+" + juce::String ((int) v) + " DB"; }))
            .setTooltip ("L'attaque est détectée quand le son dépasse le bruit de fond de tant."_fr);
        add (g, num ("Marge avant", 0.0, 20.0, 0.5, [&s] { return s.preMs; }, [&s] (double v) { s.preMs = v; },
                     [] (double v) { return juce::String (v, 1) + " MS"; }));
    }
    {
        auto& g = group ("Niveau et accord", 0, 1);
        add (g, choice ("Normalisation", { "INSTRUMENT", "PAR SAMPLE", "AUCUNE" }, [&s] { return (int) s.normalise; }, [&s] (int v) { s.normalise = (rk::Normalise) v; }))
            .setTooltip ("INSTRUMENT : tout ensemble, les couches de vélocité gardent leurs écarts. PAR SAMPLE : chacun au maximum."_fr);
        auto& t = add (g, num ("Cible", -24.0, 0.0, 0.1, [&s] { return s.normaliseDb; }, [&s] (double v) { s.normaliseDb = v; }, db));
        t.enabled = [&s] { return s.normalise != rk::Normalise::off; };
        add (g, choice ("Retirer le DC", { "OUI", "NON" }, [&s] { return s.removeDc ? 0 : 1; }, [&s] (int v) { s.removeDc = v == 0; }));
        add (g, choice ("Accordage", { "RECALER", "FIN", "NON" }, [&s] { return (int) s.tuning; }, [&s] (int v) { s.tuning = (rk::Tuning) v; }), true)
            .setTooltip ("RECALER : la note vraiment jouée est retrouvée (même à quelques demi-tons, un oscillateur mal réglé) et remise sur sa touche, puis jouée juste. FIN : seulement les cents. NON : les notes telles qu'envoyées."_fr);
        add (g, num ("Accord global", -100.0, 100.0, 1.0, [&s] { return s.tuneCents; }, [&s] (double v) { s.tuneCents = v; },
                     [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) std::lround (v)) + " CT"; }), true)
            .setTooltip ("Tout l'instrument réaccordé à la main, en cents (+ : plus haut). Une note seule : sous le sample, NOTE et ACCORD."_fr);
        add (g, choice ("Fichiers", { "24 BITS", "32 BITS FLOTTANT" }, [&s] { return s.fileBits == 32 ? 1 : 0; }, [&s] (int v) { s.fileBits = v == 1 ? 32 : 24; }))
            .setTooltip ("Les WAV écrits : 24 bits (144 dB, plus que n'importe quel synthé) ou 32 bits flottants (un tiers plus lourds). Le calcul, lui, est toujours en 32 bits flottants."_fr);
    }
    //------------------------------------------------------------------ transposition
    {
        auto& g = group ("Transposition", 0, 2);
        add (g, choice ("Algorithme", { "HQ FORMANTS", "CLASSIQUE HD", "DURÉE FIXE"_fr }, [&s] { return s.algo == rk::Algo::formant ? 0 : s.algo == rk::Algo::classic ? 1 : 2; },
                        [&s] (int v) { s.algo = v == 0 ? rk::Algo::formant : v == 1 ? rk::Algo::classic : rk::Algo::fixedTime; }), true)
            .setTooltip ("HQ FORMANTS : varispeed sans repliement, puis l'enveloppe spectrale remise en place (le filtre du synthé ne bouge pas). CLASSIQUE HD : varispeed seul. DUREE FIXE : garde aussi les durées (attaques, decays)."_fr);
        auto& f = add (g, num ("Formants", 0.0, 1.0, 0.01, [&s] { return s.formant; }, [&s] (double v) { s.formant = v; }, pct), true);
        f.enabled = [&s] { return s.algo != rk::Algo::classic; };
        f.setTooltip ("100 % : le timbre ne bouge pas avec la note (filtre fixe). Moins : le timbre suit un peu le clavier (comme un key tracking)."_fr);
        add (g, choice ("Placement", { "CENTRÉ"_fr, "VERS LE HAUT", "VERS LE BAS" }, [&s] { return (int) s.placement; }, [&s] (int v) { s.placement = (rk::Placement) v; }), true)
            .setTooltip ("Où se trouve la note enregistrée dans sa zone : au milieu, en bas (on transpose vers le haut) ou en haut."_fr);
        add (g, num ("Étendre"_fr, 0, 48, 1, [&s] { return (double) s.extend; }, [&s] (double v) { s.extend = (int) v; }, semis), true)
            .setTooltip ("Jouable au-delà des notes enregistrées, de chaque côté."_fr);
        add (g, choice ("Touches", { "RENDUES HQ", "EN DIRECT" }, [&s] { return s.renderKeys ? 0 : 1; }, [&s] (int v) { s.renderKeys = v == 0; }), true)
            .setTooltip ("RENDUES HQ : chaque touche est calculée à l'avance avec l'algorithme (le meilleur son, plus de mémoire). EN DIRECT : varispeed à la volée."_fr);
    }
}

void MainView::buildPlayerRows()
{
    auto& o = proc.options;
    auto P = [this] (int id) { return proc.player.get (id); };
    auto elastic = [P] { return P (snd::playMode) > 0.5f; };
    //------------------------------------------------------------------ SON
    {
        auto& g = group ("Voix", 1, 0);
        sound (g, snd::voiceMode).setTooltip ("POLY : jusqu'à 8 voix. MONO : une voix, chaque note repart. LEGATO : une voix, la note glisse sur la suivante sans repartir."_fr);
        sound (g, snd::voices, [P] { return P (snd::voiceMode) < 0.5f; });
        sound (g, snd::glide).setTooltip ("Le temps que met la hauteur pour rejoindre la note (portamento). 0 : aucun."_fr);
        sound (g, snd::transpose).setTooltip ("Transpose tout : la touche jouée prend le sample (rendu HQ) de la note décalée."_fr);
        sound (g, snd::fine);
        sound (g, snd::bend);
        sound (g, snd::velocity).setTooltip ("Combien la vélocité jouée baisse le niveau (les couches de vélocité changent, elles, le timbre)."_fr);
        sound (g, snd::volume);
    }
    {
        auto& g = group ("Ampli", 1, 0);
        sound (g, snd::attack);
        sound (g, snd::decay);
        sound (g, snd::sustain);
        sound (g, snd::release);
    }
    {
        auto& g = group ("Entrée"_fr, 1, 0);
        add (g, std::make_unique<ParamRow> ("Écoute"_fr, juce::StringArray { "NON", "DIRECTE", "PAR LES EFFETS" }, [&o] { return o.monitor; },
                                            [&o] (int v) { o.monitor = v; }))
            .setTooltip ("L'audio qui arrive (le synthé, ou la piste où REPLIKANT est en insert) : coupé, tel quel, ou passé dans le filtre et les effets (multi-effet, avec les mods)."_fr);
        g.rows.back()->locked = nullptr;
        add (g, std::make_unique<ParamRow> ("Clavier vers", juce::StringArray { "INSTRUMENT", "SYNTHÉ"_fr }, [&o] { return o.thru ? 1 : 0; },
                                            [&o] (int v) { o.thru = v == 1; }))
            .setTooltip ("Les notes jouées dans REPLIKANT font sonner l'instrument échantillonné, ou vont au synthé (pour comparer avec l'original)."_fr);
    }
    //------------------------------------------------------------------ LECTURE
    {
        auto& g = group ("Lecture", 1, 1);
        sound (g, snd::playMode).setTooltip ("SAMPLE : lu tel quel (les touches rendues HQ). ÉLASTIQUE : en grains, la vitesse et la hauteur séparées (0 : figé, négatif : à l'envers)."_fr);
        sound (g, snd::start).setTooltip ("Où la lecture commence dans le sample. En ÉLASTIQUE, le moduler promène la lecture dans le son."_fr);
        sound (g, snd::speed, elastic);
        sound (g, snd::grain, elastic);
        sound (g, snd::spray, elastic).setTooltip ("Le hasard de la position des grains. Sous 30 %, les grains s'alignent sur le son (pas de phasing)."_fr);
        sound (g, snd::reverse);
        sound (g, snd::loops);
        sound (g, snd::loopStart).setTooltip ("La boucle déplacée dans le sample (ses raccords lissés)."_fr);
        sound (g, snd::loopSize).setTooltip ("La boucle raccourcie : jusqu'à un grain qui bégaie."_fr);
        sound (g, snd::transients, [elastic] { return ! elastic(); })
            .setTooltip ("GARDÉS : l'attaque de chaque sample est jouée à sa vitesse, le corps seul est transposé : un kick descendu de deux octaves garde son clic."_fr);
    }
    {
        auto& g = group ("Caractère"_fr, 1, 1);
        sound (g, snd::alias).setTooltip ("0 : la lecture HQ sans repliement. Puis le filtre de l'interpolateur se relâche (E-mu), linéaire, drop-sample (SP-1200), jusqu'aux alias plus forts que le son."_fr);
        sound (g, snd::bits);
    }
    {
        auto& g = group ("Modulation de phase", 1, 1);
        sound (g, snd::pmSource, nullptr, "Source").setTooltip ("Pousse l'adresse de lecture du sample : SINUS (sur la note), SAMPLE (lui-même, une deuxième tête), ENTRÉE (l'audio qui arrive)."_fr);
        sound (g, snd::pmRatio, [P] { return (int) P (snd::pmSource) == 1 || (int) P (snd::pmSource) == 2; });
        sound (g, snd::pmIndex, [P] { return (int) P (snd::pmSource) != 0; }).setTooltip ("De combien d'échantillons la lecture est poussée, de 0 à 256."_fr);
    }
    //------------------------------------------------------------------ FILTRE
    {
        auto& g = group ("Filtre", 1, 2);
        auto on = [P] { return P (snd::fModel) > 0.5f; };
        sound (g, snd::fModel).setTooltip ("Les modèles : MOOG (échelle), MS-20 (les deux filtres), SEM, OSCAR et WASP (avec la séparation), POLIVOKS. Suréchantillonnés."_fr);
        sound (g, snd::fType, on);
        sound (g, snd::cutoff, on);
        sound (g, snd::reso, on);
        sound (g, snd::fDrive, on);
        sound (g, snd::sep, [P] { const int m = (int) P (snd::fModel); return m == 2 || m == 4; })
            .setTooltip ("MS-20 : où se place l'autre filtre. OSCAR : l'écart entre ses deux filtres."_fr);
        sound (g, snd::fm, on).setTooltip ("Le filtre réinjecté dans sa propre coupure : de la rugosité au chaos."_fr);
        sound (g, snd::quality, on).setTooltip ("Le suréchantillonnage du filtre (plus haut : plus propre, plus de calcul)."_fr);
    }
    {
        auto& g = group ("Enveloppe du filtre", 1, 2);
        auto on = [P] { return P (snd::fModel) > 0.5f; };
        sound (g, snd::fEnv, on);
        sound (g, snd::fAttack, on);
        sound (g, snd::fDecay, on);
        sound (g, snd::fSustain, on);
        sound (g, snd::fRelease, on);
        sound (g, snd::keyTrack, on);
        sound (g, snd::velCutoff, on);
    }
    {
        auto& g = group ("Drive", 1, 2);
        sound (g, snd::dModel).setTooltip ("PRÉAMPLI (façon Neve), CONSOLE (la sommation poussée), BANDE (façon Ampex : saturation, bosse de grave, pleurage)."_fr);
        auto on = [P] { return P (snd::dModel) > 0.5f; };
        sound (g, snd::dAmt, on);
        sound (g, snd::dTone, on);
        sound (g, snd::dMix, on);
    }
    //------------------------------------------------------------------ SPECTRE
    {
        auto& g = group ("Spectre", 1, 3, snd::spOn);
        for (int id : { snd::spVoices, snd::spSlice, snd::spBlur, snd::spGlide, snd::spCentre, snd::spWidth, snd::spOct, snd::spWave,
                        snd::spFreeze, snd::spFeedback, snd::spMix })
            sound (g, id);
    }
    {
        auto& g = group ("Pitch", 1, 3, snd::piOn);
        for (int id : { snd::piA, snd::piB, snd::piDetune, snd::piDelay, snd::piLevelB, snd::piFeedback, snd::piCrystals, snd::piMix })
            sound (g, id);
    }
    //------------------------------------------------------------------ ESPACE
    {
        auto& g = group ("Chorus", 1, 4, snd::chOn);
        for (int id : { snd::chMode, snd::chRate, snd::chDepth, snd::chFeedback, snd::chTone, snd::chMix })
            sound (g, id);
    }
    {
        auto& g = group ("Crush", 1, 4, snd::crOn);
        for (int id : { snd::crBits, snd::crRate, snd::crJitter, snd::crTone, snd::crMix })
            sound (g, id);
    }
    {
        auto& g = group ("Espace", 1, 4, snd::rvOn);
        for (int id : { snd::rvSize, snd::rvDecay, snd::rvTone, snd::rvShimmer, snd::rvMod, snd::rvMix })
            sound (g, id);
    }
    //------------------------------------------------------------------ MODS
    {
        auto& g = group ("Modulateur", 1, 5);
        using dsp::ModType;
        for (int m = 0; m < snd::numMods; ++m)
        {
            auto type = [P, m] { return (ModType) (int) P (snd::modParam (m, snd::mType)); };
            auto here = [this, m] { return modShown == m; };
            auto periodic = [type] { const auto t = type(); return t == ModType::wave || t == ModType::steps || t == ModType::random; };
            auto is = [here, type] (std::initializer_list<ModType> ts)
            {
                std::vector<ModType> v (ts);
                return std::function<bool()> ([here, type, v] { return here() && std::find (v.begin(), v.end(), type()) != v.end(); });
            };
            auto M = [m] (snd::ModSetting w) { return snd::modParam (m, w); };
            sound (g, M (snd::mType), here).setTooltip ("ONDE : un LFO. PAS : jusqu'à 16 pas (dessine-les à droite). HASARD : une boucle de tirages qui revient. ENV : une enveloppe que chaque note lance. SUIVEUR : suit le niveau de l'entrée ou de l'instrument."_fr);
            sound (g, M (snd::mShape), is ({ ModType::wave }));
            sound (g, M (snd::mLen), is ({ ModType::steps, ModType::random }));
            sound (g, M (snd::mVari), is ({ ModType::random })).setTooltip ("0 : la même boucle pour toujours. 100 % : jamais la même."_fr);
            sound (g, M (snd::mSync), [here, periodic] { return here() && periodic(); });
            sound (g, M (snd::mDiv), [here, periodic, P, m] { return here() && periodic() && P (snd::modParam (m, snd::mSync)) < 0.5f; });
            sound (g, M (snd::mHz), [here, periodic, P, m] { return here() && periodic() && P (snd::modParam (m, snd::mSync)) > 0.5f; });
            sound (g, M (snd::mSmooth), [here, periodic] { return here() && periodic(); });
            sound (g, M (snd::mPolarity), [here, periodic] { return here() && periodic(); });
            sound (g, M (snd::mRetrig), [here, periodic] { return here() && periodic(); });
            sound (g, M (snd::mSource), is ({ ModType::follower }));
            sound (g, M (snd::mGain), is ({ ModType::follower }));
            sound (g, M (snd::mAttack), is ({ ModType::envelope, ModType::follower }));
            sound (g, M (snd::mDecay), is ({ ModType::envelope }));
            sound (g, M (snd::mSustain), is ({ ModType::envelope }));
            sound (g, M (snd::mRelease), is ({ ModType::envelope, ModType::follower }));
            sound (g, M (snd::mAmt), here);
        }
    }
    {
        auto& g = group ("Tempo et horloge MIDI", 1, 5);
        sound (g, snd::tempo).setTooltip ("Le tempo des mods et de l'horloge quand le DAW ne joue pas (ou dans l'appli)."_fr);
        add (g, std::make_unique<ParamRow> ("Horloge", juce::StringArray { "INTERNE", "SUIT L'HÔTE"_fr },
                                            [this] { return (int) proc.clock.getMode(); },
                                            [this] (int v) { proc.setClockMode ((MidiClock::Mode) v); }))
            .setTooltip ("INTERNE : au tempo ci-dessus, lancée par le bouton de l'horloge en bas. SUIT L'HÔTE : démarre et s'arrête avec le DAW, sur sa grille."_fr);
        g.rows.back()->locked = nullptr;
        auto& c = add (g, std::make_unique<ParamRow> ("Vers", juce::StringArray { "AUCUNE" }, [this]
        {
            const auto id = proc.clock.destinationId();
            if (id == "host") return 1;
            for (int i = 0; i < midiDevices.size(); ++i)
                if (midiDevices[i].identifier == id) return i + 2;
            return 0;
        }, [this] (int v)
        {
            if (v == 0) proc.setClockDestination ({}, {});
            else if (v == 1) proc.setClockDestination ("host", "DAW");
            else if (v - 2 < midiDevices.size()) proc.setClockDestination (midiDevices[v - 2].identifier, midiDevices[v - 2].name);
        }));
        c.setTooltip ("Où va l'horloge : un port MIDI du Mac (une interface vers tes séquenceurs analogiques) ou la sortie MIDI du plug-in."_fr);
        c.locked = nullptr;
        clockRow = &c;
        add (g, std::make_unique<ParamRow> ("Pendant le sampling", juce::StringArray { "NON", "OUI" }, [this] { return proc.clockWithSampling ? 1 : 0; },
                                            [this] (int v) { proc.clockWithSampling = v == 1; proc.settingsChanged(); }))
            .setTooltip ("Une session de sampling lance l'horloge et l'arrête à la fin (les arpèges et LFO synchronisés du synthé)."_fr);
        g.rows.back()->locked = nullptr;
    }
    //------------------------------------------------------------------ SCÈNES
    {
        auto& g = group ("Scènes"_fr, 1, 6);
        sound (g, snd::sceneA);
        sound (g, snd::sceneB);
        sound (g, snd::xfade).setTooltip ("Le fader, de la scène A à la scène B (aussi en bas). Modulable par les mods, automatisable."_fr);
    }
}

void MainView::refreshMidiChoices()
{
    midiDevices = proc.midiOutputs();
    juce::StringArray names { "AUCUNE", "HÔTE (DAW)"_fr };
    for (auto& d : midiDevices)
        names.add (upper (d.name));
    auto withAbsent = [&] (const juce::String& id, const juce::String& name, juce::StringArray list)
    {
        // a port remembered but absent stays visible, so it isn't silently replaced
        if (id.isNotEmpty() && id != "host")
        {
            bool found = false;
            for (auto& d : midiDevices)
                found = found || d.identifier == id;
            if (! found)
            {
                midiDevices.add ({ name, id });
                list.add (upper (name) + " (ABSENT)");
            }
        }
        return list;
    };
    if (midiRow != nullptr)
        midiRow->setChoices (withAbsent (proc.options.midiPort, proc.options.midiPortName, names));
    if (clockRow != nullptr)
    {
        juce::StringArray c { "AUCUNE", "SORTIE DU PLUG-IN" };
        for (auto& d : midiDevices)
            if (! names.contains (upper (d.name) + " (ABSENT)"))
                c.add (upper (d.name));
        clockRow->setChoices (withAbsent (proc.clock.destinationId(), proc.clock.destinationName(), c));
    }
}

void MainView::showPage (int m, int p)
{
    mode = juce::jlimit (0, 1, m);
    page[mode] = juce::jlimit (0, numPages[mode] - 1, p);
    auto& set = proc.settingsFile();
    set.setValue ("mode", mode);
    set.setValue (mode == 0 ? "samplerPage" : "playerPage", page[mode]);
    for (int i = 0; i < 2; ++i)
    {
        modeButtons[i].setColour (juce::TextButton::buttonOnColourId, px::accent);
        ui::styleBig (modeButtons[i], i == mode);
    }
    pageButtons.clear();
    for (int i = 0; i < numPages[mode]; ++i)
    {
        auto* b = pageButtons.add (new juce::TextButton (juce::String::fromUTF8 (mode == 0 ? samplerPages[i] : playerPages[i])));
        b->onClick = [this, i] { showPage (mode, i); };
        b->setColour (juce::TextButton::buttonOnColourId, px::white);
        ui::styleBig (*b, i == page[mode]);
        addAndMakeVisible (b);
    }
    if (mode == 0 && arm.any())
    {
        arm.mod = arm.scene = -1;
        armChanged();
    }
    resized();
    repaint();
}

//==============================================================================
void MainView::resized()
{
    if (tuneNote == nullptr || tuneCents == nullptr)     // (setSize, while the constructor is still building)
        return;
    auto r = getLocalBounds();
    header = r.removeFromTop (60);
    bottom = r.removeFromBottom (92);
    r.reduce (12, 4);
    left = r.removeFromLeft (440);
    r.removeFromLeft (12);
    mapCard = r.removeFromTop (r.getHeight() * (mode == 0 ? 53 : 40) / 100);
    r.removeFromTop (10);
    waveCard = r;

    // header
    {
        auto h = header.reduced (12, 12);
        auto rightPart = h.removeFromRight (760);
        nameEditor.setBounds (h.removeFromRight (250).withSizeKeepingCentre (250, 36));
        auto place = [&rightPart] (juce::Component& b, int w)
        {
            if (! b.isVisible())
                return;
            b.setBounds (rightPart.removeFromRight (w).withSizeKeepingCentre (w, 38));
            rightPart.removeFromRight (8);
        };
        place (zoomButton, 76);
        place (updateButton, 150);
        place (audioButton, 116);
        place (packTile, 120);
        place (exportButton, 108);
        place (openButton, 92);
        place (newButton, 106);
    }

    // settings: SAMPLER / PLAYER, the pages, then the cards of the page
    for (auto& r2 : rows)
        r2->setVisible (false);
    for (auto& m : modTabs)
        m.setVisible (false);
    {
        auto l = left;
        auto modesRow = l.removeFromTop (36);
        for (int i = 0; i < 2; ++i)
        {
            modeButtons[i].setBounds (modesRow.removeFromLeft ((left.getWidth() - 6) / 2).withHeight (34));
            modesRow.removeFromLeft (6);
        }
        l.removeFromTop (6);
        auto tabs = l.removeFromTop (32);
        banner = tabs;
        const int n = pageButtons.size();
        const int w = (tabs.getWidth() - 4 * (n - 1)) / juce::jmax (1, n);
        for (auto* b : pageButtons)
        {
            b->setBounds (tabs.removeFromLeft (w).withHeight (30));
            tabs.removeFromLeft (4);
        }
        l.removeFromTop (8);
        if (mode == 1 && page[1] == 5)
        {
            auto t = l.removeFromTop (30);
            for (int m = 0; m < snd::numMods; ++m)
            {
                modTabs[m].setBounds (t.removeFromLeft ((left.getWidth() - 18) / 4).withHeight (28));
                t.removeFromLeft (6);
                modTabs[m].setVisible (true);
                modTabs[m].setColour (juce::TextButton::buttonOnColourId, px::mod[m]);
                ui::styleBig (modTabs[m], m == modShown);
                if (m != modShown)
                    modTabs[m].setColour (juce::TextButton::textColourOffId, px::mod[m]);
            }
            l.removeFromTop (8);
        }
        for (auto& g : groups)
        {
            if (g.mode != mode || g.page != page[mode])
                continue;
            std::vector<ParamRow*> visible;
            for (auto* row : g.rows)
                if (row->isShown())
                    visible.push_back (row);
            const int h = 24 + 10 + (int) visible.size() * rowH + 4;
            g.bounds = l.removeFromTop (h);
            g.band = g.bounds.withHeight (24).reduced (2, 2);
            l.removeFromTop (8);
            auto in = g.bounds.withTrimmedTop (24).reduced (10, 6);
            for (auto* row : visible)
            {
                row->setBounds (in.removeFromTop (rowH).withTrimmedBottom (2));
                row->setVisible (true);
            }
        }
    }

    // the map, and the card under it
    keymap.setBounds (mapCard.withTrimmedTop (26).reduced (10, 8));
    for (juce::Component* c : { (juce::Component*) &wave, (juce::Component*) &filterView, (juce::Component*) &chainView,
                                (juce::Component*) &modView, (juce::Component*) &sceneView })
        c->setVisible (false);
    for (auto* b : { &prevButton, &nextButton, &playButton })
        b->setVisible (mode == 0);
    tuneNote->setVisible (false);
    tuneCents->setVisible (false);
    {
        auto w = waveCard.withTrimmedTop (26).reduced (10, 8);
        if (mode == 0)
        {
            auto bar = w.removeFromBottom (34);
            w.removeFromBottom (6);
            wave.setBounds (w);
            wave.setVisible (true);
            prevButton.setBounds (bar.removeFromLeft (44));
            bar.removeFromLeft (6);
            nextButton.setBounds (bar.removeFromLeft (44));
            bar.removeFromLeft (6);
            playButton.setBounds (bar.removeFromLeft (120));
            bar.removeFromLeft (16);
            const bool tunable = shown != nullptr && selected >= 0 && selected < (int) shown->samples.size();
            tuneNote->setBounds (bar.removeFromLeft (bar.getWidth() / 2 - 6).withSizeKeepingCentre (bar.getWidth() / 2 - 6, 26));
            bar.removeFromLeft (12);
            tuneCents->setBounds (bar.withSizeKeepingCentre (bar.getWidth(), 26));
            tuneNote->setVisible (tunable);
            tuneCents->setVisible (tunable);
        }
        else
        {
            juce::Component* c = &wave;
            switch (page[1])
            {
                case 2: c = &filterView; break;
                case 3: c = &chainView; break;
                case 4: c = &chainView; break;
                case 5: c = &modView; break;
                case 6: c = &sceneView; break;
                default: break;
            }
            chainView.showSpectrum = page[1] == 3;
            c->setBounds (w);
            c->setVisible (true);
        }
    }

    // bottom
    {
        auto b = bottom.reduced (12, 10);
        meterArea = b.removeFromRight (190);
        b.removeFromRight (14);
        clockStrip.setBounds (b.removeFromRight (206));
        b.removeFromRight (14);
        sampleButton.setVisible (mode == 0);
        testButton.setVisible (mode == 0);
        strip.setVisible (mode == 1);
        if (mode == 0)
        {
            sampleButton.setBounds (b.removeFromLeft (190));
            b.removeFromLeft (10);
            testButton.setBounds (b.removeFromLeft (112).withSizeKeepingCentre (112, 40));
            b.removeFromLeft (14);
        }
        else
        {
            strip.setBounds (b.removeFromLeft (juce::jmin (b.getWidth() - 160, 4 * 64 + 10 + 420)));
            b.removeFromLeft (14);
        }
        statusArea = b;
    }
    updatePanel.setBounds (getLocalBounds());
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (px::black);

    // header
    g.setColour (px::panel);
    g.fillRect (header);
    g.setColour (px::frame);
    g.fillRect (header.removeFromBottom (2));
    const auto t = juce::Time::getMillisecondCounter();
    px::mascot (g, { 14, 17 }, 3, (t / 160) % 23 == 0, status.sampling, (int) (t / 90));
    px::drawText (g, "REPLIKANT", { 86, 8, 260, 28 }, juce::Justification::centredLeft, px::label (20.0f, true), px::white);
    px::drawText (g, "AUTOSAMPLEUR  " + Updater::currentVersion(), { 88, 34, 260, 16 }, juce::Justification::centredLeft, px::label (8.0f), px::dim);
    px::drawText (g, "NOM", nameEditor.getBounds().translated (-50, 0).withWidth (44), juce::Justification::centredRight, px::label (10.0f), px::grey);

    // the cards of this page
    for (auto& gr : groups)
        if (gr.mode == mode && gr.page == page[mode])
        {
            auto band = pageColour (mode, page[mode]);
            auto title = gr.title;
            if (gr.toggle >= 0)
            {
                const bool on = proc.player.get (gr.toggle) > 0.5f;
                if (! on)
                    band = px::faint;
                title << (on ? "   · ON" : "   · OFF (CLIQUE ICI)");
            }
            if (mode == 1 && page[1] == 5 && gr.title == "Modulateur")
                title = "M" + juce::String (modShown + 1) + "  " + juce::String::fromUTF8 (snd::text (snd::modParam (modShown, snd::mType),
                                                                                                        proc.player.get (snd::modParam (modShown, snd::mType))).c_str());
            ui::drawCard (g, gr.bounds, title, band);
            if (mode == 1 && page[1] == 5 && gr.title == "Modulateur")
                px::modSign (g, modShown, { gr.band.getRight() - 14, gr.band.getCentreY() }, 10, px::mod[modShown]);
        }
    int lowest = left.getY();
    for (auto& gr : groups)
        if (gr.mode == mode && gr.page == page[mode])
            lowest = juce::jmax (lowest, gr.bounds.getBottom());
    const bool room = lowest < left.getBottom() - 24;
    if (! room)
        ;
    else if (mode == 0 && page[0] == 2)
        px::drawText (g, "S'APPLIQUE À L'INSTRUMENT ACTUEL"_fr, left.withTrimmedBottom (2).removeFromBottom (18), juce::Justification::centredLeft,
                      px::label (8.0f), px::dim);
    else if (mode == 0 && page[0] == 1)
        px::drawText (g, "POUR LA PROCHAINE SESSION (L'ACCORDAGE ET LES FICHIERS : TOUT DE SUITE)"_fr, left.withTrimmedBottom (2).removeFromBottom (18),
                      juce::Justification::centredLeft, px::label (8.0f), px::dim);
    else if (mode == 1 && page[1] == 6)
    {
        auto help = juce::Rectangle<int> (left.getX() + 4, lowest + 14, left.getWidth() - 8, left.getBottom() - lowest - 20);
        const char* lines[] = {
            "COMME SUR L'OCTATRACK :",
            "1. CLIQUE UNE SCÈNE (À DROITE) OU A / B (EN BAS).",
            "2. TOURNE LES RÉGLAGES DE N'IMPORTE QUELLE PAGE :",
            "   ILS SE VERROUILLENT DANS LA SCÈNE (EN VIOLET).",
            "3. RECLIQUE-LA, OU ÉCHAP.",
            "LE FADER VA DE A À B. UN RÉGLAGE VERROUILLÉ D'UN",
            "SEUL CÔTÉ PART DE SA PROPRE VALEUR.",
            "CLIC DROIT SUR UNE SCÈNE : A, B, COPIER, VIDER.",
        };
        for (auto* l : lines)
        {
            px::drawText (g, juce::String::fromUTF8 (l), help.removeFromTop (20), juce::Justification::centredLeft, px::label (8.0f), px::grey);
        }
    }
    else if (mode == 1)
        px::drawText (g, "CLIQUE M1..M4 OU UNE SCÈNE EN BAS, PUIS TOURNE LES RÉGLAGES : ILS SE MODULENT, OU SE VERROUILLENT"_fr,
                      left.withTrimmedBottom (2).removeFromBottom (18), juce::Justification::centredLeft, px::label (7.0f), px::dim);

    // the map and the card under it
    auto ins = shown;
    juce::String mapTitle = "Clavier";
    if (ins != nullptr && ! status.sampling)
        mapTitle << "  ·  "_fr << ins->name << "  ·  "_fr << (int) ins->samples.size() << " samples"
                 << (ins->keys.empty() ? juce::String() : "  ·  "_fr + juce::String ((int) ins->keys.size()) + " touches rendues")
                 << "  ·  "_fr << megabytes (ins->memoryBytes());
    ui::drawCard (g, mapCard, mapTitle, px::accent);
    juce::String cardTitle;
    if (mode == 0)
        cardTitle = "Sample" + (wave.current() != nullptr ? "  ·  "_fr + wave.info() : juce::String());
    else
    {
        const char* titles[] = { "Le sample joué", "Le sample joué : la lecture, ses grains, sa boucle", "Filtre et drive",
                                 "La chaîne et le spectre", "La chaîne et la sortie", "Les modulateurs", "Les scènes" };
        cardTitle = juce::String::fromUTF8 (titles[page[1]]);
        if (page[1] <= 1 && wave.current() != nullptr)
            cardTitle << "  ·  "_fr << wave.info();
    }
    ui::drawCard (g, waveCard, cardTitle, mode == 0 ? px::accent : pageColour (1, page[1]));

    // bottom bar
    g.setColour (px::panel);
    g.fillRect (bottom);
    g.setColour (px::frame);
    g.fillRect (bottom.withHeight (2));

    auto st = statusArea;
    const bool sampling = status.sampling;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    juce::String line;
    if (sampling)
        line = status.text;
    else if (status.busy && status.text.isNotEmpty())
        line = status.text;
    else if (mode == 0)
    {
        const auto jobs = rk::planJobs (proc.session);
        const auto notes = rk::plannedNotes (proc.session);
        const auto vels = rk::plannedVelocities (proc.session);
        line << (int) notes.size() << " NOTES × "_fr << (int) vels.size() << (vels.size() > 1 ? " VÉLOCITÉS"_fr : " VÉLOCITÉ"_fr);
        if (proc.session.roundRobins > 1)
            line << " × "_fr << proc.session.roundRobins << " PRISES";
        line << " = " << (int) jobs.size() << " SAMPLES  ·  "_fr << ui::duration (rk::estimatedSeconds (proc.session));
    }
    else
        line = proc.player.activeVoices.load() > 0 ? juce::String (proc.player.activeVoices.load()) + " VOIX" : juce::String ("PRÊT"_fr);
    auto top = st.removeFromTop (26);
    px::drawText (g, upper (line), top, juce::Justification::centredLeft, px::text (mode == 0 ? 24.0f : 22.0f), sampling ? px::red : px::white);
    if (sampling && status.secondsLeft > 0)
        px::drawText (g, "RESTE " + ui::duration (status.secondsLeft), top, juce::Justification::centredRight, px::text (24.0f), px::grey);
    st.removeFromTop (4);
    if (mode == 0)
    {
        auto barArea = st.removeFromTop (14);
        if (status.progress >= 0.0f)
        {
            g.setColour (px::cell);
            g.fillRect (barArea);
            px::frameRect (g, barArea, px::frame, 2);
            px::dither (g, barArea.reduced (3).withWidth (juce::roundToInt ((float) (barArea.getWidth() - 6) * juce::jlimit (0.0f, 1.0f, status.progress))),
                        sampling ? px::red : px::cyan, 3);
            if (sampling)
                px::drawText (g, juce::String (status.done) + " / " + juce::String (status.total), barArea.translated (0, 14).withHeight (16),
                              juce::Justification::centredRight, px::text (18.0f), px::dim);
        }
        else if (! status.busy)
        {
            // what it will weigh, from the settings, live
            const int channels = proc.options.source == ReplikantProcessor::Source::testSynth ? 2 : juce::jmax (1, juce::jmin (2, proc.getTotalNumInputChannels()));
            const auto e = rk::estimateSize (proc.session, proc.currentSampleRate(), channels);
            juce::String w;
            w << "POIDS ≈ "_fr << megabytes (e.diskBytes) << " SUR LE DISQUE  ·  "_fr << megabytes (e.memoryBytes) << " EN MÉMOIRE"_fr;
            if (e.renderedKeys > 0)
                w << "  (" << e.renderedKeys << " TOUCHES RENDUES)";
            px::drawText (g, w, barArea.withHeight (16), juce::Justification::centredLeft, px::label (8.0f), px::grey);
        }
        st.removeFromTop (4);
    }
    juce::String low;
    juce::Colour lowColour = px::yellow;
    if (message.isNotEmpty() && now < messageUntil)
    {
        low = message;
        lowColour = messageIsWarning ? px::yellow : px::white;
    }
    else if (status.warning.isNotEmpty())
        low = status.warning;
    else if (auto m = proc.midiPortStatus(); m.isNotEmpty() && ! sampling && mode == 0)
        low = m;
    else if (mode == 0 && status.latency >= 0 && proc.currentSampleRate() > 0)
    {
        low = "Latence mesurée "_fr + juce::String (status.latency * 1000.0 / proc.currentSampleRate(), 1) + " ms";
        if (status.floorDb > -199.0f)
            low << "  ·  bruit de fond "_fr << juce::String (status.floorDb, 1) << " dB";
        lowColour = px::dim;
    }
    else if (mode == 0)
    {
        const double rate = proc.currentSampleRate();
        low << "Enregistré à "_fr << juce::String (rate / 1000.0, std::fmod (rate, 1000.0) < 1.0 ? 0 : 1) << " kHz, "
            << proc.session.fileBits << (proc.session.fileBits == 32 ? " bits flottants" : " bits");
        if (rate < 88000.0)
            low << "  ·  96 kHz conseillé pour transposer très bas (dans la carte son ou le projet)"_fr;
        lowColour = px::dim;
    }
    else if (proc.options.monitor == 2)
    {
        low = "MULTI-EFFET : L'ENTRÉE PASSE DANS LE FILTRE ET LES EFFETS"_fr;
        lowColour = px::cyan;
    }
    px::drawText (g, low, st.withHeight (22), juce::Justification::centredLeft, px::text (21.0f), lowColour);

    // level meter (dBFS from -72 to 0): the input while sampling, the player's output when playing
    {
        auto m = meterArea;
        const bool showOut = mode == 1 && ! sampling;
        px::drawText (g, showOut ? "SORTIE" : "ENTRÉE"_fr, m.removeFromTop (16), juce::Justification::centredLeft, px::label (9.0f), px::grey);
        const float lvl = showOut ? proc.player.outLevel.load() : proc.inputLevel();
        const float dbv = juce::Decibels::gainToDecibels (lvl, -100.0f);
        px::drawText (g, lvl > 1.0e-5f ? juce::String (dbv, 1) + " DB" : juce::String ("-INF"), m.withHeight (16).translated (0, -16),
                      juce::Justification::centredRight, px::text (20.0f), dbv > -1.0f ? px::red : px::white);
        auto bar = m.removeFromTop (18);
        const int blocks = 22;
        const int filled = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (dbv + 72.0f) / 72.0f) * blocks);
        const int floorBlock = ! showOut && status.floorDb > -199.0f ? juce::roundToInt (juce::jlimit (0.0f, 1.0f, (status.floorDb + 72.0f) / 72.0f) * blocks) : -1;
        px::bar (g, bar, blocks, filled, dbv > -3.0f ? px::red : (dbv > -12.0f ? px::yellow : px::cyan), floorBlock);
        m.removeFromTop (6);
        const auto ph = proc.phase();
        const juce::String phases[] = { "", "PROGRAMME", "BRUIT DE FOND", "PAUSE", "NOTE TENUE", "RELÂCHE"_fr };
        if (sampling)
            px::drawText (g, phases[juce::jlimit (0, 5, (int) ph)], m.withHeight (20), juce::Justification::centredLeft, px::label (9.0f), px::red);
        else if (mode == 0 && proc.player.activeVoices.load() > 0)
            px::drawText (g, juce::String (proc.player.activeVoices.load()) + " VOIX", m.withHeight (20), juce::Justification::centredLeft, px::label (9.0f), px::grey);
    }
}

void MainView::paintOverChildren (juce::Graphics& g)
{
    // armed: the band over the page tabs says what turning the rows does now
    if (arm.any() && mode == 1)
    {
        const auto c = arm.mod >= 0 ? px::mod[arm.mod] : px::violet;
        g.setColour (c);
        g.fillRect (banner);
        juce::String t = arm.mod >= 0 ? "M" + juce::String (arm.mod + 1) + " : TOURNE LES RÉGLAGES À MODULER · CLIC : INVERSE · DOUBLE : RETIRE"_fr
                                      : "SCÈNE "_fr + juce::String (arm.scene + 1) + " : TOURNE LES RÉGLAGES À VERROUILLER · DOUBLE-CLIC : RETIRE"_fr;
        px::drawText (g, t, banner.reduced (10, 0).withTrimmedRight (60), juce::Justification::centredLeft, px::label (8.0f, true), px::ink);
        px::drawText (g, "OK", banner.removeFromRight (50), juce::Justification::centred, px::label (11.0f, true), px::ink);
        if (arm.mod >= 0)
            px::modSign (g, arm.mod, { banner.getRight() - 64, banner.getCentreY() }, 10, px::ink);
    }
    // a file dragged over the window
    if (dropping)
    {
        auto r = getLocalBounds().reduced (40);
        px::dither (g, getLocalBounds(), px::black, 3);
        g.setColour (px::cyan);
        px::frameRect (g, r, px::cyan, 4);
        px::drawText (g, "DÉPOSE POUR IMPORTER"_fr, r.withSizeKeepingCentre (r.getWidth(), 40), juce::Justification::centred, px::label (20.0f, true), px::cyan);
        px::drawText (g, "UN PACK (ZIP), UN .REPLIKANT OU UN DOSSIER D'INSTRUMENT", r.withSizeKeepingCentre (r.getWidth(), 40).translated (0, 40),
                      juce::Justification::centred, px::label (10.0f), px::grey);
    }
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    // the band of an effect's card turns it on and off; the armed band ends the arming
    if (arm.any() && banner.contains (e.getPosition()))
    {
        arm.mod = arm.scene = -1;
        armChanged();
        return;
    }
    for (auto& gr : groups)
        if (gr.mode == mode && gr.page == page[mode] && gr.toggle >= 0 && gr.band.contains (e.getPosition()))
        {
            ui::setSound (proc, gr.toggle, proc.player.get (gr.toggle) > 0.5f ? 0.0f : 1.0f);
            resized();
            repaint();
            return;
        }
}

bool MainView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && arm.any())
    {
        arm.mod = arm.scene = -1;
        armChanged();
        return true;
    }
    return false;
}

void MainView::armChanged()
{
    strip.repaint();
    sceneView.repaint();
    modView.repaint();
    for (auto& r : rows)
        if (r->isVisible())
            r->repaint();
    repaint();
}

//============================================================================== drag and drop
bool MainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (instrumentio::isImportable (juce::File (f)))
            return ! proc.isSampling();
    return false;
}

void MainView::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    repaint();
    for (auto& f : files)
        if (instrumentio::isImportable (juce::File (f)))
        {
            proc.importAny (juce::File (f));
            selected = -1;
            return;
        }
}

juce::String MainView::packSignature() const
{
    auto ins = proc.getInstrument();
    if (ins == nullptr)
        return {};
    return juce::String ((juce::pointer_sized_int) ins.get()) + "|" + ins->name + "|" + proc.soundSignature();
}

void MainView::PackTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const bool fresh = ready.existsAsFile() && readyFor == view.packSignature();
    const bool on = view.proc.getInstrument() != nullptr && ! view.proc.isSampling();
    px::button (g, r, fresh, false, fresh ? px::cyan : px::white);
    const auto text = making ? juce::String ("PACK...") : fresh ? juce::String ("GLISSE-MOI") : juce::String ("PACK ZIP");
    px::drawText (g, text, r.reduced (6, 0), juce::Justification::centred, px::label (10.0f, true), ! on ? px::faint : fresh ? px::ink : px::white);
}

void MainView::PackTile::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
    if (making || view.proc.getInstrument() == nullptr || view.proc.isSampling())
        return;
    if (ready.existsAsFile() && readyFor == view.packSignature())
        return;   // ready: a drag takes it out
    making = true;
    repaint();
    const auto signature = view.packSignature();
    juce::Component::SafePointer<PackTile> safe (this);
    view.proc.buildPack ([safe, signature] (const juce::File& zip)
    {
        if (safe == nullptr)
            return;
        safe->making = false;
        safe->ready = zip;
        safe->readyFor = signature;
        if (zip.existsAsFile())
            safe->view.showMessage ("Pack prêt : glisse-le vers le Finder, AirDrop ou un mail (il est aussi dans "_fr + zip.getParentDirectory().getFullPathName() + ")");
        safe->repaint();
    });
}

void MainView::PackTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6 || ! ready.existsAsFile() || readyFor != view.packSignature())
        return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ ready.getFullPathName() }, false, this);
}

//==============================================================================
void MainView::timerCallback()
{
    status = proc.status();
    const bool sampling = proc.isSampling();
    auto ins = proc.getInstrument();
    if (ins != shown)
    {
        shown = ins;
        if (shown == nullptr || selected >= (int) shown->samples.size())
            selected = -1;
        if (! sampling && shown != nullptr && selected < 0 && ! shown->samples.empty())
            selected = 0;
        selectSample (selected);
        resized();
        repaint();
    }
    keymap.refresh (shown, status, sampling, sampling ? proc.currentJob() : -1);
    if (sampling)
    {
        if (status.last != nullptr && status.last != wave.current())
            wave.show (status.last, -1);
    }
    else if (mode == 1 && shown != nullptr)
    {
        // the player: the sample the newest voice plays, its grains, its loop as moved
        const int playing = proc.player.shownSample.load();
        if (playing >= 0 && playing < (int) shown->samples.size() && playing != wave.currentIndex())
            wave.show (std::shared_ptr<const rk::Sample> (shown, &shown->samples[(size_t) playing]), playing);
        wave.setPlayhead (playing >= 0 && playing == wave.currentIndex() ? proc.player.shownPosition.load() : -1.0f);
        std::array<float, 6> gr;
        for (int i = 0; i < 6; ++i)
            gr[(size_t) i] = playing >= 0 && playing == wave.currentIndex() ? proc.player.grainAt[(size_t) i].load() : -1.0f;
        int ls = -1, le = -1;
        if (auto cur = wave.current(); cur != nullptr && cur->loop.valid())
        {
            const float size = proc.player.get (snd::loopSize), shift = proc.player.get (snd::loopStart);
            if (size < 0.999f || std::abs (shift) > 0.001f)
            {
                const int len = cur->length(), L = juce::jlimit (64, juce::jmax (64, len - 1), (int) std::lround (cur->loop.length() * size));
                const int room = juce::jmax (0, len - 1 - L);
                ls = juce::jlimit (0, room, cur->loop.start + (int) std::lround (shift > 0 ? shift * (room - cur->loop.start) : shift * cur->loop.start));
                le = ls + L;
            }
        }
        wave.setPlayerMarks (gr, ls, le);
    }
    else if (shown != nullptr && wave.currentIndex() >= 0 && proc.player.shownSample.load() == wave.currentIndex())
        wave.setPlayhead (proc.player.shownPosition.load());
    else
        wave.setPlayhead (-1.0f);

    sampleButton.setButtonText (sampling ? "STOP" : "SAMPLER");
    for (auto* b : { &newButton, &openButton, &exportButton, &testButton })
        b->setEnabled (! sampling);
    exportButton.setEnabled (! sampling && shown != nullptr && ! shown->samples.empty());
    nameEditor.setReadOnly (sampling);

    static int tick = 0;
    ++tick;
    if (tick % 60 == 0 && ! sampling)
        refreshMidiChoices();
    if (mode == 1)
    {
        // what moves: the rows' modulation dots, the lanes, the strip, the visuals
        for (auto& r : rows)
            if (r->isVisible() && r->param >= 0)
                r->repaint();
        strip.repaint();
        if (modView.isVisible()) modView.refresh();
        if (filterView.isVisible()) filterView.refresh();
        if (chainView.isVisible() && tick % 2 == 0) chainView.refresh();
        if (sceneView.isVisible() && tick % 6 == 0) sceneView.refresh();
        // a row shown or hidden by a choice (a type, a model): the layout follows
        static juce::String lastLayout;
        juce::String layoutKey;
        for (auto& gr : groups)
            if (gr.mode == 1 && gr.page == page[1])
                for (auto* r : gr.rows)
                    layoutKey << (r->isShown() ? '1' : '0');
        if (layoutKey != lastLayout)
        {
            lastLayout = layoutKey;
            resized();
            repaint (left);
        }
    }
    else if (tick % 10 == 0)
        for (auto& r : rows)
            if (r->isVisible())
                r->repaint();
    clockStrip.repaint();
    packTile.repaint();
    repaint (bottom);
    repaint (header.withWidth (84));
    maybeShowUpdate();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster* b)
{
    if (b == &proc.updater.changes)
    {
        refreshUpdate();
        maybeShowUpdate();
        return;
    }
    refreshAll();
}

void MainView::refreshAll()
{
    if (nameEditor.getText() != proc.instrumentName && ! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (proc.instrumentName, false);
    for (auto& r : rows)
        r->repaint();
    repaint();
}

void MainView::selectSample (int i)
{
    selected = i;
    tuneNoteValue = -1;
    keymap.setSelected (i);
    if (shown != nullptr && i >= 0 && i < (int) shown->samples.size())
    {
        wave.show (std::shared_ptr<const rk::Sample> (shown, &shown->samples[(size_t) i]), i);
        tuneCentsValue = shown->samples[(size_t) i].detuneCents;
    }
    else if (! proc.isSampling())
        wave.show (nullptr, -1);
    const bool tunable = mode == 0 && shown != nullptr && i >= 0 && i < (int) shown->samples.size();
    tuneNote->setVisible (tunable);
    tuneCents->setVisible (tunable);
    tuneNote->repaint();
    tuneCents->repaint();
    repaint (waveCard);
}

void MainView::step (int dir)
{
    if (shown == nullptr || shown->samples.empty())
        return;
    const int n = (int) shown->samples.size();
    selectSample (((selected < 0 ? 0 : selected) + dir + n) % n);
}

void MainView::playShown()
{
    if (shown == nullptr || selected < 0 || selected >= (int) shown->samples.size())
        return;
    const auto& s = shown->samples[(size_t) selected];
    const int note = s.root, vel = s.velocity;
    proc.audition (note, vel, true);
    const int ms = juce::jlimit (200, 8000, (int) ((s.noteOff > 0 ? s.noteOff : s.length()) * 1000.0 / s.sampleRate));
    juce::Component::SafePointer<MainView> safe (this);
    juce::Timer::callAfterDelay (ms, [safe, note] { if (safe != nullptr) safe->proc.audition (note, 0, false); });
}

void MainView::toggleSampling()
{
    if (proc.isSampling())
    {
        proc.stopSampling();
        return;
    }
    if (proc.options.source != ReplikantProcessor::Source::testSynth && proc.options.monitor == 0)
        showMessage ("L'écoute est coupée : tu n'entendras pas le synthé pendant la session."_fr, true);
    juce::String why;
    if (! proc.startSampling (why))
        showMessage (why, true);
}

void MainView::showMessage (const juce::String& m, bool warning)
{
    message = m;
    messageIsWarning = warning;
    messageUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 7.0;
    repaint (bottom);
}

void MainView::openInstrument()
{
    chooser = std::make_unique<juce::FileChooser> ("Ouvrir un instrument REPLIKANT, ou un pack (zip)"_fr, proc.instrumentsRoot(), "*.replikant;*.zip");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() || f.isDirectory())
        {
            proc.importAny (f);
            selected = -1;
        }
    });
}

void MainView::showExportMenu()
{
    auto ins = proc.getInstrument();
    if (ins == nullptr)
        return;
    juce::PopupMenu m;
    m.addSectionHeader ("Pack à partager (zip : l'instrument, ses samples et son son)"_fr);
    m.addItem (5, "Enregistrer le pack sous..."_fr);
    m.addSectionHeader ("Dossier de WAV (note, boucle et zone dans chaque fichier)");
    m.addItem (1, "Les samples enregistrés"_fr);
    m.addItem (2, "Toutes les touches (rendu REPLIKANT)"_fr);
    m.addSectionHeader ("Bitwig .multisample");
    m.addItem (3, "Les samples enregistrés"_fr);
    m.addItem (4, "Toutes les touches (rendu REPLIKANT)"_fr);
    m.addSeparator();
    m.addItem (6, "Ouvrir le dossier de l'instrument"_fr);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportButton), [this, ins] (int r)
    {
        if (r <= 0)
            return;
        const auto start = ins->folder.isDirectory() ? ins->folder.getParentDirectory() : proc.instrumentsRoot();
        if (r == 6)
        {
            auto f = ins->folder.isDirectory() ? ins->folder : proc.instrumentsRoot();
            f.createDirectory();
            f.revealToUser();
            return;
        }
        if (r == 5)
        {
            chooser = std::make_unique<juce::FileChooser> ("Enregistrer le pack"_fr, start.getChildFile (instrumentio::safeName (ins->name) + ".zip"), "*.zip");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, ins] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                if (! f.hasFileExtension ("zip"))
                    f = f.withFileExtension ("zip");
                proc.buildPack ([this, f] (const juce::File& zip)
                {
                    if (zip.existsAsFile() && (zip == f || zip.copyFileTo (f)))
                    {
                        showMessage ("Pack enregistré : "_fr + f.getFullPathName());
                        f.revealToUser();
                    }
                });
            });
            return;
        }
        const bool bitwig = r >= 3, all = r == 2 || r == 4;
        const auto base = instrumentio::safeName (ins->name) + (all ? " (touches)" : "");
        if (bitwig)
        {
            chooser = std::make_unique<juce::FileChooser> ("Exporter en .multisample", start.getChildFile (base + ".multisample"), "*.multisample");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, all] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                if (! f.hasFileExtension ("multisample"))
                    f = f.withFileExtension ("multisample");
                proc.exportInstrument (true, all, f);
            });
        }
        else
        {
            chooser = std::make_unique<juce::FileChooser> ("Choisis où créer le dossier de WAV"_fr, start);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, all, base] (const juce::FileChooser& fc)
            {
                const auto dir = fc.getResult();
                if (dir == juce::File())
                    return;
                auto target = dir.getChildFile (base + " WAV");
                for (int n = 2; target.exists(); ++n)
                    target = dir.getChildFile (base + " WAV " + juce::String (n));
                proc.exportInstrument (false, all, target);
            });
        }
    });
}

void MainView::showZoomMenu()
{
    juce::PopupMenu m;
    int id = 1;
    for (float z : ui::zoomSteps)
        m.addItem (id++, juce::String (juce::roundToInt (z * 100)) + " %", true, std::abs (z - zoom) < 0.01f);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&zoomButton), [this] (int r)
    {
        if (r <= 0)
            return;
        zoom = ui::zoomSteps[r - 1];
        zoomButton.setButtonText (juce::String (juce::roundToInt (zoom * 100)) + " %");
        proc.settingsFile().setValue ("zoom", (double) zoom);
        proc.settingsFile().saveIfNeeded();
        if (onZoom)
            onZoom (zoom);
    });
}

void MainView::refreshUpdate()
{
    auto& up = proc.updater;
    const bool show = up.getState() == Updater::State::Available || up.getState() == Updater::State::Installing
                   || up.getState() == Updater::State::Installed;
    updateButton.setButtonText (up.getState() == Updater::State::Installed ? "Redémarrer"_fr : "Mise à jour"_fr);
    ui::styleBig (updateButton, up.getState() == Updater::State::Available);
    updateButton.setTooltip (up.getStatusText());
    if (updateButton.isVisible() != show)
    {
        updateButton.setVisible (show);
        resized();
    }
}

void MainView::maybeShowUpdate()
{
    static juce::String shownUpdate;
    static bool shownNews = false;
    if (updatePanel.isVisible() || ! isShowing())
        return;
    if (pendingNews.has_value())
    {
        if (! std::exchange (shownNews, true))
            updatePanel.showNews (*pendingNews);
        pendingNews.reset();
        return;
    }
    auto& up = proc.updater;
    if (up.getState() == Updater::State::Available && up.getRelease().version != shownUpdate)
    {
        shownUpdate = up.getRelease().version;
        updatePanel.showAvailable (up.getNews (3), (int) up.getNewer().size());
    }
}
