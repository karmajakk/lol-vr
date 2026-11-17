# Décors et images

Trois décors, que l'on change d'un clic bref du joystick gauche :

- **Bibliothèque médiévale** : une pièce fermée avec cheminée, fenêtre, étagères, tableaux, bannières et reliques sur des socles ; on y est assis à une table de lecture, face à l'écran. La vue par la fenêtre, la lumière, un tableau, les bannières et l'ambiance changent selon la zone du jeu.
- **Réalité mixte** : votre vraie pièce, avec le jeu dans son cadre doré.
- **Moniteur cathodique** : votre vraie pièce, avec le jeu sur un écran bombé des années 90, posé sur un ordinateur 486.

Tout est calculé par le programme : aucun fichier d'image ni de son n'est embarqué.

## Vos images dans la bibliothèque (facultatif)

Copiez des images PNG ou JPEG dans le dossier :

`Android/data/com.karmajakk.lolvr/files/decor`

Seul le **début du nom** compte :

| Nom commençant par | Rôle |
|---|---|
| `fenetre` | la vue par la fenêtre |
| `portrait` | le tableau au-dessus de la cheminée |
| `tableau1` | tableau du mur de gauche |
| `tableau2` | tableau du mur du fond |

L'application les prend en compte quelques secondes après la copie, sans redémarrer. Ces images ne sont pas fournies : comme les fichiers du jeu, chacun met les siennes.

## Son

Le son du jeu vient de l'écran : il reste à sa place quand vous tournez la tête. Le seul son d'ambiance est le feu de cheminée, coupé au départ (clavier X, bouton « Feu »).
