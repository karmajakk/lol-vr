# Développement

## Organisation du code

- `app/` : l'application Quest (C++, OpenXR, OpenGL ES). `core_host.cpp` fait tourner ScummVM ; `main_xr.cpp` gère le casque, les manettes, les calques et les vibrations.
- `app/src/scene.cpp`, `room.cpp`, `props.cpp`, `proctex.cpp`, `ambience.cpp`, `ui.cpp` : rendu, pièce et objets, textures calculées, son spatialisé, panneaux et clavier.
- `app/test/` : bancs d'essai qui tournent sur PC sans casque.
- `core/` et `patches/` : réglages et correctifs de ScummVM (cœur « libretro » limité à Lands of Lore). Le premier correctif ajoute le pointeur absolu en VR ; le second fait connaître à l'application la zone du jeu et les événements (coups, sons).
- `scripts/build.sh` : construit l'APK.
- `keystore/debug.keystore` : clé de signature de test, publique, qui sert seulement à pouvoir installer les mises à jour par-dessus l'ancienne version.

## Compilation et versions

Chaque envoi de code est compilé automatiquement par GitHub Actions (`.github/workflows/build.yml`).

Pour **publier une version** :

1. Ajouter une section `## [X.Y.Z]` en haut de `CHANGELOG.md`.
2. Mettre le même numéro dans le fichier `VERSION`.
3. Envoyer sur la branche `main` : la compilation publie la version `vX.Y.Z` avec l'APK et les notes du journal.

Numérotation (versionnage sémantique) : le premier chiffre change pour une grande étape, le deuxième pour une nouveauté, le troisième pour une correction. Tant que le numéro commence par 0, le projet est en développement.

## Licence

GPL version 3 ou ultérieure, comme ScummVM (voir `COPYING`). *Lands of Lore* est une marque de ses propriétaires respectifs ; ce projet n'est affilié ni à Westwood, ni à Electronic Arts, ni à l'équipe de ScummVM.
