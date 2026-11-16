// Banc d'essai du décor : rend la scène hors écran (EGL sans surface, Mesa) et écrit des images PPM.
// Usage : scene_test <dossier de sortie> [prise de vue…]
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "../src/proctex.h"
#include "../src/scene.h"
#include "../src/room.h"

using namespace lolvr;

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

static const int W = 1600, H = 1400;

static GLuint makeProgram(const char *vs, const char *fs) {
	GLuint p = glCreateProgram();
	for (int i = 0; i < 2; i++) {
		GLuint s = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
		const char *src = i ? fs : vs;
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			char log[1024];
			glGetShaderInfoLog(s, 1023, nullptr, log);
			fprintf(stderr, "shader: %s\n", log);
		}
		glAttachShader(p, s);
	}
	glLinkProgram(p);
	return p;
}

// Fausse image de jeu (aucune donnée du vrai jeu ici) : une vue de couloir schématique et une interface.
static GLuint makeFakeGame() {
	std::vector<uint8_t> px(320 * 200 * 4);
	for (int y = 0; y < 200; y++)
		for (int x = 0; x < 320; x++) {
			uint8_t *p = &px[(y * 320 + x) * 4];
			float r = 0.10f, g = 0.09f, b = 0.08f;
			if (x >= 112 && x < 288 && y < 120) {
				float cx = (x - 200) / 88.0f, cy = (y - 60) / 60.0f;
				float d = fmaxf(fabsf(cx), fabsf(cy));
				float shade = 0.15f + 0.6f * d;
				r = 0.45f * shade; g = 0.40f * shade; b = 0.33f * shade;
				if (d < 0.25f) { r = 0.05f; g = 0.04f; b = 0.06f; }
				if (((int)(d * 8)) % 2 == 0) { r *= 0.85f; g *= 0.85f; b *= 0.85f; }
			} else if (x < 112 && y < 120) {
				r = 0.25f; g = 0.18f; b = 0.10f;
				if ((x / 4 + y / 4) % 7 == 0) { r = 0.6f; g = 0.5f; b = 0.2f; }
			} else if (y >= 120 && y < 180) {
				int slot = x / 80;
				r = 0.20f + 0.05f * slot; g = 0.16f; b = 0.12f;
				if (x % 80 > 8 && x % 80 < 40 && y > 126 && y < 170) { r = 0.75f; g = 0.55f; b = 0.42f; }
				if (x % 80 > 44 && x % 80 < 50 && y > 126) { r = 0.1f; g = 0.7f; b = 0.2f; }
				if (x % 80 > 54 && x % 80 < 60 && y > 140) { r = 0.2f; g = 0.3f; b = 0.9f; }
			} else if (y >= 180) {
				r = 0.15f; g = 0.12f; b = 0.10f;
				if ((x / 16) % 2 == 0 && y > 184 && y < 196) { r = 0.5f; g = 0.42f; b = 0.25f; }
			}
			p[0] = (uint8_t)(r * 255); p[1] = (uint8_t)(g * 255); p[2] = (uint8_t)(b * 255); p[3] = 255;
		}
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

struct Shot {
	const char *name;
	int env, theme;
	Vec3 eye;
	float yawDeg, pitchDeg;
	float tanHalf; // demi-champ horizontal
	bool hands;
	float trigger, grip;
};

int main(int argc, char **argv) {
	std::string out = argc > 1 ? argv[1] : ".";
	EGLDisplay dpy = eglGetPlatformDisplay(0x31DD, EGL_DEFAULT_DISPLAY, nullptr);
	if (!eglInitialize(dpy, nullptr, nullptr)) {
		fprintf(stderr, "eglInitialize\n");
		return 1;
	}
	EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
	EGLConfig cfg;
	EGLint n;
	eglChooseConfig(dpy, ca, &cfg, 1, &n);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

	GLuint fbo, color, depth;
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &color);
	glBindTexture(GL_TEXTURE_2D, color);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, W, H);
	glGenRenderbuffers(1, &depth);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "fbo incomplet\n");
		return 1;
	}

	// Programme « dessous » : image du jeu (comme le calque quad) puis faux passthrough.
	GLuint under = makeProgram(
		"#version 300 es\nlayout(location=0) in vec3 aPos; uniform mat4 uMvp; uniform vec4 uRect; out vec2 vUv;\n"
		"void main(){ vUv = uRect.xy + uRect.zw * vec2(aPos.x + 0.5, 0.5 - aPos.y / 0.75); gl_Position = uMvp * vec4(aPos, 1.0); }",
		"#version 300 es\nprecision highp float; uniform sampler2D uTex; uniform float uMode; in vec2 vUv; out vec4 o;\n"
		"void main(){ if (uMode < 0.5) { o = vec4(texture(uTex, vUv).rgb, 1.0); } else {"
		" vec2 p = gl_FragCoord.xy / vec2(1600.0, 1400.0); vec3 wall = mix(vec3(0.55,0.52,0.47), vec3(0.78,0.76,0.72), p.y);"
		" vec3 fl = vec3(0.30,0.22,0.15) * (0.6 + p.y); o = vec4(pow(p.y < 0.33 ? fl : wall, vec3(2.2)), 1.0); } }");
	const float quad[] = {-0.5f, -0.375f, 0, 0.5f, -0.375f, 0, 0.5f, 0.375f, 0, -0.5f, -0.375f, 0, 0.5f, 0.375f, 0, -0.5f, 0.375f, 0};
	GLuint qvao, qvbo;
	glGenVertexArrays(1, &qvao);
	glGenBuffers(1, &qvbo);
	glBindVertexArray(qvao);
	glBindBuffer(GL_ARRAY_BUFFER, qvbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
	glBindVertexArray(0);

	GLuint game = makeFakeGame();
	Scene scene;
	scene.init(true);

	SceneState st;
	st.screenPos = {0, 1.20f, -2.2f};
	st.screenWidth = 2.4f;
	st.roomPos = {0, 0, 0};
	st.gameTexture = game;
	st.screenGlow = {0.10f, 0.08f, 0.06f};
	st.time = 0.0;
	// Attente des textures
	for (int i = 0; i < 4000 && !scene.ready(); i++) {
		scene.prepare(st);
		usleep(5000);
	}
	st.time = 3.0; // après le fondu d'apparition

	const Vec3 seat{0, 1.25f, 0};
	const Shot shots[] = {
		{"lib_front", kEnvLibrary, kThemeKeep, seat, 0, 0, 1.15f, true, 0.0f, 0.0f},
		{"lib_left", kEnvLibrary, kThemeKeep, seat, 75, 5, 1.15f, false, 0, 0},
		{"lib_right", kEnvLibrary, kThemeKeep, seat, -75, -5, 1.15f, false, 0, 0},
		{"lib_back", kEnvLibrary, kThemeKeep, seat, 180, 0, 1.15f, false, 0, 0},
		{"lib_up", kEnvLibrary, kThemeKeep, seat, 0, 55, 1.15f, false, 0, 0},
		{"lib_down", kEnvLibrary, kThemeKeep, seat, -35, -45, 1.15f, false, 0, 0},
		{"lib_wide", kEnvLibrary, kThemeKeep, {1.9f, 2.4f, 2.2f}, 40, -22, 1.0f, false, 0, 0},
		{"lib_table", kEnvLibrary, kThemeKeep, {0.9f, 1.35f, -0.6f}, -40, -30, 0.8f, false, 0, 0},
		{"hands", kEnvLibrary, kThemeKeep, seat, 0, -22, 0.75f, true, 0.0f, 0.0f},
		{"hands_closed", kEnvLibrary, kThemeKeep, {0.05f, 1.15f, -0.15f}, 0, -25, 0.5f, true, 1.0f, 1.0f},
		{"forest_left", kEnvLibrary, kThemeForest, seat, 75, 5, 1.15f, false, 0, 0},
		{"forest_front", kEnvLibrary, kThemeForest, seat, 20, 0, 1.15f, false, 0, 0},
		{"cave_left", kEnvLibrary, kThemeCave, seat, 75, 5, 1.15f, false, 0, 0},
		{"cimmeria_right", kEnvLibrary, kThemeCimmeria, seat, -60, 0, 1.15f, false, 0, 0},
		{"tower_left", kEnvLibrary, kThemeTower, seat, 60, 5, 1.15f, false, 0, 0},
		{"pt_frame", kEnvPassthroughFrame, kThemeKeep, seat, 0, 0, 1.15f, true, 0.0f, 0.0f},
		{"pt_crt", kEnvPassthroughCrt, kThemeKeep, seat, 0, 0, 1.15f, true, 0.0f, 0.0f},
		{"pt_crt_side", kEnvPassthroughCrt, kThemeKeep, {1.1f, 1.45f, -0.7f}, 35, -10, 0.8f, false, 0, 0},
		{"frame_close", kEnvLibrary, kThemeKeep, {0.9f, 1.9f, -1.5f}, 25, -5, 0.6f, false, 0, 0},
		{"win_front", kEnvWindow, kThemeKeep, seat, 0, 0, 1.15f, false, 0, 0},
		{"win_watch", kEnvWindow, kThemeKeep, seat, 0, -30, 0.78f, true, 0.0f, 0.2f},
		{"win_watch_r", kEnvWindow, kThemeKeep, seat, 0, -30, 0.78f, true, 0.0f, 0.2f},
		{"win_tablet", kEnvWindow, kThemeKeep, seat, 0, -12, 0.9f, true, 0.0f, 0.0f},
		{"win_cut", kEnvWindow, kThemeKeep, seat, 0, 0, 1.15f, false, 0, 0},
	};

	std::vector<uint8_t> buf(W * H * 4);
	for (const Shot &sh : shots) {
		bool wanted = argc <= 2;
		for (int i = 2; i < argc; i++)
			if (!strcmp(argv[i], sh.name))
				wanted = true;
		if (!wanted)
			continue;
		st.environment = sh.env;
		st.theme = sh.theme;
		const bool win = sh.env == kEnvWindow;
		const bool cutscene = !strcmp(sh.name, "win_cut");
		float rect[4] = {0, 0, 1, 1};
		float quadW = 2.4f, quadH = 1.8f;
		st.frameVisible = true;
		for (int i = 0; i < SceneState::kMaxPanels; i++)
			st.panels[i].visible = false;
		if (win && !cutscene) {
			st.frameVisible = false;
			rect[0] = 112.0f / 320.0f; rect[1] = 0.0f; rect[2] = 176.0f / 320.0f; rect[3] = 120.0f / 200.0f;
			quadW = 2.0f;
			quadH = 2.0f * 144.0f / 176.0f;
			st.screenWidth = quadW;
			st.screenPos = {0, 1.22f, -1.4f};
		} else if (win) {
			quadW = 2.6f;
			quadH = 1.95f;
			st.screenWidth = quadW;
			st.screenPos = {0, 1.22f, -1.4f};
		} else if (sh.env == kEnvPassthroughCrt) {
			st.screenWidth = 1.0f;
			st.screenPos = {0, 1.15f, -1.3f};
		} else {
			st.screenWidth = 2.4f;
			st.screenPos = {0, 1.20f, -2.2f};
		}
		for (int h = 0; h < 2; h++) {
			HandState &hand = st.hands[h];
			hand.visible = sh.hands;
			float sx = h == 0 ? -1.0f : 1.0f;
			hand.pos = {sx * 0.22f, 0.98f, -0.42f};
			hand.rot = multiply(quatYaw(-sx * 0.12f), quatAxisAngle({1, 0, 0}, 0.12f));
			hand.trigger = sh.trigger;
			hand.grip = sh.grip;
			hand.pointing = h == 1;
		}
		const float deg = 3.14159265f / 180.0f;
		const bool watch = !strcmp(sh.name, "win_watch") || !strcmp(sh.name, "win_watch_r");
		const bool mirrored = !strcmp(sh.name, "win_watch_r");
		const bool tablet = !strcmp(sh.name, "win_tablet");
		if (watch) {
			// Geste « regarder sa montre » : avant-bras en travers, dos de la main vers le haut.
			int np = mirrored ? 1 : 0;
			float sx = mirrored ? -1.0f : 1.0f;
			st.hands[np].pos = {sx * 0.20f, 0.95f, -0.40f};
			st.hands[np].rot = multiply(quatAxisAngle({1, 0, 0}, 38.0f * deg),
			                            multiply(quatYaw(-sx * 90.0f * deg), quatAxisAngle({0, 0, 1}, -sx * 90.0f * deg)));
			st.hands[np].pointing = false;
			st.hands[1 - np].pos = {sx * 0.30f, 0.93f, -0.30f};
			st.hands[1 - np].rot = multiply(quatYaw(sx * 70.0f * deg), quatAxisAngle({1, 0, 0}, 14.0f * deg));
			st.hands[1 - np].pointing = true;
		}
		if (tablet) {
			st.hands[0].pos = {-0.12f, 1.12f, -0.38f};
			st.hands[0].rot = quatAxisAngle({1, 0, 0}, 0.5f);
			st.hands[0].pointing = false;
			st.hands[1].pointing = true;
		}
		if (win && st.frameVisible == false && sh.hands) {
			int np = (watch && mirrored) ? 1 : 0;
			SceneState::Panel &bar = st.panels[0];
			Vec3 c, r, u, n;
			wristPanelFrame(np == 0, c, r, u, n);
			if (np != 0) {
				r = r * -1.0f;
				u = u * -1.0f;
			}
			bar.center = st.hands[np].pos + rotate(st.hands[np].rot, c);
			bar.right = rotate(st.hands[np].rot, r);
			bar.up = rotate(st.hands[np].rot, u);
			bar.halfW = 0.125f;
			bar.halfH = bar.halfW * (58.0f / 234.0f) * 1.2f;
			bar.u0 = 84.0f / 320.0f; bar.u1 = 318.0f / 320.0f; bar.v0 = 141.0f / 200.0f; bar.v1 = 199.5f / 200.0f;
			bar.alpha = 1.0f;
			bar.visible = true;
			if (tablet) {
				SceneState::Panel &tb = st.panels[1];
				Vec3 hp = st.hands[0].pos;
				Vec3 cc = hp + Vec3{0, 0.14f, 0};
				Vec3 nn = normalize(sh.eye - cc);
				Vec3 rr = normalize(cross({0, 1, 0}, nn));
				tb.center = cc; tb.right = rr; tb.up = cross(nn, rr);
				tb.halfW = 0.17f; tb.halfH = 0.17f * 0.75f;
				tb.u0 = 0; tb.u1 = 1; tb.v0 = 0; tb.v1 = 1;
				tb.alpha = 1.0f;
				tb.visible = true;
			}
		}
		st.hitValid = sh.hands && !win;
		st.hit = st.screenPos + Vec3{0.35f, -0.2f, 0.004f};
		st.clickPulse = 0.3f;
		// Changement de thème : attendre la tâche de fond
		for (int i = 0; i < 4000; i++) {
			scene.prepare(st);
			st.time += 0.016;
			if (i > 2 && scene.ready())
				break;
			usleep(2000);
		}
		for (int i = 0; i < 150; i++) {
			usleep(3000);
			scene.prepare(st);
		}
		Quat rot = multiply(quatYaw(sh.yawDeg * 3.14159265f / 180.0f), quatAxisAngle({1, 0, 0}, sh.pitchDeg * 3.14159265f / 180.0f));
		Mat4 view = viewFromPose(sh.eye, rot);
		float th = sh.tanHalf, tv = th * (float)H / (float)W;
		Mat4 proj = projectionFromTangents(-th, th, -tv, tv, 0.05f, 100.0f);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, W, H);
		scene.render(view, proj, st);
		// Compositeur simulé : image du jeu sous le décor, puis faux passthrough sous le tout.
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE);
		glUseProgram(under);
		glBindVertexArray(qvao);
		if (Scene::usesQuadLayer(sh.env)) {
			// Le quad de test mesure 1 x 0,75 : on l'étire à la taille réelle de la vue affichée.
			Mat4 mvp = multiply(multiply(proj, view), poseMatrix(st.screenPos, st.screenRot, 1.0f));
			mvp = multiply(mvp, [&] { Mat4 m = identity(); m.m[0] = quadW; m.m[5] = quadH / 0.75f; return m; }());
			glUniformMatrix4fv(glGetUniformLocation(under, "uMvp"), 1, GL_FALSE, mvp.m);
			glUniform4f(glGetUniformLocation(under, "uRect"), rect[0], rect[1], rect[2], rect[3]);
			glUniform1f(glGetUniformLocation(under, "uMode"), 0.0f);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, game);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		if (sh.env != kEnvLibrary && !win) {
			Mat4 full = identity();
			full.m[0] = 2.0f;
			full.m[5] = 2.0f / 0.75f;
			glUniformMatrix4fv(glGetUniformLocation(under, "uMvp"), 1, GL_FALSE, full.m);
			glUniform4f(glGetUniformLocation(under, "uRect"), 0, 0, 1, 1);
			glUniform1f(glGetUniformLocation(under, "uMode"), 1.0f);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		glDisable(GL_BLEND);
		glBindVertexArray(0);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
		std::string path = out + "/" + sh.name + ".ppm";
		FILE *f = fopen(path.c_str(), "wb");
		fprintf(f, "P6\n%d %d\n255\n", W, H);
		for (int y = H - 1; y >= 0; y--)
			for (int x = 0; x < W; x++)
				fwrite(&buf[(y * W + x) * 4], 1, 3, f);
		fclose(f);
		fprintf(stderr, "écrit %s (GL %x)\n", path.c_str(), glGetError());
	}
	return 0;
}
