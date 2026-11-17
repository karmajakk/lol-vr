# Lands of Lore VR (lol-vr)

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) en réalité virtuelle sur Meta Quest 3,
en autonome (sans PC). Le projet s'appuie sur [ScummVM](https://www.scummvm.org), qui fait
tourner le jeu original.

**Les fichiers du jeu ne sont pas fournis.** Il vous faut votre propre exemplaire (version CD).

## Où en est le projet

Détail des versions : [CHANGELOG.md](CHANGELOG.md).

| Version | Contenu | État |
|---|---|---|
| 0.1 | Le jeu complet sur un écran flottant en VR, manettes comme pointeur, son, sauvegardes | fait |
| 0.2 | Bibliothèque médiévale (change selon la zone du jeu), cadre, gants, réalité mixte, moniteur cathodique, vibrations, son spatialisé | fait |
| 0.3 | Mode fenêtre (donjon agrandi, interface au poignet) | abandonné |
| 0.4 – 0.5 | Interface détachée en panneaux déplaçables, clavier, écran déplaçable, ordinateur 486, images personnelles, corrections | en test |
| 1.0 | Le donjon en vraie 3D autour de vous | plus tard |

## Installation (sans rien compiler)

1. Téléchargez `lol-vr.apk` depuis la page
   [Releases](https://github.com/karmajakk/lol-vr/releases) (prenez la plus récente).
2. Branchez le casque au PC, ouvrez SideQuest et installez l'APK
   (icône « Install APK file from folder »).
3. Dans le casque, lancez **Lands of Lore VR** (Bibliothèque → filtre « Sources inconnues »).
   Un écran vous indique que les fichiers du jeu manquent : c'est normal. Ce premier lancement
   crée le dossier où les copier.
4. Sur le PC, copiez **tout le contenu du CD** du jeu (les fichiers `.PAK`, dont `GENERAL.PAK`)
   dans ce dossier du casque :

   `Android/data/com.karmajakk.lolvr/files/lol`

   (avec le gestionnaire de fichiers de SideQuest, ou l'Explorateur Windows une fois le casque
   branché et l'accès aux fichiers autorisé).

   **Version GOG :** les fichiers `.PAK` sont enfermés dans `GAME.DAT`, une image du CD.
   Ouvrez `GAME.DAT` avec [7-Zip](https://www.7-zip.org) et copiez le contenu de son dossier
   `DATA` (y compris le sous-dossier `ENG`).
5. Le jeu démarre tout seul dès que les fichiers sont en place.

Les sauvegardes sont dans `Android/data/com.karmajakk.lolvr/files/sauvegardes`.
Attention : désinstaller l'application efface ce dossier. Pour mettre à jour, installez
simplement le nouvel APK par-dessus l'ancien.

## Commandes

| Manette | Action |
|---|---|
| Viser + gâchette | Clic gauche (la main qui clique devient le pointeur) |
| Viser + poignée | Clic droit |
| Joystick gauche | Avancer, reculer, pas de côté |
| Joystick droit (gauche/droite) | Tourner |
| A | Entrée |
| B | Échap (passer une cinématique) |
| Y | Carte |
| **X** | **Clavier et options** : taper un nom de sauvegarde, couper ou remettre le son du feu, détacher ou non l'interface, tout ranger |
| Bouton menu (manette gauche) | Menu ScummVM : sauvegarder, charger, quitter |
| Clic du joystick droit | Recentrer l'écran, la pièce et les panneaux devant soi |
| Clic bref du joystick gauche | Changer de décor : bibliothèque → réalité mixte avec cadre → réalité mixte avec moniteur cathodique |
| Clic du joystick gauche maintenu + joystick droit | Haut/bas : taille de l'écran ; gauche/droite : distance |

### Déplacer l'écran et les panneaux

L'écran et chaque panneau portent une **barrette de laiton** au-dessus d'eux. Dès que le doigt
quitte l'écran et les panneaux, le rayon du pointeur s'allonge pour montrer où l'on vise.
Visez la barrette, serrez la gâchette et gardez-la serrée : l'objet suit votre doigt. Pendant que vous le tenez, le
joystick droit le pousse ou le tire (haut/bas) et le rapetisse ou l'agrandit (gauche/droite).
Relâchez la gâchette pour le poser. Tout est mémorisé d'une fois à l'autre.

## L'interface détachée

Pendant l'exploration, l'écran ne montre que la vue du donjon. Le reste de l'interface du jeu est
réparti sur des fenêtres que l'on place où l'on veut : le groupe (messages et portraits),
l'inventaire, le parchemin des sorts, les commandes (flèches, options, repos) et la boussole.
Au départ, elles sont toutes dans le plan de l'écran, autour de la vue : le pointeur passe de
l'une à l'autre sans décalage, ce qui permet de ramasser un objet au sol et de le déposer dans
l'inventaire. Elles suivent l'écran quand on le déplace ou qu'on change sa taille. Dans la
bibliothèque, elles ne traversent ni le plancher, ni les murs, ni la table.
Dans les cinématiques, les menus, la carte et les scènes qui occupent toute la largeur, l'image
entière revient d'elle-même.

Le bouton « Panneaux » du clavier (X) rend l'image entière en permanence, et « Tout ranger »
remet les panneaux à leur place d'origine autour de l'écran.

## Les décors

- **Bibliothèque médiévale** : une pièce fermée avec cheminée, fenêtre, étagères, tableaux et
  bannières ; on y est assis à une table de lecture, face à l'écran. La vue par la fenêtre, la
  lumière, un tableau, les bannières et l'ambiance sonore changent selon la zone du jeu.
- **Réalité mixte** : votre vraie pièce, avec le jeu dans son cadre doré.
- **Moniteur cathodique** : votre vraie pièce, avec le jeu sur un écran bombé des années 90 posé
  sur son ordinateur de bureau.

### Vos images dans la bibliothèque (facultatif)

Des images peuvent être ajoutées dans le dossier `Android/data/com.karmajakk.lolvr/files/decor`
du casque (PNG ou JPEG ; seul le début du nom compte) :

- `fenetre.png` : la vue par la fenêtre ;
- `portrait.png` : le tableau au-dessus de la cheminée ;
- `tableau1.png`, `tableau2.png` : deux autres tableaux (mur de gauche, mur du fond).

L'application les prend en compte quelques secondes après leur copie, sans redémarrer.

Ces images ne sont pas fournies avec l'application : comme les fichiers du jeu, chacun y met les siennes.

Les manettes vibrent quand le groupe frappe ou est blessé, à l'ouverture des portes, aux leviers
et aux sorts. Le son du jeu vient de l'écran : il reste à sa place quand vous tournez la tête.
Le seul son d'ambiance est le feu de cheminée, coupé au départ (clavier, bouton « Feu »).

## Comment c'est fait

- `app/` : l'application Quest (C++, OpenXR, OpenGL ES). `core_host.cpp` fait tourner ScummVM,
  `main_xr.cpp` gère le casque, les manettes, les calques et les vibrations. Le décor est
  entièrement calculé par le programme, sans aucun fichier d'image ni de son : `scene.cpp`
  (rendu), `room.cpp` et `props.cpp` (pièce, cadre, moniteur, gants), `proctex.cpp` (textures),
  `ambience.cpp` (son spatialisé et ambiance), `ui.cpp` (panneaux, poignées, clavier, pointeur).
- `app/test/` : bancs d'essai qui tournent sur PC sans casque (images du décor, son, cœur du jeu).
- `core/` et `patches/` : réglages et correctifs appliqués à ScummVM (cœur « libretro », limité
  au moteur de Lands of Lore). Le second correctif fait connaître à l'application la zone du jeu
  et les événements (coups, sons) pour le décor et les vibrations.
- `scripts/build.sh` : construit l'APK. GitHub le lance automatiquement à chaque modification
  (`.github/workflows/build.yml`) et publie le résultat dans les Releases.
- `keystore/debug.keystore` : clé de signature de test, publique, qui sert seulement à pouvoir
  installer les mises à jour par-dessus l'ancienne version. Elle ne protège rien.

## Licence

GPL version 3 ou ultérieure, comme ScummVM (voir `COPYING`). *Lands of Lore* est une marque de
ses propriétaires respectifs ; ce projet n'est affilié ni à Westwood, ni à Electronic Arts, ni à
l'équipe de ScummVM.
