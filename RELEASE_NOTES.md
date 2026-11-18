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
