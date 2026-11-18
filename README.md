# REPLIK8

**REPLIK8** (anciennement REPLIKANT) : l'autosampleur de tes synthés, un synthé à cycles et un sampler. Plug-in AU / VST3 et appli pour Mac. Il capture tes synthés (chaque note jouée en MIDI et enregistrée, bouclée, accordée ; ou leurs cycles, pendant qu'un CC balaie un bouton ; ou ce qui sort de tes effets) et en fait des instruments : le SYNTHÉ lit des banques de cycles, le SAMPLER joue un multisample ou n'importe quel son glissé sur la fenêtre. ORBITE (un porteur et deux satellites synchronisés, quatre câblages, une rotation qui fait tourner le timbre) s'y ajoute dans le MIXER. Les deux passent dans des filtres analogiques modélisés, un drive, cinq effets dans l'ordre que tu veux (chaque bloc a son interrupteur), quatre modulateurs et seize scènes. REPLIK8 sert aussi de multi-effet en insert. Le SUB (sinus sur la note, ou le sample relu une ou deux octaves plus bas, crossover, jamais sous 25 Hz) s'y ajoute. La lecture des samples : sinc de 128 points précalculé, coupure qui suit la vitesse (PM comprise), copies à l'octave jusqu'à 1/256, cadran TUNE de ±96 demi-tons. Exports en dossier de WAV, en `.multisample` Bitwig ou en pack zip à partager. La RADIO (0.3.5) écoute une station internet ou un récepteur ondes courtes, avec son air, et la sample : prises découpées à leurs attaques en pads, ou envoyées au sampler. Le DRUM (0.4) : huit pistes (une voix faite des cycles du synthé, un son, ou une prise de la radio et ses tranches) et leur séquenceur de 64 pas.

**Page de téléchargement** : https://distortedimages-code.github.io/REPLIKANT/

## La version 0.4.3 : WTF?! sur le DRUM

- **WTF?!** en mode DRUM : les dix tirages du player (chaque croche, glissés en 30 ms, l'amplitude du bouton comprise) passés au DRUM avant son rendu (un bloc plus tard) ; chaque coup en tire ses réglages en normalisé : TUNE (×0,4), DECAY, SWEEP (×0,6), PMOD, CUTOFF, RESO (×0,7), NOISE, START ou POSITION, DRIVE, F.ENV (×0,8), la piste t prenant le tirage `(j + 3t) mod 10` ; côté player, seuls la coupure et la résonance du filtre principal restent tirées. `Tests/ProcessorTests.cpp` : à 0 les quatre grosses caisses identiques, au bout leurs niveaux écartés.

## La version 0.4.2 : SPECTRE refait

- **SPECTRE** (`dsp::Spectral`) : une FFT d'environ 85 ms à toutes les fréquences d'échantillonnage (4096 points à 44,1 et 48 kHz, 8192 à 88,2 et 96), fenêtre de Blackman-Harris ; TRANSIENTS : le dernier quart dans une fenêtre de Hann, complété de zéros (les mêmes bins), mêlé à la longue entre 700 et 1300 Hz. BLUR : chaque bin suit les analyses avec `α = 1 - exp(-Δt/τ)`, τ de 20 ms à 10 s (Δt : le temps réel entre deux analyses), figé au bout. La bande (ou le notch) avant les pics ; les pics au-dessus de -60 dB du plus fort, interpolés en parabole sur les logarithmes, les plus forts gardés, triés en fréquence. RANK (les oscillateurs qui jouent, dans l'ordre de leurs partiels) ou TRACK (les paires les plus proches en octaves d'abord, sous la demi-octave ; les autres pics à des oscillateurs muets). Un oscillateur qui naît suit son pic sans glisser pendant la durée d'une fenêtre. Les hauteurs : WARP `f' = c·(f/c)^q`, c la moyenne géométrique des pics pondérée par leurs niveaux, q de 0 à 2 ; puis OCTAVE et FINE ; GLIDE en log2, un pas toutes les 8 échantillons réparti sur les 8, τ de 1 ms à 10 s, HOLD. XF : l'ancienne hauteur gardée par un second oscillateur, fondue en puissance constante sur la durée de la slice (un troisième lâché en 2 ms si une slice tombe en plein fondu). Les slices : FREE (6 à 506 ms × 1 à 32), SYNC (sur la position de l'hôte, à l'échantillon ; au tempo quand il est arrêté), NOTES et ATTACKS (deux suiveurs, rapide et lent ; l'analyse 55 ms après, 30 avec TRANSIENTS, que la fenêtre tienne le nouveau son). FEEDBACK : la sortie renvoyée dans l'analyse par un limiteur (montée instantanée, 300 ms de retour, sous 0,5) ; un nombre non fini remet tout à zéro. MIX à puissance constante. `Tests/DspTests.cpp` : hauteurs au cent près de 55 à 440 Hz, bande et notch, BLUR indépendant des slices, gel, les quatre horloges, TRANSIENTS plus rapide, WARP, TRACK contre RANK, formes XF, feedback borné, coût.

## La version 0.4.0 : le drum

- **DRUM** (`Source/drum/DrumCore`), sans JUCE. Huit pistes, deux voix chacune (un coup efface le précédent en 3 ms), 128 patterns (`drum::Pattern` : huit `drum::Track`, une longueur de changement), alloués la première fois que l'onglet s'ouvre (environ 8 Mo).
- **Les voix** : SYNTH lit la banque de cycles des oscillateurs (basique ou capturée) deux octaves sous la note (C3 : 65 Hz), POSITION dans la banque ; SAMPLE et SLICES lisent un son en Hermite (`2^((note-60+tune)/12)`), START, REVERSE, la tranche choisie par SLICE. Puis SWEEP (la hauteur décalée qui retombe en exponentielle en SWEEP TIME), PMOD (modulation de phase par un sinus à RATIO), CLICK, NOISE (coloré par TONE), ATTACK et DECAY (HOLD : jusqu'au bout), un SVF (LP / HP / BP) et son enveloppe (F.ENV : ±6 octaves sur la coupure, F.ATTACK jusqu'à 200 ms, F.DECAY ou HOLD ; le filtre recalculé tous les 8 échantillons), DRIVE (tanh), LEVEL, PAN à puissance constante, CHOKE en quatre groupes (coupe en 5 ms).
- **Le séquenceur** : les pas tirés de la position (ppq) de l'hôte, ou de l'horloge interne quand ▶ tourne : `pas = (ppq - début du pattern) / (0,25 / vitesse)`, placés à l'échantillon. SWING retarde les pas impairs, MICRO décale de ±½ pas, PROB est tirée quand le pas est placé. RATCHET partage le pas en n coups égaux ; RETRIG ajoute COUNT coups à la division choisie (1/1 à 1/128), leurs écarts suivant une raison `q = 2^(e/(n-1))`, `e = ±4 |CURVE|`, mis à l'échelle pour que le dernier tombe où finirait un roulement régulier ; VEL CURVE les éteint ou les fait monter. Chaque pas garde ses verrous (`lock[pas][réglage]`, NaN : celui de la piste).
- **Patterns** : un changement part à la fin du pattern qui joue (AUTO : sa plus longue piste, sinon 1 à 8 mesures), compté depuis son départ ; une chaîne en enchaîne jusqu'à 64.
- **Les sons des pistes** : passés au fil audio comme ceux de la radio (pointeurs atomiques, l'ancien gardé deux secondes) ; un WAV glissé est retrouvé où il est, le reste (une prise, un pad, un autre format) gardé en WAV 24 bits dans `Drums/` à côté des instruments. Le projet garde les patterns, les chaînes, les muets et les sons (`drum` : JSON).
- **Fenêtre** (`Source/ui/DrumUi`) : la grille des huit pistes sur la page de pas montrée (cliquable), le son de la piste ; sous l'écran, les pistes (clic : la choisir et la jouer, clic droit : muette), seize pas (clic : trig, glissé vers le haut : ratchet, maj-clic : lever pour verrouiller), quatre pages, PTN (banques, patterns, CHAIN, COPY, PASTE, CLEAR).
- `Tests/DrumTests.cpp` (pas à l'échantillon, micro, swing, prob, verrous, polymétrie, ratchet, retrig et ses courbes, changements, chaînes, clavier, choke, sons, CPU) ; `Tests/ProcessorTests.cpp` : le DRUM sur le transport d'un hôte, la radio vers le DRUM, le projet relu.

## La version 0.3.5 : la radio

- **RADIO** (`Source/radio/`), un quatrième onglet. Quatre récepteurs (`radio::Bank`) : une station internet (un flux MP3 en HTTP ou HTTPS, ses titres ICY ; l'annuaire libre des stations, filtré sur le MP3) ou un récepteur ondes courtes partagé sur le réseau (sa WebSocket, son IMA ADPCM, AM / USB / LSB / CW, son S-mètre). `RadioNet` s'y connecte, un fil par récepteur, décode (`RadioDec` : minimp3, puis un sinc fenêtré de Blackman à 12 passages par zéro, coupé à 90 % de la plus basse des deux fréquences) en stéréo à 48 kHz et confie le son à la banque.
- **L'horloge** : le fil audio lit la file un peu plus vite ou plus lentement (±0,6 %) pour garder une seconde en main (une demi pour les ondes courtes) ; il attend de l'avoir avant de jouer, et de nouveau après un trou ; ce qu'il lit va dans l'historique du récepteur (21,8 s).
- **L'écoute** (`radio::Engine`) : LIVE (décalage en fréquence par deux chaînes de passe-tout en quadrature, squelch 2 ms / 25 ms), LOOP (la dernière LENGTH reprise à chaque tour avec la probabilité GRAB, calée sur la mesure de l'hôte), DIG (LENGTH prise au hasard dans les DEPTH dernières secondes) ; coutures fondues à deux têtes. L'AIR : bande à quatre pôles, fading avec son encoche, porteuse qui erre, craquements et orage, détecteur saturé, souffle rose. Puis le filtre et la chaîne d'effets, comme une entrée en FX.
- **Les prises** : ce qui est entendu (l'air compris) est gardé 32 s ; GRAB en prend les dernières secondes, REC tout jusqu'à STOP (30 s au plus). Une prise est gardée en WAV 24 bits dans `Radio/` à côté des instruments et rouverte avec le projet.
- **Le découpage** (`RadioSlicer`) : le flux spectral (bandes d'un sixième d'octave, comparées à leurs voisines deux trames plus tôt), un seuil qui suit les alentours, et une montée d'énergie réelle exigée dans le grave, le médium ou l'aigu ; chaque attaque placée à l'échantillon, une milliseconde avant que le son monte. Les pads prennent les plus fortes, dans l'ordre de la prise, chacun jusqu'à l'attaque suivante (4 s au plus).
- **Vers le SAMPLER** : la prise (bouclée) ou un pad devient un instrument sur C3, enregistré avec les autres.
- **Bonnes manières** : seul le récepteur écouté reste connecté (lâché 45 s après) ; une connexion par récepteur ondes courtes, jamais redemandé s'il est plein ou refuse ; erreurs réessayées trois fois (3, 10, 30 s) ; la fréquence change au plus cinq fois par seconde ; le nom REPLIK8 envoyé ; chaque station choisie comptée dans l'annuaire, comme il le demande.

## La version 0.3.0 : synthé, sampler, capture des cycles, rééchantillonnage

- **Modes** : SYNTH (par défaut, il joue dès l'ouverture), SAMPLER, DRUM (0.4), RADIO (0.3.5) ; CAPTURE par-dessus : notes, cycles ou rééchantillonner.
- **SYNTH** (`Source/dsp/Cycles`) : deux oscillateurs de cycles de 2048 points, chaque cycle gardé en 20 copies limitées en bande (une par demi-octave) et lu en Hermite, POSITION qui fond deux cycles voisins ; 64 basiques ou les cycles capturés ; PM de B vers A, sub, bruit, unisson jusqu'à 4 (±30 cents, étalés à puissance constante).
- **SAMPLER** : un son glissé (wav, aiff, flac, mp3 sur Mac) devient un instrument d'un sample sur tout le clavier, en varispeed ; sa note vient du chunk `smpl`, de son nom (« Basse A2.wav ») ou de sa hauteur (McLeod puis YIN). Boucle : fondu en cosinus surélevé de 0 à 100 % de sa moitié.
- **CAPTURE CYCLES** : N prises d'une note grave, un CC envoyé avant chacune (ou la vélocité, ou rien : à la main) ; dans chaque prise, la période trouvée par autocorrélation près de la note (ou du double, quand un sub est là), le cycle pris sur un passage par zéro et rééchantillonné en Lanczos. Banque gardée dans `<Nom>/Cycles/<Nom> - cycles 2048.wav` (32 bits flottants, lu par Serum, Vital, Bitwig).
- **RÉÉCHANTILLONNER** (`Player::resampleChunk`) : la sortie de la chaîne, avant le volume, enregistrée sur 1 à 8 mesures (départ sur la mesure de l'hôte) ou en libre ; la passation se fait en 20 ms à puissance constante. Ensuite, la chaîne rendue neutre (filtre, drive, effets, WTF?! coupés et vidés, voix arrêtées), l'enregistrement y repasse en boucle comme une entrée, identique à l'échantillon près. Sa fin est fondue dans son début avec les 10 ms enregistrées après elle. Injecté dans le SAMPLER, les réglages remis à zéro, puis la chaîne relâchée.
- **Ordre des effets** libre : une permutation des six étages, prise au fond d'un creux de 10 ms.
- **Interface** (`Source/ui/Machine`, `Look`, `Pictos`) : écran noir, pages et banques, huit encodeurs, pupitre, vu-mètres ; pictos dessinés (schémas des filtres, bobines, chaîne, câble de patch…).
- **Lissage** : coupure, profondeur d'enveloppe et pas de lecture en rampes à l'échantillon ; réglages continus glissés (~12 ms) pendant que ça joue.
- **WTF?!** : dix réglages du moteur tirés au sort à chaque croche (LIVE) ou en boucle de huit (LOOP), glissés en 30 ms.

## La version 0.2.0 : le player

- **SAMPLER / PLAYER** : deux modes ; le player a ses pages SON, LECTURE, FILTRE, SPECTRE, ESPACE, MODS, SCÈNES.
- **Voix** : 8, mono ou stéréo, POLY / MONO / LEGATO, glisse, transposition (la touche rendue HQ de la note décalée), ADSR.
- **Lecture** : SAMPLE ou ÉLASTIQUE (grains alignés par corrélation, WSOLA : vitesse et hauteur séparées) ; DÉPART, SENS, boucle déplacée et raccourcie (raccords lissés à puissance constante).
- **Pitch extrême** : sinc de Kaiser à coupure qui suit la transposition ; copies de chaque enregistrement une, deux, trois octaves plus bas (demi-bande de 255 points, ~120 dB) lues dès que la voix monte d'une quinte et demie ; TRANSITOIRES GARDÉS (l'attaque, mesurée jusqu'à ce que l'enveloppe retombe de 9 dB, jouée à sa vitesse, le corps transposé ensuite) ; passe-haut de sécurité à 5 Hz sur les voix.
- **ALIASING** : sinc qui suit la hauteur → sinc qui ne la suit plus → noyau court et doux → linéaire → drop-sample → au-delà, l'interpolation passe-haut (les images plus fortes que le son) ; BITS de 32 à 1, fractionnaires.
- **Modulation de phase** : l'adresse de lecture poussée de ±INDICE échantillons (0 à 256) par un sinus sur la note × RAPPORT, par le sample (une deuxième tête à RAPPORT fois sa vitesse) ou par l'entrée.
- **Filtres** (`Source/dsp/VAFilter`) : MOOG (échelle D'Angelo–Välimäki), MS-20 (Korg 35 en série, SÉPARATION), SEM (OTA), OSCAR (deux 12 dB écartés), WASP (CMOS), POLIVOKS (rails) ; ZDF, suréchantillonnés 1X / 2X / 4X (demi-bandes), réinjection FM, enveloppe, suivi du clavier, vélocité.
- **Drive** : PRÉAMPLI, CONSOLE, BANDE (pré/désaccentuation exactes, bosse de tête, pleurage), suréchantillonné 4X.
- **Effets**, dans cet ordre : SPECTRE (pics d'une FFT d'environ 85 ms rendus par 1 à 33 oscillateurs ; refait en 0.4.2, voir plus haut), PITCH (deux voix Signalsmith, retards, réinjection, CRISTAUX : grains à l'envers), CHORUS / FLANGER / VIBRATO, CRUSH, ESPACE (FDN de 8 lignes de MIFM1, shimmer, gel).
- **Mods** : 4, ONDE / PAS / HASARD / ENV / SUIVEUR, au tempo de l'hôte (sur sa grille) ou au leur, 8 routes chacun ; assignation en tournant le potard.
- **Scènes** : 16, deux sous le fader (A, B) ; un réglage tenu d'un seul côté part de sa propre valeur.
- **Multi-effet** : MONITOR THRU FX, l'entrée (une piste en insert) passe dans le filtre et la chaîne.
- **Horloge MIDI** : 24 tops par noire, Start / Stop ; vers un port du Mac (fil à haute priorité) ou la sortie du plug-in (à l'échantillon), interne ou suivant l'hôte.
- **Pack** : `<Nom>.zip` = `<Nom>/<Nom>.replikant` (avec le son) + `<Nom>/Samples/*.wav` ; glisser hors de la fenêtre (PACK ZIP), glisser un pack sur la fenêtre pour l'importer.
- **Poids estimé** de la session (disque, mémoire), WAV 24 bits ou 32 bits flottants.

## Ce que fait l'autosampleur (depuis la 0.1.0)

- **Session automatique** : les notes (tous les N demi-tons, N notes réparties, ou à la main en Alt-clic sur le clavier), 1 à 16 couches de vélocité (réparties, plus dans le doux ou plus dans le fort), 1 à 8 prises (round robin), une tenue, une relâche maximale, une pause. Changement de programme optionnel avant la session. MIDI envoyé directement au port choisi (ou par la sortie MIDI du plug-in).
- **Découpe** : le bruit de fond est mesuré avant la première note ; chaque enregistrement commence à son attaque (détectée au-dessus du bruit, avec une marge) ou à une latence fixe mesurée (la plus courte vue : les attaques lentes gardent leur forme exacte) ; la queue s'arrête quand elle retombe dans le bruit ; DC retiré, fondus courts.
- **Boucles** : cherchées dans la partie tenue, longues de préférence ; si le son a une modulation lente (LFO, PWM), la boucle en fait un nombre entier de périodes ; chaque paire de points est notée sur la forme d'onde des deux côtés du raccord et sur le niveau ; le raccord est fondu avec des gains qui gardent la puissance pour la corrélation trouvée.
- **Accordage** : la hauteur de chaque note est mesurée (fenêtres courtes pour la confiance, une longue pour la moyenne : oscillateurs désaccordés et dérive ; un sub-oscillateur une ou deux octaves en dessous est reconnu), une valeur par note, corrigée en jouant et écrite dans les WAV.
- **Transposition** des touches entre les notes enregistrées :
  - *HQ formants* : varispeed par sinc fenêtré de Kaiser (64 passages par zéro, ~100 dB, sans repliement en montant), puis, trame par trame, l'enveloppe spectrale (les sommets des harmoniques, espacés comme la hauteur connue le dit, reliés en dB, prolongés sous la fondamentale par la pente des deux premiers) remise où elle était ; l'énergie de chaque trame est gardée.
  - *Classique HD* : le varispeed seul.
  - *Durée fixe* : Signalsmith Stretch (pitch-shift spectral, durées gardées) puis la même correction de formants ; la boucle est recherchée à nouveau près de l'ancienne.
  - Les boucles des touches rendues restent des nombres entiers d'échantillons (vitesse ajustée de moins de 0,1 cent), la zone bouclée rendue comme une boucle sans fin.
  - *Touches rendues HQ* (en avance, sur tous les cœurs) ou *en direct* (sinc à la volée).
- **Carte et clavier** : ce qui va être enregistré (cadres), ce qui l'est (rempli, la note enregistrée en blanc), la note en cours (rouge), la zone qui joue ; clavier jouable (plus haut sur la touche = plus doux), poignées de la plage à enregistrer.
- **Sample** : la forme d'onde, la partie tenue, le note-off, la boucle et son fondu, la tête de lecture, et le raccord tel qu'il s'entend (fin de boucle puis début).
- **Jeu** : couches et round robin, boucles, enveloppe, sensibilité à la vélocité dans une couche, pitch bend, pédale de sustain. *Clavier vers synthé* pour comparer avec l'original.
- **Synthé test** intégré (deux dents de scie désaccordées, un sub, un filtre 24 dB, une dérive, un peu de bruit et de latence) pour tout essayer sans matériel.
- **Instruments** dans `~/Music/REPLIK8/<Nom>/` (l'ancien dossier `~/Music/REPLIKANT` y est déplacé tout seul) : `<Nom>.replikant` (JSON) et `Samples/*.wav` (24 bits, chunks `smpl` et `inst`). Le projet du DAW garde le chemin et le recharge.
- **Exports** : dossier de WAV (note, justesse, boucle, zone dans chaque fichier) ou `.multisample` Bitwig, les samples enregistrés ou toutes les touches rendues.
- **Mises à jour** depuis le plug-in.

## Comment ça se branche

- **Appli** : FICHIER › Réglages audio et MIDI, puis CAPTURE › SOURCE : Son = entrée, Sortie MIDI = le port du synthé. 96 kHz dans la carte son pour transposer très bas.
- **En multi-effet** : REPLIK8 en insert sur n'importe quelle piste, SETTINGS › MONITOR = THRU FX (ou, dans CAPTURE › RÉÉCHANTILLONNER, ENTRÉE : PAR LES EFFETS).
- **Logic** : REPLIK8 en effet audio sur une piste qui reçoit le synthé (type `aumf`, « effet contrôlé par MIDI ») ; pour jouer, dans le slot d'instrument (effets contrôlés par MIDI).
- **Bitwig / Reaper / Cubase** : sur une piste qui reçoit l'audio du synthé ; les notes de la piste jouent l'instrument.
- **Live** : l'enregistrement marche sur une piste audio ; Live n'envoyant pas de notes aux effets audio, on joue l'export (Sampler) ou l'appli.

## Installer

Télécharge `REPLIK8-mac.dmg` (lien sur la page) : l'installeur (AU, VST3, appli), ou glisser chaque élément sur le raccourci de son dossier puis coller la commande Terminal qui débloque l'AU et le VST3 et fait valider l'AU (`auval -v aumf Rplk Dimg`).

## Publier une version

1. Monter `VERSION` dans `CMakeLists.txt` (`project(REPLIKANT VERSION x.y.z …)`) et écrire `RELEASE_NOTES.md` (première ligne `**REPLIK8 X.Y.Z** : …`, puis `- **Titre court** détails`).
2. Fusionner dans `main` : le workflow **release** construit, teste sur un Mac et publie le `.dmg`, le `.zip` et son contrôle. Les plug-ins installés proposent la mise à jour.

## Développer

```
cmake -B build -DCMAKE_BUILD_TYPE=Release        # (-DJUCE_DIR=… pour un JUCE local)
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- `Source/core/` : le DSP sans JUCE (sinc, formants, boucles, hauteur, découpe, plan, WAV) ; `Tests/CoreTests.cpp` le vérifie, transposition comprise (écart des harmoniques avec la vraie note).
- `Source/dsp/` : le DSP du player, sans JUCE (suréchantillonnage, filtres, drives, modulateurs, effets) ; `Tests/DspTests.cpp`.
- `Source/engine/` : l'enregistreur (fil audio, sans allocation), le worker (découpe, assemblage, sauvegarde, rendu des touches en parallèle), l'instrument, le player (`Sound.h` : la table de tous ses réglages), le synthé test ; `Tests/EngineTests.cpp` fait une session entière sur le synthé test (et un pack chez un pote) ; `Tests/PlayerTests.cpp` mesure le player (repliement, échelle d'aliasing, décalage de la PM, transitoires, scènes, horloge, lissage, synthé, rééchantillonnage, CPU) ; `EngineTests` fait aussi une capture de cycles et lit des sons glissés.
- `Source/ui/` : l'interface (écran, pupitre, encodeurs, pictos) ; la cible `Snapshot` (hors build par défaut) dessine la fenêtre en PNG, chaque page, une capture, un rééchantillonnage et une capture de cycles.
- `Source/radio/` : la radio (la banque des récepteurs, l'écoute et son air, les prises, les pads, le découpage ; le réseau dans `RadioNet`) ; `Tests/RadioTests.cpp` la vérifie sans réseau (décodage, horloge, écoute, prises, découpage, pads), `Tests/RadioNetTests.cpp` contre des serveurs locaux (`Tests/radio/radio_mock.py`, python3).
- `Source/drum/` : le DRUM (les pistes, leurs voix, le séquenceur et ses patterns) ; `Tests/DrumTests.cpp` le vérifie.
- `Source/radio/ext/minimp3.h` : le décodeur MP3 minimp3 (lieff, domaine public, CC0).
- `ThirdParty/` : Signalsmith Stretch et Signalsmith Linear (licence MIT, textes à côté).
- `Source/dsp/Space.*` : la réverbération de MIFM1 (MPL-2.0).
- REPLIK8 est sous licence **GPL-3** (`LICENSE`).
- `Resources/fonts/` : Space Grotesk, SIL Open Font License.
