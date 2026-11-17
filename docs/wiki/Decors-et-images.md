# Décors et images

Trois décors, que l'on change d'un clic bref du joystick gauche :

- **Bibliothèque médiévale** : une pièce fermée avec cheminée, fenêtre, étagères, tableaux, bannières et reliques sur des socles ; le milieu de la pièce est libre. La vue par la fenêtre, la lumière, un tableau, les bannières et l'ambiance changent selon la zone du jeu.
- **Réalité mixte** : votre vraie pièce, avec le jeu dans son cadre doré.
- **Moniteur cathodique** : votre vraie pièce, avec le jeu sur un écran bombé des années 90, posé sur un ordinateur 486.

Tout est calculé par le programme : aucun fichier d'image ni de son n'est embarqué.

## Vos images dans la bibliothèque (facultatif)

Copiez des images PNG ou JPEG dans le dossier :

`Android/data/com.karmajakk.lolvr/files/decor`

Le **nom** indique l'emplacement :

| Nom contenant | Rôle |
|---|---|
| `fenetre` (ou `chateau`, `castle`) | la vue par la fenêtre |
| `portrait` (ou `roi`, `king`) | le tableau au-dessus de la cheminée |
| `tableau1` | tableau du mur de gauche |
| `tableau2` | tableau du mur du fond |

Une image au nom quelconque prend la première place libre. L'application les prend en compte quelques secondes après la copie, sans redémarrer.

**Pour vérifier** : ouvrez le clavier (bouton X). Sous les touches, deux lignes indiquent quelles images sont affichées et quels fichiers l'application voit dans le dossier `decor`. Ces images ne sont pas fournies : comme les fichiers du jeu, chacun met les siennes.

## Son

Le son du jeu vient de l'écran : il reste à sa place quand vous tournez la tête. Le seul son d'ambiance est le feu de cheminée, coupé au départ (clavier X, bouton « Feu »).
