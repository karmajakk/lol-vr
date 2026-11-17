# Journal des changements

Format : [versionnage sémantique](https://semver.org/lang/fr/). Tant que le numéro commence par 0,
le projet est en développement et tout peut encore changer.

## [0.9.0] — Aide, table déplaçable, six images

- Panneau d'aide sur les manettes et les boutons : affiché au premier lancement, puis par la
  touche « Aide » du clavier (X).
- Le personnage choisi pour le combat est simplement encadré de jaune (plus de voile jaune).
- La table se déplace (barrette au-dessus du plateau) et change de taille (joystick droit en la
  tenant) ; sa place est mémorisée.
- Six images personnelles au lieu de quatre : `tableau3` (mur de gauche) et `tableau4` (mur du
  fond, à droite de la porte) s'ajoutent.

## [0.8.0] — Fiche de personnage dans l'écran, table au choix

- La fiche d'un personnage s'affiche en entier dans l'écran (qui s'élargit vers la droite) ; les
  fenêtres restent en place et celle de la boussole s'éteint. Remplace le retour à l'image
  entière de la 0.7.1, qui ne concerne plus que le menu ScummVM.
- Bouton « Table » sous le clavier (X) : affiche ou retire la table et le tabouret.
- Le bouton « Tout ranger » s'appelle maintenant « Ranger ».

## [0.7.1] — Fiche de personnage et menu ScummVM en image entière

- Quand on ouvre la fiche d'un personnage, l'image entière revient à l'écran (la fiche n'est plus
  coupée entre l'écran et la fenêtre de la boussole) ; les fenêtres reviennent à la sortie.
- Même chose pour le menu ScummVM (bouton menu de la manette gauche).

## [0.7.0] — Combat aux boutons

- Dans le donjon, le bouton A fait attaquer le personnage choisi et le bouton B ouvre sa magie ;
  un coup de joystick droit vers le haut ou le bas change de personnage (son cadre s'éclaire dans
  la fenêtre du groupe). Ailleurs, A et B restent Entrée et Échap.

## [0.6.0] — Fenêtres indépendantes, coins pour agrandir

- Chaque fenêtre (groupe, inventaire, sorts, commandes, boussole) est indépendante de l'écran :
  déplacer ou agrandir l'écran ne bouge plus les autres. La disposition repart de zéro une fois.
- Agrandir ou réduire l'écran ou une fenêtre en tirant sur un coin (une équerre de laiton apparaît
  quand on vise le coin, juste à l'extérieur).
- Les barrettes de déplacement des fenêtres du jeu sont sur le côté (et non plus au-dessus) : le
  pointeur passe de l'écran au groupe et à l'inventaire sans rencontrer de barrette.
- L'endroit où l'on a posé l'écran est mémorisé d'une fois à l'autre.
- Images personnelles : noms plus souples (« chateau », « roi »…, ou n'importe quel nom), et deux
  lignes sous le clavier (bouton X) qui disent quelles images sont affichées et quels fichiers
  l'application voit dans le dossier `decor`.

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
