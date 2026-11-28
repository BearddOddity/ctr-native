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

	// Extras (the launcher's Extras tab): opt-in changes to how the game plays. All off by default, so the
	// game plays as the disc does. platform/native_extras.c applies them.
	int skipIntro; // boot past the Naughty Dog crate and the "Sony Computer Entertainment presents" voice
	int unlockAll; // every character, track, battle arena and the scrapbook, as the game's own unlock codes do
	int cheats;    // NATIVE_CHEAT_* bits: the game's own main-menu cheat codes, always on

	// Camera (Display tab). The race camera's horizontal field of view in degrees for one player, the PS1's
	// 90 by default (split screen views scale with it), and its distance behind the kart in percent.
	int fov;
	int cameraDistance;
};

#define NATIVE_CONFIG_FOV_DEFAULT 90
#define NATIVE_CONFIG_FOV_MIN     60
#define NATIVE_CONFIG_FOV_MAX     120
#define NATIVE_CONFIG_CAMERA_DISTANCE_MIN 50
#define NATIVE_CONFIG_CAMERA_DISTANCE_MAX 200

// The game's main-menu cheat codes, by their ctr_native.ini key (cheat_<name>).
enum
{
	NATIVE_CHEAT_WUMPA,      // 99 Wumpa fruit
	NATIVE_CHEAT_MASKS,      // infinite Aku Aku / Uka Uka masks
	NATIVE_CHEAT_TURBOS,     // infinite turbo boosts
	NATIVE_CHEAT_BOMBS,      // infinite bombs
	NATIVE_CHEAT_INVISIBLE,  // infinite invisibility
	NATIVE_CHEAT_ENGINE,     // super engine
	NATIVE_CHEAT_ICY,        // icy tracks
	NATIVE_CHEAT_TURBO_PADS, // super turbo pads
	NATIVE_CHEAT_TURBO_COUNT, // the turbo counter on the HUD
	NATIVE_CHEAT_ONE_LAP,    // one-lap races
	NATIVE_CHEAT_SUPER_HARD, // super hard AI
	NATIVE_CHEAT_HARD_ADV,   // harder adventure AI
	NATIVE_CHEAT_COUNT
};

extern const char *const g_nativeCheatKeys[NATIVE_CHEAT_COUNT];

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
