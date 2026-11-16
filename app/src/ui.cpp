// lol-vr — interface détachée : panneaux, poignées, clavier et pointeur.

#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <GLES3/gl3.h>

#include "font8x16.h"
#include "room.h"

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

namespace lolvr {

namespace {

// --- Zones de l'écran du jeu (pixels, image 320x200) ------------------------------------------

struct Source {
	float x, y, w, h;
};

// Clavier : grille de 11 colonnes sur 6 rangées.
const int kCellW = 46, kCellH = 40, kCols = 11, kRows = 6;
const int kKbW = kCellW * kCols, kKbH = kCellH * kRows;

const Source kSources[kUiPanelCount] = {
	{83, 121, 237, 58},  // groupe : ligne de messages + portraits
	{83, 178, 237, 22},  // inventaire
	{0, 0, 112, 128},    // colonne de gauche (parchemin des sorts)
	{0, 128, 84, 72},    // flèches, options, repos
	{288, 0, 32, 122},   // colonne de droite (boussole, atlas, lampe, bourse)
	{0, 0, 320, 200},    // atlas : image entière de la carte
	{0, 0, (float)kKbW, (float)kKbH},
};

// Les pixels du jeu sont 1,2 fois plus hauts que larges ; ceux du clavier sont carrés.
float pixelAspect(int id) {
	return id == kPanelKeyboard ? 1.0f : 1.2f;
}

struct Key {
	int col, row, span;
	std::string label, shiftLabel;
	unsigned key; // code ASCII envoyé au jeu, 0 pour une touche spéciale
	int action;   // UiAction, ou -1 pour la touche Maj
};

struct Quad {
	Vec3 c = {0, 0, 0}, r = {1, 0, 0}, u = {0, 1, 0};
	float hw = 0.1f, hh = 0.1f;
};

// Intersection d'un rayon avec un rectangle. x, y : position en demi-dimensions (-1..1 à l'intérieur).
bool rayQuad(Vec3 origin, Vec3 dir, const Quad &q, float &t, float &x, float &y) {
	Vec3 n = cross(q.r, q.u);
	float denom = dot(dir, n);
	if (fabsf(denom) < 1e-5f)
		return false;
	t = dot(q.c - origin, n) / denom;
	if (t <= 0.0f)
		return false;
	Vec3 dlt = origin + dir * t - q.c;
	x = dot(dlt, q.r) / q.hw;
	y = dot(dlt, q.u) / q.hh;
	return true;
}

float wrapAngle(float a) {
	const float pi = 3.14159265f;
	while (a > pi)
		a -= 2.0f * pi;
	while (a < -pi)
		a += 2.0f * pi;
	return a;
}

Vec3 directionFor(float yaw, float pitch) {
	return {-sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch)};
}

// --- Dessin du clavier --------------------------------------------------------------------------

struct Canvas {
	int w, h;
	std::vector<uint8_t> px;
	void rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
		for (int y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
			for (int x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) {
				uint8_t *p = &px[((size_t)y * w + x) * 4];
				p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
			}
	}
	void text(const std::string &s, int x, int y, int scale, uint8_t r, uint8_t g, uint8_t b) {
		for (size_t i = 0; i < s.size(); i++) {
			unsigned ch = (unsigned char)s[i];
			if (ch < 32)
				continue;
			const uint8_t *glyph = kFont8x16[ch - 32];
			for (int gy = 0; gy < 16; gy++)
				for (int gx = 0; gx < 8; gx++)
					if (glyph[gy] & (0x80 >> gx))
						rect(x + ((int)i * 8 + gx) * scale, y + gy * scale, x + ((int)i * 8 + gx + 1) * scale, y + (gy + 1) * scale, r, g, b);
		}
	}
};

} // namespace

struct Ui::Impl {
	std::vector<Key> keys;
	bool shift = false;
	bool fireOn = false, detached = true;
	GLuint kbTex = 0;
	bool kbDirty = true;
	bool srgb = true;
	int hotKey = -1;
	float pressFlash = 0.0f;
	int pressKey = -1;

	// Poignée tenue : -1 aucune, 0..kUiPanelCount-1 un panneau, kScreenTarget l'écran.
	static const int kScreenTarget = 100;
	int drag = -1;
	float grabDist = 1.0f;
	Vec3 grabOffset = {0, 0, 0};

	void buildKeys();
	void renderKeyboard();
};

void Ui::Impl::buildKeys() {
	keys.clear();
	keys.push_back({0, 0, 3, "", "", 0, kUiActionToggleFire});
	keys.push_back({3, 0, 3, "", "", 0, kUiActionToggleDetach});
	keys.push_back({6, 0, 3, "Tout ranger", "", 0, kUiActionResetLayout});
	keys.push_back({9, 0, 2, "Fermer", "", 0, kUiActionCloseKeyboard});
	const char *digits = "1234567890";
	for (int i = 0; i < 10; i++)
		keys.push_back({i, 1, 1, std::string(1, digits[i]), "", (unsigned)digits[i], kUiActionNone});
	keys.push_back({10, 1, 1, "<-", "", 8, kUiActionNone});
	const char *rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
	for (int i = 0; i < 10; i++)
		keys.push_back({i, 2, 1, std::string(1, rows[0][i]), std::string(1, (char)(rows[0][i] - 32)), (unsigned)rows[0][i], kUiActionNone});
	keys.push_back({10, 2, 1, "-", "", '-', kUiActionNone});
	for (int i = 0; i < 9; i++)
		keys.push_back({i, 3, 1, std::string(1, rows[1][i]), std::string(1, (char)(rows[1][i] - 32)), (unsigned)rows[1][i], kUiActionNone});
	keys.push_back({9, 3, 1, "'", "", '\'', kUiActionNone});
	keys.push_back({10, 3, 1, "!", "", '!', kUiActionNone});
	keys.push_back({0, 4, 1, "Maj", "MAJ", 0, -1});
	for (int i = 0; i < 7; i++)
		keys.push_back({1 + i, 4, 1, std::string(1, rows[2][i]), std::string(1, (char)(rows[2][i] - 32)), (unsigned)rows[2][i], kUiActionNone});
	keys.push_back({8, 4, 1, ",", "", ',', kUiActionNone});
	keys.push_back({9, 4, 1, ".", "", '.', kUiActionNone});
	keys.push_back({10, 4, 1, "?", "", '?', kUiActionNone});
	keys.push_back({0, 5, 2, "\xC9" "chap", "", 27, kUiActionNone});
	keys.push_back({2, 5, 6, "Espace", "", ' ', kUiActionNone});
	keys.push_back({8, 5, 3, "Entr\xE9" "e", "", 13, kUiActionNone});
}

void Ui::Impl::renderKeyboard() {
	Canvas cv{kKbW, kKbH, std::vector<uint8_t>((size_t)kKbW * kKbH * 4)};
	cv.rect(0, 0, kKbW, kKbH, 34, 24, 16);
	keys[0].label = fireOn ? "Feu : oui" : "Feu : non";
	keys[1].label = detached ? "Panneaux : oui" : "Panneaux : non";
	for (size_t i = 0; i < keys.size(); i++) {
		const Key &k = keys[i];
		int x0 = k.col * kCellW + 2, y0 = k.row * kCellH + 2, x1 = (k.col + k.span) * kCellW - 2, y1 = (k.row + 1) * kCellH - 2;
		bool option = k.row == 0;
		bool lit = (k.action == -1 && shift) || (k.action == kUiActionToggleFire && fireOn) || (k.action == kUiActionToggleDetach && detached);
		// Touche en relief : bord clair en haut, sombre en bas
		cv.rect(x0, y0, x1, y1, 30, 20, 12);
		if (lit)
			cv.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 150, 104, 40);
		else if (option)
			cv.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 74, 60, 70);
		else
			cv.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 96, 72, 46);
		cv.rect(x0 + 1, y0 + 1, x1 - 1, y0 + 3, lit ? 220 : 140, lit ? 170 : 108, lit ? 80 : 72);
		cv.rect(x0 + 1, y1 - 3, x1 - 1, y1 - 1, 52, 38, 24);
		const std::string &label = (shift && !k.shiftLabel.empty()) ? k.shiftLabel : k.label;
		int scale = label.size() == 1 ? 2 : 1;
		int tw = (int)label.size() * 8 * scale, th = 16 * scale;
		cv.text(label, (x0 + x1 - tw) / 2, (y0 + y1 - th) / 2 - (scale == 2 ? 2 : 0), scale, 250, 236, 200);
	}
	if (!kbTex)
		glGenTextures(1, &kbTex);
	glBindTexture(GL_TEXTURE_2D, kbTex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, kKbW, kKbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, cv.px.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	kbDirty = false;
}

Ui::Ui() : d(new Impl()) {
	d->buildKeys();
}

Ui::~Ui() {
	delete d;
}

const char *Ui::panelKey(int id) {
	static const char *names[kUiPanelCount] = {"groupe", "inventaire", "sorts", "commandes", "boussole", "atlas", "clavier"};
	return id >= 0 && id < kUiPanelCount ? names[id] : "";
}

void Ui::initGl(bool srgb) {
	d->srgb = srgb;
	d->kbDirty = true;
}

void Ui::setOptions(bool fireOn, bool detached) {
	if (fireOn != d->fireOn || detached != d->detached)
		d->kbDirty = true;
	d->fireOn = fireOn;
	d->detached = detached;
}

void Ui::resetLayout(const UiFrameInput &in) {
	const float deg = 3.14159265f / 180.0f;
	Vec3 v = in.screenPos - in.anchorPos;
	float dh = sqrtf(v.x * v.x + v.z * v.z);
	if (dh < 0.3f)
		dh = 0.3f;
	float screenYaw = wrapAngle(atan2f(-v.x, -v.z) - in.anchorYaw);
	float centerPitch = atan2f(v.y, dh);
	// Bas du cadre et bords latéraux, vus du point d'ancrage
	float bottomPitch = atan2f(v.y - in.dispH * 0.5f - 0.10f * in.dispW, dh);
	float sideYaw = atan2f(in.dispW * 0.60f, dh);
	auto halfAngle = [](float halfSize, float dist) { return atan2f(halfSize, dist); };
	auto halfW = [](int id, float scale) { return kSources[id].w * scale * 0.5f; };
	auto halfH = [](int id, float scale) { return kSources[id].h * pixelAspect(id) * scale * 0.5f; };

	// Sous l'écran, à portée de main, inclinés vers le visage : le groupe puis l'inventaire.
	UiPlacement &party = placement[kPanelParty];
	party.dist = 0.70f;
	party.scale = 0.0021f;
	party.yaw = screenYaw;
	party.pitch = bottomPitch - 2.0f * deg - halfAngle(halfH(kPanelParty, party.scale), party.dist);
	if (party.pitch < -50.0f * deg)
		party.pitch = -50.0f * deg;
	UiPlacement &inv = placement[kPanelInventory];
	inv.dist = 0.66f;
	inv.scale = 0.0020f;
	inv.yaw = screenYaw;
	inv.pitch = party.pitch - halfAngle(halfH(kPanelParty, party.scale), party.dist) - 3.4f * deg - halfAngle(halfH(kPanelInventory, inv.scale), inv.dist);

	// À gauche de l'écran : le parchemin des sorts, et dessous les commandes.
	UiPlacement &spells = placement[kPanelSpells];
	spells.dist = 0.95f;
	spells.scale = 0.0026f;
	spells.yaw = screenYaw + sideYaw + 6.0f * deg + halfAngle(halfW(kPanelSpells, spells.scale), spells.dist);
	spells.pitch = centerPitch - 3.0f * deg;
	UiPlacement &ctl = placement[kPanelControls];
	ctl.dist = 0.95f;
	ctl.scale = 0.0026f;
	ctl.yaw = spells.yaw;
	ctl.pitch = spells.pitch - halfAngle(halfH(kPanelSpells, spells.scale), spells.dist) - 3.5f * deg - halfAngle(halfH(kPanelControls, ctl.scale), ctl.dist);

	// À droite : la boussole, et dessous l'atlas.
	UiPlacement &compass = placement[kPanelCompass];
	compass.dist = 0.95f;
	compass.scale = 0.0034f;
	compass.yaw = screenYaw - sideYaw - 6.0f * deg - halfAngle(halfW(kPanelCompass, compass.scale), compass.dist);
	compass.pitch = centerPitch - 3.0f * deg;
	UiPlacement &atlas = placement[kPanelAtlas];
	atlas.dist = 0.85f;
	atlas.scale = 0.0015f;
	atlas.yaw = compass.yaw - 10.0f * deg;
	atlas.pitch = compass.pitch - halfAngle(halfH(kPanelCompass, compass.scale), compass.dist) - 3.5f * deg - halfAngle(halfH(kPanelAtlas, atlas.scale), atlas.dist);

	// Le clavier vient se poser devant soi, par-dessus le reste.
	UiPlacement &kb = placement[kPanelKeyboard];
	kb.dist = 0.58f;
	kb.scale = 0.0011f;
	kb.yaw = screenYaw;
	kb.pitch = -29.0f * deg;
	placed = true;
}

void Ui::update(const UiFrameInput &in, UiFrameOutput &out, SceneState &scene) {
	out = UiFrameOutput();
	for (int i = 0; i < SceneState::kMaxPanels; i++)
		scene.panels[i].visible = false;
	for (int i = 0; i < SceneState::kMaxHandles; i++)
		scene.handles[i].visible = false;
	if (d->kbDirty)
		d->renderKeyboard();
	d->pressFlash -= in.dt;

	// Tant que la disposition n'est pas établie (écran pas encore placé), aucun panneau.
	bool visible[kUiPanelCount];
	for (int i = 0; i <= kPanelCompass; i++)
		visible[i] = placed && in.split;
	visible[kPanelAtlas] = placed && in.atlasVisible && in.atlasTexture != 0;
	visible[kPanelKeyboard] = placed && in.keyboardOpen;

	Vec3 screenR = rotate(in.screenRot, {1, 0, 0}), screenU = rotate(in.screenRot, {0, 1, 0}), screenN = rotate(in.screenRot, {0, 0, 1});
	float screenDist = length(in.screenPos - in.anchorPos);
	if (screenDist < 0.3f)
		screenDist = 0.3f;

	// --- 1. Poignée tenue : l'objet suit le rayon du doigt ---
	if (d->drag != -1) {
		bool gone = d->drag != Impl::kScreenTarget && !visible[d->drag];
		if (!in.triggerDown || !in.aimValid || gone) {
			d->drag = -1;
			out.released = true;
			out.layoutChanged = true;
		} else {
			d->grabDist = clampf(d->grabDist * expf(in.stickY * 1.3f * in.dt), 0.20f, 6.0f);
			Vec3 c = in.tip + in.dir * d->grabDist + d->grabOffset;
			out.dragging = true;
			if (d->drag == Impl::kScreenTarget) {
				out.screenDragging = true;
				out.screenCenter = c;
				out.screenZoom = expf(in.stickX * 0.9f * in.dt);
			} else {
				UiPlacement &pl = placement[d->drag];
				Vec3 v = c - in.anchorPos;
				pl.dist = clampf(length(v), 0.20f, 6.0f);
				pl.pitch = clampf(asinf(clampf(v.y / pl.dist, -1.0f, 1.0f)), -1.35f, 1.35f);
				pl.yaw = wrapAngle(atan2f(-v.x, -v.z) - in.anchorYaw);
				pl.scale = clampf(pl.scale * expf(in.stickX * 0.9f * in.dt), 0.0005f, 0.02f);
			}
		}
	}

	// --- 2. Pose des panneaux et de leurs poignées ---
	Quad content[kUiPanelCount], handle[kUiPanelCount + 1];
	for (int i = 0; i < kUiPanelCount; i++) {
		const UiPlacement &pl = placement[i];
		Vec3 dir = directionFor(in.anchorYaw + pl.yaw, pl.pitch);
		Quad &q = content[i];
		q.c = in.anchorPos + dir * pl.dist;
		Vec3 n = dir * -1.0f;
		Vec3 r = cross({0, 1, 0}, n);
		float rl = length(r);
		q.r = rl > 1e-4f ? r * (1.0f / rl) : Vec3{1, 0, 0};
		q.u = cross(n, q.r);
		q.hw = kSources[i].w * pl.scale * 0.5f;
		q.hh = kSources[i].h * pixelAspect(i) * pl.scale * 0.5f;
		Quad &hq = handle[i];
		float border = clampf(0.012f * pl.dist, 0.004f, 0.012f);
		hq.hh = clampf(0.013f * pl.dist, 0.006f, 0.05f);
		hq.hw = fminf(q.hw, clampf(0.060f * pl.dist, 0.02f, 0.25f));
		hq.r = q.r;
		hq.u = q.u;
		hq.c = q.c + q.u * (q.hh + border + hq.hh + 0.002f);
	}
	// Poignée de l'écran : au-dessus du cadre (ou du moniteur)
	{
		Quad &hq = handle[kUiPanelCount];
		hq.r = screenR;
		hq.u = screenU;
		hq.hh = clampf(0.011f * screenDist, 0.006f, 0.05f);
		hq.hw = clampf(0.055f * screenDist, 0.02f, 0.25f);
		float top = in.crt ? 0.50f * in.dispW : in.dispH * 0.5f + 0.165f * in.dispW;
		hq.c = in.screenPos + screenU * (top + hq.hh) + screenN * (in.crt ? -0.02f * in.dispW : 0.02f * in.dispW);
	}

	// --- 3. Que vise le doigt ? ---
	enum { kHitNone, kHitHandle, kHitPanel, kHitScreen };
	int hitKind = kHitNone, hitIndex = -1;
	float bestT = 1e9f, hx = 0, hy = 0;
	d->hotKey = -1;
	if (in.aimValid && d->drag == -1) {
		for (int i = 0; i <= kUiPanelCount; i++) {
			if (i < kUiPanelCount && !visible[i])
				continue;
			float t, x, y;
			// Zone de prise un peu plus généreuse que la poignée dessinée
			if (rayQuad(in.tip, in.dir, handle[i], t, x, y) && t < bestT && fabsf(x) <= 1.25f && fabsf(y) <= 1.9f) {
				bestT = t; hitKind = kHitHandle; hitIndex = i; hx = x; hy = y;
			}
		}
		for (int i = 0; i < kUiPanelCount; i++) {
			if (!visible[i])
				continue;
			float t, x, y;
			if (rayQuad(in.tip, in.dir, content[i], t, x, y) && t < bestT && fabsf(x) <= 1.04f && fabsf(y) <= 1.04f) {
				bestT = t; hitKind = kHitPanel; hitIndex = i; hx = clampf(x, -1.0f, 1.0f); hy = clampf(y, -1.0f, 1.0f);
			}
		}
		// L'écran principal (la dalle du moniteur cathodique est bombée : on affine le point d'impact)
		{
			Quat inv = conjugate(in.screenRot);
			Vec3 origin = rotate(inv, in.tip - in.screenPos);
			Vec3 dir = rotate(inv, in.dir);
			if (dir.z < -1e-4f && origin.z > 0.0f) {
				float zPlane = 0.0f, t = 0.0f;
				Vec3 hit = origin;
				for (int it = 0; it < (in.crt ? 3 : 1); it++) {
					t = (zPlane - origin.z) / dir.z;
					hit = origin + dir * t;
					if (in.crt)
						zPlane = in.dispW * crtGlassZ(hit.x / in.dispW, hit.y / in.dispW);
				}
				float u = hit.x / in.dispW + 0.5f, v = 0.5f - hit.y / in.dispH;
				if (t < bestT && u > -0.12f && u < 1.12f && v > -0.12f && v < 1.12f) {
					bestT = t; hitKind = kHitScreen; hitIndex = 0;
					u = clampf(u, 0.0f, 1.0f);
					v = clampf(v, 0.0f, 1.0f);
					out.gu = (in.cropX + in.cropW * u) / 320.0f;
					out.gv = (in.cropY + in.cropH * v) / 200.0f;
					Vec3 local{(u - 0.5f) * in.dispW, (0.5f - v) * in.dispH, zPlane + 0.004f};
					out.hit = rotate(in.screenRot, local) + in.screenPos;
				}
			}
		}
	}

	float handleGlow[kUiPanelCount + 1];
	for (int i = 0; i <= kUiPanelCount; i++)
		handleGlow[i] = 0.0f;
	if (d->drag != -1)
		handleGlow[d->drag == Impl::kScreenTarget ? kUiPanelCount : d->drag] = 1.0f;

	if (hitKind == kHitScreen) {
		out.pointerValid = true;
		out.buttonsToGame = true;
		out.hitValid = true;
		out.hitRadius = 0.0f;
	} else if (hitKind == kHitHandle) {
		handleGlow[hitIndex] = 0.7f;
		const Quad &hq = handle[hitIndex];
		Vec3 p = in.tip + in.dir * bestT;
		out.hitValid = true;
		out.hit = p;
		out.hitRight = hq.r;
		out.hitUp = hq.u;
		out.hitRadius = 0.010f * bestT;
		if (in.triggerPressed) {
			d->drag = hitIndex == kUiPanelCount ? Impl::kScreenTarget : hitIndex;
			d->grabDist = bestT;
			d->grabOffset = (hitIndex == kUiPanelCount ? in.screenPos : content[hitIndex].c) - p;
			handleGlow[hitIndex] = 1.0f;
			out.grabbed = true;
			out.dragging = true;
			if (d->drag == Impl::kScreenTarget) {
				out.screenDragging = true;
				out.screenCenter = in.screenPos;
			}
		}
		(void)hx;
		(void)hy;
	} else if (hitKind == kHitPanel) {
		const Quad &q = content[hitIndex];
		out.hitValid = true;
		out.hit = q.c + q.r * (hx * q.hw) + q.u * (hy * q.hh) + cross(q.r, q.u) * 0.001f;
		out.hitRight = q.r;
		out.hitUp = q.u;
		out.hitRadius = 0.010f * bestT;
		float u = (hx + 1.0f) * 0.5f, v = (1.0f - hy) * 0.5f;
		if (hitIndex == kPanelKeyboard) {
			int col = (int)(u * (float)kCols), row = (int)(v * (float)kRows);
			for (size_t k = 0; k < d->keys.size(); k++) {
				const Key &key = d->keys[k];
				if (key.row == row && col >= key.col && col < key.col + key.span)
					d->hotKey = (int)k;
			}
			if (d->hotKey >= 0 && in.triggerPressed) {
				const Key &key = d->keys[d->hotKey];
				out.clicked = true;
				d->pressKey = d->hotKey;
				d->pressFlash = 0.15f;
				if (key.action == -1) {
					d->shift = !d->shift;
					d->kbDirty = true;
				} else if (key.action != kUiActionNone) {
					out.action = key.action;
				} else if (key.key && out.keyCount < 4) {
					bool letter = key.key >= 'a' && key.key <= 'z';
					out.keys[out.keyCount] = key.key;
					out.keyMods[out.keyCount] = (letter && d->shift) ? kUiModShift : 0;
					out.keyCount++;
					if (d->shift) {
						d->shift = false;
						d->kbDirty = true;
					}
				}
			}
		} else if (hitIndex == kPanelAtlas) {
			if (in.atlasLive) {
				out.pointerValid = true;
				out.buttonsToGame = true;
				out.gu = u;
				out.gv = v;
			}
		} else {
			const Source &src = kSources[hitIndex];
			out.pointerValid = true;
			out.buttonsToGame = true;
			out.gu = (src.x + u * src.w) / 320.0f;
			out.gv = (src.y + v * src.h) / 200.0f;
		}
	}

	// --- 4. Ce que la scène doit dessiner ---
	int np = 0, nh = 0;
	for (int i = 0; i < kUiPanelCount; i++) {
		if (!visible[i])
			continue;
		const Quad &q = content[i];
		const Source &src = kSources[i];
		if (np < SceneState::kMaxPanels) {
			SceneState::Panel &p = scene.panels[np++];
			p = SceneState::Panel();
			p.visible = true;
			p.center = q.c;
			p.right = q.r;
			p.up = q.u;
			p.halfW = q.hw;
			p.halfH = q.hh;
			p.border = clampf(0.012f * placement[i].dist, 0.004f, 0.012f);
			if (i == kPanelKeyboard) {
				p.texture = d->kbTex;
				p.texW = kKbW;
				p.texH = kKbH;
				int k = d->pressFlash > 0.0f ? d->pressKey : d->hotKey;
				if (k >= 0) {
					const Key &key = d->keys[k];
					p.hot[0] = (float)(key.col * kCellW) / (float)kKbW;
					p.hot[1] = (float)(key.row * kCellH) / (float)kKbH;
					p.hot[2] = (float)((key.col + key.span) * kCellW) / (float)kKbW;
					p.hot[3] = (float)((key.row + 1) * kCellH) / (float)kKbH;
				}
			} else if (i == kPanelAtlas) {
				p.texture = in.atlasTexture;
				p.texW = in.atlasW;
				p.texH = in.atlasH;
			} else {
				p.u0 = src.x / 320.0f;
				p.u1 = (src.x + src.w) / 320.0f;
				p.v0 = src.y / 200.0f;
				p.v1 = (src.y + src.h) / 200.0f;
			}
		}
		if (nh < SceneState::kMaxHandles) {
			SceneState::Handle &h = scene.handles[nh++];
			const Quad &hq = handle[i];
			h.visible = true;
			h.center = hq.c;
			h.right = hq.r;
			h.up = hq.u;
			h.halfW = hq.hw;
			h.halfH = hq.hh;
			h.glow = handleGlow[i];
			h.alpha = 0.75f + 0.25f * handleGlow[i];
		}
	}
	if (nh < SceneState::kMaxHandles) {
		SceneState::Handle &h = scene.handles[nh++];
		const Quad &hq = handle[kUiPanelCount];
		h.visible = true;
		h.center = hq.c;
		h.right = hq.r;
		h.up = hq.u;
		h.halfW = hq.hw;
		h.halfH = hq.hh;
		h.glow = handleGlow[kUiPanelCount];
		h.alpha = 0.75f + 0.25f * handleGlow[kUiPanelCount];
	}
}

} // namespace lolvr
