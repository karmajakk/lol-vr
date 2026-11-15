// lol-vr — application Meta Quest (étape 1) : Lands of Lore sur un écran flottant en VR.
//
// Application Android 100 % native (NativeActivity) :
//   - OpenXR pour le casque et les manettes ;
//   - l'image du jeu est affichée sur un calque « quad » (netteté maximale) ;
//   - un calque de projection transparent dessine le rayon de la manette ;
//   - AAudio pour le son ;
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

#include "core_host.h"
#include "libretro.h"

#define LOG_TAG "lolvr"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

using lolvr::CoreHost;
using lolvr::Frame;

namespace {

// ---------------------------------------------------------------------------
// Petites maths

struct Vec3 {
	float x, y, z;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalize(Vec3 a) {
	float l = sqrtf(dot(a, a));
	return l > 1e-6f ? a * (1.0f / l) : Vec3{0, 0, 0};
}
Vec3 toVec3(const XrVector3f &v) { return {v.x, v.y, v.z}; }

Vec3 rotate(const XrQuaternionf &q, Vec3 v) {
	Vec3 u{q.x, q.y, q.z};
	Vec3 t = cross(u, v) * 2.0f;
	return v + t * q.w + cross(u, t);
}

XrQuaternionf conjugate(const XrQuaternionf &q) { return {-q.x, -q.y, -q.z, q.w}; }

struct Mat4 {
	float m[16]; // colonnes d'abord (convention OpenGL)
};

Mat4 multiply(const Mat4 &a, const Mat4 &b) {
	Mat4 r;
	for (int c = 0; c < 4; c++)
		for (int row = 0; row < 4; row++) {
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += a.m[k * 4 + row] * b.m[c * 4 + k];
			r.m[c * 4 + row] = s;
		}
	return r;
}

Mat4 projectionFromFov(const XrFovf &fov, float nearZ, float farZ) {
	float l = tanf(fov.angleLeft), r = tanf(fov.angleRight);
	float d = tanf(fov.angleDown), u = tanf(fov.angleUp);
	Mat4 p;
	memset(&p, 0, sizeof(p));
	p.m[0] = 2.0f / (r - l);
	p.m[5] = 2.0f / (u - d);
	p.m[8] = (r + l) / (r - l);
	p.m[9] = (u + d) / (u - d);
	p.m[10] = -(farZ + nearZ) / (farZ - nearZ);
	p.m[11] = -1.0f;
	p.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
	return p;
}

// Matrice de vue = inverse de la pose de l'œil.
Mat4 viewFromPose(const XrPosef &pose) {
	XrQuaternionf inv = conjugate(pose.orientation);
	Vec3 x = rotate(inv, {1, 0, 0});
	Vec3 y = rotate(inv, {0, 1, 0});
	Vec3 z = rotate(inv, {0, 0, 1});
	Vec3 t = rotate(inv, toVec3(pose.position) * -1.0f);
	Mat4 v;
	v.m[0] = x.x; v.m[1] = x.y; v.m[2] = x.z; v.m[3] = 0;
	v.m[4] = y.x; v.m[5] = y.y; v.m[6] = y.z; v.m[7] = 0;
	v.m[8] = z.x; v.m[9] = z.y; v.m[10] = z.z; v.m[11] = 0;
	v.m[12] = t.x; v.m[13] = t.y; v.m[14] = t.z; v.m[15] = 1;
	return v;
}

double nowSeconds() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------------------
// Fichiers

bool fileExists(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

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
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

AAudioStream *startAudio(int sampleRate) {
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
in vec2 vUv;
out vec4 oColor;
void main() {
	vec2 texel = vUv * uTexSize;
	vec2 i = floor(texel);
	vec2 f = texel - i;
	vec2 scale = max(uOutSize / uTexSize, vec2(1.0));
	vec2 region = 0.5 - 0.5 / scale;
	vec2 c = f - 0.5;
	vec2 fpp = (c - clamp(c, -region, region)) * scale + 0.5;
	oColor = vec4(texture(uTex, (i + fpp) / uTexSize).rgb, 1.0);
}
)";

const char *kColorVs = R"(#version 300 es
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char *kColorFs = R"(#version 300 es
precision mediump float;
uniform vec4 uColor;
out vec4 oColor;
void main() {
	oColor = uColor;
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

struct App {
	android_app *androidApp = nullptr;
	bool resumed = false;

	Egl egl;

	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	bool sessionRunning = false;
	bool exitRequested = false;

	bool srgb = false;
	std::vector<XrViewConfigurationView> viewConfigs;
	Swapchain eyeSwapchains[2];
	Swapchain quadSwapchain;

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
	XrSpace aimSpaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

	// OpenGL
	GLuint fbo = 0;
	GLuint blitProgram = 0;
	GLint blitTexSize = -1, blitOutSize = -1;
	GLuint colorProgram = 0;
	GLint colorMvp = -1, colorColor = -1;
	GLuint lineVao = 0, lineVbo = 0;
	GLuint srcTex = 0;
	int srcTexW = 0, srcTexH = 0;

	// Écran virtuel
	bool screenPlaced = false;
	XrPosef screenPose = {{0, 0, 0, 1}, {0, 0, -2.2f}};
	float screenWidth = 2.4f; // mètres ; hauteur = largeur * 3/4
	float screenDistance = 2.2f;

	// Pointeur
	int activeHand = kRight;
	bool aimValid[2] = {false, false};
	XrPosef aimPose[2];
	bool hitValid = false;
	Vec3 hitPoint = {0, 0, 0};
	bool triggerDown[2] = {false, false};
	bool squeezeDown[2] = {false, false};

	// Touches
	Button kUp, kDown, kSlideL, kSlideR, kTurnL, kTurnR, kEsc, kMap, kEnter;
	bool recenterDown = false;

	// Jeu
	std::string systemDir, saveDir, gameRoot, gameDir;
	bool coreStarted = false;
	double nextScan = 0;
	Frame frame;
	bool frameDirty = false;
	AAudioStream *audio = nullptr;
};

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
	    !createAction(app, app.menuAction, "bouton_menu", "Bouton menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false))
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

bool initXr(App &app) {
	ANativeActivity *activity = app.androidApp->activity;

	// Le chargeur OpenXR d'Android doit être initialisé avant toute autre fonction.
	PFN_xrInitializeLoaderKHR initLoader = nullptr;
	XR_CHECK(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&initLoader));
	XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
	loaderInfo.applicationVM = activity->vm;
	loaderInfo.applicationContext = activity->clazz;
	XR_CHECK(initLoader((const XrLoaderInitInfoBaseHeaderKHR *)&loaderInfo));

	const char *extensions[] = {
		XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
		XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
	};
	XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	androidInfo.applicationVM = activity->vm;
	androidInfo.applicationActivity = activity->clazz;
	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.next = &androidInfo;
	strcpy(instanceInfo.applicationInfo.applicationName, "Lands of Lore VR");
	instanceInfo.applicationInfo.applicationVersion = 1;
	strcpy(instanceInfo.applicationInfo.engineName, "lol-vr");
	instanceInfo.applicationInfo.engineVersion = 1;
	instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	instanceInfo.enabledExtensionCount = 2;
	instanceInfo.enabledExtensionNames = extensions;
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
	if (!createSwapchain(app, 1600, 1200, app.quadSwapchain))
		return false;

	return initActions(app);
}

bool initGl(App &app) {
	glGenFramebuffers(1, &app.fbo);
	app.blitProgram = linkProgram(kBlitVs, kBlitFs);
	app.blitTexSize = glGetUniformLocation(app.blitProgram, "uTexSize");
	app.blitOutSize = glGetUniformLocation(app.blitProgram, "uOutSize");
	app.colorProgram = linkProgram(kColorVs, kColorFs);
	app.colorMvp = glGetUniformLocation(app.colorProgram, "uMvp");
	app.colorColor = glGetUniformLocation(app.colorProgram, "uColor");
	glGenVertexArrays(1, &app.lineVao);
	glGenBuffers(1, &app.lineVbo);
	glBindVertexArray(app.lineVao);
	glBindBuffer(GL_ARRAY_BUFFER, app.lineVbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glBindVertexArray(0);
	glGenTextures(1, &app.srcTex);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	return true;
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
			if (host.start(cfg)) {
				app.coreStarted = true;
				app.audio = startAudio(host.sampleRate());
			}
		} else if (app.frame.serial == 0) {
			showMissingFilesMessage(app);
		}
		return;
	}
	if (host.fetchFrame(app.frame))
		app.frameDirty = true;
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

void placeScreen(App &app, const XrPosef &head) {
	Vec3 fwd = rotate(head.orientation, {0, 0, -1});
	float yaw = atan2f(-fwd.x, -fwd.z);
	XrQuaternionf q{0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f)};
	Vec3 flat = rotate(q, {0, 0, -1});
	Vec3 pos = toVec3(head.position) + flat * app.screenDistance;
	pos.y = head.position.y - 0.05f;
	app.screenPose.orientation = q;
	app.screenPose.position = {pos.x, pos.y, pos.z};
	app.screenPlaced = true;
}

void updateInput(App &app, XrTime time, float dt) {
	XrActiveActionSet active{app.actionSet, XR_NULL_PATH};
	XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	xrSyncActions(app.session, &sync);

	// Position de la tête : place l'écran au premier passage et sur demande.
	XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
	bool headValid = XR_SUCCEEDED(xrLocateSpace(app.viewSpace, app.localSpace, time, &head)) &&
	                 (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
	                 (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
	bool recenter = getBool(app, app.stickClickAction, kRight);
	if (headValid && (!app.screenPlaced || (recenter && !app.recenterDown)))
		placeScreen(app, head.pose);
	app.recenterDown = recenter;

	// Rayons des manettes
	for (int h = 0; h < 2; h++) {
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		app.aimValid[h] = XR_SUCCEEDED(xrLocateSpace(app.aimSpaces[h], app.localSpace, time, &loc)) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
		if (app.aimValid[h])
			app.aimPose[h] = loc.pose;
	}

	// Gâchette = clic gauche, poignée = clic droit. La main qui clique devient le pointeur.
	bool trigger[2], squeeze[2];
	for (int h = 0; h < 2; h++) {
		trigger[h] = threshold(app.triggerDown[h], getFloat(app, app.triggerAction, h));
		squeeze[h] = threshold(app.squeezeDown[h], getFloat(app, app.squeezeAction, h));
		bool pressedNow = (trigger[h] && !app.triggerDown[h]) || (squeeze[h] && !app.squeezeDown[h]);
		int other = 1 - h;
		if (pressedNow && app.aimValid[h] && !app.triggerDown[other] && !app.squeezeDown[other])
			app.activeHand = h;
		app.triggerDown[h] = trigger[h];
		app.squeezeDown[h] = squeeze[h];
	}
	if (!app.aimValid[app.activeHand] && app.aimValid[1 - app.activeHand])
		app.activeHand = 1 - app.activeHand;

	// Intersection du rayon avec l'écran
	app.hitValid = false;
	int hand = app.activeHand;
	float screenHeight = app.screenWidth * 0.75f;
	if (app.aimValid[hand] && app.screenPlaced) {
		XrQuaternionf inv = conjugate(app.screenPose.orientation);
		Vec3 origin = rotate(inv, toVec3(app.aimPose[hand].position) - toVec3(app.screenPose.position));
		Vec3 dir = rotate(inv, rotate(app.aimPose[hand].orientation, {0, 0, -1}));
		if (dir.z < -1e-4f && origin.z > 0.0f) {
			float t = -origin.z / dir.z;
			Vec3 hit = origin + dir * t;
			float u = hit.x / app.screenWidth + 0.5f;
			float v = 0.5f - hit.y / screenHeight;
			if (u > -0.15f && u < 1.15f && v > -0.15f && v < 1.15f) {
				u = fminf(fmaxf(u, 0.0f), 1.0f);
				v = fminf(fmaxf(v, 0.0f), 1.0f);
				CoreHost::get().setPointer(u, v);
				Vec3 local{(u - 0.5f) * app.screenWidth, (0.5f - v) * screenHeight, 0.004f};
				app.hitPoint = rotate(app.screenPose.orientation, local) + toVec3(app.screenPose.position);
				app.hitValid = true;
			}
		}
	}
	CoreHost::get().setMouseButtons(app.triggerDown[hand] && app.hitValid, app.squeezeDown[hand] && app.hitValid);

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

	// Clic du joystick gauche maintenu + joystick droit haut/bas : taille de l'écran.
	if (zoomMode && fabsf(right.y) > 0.3f) {
		app.screenWidth *= 1.0f + right.y * 0.8f * dt;
		app.screenWidth = fminf(fmaxf(app.screenWidth, 0.8f), 6.0f);
	}

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
	glBindFramebuffer(GL_FRAMEBUFFER, app.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.quadSwapchain.images[index].image, 0);
	glViewport(0, 0, app.quadSwapchain.width, app.quadSwapchain.height);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	if (app.srcTexW > 0) {
		glUseProgram(app.blitProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, app.srcTex);
		glUniform2f(app.blitTexSize, (float)app.srcTexW, (float)app.srcTexH);
		glUniform2f(app.blitOutSize, (float)app.quadSwapchain.width, (float)app.quadSwapchain.height);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	releaseImage(app.quadSwapchain);
	return true;
}

void pushQuad(std::vector<float> &v, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
	const Vec3 tri[6] = {a, b, c, a, c, d};
	for (const Vec3 &p : tri) {
		v.push_back(p.x);
		v.push_back(p.y);
		v.push_back(p.z);
	}
}

// Couleur sRGB -> valeur à écrire selon le format de l'image.
float channel(const App &app, float c) {
	return app.srgb ? powf(c, 2.2f) : c;
}

void renderEye(App &app, int eye, const XrView &view, std::vector<float> &rayVerts, std::vector<float> &dotVerts) {
	Swapchain &sc = app.eyeSwapchains[eye];
	glBindFramebuffer(GL_FRAMEBUFFER, app.fbo);
	glViewport(0, 0, sc.width, sc.height);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	Mat4 mvp = multiply(projectionFromFov(view.fov, 0.05f, 100.0f), viewFromPose(view.pose));
	glUseProgram(app.colorProgram);
	glUniformMatrix4fv(app.colorMvp, 1, GL_FALSE, mvp.m);
	glBindVertexArray(app.lineVao);
	glBindBuffer(GL_ARRAY_BUFFER, app.lineVbo);
	if (!rayVerts.empty()) {
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(rayVerts.size() * sizeof(float)), rayVerts.data(), GL_DYNAMIC_DRAW);
		glUniform4f(app.colorColor, channel(app, 0.95f), channel(app, 0.75f), channel(app, 0.25f), 1.0f);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(rayVerts.size() / 3));
	}
	if (!dotVerts.empty()) {
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(dotVerts.size() * sizeof(float)), dotVerts.data(), GL_DYNAMIC_DRAW);
		glUniform4f(app.colorColor, 1.0f, 1.0f, 1.0f, 1.0f);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(dotVerts.size() / 3));
	}
	glBindVertexArray(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void buildPointerGeometry(App &app, std::vector<float> &rayVerts, std::vector<float> &dotVerts) {
	int hand = app.activeHand;
	if (!app.aimValid[hand])
		return;
	Vec3 origin = toVec3(app.aimPose[hand].position);
	Vec3 dir = rotate(app.aimPose[hand].orientation, {0, 0, -1});
	Vec3 start = origin + dir * 0.05f;
	Vec3 end = app.hitValid ? app.hitPoint : origin + dir * 1.5f;
	Vec3 axis = normalize(end - start);
	Vec3 side = normalize(cross(axis, {0, 1, 0}));
	if (dot(side, side) < 0.5f)
		side = {1, 0, 0};
	Vec3 up = normalize(cross(side, axis));
	const float w0 = 0.0025f, w1 = 0.0012f;
	pushQuad(rayVerts, start - side * w0, start + side * w0, end + side * w1, end - side * w1);
	pushQuad(rayVerts, start - up * w0, start + up * w0, end + up * w1, end - up * w1);

	if (app.hitValid) {
		Vec3 sx = rotate(app.screenPose.orientation, {1, 0, 0}) * 0.007f;
		Vec3 sy = rotate(app.screenPose.orientation, {0, 1, 0}) * 0.007f;
		Vec3 c = app.hitPoint;
		pushQuad(dotVerts, c - sx - sy, c + sx - sy, c + sx + sy, c - sx + sy);
	}
}

void renderFrame(App &app, float dt) {
	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	XrFrameState frameState{XR_TYPE_FRAME_STATE};
	if (XR_FAILED(xrWaitFrame(app.session, &waitInfo, &frameState)))
		return;
	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	if (XR_FAILED(xrBeginFrame(app.session, &beginInfo)))
		return;

	XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
	XrCompositionLayerProjection projLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerProjectionView projViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
	std::vector<const XrCompositionLayerBaseHeader *> layers;

	if (frameState.shouldRender) {
		if (app.sessionState == XR_SESSION_STATE_FOCUSED)
			updateInput(app, frameState.predictedDisplayTime, dt);
		else
			CoreHost::get().setMouseButtons(false, false);

		uploadFrame(app);

		if (app.screenPlaced && renderQuad(app)) {
			quadLayer.space = app.localSpace;
			quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			quadLayer.subImage.swapchain = app.quadSwapchain.handle;
			quadLayer.subImage.imageRect.offset = {0, 0};
			quadLayer.subImage.imageRect.extent = {app.quadSwapchain.width, app.quadSwapchain.height};
			quadLayer.pose = app.screenPose;
			quadLayer.size = {app.screenWidth, app.screenWidth * 0.75f};
			layers.push_back((const XrCompositionLayerBaseHeader *)&quadLayer);
		}

		XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locateInfo.displayTime = frameState.predictedDisplayTime;
		locateInfo.space = app.localSpace;
		XrViewState viewState{XR_TYPE_VIEW_STATE};
		XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
		uint32_t viewCount = 0;
		bool viewsOk = XR_SUCCEEDED(xrLocateViews(app.session, &locateInfo, &viewState, 2, &viewCount, views)) && viewCount == 2 &&
		               (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
		               (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
		if (viewsOk) {
			std::vector<float> rayVerts, dotVerts;
			if (app.sessionState == XR_SESSION_STATE_FOCUSED)
				buildPointerGeometry(app, rayVerts, dotVerts);
			bool ok = true;
			for (int eye = 0; eye < 2 && ok; eye++) {
				uint32_t index = 0;
				if (!acquireImage(app.eyeSwapchains[eye], index)) {
					ok = false;
					break;
				}
				glBindFramebuffer(GL_FRAMEBUFFER, app.fbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.eyeSwapchains[eye].images[index].image, 0);
				renderEye(app, eye, views[eye], rayVerts, dotVerts);
				releaseImage(app.eyeSwapchains[eye]);
				projViews[eye].pose = views[eye].pose;
				projViews[eye].fov = views[eye].fov;
				projViews[eye].subImage.swapchain = app.eyeSwapchains[eye].handle;
				projViews[eye].subImage.imageRect.offset = {0, 0};
				projViews[eye].subImage.imageRect.extent = {app.eyeSwapchains[eye].width, app.eyeSwapchains[eye].height};
			}
			if (ok) {
				// Calque transparent posé par-dessus l'écran : seuls le rayon et le point sont visibles.
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
	makeDirs(app.systemDir);
	makeDirs(app.saveDir);
	makeDirs(app.gameRoot);
	extractAssetDir(activity->assetManager, "scummvm/theme", app.systemDir + "/scummvm/theme");
	extractAssetDir(activity->assetManager, "scummvm/extra", app.systemDir + "/scummvm/extra");
	LOGI("dossier du jeu : %s", app.gameRoot.c_str());

	// Tant que la session VR n'est pas active, le jeu reste en pause.
	CoreHost::get().setPaused(true);

	bool ready = initEgl(app.egl) && initXr(app) && initGl(app);
	if (!ready) {
		LOGE("initialisation impossible : fermeture");
		ANativeActivity_finish(activity);
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
			renderFrame(app, dt);
	}

	// Fermeture : on arrête le son et le jeu, puis on quitte le processus pour repartir
	// d'un état propre au prochain lancement.
	if (app.audio) {
		AAudioStream_requestStop(app.audio);
		AAudioStream_close(app.audio);
	}
	CoreHost::get().stop();
	if (app.session != XR_NULL_HANDLE)
		xrDestroySession(app.session);
	if (app.instance != XR_NULL_HANDLE)
		xrDestroyInstance(app.instance);
	androidApp->activity->vm->DetachCurrentThread();
	LOGI("fin de l'application");
	_exit(0);
}
