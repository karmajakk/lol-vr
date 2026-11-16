// lol-vr — rendu du décor 3D autour de l'écran du jeu (OpenGL ES 3, indépendant de la plateforme).
#pragma once

#include "vecmath.h"

namespace lolvr {

enum Environment {
	kEnvLibrary = 0,          // bibliothèque médiévale, écran dans un cadre ouvragé
	kEnvPassthroughFrame = 1, // la vraie pièce du joueur, écran dans le cadre
	kEnvPassthroughCrt = 2,   // la vraie pièce du joueur, moniteur cathodique
	kEnvCount
};

struct HandState {
	bool visible = false;
	Vec3 pos = {0, 0, 0}; // pose de visée de la manette
	Quat rot = {0, 0, 0, 1};
	float trigger = 0.0f;
	float grip = 0.0f;
	bool pointing = false; // cette main dirige le pointeur
};

struct SceneState {
	int environment = kEnvLibrary;
	int theme = 0;
	double time = 0.0;

	// Écran du jeu : centre, orientation, largeur de l'image en mètres (hauteur = 3/4).
	Vec3 screenPos = {0, 0, -2.2f};
	Quat screenRot = {0, 0, 0, 1};
	float screenWidth = 2.4f;

	// Origine de la pièce (au sol, sous le joueur) et son orientation.
	Vec3 roomPos = {0, -1.2f, 0};
	float roomYaw = 0.0f;

	HandState hands[2];
	bool hitValid = false;
	Vec3 hit = {0, 0, 0};
	float clickPulse = 0.0f; // 1 au moment du clic, retombe vers 0

	Vec3 screenGlow = {0.1f, 0.1f, 0.1f}; // couleur moyenne de l'image du jeu (linéaire)
	unsigned gameTexture = 0;             // texture OpenGL de l'image du jeu (mode cathodique)
	int gameWidth = 320, gameHeight = 200;

	// À l'essai (désactivé dans l'application) : bandeau d'inventaire sur l'avant-bras gauche,
	// qui reprend en direct la barre d'objets du bas de l'écran.
	bool wristPanel = false;
};

class Scene {
public:
	Scene();
	~Scene();

	// srgbTarget : vrai si l'image de destination est au format sRGB.
	bool init(bool srgbTarget);
	// À appeler une fois par image, avant render() : thèmes, objets animés.
	void prepare(const SceneState &state);
	// Dessine une vue. Le tampon de destination (couleur + profondeur) doit être lié et le
	// viewport réglé ; la fonction l'efface elle-même. Le résultat est en alpha prémultiplié :
	// alpha 0 = transparent (image du jeu en dessous, ou passthrough).
	void render(const Mat4 &view, const Mat4 &proj, const SceneState &state);

	// Vrai quand l'image du jeu doit être affichée par un calque « quad » sous le décor.
	static bool usesQuadLayer(int environment) { return environment != kEnvPassthroughCrt; }
	// Vrai quand les textures sont prêtes (le décor apparaît alors en fondu).
	bool ready() const;

private:
	struct Impl;
	Impl *d;
};

} // namespace lolvr
