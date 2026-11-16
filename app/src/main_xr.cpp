// lol-vr — application Meta Quest : Lands of Lore en VR.
//
// Application Android 100 % native (NativeActivity) :
//   - OpenXR pour le casque et les manettes ;
//   - l'image du jeu est affichée sur un calque « quad » (netteté maximale) ;
//   - un calque de projection dessine le décor autour de l'écran (bibliothèque médiévale,
//     cadre, moniteur cathodique), les gants et le pointeur (voir scene.cpp) ;
//   - la réalité mixte (passthrough) remplace la bibliothèque par la vraie pièce du joueur ;
//   - AAudio pour le son, avec une ambiance sonore spatialisée (voir ambience.cpp) ;
//   - le cœur ScummVM tourne dans son propre fil (voir core_host.cpp).

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "ambience.h"
#include "core_host.h"
#include "libretro.h"
#include "proctex.h"
#include "room.h"
#include "scene.h"
#include "vecmath.h"

#define LOG_TAG "lolvr"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

using lolvr::Ambience;
using lolvr::CoreHost;
using lolvr::Frame;
using lolvr::Mat4;
using lolvr::Quat;
using lolvr::Scene;
using lolvr::SceneState;
using lolvr::Vec3;
using lolvr::clampf;
using lolvr::quatYaw;

namespace {

// ---------------------------------------------------------------------------
// Petites conversions

Vec3 toVec3(const XrVector3f &v) { return {v.x, v.y, v.z}; }
Quat toQuat(const XrQuaternionf &q) { return {q.x, q.y, q.z, q.w}; }
XrVector3f toXr(Vec3 v) { return {v.x, v.y, v.z}; }
XrQuaternionf toXr(Quat q) { return {q.x, q.y, q.z, q.w}; }

double nowSeconds() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------------------
// Fichiers

void makeDirs(const std::string &path) {
	for (size_t i = 1; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '/') {
			std::string sub = path.substr(0, i);
			if (mkdir(sub.c_str(), 0775) != 0 && errno != EEXIST)
				LOGW("mkdir %s : %s", sub.c_str(), strerror(errno));
		}
	}
}

// Copie les données de ScummVM (thèmes, kyra.dat) de l'APK vers le stockage de l'application.
void extractAssetDir(AAssetManager *mgr, const char *assetDir, const std::string &destDir) {
	makeDirs(destDir);
	AAssetDir *dir = AAssetManager_openDir(mgr, assetDir);
	if (!dir) {
		LOGW("dossier d'assets absent : %s", assetDir);
		return;
	}
	const char *name;
	while ((name = AAssetDir_getNextFileName(dir)) != nullptr) {
		std::string src = std::string(assetDir) + "/" + name;
		std::string dst = destDir + "/" + name;
		AAsset *asset = AAssetManager_open(mgr, src.c_str(), AASSET_MODE_STREAMING);
		if (!asset)
			continue;
		off64_t len = AAsset_getLength64(asset);
		struct stat st;
		if (stat(dst.c_str(), &st) == 0 && (off64_t)st.st_size == len) {
			AAsset_close(asset);
			continue; // déjà extrait
		}
		FILE *fp = fopen(dst.c_str(), "wb");
		if (fp) {
			char buf[65536];
			int n;
			while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0)
				fwrite(buf, 1, (size_t)n, fp);
			fclose(fp);
			LOGI("extrait : %s", dst.c_str());
		} else {
			LOGE("écriture impossible : %s (%s)", dst.c_str(), strerror(errno));
		}
		AAsset_close(asset);
	}
	AAssetDir_close(dir);
}

// Cherche GENERAL.PAK (présent dans toutes les versions du jeu) dans `dir` et ses sous-dossiers.
bool findGameDir(const std::string &dir, int depth, std::string &found) {
	DIR *d = opendir(dir.c_str());
	if (!d)
		return false;
	std::vector<std::string> subdirs;
	bool ok = false;
	struct dirent *e;
	while ((e = readdir(d)) != nullptr) {
		if (e->d_name[0] == '.')
			continue;
		if (!strcasecmp(e->d_name, "GENERAL.PAK")) {
			ok = true;
			break;
		}
		std::string full = dir + "/" + e->d_name;
		struct stat st;
		if (depth > 0 && stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			subdirs.push_back(full);
	}
	closedir(d);
	if (ok) {
		found = dir;
		return true;
	}
	for (const std::string &s : subdirs)
		if (findGameDir(s, depth - 1, found))
			return true;
	return false;
}

void coreLog(int level, const char *msg) {
	int prio = level >= 3 ? ANDROID_LOG_ERROR : level == 2 ? ANDROID_LOG_WARN : ANDROID_LOG_INFO;
	__android_log_print(prio, "lolvr-core", "%s", msg);
}

// ---------------------------------------------------------------------------
// Audio

aaudio_data_callback_result_t audioCallback(AAudioStream *, void *, void *audioData, int32_t numFrames) {
	CoreHost::get().readAudio((int16_t *)audioData, (size_t)numFrames);
	// Ancre le son du jeu à l'écran et ajoute l'ambiance de la pièce.
	Ambience::get().process((int16_t *)audioData, (size_t)numFrames);
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

AAudioStream *startAudio(int sampleRate) {
	Ambience::get().setSampleRate(sampleRate);
	AAudioStreamBuilder *builder = nullptr;
	if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder)
		return nullptr;
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStreamBuilder_setSampleRate(builder, sampleRate);
	AAudioStreamBuilder_setChannelCount(builder, 2);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setDataCallback(builder, audioCallback, nullptr);
	AAudioStream *stream = nullptr;
	aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &stream);
	AAudioStreamBuilder_delete(builder);
	if (res != AAUDIO_OK || !stream) {
		LOGE("ouverture du son impossible : %d", (int)res);
		return nullptr;
	}
	AAudioStream_requestStart(stream);
	return stream;
}

void stopAudio(AAudioStream *&stream) {
	if (!stream)
		return;
	AAudioStream_requestStop(stream);
	AAudioStream_close(stream);
	stream = nullptr;
}

// ---------------------------------------------------------------------------
// OpenGL ES

GLuint compileShader(GLenum type, const char *src) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = {0};
		glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
		LOGE("erreur de shader : %s", log);
	}
	return s;
}

GLuint linkProgram(const char *vs, const char *fs) {
	GLuint p = glCreateProgram();
	GLuint v = compileShader(GL_VERTEX_SHADER, vs);
	GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024] = {0};
		glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
		LOGE("erreur de liaison : %s", log);
	}
	glDeleteShader(v);
	glDeleteShader(f);
	return p;
}

// Copie l'image du jeu vers le calque quad, avec un agrandissement net (« sharp bilinear »).
const char *kBlitVs = R"(#version 300 es
out vec2 vUv;
void main() {
	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	vUv = vec2(p.x, 1.0 - p.y);
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *kBlitFs = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec2 uTexSize;
uniform vec2 uOutSize;
uniform vec4 uRect; // portion copiée, en pixels de l'image du jeu : x, y, largeur, hauteur
in vec2 vUv;
out vec4 oColor;
void main() {
	vec2 texel = uRect.xy + vUv * uRect.zw;
	vec2 i = floor(texel);
	vec2 f = texel - i;
	vec2 scale = max(uOutSize / uRect.zw, vec2(1.0));
	vec2 region = 0.5 - 0.5 / scale;
	vec2 c = f - 0.5;
	vec2 fpp = (c - clamp(c, -region, region)) * scale + 0.5;
	vec2 p = clamp(i + fpp, uRect.xy + 0.5, uRect.xy + uRect.zw - 0.5);
	oColor = vec4(texture(uTex, p / uTexSize).rgb, 1.0);
}
)";

struct Egl {
	EGLDisplay display = EGL_NO_DISPLAY;
	EGLConfig config = nullptr;
	EGLContext context = EGL_NO_CONTEXT;
	EGLSurface surface = EGL_NO_SURFACE;
};

bool initEgl(Egl &egl) {
	egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (egl.display == EGL_NO_DISPLAY || !eglInitialize(egl.display, nullptr, nullptr)) {
		LOGE("eglInitialize a échoué");
		return false;
	}
	const EGLint configAttribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_SAMPLES, 0,
		EGL_NONE};
	EGLint count = 0;
	if (!eglChooseConfig(egl.display, configAttribs, &egl.config, 1, &count) || count < 1) {
		LOGE("eglChooseConfig a échoué");
		return false;
	}
	const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
	egl.context = eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, contextAttribs);
	if (egl.context == EGL_NO_CONTEXT) {
		LOGE("eglCreateContext a échoué : 0x%x", eglGetError());
		return false;
	}
	const EGLint surfaceAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
	egl.surface = eglCreatePbufferSurface(egl.display, egl.config, surfaceAttribs);
	if (egl.surface == EGL_NO_SURFACE) {
		LOGE("eglCreatePbufferSurface a échoué : 0x%x", eglGetError());
		return false;
	}
	if (!eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context)) {
		LOGE("eglMakeCurrent a échoué : 0x%x", eglGetError());
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// OpenXR

#define XR_CHECK(call)                                                        \
	do {                                                                      \
		XrResult xrCheckRes = (call);                                         \
		if (XR_FAILED(xrCheckRes)) {                                          \
			LOGE("OpenXR : %s a échoué (%d)", #call, (int)xrCheckRes);        \
			return false;                                                     \
		}                                                                     \
	} while (0)

struct Swapchain {
	XrSwapchain handle = XR_NULL_HANDLE;
	int32_t width = 0;
	int32_t height = 0;
	std::vector<XrSwapchainImageOpenGLESKHR> images;
};

enum Hand { kLeft = 0, kRight = 1 };

struct Button {
	bool down = false;
};

// Image du jeu dans son calque : 1600x1200 au centre, plus une marge noire.
const int kQuadImageW = 1600, kQuadImageH = 1200, kQuadMarginX = 16, kQuadMarginY = 12;
const float kQuadScale = (float)(kQuadImageW + 2 * kQuadMarginX) / (float)kQuadImageW;

// Extension GL_EXT_multisampled_render_to_texture (anticrénelage sans copie, standard sur Quest).
typedef void(GL_APIENTRY *RenderbufferStorageMultisampleFn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void(GL_APIENTRY *FramebufferTexture2DMultisampleFn)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);

const int kEnvCount = lolvr::kEnvCount;

struct App {
	android_app *androidApp = nullptr;
	bool resumed = false;

	Egl egl;

	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	XrSpace stageSpace = XR_NULL_HANDLE; // facultatif : donne la hauteur du sol
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	bool sessionRunning = false;
	bool exitRequested = false;

	bool srgb = false;
	std::vector<XrViewConfigurationView> viewConfigs;
	Swapchain eyeSwapchains[2];
	Swapchain quadSwapchain;

	// Réalité mixte (XR_FB_passthrough)
	bool passthroughSupported = false;
	bool passthroughActive = false;
	XrPassthroughFB passthrough = XR_NULL_HANDLE;
	XrPassthroughLayerFB passthroughLayer = XR_NULL_HANDLE;
	PFN_xrCreatePassthroughFB xrCreatePassthroughFB = nullptr;
	PFN_xrPassthroughStartFB xrPassthroughStartFB = nullptr;
	PFN_xrPassthroughPauseFB xrPassthroughPauseFB = nullptr;
	PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB = nullptr;
	PFN_xrPassthroughLayerResumeFB xrPassthroughLayerResumeFB = nullptr;
	PFN_xrPassthroughLayerPauseFB xrPassthroughLayerPauseFB = nullptr;

	// Actions
	XrActionSet actionSet = XR_NULL_HANDLE;
	XrPath handPaths[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrAction aimAction = XR_NULL_HANDLE;
	XrAction triggerAction = XR_NULL_HANDLE;
	XrAction squeezeAction = XR_NULL_HANDLE;
	XrAction stickAction = XR_NULL_HANDLE;
	XrAction stickClickAction = XR_NULL_HANDLE;
	XrAction aAction = XR_NULL_HANDLE, bAction = XR_NULL_HANDLE;
	XrAction xAction = XR_NULL_HANDLE, yAction = XR_NULL_HANDLE;
	XrAction menuAction = XR_NULL_HANDLE;
	XrAction hapticAction = XR_NULL_HANDLE;
	XrSpace aimSpaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

	// OpenGL
	GLuint fbo = 0;
	GLuint eyeFbo[2] = {0, 0};
	GLuint eyeDepth[2] = {0, 0};
	int msaa = 1; // échantillons par pixel des images des yeux
	bool eyeTargetsChecked = false;
	RenderbufferStorageMultisampleFn renderbufferStorageMultisample = nullptr;
	FramebufferTexture2DMultisampleFn framebufferTexture2DMultisample = nullptr;
	GLuint blitProgram = 0;
	GLint blitTexSize = -1, blitOutSize = -1, blitRect = -1;
	GLuint srcTex = 0;
	int srcTexW = 0, srcTexH = 0;

	// Décor
	Scene scene;
	SceneState ss;
	bool sceneOk = false;
	int environment = lolvr::kEnvLibrary;
	bool environmentRestored = false;
	int shownTheme = 0, wantedTheme = 0;
	double wantedThemeSince = 0;
	double startTime = 0;
	Vec3 screenGlow = {0.05f, 0.05f, 0.05f};

	// Écran virtuel. La taille et la distance sont mémorisées pour chaque décor.
	bool screenPlaced = false;
	Vec3 screenPos = {0, 0, -2.2f};
	float screenYaw = 0.0f;
	float widths[kEnvCount] = {2.4f, 1.8f, 0.62f, 2.0f}; // mètres ; hauteur = largeur * 3/4 (fenêtre : largeur de la vue du donjon)
	float distances[kEnvCount] = {2.2f, 2.0f, 0.95f, 1.4f};

	// Mode fenêtre : la vue du donjon seule, en grand, pendant que l'on explore.
	bool inDungeon = false;         // la boucle du donjon tourne et aucun écran de personnage n'est ouvert
	uint32_t loopSerial = 0;
	double loopChangedAt = -10.0, dungeonSince = 0;
	float windowMix = 0.0f;         // 0 = image entière, 1 = vue du donjon seule
	// Ce que le calque affiche en ce moment : portion de l'image du jeu (pixels) et taille réelle (mètres).
	float cropX = 0, cropY = 0, cropW = 320, cropH = 200;
	float dispW = 2.4f, dispH = 1.8f;
	int quadRectW = 0, quadRectH = 0; // zone du calque quad réellement utilisée
	Vec3 panelCenter[2];             // [0] = barre du poignet, [1] = tablette
	SceneState::Panel panels[2];
	Vec3 anchorPos = {0, 0, 0}; // tête du joueur au moment du placement
	Vec3 anchorDir = {0, 0, -1}; // direction tête -> écran
	Vec3 roomPos = {0, -1.25f, 0};
	float floorY = -1.25f;
	bool headValid = false;
	Vec3 headPos = {0, 0, 0};
	Quat headRot = {0, 0, 0, 1};

	// Pointeur
	int activeHand = kRight;
	bool aimValid[2] = {false, false};
	XrPosef aimPose[2] = {{{0, 0, 0, 1}, {0, 0, 0}}, {{0, 0, 0, 1}, {0, 0, 0}}};
	bool hitValid = false;
	Vec3 hitPoint = {0, 0, 0};
	Vec3 hitRight = {0, 0, 0}, hitUp = {0, 0, 0};
	float hitRadius = 0.0f;
	bool triggerDown[2] = {false, false};
	bool squeezeDown[2] = {false, false};
	float triggerValue[2] = {0, 0}, squeezeValue[2] = {0, 0};
	float clickPulse = 0.0f;

	// Touches
	Button kUp, kDown, kSlideL, kSlideR, kTurnL, kTurnR, kEsc, kMap, kEnter;
	bool recenterDown = false;
	bool zoomDown = false, zoomUsed = false;
	double zoomSince = 0;
	bool settingsDirty = false;

	// Jeu
	std::string systemDir, saveDir, gameRoot, gameDir, settingsPath;
	bool coreStarted = false;
	double nextScan = 0;
	Frame frame;
	bool frameDirty = false;
	AAudioStream *audio = nullptr;
	int audioRate = 48000;

	// Événements du jeu déjà traités (vibrations)
	bool eventsPrimed = false;
	uint32_t sfxSerial = 0, hurtSerial = 0, hitSerial = 0;
};

float &screenWidth(App &app) { return app.widths[app.environment]; }
float &screenDistance(App &app) { return app.distances[app.environment]; }
Quat screenRot(const App &app) { return quatYaw(app.screenYaw); }

bool createSwapchain(App &app, int32_t width, int32_t height, Swapchain &out) {
	XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	info.format = app.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
	info.sampleCount = 1;
	info.width = (uint32_t)width;
	info.height = (uint32_t)height;
	info.faceCount = 1;
	info.arraySize = 1;
	info.mipCount = 1;
	XR_CHECK(xrCreateSwapchain(app.session, &info, &out.handle));
	out.width = width;
	out.height = height;
	uint32_t count = 0;
	XR_CHECK(xrEnumerateSwapchainImages(out.handle, 0, &count, nullptr));
	out.images.assign(count, XrSwapchainImageOpenGLESKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
	XR_CHECK(xrEnumerateSwapchainImages(out.handle, count, &count, (XrSwapchainImageBaseHeader *)out.images.data()));
	return true;
}

bool createAction(App &app, XrAction &action, const char *name, const char *label, XrActionType type, bool perHand) {
	XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
	strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	info.actionType = type;
	if (perHand) {
		info.countSubactionPaths = 2;
		info.subactionPaths = app.handPaths;
	}
	XR_CHECK(xrCreateAction(app.actionSet, &info, &action));
	return true;
}

bool initActions(App &app) {
	XR_CHECK(xrStringToPath(app.instance, "/user/hand/left", &app.handPaths[kLeft]));
	XR_CHECK(xrStringToPath(app.instance, "/user/hand/right", &app.handPaths[kRight]));

	XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
	strcpy(setInfo.actionSetName, "jeu");
	strcpy(setInfo.localizedActionSetName, "Jeu");
	XR_CHECK(xrCreateActionSet(app.instance, &setInfo, &app.actionSet));

	if (!createAction(app, app.aimAction, "viser", "Viser", XR_ACTION_TYPE_POSE_INPUT, true) ||
	    !createAction(app, app.triggerAction, "clic_gauche", "Clic gauche", XR_ACTION_TYPE_FLOAT_INPUT, true) ||
	    !createAction(app, app.squeezeAction, "clic_droit", "Clic droit", XR_ACTION_TYPE_FLOAT_INPUT, true) ||
	    !createAction(app, app.stickAction, "joystick", "Joystick", XR_ACTION_TYPE_VECTOR2F_INPUT, true) ||
	    !createAction(app, app.stickClickAction, "clic_joystick", "Clic du joystick", XR_ACTION_TYPE_BOOLEAN_INPUT, true) ||
	    !createAction(app, app.aAction, "bouton_a", "Bouton A", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.bAction, "bouton_b", "Bouton B", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.xAction, "bouton_x", "Bouton X", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.yAction, "bouton_y", "Bouton Y", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.menuAction, "bouton_menu", "Bouton menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.hapticAction, "vibration", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, true))
		return false;

	struct Binding {
		XrAction action;
		const char *path;
	};
	const Binding table[] = {
		{app.aimAction, "/user/hand/left/input/aim/pose"},
		{app.aimAction, "/user/hand/right/input/aim/pose"},
		{app.triggerAction, "/user/hand/left/input/trigger/value"},
		{app.triggerAction, "/user/hand/right/input/trigger/value"},
		{app.squeezeAction, "/user/hand/left/input/squeeze/value"},
		{app.squeezeAction, "/user/hand/right/input/squeeze/value"},
		{app.stickAction, "/user/hand/left/input/thumbstick"},
		{app.stickAction, "/user/hand/right/input/thumbstick"},
		{app.stickClickAction, "/user/hand/left/input/thumbstick/click"},
		{app.stickClickAction, "/user/hand/right/input/thumbstick/click"},
		{app.aAction, "/user/hand/right/input/a/click"},
		{app.bAction, "/user/hand/right/input/b/click"},
		{app.xAction, "/user/hand/left/input/x/click"},
		{app.yAction, "/user/hand/left/input/y/click"},
		{app.menuAction, "/user/hand/left/input/menu/click"},
		{app.hapticAction, "/user/hand/left/output/haptic"},
		{app.hapticAction, "/user/hand/right/output/haptic"},
	};
	std::vector<XrActionSuggestedBinding> bindings;
	for (const Binding &b : table) {
		XrPath path = XR_NULL_PATH;
		XR_CHECK(xrStringToPath(app.instance, b.path, &path));
		bindings.push_back({b.action, path});
	}
	XrPath profile = XR_NULL_PATH;
	XR_CHECK(xrStringToPath(app.instance, "/interaction_profiles/oculus/touch_controller", &profile));
	XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	suggested.interactionProfile = profile;
	suggested.suggestedBindings = bindings.data();
	suggested.countSuggestedBindings = (uint32_t)bindings.size();
	XR_CHECK(xrSuggestInteractionProfileBindings(app.instance, &suggested));

	for (int h = 0; h < 2; h++) {
		XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
		spaceInfo.action = app.aimAction;
		spaceInfo.subactionPath = app.handPaths[h];
		spaceInfo.poseInActionSpace.orientation.w = 1.0f;
		XR_CHECK(xrCreateActionSpace(app.session, &spaceInfo, &app.aimSpaces[h]));
	}

	XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attach.countActionSets = 1;
	attach.actionSets = &app.actionSet;
	XR_CHECK(xrAttachSessionActionSets(app.session, &attach));
	return true;
}

// Réalité mixte : facultative. En cas d'échec, l'application garde simplement la bibliothèque.
void initPassthrough(App &app) {
	struct Fn {
		const char *name;
		PFN_xrVoidFunction *ptr;
	};
	const Fn fns[] = {
		{"xrCreatePassthroughFB", (PFN_xrVoidFunction *)&app.xrCreatePassthroughFB},
		{"xrPassthroughStartFB", (PFN_xrVoidFunction *)&app.xrPassthroughStartFB},
		{"xrPassthroughPauseFB", (PFN_xrVoidFunction *)&app.xrPassthroughPauseFB},
		{"xrCreatePassthroughLayerFB", (PFN_xrVoidFunction *)&app.xrCreatePassthroughLayerFB},
		{"xrPassthroughLayerResumeFB", (PFN_xrVoidFunction *)&app.xrPassthroughLayerResumeFB},
		{"xrPassthroughLayerPauseFB", (PFN_xrVoidFunction *)&app.xrPassthroughLayerPauseFB},
	};
	for (const Fn &f : fns) {
		if (XR_FAILED(xrGetInstanceProcAddr(app.instance, f.name, f.ptr)) || !*f.ptr) {
			LOGW("réalité mixte indisponible : %s manquant", f.name);
			return;
		}
	}
	XrPassthroughCreateInfoFB ptInfo{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
	XrResult res = app.xrCreatePassthroughFB(app.session, &ptInfo, &app.passthrough);
	if (XR_FAILED(res)) {
		LOGW("réalité mixte indisponible : xrCreatePassthroughFB (%d)", (int)res);
		return;
	}
	XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
	layerInfo.passthrough = app.passthrough;
	layerInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
	res = app.xrCreatePassthroughLayerFB(app.session, &layerInfo, &app.passthroughLayer);
	if (XR_FAILED(res)) {
		LOGW("réalité mixte indisponible : xrCreatePassthroughLayerFB (%d)", (int)res);
		return;
	}
	app.passthroughSupported = true;
	LOGI("réalité mixte disponible");
}

// Allume ou éteint les caméras du casque selon le décor choisi.
void setPassthrough(App &app, bool on) {
	if (!app.passthroughSupported || on == app.passthroughActive)
		return;
	if (on) {
		XrResult res = app.xrPassthroughStartFB(app.passthrough);
		if (XR_SUCCEEDED(res))
			res = app.xrPassthroughLayerResumeFB(app.passthroughLayer);
		if (XR_FAILED(res)) {
			LOGW("démarrage de la réalité mixte impossible (%d)", (int)res);
			app.passthroughSupported = false;
			return;
		}
	} else {
		app.xrPassthroughLayerPauseFB(app.passthroughLayer);
		app.xrPassthroughPauseFB(app.passthrough);
	}
	app.passthroughActive = on;
}

bool initXr(App &app) {
	ANativeActivity *activity = app.androidApp->activity;

	// Le chargeur OpenXR d'Android doit être initialisé avant toute autre fonction.
	PFN_xrInitializeLoaderKHR initLoader = nullptr;
	XR_CHECK(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&initLoader));
	XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
	loaderInfo.applicationVM = activity->vm;
	loaderInfo.applicationContext = activity->clazz;
	XR_CHECK(initLoader((const XrLoaderInitInfoBaseHeaderKHR *)&loaderInfo));

	// Extensions facultatives proposées par le casque
	bool hasPassthrough = false;
	{
		uint32_t count = 0;
		if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr)) && count > 0) {
			std::vector<XrExtensionProperties> props(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
			if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data()))) {
				for (const XrExtensionProperties &p : props)
					if (!strcmp(p.extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME))
						hasPassthrough = true;
			}
		}
	}

	std::vector<const char *> extensions = {
		XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
		XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
	};
	if (hasPassthrough)
		extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
	XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	androidInfo.applicationVM = activity->vm;
	androidInfo.applicationActivity = activity->clazz;
	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.next = &androidInfo;
	strcpy(instanceInfo.applicationInfo.applicationName, "Lands of Lore VR");
	instanceInfo.applicationInfo.applicationVersion = 2;
	strcpy(instanceInfo.applicationInfo.engineName, "lol-vr");
	instanceInfo.applicationInfo.engineVersion = 2;
	instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	instanceInfo.enabledExtensionCount = (uint32_t)extensions.size();
	instanceInfo.enabledExtensionNames = extensions.data();
	XR_CHECK(xrCreateInstance(&instanceInfo, &app.instance));

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XR_CHECK(xrGetSystem(app.instance, &systemInfo, &app.systemId));

	// Appel obligatoire avant de créer la session.
	PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements = nullptr;
	XR_CHECK(xrGetInstanceProcAddr(app.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&getRequirements));
	XrGraphicsRequirementsOpenGLESKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
	XR_CHECK(getRequirements(app.instance, app.systemId, &requirements));

	XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
	binding.display = app.egl.display;
	binding.config = app.egl.config;
	binding.context = app.egl.context;
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &binding;
	sessionInfo.systemId = app.systemId;
	XR_CHECK(xrCreateSession(app.instance, &sessionInfo, &app.session));

	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	XR_CHECK(xrCreateReferenceSpace(app.session, &spaceInfo, &app.localSpace));
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	XR_CHECK(xrCreateReferenceSpace(app.session, &spaceInfo, &app.viewSpace));
	// L'espace « stage » a son origine au sol : il sert à poser la bibliothèque à la bonne hauteur.
	{
		uint32_t count = 0;
		if (XR_SUCCEEDED(xrEnumerateReferenceSpaces(app.session, 0, &count, nullptr)) && count > 0) {
			std::vector<XrReferenceSpaceType> types(count);
			if (XR_SUCCEEDED(xrEnumerateReferenceSpaces(app.session, count, &count, types.data()))) {
				for (XrReferenceSpaceType t : types) {
					if (t != XR_REFERENCE_SPACE_TYPE_STAGE)
						continue;
					spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
					if (XR_FAILED(xrCreateReferenceSpace(app.session, &spaceInfo, &app.stageSpace)))
						app.stageSpace = XR_NULL_HANDLE;
				}
			}
		}
	}

	if (hasPassthrough)
		initPassthrough(app);

	// Format des images : sRGB si possible (couleurs fidèles et filtrage correct).
	uint32_t formatCount = 0;
	XR_CHECK(xrEnumerateSwapchainFormats(app.session, 0, &formatCount, nullptr));
	std::vector<int64_t> formats(formatCount);
	XR_CHECK(xrEnumerateSwapchainFormats(app.session, formatCount, &formatCount, formats.data()));
	app.srgb = false;
	for (int64_t f : formats)
		if (f == GL_SRGB8_ALPHA8)
			app.srgb = true;
	LOGI("format des images : %s", app.srgb ? "sRGB" : "RGBA8");

	uint32_t viewCount = 0;
	XR_CHECK(xrEnumerateViewConfigurationViews(app.instance, app.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr));
	app.viewConfigs.assign(viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
	XR_CHECK(xrEnumerateViewConfigurationViews(app.instance, app.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, app.viewConfigs.data()));
	if (viewCount != 2) {
		LOGE("nombre de vues inattendu : %u", viewCount);
		return false;
	}
	for (int eye = 0; eye < 2; eye++) {
		if (!createSwapchain(app, (int32_t)app.viewConfigs[eye].recommendedImageRectWidth,
		                     (int32_t)app.viewConfigs[eye].recommendedImageRectHeight, app.eyeSwapchains[eye]))
			return false;
	}
	// 1600x1200 = 320x200 agrandi 5x en largeur et 6x en hauteur : pixels nets, format 4:3 d'origine.
	// Une marge noire entoure l'image ; elle reste cachée sous le cadre et évite tout liseré
	// transparent au bord de l'écran quand la tête bouge vite.
	if (!createSwapchain(app, kQuadImageW + 2 * kQuadMarginX, kQuadImageH + 2 * kQuadMarginY, app.quadSwapchain))
		return false;

	return initActions(app);
}

// Tampons de profondeur des deux yeux (le décor est en 3D), anticrénelés si possible.
void createEyeDepth(App &app) {
	for (int eye = 0; eye < 2; eye++) {
		if (!app.eyeFbo[eye])
			glGenFramebuffers(1, &app.eyeFbo[eye]);
		if (app.eyeDepth[eye])
			glDeleteRenderbuffers(1, &app.eyeDepth[eye]);
		glGenRenderbuffers(1, &app.eyeDepth[eye]);
		glBindRenderbuffer(GL_RENDERBUFFER, app.eyeDepth[eye]);
		const Swapchain &sc = app.eyeSwapchains[eye];
		if (app.msaa > 1)
			app.renderbufferStorageMultisample(GL_RENDERBUFFER, app.msaa, GL_DEPTH_COMPONENT24, sc.width, sc.height);
		else
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, sc.width, sc.height);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, app.eyeFbo[eye]);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, app.eyeDepth[eye]);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void attachEyeColor(App &app, GLuint texture) {
	if (app.msaa > 1)
		app.framebufferTexture2DMultisample(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0, app.msaa);
	else
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
}

// Lie l'image d'un œil comme destination du rendu. Au premier passage, vérifie que
// l'anticrénelage est accepté par le casque ; sinon on s'en passe.
void bindEyeTarget(App &app, int eye, GLuint texture) {
	glBindFramebuffer(GL_FRAMEBUFFER, app.eyeFbo[eye]);
	attachEyeColor(app, texture);
	if (app.eyeTargetsChecked)
		return;
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE && app.msaa > 1) {
		LOGW("anticrénelage refusé : rendu sans anticrénelage");
		app.msaa = 1;
		createEyeDepth(app);
		glBindFramebuffer(GL_FRAMEBUFFER, app.eyeFbo[eye]);
		attachEyeColor(app, texture);
	}
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		LOGE("image de l'œil %d inutilisable (0x%x)", eye, glCheckFramebufferStatus(GL_FRAMEBUFFER));
	if (eye == 1)
		app.eyeTargetsChecked = true;
}

bool initGl(App &app) {
	glGenFramebuffers(1, &app.fbo);
	app.blitProgram = linkProgram(kBlitVs, kBlitFs);
	app.blitTexSize = glGetUniformLocation(app.blitProgram, "uTexSize");
	app.blitOutSize = glGetUniformLocation(app.blitProgram, "uOutSize");
	app.blitRect = glGetUniformLocation(app.blitProgram, "uRect");
	glGenTextures(1, &app.srcTex);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);

	const char *ext = (const char *)glGetString(GL_EXTENSIONS);
	if (ext && strstr(ext, "GL_EXT_multisampled_render_to_texture")) {
		app.renderbufferStorageMultisample = (RenderbufferStorageMultisampleFn)eglGetProcAddress("glRenderbufferStorageMultisampleEXT");
		app.framebufferTexture2DMultisample = (FramebufferTexture2DMultisampleFn)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
		if (app.renderbufferStorageMultisample && app.framebufferTexture2DMultisample)
			app.msaa = 4;
	}
	LOGI("anticrénelage : x%d", app.msaa);
	createEyeDepth(app);

	app.sceneOk = app.scene.init(app.srgb);
	return app.sceneOk;
}

// ---------------------------------------------------------------------------
// Jeu

void showMissingFilesMessage(App &app) {
	std::string shortPath = app.gameRoot;
	size_t pos = shortPath.find("/Android/");
	if (pos != std::string::npos)
		shortPath = shortPath.substr(pos + 1);
	std::string body =
		"Les fichiers du jeu sont introuvables.\n"
		"\n"
		"1. Branchez le casque à votre ordinateur.\n"
		"2. Copiez tout le contenu du CD de Lands of Lore\n"
		"   (les fichiers .PAK, dont GENERAL.PAK) dans ce\n"
		"   dossier du casque :\n"
		"\n"
		"   " + shortPath + "\n"
		"\n"
		"Le jeu démarrera tout seul dès que les fichiers\n"
		"seront en place. Rien d'autre à faire.";
	lolvr::renderMessage(app.frame, "Lands of Lore VR", body.c_str());
	app.frameDirty = true;
}

void updateGame(App &app) {
	CoreHost &host = CoreHost::get();
	if (!app.coreStarted) {
		double now = nowSeconds();
		if (now < app.nextScan)
			return;
		app.nextScan = now + 1.5;
		std::string found;
		if (findGameDir(app.gameRoot, 2, found)) {
			app.gameDir = found;
			LOGI("jeu trouvé dans %s", found.c_str());
			lolvr::HostConfig cfg;
			cfg.systemDir = app.systemDir;
			cfg.saveDir = app.saveDir;
			cfg.gameDir = app.gameDir;
			cfg.log = coreLog;
			lolvr::renderMessage(app.frame, "Lands of Lore VR", "Démarrage du jeu…");
			app.frameDirty = true;
			if (host.start(cfg))
				app.coreStarted = true;
		} else if (app.frame.serial == 0) {
			showMissingFilesMessage(app);
		}
		return;
	}
	if (host.fetchFrame(app.frame))
		app.frameDirty = true;
	// Le son est ouvert dès le lancement (ambiance) ; on le rouvre si le jeu utilise une autre fréquence.
	if (host.sampleRate() != app.audioRate) {
		stopAudio(app.audio);
		app.audioRate = host.sampleRate();
		app.audio = startAudio(app.audioRate);
	}
	if (host.finished() && !app.exitRequested) {
		LOGI("ScummVM s'est terminé : fermeture de l'application");
		app.exitRequested = true;
		ANativeActivity_finish(app.androidApp->activity);
	}
}

// ---------------------------------------------------------------------------
// Entrées

float getFloat(App &app, XrAction action, int hand) {
	XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
	info.action = action;
	info.subactionPath = app.handPaths[hand];
	XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
	if (XR_FAILED(xrGetActionStateFloat(app.session, &info, &state)) || !state.isActive)
		return 0.0f;
	return state.currentState;
}

bool getBool(App &app, XrAction action, int hand) {
	XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
	info.action = action;
	if (hand >= 0)
		info.subactionPath = app.handPaths[hand];
	XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
	if (XR_FAILED(xrGetActionStateBoolean(app.session, &info, &state)) || !state.isActive)
		return false;
	return state.currentState == XR_TRUE;
}

XrVector2f getStick(App &app, int hand) {
	XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
	info.action = app.stickAction;
	info.subactionPath = app.handPaths[hand];
	XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
	if (XR_FAILED(xrGetActionStateVector2f(app.session, &info, &state)) || !state.isActive)
		return {0, 0};
	return state.currentState;
}

// Seuil avec hystérésis : évite les rebonds autour de la valeur limite.
bool threshold(bool wasDown, float value) {
	return wasDown ? value > 0.35f : value > 0.6f;
}

void setKey(Button &b, bool down, unsigned key, unsigned mods = 0) {
	if (down == b.down)
		return;
	b.down = down;
	CoreHost::get().key(key, down, mods);
}

// ---------------------------------------------------------------------------
// Réglages mémorisés (décor choisi, taille et distance de l'écran)

struct Limits {
	float wMin, wMax, dMin, dMax;
};
// Dans la bibliothèque, l'écran doit tenir entre les étagères et devant le mur du fond.
const Limits kLimits[kEnvCount] = {{1.2f, 3.0f, 1.5f, 2.9f}, {0.5f, 5.0f, 0.5f, 5.0f}, {0.25f, 2.0f, 0.35f, 3.0f}, {0.8f, 4.0f, 0.6f, 3.0f}};

void loadSettings(App &app, int &environment) {
	FILE *f = fopen(app.settingsPath.c_str(), "r");
	if (!f)
		return;
	int env = 0;
	if (fscanf(f, " decor %d", &env) == 1 && env >= 0 && env < kEnvCount)
		environment = env;
	for (int i = 0; i < kEnvCount; i++) {
		float w = 0, d = 0;
		if (fscanf(f, " ecran %f %f", &w, &d) != 2)
			break;
		if (w >= kLimits[i].wMin && w <= kLimits[i].wMax)
			app.widths[i] = w;
		if (d >= kLimits[i].dMin && d <= kLimits[i].dMax)
			app.distances[i] = d;
	}
	fclose(f);
}

void saveSettings(App &app) {
	app.settingsDirty = false;
	FILE *f = fopen(app.settingsPath.c_str(), "w");
	if (!f)
		return;
	fprintf(f, "decor %d\n", app.environment);
	for (int i = 0; i < kEnvCount; i++)
		fprintf(f, "ecran %.3f %.3f\n", app.widths[i], app.distances[i]);
	fclose(f);
}

// ---------------------------------------------------------------------------
// Placement de l'écran et de la pièce

void updateHead(App &app, XrTime time) {
	XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
	app.headValid = XR_SUCCEEDED(xrLocateSpace(app.viewSpace, app.localSpace, time, &head)) &&
	                (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
	                (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
	if (app.headValid) {
		app.headPos = toVec3(head.pose.position);
		app.headRot = toQuat(head.pose.orientation);
	}
}

// Hauteur du sol : donnée par le casque si possible, sinon on suppose un joueur assis.
void updateFloor(App &app, XrTime time) {
	float y = app.headPos.y - 1.25f;
	if (app.stageSpace != XR_NULL_HANDLE) {
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		if (XR_SUCCEEDED(xrLocateSpace(app.stageSpace, app.localSpace, time, &loc)) &&
		    (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
			float below = app.headPos.y - loc.pose.position.y;
			if (below > 0.6f && below < 2.3f)
				y = loc.pose.position.y;
		}
	}
	app.floorY = y;
}

// Place l'écran devant le joueur. newAnchor : repart de la position et du regard actuels
// (premier affichage, recentrage, changement de décor) ; sinon seule la distance a changé.
void placeScreen(App &app, bool newAnchor) {
	bool library = app.environment == lolvr::kEnvLibrary;
	if (newAnchor) {
		Vec3 fwd = rotate(app.headRot, {0, 0, -1});
		float yaw = atan2f(-fwd.x, -fwd.z);
		Vec3 flat = rotate(quatYaw(yaw), {0, 0, -1});
		app.screenYaw = yaw;
		app.anchorPos = app.headPos;
		if (library) {
			app.anchorDir = flat;
		} else {
			// En réalité mixte, l'écran se pose là où l'on regarde (sur un bureau, par exemple).
			float pitch = clampf(asinf(clampf(fwd.y, -1.0f, 1.0f)), -0.6f, 0.45f);
			app.anchorDir = flat * cosf(pitch) + Vec3{0, sinf(pitch), 0};
		}
		app.roomPos = {app.headPos.x, app.floorY, app.headPos.z};
	}
	float w = screenWidth(app);
	Vec3 pos = app.anchorPos + app.anchorDir * screenDistance(app);
	if (library) {
		// Juste sous la ligne des yeux, sans jamais toucher le plancher.
		pos.y = app.anchorPos.y - 0.05f;
		float minY = app.floorY + 0.375f * w * 1.2f + 0.12f;
		if (pos.y < minY)
			pos.y = minY;
	}
	app.screenPos = pos;
	app.screenPlaced = true;
}

bool usesPassthrough(int env) { return env == lolvr::kEnvPassthroughFrame || env == lolvr::kEnvPassthroughCrt; }

// Décor suivant, en sautant ceux qui demandent la réalité mixte quand elle n'est pas disponible.
int nextEnvironment(const App &app, int env) {
	for (int i = 0; i < kEnvCount; i++) {
		env = (env + 1) % kEnvCount;
		if (!usesPassthrough(env) || app.passthroughSupported)
			return env;
	}
	return lolvr::kEnvLibrary;
}

void setEnvironment(App &app, int env, XrTime time) {
	if (env < 0 || env >= kEnvCount)
		env = lolvr::kEnvLibrary;
	if (usesPassthrough(env) && !app.passthroughSupported)
		env = lolvr::kEnvLibrary;
	setPassthrough(app, usesPassthrough(env));
	if (usesPassthrough(env) && !app.passthroughActive)
		env = lolvr::kEnvLibrary;
	app.windowMix = 0.0f;
	app.environment = env;
	LOGI("décor : %d", env);
	if (app.headValid) {
		updateFloor(app, time);
		placeScreen(app, true);
	}
}

// ---------------------------------------------------------------------------
// Vibrations

void pulse(App &app, int hand, float amplitude, float seconds) {
	if (app.sessionState != XR_SESSION_STATE_FOCUSED || app.hapticAction == XR_NULL_HANDLE)
		return;
	XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
	vibration.amplitude = clampf(amplitude, 0.0f, 1.0f);
	vibration.duration = (XrDuration)(seconds * 1e9f);
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
	info.action = app.hapticAction;
	info.subactionPath = app.handPaths[hand];
	xrApplyHapticFeedback(app.session, &info, (const XrHapticBaseHeader *)&vibration);
}

enum SfxKind { kSfxNone = 0, kSfxDoor, kSfxMechanism, kSfxSwing, kSfxSpell, kSfxExplosion };

// Classe un effet sonore du jeu d'après le nom de son fichier (version CD).
int classifySfx(const char *name) {
	struct Rule {
		const char *prefix;
		int kind;
	};
	static const Rule rules[] = {
		{"DOOR", kSfxDoor}, {"DOR", kSfxDoor}, {"MTLDR", kSfxDoor}, {"PORTCUL", kSfxDoor},
		{"LEVER", kSfxMechanism}, {"PRESS", kSfxMechanism}, {"PLATE", kSfxMechanism}, {"KEYOPEN", kSfxMechanism},
		{"KEYLOCK", kSfxMechanism}, {"LOKPICK", kSfxMechanism}, {"CHEST", kSfxMechanism}, {"OPENBOX", kSfxMechanism},
		{"SWING", kSfxSwing}, {"SHWING", kSfxSwing}, {"NUNCHUK", kSfxSwing}, {"THROW", kSfxSwing},
		{"CROSSBOW", kSfxSwing}, {"BOW", kSfxSwing},
		{"FIRESPL", kSfxSpell}, {"LITENIN", kSfxSpell}, {"ICESPEL", kSfxSpell}, {"SPELL", kSfxSpell},
		{"3BOLTC", kSfxSpell}, {"2BOLTC", kSfxSpell}, {"1BOLTC", kSfxSpell}, {"DAWNSPL", kSfxSpell},
		{"HEAL", kSfxSpell}, {"SHIELD", kSfxSpell}, {"MORPH", kSfxSpell},
		{"EXPLODE", kSfxExplosion},
	};
	if (!name || !name[0])
		return kSfxNone;
	for (const Rule &r : rules)
		if (!strncasecmp(name, r.prefix, strlen(r.prefix)))
			return r.kind;
	return kSfxNone;
}

// Suit ce qui se passe dans le jeu : zone traversée (décor), coups et sons (vibrations).
void updateGameEvents(App &app, double now) {
	lolvr::GameState gs;
	if (!app.coreStarted || !CoreHost::get().gameState(gs)) {
		app.inDungeon = false;
		return;
	}

	// Vue du donjon active : la boucle du jeu tourne et aucun écran de personnage n'est ouvert.
	if (gs.loopSerial != app.loopSerial) {
		app.loopSerial = gs.loopSerial;
		app.loopChangedAt = now;
	}
	bool dungeon = gs.level > 0 && gs.controlMode == 0 && now - app.loopChangedAt < 0.7;
	if (dungeon && !app.inDungeon)
		app.dungeonSince = now;
	app.inDungeon = dungeon;

	// Le décor change une fois la nouvelle zone bien installée (pas pendant un chargement).
	int theme = lolvr::themeFromGame(gs.tileset, gs.level);
	if (theme != app.wantedTheme) {
		app.wantedTheme = theme;
		app.wantedThemeSince = now;
	}
	if (app.shownTheme != app.wantedTheme && now - app.wantedThemeSince > 0.75) {
		app.shownTheme = app.wantedTheme;
		LOGI("zone du jeu : %s (niveau %d) -> décor « %s »", gs.tileset, gs.level, lolvr::themeInfo(app.shownTheme).name);
	}

	if (!app.eventsPrimed) {
		app.eventsPrimed = true;
		app.sfxSerial = gs.sfxSerial;
		app.hurtSerial = gs.hurtSerial;
		app.hitSerial = gs.hitSerial;
		return;
	}
	int hand = app.activeHand, other = 1 - hand;
	if (gs.hurtSerial != app.hurtSerial) {
		// Un compagnon est blessé : forte secousse dans les deux mains.
		app.hurtSerial = gs.hurtSerial;
		float a = clampf(0.55f + (float)gs.hurtDamage / 60.0f, 0.55f, 1.0f);
		pulse(app, kLeft, a, 0.22f);
		pulse(app, kRight, a, 0.22f);
	}
	if (gs.hitSerial != app.hitSerial) {
		// Le groupe touche un monstre : coup sec dans la main qui a frappé.
		app.hitSerial = gs.hitSerial;
		pulse(app, hand, 0.75f, 0.07f);
		pulse(app, other, 0.25f, 0.05f);
	}
	if (gs.sfxSerial != app.sfxSerial) {
		app.sfxSerial = gs.sfxSerial;
		float v = 0.4f + 0.6f * clampf((float)gs.sfxVolume / 255.0f, 0.0f, 1.0f);
		switch (classifySfx(gs.sfxName)) {
		case kSfxDoor:
			pulse(app, kLeft, 0.40f * v, 0.28f);
			pulse(app, kRight, 0.40f * v, 0.28f);
			break;
		case kSfxMechanism:
			pulse(app, hand, 0.40f * v, 0.05f);
			break;
		case kSfxSwing:
			pulse(app, hand, 0.35f * v, 0.06f);
			break;
		case kSfxSpell:
			pulse(app, kLeft, 0.50f * v, 0.18f);
			pulse(app, kRight, 0.50f * v, 0.18f);
			break;
		case kSfxExplosion:
			pulse(app, kLeft, 1.0f * v, 0.30f);
			pulse(app, kRight, 1.0f * v, 0.30f);
			break;
		default:
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Mode fenêtre : vue du donjon seule + interface sur le poignet

// Calcule ce que le calque affiche (portion de l'image, taille réelle) en fondu entre l'image
// entière et la vue du donjon seule.
void updateWindowView(App &app, double now, float dt) {
	const float kWinX = 112, kWinY = 0, kWinW = 176, kWinH = 120; // vue du donjon dans l'image 320x200
	float ww = screenWidth(app);
	if (app.environment != lolvr::kEnvWindow) {
		app.windowMix = 0.0f;
		app.cropX = 0; app.cropY = 0; app.cropW = 320; app.cropH = 200;
		app.dispW = ww;
		app.dispH = ww * 0.75f;
		return;
	}
	float target = (app.inDungeon && now - app.dungeonSince > 0.3) ? 1.0f : 0.0f;
	float step = dt * 2.5f;
	app.windowMix += clampf(target - app.windowMix, -step, step);
	float m = app.windowMix * app.windowMix * (3.0f - 2.0f * app.windowMix);
	app.cropX = kWinX * m;
	app.cropY = kWinY * m;
	app.cropW = 320.0f + (kWinW - 320.0f) * m;
	app.cropH = 200.0f + (kWinH - 200.0f) * m;
	// Même finesse d'image des deux côtés : la vue du donjon a la largeur choisie, l'image entière 1,3 fois plus.
	float scaleWin = ww / kWinW, scaleFull = ww * 1.3f / 320.0f;
	float scale = scaleFull + (scaleWin - scaleFull) * m;
	app.dispW = app.cropW * scale;
	app.dispH = app.cropH * 1.2f * scale; // les pixels du jeu sont 1,2 fois plus hauts que larges
}

// Panneaux d'interface sur la main qui ne vise pas : barre (portraits + objets) sur l'avant-bras,
// et tablette (image entière) qui apparaît quand on approche la main du visage.
void updatePanels(App &app) {
	for (int i = 0; i < 2; i++)
		app.panels[i].visible = false;
	if (app.environment != lolvr::kEnvWindow || app.windowMix < 0.6f)
		return;
	int np = 1 - app.activeHand;
	if (!app.aimValid[np] || !app.headValid)
		return;
	Quat hr = toQuat(app.aimPose[np].orientation);
	Vec3 hp = toVec3(app.aimPose[np].position);

	// Barre : bas de l'image (portraits, barres de vie, objets)
	{
		SceneState::Panel &p = app.panels[0];
		Vec3 c, r, u, n;
		lolvr::wristPanelFrame(np == kLeft, c, r, u, n);
		if (np != kLeft) {
			r = r * -1.0f;
			u = u * -1.0f;
		}
		p.center = hp + rotate(hr, c);
		p.right = rotate(hr, r);
		p.up = rotate(hr, u);
		p.halfW = 0.125f;
		p.halfH = p.halfW * (58.0f / 234.0f) * 1.2f;
		p.u0 = 84.0f / 320.0f; p.u1 = 318.0f / 320.0f;
		p.v0 = 141.0f / 200.0f; p.v1 = 199.5f / 200.0f;
		p.alpha = 1.0f;
		p.visible = true;
	}
	// Tablette : l'image entière, devant la main, tournée vers le visage
	{
		SceneState::Panel &p = app.panels[1];
		Vec3 toHead = app.headPos - hp;
		float dist = length(toHead);
		Vec3 fwd = rotate(app.headRot, {0, 0, -1});
		Vec3 dirToHand = dist > 1e-3f ? toHead * (-1.0f / dist) : fwd;
		float inFront = dot(dirToHand, fwd);
		float a = clampf((0.60f - dist) / 0.20f, 0.0f, 1.0f) * clampf((inFront - 0.35f) / 0.2f, 0.0f, 1.0f);
		if (a > 0.02f) {
			Vec3 c = hp + Vec3{0, 0.14f, 0};
			Vec3 n = normalize(app.headPos - c);
			Vec3 r = normalize(cross({0, 1, 0}, n));
			p.center = c;
			p.right = r;
			p.up = cross(n, r);
			p.halfW = 0.17f;
			p.halfH = p.halfW * 0.75f;
			p.u0 = 0; p.u1 = 1; p.v0 = 0; p.v1 = 1;
			p.alpha = a;
			p.visible = true;
		}
	}
}

// ---------------------------------------------------------------------------
// Manettes

void updateInput(App &app, XrTime time, float dt, double now) {
	XrActiveActionSet active{app.actionSet, XR_NULL_PATH};
	XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	xrSyncActions(app.session, &sync);

	// Clic du joystick droit : recentrer l'écran (et la pièce) devant soi.
	bool recenter = getBool(app, app.stickClickAction, kRight);
	if (app.headValid && recenter && !app.recenterDown) {
		updateFloor(app, time);
		placeScreen(app, true);
	}
	app.recenterDown = recenter;

	// Position des manettes (les gants les suivent)
	for (int h = 0; h < 2; h++) {
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		app.aimValid[h] = XR_SUCCEEDED(xrLocateSpace(app.aimSpaces[h], app.localSpace, time, &loc)) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
		if (app.aimValid[h])
			app.aimPose[h] = loc.pose;
	}

	// Gâchette = clic gauche, poignée = clic droit. La main qui clique devient le pointeur.
	bool clicked[2] = {false, false};
	for (int h = 0; h < 2; h++) {
		app.triggerValue[h] = getFloat(app, app.triggerAction, h);
		app.squeezeValue[h] = getFloat(app, app.squeezeAction, h);
		bool trigger = threshold(app.triggerDown[h], app.triggerValue[h]);
		bool squeeze = threshold(app.squeezeDown[h], app.squeezeValue[h]);
		bool pressedNow = (trigger && !app.triggerDown[h]) || (squeeze && !app.squeezeDown[h]);
		int other = 1 - h;
		if (pressedNow && app.aimValid[h] && !app.triggerDown[other] && !app.squeezeDown[other])
			app.activeHand = h;
		clicked[h] = pressedNow;
		app.triggerDown[h] = trigger;
		app.squeezeDown[h] = squeeze;
	}
	if (!app.aimValid[app.activeHand] && app.aimValid[1 - app.activeHand])
		app.activeHand = 1 - app.activeHand;

	// Le pointeur part du bout de l'index du gant et va jusqu'à un panneau ou à l'écran.
	app.hitValid = false;
	app.hitRadius = 0.0f;
	int hand = app.activeHand;
	updatePanels(app);
	float w = app.dispW, h = app.dispH;
	if (app.aimValid[hand] && app.screenPlaced) {
		bool crt = app.environment == lolvr::kEnvPassthroughCrt;
		Quat aimRot = toQuat(app.aimPose[hand].orientation);
		Vec3 tip = toVec3(app.aimPose[hand].position) + rotate(aimRot, lolvr::gloveFingertip(hand == kLeft, 0.0f));
		Vec3 dirW = rotate(aimRot, {0, 0, -1});
		float gu = 0, gv = 0;
		bool got = false;

		// 1. Les panneaux d'interface (devant l'écran, plus près de la main)
		float bestT = 1e9f;
		for (int i = 0; i < 2; i++) {
			const SceneState::Panel &p = app.panels[i];
			if (!p.visible || p.alpha < 0.5f)
				continue;
			Vec3 n = cross(p.right, p.up);
			float denom = dot(dirW, n);
			if (fabsf(denom) < 1e-4f)
				continue;
			float t = dot(p.center - tip, n) / denom;
			if (t <= 0.0f || t >= bestT)
				continue;
			Vec3 hitW = tip + dirW * t;
			Vec3 d = hitW - p.center;
			float x = dot(d, p.right) / p.halfW, y = dot(d, p.up) / p.halfH;
			if (fabsf(x) > 1.08f || fabsf(y) > 1.12f)
				continue;
			x = clampf(x, -1.0f, 1.0f);
			y = clampf(y, -1.0f, 1.0f);
			gu = p.u0 + (p.u1 - p.u0) * (x + 1.0f) * 0.5f;
			gv = p.v0 + (p.v1 - p.v0) * (1.0f - y) * 0.5f;
			bestT = t;
			got = true;
			app.hitPoint = hitW;
			app.hitRight = p.right;
			app.hitUp = p.up;
			app.hitRadius = p.halfH * 0.10f;
		}

		// 2. L'écran lui-même (ou la vue du donjon)
		if (!got) {
			Quat inv = conjugate(screenRot(app));
			Vec3 origin = rotate(inv, tip - app.screenPos);
			Vec3 dir = rotate(inv, dirW);
			if (dir.z < -1e-4f && origin.z > 0.0f) {
				// La dalle du moniteur cathodique est bombée : on affine le point d'impact.
				float zPlane = 0.0f;
				Vec3 hit = origin;
				for (int it = 0; it < (crt ? 3 : 1); it++) {
					float t = (zPlane - origin.z) / dir.z;
					hit = origin + dir * t;
					if (crt)
						zPlane = w * lolvr::crtGlassZ(hit.x / w, hit.y / w);
				}
				float u = hit.x / w + 0.5f;
				float v = 0.5f - hit.y / h;
				if (u > -0.15f && u < 1.15f && v > -0.15f && v < 1.15f) {
					u = clampf(u, 0.0f, 1.0f);
					v = clampf(v, 0.0f, 1.0f);
					gu = (app.cropX + app.cropW * u) / 320.0f;
					gv = (app.cropY + app.cropH * v) / 200.0f;
					Vec3 local{(u - 0.5f) * w, (0.5f - v) * h, zPlane + 0.004f};
					app.hitPoint = rotate(screenRot(app), local) + app.screenPos;
					got = true;
				}
			}
		}
		if (got) {
			CoreHost::get().setPointer(gu, gv);
			app.hitValid = true;
		}
	}
	CoreHost::get().setMouseButtons(app.triggerDown[hand] && app.hitValid, app.squeezeDown[hand] && app.hitValid);
	if (clicked[hand] && app.hitValid) {
		app.clickPulse = 1.0f;
		pulse(app, hand, 0.28f, 0.018f);
	}

	// Joysticks : gauche = avancer/reculer/pas de côté, droit = tourner.
	XrVector2f left = getStick(app, kLeft);
	XrVector2f right = getStick(app, kRight);
	bool zoomMode = getBool(app, app.stickClickAction, kLeft);
	setKey(app.kUp, threshold(app.kUp.down, left.y), RETROK_UP);
	setKey(app.kDown, threshold(app.kDown.down, -left.y), RETROK_DOWN);
	setKey(app.kSlideL, threshold(app.kSlideL.down, -left.x), RETROK_LEFT);
	setKey(app.kSlideR, threshold(app.kSlideR.down, left.x), RETROK_RIGHT);
	setKey(app.kTurnL, !zoomMode && threshold(app.kTurnL.down, -right.x), RETROK_HOME);
	setKey(app.kTurnR, !zoomMode && threshold(app.kTurnR.down, right.x), RETROK_PAGEUP);

	// Clic du joystick gauche :
	//   - maintenu + joystick droit haut/bas : taille de l'écran ; gauche/droite : distance ;
	//   - simple clic bref : décor suivant (bibliothèque, réalité mixte, moniteur cathodique).
	if (zoomMode && !app.zoomDown) {
		app.zoomSince = now;
		app.zoomUsed = false;
	}
	if (zoomMode) {
		const Limits &lim = kLimits[app.environment];
		if (fabsf(right.y) > 0.3f && fabsf(right.y) >= fabsf(right.x)) {
			screenWidth(app) = clampf(w * (1.0f + right.y * 0.8f * dt), lim.wMin, lim.wMax);
			app.zoomUsed = true;
		} else if (fabsf(right.x) > 0.3f) {
			screenDistance(app) = clampf(screenDistance(app) * (1.0f + right.x * 0.6f * dt), lim.dMin, lim.dMax);
			app.zoomUsed = true;
		}
		if (app.zoomUsed)
			placeScreen(app, false);
	} else if (app.zoomDown) {
		if (!app.zoomUsed && now - app.zoomSince < 0.6) {
			setEnvironment(app, nextEnvironment(app, app.environment), time);
			pulse(app, kLeft, 0.3f, 0.03f);
		}
		app.settingsDirty = true;
	}
	app.zoomDown = zoomMode;

	// Boutons
	setKey(app.kEnter, getBool(app, app.aAction, -1), RETROK_RETURN);
	setKey(app.kEsc, getBool(app, app.bAction, -1), RETROK_ESCAPE);
	setKey(app.kMap, getBool(app, app.yAction, -1), RETROK_m);
	// X = clavier virtuel (pour nommer une sauvegarde), bouton menu = menu ScummVM.
	CoreHost::get().setSpecialButtons(getBool(app, app.xAction, -1), getBool(app, app.menuAction, -1));
}

// ---------------------------------------------------------------------------
// Rendu

void uploadFrame(App &app) {
	if (!app.frameDirty || app.frame.width <= 0 || app.frame.height <= 0)
		return;
	app.frameDirty = false;
	glBindTexture(GL_TEXTURE_2D, app.srcTex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (app.srcTexW != app.frame.width || app.srcTexH != app.frame.height) {
		app.srcTexW = app.frame.width;
		app.srcTexH = app.frame.height;
		glTexImage2D(GL_TEXTURE_2D, 0, app.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, app.srcTexW, app.srcTexH, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, app.frame.rgba.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, app.srcTexW, app.srcTexH, GL_RGBA, GL_UNSIGNED_BYTE, app.frame.rgba.data());
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	// Couleur moyenne de l'image : l'écran éclaire doucement les gants et la pièce.
	const uint8_t *px = app.frame.rgba.data();
	const size_t count = (size_t)app.frame.width * (size_t)app.frame.height;
	float sum[3] = {0, 0, 0};
	size_t n = 0;
	for (size_t i = 0; i < count; i += 53, n++)
		for (int k = 0; k < 3; k++) {
			float c = (float)px[i * 4 + k] * (1.0f / 255.0f);
			sum[k] += c * c; // approximation de la conversion sRGB -> linéaire
		}
	if (n > 0) {
		Vec3 avg{sum[0] / (float)n, sum[1] / (float)n, sum[2] / (float)n};
		app.screenGlow = lolvr::lerp(app.screenGlow, avg, 0.25f);
	}
}

bool acquireImage(Swapchain &sc, uint32_t &index) {
	XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	if (XR_FAILED(xrAcquireSwapchainImage(sc.handle, &acquire, &index)))
		return false;
	XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	wait.timeout = XR_INFINITE_DURATION;
	if (XR_FAILED(xrWaitSwapchainImage(sc.handle, &wait))) {
		XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xrReleaseSwapchainImage(sc.handle, &release);
		return false;
	}
	return true;
}

void releaseImage(Swapchain &sc) {
	XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	xrReleaseSwapchainImage(sc.handle, &release);
}

bool renderQuad(App &app) {
	uint32_t index = 0;
	if (!acquireImage(app.quadSwapchain, index))
		return false;
	const bool windowEnv = app.environment == lolvr::kEnvWindow;
	const int sw = app.quadSwapchain.width, sh = app.quadSwapchain.height;
	// Zone du calque utilisée : en mode fenêtre, au format exact de ce qui est affiché ;
	// sinon l'image 4:3 avec sa petite marge noire cachée sous le cadre.
	int vx = kQuadMarginX, vy = kQuadMarginY, vw = kQuadImageW, vh = kQuadImageH;
	if (windowEnv) {
		float aspect = app.dispW / app.dispH;
		vw = sw;
		vh = (int)((float)sw / aspect);
		if (vh > sh) {
			vh = sh;
			vw = (int)((float)sh * aspect);
		}
		vx = vy = 0;
	}
	app.quadRectW = windowEnv ? vw : sw;
	app.quadRectH = windowEnv ? vh : sh;
	glBindFramebuffer(GL_FRAMEBUFFER, app.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.quadSwapchain.images[index].image, 0);
	glViewport(0, 0, sw, sh);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	if (app.srcTexW > 0) {
		glViewport(vx, vy, vw, vh);
		glUseProgram(app.blitProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, app.srcTex);
		glUniform2f(app.blitTexSize, (float)app.srcTexW, (float)app.srcTexH);
		glUniform2f(app.blitOutSize, (float)vw, (float)vh);
		float sx = (float)app.srcTexW / 320.0f, sy = (float)app.srcTexH / 200.0f;
		glUniform4f(app.blitRect, app.cropX * sx, app.cropY * sy, app.cropW * sx, app.cropH * sy);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	releaseImage(app.quadSwapchain);
	return true;
}

void fillSceneState(App &app, double now, bool focused) {
	SceneState &s = app.ss;
	s.environment = app.environment;
	s.theme = app.shownTheme;
	s.time = now - app.startTime;
	s.screenPos = app.screenPos;
	s.screenRot = screenRot(app);
	s.screenWidth = app.dispW;
	s.frameVisible = app.environment != lolvr::kEnvWindow || app.windowMix < 0.02f;
	for (int i = 0; i < 2; i++)
		s.panels[i] = app.panels[i];
	if (app.environment != lolvr::kEnvWindow || app.windowMix < 0.6f)
		for (int i = 0; i < 2; i++)
			s.panels[i].visible = false;
	s.hitRight = app.hitRight;
	s.hitUp = app.hitUp;
	s.hitRadius = app.hitRadius;
	s.roomPos = app.roomPos;
	s.roomYaw = app.screenYaw;
	for (int h = 0; h < 2; h++) {
		lolvr::HandState &hand = s.hands[h];
		bool pointing = h == app.activeHand;
		hand.visible = focused && app.aimValid[h];
		hand.pos = toVec3(app.aimPose[h].position);
		hand.rot = toQuat(app.aimPose[h].orientation);
		// L'index qui vise ne se replie qu'un peu au clic, pour que le pointeur ne bouge pas.
		hand.trigger = pointing ? app.triggerValue[h] * 0.3f : app.triggerValue[h];
		hand.grip = app.squeezeValue[h];
		hand.pointing = pointing;
	}
	s.hitValid = focused && app.hitValid;
	s.hit = app.hitPoint;
	s.clickPulse = app.clickPulse;
	s.screenGlow = app.screenGlow;
	s.gameTexture = app.srcTexW > 0 ? app.srcTex : 0;
	s.gameWidth = app.srcTexW > 0 ? app.srcTexW : 320;
	s.gameHeight = app.srcTexH > 0 ? app.srcTexH : 200;
}

void renderFrame(App &app, float dt, int savedEnvironment) {
	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	XrFrameState frameState{XR_TYPE_FRAME_STATE};
	if (XR_FAILED(xrWaitFrame(app.session, &waitInfo, &frameState)))
		return;
	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	if (XR_FAILED(xrBeginFrame(app.session, &beginInfo)))
		return;

	XrCompositionLayerPassthroughFB passthroughLayer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
	XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
	XrCompositionLayerProjection projLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerProjectionView projViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
	std::vector<const XrCompositionLayerBaseHeader *> layers;

	if (frameState.shouldRender) {
		const XrTime time = frameState.predictedDisplayTime;
		const double now = nowSeconds();
		const bool focused = app.sessionState == XR_SESSION_STATE_FOCUSED;
		const bool visible = focused || app.sessionState == XR_SESSION_STATE_VISIBLE;

		updateHead(app, time);
		// Au premier affichage : on retrouve le décor choisi la dernière fois et on place l'écran.
		if (!app.environmentRestored && app.headValid) {
			app.environmentRestored = true;
			setEnvironment(app, savedEnvironment, time);
		}
		if (app.headValid && !app.screenPlaced) {
			updateFloor(app, time);
			placeScreen(app, true);
		}

		updateGameEvents(app, now);
		updateWindowView(app, now, dt);
		if (focused) {
			updateInput(app, time, dt, now);
		} else {
			CoreHost::get().setMouseButtons(false, false);
			app.hitValid = false;
		}
		app.clickPulse *= expf(-dt * 7.0f);
		if (app.settingsDirty)
			saveSettings(app);

		uploadFrame(app);
		fillSceneState(app, now, focused);
		app.scene.prepare(app.ss);

		// Son : l'auditeur est la tête du joueur ; le son du jeu vient de l'écran.
		Ambience &ambience = Ambience::get();
		if (app.headValid)
			ambience.setListener(app.headPos, app.headRot);
		ambience.setScreen(app.screenPos, screenRot(app), app.dispW);
		ambience.setRoom(app.roomPos, app.screenYaw);
		ambience.setMood(visible && app.environment == lolvr::kEnvLibrary && app.scene.ready(), app.shownTheme);

		// Calques, du fond vers l'avant : vraie pièce (réalité mixte), image du jeu, décor.
		if (app.passthroughActive) {
			passthroughLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			passthroughLayer.space = XR_NULL_HANDLE;
			passthroughLayer.layerHandle = app.passthroughLayer;
			layers.push_back((const XrCompositionLayerBaseHeader *)&passthroughLayer);
		}
		if (app.screenPlaced && Scene::usesQuadLayer(app.environment) && renderQuad(app)) {
			quadLayer.space = app.localSpace;
			quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			quadLayer.subImage.swapchain = app.quadSwapchain.handle;
			quadLayer.subImage.imageRect.offset = {0, 0};
			quadLayer.subImage.imageRect.extent = {app.quadRectW, app.quadRectH};
			quadLayer.pose.orientation = toXr(screenRot(app));
			quadLayer.pose.position = toXr(app.screenPos);
			if (app.environment == lolvr::kEnvWindow)
				quadLayer.size = {app.dispW, app.dispH};
			else
				quadLayer.size = {app.dispW * kQuadScale, app.dispH * kQuadScale};
			layers.push_back((const XrCompositionLayerBaseHeader *)&quadLayer);
		}

		XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locateInfo.displayTime = time;
		locateInfo.space = app.localSpace;
		XrViewState viewState{XR_TYPE_VIEW_STATE};
		XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
		uint32_t viewCount = 0;
		bool viewsOk = XR_SUCCEEDED(xrLocateViews(app.session, &locateInfo, &viewState, 2, &viewCount, views)) && viewCount == 2 &&
		               (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
		               (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
		if (viewsOk && app.screenPlaced) {
			bool ok = true;
			for (int eye = 0; eye < 2 && ok; eye++) {
				Swapchain &sc = app.eyeSwapchains[eye];
				uint32_t index = 0;
				if (!acquireImage(sc, index)) {
					ok = false;
					break;
				}
				bindEyeTarget(app, eye, sc.images[index].image);
				glViewport(0, 0, sc.width, sc.height);
				const XrFovf &fov = views[eye].fov;
				Mat4 proj = lolvr::projectionFromTangents(tanf(fov.angleLeft), tanf(fov.angleRight), tanf(fov.angleDown), tanf(fov.angleUp), 0.05f, 60.0f);
				Mat4 view = lolvr::viewFromPose(toVec3(views[eye].pose.position), toQuat(views[eye].pose.orientation));
				app.scene.render(view, proj, app.ss);
				const GLenum discard[] = {GL_DEPTH_ATTACHMENT};
				glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, discard);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				releaseImage(sc);
				projViews[eye].pose = views[eye].pose;
				projViews[eye].fov = views[eye].fov;
				projViews[eye].subImage.swapchain = sc.handle;
				projViews[eye].subImage.imageRect.offset = {0, 0};
				projViews[eye].subImage.imageRect.extent = {sc.width, sc.height};
			}
			if (ok) {
				// Le décor est en alpha prémultiplié : là où il est transparent (l'ouverture du
				// cadre, ou tout ce qui entoure l'écran en réalité mixte), on voit les calques du dessous.
				projLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
				projLayer.space = app.localSpace;
				projLayer.viewCount = 2;
				projLayer.views = projViews;
				layers.push_back((const XrCompositionLayerBaseHeader *)&projLayer);
			}
		}
	}

	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	endInfo.displayTime = frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = (uint32_t)layers.size();
	endInfo.layers = layers.empty() ? nullptr : layers.data();
	xrEndFrame(app.session, &endInfo);
}

// ---------------------------------------------------------------------------
// Boucle principale

void releaseAllKeys(App &app) {
	Button *all[] = {&app.kUp, &app.kDown, &app.kSlideL, &app.kSlideR, &app.kTurnL, &app.kTurnR,
	                 &app.kEsc, &app.kMap, &app.kEnter};
	const unsigned keys[] = {RETROK_UP, RETROK_DOWN, RETROK_LEFT, RETROK_RIGHT, RETROK_HOME, RETROK_PAGEUP,
	                         RETROK_ESCAPE, RETROK_m, RETROK_RETURN};
	for (int i = 0; i < 9; i++)
		setKey(*all[i], false, keys[i]);
	CoreHost::get().setSpecialButtons(false, false);
	app.triggerDown[0] = app.triggerDown[1] = false;
	app.squeezeDown[0] = app.squeezeDown[1] = false;
	app.triggerValue[0] = app.triggerValue[1] = 0.0f;
	app.squeezeValue[0] = app.squeezeValue[1] = 0.0f;
	app.zoomDown = false;
	app.recenterDown = false;
	CoreHost::get().setMouseButtons(false, false);
}

void pollXrEvents(App &app) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	while (xrPollEvent(app.instance, &event) == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event;
			app.sessionState = changed->state;
			LOGI("état de la session : %d", (int)changed->state);
			switch (changed->state) {
			case XR_SESSION_STATE_READY: {
				XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
				begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (XR_SUCCEEDED(xrBeginSession(app.session, &begin)))
					app.sessionRunning = true;
				break;
			}
			case XR_SESSION_STATE_STOPPING:
				xrEndSession(app.session);
				app.sessionRunning = false;
				break;
			case XR_SESSION_STATE_EXITING:
			case XR_SESSION_STATE_LOSS_PENDING:
				app.exitRequested = true;
				ANativeActivity_finish(app.androidApp->activity);
				break;
			default:
				break;
			}
			// Le jeu ne tourne que lorsque le casque est porté et l'application visible.
			bool active = changed->state == XR_SESSION_STATE_VISIBLE || changed->state == XR_SESSION_STATE_FOCUSED;
			CoreHost::get().setPaused(!active);
			if (!active)
				Ambience::get().setMood(false, app.shownTheme);
			if (changed->state != XR_SESSION_STATE_FOCUSED)
				releaseAllKeys(app);
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			app.exitRequested = true;
			ANativeActivity_finish(app.androidApp->activity);
			break;
		default:
			break;
		}
		event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
	}
}

void onAppCmd(android_app *androidApp, int32_t cmd) {
	App *app = (App *)androidApp->userData;
	if (cmd == APP_CMD_RESUME)
		app->resumed = true;
	else if (cmd == APP_CMD_PAUSE)
		app->resumed = false;
}

} // namespace

void android_main(android_app *androidApp) {
	App app;
	app.androidApp = androidApp;
	androidApp->userData = &app;
	androidApp->onAppCmd = onAppCmd;

	JNIEnv *env = nullptr;
	androidApp->activity->vm->AttachCurrentThread(&env, nullptr);

	ANativeActivity *activity = androidApp->activity;
	std::string internal = activity->internalDataPath ? activity->internalDataPath : "/data/local/tmp";
	std::string external = activity->externalDataPath ? activity->externalDataPath : internal;
	app.systemDir = internal + "/system";
	app.saveDir = external + "/sauvegardes";
	app.gameRoot = external + "/lol";
	app.settingsPath = internal + "/reglages.txt";
	makeDirs(app.systemDir);
	makeDirs(app.saveDir);
	makeDirs(app.gameRoot);
	extractAssetDir(activity->assetManager, "scummvm/theme", app.systemDir + "/scummvm/theme");
	extractAssetDir(activity->assetManager, "scummvm/extra", app.systemDir + "/scummvm/extra");
	LOGI("dossier du jeu : %s", app.gameRoot.c_str());
	int savedEnvironment = lolvr::kEnvLibrary;
	loadSettings(app, savedEnvironment);
	app.startTime = nowSeconds();

	// Tant que la session VR n'est pas active, le jeu reste en pause.
	CoreHost::get().setPaused(true);

	bool ready = initEgl(app.egl) && initXr(app) && initGl(app);
	if (!ready) {
		LOGE("initialisation impossible : fermeture");
		ANativeActivity_finish(activity);
	} else {
		// Le son démarre tout de suite : l'ambiance de la pièce accompagne même l'écran d'accueil.
		app.audio = startAudio(app.audioRate);
	}

	double lastTime = nowSeconds();
	while (!androidApp->destroyRequested) {
		// Événements Android : on attend seulement quand il n'y a rien à afficher.
		for (;;) {
			int events = 0;
			android_poll_source *source = nullptr;
			int timeout = (ready && app.sessionRunning) || androidApp->destroyRequested ? 0 : 100;
			if (ALooper_pollOnce(timeout, nullptr, &events, (void **)&source) < 0)
				break;
			if (source)
				source->process(androidApp, source);
			if (androidApp->destroyRequested)
				break;
		}
		if (androidApp->destroyRequested || !ready)
			continue;

		pollXrEvents(app);
		updateGame(app);

		double now = nowSeconds();
		float dt = (float)fmin(now - lastTime, 0.1);
		lastTime = now;
		if (app.sessionRunning)
			renderFrame(app, dt, savedEnvironment);
	}

	// Fermeture : on arrête le son et le jeu, puis on quitte le processus pour repartir
	// d'un état propre au prochain lancement.
	stopAudio(app.audio);
	CoreHost::get().stop();
	if (app.session != XR_NULL_HANDLE)
		xrDestroySession(app.session);
	if (app.instance != XR_NULL_HANDLE)
		xrDestroyInstance(app.instance);
	androidApp->activity->vm->DetachCurrentThread();
	LOGI("fin de l'application");
	_exit(0);
}
