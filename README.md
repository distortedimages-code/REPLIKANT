# REPLIKANT

**REPLIKANT** : l'autosampleur de tes synthés. Plug-in AU / VST3 et appli pour Mac. Il joue chaque note au synthé (MIDI), l'enregistre (audio), trouve la boucle, mesure la justesse, et fait de l'ensemble un instrument jouable, exportable en dossier de WAV ou en `.multisample` Bitwig.

**Page de téléchargement** : https://distortedimages-code.github.io/REPLIKANT/

## Ce que fait la version 0.1.0

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
- **Jeu** : polyphonie 48 voix, couches et round robin, boucles, enveloppe, sensibilité à la vélocité dans une couche, pitch bend, pédale de sustain. *Clavier vers synthé* pour comparer avec l'original.
- **Synthé test** intégré (deux dents de scie désaccordées, un sub, un filtre 24 dB, une dérive, un peu de bruit et de latence) pour tout essayer sans matériel.
- **Instruments** dans `~/Music/REPLIKANT/<Nom>/` : `<Nom>.replikant` (JSON) et `Samples/*.wav` (24 bits, chunks `smpl` et `inst`). Le projet du DAW garde le chemin et le recharge.
- **Exports** : dossier de WAV (note, justesse, boucle, zone dans chaque fichier) ou `.multisample` Bitwig, les samples enregistrés ou toutes les touches rendues.
- **Mises à jour** depuis le plug-in (comme OXIde, IMPAKT et RackAttack Editor).

## Comment ça se branche

- **Appli** : *Carte son* en haut, Source = Entrée, Sortie MIDI = le port du synthé.
- **Logic** : REPLIKANT en effet audio sur une piste qui reçoit le synthé (type `aumf`, « effet contrôlé par MIDI ») ; pour jouer, dans le slot d'instrument (effets contrôlés par MIDI).
- **Bitwig / Reaper / Cubase** : sur une piste qui reçoit l'audio du synthé ; les notes de la piste jouent l'instrument.
- **Live** : l'enregistrement marche sur une piste audio ; Live n'envoyant pas de notes aux effets audio, on joue l'export (Sampler) ou l'appli.

## Installer

Télécharge `REPLIKANT-mac.dmg` (lien sur la page) : l'installeur (AU, VST3, appli), ou glisser chaque élément sur le raccourci de son dossier puis coller la commande Terminal qui débloque l'AU et le VST3 et fait valider l'AU (`auval -v aumf Rplk Dimg`).

## Publier une version

1. Monter `VERSION` dans `CMakeLists.txt` (`project(REPLIKANT VERSION x.y.z …)`) et écrire `RELEASE_NOTES.md` (première ligne `**REPLIKANT X.Y.Z** : …`, puis `- **Titre court** détails`).
2. Fusionner dans `main` : le workflow **release** construit, teste sur un Mac et publie le `.dmg`, le `.zip` et son contrôle. Les plug-ins installés proposent la mise à jour.

## Développer

```
cmake -B build -DCMAKE_BUILD_TYPE=Release        # (-DJUCE_DIR=… pour un JUCE local)
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- `Source/core/` : le DSP sans JUCE (sinc, formants, boucles, hauteur, découpe, plan, WAV) ; `Tests/CoreTests.cpp` le vérifie, transposition comprise (écart des harmoniques avec la vraie note).
- `Source/engine/` : l'enregistreur (fil audio, sans allocation), le worker (découpe, assemblage, sauvegarde, rendu des touches en parallèle), l'instrument, le lecteur, le synthé test ; `Tests/EngineTests.cpp` fait une session entière sur le synthé test.
- `Source/ui/` : l'interface rétro pixel (palette et polices de RackAttack Editor) ; la cible `Snapshot` (hors build par défaut) dessine la fenêtre en PNG.
- `ThirdParty/` : Signalsmith Stretch et Signalsmith Linear (licence MIT, textes à côté).
- `Resources/fonts/` : Silkscreen et VT323, SIL Open Font License.
