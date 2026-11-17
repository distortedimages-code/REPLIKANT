**REPLIKANT 0.1.0** : la première version, un autosampleur complet pour tes synthés.

- **Enregistrement automatique** des notes que tu choisis (tous les N demi-tons, N notes réparties, ou à la main), avec les couches de vélocité et les prises (round robin) que tu veux, le MIDI envoyé directement au synthé
- **Découpe propre** : bruit de fond mesuré, attaque détectée (ou une latence fixe pour tous), la queue gardée jusqu'à ce qu'elle retombe dans le bruit
- **Boucles automatiques** longues, qui suivent les modulations lentes (LFO, PWM), avec un fondu qui garde la puissance
- **Accordage automatique** : la justesse de chaque note est mesurée et corrigée à la lecture
- **Transposition HQ** : les touches entre deux notes enregistrées sont calculées avec un varispeed sans repliement puis l'enveloppe spectrale remise en place (le filtre du synthé ne bouge pas avec la note)
- **Clavier et carte des zones** : ce qui va être enregistré, ce qui l'est, ce qui joue
- **Exports** : dossier de WAV avec note, boucle et zone dans chaque fichier (Logic, Ableton, Kontakt...) et .multisample Bitwig, les samples enregistrés ou toutes les touches rendues
