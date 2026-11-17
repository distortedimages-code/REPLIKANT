#include "MainView.h"
#include "Pixel.h"
#include "Text.h"

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

    bool standalone (const juce::AudioProcessor& p)
    {
        return p.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    }
}

MainView::MainView (ReplikantProcessor& p) : proc (p)
{
    setSize (ui::designWidth, ui::designHeight);
    setWantsKeyboardFocus (true);

    const char* pages[3] = { "ENREGISTREMENT", "TRAITEMENT", "TRANSPOSITION" };
    for (int i = 0; i < 3; ++i)
    {
        pageButtons[i].setButtonText (pages[i]);
        pageButtons[i].setClickingTogglesState (false);
        pageButtons[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (pageButtons[i]);
    }
    buildRows();

    nameEditor.setText (proc.instrumentName, false);
    nameEditor.setFont (px::text (26.0f));
    nameEditor.setIndents (8, 4);
    nameEditor.onTextChange = [this] { proc.instrumentName = nameEditor.getText(); };
    nameEditor.onReturnKey = [] { juce::Component::unfocusAllComponents(); };
    nameEditor.setTooltip ("Le nom de l'instrument : celui de son dossier et de ses fichiers."_fr);
    addAndMakeVisible (nameEditor);

    for (auto* b : { &newButton, &openButton, &exportButton, &folderButton, &audioButton, &zoomButton, &updateButton })
        addAndMakeVisible (b);
    audioButton.setVisible (standalone (proc));
    updateButton.setVisible (false);
    newButton.onClick = [this] { proc.newInstrument(); selectSample (-1); };
    openButton.onClick = [this] { openInstrument(); };
    exportButton.onClick = [this] { showExportMenu(); };
    folderButton.onClick = [this]
    {
        auto ins = proc.getInstrument();
        auto f = ins != nullptr && ins->folder.isDirectory() ? ins->folder : proc.instrumentsRoot();
        f.createDirectory();
        f.revealToUser();
    };
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
    exportButton.setTooltip ("Exporter en dossier de WAV (Logic, Ableton, Kontakt...) ou en .multisample Bitwig."_fr);

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

    addAndMakeVisible (keymap);
    addAndMakeVisible (wave);
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
        const auto now = Updater::currentVersion();
        const auto before = set.getValue ("lastRunVersion");
        if (before.isNotEmpty() && before != now)
            pendingNews = Updater::summarise (now, juce::String::fromUTF8 (RkNotesData::RELEASE_NOTES_md, RkNotesData::RELEASE_NOTES_mdSize));
        if (before != now)
        {
            set.setValue ("lastRunVersion", now);
            set.saveIfNeeded();
        }
    }

    showPage (0);
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

void MainView::buildRows()
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

    groups.reserve (12);
    //------------------------------------------------------------------ recording
    groups.push_back ({ "Source", 0, {}, {} });
    {
        auto& g = groups.back();
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
    groups.push_back ({ "Notes", 0, {}, {} });
    {
        auto& g = groups.back();
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
    groups.push_back ({ "Vélocités"_fr, 0, {}, {} });
    {
        auto& g = groups.back();
        add (g, num ("Couches", 1, 16, 1, [&s] { return (double) s.layers; }, [&s] (double v) { s.layers = (int) v; }, intText));
        add (g, choice ("Répartition"_fr, { "LINÉAIRE"_fr, "DOUCES", "FORTES" }, [&s] { return juce::jmin (2, (int) s.curve); },
                        [&s] (int v) { s.curve = (rk::VelCurve) v; }))
            .setTooltip ("Où mettre les couches : réparties également, plus nombreuses dans le jeu doux, ou dans le fort."_fr);
        add (g, num ("Round robin", 1, 8, 1, [&s] { return (double) s.roundRobins; }, [&s] (double v) { s.roundRobins = (int) v; },
                     [] (double v) { return juce::String ((int) v) + ((int) v > 1 ? " PRISES" : " PRISE"); }))
            .setTooltip ("Plusieurs prises de chaque note, jouées à tour de rôle (vie des synthés analogiques)."_fr);
    }
    groups.push_back ({ "Durées"_fr, 0, {}, {} });
    {
        auto& g = groups.back();
        add (g, choice ("Jeu", { "TENUE", "ONE-SHOT" }, [&s] { return s.oneShot ? 1 : 0; }, [&s] (int v) { s.oneShot = v == 1; }))
            .setTooltip ("TENUE : la note tient tant que la touche est enfoncée (boucle). ONE-SHOT : joue jusqu'au bout (percussions)."_fr);
        add (g, num ("Tenue", 0.05, 30.0, 0.0, [&s] { return s.hold; }, [&s] (double v) { s.hold = v; }, ui::ms, true));
        add (g, num ("Relâche max"_fr, 0.0, 30.0, 0.05, [&s] { return s.releaseMax; }, [&s] (double v) { s.releaseMax = v; }, ui::ms))
            .setTooltip ("La plus longue queue enregistrée après le relâchement ; elle s'arrête avant si le son retombe dans le bruit."_fr);
        add (g, num ("Pause", 0.0, 5.0, 0.05, [&s] { return s.pause; }, [&s] (double v) { s.pause = v; }, ui::ms));
    }
    //------------------------------------------------------------------ processing
    groups.push_back ({ "Boucles", 1, {}, {} });
    {
        auto& g = groups.back();
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
    groups.push_back ({ "Attaque", 1, {}, {} });
    {
        auto& g = groups.back();
        add (g, choice ("Calage", { "PAR NOTE", "LATENCE FIXE" }, [&s] { return (int) s.align; }, [&s] (int v) { s.align = (rk::Align) v; }))
            .setTooltip ("PAR NOTE : chaque sample commence à son attaque. LATENCE FIXE : la même latence mesurée pour tous (garde la forme exacte des attaques lentes)."_fr);
        add (g, num ("Seuil", 3.0, 40.0, 1.0, [&s] { return s.thresholdDb; }, [&s] (double v) { s.thresholdDb = v; },
                     [] (double v) { return "+" + juce::String ((int) v) + " DB"; }))
            .setTooltip ("L'attaque est détectée quand le son dépasse le bruit de fond de tant."_fr);
        add (g, num ("Marge avant", 0.0, 20.0, 0.5, [&s] { return s.preMs; }, [&s] (double v) { s.preMs = v; },
                     [] (double v) { return juce::String (v, 1) + " MS"; }));
    }
    groups.push_back ({ "Niveau et accord", 1, {}, {} });
    {
        auto& g = groups.back();
        add (g, choice ("Normalisation", { "INSTRUMENT", "PAR SAMPLE", "AUCUNE" }, [&s] { return (int) s.normalise; }, [&s] (int v) { s.normalise = (rk::Normalise) v; }))
            .setTooltip ("INSTRUMENT : tout ensemble, les couches de vélocité gardent leurs écarts. PAR SAMPLE : chacun au maximum."_fr);
        auto& t = add (g, num ("Cible", -24.0, 0.0, 0.1, [&s] { return s.normaliseDb; }, [&s] (double v) { s.normaliseDb = v; }, db));
        t.enabled = [&s] { return s.normalise != rk::Normalise::off; };
        add (g, choice ("Retirer le DC", { "OUI", "NON" }, [&s] { return s.removeDc ? 0 : 1; }, [&s] (int v) { s.removeDc = v == 0; }));
        add (g, choice ("Accordage", { "RECALER", "FIN", "NON" }, [&s] { return (int) s.tuning; }, [&s] (int v) { s.tuning = (rk::Tuning) v; }), true)
            .setTooltip ("RECALER : la note vraiment jouée est retrouvée (même à quelques demi-tons, un oscillateur mal réglé) et remise sur sa touche, puis jouée juste. FIN : seulement les cents. NON : les notes telles qu'envoyées."_fr);
    }
    //------------------------------------------------------------------ transposition
    groups.push_back ({ "Transposition", 2, {}, {} });
    {
        auto& g = groups.back();
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
    groups.push_back ({ "Jeu", 2, {}, {} });
    {
        auto& g = groups.back();
        auto param = [this] (const char* id) { return proc.params.getParameter (id); };
        auto paramRow = [&] (const juce::String& name, const char* id, double lo, double hi, double st, std::function<juce::String (double)> f, bool log)
        {
            auto* p = param (id);
            auto* raw = proc.params.getRawParameterValue (id);
            auto r = std::make_unique<ParamRow> (name, lo, hi, st, [raw] { return (double) raw->load(); },
                                                 [p] (double v) { p->setValueNotifyingHost (p->convertTo0to1 ((float) v)); }, std::move (f), log);
            auto& row = add (g, std::move (r));
            row.onChange = [this] { repaint(); };
            row.locked = nullptr;   // playing settings stay free during a session
            return &row;
        };
        paramRow ("Volume", "volume", -36.0, 12.0, 0.1, db, false);
        paramRow ("Attaque", "attack", 0.0, 4000.0, 0.0, [] (double v) { return ui::ms (v / 1000.0); }, false);
        paramRow ("Relâche"_fr, "release", 5.0, 10000.0, 0.0, [] (double v) { return ui::ms (v / 1000.0); }, true);
        paramRow ("Vélocité"_fr, "velocity", 0.0, 1.0, 0.01, pct, false);
        paramRow ("Pitch bend", "bend", 0.0, 12.0, 1.0, semis, false);
        auto& l = add (g, choice ("Boucles", { "OUI", "NON" }, [this] { return proc.player.get (snd::loops) > 0.5f ? 0 : 1; },
                                  [this] (int v) { auto* p = proc.params.getParameter ("loops"); p->setValueNotifyingHost (v == 0 ? 1.0f : 0.0f); }));
        l.locked = nullptr;
        auto& o2 = proc.options;
        add (g, choice ("Clavier vers", { "INSTRUMENT", "SYNTHÉ"_fr }, [&o2] { return o2.thru ? 1 : 0; }, [&o2] (int v) { o2.thru = v == 1; }))
            .setTooltip ("Les notes jouées dans REPLIKANT font sonner l'instrument échantillonné, ou vont au synthé (pour comparer avec l'original)."_fr);
    }
}

void MainView::refreshMidiChoices()
{
    midiDevices = proc.midiOutputs();
    juce::StringArray names { "AUCUNE", "HÔTE (DAW)"_fr };
    for (auto& d : midiDevices)
        names.add (upper (d.name));
    if (midiRow != nullptr)
    {
        // a port remembered but absent stays visible, so it isn't silently replaced
        if (proc.options.midiPort.isNotEmpty() && proc.options.midiPort != "host")
        {
            bool found = false;
            for (auto& d : midiDevices)
                found = found || d.identifier == proc.options.midiPort;
            if (! found)
            {
                midiDevices.add ({ proc.options.midiPortName, proc.options.midiPort });
                names.add (upper (proc.options.midiPortName) + " (ABSENT)");
            }
        }
        midiRow->setChoices (names);
        midiRow->repaint();
    }
}

void MainView::showPage (int p)
{
    page = p;
    for (int i = 0; i < 3; ++i)
        ui::styleBig (pageButtons[i], i == p);
    for (auto& g : groups)
        for (auto* r : g.rows)
            r->setVisible (g.page == p);
    resized();
    repaint();
}

//==============================================================================
void MainView::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (60);
    bottom = r.removeFromBottom (92);
    r.reduce (12, 4);
    left = r.removeFromLeft (440);
    r.removeFromLeft (12);
    mapCard = r.removeFromTop (r.getHeight() * 53 / 100);
    r.removeFromTop (10);
    waveCard = r;

    // header: name on the left of the buttons
    {
        auto h = header.reduced (12, 12);
        auto rightPart = h.removeFromRight (720);
        nameEditor.setBounds (h.removeFromRight (300).withSizeKeepingCentre (300, 36));
        auto place = [&rightPart] (juce::TextButton& b, int w)
        {
            if (! b.isVisible())
                return;
            b.setBounds (rightPart.removeFromRight (w).withSizeKeepingCentre (w, 38));
            rightPart.removeFromRight (8);
        };
        place (zoomButton, 80);
        place (updateButton, 150);
        place (audioButton, 120);
        place (folderButton, 100);
        place (exportButton, 110);
        place (openButton, 96);
        place (newButton, 110);
    }

    // settings: the page buttons, then the cards of the page
    {
        auto l = left;
        auto tabs = l.removeFromTop (38);
        const int w = (tabs.getWidth() - 8) / 3;
        for (int i = 0; i < 3; ++i)
        {
            pageButtons[i].setBounds (tabs.removeFromLeft (w).withHeight (36));
            tabs.removeFromLeft (4);
        }
        l.removeFromTop (8);
        const int rowH = 26;
        for (auto& g : groups)
        {
            if (g.page != page)
                continue;
            const int h = 24 + 10 + (int) g.rows.size() * rowH + 4;
            g.bounds = l.removeFromTop (h);
            l.removeFromTop (8);
            auto in = g.bounds.withTrimmedTop (24).reduced (10, 6);
            for (auto* row : g.rows)
                row->setBounds (in.removeFromTop (rowH).withTrimmedBottom (2));
        }
    }

    // the map and the sample
    keymap.setBounds (mapCard.withTrimmedTop (26).reduced (10, 8));
    {
        auto w = waveCard.withTrimmedTop (26).reduced (10, 8);
        auto bar = w.removeFromBottom (34);
        w.removeFromBottom (6);
        wave.setBounds (w);
        prevButton.setBounds (bar.removeFromLeft (44));
        bar.removeFromLeft (6);
        nextButton.setBounds (bar.removeFromLeft (44));
        bar.removeFromLeft (6);
        playButton.setBounds (bar.removeFromLeft (120));
    }

    // bottom
    {
        auto b = bottom.reduced (12, 10);
        sampleButton.setBounds (b.removeFromLeft (220));
        b.removeFromLeft (10);
        testButton.setBounds (b.removeFromLeft (120).withSizeKeepingCentre (120, 40));
        b.removeFromLeft (14);
        meterArea = b.removeFromRight (260);
        b.removeFromRight (14);
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
    px::mascot (g, { 16, 14 }, 4, (juce::Time::getMillisecondCounter() / 160) % 23 == 0);
    px::drawText (g, "REPLIKANT", { 66, 8, 260, 28 }, juce::Justification::centredLeft, px::label (20.0f, true), px::white);
    px::drawText (g, "AUTOSAMPLEUR  " + Updater::currentVersion(), { 68, 34, 260, 16 }, juce::Justification::centredLeft, px::label (8.0f), px::dim);
    px::drawText (g, "NOM", nameEditor.getBounds().translated (-50, 0).withWidth (44), juce::Justification::centredRight, px::label (10.0f), px::grey);

    // the settings cards of this page
    for (auto& gr : groups)
        if (gr.page == page)
            ui::drawCard (g, gr.bounds, gr.title);
    if (page == 2)
        px::drawText (g, "S'APPLIQUE À L'INSTRUMENT ACTUEL"_fr, left.withTrimmedBottom (2).removeFromBottom (18), juce::Justification::centredLeft,
                      px::label (8.0f), px::dim);
    else if (page == 1)
        px::drawText (g, "POUR LA PROCHAINE SESSION (L'ACCORDAGE : TOUT DE SUITE)"_fr, left.withTrimmedBottom (2).removeFromBottom (18), juce::Justification::centredLeft,
                      px::label (8.0f), px::dim);

    // the map and the sample cards
    auto ins = shown;
    juce::String mapTitle = "Clavier";
    if (ins != nullptr && ! status.sampling)
        mapTitle << "  ·  "_fr << ins->name << "  ·  "_fr << (int) ins->samples.size() << " samples"
                 << (ins->keys.empty() ? juce::String() : "  ·  "_fr + juce::String ((int) ins->keys.size()) + " touches rendues")
                 << "  ·  "_fr << juce::String ((double) ins->memoryBytes() / 1048576.0, 0) << " Mo";
    ui::drawCard (g, mapCard, mapTitle);
    ui::drawCard (g, waveCard, "Sample" + (wave.current() != nullptr ? "  ·  "_fr + wave.info() : juce::String()));

    // bottom bar
    g.setColour (px::panel);
    g.fillRect (bottom);
    g.setColour (px::frame);
    g.fillRect (bottom.withHeight (2));

    auto st = statusArea;
    const bool sampling = status.sampling;
    juce::String line;
    if (sampling)
        line = status.text;
    else if (status.busy && status.text.isNotEmpty())
        line = status.text;
    else
    {
        const auto jobs = rk::planJobs (proc.session);
        const auto notes = rk::plannedNotes (proc.session);
        const auto vels = rk::plannedVelocities (proc.session);
        line << (int) notes.size() << " NOTES × "_fr << (int) vels.size() << (vels.size() > 1 ? " VÉLOCITÉS"_fr : " VÉLOCITÉ"_fr);
        if (proc.session.roundRobins > 1)
            line << " × "_fr << proc.session.roundRobins << " PRISES";
        line << " = " << (int) jobs.size() << " SAMPLES  ·  ENVIRON "_fr << ui::duration (rk::estimatedSeconds (proc.session));
    }
    auto top = st.removeFromTop (26);
    px::drawText (g, upper (line), top, juce::Justification::centredLeft, px::text (26.0f), sampling ? px::red : px::white);
    if (sampling && status.secondsLeft > 0)
        px::drawText (g, "RESTE " + ui::duration (status.secondsLeft), top, juce::Justification::centredRight, px::text (24.0f), px::grey);
    st.removeFromTop (4);
    auto barArea = st.removeFromTop (14);
    if (status.progress >= 0.0f)
    {
        g.setColour (px::cell);
        g.fillRect (barArea);
        px::frameRect (g, barArea, px::frame, 2);
        px::dither (g, barArea.reduced (3).withWidth (juce::roundToInt ((float) (barArea.getWidth() - 6) * juce::jlimit (0.0f, 1.0f, status.progress))),
                    sampling ? px::red : px::white, 3);
        if (sampling)
            px::drawText (g, juce::String (status.done) + " / " + juce::String (status.total), barArea.translated (0, 14).withHeight (16),
                          juce::Justification::centredRight, px::text (18.0f), px::dim);
    }
    st.removeFromTop (4);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    juce::String low;
    juce::Colour lowColour = px::yellow;
    if (message.isNotEmpty() && now < messageUntil)
    {
        low = message;
        lowColour = messageIsWarning ? px::yellow : px::white;
    }
    else if (status.warning.isNotEmpty())
        low = status.warning;
    else if (auto m = proc.midiPortStatus(); m.isNotEmpty() && ! sampling)
        low = m;
    else if (status.latency >= 0 && proc.currentSampleRate() > 0)
    {
        low = "Latence mesurée "_fr + juce::String (status.latency * 1000.0 / proc.currentSampleRate(), 1) + " ms";
        if (status.floorDb > -199.0f)
            low << "  ·  bruit de fond "_fr << juce::String (status.floorDb, 1) << " dB";
        lowColour = px::dim;
    }
    px::drawText (g, low, st.withHeight (22), juce::Justification::centredLeft, px::text (21.0f), lowColour);

    // input meter (dBFS from -72 to 0), the floor marked, red near clipping
    {
        auto m = meterArea;
        px::drawText (g, "ENTREE"_fr, m.removeFromTop (16), juce::Justification::centredLeft, px::label (9.0f), px::grey);
        const float lvl = proc.inputLevel();
        const float dbv = juce::Decibels::gainToDecibels (lvl, -100.0f);
        px::drawText (g, lvl > 1.0e-5f ? juce::String (dbv, 1) + " DB" : juce::String ("-INF"), m.withHeight (16).translated (0, -16),
                      juce::Justification::centredRight, px::text (20.0f), dbv > -1.0f ? px::red : px::white);
        auto bar = m.removeFromTop (18);
        const int blocks = 24;
        const int filled = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (dbv + 72.0f) / 72.0f) * blocks);
        const int floorBlock = status.floorDb > -199.0f ? juce::roundToInt (juce::jlimit (0.0f, 1.0f, (status.floorDb + 72.0f) / 72.0f) * blocks) : -1;
        px::bar (g, bar, blocks, filled, dbv > -3.0f ? px::red : (dbv > -12.0f ? px::yellow : px::white), floorBlock);
        m.removeFromTop (6);
        const auto ph = proc.phase();
        const juce::String phases[] = { "", "PROGRAMME", "BRUIT DE FOND", "PAUSE", "NOTE TENUE", "RELÂCHE"_fr };
        if (sampling)
            px::drawText (g, phases[juce::jlimit (0, 5, (int) ph)], m.withHeight (20), juce::Justification::centredLeft, px::label (9.0f), px::red);
        else if (proc.player.activeVoices.load() > 0)
            px::drawText (g, juce::String (proc.player.activeVoices.load()) + " VOIX", m.withHeight (20), juce::Justification::centredLeft, px::label (9.0f), px::grey);
    }
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
        repaint();
    }
    keymap.refresh (shown, status, sampling, sampling ? proc.currentJob() : -1);
    if (sampling)
    {
        if (status.last != nullptr && status.last != wave.current())
            wave.show (status.last, -1);
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
    if (++tick % 60 == 0 && ! sampling)
        refreshMidiChoices();
    if (tick % 10 == 0)
        for (auto& r : rows)
            if (r->isVisible())
                r->repaint();
    if (status.warning.isNotEmpty() && status.warning != lastWarning)
        lastWarning = status.warning;
    repaint (bottom);
    repaint (header.withWidth (70));
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
    keymap.setSelected (i);
    if (shown != nullptr && i >= 0 && i < (int) shown->samples.size())
        wave.show (std::shared_ptr<const rk::Sample> (shown, &shown->samples[(size_t) i]), i);
    else if (! proc.isSampling())
        wave.show (nullptr, -1);
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
    chooser = std::make_unique<juce::FileChooser> ("Ouvrir un instrument REPLIKANT"_fr, proc.instrumentsRoot(), "*.replikant");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() || f.isDirectory())
        {
            proc.openInstrument (f);
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
    m.addSectionHeader ("Dossier de WAV (note, boucle et zone dans chaque fichier)");
    m.addItem (1, "Les samples enregistrés"_fr);
    m.addItem (2, "Toutes les touches (rendu REPLIKANT)"_fr);
    m.addSectionHeader ("Bitwig .multisample");
    m.addItem (3, "Les samples enregistrés"_fr);
    m.addItem (4, "Toutes les touches (rendu REPLIKANT)"_fr);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportButton), [this, ins] (int r)
    {
        if (r <= 0)
            return;
        const bool bitwig = r >= 3, all = r == 2 || r == 4;
        const auto base = instrumentio::safeName (ins->name) + (all ? " (touches)" : "");
        const auto start = ins->folder.isDirectory() ? ins->folder.getParentDirectory() : proc.instrumentsRoot();
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
