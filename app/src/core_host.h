// lol-vr — hôte du cœur ScummVM (interface libretro).
// Code indépendant de la plateforme : il fait tourner ScummVM dans son propre fil
// d'exécution et expose l'image, le son et les entrées au reste de l'application.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace lolvr {

struct HostConfig {
	std::string systemDir; // contient scummvm/theme et scummvm/extra (kyra.dat)
	std::string saveDir;   // sauvegardes
	std::string gameDir;   // fichiers du jeu ; vide = lanceur ScummVM
	void (*log)(int level, const char *msg) = nullptr;
};

// Image courante du jeu, convertie en RGBA 8 bits (octets R,G,B,A).
struct Frame {
	std::vector<uint8_t> rgba;
	int width = 0;
	int height = 0;
	uint64_t serial = 0; // augmente à chaque nouvelle image
};

class CoreHost {
public:
	static CoreHost &get();

	bool start(const HostConfig &cfg);
	void stop();
	bool running() const;
	// Vrai quand ScummVM s'est terminé de lui-même (bouton « Quitter »).
	bool finished() const;

	// Copie l'image courante dans `out` si elle est plus récente que out.serial.
	bool fetchFrame(Frame &out);

	// Pointeur absolu : u,v dans [0,1] sur l'image du jeu (0,0 = coin haut-gauche).
	void setPointer(float u, float v);
	void setMouseButtons(bool left, bool right);
	// Touche clavier (codes RETROK_*, identiques aux codes ScummVM/SDL).
	void key(unsigned retroKey, bool down, unsigned retroMods = 0);

	// Boutons spéciaux gérés par ScummVM : clavier virtuel et menu principal (sauver, charger, quitter).
	void setSpecialButtons(bool virtualKeyboard, bool mainMenu);

	// Met le jeu en pause (casque retiré, menu système…) : plus d'images ni de son.
	void setPaused(bool paused);

	// Lit jusqu'à `frames` trames stéréo 16 bits ; complète par du silence.
	void readAudio(int16_t *out, size_t frames);
	int sampleRate() const;

private:
	CoreHost() = default;
};

// Dessine un écran de message (texte UTF-8, lignes séparées par '\n') dans une image.
void renderMessage(Frame &out, const char *title, const char *body);

} // namespace lolvr
