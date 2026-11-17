# Dépannage

**L'écran dit que les fichiers du jeu manquent.**
Normal au premier lancement. Copiez le contenu du CD dans `Android/data/com.karmajakk.lolvr/files/lol` (voir [[Installation]]). Avec la version GOG, ouvrez d'abord `GAME.DAT` avec 7-Zip.

**Mes images (fenêtre, portrait) n'apparaissent pas.**
Ouvrez le clavier (bouton X) et lisez les deux lignes sous les touches : elles disent quelles images sont affichées et quels fichiers l'application voit. « Dossier decor vide » signifie que les fichiers ne sont pas dans `Android/data/com.karmajakk.lolvr/files/decor` ; « Illisible » signifie que le format n'est pas pris en charge (utilisez PNG ou JPEG, pas WebP ni HEIC). Voir [[Décors et images|Decors-et-images]].

**Les fenêtres ou l'écran sont mal placés.**
Clic du joystick droit pour recentrer, ou bouton X → « Tout ranger ».

**Un reflet ou une lumière parasite sur l'image du jeu.**
Signalez-le dans les [Issues](https://github.com/karmajakk/lol-vr/issues) en précisant à quel endroit de l'écran.

**Le pas de côté est difficile.**
Le joystick gauche ne donne qu'une direction à la fois, celle où il penche le plus ; penchez-le franchement de côté plutôt qu'en biais.

**J'ai perdu mes sauvegardes.**
Désinstaller l'application efface le dossier `sauvegardes`. Pour mettre à jour, installez l'APK par-dessus l'ancien sans désinstaller.

**Je n'entends pas le feu.**
Il est coupé par défaut : bouton X → « Feu ».

**Autre problème.**
Ouvrez une [Issue](https://github.com/karmajakk/lol-vr/issues) en indiquant la version (voir la page des [Releases](https://github.com/karmajakk/lol-vr/releases)) et ce que vous faisiez.
