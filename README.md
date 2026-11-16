# Lands of Lore VR (lol-vr)

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) en réalité virtuelle sur Meta Quest 3,
en autonome (sans PC). Le projet s'appuie sur [ScummVM](https://www.scummvm.org), qui fait
tourner le jeu original.

**Les fichiers du jeu ne sont pas fournis.** Il vous faut votre propre exemplaire (version CD).

## Où en est le projet

| Étape | Contenu | État |
|---|---|---|
| 1 | Le jeu complet sur un écran en VR, manettes comme pointeur, son, sauvegardes | fait |
| 2 | Bibliothèque médiévale autour de l'écran (qui change selon la zone du jeu), cadre ouvragé, gants, réalité mixte, moniteur cathodique, vibrations, son spatialisé | en test |
| 3 | Mode fenêtre : vue du donjon en grand, interface au poignet | en test |
| 4 | Le donjon en vraie 3D autour de vous | plus tard |

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
| X | Clavier virtuel (pour nommer une sauvegarde) |
| Bouton menu (manette gauche) | Menu ScummVM : sauvegarder, charger, quitter |
| Clic du joystick droit | Recentrer l'écran (et la pièce) devant soi |
| **Clic bref du joystick gauche** | **Changer de décor** : bibliothèque → réalité mixte avec cadre → réalité mixte avec moniteur cathodique → mode fenêtre |
| Clic du joystick gauche maintenu + joystick droit haut/bas | Agrandir ou réduire l'écran |
| Clic du joystick gauche maintenu + joystick droit gauche/droite | Rapprocher ou éloigner l'écran |

Le décor choisi, la taille et la distance de l'écran sont mémorisés d'une fois à l'autre.
En réalité mixte, « recentrer » pose l'écran là où vous regardez : visez votre bureau pour y
poser le moniteur cathodique.

## Les décors

- **Bibliothèque médiévale** : une pièce fermée avec cheminée, fenêtre, étagères, tableaux et
  bannières. La vue par la fenêtre, la lumière, un tableau, les bannières et l'ambiance sonore
  changent selon la zone du jeu (château, forêt, cavernes, marais, mines, Tour Blanche, Yvel,
  ruines, Cimmeria…).
- **Réalité mixte** : votre vraie pièce, avec le jeu dans son cadre doré.
- **Moniteur cathodique** : votre vraie pièce, avec le jeu sur un écran bombé des années 90
  (lignes de balayage, reflets).

- **Mode fenêtre** : le vide noir, avec la vue du donjon seule, en grand (c'est ce que Gemini
  appelait le mode « immersion »). Pendant que l'on explore, le reste de l'interface passe sur le
  poignet de la main qui ne vise pas : une barre (portraits, barres de vie, objets) sur l'avant-bras,
  et, quand on approche cette main du visage, une tablette avec l'image entière du jeu. On clique
  dessus avec l'autre main. Dans les cinématiques, menus et écrans de personnage, l'image entière
  revient avec son cadre doré.

Les manettes vibrent quand le groupe frappe ou est blessé, à l'ouverture des portes, aux leviers
et aux sorts. Le son du jeu vient de l'écran : il reste à sa place quand vous tournez la tête.

## Comment c'est fait

- `app/` : l'application Quest (C++, OpenXR, OpenGL ES). `core_host.cpp` fait tourner ScummVM,
  `main_xr.cpp` gère le casque, les manettes, les calques et les vibrations. Le décor est
  entièrement calculé par le programme, sans aucun fichier d'image ni de son : `scene.cpp`
  (rendu), `room.cpp` et `props.cpp` (pièce, cadre, moniteur, gants), `proctex.cpp` (textures),
  `ambience.cpp` (son spatialisé et ambiance).
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
