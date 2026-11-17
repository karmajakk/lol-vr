# Installation

Aucune compilation n'est nécessaire.

## 1. Installer l'application

1. Téléchargez `lol-vr.apk` depuis la [dernière version](https://github.com/karmajakk/lol-vr/releases/latest).
2. Branchez le casque au PC, ouvrez [SideQuest](https://sidequestvr.com) et installez l'APK (icône « Install APK file from folder »).
3. Dans le casque : Bibliothèque → filtre « Sources inconnues » → **Lands of Lore VR**.

Au premier lancement, un écran indique que les fichiers du jeu manquent : c'est normal. Ce lancement crée le dossier où les copier.

## 2. Copier les fichiers du jeu

Copiez **tout le contenu du CD** du jeu (les fichiers `.PAK`, dont `GENERAL.PAK`) dans ce dossier du casque :

`Android/data/com.karmajakk.lolvr/files/lol`

Avec le gestionnaire de fichiers de SideQuest, ou l'Explorateur Windows une fois le casque branché et l'accès aux fichiers autorisé.

**Version GOG** : les `.PAK` sont enfermés dans `GAME.DAT`, une image du CD. Ouvrez `GAME.DAT` avec [7-Zip](https://www.7-zip.org) et copiez le contenu de son dossier `DATA` (y compris le sous-dossier `ENG`).

Le jeu démarre tout seul dès que les fichiers sont en place.

## Mettre à jour

Installez simplement le nouvel APK **par-dessus** l'ancien : les fichiers du jeu et les sauvegardes sont conservés.

## Sauvegardes

Elles sont dans `Android/data/com.karmajakk.lolvr/files/sauvegardes`.

**Attention : désinstaller l'application efface ce dossier.** Copiez-le sur le PC avant de désinstaller.
