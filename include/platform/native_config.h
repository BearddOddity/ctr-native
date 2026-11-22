#ifndef NATIVE_CONFIG_H
#define NATIVE_CONFIG_H

// Player settings for the native build: window size, presentation aspect and
// which input devices the game listens to. They are read from ctr_native.ini
// (the CTR Launcher writes it) and can be overridden on the command line.

#define NATIVE_CONFIG_FILE_NAME   "ctr_native.ini"
#define NATIVE_CONFIG_MIN_WIDTH   320
#define NATIVE_CONFIG_MIN_HEIGHT  240
#define NATIVE_CONFIG_MAX_WIDTH   7680
#define NATIVE_CONFIG_MAX_HEIGHT  4320
#define NATIVE_CONFIG_MAX_RENDER_SCALE 8

struct NativeConfig
{
	int width;
	int height;
	int fullscreen;
	// 1: the PSX frame fills the window's aspect (16:9, 16:10). 0: 4:3, pillarboxed.
	int widescreen;
	int keyboard;
	int controller;
	// Controller families the game opens when controller support is on.
	int xboxControllers;
	int playstationControllers;
	int vibration;

	// Graphics: how the PSX frame reaches the window.
	int vsync;
	int upscale;      // NATIVE_UPSCALE_*
	int sharpness;    // FSR sharpening: 0 low, 1 medium, 2 high
	int antialias;    // NATIVE_AA_*
	int crt;          // NATIVE_CRT_*
	int textureFilter; // bilinear texture filtering in 3D (the PSX had none)
	int brightness;   // -2 .. 2
	int integerScale; // whole-number scaling only (sharp pixels, bars around)
	// Internal resolution: the 3D is drawn at the PSX display size times this (1 = the PlayStation's own).
	int renderScale;
};

enum
{
	NATIVE_UPSCALE_SHARP,  // nearest neighbour: the original pixels
	NATIVE_UPSCALE_SMOOTH, // bilinear
	NATIVE_UPSCALE_FSR,    // AMD FidelityFX Super Resolution 1 (EASU + RCAS)
};

enum
{
	NATIVE_AA_OFF,
	NATIVE_AA_FXAA,
};

enum
{
	NATIVE_CRT_OFF,
	NATIVE_CRT_SCANLINES,
	NATIVE_CRT_CURVED, // scanlines, a shadow mask, curvature and a vignette
};

extern struct NativeConfig g_nativeConfig;

void NativeConfig_SetDefaults(struct NativeConfig *config);
// Returns 1 when the file was read, 0 when it is missing or unreadable.
int NativeConfig_LoadFile(struct NativeConfig *config, const char *path);
int NativeConfig_ParseText(struct NativeConfig *config, const char *text);
// Returns the value of --config, or NULL.
const char *NativeConfig_PathFromArgs(int argc, char **argv);
// Applies --width N, --height N, --resolution WxH, --fullscreen, --windowed,
// --widescreen, --no-widescreen, --no-keyboard, --no-controller, --no-xbox, --no-playstation,
// --vsync, --no-vsync, --upscale sharp|smooth|fsr, --aa off|fxaa, --crt off|scanlines|curved, --render-scale N.
void NativeConfig_ApplyArgs(struct NativeConfig *config, int argc, char **argv);
// CTR_NATIVE_WIDTH, _HEIGHT, _FULLSCREEN (1/0), _ASPECT ("16:9", "4:3"), _KEYBOARD, _GAMEPAD, _RENDER_SCALE.
void NativeConfig_ApplyEnv(struct NativeConfig *config);
void NativeConfig_Clamp(struct NativeConfig *config);

#endif
