**REPLIK8 0.4.15** : un S-mètre à aiguille et un VU de la radio.

- **Les vu-mètres de la radio** : sur les pages TUNE, LISTEN, AIR, TAKE et PADS, la partie droite de l'écran montre maintenant un cadran à aiguille et, en dessous, une barre de niveau
- **L'aiguille** : sur un récepteur ondes courtes, c'est son S-mètre, comme sur un vrai poste : de S1 à S9 (6 dB par point, S9 à -73 dBm), puis +20, +40, +60 en rouge pour les stations puissantes. Elle monte vite et redescend lentement, et aide à l'accord : elle grimpe quand tu tombes sur une station. Sur une station internet, sans signal radio à mesurer, c'est un VU-mètre de son son (0 VU à -18 dBFS)
- **La barre** : le niveau du son tel qu'il arrive, avec son air, avant LEVEL, de -48 à 0 dBFS : sa moyenne allumée, ses crêtes par-dessus, la plus haute tenue un instant

**REPLIK8 0.4.14** : le potard d'accord tourne lentement, et la bande défile sans coupure.

- **Accord fin** : le gros potard FREQUENCY des ondes courtes tourne plus lentement que les autres : 40 pixels pour 1 kHz, un tour de son cadran (400 pixels) pour 10 kHz. Plus tu vas vite, plus il accélère (jusqu'à 10 kHz par pixel), pour traverser la bande d'un geste puis revenir lentement sur une station. Maj : encore plus fin. La molette : 1 kHz par cran (Maj : 0,1 kHz). L'afficheur montre toujours le dixième de kHz
- **SPECTRAL et FREEZE depuis SAMPLE** : la banque freeze marche aussi quand le son est en SAMPLE. Choisir SPECTRAL ou allumer FREEZE passe le son en STRETCH tout seul. Si tu repasses en SAMPLE, les réglages de SPECTRAL restent visibles mais grisés
- **Les scènes A et B, plus directes** : appuie sur **A**, tu es en p-lock dans la scène A et le fader passe côté A, pour entendre ce que tu règles. Les potards tournés y sont verrouillés. Appuie sur **B**, pareil côté B. Réappuie sur la touche allumée pour sortir, puis glisse de l'une à l'autre avec le fader. Pendant que A ou B est allumé, une case (1 à 16) choisit quelle scène il joue. Clic droit sur A ou B : effacer sa scène. Les cases seules marchent comme avant (une case allumée : ses réglages verrouillés)
- **La bande entendue en tournant** : un petit mouvement (jusqu'à 5 kHz, même mode) accorde le récepteur sans couper le son, comme sur un vrai poste. Le changement de mode ou un grand saut passent toujours par le fondu MORPH

**REPLIK8 0.4.13** : un instant du son tenu sans fin, en nappe, et joué sur le clavier.

- **STRETCH › SPECTRAL** : dans le SAMPLER, page PLAY, deuxième banque (appuie à nouveau sur PLAY). ENGINE passe de GRAINS à SPECTRAL : le son n'est plus relu en grains. Un instant du son est analysé, ses phases sont tirées à nouveau au hasard, et il est rejoué sans fin, lisse, sans boucle audible. SPEED le fait avancer (à 0,05, vingt fois plus lent), SPRAY le brouille
- **FREEZE** : la lecture s'arrête là où elle est, le son tenu tant que la note dure. Ça marche aussi en GRAINS. À moduler, ou à mettre sur une scène pour le déclencher en jouant
- **Des réglages musicaux** : SMEAR, la longueur de l'instant tenu (de 40 ms, qui garde le grain, à 1 s, qui fond tout en nappe). TONAL fait ressortir les partiels et enfonce ce qui est dessous. HARMONICS fait passer le son par les harmoniques de la note jouée : à fond, il ne reste qu'elles, et un bruit, une voix ou une station deviennent des accords. SHIMMER ajoute l'octave au-dessus puis la douzième. WIDTH écarte les deux côtés, même d'un son mono. Le niveau reste le même quoi que tu tournes
- **Sur le clavier** : chaque note transpose l'instant tenu sans changer sa durée. Les accords marchent
- **Une nappe dans un bout de radio** : prends un pad de la radio, envoie-le au SAMPLER, passe en STRETCH, ENGINE sur SPECTRAL, FREEZE. Même 50 ms suffisent : ce qui manque à la fenêtre est compensé
- L'écran de la page PLAY montre les fenêtres qui se chevauchent, les harmoniques gardées et FREEZE

**REPLIK8 0.4.12** : changer de station sans siffler, un gros potard d'accord, le DRUM en stéréo.

- **Plus de sifflements ni de souffle à l'accord** : quand un récepteur change de fréquence, de station ou que tu passes à un autre récepteur, la station quittée est figée (sa dernière demi-seconde, en grains qui se chevauchent), elle s'assombrit et s'efface pendant que la suivante arrive par-dessus. **MORPH** (page LISTEN, à la place de STATIC) règle ce passage : à 0, un simple fondu de 30 ms ; à 50 %, la station tenue environ une seconde ; à fond, une longue traîne de plusieurs secondes. Les réglages STATIC et WHISTLE de l'air sont retirés (CRACKLE reste)
- **Un gros potard d'accord** pour la fréquence des récepteurs ondes courtes : une jupe noire striée qui tourne, son cadran gradué de 0 à 9 imprimé autour (un tour = 10 kHz, ses chiffres lisent les kHz). Glisse pour parcourir la bande (environ 7 kHz par pixel vers 10 MHz, Maj : 1 kHz), la molette pour les petits pas
- **Les potards à zéro au milieu de leur course** (VOLUME de -36 à +12 dB, et les autres réglages qui passent par zéro sans être symétriques) : leur arc lumineux part maintenant de leur zéro, plus du haut du potard
- **Le DRUM joue les sons et les prises en stéréo** : un sample stéréo, une prise de la radio et ses tranches gardent leur gauche et leur droite (filtre et drive sur chaque côté). PAN devient une balance. **STEREO** (page SOURCE, allumé par défaut) : coupé, la piste repasse en mono, placée par PAN comme avant

**REPLIK8 0.4.11** : les récepteurs ondes courtes reviennent.

- **La liste des récepteurs ondes courtes était vide** : la page qui les recensait demande maintenant de cliquer avant de les montrer. REPLIK8 lit la liste sur la carte qui la republie toutes les quelques minutes (plus de 800 récepteurs dans le monde), les plus libres en premier, et revient à l'ancienne page si celle-ci ne répond pas
- **Coller l'adresse d'un récepteur** : sous les bandes, un champ prend l'adresse telle que ton navigateur l'affiche (http://…:8073). Si elle porte une fréquence et un mode (?f=9420am), REPLIK8 s'y accorde. Pratique pour un récepteur trouvé sur une carte ou un forum
- Les récepteurs derrière le relais du fabricant sont joints directement sur leur port

**REPLIK8 0.4.10** : le filtre WASP refait.

- **WASP** : son caractère vient d'amplis de fortune, des inverseurs logiques détournés, et il est maintenant modélisé à partir d'eux. Ces inverseurs ont peu de gain : les intégrateurs du filtre fuient, d'autant plus qu'ils approchent de leurs limites. Résultat : il ne part jamais en auto-oscillation, sa résonance est nasale et marquée à bas niveau (jusqu'à +16 dB à la coupure) et s'effondre en grain quand le son est fort ou que DRIVE monte. Leur courbe est bancale : des harmoniques paires, de plus en plus quand on le pousse. Le courant qui règle la coupure sature aussi, et l'entrée passe elle-même par un inverseur
- **WASP en NOTCH** : c'est un mélange passe-bas / passe-haut. MIX (page FILTER) à 0 : une encoche symétrique et profonde ; en tournant MIX, l'encoche penche vers le passe-haut

**REPLIK8 0.4.9** : quand le test de l'assistant trouve un problème, il le corrige, ou il dit quoi faire.

- **Ce que REPLIK8 peut corriger, il le corrige** et refait le test (marqué CORRIGÉ) : sa propre sortie qui revient dans son entrée (MONITOR coupé), un plugin qui dépasse 0 dB (fichiers en 32 bits flottants, rien d'écrêté), le son arrivé par l'autre entrée (SOURCE changée), un CC qui ne change rien au son (il essaie les CC courants l'un après l'autre et garde celui qui marche)
- **Ce qui dépend de ton logiciel ou de ton matériel** : un encadré À FAIRE CHEZ TOI, la marche à suivre en deux ou trois lignes pour ton logiciel, et les touches qui aident : jouer une note (pour voir le témoin MIDI de la piste), envoyer le CC (pour le MIDI learn du synthé), passer le balayage en vélocité ou à la main
- **Fausse alerte corrigée** : « REPLIK8 entend sa propre sortie » s'affichait avec un synthé qui laisse un tout petit courant continu quand il se tait (beaucoup de synthés logiciels). Et PRÊT ne s'affiche plus à côté d'une ligne rouge
- **Le test du CC** écoute le son avant, pendant et après le CC : l'enveloppe de la note qui continue d'évoluer n'est plus prise pour un effet du CC
- **Une latence qui varie un peu** n'est plus un avertissement : pour les notes, chaque attaque est recalée ; pour les cycles, elle ne compte pas
- Le test commence par couper les notes restées en l'air, et mesure le bruit de fond sur les 10 ms les plus calmes

**REPLIK8 0.4.8** : un assistant de capture, pas à pas, jusqu'à PRÊT.

- **L'assistant de capture** : dans CAPTURE, choisis NOTES ou CYCLES (ou sa touche ? ASSISTANT) : il prend l'écran et te mène pas à pas, en cinq étapes au plus. LOGICIEL : il reconnaît Live, Logic, Bitwig, Reaper, Cubase ou Studio One (et le format, AU ou VST3), ou tu le choisis. SOURCE : un plugin dans ton logiciel, un synthé hardware, ou le synthé de test
- **Un synthé logiciel** passe par le bus IAC, le câble MIDI intégré au Mac. S'il est éteint, un bouton ouvre Configuration audio et MIDI et l'assistant le guette : dès qu'il apparaît, il le coche et règle REPLIK8 (source, sortie MIDI, canal, écoute directe)
- **Un synthé hardware** : ses ports MIDI et les entrées de ta carte son, sous les noms que leur donne ton Mac, à choisir d'un clic (mono ou stéréo). Dans l'appli, l'entrée choisie est réglée directement
- **PISTE** : ce qu'il y a à régler dans ton logiciel, dessiné comme il le montre (l'entrée MIDI ou audio, le monitoring In/Auto/Off, l'ordre des plugins), ce qui est à changer en or, avec les raccourcis du Mac et le nom de ta piste quand le logiciel le donne
- **TEST** : trois notes envoyées, ce qui revient écouté. Il vérifie que le son arrive, son niveau (ni trop faible, ni saturé), qu'il ne s'agit pas de la propre sortie de REPLIK8, et mesure la latence : stable, toutes les notes sont calées dessus. Pour les cycles, il vérifie aussi que le CC balayé change bien le son. Des vumètres, la latence sur une ligne de temps, et PRÊT quand tout va bien. Le son arrive par l'autre entrée (sidechain) ? Il change la source tout seul et refait le test
- **SETTINGS › ASSISTANT** : OFF pour qu'il ne s'ouvre plus tout seul (sa touche reste dans CAPTURE)

**REPLIK8 0.4.7** : les mods partout (les réglages crantés, le DRUM), un rythme découpé dans la radio, et le DRUM qu'on règle en l'écoutant.

- **Assigner un mod, plus clair** : clique M1 (en bas à droite) : un bandeau sur l'écran dit quoi faire, les potards qu'il peut moduler s'entourent de sa couleur, les autres s'effacent. Tourne-en un : la distance devient la profondeur (vers la gauche, la modulation descend). Double-clic : la route retirée. Re-clique M1 pour finir
- **Les réglages crantés se modulent** : LENGTH de la radio, son récepteur et son mode, les modèles de filtre et de drive, le cube, les formes d'onde, les octaves et demi-tons des oscillateurs, la transposition du sampler, la synchro, les têtes et le mouvement d'ECHO, les attaques du COMP… Le mod passe d'un cran à l'autre, et l'écran montre le cran où il est
- **Les mods sur le DRUM** : chaque réglage d'une piste peut recevoir un mod (arme-le, tourne le potard de la piste choisie). Il est pris à chaque coup, et la coupure, la résonance, l'enveloppe du filtre, le drive, le niveau, le panoramique, la couleur du bruit suivent même pendant qu'un coup résonne
- **Régler un son du DRUM en l'écoutant** : à l'arrêt, chaque potard tourné rejoue la piste ; en lecture, ce qu'on tourne s'entend tout de suite, même sur le coup qui sonne encore
- **RADIO › CUT** : un rythme découpé dans tous les récepteurs accordés à la fois. Chaque pas (de 1 à 32 seizièmes, LENGTH) prend un récepteur au hasard et un moment de ses dernières secondes (DEPTH), calé sur une attaque, ou se tait (DENSITY). VARY ajoute des ratchets, des pas à l'envers, des pas coupés court. Le motif boucle ; à chaque tour, GRAB est la chance que chaque pas soit tiré à nouveau, et un clic sur le motif en tire un autre tout de suite. L'écran montre le motif, chaque pas de la couleur de son récepteur
- **Changer de récepteur ou de mode** (à la main ou par un mod) passe par un fondu de quelques millisecondes : plus de clic
- **LIMIT**, dans SETTINGS : un limiteur de haute qualité, la toute dernière étape, allumé par défaut. Rien ne sort au-dessus de CEILING (-0,3 dB), crêtes entre les échantillons comprises, quoi que fassent les filtres, les drives et les effets. Il anticipe les crêtes de 1,5 ms (le DAW compense ce délai) pour descendre avant elles, sans écraser le son. RELEASE, DRIVE pour le pousser, et l'écran qui montre ce qu'il retire

**REPLIK8 0.4.6** : ECHO, un compresseur en sortie, la capture de cycles à l'écoute, et une machine qu'on prend en main.

- **ECHO**, un septième bloc dans la chaîne : une bande et quatre têtes de lecture, réparties sur son temps (libre ou calé sur le tempo), chacune de son côté dans la stéréo. Elles lisent dans le sens de la bande, à l'envers, ou une sur deux à l'envers ; elles restent en place, se balancent (WOBBLE), glissent le long de la bande pour faire monter ou descendre chaque écho jusqu'à l'octave (SCAN), ou sautent ailleurs (JUMP). FEEDBACK jusqu'à 110 % sans jamais exploser, TONE, WEAR (pleurage, saturation, aigus usés). À l'écran : la bande, ses têtes, et l'endroit où tombent les échos
- **COMP**, dans SETTINGS, à la toute fin de la chaîne : un seul bouton pour le seuil, le taux et le rattrapage, de rien du tout jusqu'au limiteur, puis au-delà, où les coups passent sous leurs propres queues (pompe, aspiration). Six attaques, cinq relâchements et AUTO pour les nappes, un gain d'entrée, une gate, un drive, un mélange parallèle. Il écoute le son lui-même, le sidechain du DAW, l'entrée, ou une seule piste du DRUM. Le volume reste à sa place quand on le tourne. À l'écran, sa courbe et ce qu'il retire
- **CAPTURE CYCLES › LISTEN** : lance la capture, va à ton synthé, tiens une note en tournant un bouton. L'enregistrement part au seuil et s'arrête au silence (ou STOP) ; REPLIK8 suit la hauteur, garde les cycles là où le timbre bouge, les aligne entre eux et en fait la banque du synthé
- **Des onglets** au bas de l'écran pour les banques de chaque page (une lampe pour chaque effet allumé dans FX)
- **À la souris sur l'écran** : la courbe du filtre (coupure, résonance), les enveloppes, la pile de cycles, le début du sample, la bande de SPECTRE, la bande d'ECHO, la courbe du COMP. Maj pour aller fin, double-clic pour revenir
- **La chaîne d'effets** : garde un bloc appuyé une seconde, il sort de son emplacement ; glisse-le, les autres se poussent ; lâche-le, il s'enfonce à sa place
- **SPECTRE** : ses formes d'onde dessinées autour de leur potard
- **Trois claviers**, dans SETTINGS : CLASSIC, PADS (deux rangées rétroéclairées), FLAT (une dalle sombre qui s'allume par en dessous)
- **RADIO** : ON AIR, REC, GRAB et les envois vers le SAMPLER et le DRUM redessinés, avec leurs pictos
- Quelques textes de l'écran mal affichés (des accents) corrigés

**REPLIK8 0.4.5** : plus un clic, des effets qui gardent le volume, un suiveur qui écoute ce que tu veux.

- **Plus de clics** : tous les potards glissent en douceur, échantillon par échantillon (les niveaux du mixer, le DRIVE, les positions dans les cycles, le filtre). Changer de modèle de drive ou de filtre, de forme d'onde dans SPECTRE ou de mode dans CHORUS passe par un fondu de quelques millisecondes
- **Les effets gardent le volume** : SPECTRE, PITCH, CHORUS et ESPACE ne font plus baisser le son. Le mélange sec/effet se fait à puissance constante et l'effet arrive au niveau du son sec : on l'ouvre, il s'ajoute, rien ne se creuse
- **ESPACE : DRY et WET** séparés. À 100 % de DRY, la réverbe s'ajoute sans rien prendre au son
- **DRIVE refait** : plus d'effet de phase quand on le mélange (le mélange se fait dans la saturation, la bande pleure pareil à gauche et à droite), et un niveau qui reste à sa place quand on pousse
- **FOLLOW marche partout** : il écoute l'entrée audio (INPUT), l'instrument lui-même (SELF : ses voix, ou ce que jouent le DRUM et la RADIO), le sidechain du DAW, ou une piste du DRUM seule (DRUM 1 à 8). L'écran montre ce qu'il envoie, en direct
- **Vers le haut, vers le bas, des deux côtés** : chaque mod a sa polarité, ± (la modulation s'étend des deux côtés du réglage) ou + (elle part du réglage et va d'un seul côté). Une route peut maintenant descendre : arme le mod, tourne le potard vers la gauche. Les enveloppes et les suiveurs passent aussi en ±
- **WTF?! LOOP** : sa boucle va de 2 à 16 tirages (clique les points), et le dé en tire une nouvelle. En LIVE, le dé relance un tirage tout de suite. La boucle est gardée avec le son
- **DRUM à 44,1 kHz** : il jouait un peu trop lent et trop grave quand le DAW tournait à 44,1 kHz. Corrigé

**REPLIK8 0.4.4** : CUBE, un filtre qu'on parcourt en trois dimensions.

- **CUBE**, un nouveau modèle dans FILTER : sept sections de résonances et de creux posées aux huit coins d'un cube. Déplace le point dedans et le filtre passe d'un coin à l'autre sans une couture : des voyelles qui se changent l'une en l'autre, des formants qui glissent, des peignes qui s'accordent, des cloches qui deviennent des barres de métal
- **Dix cubes** : VOWELS, SWEEP, PHASER, COMB, BELLS, TILT, DUO, HOLLOW, BODY, SPREAD
- **X, c'est le CUTOFF** : l'enveloppe du filtre, le clavier, la vélocité, les mods et WTF?! le promènent dans le cube. Y et Z ont leurs réglages (et se modulent), RESO affûte ce qui résonne, DRIVE le pousse dans une saturation douce. Dans la banque CUBE : SMOOTH (le point glisse jusqu'à 200 ms quand il saute) et TRIM (le niveau de sortie)
- **À l'écran**, le cube en 3D : attrape le point et glisse-le (X et Y ; avec Maj, X et Z ; la molette pour Z). Les coins s'allument selon leur part dans le son, une traînée suit le point quand il bouge, et les sept sections s'affichent sur les fréquences
- Il marche partout : synthé, sampler, DRUM, RADIO et l'entrée en multi-effet

**REPLIK8 0.4.3** : WTF?! marche sur le DRUM.

- **WTF?! sur le DRUM** : il ne tirait que des réglages du sampler, que le DRUM ne joue pas (et le filtre principal, éteint le plus souvent) : rien ne bougeait. Maintenant chaque coup est tiré à sa façon : TUNE, DECAY, SWEEP, PMOD, la coupure et la résonance du filtre de la piste, NOISE, START (ou POSITION sur une voix synth), DRIVE et F.ENV, chaque piste avec sa propre part du tirage. LIVE et LOOP marchent comme ailleurs, sur le tempo du DAW ou de l'appli

**REPLIK8 0.4.2** : SPECTRE refait de fond en comble.

- **Les slices** : FREE (avec MULTIPLY, jusqu'à 32 fois plus lent : une analyse toutes les quelques secondes), SYNC sur la grille du DAW, NOTES (une analyse à chaque note jouée, puis le spectre tenu jusqu'à la suivante) ou ATTACKS (à chaque attaque du son qui entre : parfait sur le DRUM ou en insert)
- **BLUR** en secondes (de 20 ms à 10 s, figé au bout), le même quelle que soit la vitesse des slices ; FREEZE garde les oscillateurs qui jouent, la bande et les hauteurs toujours réglables
- **TRANSIENTS** : une seconde analyse, quatre fois plus courte, prend le relais au-dessus de 1 kHz : les aigus et les attaques répondent trois fois plus vite
- **NOTCH** : la bande rejetée au lieu d'être gardée
- **Les hauteurs** : FINE (±7 demi-tons), OCTAVE de -2 à +3, et WARP, qui resserre les partiels vers une seule hauteur ou les écarte jusqu'à l'inharmonique. Elles bougent aussi sur un spectre figé
- **GLIDE** en demi-tons, jusqu'à 10 s, et HOLD qui fige les hauteurs. FOLLOW : RANK (les partiels dans l'ordre, tout glisse quand un nouveau arrive en dessous) ou TRACK (chaque oscillateur garde le plus proche du sien)
- **Formes d'onde** : PULSE, et SINE XF / SAW XF, où l'ancienne hauteur s'efface pendant que la nouvelle monte, sur toute la durée de la slice
- **FEEDBACK** jusqu'au bout, par un limiteur : le spectre se nourrit de lui-même jusqu'au drone, sans jamais s'emballer. MIX à puissance constante
- SPECTRE a maintenant trois banques dans la page FX, et l'écran montre l'horloge, la bande ou le notch

**REPLIK8 0.4.1** : une enveloppe pour le filtre de chaque piste du DRUM.

- **F.ENV, F.ATTACK, F.DECAY** dans la page TONE : au coup, la coupure du filtre de la piste monte (ou descend) jusqu'à six octaves, puis revient en F.DECAY (HOLD : elle reste). De quoi faire claquer une caisse ou miauler une basse, et chaque pas peut avoir les siens
- L'écran de la page TONE montre où l'enveloppe emmène le filtre au moment du coup

**REPLIK8 0.4.0** : le DRUM, huit pistes et leur séquenceur.

- **Huit pistes** : chacune joue une voix faite des cycles du synthé (les basiques ou ceux capturés de ton synthé), un son glissé sur la fenêtre, ou une prise de la radio et ses tranches. Elle se règle comme une batterie : NOTE et TUNE, SWEEP (la hauteur qui tombe, pour les grosses caisses et les toms) et son temps, PMOD et RATIO (le métal, les cloches), ATTACK et DECAY, BODY, CLICK, NOISE et sa couleur, un filtre par piste, DRIVE, LEVEL, PAN et quatre groupes de CHOKE
- **Le séquenceur** : 64 pas en quatre pages, une longueur et une vitesse par piste (de 1/8X à 2X, ternaires compris) pour faire tourner les pistes les unes contre les autres. Chaque pas peut garder ses propres réglages (lève-le, tourne les encodeurs), un ratchet (glisse le pas vers le haut), sa probabilité, son micro-décalage, et un retrig en roulement dont les coups se resserrent ou s'écartent, montent ou s'éteignent. SWING pour toute la machine
- **Patterns** : 128, en huit banques de seize (A01 à H16). Le suivant part à la fin de celui qui joue, ou au bout de 1 à 8 mesures ; des chaînes les enchaînent ; copier, coller, vider
- **L'horloge** : le DRUM suit le transport du DAW, ou la touche ▶ dans l'appli. Les pistes se jouent aussi sur ton clavier, de C1 à G1
- **De la RADIO au DRUM** : → DRUM pose la prise sur la piste choisie, une tranche par pas, ou un seul pad. Un son glissé sur la fenêtre va sur la piste choisie (plusieurs sons : sur les suivantes)
- Le DRUM passe dans le filtre et les effets, avec les mods et les scènes

**REPLIK8 0.3.6** : plus de larsen à la capture, et la radio qu'on peut couper.

- **MONITOR sur OFF par défaut** : l'entrée n'est plus renvoyée à la sortie quand la page CAPTURE s'ouvre, ni pendant une capture (elle enregistre quand même). Une entrée qui entend les enceintes (le micro du Mac, une piste qui écoute sa sortie) ne peut plus siffler. Les anciens réglages repassent sur OFF ; DIRECT reste dans SETTINGS, au casque
- **ON AIR** : un interrupteur dans la RADIO. Coupée, elle se tait et lâche sa connexion au bout de 45 s ; la prise et ses pads restent jouables

**REPLIK8 0.3.5** : la RADIO, un onglet à elle. Tu écoutes, tu samples, tu découpes, tu joues.

- **RADIO** : quatre récepteurs, chacun accordé sur une station internet (l'annuaire libre des stations, en MP3) ou sur un récepteur ondes courtes partagé sur le réseau, à la fréquence que tu veux (AM, USB, LSB, CW, les bandes de radiodiffusion, les radioamateurs, les signaux horaires). Le flux garde toujours une seconde d'avance et son horloge reste calée sur celle de l'audio, sans dérive
- **LIVE, LOOP, DIG** : ce qui passe maintenant (TUNE le décale comme un récepteur accordé à côté, SQUELCH hache entre les mots), sa dernière LENGTH rejouée en boucle au tempo, ou des morceaux allés chercher plus loin en arrière, à chaque tour
- **AIR** : la bande du récepteur, le fading des ondes courtes, le sifflement d'une porteuse, les craquements d'un orage, le détecteur poussé, le souffle
- **Prises** : GRAB garde les dernières secondes entendues (de 1 à 30), REC tout ce qui passe jusqu'à STOP. La prise est découpée à ses attaques en 4, 8 ou 16 pads, jouables sur le clavier à partir de C1
- **Vers le SAMPLER** : la prise entière (bouclée) ou un seul pad, en un clic, avec tout le moteur de lecture, le SUB, ORBITE et les filtres

**REPLIK8 0.3.4** : un vrai sub sous tes samples, et SPECTRE qu'on voit travailler.

- **SUB** : un grave propre sous le sample, même transposé très bas. SINE, un sinus sur la note qui respire avec le sample, ou OCTAVE, le sample lui-même relu une ou deux octaves plus bas par le moteur de lecture, en grains calés sur son temps. Un crossover remplace le grave étiré du sample, DRIVE le réchauffe, et il ne descend jamais sous 25 Hz
- **SPECTRE à l'écran** : ses partiels en direct sur une échelle de fréquences, avec leur traînée quand ils glissent, et la bande analysée
- **LFO** : le point blanc roule sur la forme d'onde, là où en est vraiment la modulation
