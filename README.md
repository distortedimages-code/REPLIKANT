# REPLIK8

**REPLIK8** (anciennement REPLIKANT) : l'autosampleur de tes synthés, un synthé à cycles et un sampler. Plug-in AU / VST3 et appli pour Mac. Il capture tes synthés (chaque note jouée en MIDI et enregistrée, bouclée, accordée ; ou leurs cycles, pendant qu'un CC balaie un bouton ; ou ce qui sort de tes effets) et en fait des instruments : le SYNTHÉ lit des banques de cycles, le SAMPLER joue un multisample ou n'importe quel son glissé sur la fenêtre. ORBITE (un porteur et deux satellites synchronisés, quatre câblages, une rotation qui fait tourner le timbre) s'y ajoute dans le MIXER. Les deux passent dans des filtres analogiques modélisés, un drive, cinq effets dans l'ordre que tu veux (chaque bloc a son interrupteur), quatre modulateurs et seize scènes. REPLIK8 sert aussi de multi-effet en insert. La lecture des samples : sinc de 128 points précalculé, coupure qui suit la vitesse (PM comprise), copies à l'octave jusqu'à 1/256, cadran TUNE de ±96 demi-tons. Exports en dossier de WAV, en `.multisample` Bitwig ou en pack zip à partager.

**Page de téléchargement** : https://distortedimages-code.github.io/REPLIKANT/

## La version 0.3.0 : synthé, sampler, capture des cycles, rééchantillonnage

- **Modes** : SYNTH (par défaut, il joue dès l'ouverture), SAMPLER, DRUM (en 0.4) ; CAPTURE par-dessus : notes, cycles ou rééchantillonner.
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
- **Effets**, dans cet ordre : SPECTRE (pics d'une FFT de 4096 points rendus par 1 à 33 oscillateurs), PITCH (deux voix Signalsmith, retards, réinjection, CRISTAUX : grains à l'envers), CHORUS / FLANGER / VIBRATO, CRUSH, ESPACE (FDN de 8 lignes de MIFM1, shimmer, gel).
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
- `ThirdParty/` : Signalsmith Stretch et Signalsmith Linear (licence MIT, textes à côté).
- `Source/dsp/Space.*` : la réverbération de MIFM1 (MPL-2.0).
- REPLIK8 est sous licence **GPL-3** (`LICENSE`).
- `Resources/fonts/` : Space Grotesk, SIL Open Font License.
