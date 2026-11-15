# Lands of Lore VR (lol-vr)

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) en réalité virtuelle sur Meta Quest 3,
en autonome (sans PC). Le projet s'appuie sur [ScummVM](https://www.scummvm.org), qui fait
tourner le jeu original.

**Les fichiers du jeu ne sont pas fournis.** Il vous faut votre propre exemplaire (version CD).

## Où en est le projet

| Étape | Contenu | État |
|---|---|---|
| 1 | Le jeu complet sur un écran flottant en VR, manettes comme pointeur, son, sauvegardes | en test |
| 2 | Le donjon en vraie 3D autour de vous, déplacement case par case | à venir |
| 3 | Interface pensée pour la VR : carte au poignet, inventaire flottant, sorts sur les gâchettes | à venir |

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
5. Le jeu démarre tout seul dès que les fichiers sont en place.

Les sauvegardes sont dans `Android/data/com.karmajakk.lolvr/files/sauvegardes`.
Attention : désinstaller l'application efface ce dossier. Pour mettre à jour, installez
simplement le nouvel APK par-dessus l'ancien.

## Commandes (étape 1)

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
| Clic du joystick droit | Recentrer l'écran devant soi |
| Clic du joystick gauche maintenu + joystick droit haut/bas | Agrandir ou réduire l'écran |

## Comment c'est fait

- `app/` : l'application Quest (C++, OpenXR, OpenGL ES). `core_host.cpp` fait tourner ScummVM,
  `main_xr.cpp` gère le casque, l'affichage, les manettes et le son.
- `core/` et `patches/` : réglages et correctif appliqués à ScummVM (cœur « libretro », limité
  au moteur de Lands of Lore).
- `scripts/build.sh` : construit l'APK. GitHub le lance automatiquement à chaque modification
  (`.github/workflows/build.yml`) et publie le résultat dans les Releases.
- `keystore/debug.keystore` : clé de signature de test, publique, qui sert seulement à pouvoir
  installer les mises à jour par-dessus l'ancienne version. Elle ne protège rien.

## Licence

GPL version 3 ou ultérieure, comme ScummVM (voir `COPYING`). *Lands of Lore* est une marque de
ses propriétaires respectifs ; ce projet n'est affilié ni à Westwood, ni à Electronic Arts, ni à
l'équipe de ScummVM.
