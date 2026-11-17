# Journal des changements

Format : [versionnage sémantique](https://semver.org/lang/fr/). Tant que le numéro commence par 0,
le projet est en développement et tout peut encore changer.

## [0.5.0] — Interface détachée, corrections

- Plus aucune lumière parasite sur l'image du jeu (les lueurs du décor sont effacées sur l'écran).
- Les sons d'extérieur (vent, gouttes, grillons) sont retirés ; il ne reste que le feu, coupé par défaut.
- Rayon de visée jaune visible en entier quand on pointe hors de l'écran ; poignées de déplacement plus grandes.
- Fenêtres (groupe, inventaire, sorts, commandes, boussole) placées dans le plan de l'écran, attachées à lui
  et qui ne traversent plus le sol, les murs ni la table ; plus de panneau d'atlas en double.
- Joystick gauche : une seule direction à la fois (pas de côté plus facile).
- Images personnelles : fenêtre, portrait, `tableau1` et `tableau2`, détectées par préfixe de nom dans le
  dossier `decor`, sans relancer l'application.

## [0.4.0] — Panneaux déplaçables

- Interface détachée de l'écran : inventaire, sorts, portraits, boussole et commandes en panneaux
  que l'on saisit (gâchette sur la poignée) et que l'on place où l'on veut.
- Clavier virtuel (bouton X) pour nommer les sauvegardes.
- Bibliothèque réaménagée : table devant le joueur, reliques sur socles, portrait du roi, vue de fenêtre.
- Écran déplaçable et ajustable en réalité mixte ; ordinateur 486 sous le moniteur cathodique.
- Son du feu avec interrupteur.

## [0.3.0] — Mode fenêtre (abandonné)

- Vue du donjon agrandie avec interface au poignet. Jugé peu réussi ; remplacé par les panneaux de la 0.4.0.

## [0.2.0] — Bibliothèque et réalité mixte

- Bibliothèque médiévale autour de l'écran, qui change selon la zone du jeu.
- Cadre ouvragé, gants, moniteur cathodique, réalité mixte, vibrations, son spatialisé.

## [0.1.0] — Écran flottant

- Le jeu complet sur un écran flottant en VR sur Meta Quest 3, avec manettes comme pointeur, son et sauvegardes.
