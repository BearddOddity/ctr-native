// CTR Launcher: a small Windows front end for CTR Native.
//
// Two sides: the left always shows the game's cover (an image the player picks;
// a drawn placeholder otherwise). The right is either Play (drop or browse for
// your own disc, the recompile's progress as a race track, Play) or the Garage
// (Settings: Display, Graphics, Controls). Switching changes only the right side.
// The look is a kart-racing theme drawn in code; no game art is used.
//
// Play recompiles the game from the source next to the launcher with the
// compiler bundled in tools\ (llvm-mingw, CMake and Ninja), so nothing has to
// be installed, then starts it with the settings in ctr_native.ini.
//
// Plain Win32 and GDI; GDI+ is loaded at run time for PNG/JPEG covers.

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(_MSC_VER)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

#define APP_TITLE L"CTR Launcher"
#define GAME_TITLE L"Crash Team Racing"
#define PATH_CAP 1024

// ---------------------------------------------------------------------------
// Theme and layout (logical pixels at 96 DPI)
//
// A kart-racing look drawn in code: a sunset sky, an asphalt panel with a
// red-and-white curb, chunky outlined buttons and a race-track progress bar
// with a kart driving to the checkered flag. No game art is used.

#define COL_SKY_TOP     RGB(255, 170, 64)
#define COL_SKY_MID     RGB(236, 84, 92)
#define COL_SKY_BOTTOM  RGB(54, 26, 92)
#define COL_OUTLINE     RGB(26, 16, 38)
#define COL_SHADOW      RGB(20, 10, 34)
#define COL_ASPHALT     RGB(38, 38, 48)
#define COL_ASPHALT_LT  RGB(52, 52, 64)
#define COL_ASPHALT_DK  RGB(26, 26, 34)
#define COL_CARD        RGB(48, 48, 60)
#define COL_CURB_RED    RGB(222, 44, 44)
#define COL_CURB_WHITE  RGB(244, 240, 232)
#define COL_TEXT        RGB(246, 242, 236)
#define COL_TEXT_SOFT   RGB(190, 186, 200)
#define COL_TEXT_FAINT  RGB(130, 128, 146)
#define COL_YELLOW      RGB(255, 214, 10)
#define COL_ORANGE      RGB(255, 132, 0)
#define COL_ORANGE_LT   RGB(255, 176, 58)
#define COL_ORANGE_DK   RGB(214, 96, 0)
#define COL_JUNGLE      RGB(40, 160, 86)
#define COL_JUNGLE_LT   RGB(84, 200, 120)
#define COL_BLUE        RGB(52, 120, 230)
#define COL_BLUE_LT     RGB(100, 160, 255)
#define COL_OK          RGB(84, 214, 120)
#define COL_WARN        RGB(255, 190, 60)
#define COL_BAD         RGB(255, 92, 92)
#define COL_GREY        RGB(96, 96, 110)
#define COL_GREY_LT     RGB(130, 130, 146)

#define CLIENT_W 1120
#define CLIENT_H 700

// Left: the cover. Right: Play or Settings.
#define LP_L 28
#define LP_T 28
#define LP_R 532
#define LP_B 672
#define RP_L 556
#define RP_T 28
#define RP_R 1092
#define RP_B 672

#define RX (RP_L + 36)
#define RR (RP_R - 36)

enum
{
	ID_PLAY = 100,
	ID_REBUILD,
	ID_LOG,
	ID_BROWSE,
	ID_SETTINGS,
	ID_TAB_DISPLAY,
	ID_TAB_GRAPHICS,
	ID_TAB_CONTROLS,
	ID_RESOLUTION,
	ID_UPSCALE,
	ID_SHARPNESS,
	ID_AA,
	ID_CRT,
	ID_BRIGHTNESS,
	// Toggle switches (one contiguous run: IsToggle).
	ID_FULLSCREEN,
	ID_WIDESCREEN,
	ID_VSYNC,
	ID_INTSCALE,
	ID_TEXFILTER,
	ID_KEYBOARD,
	ID_CONTROLLER,
	ID_XBOX,
	ID_PLAYSTATION,
	ID_VIBRATION,
};

enum
{
	WM_APP_STATUS = WM_APP + 1, // lParam: heap WCHAR* (freed by the UI)
	WM_APP_STEP,                // wParam: done, lParam: total
	WM_APP_PHASE,               // wParam: phase
	WM_APP_DONE,                // wParam: 1 ok, 0 failed
	WM_APP_MINIMIZE,
	WM_APP_RESTORE,
};

enum
{
	PHASE_IDLE,
	PHASE_COPY,
	PHASE_CONFIGURE,
	PHASE_BUILD,
	PHASE_LAUNCH,
	PHASE_PLAYING,
	PHASE_DONE,
	PHASE_FAILED,
};

enum
{
	PAGE_HOME,
	PAGE_SETTINGS,
};

enum
{
	TAB_DISPLAY,
	TAB_GRAPHICS,
	TAB_CONTROLS,
};

enum
{
	MODE_PREBUILT, // no source next to us: play the exe that is there
	MODE_PACKAGE,  // source\ beside the launcher: the release folder
	MODE_DEV,      // the launcher sits in a ctr-native checkout
};

enum
{
	TASK_PLAY = 1,
	TASK_CLEAN = 2,
};

struct Resolution
{
	int w, h;
	const wchar_t *aspect;
};

static const struct Resolution s_resolutions[] = {
	{1280, 800, L"16:10"},  {1280, 960, L"4:3"},    {1366, 768, L"16:9"},
	{1440, 900, L"16:10"},  {1600, 900, L"16:9"},   {1680, 1050, L"16:10"},
	{1920, 1080, L"16:9"},  {1920, 1200, L"16:10"}, {2560, 1080, L"21:9"},
	{2560, 1440, L"16:9"},
};
#define RES_COUNT ((int)(sizeof(s_resolutions) / sizeof(s_resolutions[0])))

struct Settings
{
	int width, height;
	int fullscreen, widescreen, vsync, intscale;
	int upscale, sharpness, aa, crt, brightness, texfilter;
	int keyboard, controller, xbox, playstation, vibration;
};

// The words ctr_native.ini uses (platform/native_config.c reads the same).
static const wchar_t *const s_upscaleWords[] = {L"sharp", L"smooth", L"fsr"};
static const wchar_t *const s_upscaleNames[] = {L"Sharp pixels (original)", L"Smooth (bilinear)",
	L"AMD FSR 1 (EASU + RCAS)"};
static const wchar_t *const s_sharpWords[] = {L"low", L"medium", L"high"};
static const wchar_t *const s_sharpNames[] = {L"Low", L"Medium", L"High"};
static const wchar_t *const s_aaWords[] = {L"off", L"fxaa"};
static const wchar_t *const s_aaNames[] = {L"Off", L"FXAA"};
static const wchar_t *const s_crtWords[] = {L"off", L"scanlines", L"curved"};
static const wchar_t *const s_crtNames[] = {L"Off", L"Scanlines", L"Curved CRT (scanlines, mask, curve)"};
static const wchar_t *const s_brightNames[] = {L"Darker (-2)", L"Dark (-1)", L"Normal", L"Bright (+1)",
	L"Brighter (+2)"};

struct DiscInfo
{
	int state; // 0 none, 1 ok, 2 warning (playable), 3 refused
	wchar_t bin[PATH_CAP];
	wchar_t title[128];
	wchar_t detail[256];
};

struct Tools
{
	wchar_t cmake[PATH_CAP];
	wchar_t ninja[PATH_CAP];
	wchar_t cc[PATH_CAP];
	wchar_t cxx[PATH_CAP];
	wchar_t rc[PATH_CAP];
	wchar_t binDirs[4 * PATH_CAP];
	wchar_t command[PATH_CAP]; // [build] command= override (tests, custom setups)
	int bundled;
};

// ---------------------------------------------------------------------------
// State

static HINSTANCE g_inst;
static HWND g_wnd;
static float g_scale = 1.0f;

static wchar_t g_exeDir[PATH_CAP];
static wchar_t g_srcRoot[PATH_CAP];
static wchar_t g_dataRoot[PATH_CAP];
static wchar_t g_buildDir[PATH_CAP];
static wchar_t g_gameIni[PATH_CAP];
static wchar_t g_launcherIni[PATH_CAP];
static wchar_t g_logDir[PATH_CAP];
static wchar_t g_buildLog[PATH_CAP];
static wchar_t g_gameLog[PATH_CAP];
static int g_mode;

static struct Settings g_set;
static struct DiscInfo g_disc;
static wchar_t g_discSource[PATH_CAP];
static wchar_t g_coverPath[PATH_CAP];
static HBITMAP g_coverBmp;
static int g_coverW, g_coverH;
static HBITMAP g_coverScaled; // g_coverBmp at its size on the page (scaled once)
static int g_coverScaledW, g_coverScaledH;

static int g_page = PAGE_HOME;
static int g_tab = TAB_DISPLAY;
static HFONT g_fontTitle, g_fontHeading, g_fontBody, g_fontSmall, g_fontCaps, g_fontButton, g_fontBig, g_fontCover;
static HBRUSH g_cardBrush;

static HWND g_btnPlay, g_btnRebuild, g_btnLog, g_btnBrowse, g_btnSettings;
static HWND g_cmbRes, g_chkFull, g_chkWide, g_chkKb, g_chkPad, g_chkXbox, g_chkPs;
static HWND g_tabDisplay, g_tabGraphics, g_tabControls;
static HWND g_cmbUpscale, g_cmbSharp, g_cmbAa, g_cmbCrt, g_cmbBright;
static HWND g_chkVsync, g_chkIntScale, g_chkTexFilter, g_chkVibration;
static int g_customResIndex = -1;

static volatile LONG g_busy;
static volatile LONG g_cancel;
static HANDLE g_job;
static int g_phase = PHASE_IDLE;
static int g_stepDone, g_stepTotal;
static DWORD g_stepTick, g_phaseTick;
static double g_avgStepMs = 400.0;
static double g_shownPct;
static wchar_t g_status[512];
static wchar_t g_detail[512];
static int g_showLog;

static RECT g_rcCoverLink, g_rcDrop, g_rcCover;

#define S(x) ((int)((x) * g_scale + 0.5f))

// ---------------------------------------------------------------------------
// Small helpers

static void Copy(wchar_t *dst, size_t cap, const wchar_t *src)
{
	if (cap == 0)
	{
		return;
	}
	wcsncpy(dst, src != NULL ? src : L"", cap - 1);
	dst[cap - 1] = 0;
}

static void Join(wchar_t *dst, size_t cap, const wchar_t *a, const wchar_t *b)
{
	size_t n;

	Copy(dst, cap, a);
	n = wcslen(dst);
	if ((n > 0) && (dst[n - 1] != L'\\') && (dst[n - 1] != L'/') && (n + 1 < cap))
	{
		dst[n++] = L'\\';
		dst[n] = 0;
	}
	wcsncat(dst, b, cap - n - 1);
}

static int FileExists(const wchar_t *path)
{
	DWORD a = GetFileAttributesW(path);
	return (a != INVALID_FILE_ATTRIBUTES) && ((a & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static int DirExists(const wchar_t *path)
{
	DWORD a = GetFileAttributesW(path);
	return (a != INVALID_FILE_ATTRIBUTES) && ((a & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

static void ParentDir(wchar_t *path)
{
	wchar_t *slash = wcsrchr(path, L'\\');
	wchar_t *fwd = wcsrchr(path, L'/');

	if ((fwd != NULL) && ((slash == NULL) || (fwd > slash)))
	{
		slash = fwd;
	}
	if (slash != NULL)
	{
		*slash = 0;
	}
}

static const wchar_t *BaseName(const wchar_t *path)
{
	const wchar_t *a = wcsrchr(path, L'\\');
	const wchar_t *b = wcsrchr(path, L'/');
	const wchar_t *p = (a > b) ? a : b;
	return (p != NULL) ? p + 1 : path;
}

static int HasExt(const wchar_t *path, const wchar_t *ext)
{
	size_t n = wcslen(path);
	size_t e = wcslen(ext);
	return (n >= e) && (_wcsicmp(path + n - e, ext) == 0);
}

static int IsImage(const wchar_t *path)
{
	return HasExt(path, L".png") || HasExt(path, L".jpg") || HasExt(path, L".jpeg") || HasExt(path, L".bmp") ||
		HasExt(path, L".gif") || HasExt(path, L".tif") || HasExt(path, L".tiff");
}

static void ForwardSlashes(wchar_t *dst, size_t cap, const wchar_t *src)
{
	Copy(dst, cap, src);
	for (wchar_t *p = dst; *p != 0; p++)
	{
		if (*p == L'\\')
		{
			*p = L'/';
		}
	}
}

static wchar_t *DupW(const wchar_t *s)
{
	size_t n = wcslen(s) + 1;
	wchar_t *d = (wchar_t *)malloc(n * sizeof(wchar_t));
	if (d != NULL)
	{
		memcpy(d, s, n * sizeof(wchar_t));
	}
	return d;
}

static unsigned long long FileSize64(const wchar_t *path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;

	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad))
	{
		return 0;
	}
	return ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

// Posts a status line from any thread.
static void PostStatus(const wchar_t *fmt, ...)
{
	wchar_t buf[512];
	va_list ap;

	va_start(ap, fmt);
	_vsnwprintf(buf, 511, fmt, ap);
	va_end(ap);
	buf[511] = 0;
	PostMessageW(g_wnd, WM_APP_STATUS, 0, (LPARAM)DupW(buf));
}

static void PostDetail(const wchar_t *fmt, ...)
{
	wchar_t buf[512];
	va_list ap;

	va_start(ap, fmt);
	_vsnwprintf(buf, 511, fmt, ap);
	va_end(ap);
	buf[511] = 0;
	PostMessageW(g_wnd, WM_APP_STATUS, 1, (LPARAM)DupW(buf));
}

static void PostPhase(int phase)
{
	PostMessageW(g_wnd, WM_APP_PHASE, (WPARAM)phase, 0);
}

// ---------------------------------------------------------------------------
// GDI+ (loaded at run time so the launcher has no import on it)

typedef struct
{
	UINT32 GdiplusVersion;
	void *DebugEventCallback;
	BOOL SuppressBackgroundThread;
	BOOL SuppressExternalCodecs;
} GdipStartupInput;

typedef int(WINAPI *PFN_GdiplusStartup)(ULONG_PTR *, const GdipStartupInput *, void *);
typedef int(WINAPI *PFN_GdipCreateBitmapFromFile)(const WCHAR *, void **);
typedef int(WINAPI *PFN_GdipCreateHBITMAPFromBitmap)(void *, HBITMAP *, DWORD);
typedef int(WINAPI *PFN_GdipGetImageWidth)(void *, UINT *);
typedef int(WINAPI *PFN_GdipGetImageHeight)(void *, UINT *);
typedef int(WINAPI *PFN_GdipDisposeImage)(void *);

static struct
{
	int ready;
	ULONG_PTR token;
	PFN_GdipCreateBitmapFromFile createFromFile;
	PFN_GdipCreateHBITMAPFromBitmap toHbitmap;
	PFN_GdipGetImageWidth width;
	PFN_GdipGetImageHeight height;
	PFN_GdipDisposeImage dispose;
} s_gdip;

static void Gdip_Init(void)
{
	HMODULE lib = LoadLibraryW(L"gdiplus.dll");
	PFN_GdiplusStartup startup;
	GdipStartupInput input;

	if (lib == NULL)
	{
		return;
	}
	startup = (PFN_GdiplusStartup)(void *)GetProcAddress(lib, "GdiplusStartup");
	s_gdip.createFromFile = (PFN_GdipCreateBitmapFromFile)(void *)GetProcAddress(lib, "GdipCreateBitmapFromFile");
	s_gdip.toHbitmap = (PFN_GdipCreateHBITMAPFromBitmap)(void *)GetProcAddress(lib, "GdipCreateHBITMAPFromBitmap");
	s_gdip.width = (PFN_GdipGetImageWidth)(void *)GetProcAddress(lib, "GdipGetImageWidth");
	s_gdip.height = (PFN_GdipGetImageHeight)(void *)GetProcAddress(lib, "GdipGetImageHeight");
	s_gdip.dispose = (PFN_GdipDisposeImage)(void *)GetProcAddress(lib, "GdipDisposeImage");
	if ((startup == NULL) || (s_gdip.createFromFile == NULL) || (s_gdip.toHbitmap == NULL) || (s_gdip.width == NULL) ||
		(s_gdip.height == NULL) || (s_gdip.dispose == NULL))
	{
		return;
	}
	memset(&input, 0, sizeof(input));
	input.GdiplusVersion = 1;
	s_gdip.ready = startup(&s_gdip.token, &input, NULL) == 0;
}

static HBITMAP LoadPicture(const wchar_t *path, int *w, int *h)
{
	HBITMAP bmp = NULL;

	*w = 0;
	*h = 0;
	if (s_gdip.ready)
	{
		void *image = NULL;
		UINT iw = 0, ih = 0;

		if ((s_gdip.createFromFile(path, &image) == 0) && (image != NULL))
		{
			s_gdip.width(image, &iw);
			s_gdip.height(image, &ih);
			if ((s_gdip.toHbitmap(image, &bmp, 0xFFF6F1E6u) != 0))
			{
				bmp = NULL;
			}
			s_gdip.dispose(image);
			*w = (int)iw;
			*h = (int)ih;
		}
	}
	if ((bmp == NULL) && HasExt(path, L".bmp"))
	{
		BITMAP info;

		bmp = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		if ((bmp != NULL) && GetObjectW(bmp, sizeof(info), &info))
		{
			*w = info.bmWidth;
			*h = info.bmHeight;
		}
	}
	if ((bmp != NULL) && ((*w <= 0) || (*h <= 0)))
	{
		DeleteObject(bmp);
		bmp = NULL;
	}
	return bmp;
}

// ---------------------------------------------------------------------------
// Paths, mode, settings files

static int LooksLikeSource(const wchar_t *dir)
{
	wchar_t path[PATH_CAP];
	char buf[512];
	FILE *f;
	size_t n;

	Join(path, PATH_CAP, dir, L"CMakeLists.txt");
	f = _wfopen(path, L"rb");
	if (f == NULL)
	{
		return 0;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	if (strstr(buf, "CTR-Native") == NULL)
	{
		return 0;
	}
	Join(path, PATH_CAP, dir, L"main.c");
	return FileExists(path);
}

static void FindPaths(void)
{
	wchar_t probe[PATH_CAP];

	GetModuleFileNameW(NULL, g_exeDir, PATH_CAP);
	ParentDir(g_exeDir);

	Join(probe, PATH_CAP, g_exeDir, L"source");
	if (LooksLikeSource(probe))
	{
		g_mode = MODE_PACKAGE;
		Copy(g_srcRoot, PATH_CAP, probe);
		Copy(g_dataRoot, PATH_CAP, g_exeDir);
		Join(g_buildDir, PATH_CAP, g_exeDir, L"build");
	}
	else
	{
		// A checkout: the launcher was built into build-*\ (or deeper).
		Copy(probe, PATH_CAP, g_exeDir);
		g_mode = MODE_PREBUILT;
		for (int up = 0; up < 4; up++)
		{
			if (LooksLikeSource(probe))
			{
				g_mode = MODE_DEV;
				Copy(g_srcRoot, PATH_CAP, probe);
				Copy(g_dataRoot, PATH_CAP, probe);
				Join(g_buildDir, PATH_CAP, probe, L"build-launcher");
				break;
			}
			if (wcschr(probe, L'\\') == NULL)
			{
				break;
			}
			ParentDir(probe);
		}
		if (g_mode == MODE_PREBUILT)
		{
			Copy(g_dataRoot, PATH_CAP, g_exeDir);
			Copy(g_buildDir, PATH_CAP, g_exeDir);
		}
	}

	Join(g_gameIni, PATH_CAP, g_dataRoot, L"ctr_native.ini");
	Join(g_launcherIni, PATH_CAP, g_dataRoot, L"launcher.ini");
	Join(g_logDir, PATH_CAP, g_dataRoot, L"logs");
	Join(g_buildLog, PATH_CAP, g_logDir, L"build.log");
	Join(g_gameLog, PATH_CAP, g_logDir, L"game.log");
}

static void IniGetString(const wchar_t *file, const wchar_t *sec, const wchar_t *key, wchar_t *dst, DWORD cap)
{
	GetPrivateProfileStringW(sec, key, L"", dst, cap, file);
}

static void IniSetInt(const wchar_t *file, const wchar_t *sec, const wchar_t *key, int v)
{
	wchar_t buf[32];
	swprintf(buf, 32, L"%d", v);
	WritePrivateProfileStringW(sec, key, buf, file);
}

// launcher.ini holds paths, so it is UTF-16 (the profile API keeps a file's encoding).
static void EnsureUnicodeIni(const wchar_t *path)
{
	if (!FileExists(path))
	{
		HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE)
		{
			static const unsigned char bom[2] = {0xFF, 0xFE};
			DWORD written;
			WriteFile(h, bom, 2, &written, NULL);
			CloseHandle(h);
		}
	}
}

static void DefaultResolution(int *w, int *h)
{
	RECT work;
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	*w = s_resolutions[0].w;
	*h = s_resolutions[0].h;
	for (int i = RES_COUNT - 1; i >= 0; i--)
	{
		// Windowed by default: leave room for the title bar.
		if ((s_resolutions[i].w <= work.right - work.left) && (s_resolutions[i].h + 40 <= work.bottom - work.top) &&
			(wcscmp(s_resolutions[i].aspect, L"21:9") != 0) && (wcscmp(s_resolutions[i].aspect, L"4:3") != 0))
		{
			*w = s_resolutions[i].w;
			*h = s_resolutions[i].h;
			return;
		}
	}
}

static int IniChoice(const wchar_t *sec, const wchar_t *key, const wchar_t *const *words, int count, int def)
{
	wchar_t value[64];

	IniGetString(g_gameIni, sec, key, value, 64);
	for (int i = 0; i < count; i++)
	{
		if (_wcsicmp(value, words[i]) == 0)
		{
			return i;
		}
	}
	return def;
}

static void LoadSettings(void)
{
	int dw, dh;

	DefaultResolution(&dw, &dh);
	g_set.width = (int)GetPrivateProfileIntW(L"video", L"width", dw, g_gameIni);
	g_set.height = (int)GetPrivateProfileIntW(L"video", L"height", dh, g_gameIni);
	g_set.fullscreen = GetPrivateProfileIntW(L"video", L"fullscreen", 0, g_gameIni) != 0;
	g_set.widescreen = GetPrivateProfileIntW(L"video", L"widescreen", 1, g_gameIni) != 0;
	g_set.keyboard = GetPrivateProfileIntW(L"input", L"keyboard", 1, g_gameIni) != 0;
	g_set.controller = GetPrivateProfileIntW(L"input", L"controller", 1, g_gameIni) != 0;
	g_set.xbox = GetPrivateProfileIntW(L"input", L"xbox", 1, g_gameIni) != 0;
	g_set.playstation = GetPrivateProfileIntW(L"input", L"playstation", 1, g_gameIni) != 0;
	g_set.vibration = GetPrivateProfileIntW(L"input", L"vibration", 1, g_gameIni) != 0;
	g_set.vsync = GetPrivateProfileIntW(L"video", L"vsync", 0, g_gameIni) != 0;
	g_set.intscale = GetPrivateProfileIntW(L"graphics", L"integer_scale", 0, g_gameIni) != 0;
	g_set.texfilter = GetPrivateProfileIntW(L"graphics", L"texture_filter", 0, g_gameIni) != 0;
	g_set.upscale = IniChoice(L"graphics", L"upscale", s_upscaleWords, 3, 0);
	g_set.sharpness = IniChoice(L"graphics", L"sharpness", s_sharpWords, 3, 1);
	g_set.aa = IniChoice(L"graphics", L"antialiasing", s_aaWords, 2, 0);
	g_set.crt = IniChoice(L"graphics", L"crt", s_crtWords, 3, 0);
	g_set.brightness = (int)GetPrivateProfileIntW(L"graphics", L"brightness", 0, g_gameIni);
	if ((g_set.brightness < -2) || (g_set.brightness > 2))
	{
		g_set.brightness = 0;
	}
	if ((g_set.width < 320) || (g_set.height < 240))
	{
		g_set.width = dw;
		g_set.height = dh;
	}

	IniGetString(g_launcherIni, L"launcher", L"disc", g_discSource, PATH_CAP);
	IniGetString(g_launcherIni, L"launcher", L"cover", g_coverPath, PATH_CAP);
}

static void SaveSettings(void)
{
	// ctr_native.ini: read by the game (ASCII keys and numbers only).
	IniSetInt(g_gameIni, L"video", L"width", g_set.width);
	IniSetInt(g_gameIni, L"video", L"height", g_set.height);
	IniSetInt(g_gameIni, L"video", L"fullscreen", g_set.fullscreen);
	IniSetInt(g_gameIni, L"video", L"widescreen", g_set.widescreen);
	IniSetInt(g_gameIni, L"input", L"keyboard", g_set.keyboard);
	IniSetInt(g_gameIni, L"input", L"controller", g_set.controller);
	IniSetInt(g_gameIni, L"input", L"xbox", g_set.xbox);
	IniSetInt(g_gameIni, L"input", L"playstation", g_set.playstation);
	IniSetInt(g_gameIni, L"input", L"vibration", g_set.vibration);
	IniSetInt(g_gameIni, L"video", L"vsync", g_set.vsync);
	WritePrivateProfileStringW(L"graphics", L"upscale", s_upscaleWords[g_set.upscale], g_gameIni);
	WritePrivateProfileStringW(L"graphics", L"sharpness", s_sharpWords[g_set.sharpness], g_gameIni);
	WritePrivateProfileStringW(L"graphics", L"antialiasing", s_aaWords[g_set.aa], g_gameIni);
	WritePrivateProfileStringW(L"graphics", L"crt", s_crtWords[g_set.crt], g_gameIni);
	IniSetInt(g_gameIni, L"graphics", L"brightness", g_set.brightness);
	IniSetInt(g_gameIni, L"graphics", L"texture_filter", g_set.texfilter);
	IniSetInt(g_gameIni, L"graphics", L"integer_scale", g_set.intscale);
	WritePrivateProfileStringW(NULL, NULL, NULL, g_gameIni);
}

static void SaveLauncherPaths(void)
{
	EnsureUnicodeIni(g_launcherIni);
	WritePrivateProfileStringW(L"launcher", L"disc", g_discSource, g_launcherIni);
	WritePrivateProfileStringW(L"launcher", L"cover", g_coverPath, g_launcherIni);
	WritePrivateProfileStringW(NULL, NULL, NULL, g_launcherIni);
}

// ---------------------------------------------------------------------------
// Disc detection

#define SECTOR_RAW 2352

static const unsigned char s_sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

// Reads the boot executable's name (SYSTEM.CNF's "BOOT = cdrom:\SCUS_944.26;1")
// from the first sectors of a raw MODE2/2352 image.
static int ReadDiscId(const wchar_t *path, char *id, size_t cap, int *isRaw)
{
	FILE *f = _wfopen(path, L"rb");
	unsigned char *buf;
	size_t n;
	const size_t want = (size_t)SECTOR_RAW * 400;

	*isRaw = 0;
	id[0] = 0;
	if (f == NULL)
	{
		return 0;
	}
	buf = (unsigned char *)malloc(want);
	if (buf == NULL)
	{
		fclose(f);
		return 0;
	}
	n = fread(buf, 1, want, f);
	fclose(f);

	if ((n > (size_t)SECTOR_RAW * 17) && (memcmp(buf + SECTOR_RAW * 16, s_sync, 12) == 0) &&
		(memcmp(buf + SECTOR_RAW * 16 + 24 + 1, "CD001", 5) == 0))
	{
		*isRaw = 1;
	}

	for (size_t i = 0; i + 20 < n; i++)
	{
		if (((buf[i] | 0x20) == 'c') && ((buf[i + 1] | 0x20) == 'd') && ((buf[i + 2] | 0x20) == 'r') &&
			((buf[i + 3] | 0x20) == 'o') && ((buf[i + 4] | 0x20) == 'm') && (buf[i + 5] == ':'))
		{
			size_t j = i + 6;
			size_t k = 0;

			while ((j < n) && ((buf[j] == '\\') || (buf[j] == '/')))
			{
				j++;
			}
			while ((j < n) && (k + 1 < cap) && (buf[j] > ' ') && (buf[j] != ';'))
			{
				id[k++] = (char)buf[j++];
			}
			id[k] = 0;
			if (k >= 8)
			{
				break;
			}
			id[0] = 0;
		}
	}
	free(buf);
	return id[0] != 0;
}

static void AnalyzeBin(const wchar_t *bin, struct DiscInfo *info)
{
	char id[64];
	int isRaw = 0;
	unsigned long long size = FileSize64(bin);
	wchar_t wid[64];

	memset(info, 0, sizeof(*info));
	Copy(info->bin, PATH_CAP, bin);

	if (size < 16ull * 1024 * 1024)
	{
		info->state = 3;
		Copy(info->title, 128, L"That file is too small to be a disc image");
		Copy(info->detail, 256, bin);
		return;
	}

	ReadDiscId(bin, id, sizeof(id), &isRaw);
	MultiByteToWideChar(CP_ACP, 0, id, -1, wid, 64);

	if (!isRaw || ((size % SECTOR_RAW) != 0))
	{
		info->state = 3;
		Copy(info->title, 128, L"Not a raw (2352-byte sector) disc image");
		Copy(info->detail, 256,
			L"Rip the disc as BIN/CUE (MODE2/2352). A cooked .iso loses the audio and video sectors.");
		return;
	}

	if (_strnicmp(id, "SCUS_944.26", 11) == 0)
	{
		info->state = 1;
		Copy(info->title, 128, L"Crash Team Racing (NTSC-U) found");
		swprintf(info->detail, 256, L"SCUS-94426 \x00B7 %llu MB \x00B7 %ls", size / (1024 * 1024), BaseName(bin));
	}
	else if ((_strnicmp(id, "SCES_021.05", 11) == 0) || (_strnicmp(id, "SCPS_101.18", 11) == 0))
	{
		info->state = 3;
		swprintf(info->title, 128, L"This is a %ls copy of CTR", (id[2] == 'E') ? L"PAL" : L"Japanese");
		Copy(info->detail, 256, L"CTR Native plays the NTSC-U disc (SCUS-94426). Use that release.");
	}
	else if (id[0] != 0)
	{
		info->state = 3;
		Copy(info->title, 128, L"This disc is not Crash Team Racing");
		swprintf(info->detail, 256, L"It boots %ls. CTR Native needs SCUS-94426 (NTSC-U).", wid);
	}
	else
	{
		info->state = 2;
		Copy(info->title, 128, L"Disc image found (region not read)");
		swprintf(info->detail, 256, L"%llu MB \x00B7 %ls. It should be the NTSC-U release.", size / (1024 * 1024),
			BaseName(bin));
	}
}

// .cue -> the first FILE "x.bin" it names, next to it.
static int BinFromCue(const wchar_t *cue, wchar_t *bin, size_t cap)
{
	FILE *f = _wfopen(cue, L"rb");
	char line[1024];

	if (f == NULL)
	{
		return 0;
	}
	while (fgets(line, sizeof(line), f) != NULL)
	{
		char *p = line;
		while ((*p == ' ') || (*p == '\t'))
		{
			p++;
		}
		if (_strnicmp(p, "FILE", 4) == 0)
		{
			char *q1 = strchr(p, '"');
			char *q2 = (q1 != NULL) ? strchr(q1 + 1, '"') : NULL;
			wchar_t name[512];
			wchar_t dir[PATH_CAP];

			if ((q1 == NULL) || (q2 == NULL))
			{
				break;
			}
			*q2 = 0;
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, q1 + 1, -1, name, 512) == 0)
			{
				MultiByteToWideChar(CP_ACP, 0, q1 + 1, -1, name, 512);
			}
			Copy(dir, PATH_CAP, cue);
			ParentDir(dir);
			Join(bin, cap, dir, name);
			fclose(f);
			return FileExists(bin);
		}
	}
	fclose(f);
	return 0;
}

struct ScanBest
{
	wchar_t bin[PATH_CAP];
	unsigned long long size;
	int named;
	wchar_t image[PATH_CAP];
	int imageScore;
	int seen;
};

static void ScanFolder(const wchar_t *dir, int depth, struct ScanBest *best)
{
	wchar_t pattern[PATH_CAP];
	WIN32_FIND_DATAW fd;
	HANDLE h;

	Join(pattern, PATH_CAP, dir, L"*");
	h = FindFirstFileW(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
	{
		return;
	}
	do
	{
		wchar_t full[PATH_CAP];

		if ((wcscmp(fd.cFileName, L".") == 0) || (wcscmp(fd.cFileName, L"..") == 0))
		{
			continue;
		}
		if (++best->seen > 4000)
		{
			break;
		}
		Join(full, PATH_CAP, dir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (depth > 0)
			{
				ScanFolder(full, depth - 1, best);
			}
			continue;
		}
		if (HasExt(full, L".bin") || HasExt(full, L".img"))
		{
			unsigned long long size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
			int named = (wcsstr(fd.cFileName, L"ctr") != NULL) || (wcsstr(fd.cFileName, L"CTR") != NULL) ||
				(_wcsnicmp(fd.cFileName, L"crash", 5) == 0);

			if ((size % SECTOR_RAW) == 0 && size >= 16ull * 1024 * 1024 &&
				((named > best->named) || ((named == best->named) && (size > best->size))))
			{
				Copy(best->bin, PATH_CAP, full);
				best->size = size;
				best->named = named;
			}
		}
		else if (IsImage(full))
		{
			int score = 1;
			if ((wcsstr(fd.cFileName, L"front") != NULL) || (wcsstr(fd.cFileName, L"Front") != NULL) ||
				(wcsstr(fd.cFileName, L"cover") != NULL) || (wcsstr(fd.cFileName, L"Cover") != NULL))
			{
				score = 2;
			}
			if (score > best->imageScore)
			{
				Copy(best->image, PATH_CAP, full);
				best->imageScore = score;
			}
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static void SetCover(const wchar_t *path, int save);

static void ChooseDisc(const wchar_t *path, int save)
{
	wchar_t bin[PATH_CAP];

	memset(&g_disc, 0, sizeof(g_disc));
	if ((path == NULL) || (path[0] == 0))
	{
		return;
	}

	bin[0] = 0;
	if (DirExists(path))
	{
		struct ScanBest best;
		memset(&best, 0, sizeof(best));
		ScanFolder(path, 2, &best);
		Copy(bin, PATH_CAP, best.bin);
		if (save && (g_coverPath[0] == 0) && (best.image[0] != 0))
		{
			SetCover(best.image, 1);
		}
	}
	else if (HasExt(path, L".cue"))
	{
		if (!BinFromCue(path, bin, PATH_CAP))
		{
			bin[0] = 0;
		}
	}
	else if (FileExists(path))
	{
		Copy(bin, PATH_CAP, path);
	}

	if (bin[0] == 0)
	{
		g_disc.state = 3;
		Copy(g_disc.title, 128, L"No disc image there");
		Copy(g_disc.detail, 256, L"Drop the folder holding your CTR .bin/.cue, or the .bin itself.");
	}
	else
	{
		AnalyzeBin(bin, &g_disc);
	}

	if (save)
	{
		Copy(g_discSource, PATH_CAP, path);
		SaveLauncherPaths();
	}
}

// The disc already placed in assets\ (a dev checkout, or an earlier run).
static void DetectPlacedDisc(void)
{
	wchar_t placed[PATH_CAP];
	wchar_t assets[PATH_CAP];

	Join(assets, PATH_CAP, g_dataRoot, L"assets");
	Join(placed, PATH_CAP, assets, L"ctr-u.bin");
	if ((g_discSource[0] != 0) && (FileExists(g_discSource) || DirExists(g_discSource)))
	{
		ChooseDisc(g_discSource, 0);
	}
	else if (FileExists(placed))
	{
		ChooseDisc(placed, 0);
	}
}

// ---------------------------------------------------------------------------
// Cover

static void SetCover(const wchar_t *path, int save)
{
	HBITMAP bmp = NULL;
	int w = 0, h = 0;

	if ((path != NULL) && (path[0] != 0))
	{
		bmp = LoadPicture(path, &w, &h);
		if (bmp == NULL)
		{
			if (save)
			{
				MessageBoxW(g_wnd, L"That picture could not be opened. Use a PNG, JPEG or BMP file.", APP_TITLE,
					MB_ICONWARNING);
			}
			return;
		}
	}
	if (g_coverBmp != NULL)
	{
		DeleteObject(g_coverBmp);
	}
	if (g_coverScaled != NULL)
	{
		DeleteObject(g_coverScaled);
		g_coverScaled = NULL;
	}
	g_coverBmp = bmp;
	g_coverW = w;
	g_coverH = h;
	if (save)
	{
		Copy(g_coverPath, PATH_CAP, (path != NULL) ? path : L"");
		SaveLauncherPaths();
	}
	if (g_wnd != NULL)
	{
		InvalidateRect(g_wnd, NULL, FALSE);
	}
}

static void LoadInitialCover(void)
{
	static const wchar_t *names[] = {L"cover.png", L"cover.jpg", L"cover.jpeg", L"cover.bmp"};
	wchar_t path[PATH_CAP];
	wchar_t assets[PATH_CAP];

	if ((g_coverPath[0] != 0) && FileExists(g_coverPath))
	{
		SetCover(g_coverPath, 0);
		return;
	}
	Join(assets, PATH_CAP, g_dataRoot, L"assets");
	for (int i = 0; i < 4; i++)
	{
		Join(path, PATH_CAP, g_dataRoot, names[i]);
		if (!FileExists(path))
		{
			Join(path, PATH_CAP, assets, names[i]);
		}
		if (FileExists(path))
		{
			SetCover(path, 0);
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Child processes

typedef void (*LineFn)(const char *line);

static HANDLE s_logFile = INVALID_HANDLE_VALUE;

static void LogBytes(const char *s, size_t n)
{
	if (s_logFile != INVALID_HANDLE_VALUE)
	{
		DWORD written;
		WriteFile(s_logFile, s, (DWORD)n, &written, NULL);
	}
}

static void LogLine(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
	va_end(ap);
	if (n < 0)
	{
		return;
	}
	if (n > (int)sizeof(buf) - 3)
	{
		n = (int)sizeof(buf) - 3;
	}
	buf[n++] = '\r';
	buf[n++] = '\n';
	LogBytes(buf, (size_t)n);
}

static void LogLineW(const wchar_t *text)
{
	char buf[4096];
	WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, sizeof(buf), NULL, NULL);
	LogLine("%s", buf);
}

// Runs a command line, sending each output line to onLine and the log.
// The process tree lives in a job object, so Cancel (or closing the launcher)
// stops the compiler too. Returns the exit code, or -1 if it could not start.
static int RunCommand(wchar_t *cmdline, const wchar_t *cwd, LineFn onLine)
{
	SECURITY_ATTRIBUTES sa;
	HANDLE readPipe = NULL, writePipe = NULL;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	char buf[4096];
	char line[4096];
	size_t lineLen = 0;
	DWORD exitCode = (DWORD)-1;

	LogLineW(L"");
	LogLineW(cmdline);

	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
	{
		return -1;
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	si.hStdError = writePipe;
	memset(&pi, 0, sizeof(pi));

	if (!CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, cwd, &si, &pi))
	{
		LogLine("could not start it (error %lu)", (unsigned long)GetLastError());
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		return -1;
	}
	if (g_job != NULL)
	{
		AssignProcessToJobObject(g_job, pi.hProcess);
	}
	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	CloseHandle(writePipe);

	for (;;)
	{
		DWORD got = 0;
		if (!ReadFile(readPipe, buf, sizeof(buf), &got, NULL) || (got == 0))
		{
			break;
		}
		LogBytes(buf, got);
		for (DWORD i = 0; i < got; i++)
		{
			char c = buf[i];
			if ((c == '\n') || (c == '\r'))
			{
				if (lineLen > 0)
				{
					line[lineLen] = 0;
					onLine(line);
					lineLen = 0;
				}
			}
			else if (lineLen + 1 < sizeof(line))
			{
				line[lineLen++] = c;
			}
		}
	}
	if (lineLen > 0)
	{
		line[lineLen] = 0;
		onLine(line);
	}
	CloseHandle(readPipe);
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	LogLine("exit code %ld", (long)exitCode);
	return (int)exitCode;
}

// ---------------------------------------------------------------------------
// Tools: the bundled compiler first, then (for developers) PATH

static int FindOnPath(const wchar_t *name, wchar_t *dst)
{
	wchar_t *part;
	return SearchPathW(NULL, name, NULL, PATH_CAP, dst, &part) != 0;
}

static void AddBinDir(struct Tools *t, const wchar_t *exe)
{
	wchar_t dir[PATH_CAP];
	size_t n;

	Copy(dir, PATH_CAP, exe);
	ParentDir(dir);
	if (wcsstr(t->binDirs, dir) != NULL)
	{
		return;
	}
	n = wcslen(t->binDirs);
	if (n > 0)
	{
		wcsncat(t->binDirs, L";", 4 * PATH_CAP - n - 1);
	}
	wcsncat(t->binDirs, dir, 4 * PATH_CAP - wcslen(t->binDirs) - 1);
}

static int FindTools(struct Tools *t)
{
	wchar_t tools[PATH_CAP];
	wchar_t llvm[PATH_CAP];
	wchar_t p[PATH_CAP];

	memset(t, 0, sizeof(*t));
	IniGetString(g_launcherIni, L"build", L"command", t->command, PATH_CAP);
	if (t->command[0] != 0)
	{
		return 1;
	}

	Join(tools, PATH_CAP, g_exeDir, L"tools");
	Join(p, PATH_CAP, tools, L"cmake\\bin\\cmake.exe");
	if (FileExists(p))
	{
		Copy(t->cmake, PATH_CAP, p);
	}
	Join(p, PATH_CAP, tools, L"ninja\\ninja.exe");
	if (FileExists(p))
	{
		Copy(t->ninja, PATH_CAP, p);
	}
	Join(llvm, PATH_CAP, tools, L"llvm-mingw\\bin");
	Join(p, PATH_CAP, llvm, L"i686-w64-mingw32-clang.exe");
	if (FileExists(p))
	{
		Copy(t->cc, PATH_CAP, p);
		Join(t->cxx, PATH_CAP, llvm, L"i686-w64-mingw32-clang++.exe");
		Join(t->rc, PATH_CAP, llvm, L"i686-w64-mingw32-windres.exe");
	}
	t->bundled = (t->cmake[0] != 0) && (t->ninja[0] != 0) && (t->cc[0] != 0);

	// Overrides, then PATH (a developer's own MinGW setup).
	IniGetString(g_launcherIni, L"build", L"cmake", p, PATH_CAP);
	if (p[0] != 0)
	{
		Copy(t->cmake, PATH_CAP, p);
	}
	IniGetString(g_launcherIni, L"build", L"ninja", p, PATH_CAP);
	if (p[0] != 0)
	{
		Copy(t->ninja, PATH_CAP, p);
	}
	IniGetString(g_launcherIni, L"build", L"cc", p, PATH_CAP);
	if (p[0] != 0)
	{
		Copy(t->cc, PATH_CAP, p);
		t->cxx[0] = 0;
		t->rc[0] = 0;
	}
	if (t->cmake[0] == 0)
	{
		FindOnPath(L"cmake.exe", t->cmake);
	}
	if (t->ninja[0] == 0)
	{
		FindOnPath(L"ninja.exe", t->ninja);
	}
	if (t->cc[0] == 0)
	{
		if (FindOnPath(L"i686-w64-mingw32-clang.exe", t->cc))
		{
			FindOnPath(L"i686-w64-mingw32-clang++.exe", t->cxx);
			FindOnPath(L"i686-w64-mingw32-windres.exe", t->rc);
		}
		else if (FindOnPath(L"i686-w64-mingw32-gcc.exe", t->cc))
		{
			FindOnPath(L"i686-w64-mingw32-g++.exe", t->cxx);
			FindOnPath(L"windres.exe", t->rc);
		}
	}

	if ((t->cmake[0] == 0) || (t->ninja[0] == 0) || (t->cc[0] == 0))
	{
		return 0;
	}
	AddBinDir(t, t->cc);
	AddBinDir(t, t->ninja);
	AddBinDir(t, t->cmake);
	return 1;
}

static int FindGameExe(wchar_t *dst)
{
	static const wchar_t *candidates[] = {
		L"ctr_native.exe",
		L"build-clangcl-x86\\ctr_native.exe",
		L"build\\ctr_native.exe",
		L"build-msvc-x86\\Release\\ctr_native.exe",
	};

	Join(dst, PATH_CAP, g_buildDir, L"ctr_native.exe");
	if (FileExists(dst))
	{
		return 1;
	}
	Join(dst, PATH_CAP, g_exeDir, L"ctr_native.exe");
	if (FileExists(dst))
	{
		return 1;
	}
	if (g_srcRoot[0] != 0)
	{
		for (int i = 0; i < 4; i++)
		{
			Join(dst, PATH_CAP, g_srcRoot, candidates[i]);
			if (FileExists(dst))
			{
				return 1;
			}
		}
	}
	dst[0] = 0;
	return 0;
}

// ---------------------------------------------------------------------------
// Worker: place the disc, build, play

static char s_firstError[512];
static int s_configureLines;

static void OnConfigureLine(const char *line)
{
	s_configureLines++;
	PostMessageW(g_wnd, WM_APP_STEP, (WPARAM)(s_configureLines < 60 ? s_configureLines : 59), 60);
	if ((s_firstError[0] == 0) && (strstr(line, "CMake Error") != NULL))
	{
		snprintf(s_firstError, sizeof(s_firstError), "%s", line);
	}
}

static void OnBuildLine(const char *line)
{
	int done = 0, total = 0;

	if ((line[0] == '[') && (sscanf(line, "[%d/%d]", &done, &total) == 2) && (total > 0))
	{
		const char *desc = strchr(line, ']');
		const char *last = NULL;
		wchar_t name[260];
		char tmp[260];

		PostMessageW(g_wnd, WM_APP_STEP, (WPARAM)done, (LPARAM)total);

		// "[12/486] Building C object .../src/video/SDL_video.c.obj" -> SDL_video.c
		if (desc != NULL)
		{
			for (const char *p = desc; *p != 0; p++)
			{
				if ((*p == '/') || (*p == '\\') || (*p == ' '))
				{
					last = p + 1;
				}
			}
		}
		snprintf(tmp, sizeof(tmp), "%s", (last != NULL) ? last : "");
		{
			size_t n = strlen(tmp);
			if ((n > 4) && (strcmp(tmp + n - 4, ".obj") == 0))
			{
				tmp[n - 4] = 0;
			}
			else if ((n > 2) && (strcmp(tmp + n - 2, ".o") == 0))
			{
				tmp[n - 2] = 0;
			}
		}
		MultiByteToWideChar(CP_UTF8, 0, tmp, -1, name, 260);
		if (strstr(line, "Linking") != NULL)
		{
			PostDetail(L"Step %d of %d \x00B7 linking %ls", done, total, name);
		}
		else
		{
			PostDetail(L"Step %d of %d \x00B7 %ls", done, total, name);
		}
	}
	else if (strstr(line, "no work to do") != NULL)
	{
		PostMessageW(g_wnd, WM_APP_STEP, 1, 1);
		PostDetail(L"Already up to date");
	}
	else if ((s_firstError[0] == 0) && ((strstr(line, "error:") != NULL) || (strncmp(line, "FAILED:", 7) == 0)))
	{
		snprintf(s_firstError, sizeof(s_firstError), "%s", line);
	}
}

static int OpenLog(const wchar_t *path)
{
	CreateDirectoryW(g_logDir, NULL);
	s_logFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	return s_logFile != INVALID_HANDLE_VALUE;
}

static void CloseLog(void)
{
	if (s_logFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(s_logFile);
		s_logFile = INVALID_HANDLE_VALUE;
	}
}

static int SameFile(const wchar_t *a, const wchar_t *b)
{
	BY_HANDLE_FILE_INFORMATION ia, ib;
	HANDLE ha, hb;
	int same = 0;

	ha = CreateFileW(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	hb = CreateFileW(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if ((ha != INVALID_HANDLE_VALUE) && (hb != INVALID_HANDLE_VALUE) && GetFileInformationByHandle(ha, &ia) &&
		GetFileInformationByHandle(hb, &ib))
	{
		same = (ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber) && (ia.nFileIndexHigh == ib.nFileIndexHigh) &&
			(ia.nFileIndexLow == ib.nFileIndexLow);
	}
	if (ha != INVALID_HANDLE_VALUE)
	{
		CloseHandle(ha);
	}
	if (hb != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hb);
	}
	return same;
}

static DWORD CALLBACK CopyProgress(LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER streamTotal,
	LARGE_INTEGER streamDone, DWORD stream, DWORD reason, HANDLE src, HANDLE dst, LPVOID data)
{
	(void)streamTotal;
	(void)streamDone;
	(void)stream;
	(void)reason;
	(void)src;
	(void)dst;
	(void)data;
	if (total.QuadPart > 0)
	{
		PostMessageW(g_wnd, WM_APP_STEP, (WPARAM)(done.QuadPart / (1024 * 1024)),
			(LPARAM)(total.QuadPart / (1024 * 1024) + 1));
	}
	return g_cancel ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}

// Puts the player's disc where the game reads it: assets\ctr-u.bin. A hard link
// when it is on the same drive (instant, no extra space), else a copy. The
// player's own file is never changed or moved.
static int PlaceDisc(void)
{
	wchar_t assets[PATH_CAP];
	wchar_t dest[PATH_CAP];

	if ((g_disc.state != 1) && (g_disc.state != 2))
	{
		PostStatus(L"Add your Crash Team Racing disc first");
		PostDetail(L"Drop the folder with your .bin/.cue on the right page, or press Browse.");
		return 0;
	}

	Join(assets, PATH_CAP, g_dataRoot, L"assets");
	Join(dest, PATH_CAP, assets, L"ctr-u.bin");
	CreateDirectoryW(assets, NULL);

	if (FileExists(dest))
	{
		if (SameFile(dest, g_disc.bin))
		{
			return 1;
		}
		if (FileSize64(dest) == FileSize64(g_disc.bin))
		{
			// Most likely the same disc copied earlier.
			return 1;
		}
		// Someone else's file: keep it, under another name.
		{
			wchar_t keep[PATH_CAP];
			for (int i = 1; i < 100; i++)
			{
				swprintf(keep, PATH_CAP, L"%ls.old%d", dest, i);
				if (!FileExists(keep))
				{
					MoveFileW(dest, keep);
					LogLineW(L"kept the earlier assets\\ctr-u.bin as");
					LogLineW(keep);
					break;
				}
			}
		}
	}

	if (CreateHardLinkW(dest, g_disc.bin, NULL))
	{
		LogLineW(L"linked the disc into assets\\ctr-u.bin");
		return 1;
	}

	PostPhase(PHASE_COPY);
	PostStatus(L"Copying your disc into the game folder");
	PostDetail(L"%ls", BaseName(g_disc.bin));
	if (!CopyFileExW(g_disc.bin, dest, CopyProgress, NULL, NULL, 0))
	{
		DeleteFileW(dest);
		PostStatus(g_cancel ? L"Stopped" : L"Could not copy the disc");
		PostDetail(L"Check there is about 700 MB free next to the launcher.");
		return 0;
	}
	return 1;
}

static int DeleteTree(const wchar_t *dir)
{
	wchar_t from[PATH_CAP + 2];
	SHFILEOPSTRUCTW op;
	size_t n;

	// Only our own build folder.
	if (!DirExists(dir) || ((wcsstr(dir, L"\\build") == NULL)))
	{
		return 1;
	}
	Copy(from, PATH_CAP, dir);
	n = wcslen(from);
	from[n + 1] = 0;
	memset(&op, 0, sizeof(op));
	op.wFunc = FO_DELETE;
	op.pFrom = from;
	op.fFlags = FOF_NO_UI;
	return SHFileOperationW(&op) == 0;
}

static int BuildGame(int clean)
{
	struct Tools t;
	wchar_t exe[PATH_CAP];
	wchar_t cmd[8 * PATH_CAP];
	wchar_t cache[PATH_CAP];
	wchar_t stamp[PATH_CAP];
	wchar_t lastStamp[PATH_CAP];
	int haveTools;
	int rc;

	haveTools = (g_srcRoot[0] != 0) && FindTools(&t);
	if (!haveTools)
	{
		if (!clean && FindGameExe(exe))
		{
			PostDetail(L"No compiler bundle here; using the game that is already built.");
			return 1;
		}
		PostStatus(L"The compiler bundle is missing");
		PostDetail(g_srcRoot[0] != 0
				? L"tools\\ (llvm-mingw, CMake, Ninja) should sit next to the launcher. Download the full CTR Launcher package again."
				: L"No game source next to the launcher. Download the full CTR Launcher package again.");
		return 0;
	}

	OpenLog(g_buildLog);
	LogLine("CTR Launcher build log");
	s_firstError[0] = 0;

	// Absolute paths go into the build folder; if the folder moved, start it fresh.
	swprintf(stamp, PATH_CAP, L"%ls|%ls", g_srcRoot, t.command[0] ? t.command : t.cc);
	IniGetString(g_launcherIni, L"launcher", L"build_stamp", lastStamp, PATH_CAP);
	Join(cache, PATH_CAP, g_buildDir, L"CMakeCache.txt");
	if (clean || (wcscmp(stamp, lastStamp) != 0))
	{
		if ((g_mode != MODE_PREBUILT) && FileExists(cache))
		{
			PostStatus(L"Clearing the old build");
			DeleteTree(g_buildDir);
		}
	}

	if (t.command[0] != 0)
	{
		PostPhase(PHASE_BUILD);
		PostStatus(L"Recompiling the game");
		Copy(cmd, 8 * PATH_CAP, t.command);
		rc = RunCommand(cmd, g_srcRoot, OnBuildLine);
	}
	else
	{
		wchar_t oldPath[8192];
		wchar_t newPath[8192 + 4 * PATH_CAP];
		wchar_t fs[6][PATH_CAP];

		GetEnvironmentVariableW(L"PATH", oldPath, 8192);
		swprintf(newPath, 8192 + 4 * PATH_CAP, L"%ls;%ls", t.binDirs, oldPath);
		SetEnvironmentVariableW(L"PATH", newPath);
		SetEnvironmentVariableW(L"NINJA_STATUS", L"[%f/%t] ");

		rc = 0;
		if (!FileExists(cache))
		{
			PostPhase(PHASE_CONFIGURE);
			PostStatus(L"Preparing the build");
			PostDetail(L"Checking the bundled compiler");
			s_configureLines = 0;
			ForwardSlashes(fs[0], PATH_CAP, t.ninja);
			ForwardSlashes(fs[1], PATH_CAP, t.cc);
			ForwardSlashes(fs[2], PATH_CAP, t.cxx[0] ? t.cxx : t.cc);
			ForwardSlashes(fs[3], PATH_CAP, t.rc);
			swprintf(cmd, 8 * PATH_CAP,
				L"\"%ls\" -S \"%ls\" -B \"%ls\" -G Ninja \"-DCMAKE_MAKE_PROGRAM=%ls\" \"-DCMAKE_C_COMPILER=%ls\" "
				L"\"-DCMAKE_CXX_COMPILER=%ls\" %ls%ls%ls -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "
				L"-DCTR_NATIVE_BUILD_LAUNCHER=OFF",
				t.cmake, g_srcRoot, g_buildDir, fs[0], fs[1], fs[2], t.rc[0] ? L"\"-DCMAKE_RC_COMPILER=" : L"",
				t.rc[0] ? fs[3] : L"", t.rc[0] ? L"\"" : L"");
			rc = RunCommand(cmd, g_srcRoot, OnConfigureLine);
			if (rc != 0)
			{
				DeleteFileW(cache);
			}
		}
		if (rc == 0)
		{
			// The build folder now belongs to this source and compiler.
			EnsureUnicodeIni(g_launcherIni);
			WritePrivateProfileStringW(L"launcher", L"build_stamp", stamp, g_launcherIni);
		}
		if ((rc == 0) && !g_cancel)
		{
			PostPhase(PHASE_BUILD);
			PostStatus(L"Recompiling the game from source");
			PostDetail(L"Starting the compiler");
			swprintf(cmd, 8 * PATH_CAP, L"\"%ls\" --build \"%ls\"", t.cmake, g_buildDir);
			rc = RunCommand(cmd, g_srcRoot, OnBuildLine);
		}
		SetEnvironmentVariableW(L"PATH", oldPath);
	}
	CloseLog();

	if (g_cancel)
	{
		PostStatus(L"Stopped");
		PostDetail(L"The build was cancelled.");
		return 0;
	}
	if (rc != 0)
	{
		wchar_t why[512];
		MultiByteToWideChar(CP_UTF8, 0, s_firstError[0] ? s_firstError : "see the log", -1, why, 512);
		PostStatus(L"The build failed");
		PostDetail(L"%ls", why);
		return 0;
	}
	if (t.command[0] != 0)
	{
		EnsureUnicodeIni(g_launcherIni);
		WritePrivateProfileStringW(L"launcher", L"build_stamp", stamp, g_launcherIni);
	}
	if (!FindGameExe(exe))
	{
		PostStatus(L"The build finished but ctr_native.exe is missing");
		PostDetail(L"See the log.");
		return 0;
	}
	return 1;
}

static int LaunchGame(void)
{
	wchar_t exe[PATH_CAP];
	wchar_t cmd[3 * PATH_CAP];
	wchar_t dir[PATH_CAP];
	SECURITY_ATTRIBUTES sa;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	HANDLE log;
	DWORD code = 0;
	DWORD start;

	if (!FindGameExe(exe))
	{
		PostStatus(L"ctr_native.exe was not found");
		return 0;
	}
	PostPhase(PHASE_LAUNCH);
	PostStatus(L"Starting Crash Team Racing");
	PostDetail(L"%dx%d%ls%ls", g_set.width, g_set.height, g_set.widescreen ? L" \x00B7 widescreen" : L" \x00B7 4:3",
		g_set.fullscreen ? L" \x00B7 fullscreen" : L"");

	CreateDirectoryW(g_logDir, NULL);
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	log = CreateFileW(g_gameLog, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	Copy(dir, PATH_CAP, exe);
	ParentDir(dir);
	swprintf(cmd, 3 * PATH_CAP, L"\"%ls\" --config \"%ls\"", exe, g_gameIni);
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	if (log != INVALID_HANDLE_VALUE)
	{
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = NULL;
		si.hStdOutput = log;
		si.hStdError = log;
	}
	if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, dir, &si, &pi))
	{
		if (log != INVALID_HANDLE_VALUE)
		{
			CloseHandle(log);
		}
		PostStatus(L"Could not start the game (error %lu)", (unsigned long)GetLastError());
		return 0;
	}
	if (log != INVALID_HANDLE_VALUE)
	{
		CloseHandle(log);
	}
	CloseHandle(pi.hThread);
	start = GetTickCount();
	PostPhase(PHASE_PLAYING);
	PostStatus(L"Playing");
	PostDetail(L"The launcher comes back when you quit the game.");
	PostMessageW(g_wnd, WM_APP_MINIMIZE, 0, 0);
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	PostMessageW(g_wnd, WM_APP_RESTORE, 0, 0);

	if (code != 0)
	{
		PostStatus(L"The game stopped (exit code %ld)", (long)code);
		PostDetail(GetTickCount() - start < 15000 ? L"It closed right away. Show log has what it said."
												  : L"Show log has what it said.");
		return 0;
	}
	PostStatus(L"Thanks for playing");
	PostDetail(L"Press Play to race again.");
	return 1;
}

static DWORD WINAPI Worker(LPVOID param)
{
	int task = (int)(INT_PTR)param;
	int ok = 1;

	if (task & TASK_PLAY)
	{
		ok = PlaceDisc();
	}
	if (ok && !g_cancel)
	{
		ok = BuildGame((task & TASK_CLEAN) != 0);
	}
	if (ok && !g_cancel && (task & TASK_PLAY))
	{
		ok = LaunchGame();
	}
	else if (ok && !g_cancel)
	{
		PostStatus(L"The game is rebuilt");
		PostDetail(L"Press Play to start it.");
	}
	ok = ok && !g_cancel;
	InterlockedExchange(&g_busy, 0);
	PostMessageW(g_wnd, WM_APP_DONE, (WPARAM)ok, 0);
	return 0;
}

static void UpdateButtons(void);

static void StartTask(int task)
{
	HANDLE thread;

	if (InterlockedCompareExchange(&g_busy, 1, 0) != 0)
	{
		return;
	}
	SaveSettings();
	g_cancel = 0;
	g_showLog = 0;
	g_phase = PHASE_IDLE;
	g_stepDone = 0;
	g_stepTotal = 0;
	g_shownPct = 0;
	g_phaseTick = g_stepTick = GetTickCount();
	Copy(g_status, 512, (task & TASK_PLAY) ? L"Getting ready" : L"Rebuilding the game");
	Copy(g_detail, 512, L"");
	UpdateButtons();
	SetTimer(g_wnd, 1, 60, NULL);
	thread = CreateThread(NULL, 0, Worker, (LPVOID)(INT_PTR)task, 0, NULL);
	if (thread == NULL)
	{
		InterlockedExchange(&g_busy, 0);
		UpdateButtons();
		return;
	}
	CloseHandle(thread);
}

// ---------------------------------------------------------------------------
// Progress (with a gentle creep while one long step runs, e.g. main.c)

static double TargetPct(void)
{
	double base, next, f;
	DWORD now = GetTickCount();

	switch (g_phase)
	{
	case PHASE_COPY:
		return (g_stepTotal > 0) ? (100.0 * g_stepDone / g_stepTotal) : 0.0;
	case PHASE_CONFIGURE:
		return 6.0 * ((g_stepTotal > 0) ? (double)g_stepDone / g_stepTotal : 0.0);
	case PHASE_BUILD:
		if (g_stepTotal <= 0)
		{
			return 6.0;
		}
		base = 6.0 + 94.0 * g_stepDone / g_stepTotal;
		next = 6.0 + 94.0 * (g_stepDone + 1 > g_stepTotal ? g_stepTotal : g_stepDone + 1) / g_stepTotal;
		f = (double)(now - g_stepTick) / (g_avgStepMs * 3.0 + 1.0);
		f = f / (1.0 + f); // approaches 1, never reaches it
		return base + (next - base) * 0.9 * f;
	case PHASE_LAUNCH:
	case PHASE_PLAYING:
	case PHASE_DONE:
		return 100.0;
	default:
		return g_shownPct;
	}
}

// ---------------------------------------------------------------------------
// Painting

static HFONT MakeFont(const wchar_t *face, int size, int weight, int italic)
{
	return CreateFontW(-S(size), 0, 0, 0, weight, italic, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static COLORREF Mix(COLORREF a, COLORREF b, int k) // k: 0 = a, 255 = b
{
	return RGB((GetRValue(a) * (255 - k) + GetRValue(b) * k) / 255, (GetGValue(a) * (255 - k) + GetGValue(b) * k) / 255,
		(GetBValue(a) * (255 - k) + GetBValue(b) * k) / 255);
}

static void FillRectColor(HDC dc, int l, int t, int r, int b, COLORREF c)
{
	RECT rc;
	HBRUSH br = CreateSolidBrush(c);
	SetRect(&rc, l, t, r, b);
	FillRect(dc, &rc, br);
	DeleteObject(br);
}

static void RoundRectColor(HDC dc, int l, int t, int r, int b, int radius, COLORREF fill, COLORREF line, int width)
{
	HBRUSH br = CreateSolidBrush(fill);
	HPEN pen = CreatePen(PS_SOLID, width, line);
	HGDIOBJ ob = SelectObject(dc, br);
	HGDIOBJ op = SelectObject(dc, pen);
	RoundRect(dc, l, t, r, b, radius, radius);
	SelectObject(dc, ob);
	SelectObject(dc, op);
	DeleteObject(br);
	DeleteObject(pen);
}

static void EllipseColor(HDC dc, int l, int t, int r, int b, COLORREF fill, COLORREF line, int width)
{
	HBRUSH br = CreateSolidBrush(fill);
	HPEN pen = CreatePen(PS_SOLID, width, line);
	HGDIOBJ ob = SelectObject(dc, br);
	HGDIOBJ op = SelectObject(dc, pen);
	Ellipse(dc, l, t, r, b);
	SelectObject(dc, ob);
	SelectObject(dc, op);
	DeleteObject(br);
	DeleteObject(pen);
}

static void PolygonColor(HDC dc, const POINT *pts, int n, COLORREF fill, COLORREF line, int width)
{
	HBRUSH br = CreateSolidBrush(fill);
	HPEN pen = CreatePen(PS_SOLID, width, line);
	HGDIOBJ ob = SelectObject(dc, br);
	HGDIOBJ op = SelectObject(dc, pen);
	Polygon(dc, pts, n);
	SelectObject(dc, ob);
	SelectObject(dc, op);
	DeleteObject(br);
	DeleteObject(pen);
}

static void LineColor(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int width)
{
	HPEN pen = CreatePen(PS_SOLID, width, c);
	HGDIOBJ op = SelectObject(dc, pen);
	MoveToEx(dc, x0, y0, NULL);
	LineTo(dc, x1, y1);
	SelectObject(dc, op);
	DeleteObject(pen);
}

static void Text(HDC dc, HFONT font, COLORREF color, int l, int t, int r, int b, const wchar_t *s, UINT flags)
{
	RECT rc;
	HGDIOBJ of = SelectObject(dc, font);
	SetTextColor(dc, color);
	SetBkMode(dc, TRANSPARENT);
	SetRect(&rc, l, t, r, b);
	DrawTextW(dc, s, -1, &rc, flags | DT_NOPREFIX);
	SelectObject(dc, of);
}

// Cartoon lettering: a drop shadow, a thick dark outline, then the fill.
static void OutlinedText(HDC dc, HFONT font, COLORREF fill, int l, int t, int r, int b, const wchar_t *s, UINT flags,
	int thick)
{
	Text(dc, font, COL_SHADOW, l, t + thick + S(2), r, b + thick + S(2), s, flags);
	for (int dy = -thick; dy <= thick; dy++)
	{
		for (int dx = -thick; dx <= thick; dx++)
		{
			if ((dx * dx + dy * dy) <= (thick * thick + 1) && ((dx != 0) || (dy != 0)))
			{
				Text(dc, font, COL_OUTLINE, l + dx, t + dy, r + dx, b + dy, s, flags);
			}
		}
	}
	Text(dc, font, fill, l, t, r, b, s, flags);
}

// Vertical gradient through three colors, by bands (no msimg32 needed).
static void Sky(HDC dc, int l, int t, int r, int b)
{
	int h = b - t;
	for (int i = 0; i < h; i += 2)
	{
		int k = (h > 1) ? (i * 510 / (h - 1)) : 0;
		COLORREF c = (k < 255) ? Mix(COL_SKY_TOP, COL_SKY_MID, k) : Mix(COL_SKY_MID, COL_SKY_BOTTOM, k - 255);
		FillRectColor(dc, l, t + i, r, (t + i + 2 < b) ? t + i + 2 : b, c);
	}
}

static void Checker(HDC dc, int l, int t, int r, int b, int sq, COLORREF a, COLORREF c)
{
	for (int y = t, row = 0; y < b; y += sq, row++)
	{
		for (int x = l, col = 0; x < r; x += sq, col++)
		{
			FillRectColor(dc, x, y, (x + sq < r) ? x + sq : r, (y + sq < b) ? y + sq : b, ((row + col) & 1) ? a : c);
		}
	}
}

// A kart seen from the side, facing right; (x, y) = the ground under its middle.
static void PaintKart(HDC dc, int x, int y, int k, COLORREF body)
{
	int w = 10 * k, wheel = 3 * k;
	POINT shell[] = {
		{x - w / 2, y - wheel - k},
		{x - w / 2 + k, y - wheel - 3 * k},
		{x + k, y - wheel - 3 * k},
		{x + w / 2 - k, y - wheel - 2 * k},
		{x + w / 2 + k, y - wheel - k},
		{x + w / 2, y - wheel},
		{x - w / 2, y - wheel},
	};
	POINT wing[] = {
		{x - w / 2 - k, y - wheel - 5 * k},
		{x - w / 2 + 2 * k, y - wheel - 5 * k},
		{x - w / 2 + k, y - wheel - 2 * k},
		{x - w / 2, y - wheel - 2 * k},
	};
	int line = (k / 3 > 1) ? k / 3 : 1;

	PolygonColor(dc, wing, 4, Mix(body, COL_OUTLINE, 70), COL_OUTLINE, line);
	PolygonColor(dc, shell, 7, body, COL_OUTLINE, line);
	// Driver: helmet and visor.
	EllipseColor(dc, x - 2 * k, y - wheel - 7 * k, x + 2 * k, y - wheel - 3 * k, COL_CURB_WHITE, COL_OUTLINE, line);
	FillRectColor(dc, x, y - wheel - 6 * k, x + 2 * k - line, y - wheel - 5 * k, COL_OUTLINE);
	// Wheels.
	EllipseColor(dc, x - w / 2 - k, y - 2 * wheel, x - w / 2 - k + 2 * wheel, y, RGB(30, 30, 34), COL_OUTLINE, line);
	EllipseColor(dc, x + w / 2 + k - 2 * wheel, y - 2 * wheel, x + w / 2 + k, y, RGB(30, 30, 34), COL_OUTLINE, line);
	EllipseColor(dc, x - w / 2 - k + wheel - k, y - wheel - k, x - w / 2 - k + wheel + k, y - wheel + k, COL_GREY_LT,
		COL_GREY_LT, 1);
	EllipseColor(dc, x + w / 2 + k - wheel - k, y - wheel - k, x + w / 2 + k - wheel + k, y - wheel + k, COL_GREY_LT,
		COL_GREY_LT, 1);
}

static void PaintBackdrop(HDC dc)
{
	RECT client;

	GetClientRect(g_wnd, &client);
	Sky(dc, 0, 0, client.right, client.bottom);

	// Speed streaks across the sky.
	for (int i = 0; i < 7; i++)
	{
		int y = S(60 + i * 92);
		int x0 = S((i * 173) % 400);
		COLORREF c = Mix(COL_SKY_MID, COL_CURB_WHITE, 46);
		LineColor(dc, x0, y, x0 + S(260 + (i * 37) % 140), y - S(26), c, S(2));
	}
}

static void PaintPlaceholderCover(HDC dc, const RECT *box)
{
	int w = box->right - box->left;
	int h = box->bottom - box->top;
	int cw, ch, l, t;

	ch = h;
	cw = ch * 3 / 4;
	if (cw > w)
	{
		cw = w;
		ch = cw * 4 / 3;
	}
	l = box->left + (w - cw) / 2;
	t = box->top + (h - ch) / 2;
	SetRect(&g_rcCover, l, t, l + cw, t + ch);

	RoundRectColor(dc, l + S(6), t + S(8), l + cw + S(6), t + ch + S(8), S(18), COL_SHADOW, COL_SHADOW, 1);
	RoundRectColor(dc, l, t, l + cw, t + ch, S(18), COL_ASPHALT, COL_OUTLINE, S(4));
	{
		// Sunset band with a road heading to the horizon.
		HRGN clip = CreateRoundRectRgn(l + S(3), t + S(3), l + cw - S(2), t + ch - S(2), S(16), S(16));
		POINT road[] = {{l + cw / 2 - S(8), t + ch * 52 / 100}, {l + cw / 2 + S(8), t + ch * 52 / 100},
			{l + cw - S(20), t + ch}, {l + S(20), t + ch}};
		SelectClipRgn(dc, clip);
		Sky(dc, l, t, l + cw, t + ch * 52 / 100);
		FillRectColor(dc, l, t + ch * 52 / 100, l + cw, t + ch, RGB(36, 110, 60));
		PolygonColor(dc, road, 4, COL_ASPHALT_DK, COL_ASPHALT_DK, 1);
		for (int i = 0; i < 5; i++)
		{
			int y0 = t + ch * 56 / 100 + i * i * S(10) + i * S(14);
			LineColor(dc, l + cw / 2, y0, l + cw / 2, y0 + S(6 + i * 4), COL_YELLOW, S(2 + i));
		}
		SelectClipRgn(dc, NULL);
		DeleteObject(clip);
	}
	PaintKart(dc, l + cw / 2, t + ch * 86 / 100, S(9), COL_ORANGE);
	OutlinedText(dc, g_fontCover, COL_YELLOW, l, t + ch * 12 / 100, l + cw, t + ch * 34 / 100, L"CTR",
		DT_CENTER | DT_SINGLELINE | DT_VCENTER, S(3));
	OutlinedText(dc, g_fontButton, COL_TEXT, l + S(16), t + ch * 36 / 100, l + cw - S(16), t + ch * 36 / 100 + S(26),
		L"Drop your cover art here", DT_CENTER | DT_SINGLELINE, S(2));
	Text(dc, g_fontSmall, COL_TEXT, l + S(16), t + ch * 36 / 100 + S(28), l + cw - S(16), t + ch * 36 / 100 + S(48),
		L"or click to pick a picture", DT_CENTER | DT_SINGLELINE);
}

static void PaintCover(HDC dc, const RECT *box)
{
	int w = box->right - box->left;
	int h = box->bottom - box->top;
	int dw, dh, l, t;
	HDC mem;
	HGDIOBJ old;

	if (g_coverBmp == NULL)
	{
		PaintPlaceholderCover(dc, box);
		return;
	}
	dw = w;
	dh = (int)((long long)g_coverH * w / g_coverW);
	if (dh > h)
	{
		dh = h;
		dw = (int)((long long)g_coverW * h / g_coverH);
	}
	l = box->left + (w - dw) / 2;
	t = box->top + (h - dh) / 2;
	SetRect(&g_rcCover, l, t, l + dw, t + dh);

	// Glow, shadow, then the picture in a thick cartoon frame.
	for (int i = 6; i >= 1; i--)
	{
		FillRectColor(dc, l - S(4 + i * 3), t - S(4 + i * 3), l + dw + S(4 + i * 3), t + dh + S(4 + i * 3),
			Mix(COL_ASPHALT_DK, COL_ORANGE, 30 + (6 - i) * 14));
	}
	FillRectColor(dc, l + S(4), t + S(10), l + dw + S(10), t + dh + S(12), COL_SHADOW);
	FillRectColor(dc, l - S(5), t - S(5), l + dw + S(5), t + dh + S(5), COL_OUTLINE);
	FillRectColor(dc, l - S(2), t - S(2), l + dw + S(2), t + dh + S(2), COL_CURB_WHITE);
	if ((g_coverScaled == NULL) || (g_coverScaledW != dw) || (g_coverScaledH != dh))
	{
		// HALFTONE from a big picture is slow: do it once, not on every progress tick.
		HDC src = CreateCompatibleDC(dc);
		HDC dst = CreateCompatibleDC(dc);
		HGDIOBJ oldSrc, oldDst;

		if (g_coverScaled != NULL)
		{
			DeleteObject(g_coverScaled);
		}
		g_coverScaled = CreateCompatibleBitmap(dc, dw, dh);
		g_coverScaledW = dw;
		g_coverScaledH = dh;
		oldSrc = SelectObject(src, g_coverBmp);
		oldDst = SelectObject(dst, g_coverScaled);
		SetStretchBltMode(dst, HALFTONE);
		SetBrushOrgEx(dst, 0, 0, NULL);
		StretchBlt(dst, 0, 0, dw, dh, src, 0, 0, g_coverW, g_coverH, SRCCOPY);
		SelectObject(src, oldSrc);
		SelectObject(dst, oldDst);
		DeleteDC(src);
		DeleteDC(dst);
	}
	mem = CreateCompatibleDC(dc);
	old = SelectObject(mem, g_coverScaled);
	BitBlt(dc, l, t, dw, dh, mem, 0, 0, SRCCOPY);
	SelectObject(mem, old);
	DeleteDC(mem);
}

// A red-and-white racing curb along a rectangle's top.
static void PaintCurb(HDC dc, int l, int t, int r, int h)
{
	int step = S(22);
	HRGN clip = CreateRectRgn(l, t, r, t + h);

	SelectClipRgn(dc, clip);
	for (int x = l - h, i = 0; x < r; x += step, i++)
	{
		POINT p[] = {{x, t + h}, {x + h, t}, {x + h + step, t}, {x + step, t + h}};
		PolygonColor(dc, p, 4, (i & 1) ? COL_CURB_WHITE : COL_CURB_RED, (i & 1) ? COL_CURB_WHITE : COL_CURB_RED, 1);
	}
	SelectClipRgn(dc, NULL);
	DeleteObject(clip);
	FillRectColor(dc, l, t + h, r, t + h + S(3), COL_OUTLINE);
}

// The asphalt panel: rounded, outlined, a curb on top and a faint tread texture.
static void PaintPanel(HDC dc, int l, int t, int r, int b)
{
	HRGN clip;

	RoundRectColor(dc, l + S(6), t + S(10), r + S(6), b + S(10), S(28), COL_SHADOW, COL_SHADOW, 1);
	RoundRectColor(dc, l, t, r, b, S(28), COL_ASPHALT, COL_OUTLINE, S(4));
	clip = CreateRoundRectRgn(l + S(3), t + S(3), r - S(2), b - S(2), S(26), S(26));
	SelectClipRgn(dc, clip);
	for (int y = t + S(30); y < b; y += S(18))
	{
		for (int x = l + ((y / S(18)) & 1) * S(14); x < r; x += S(28))
		{
			LineColor(dc, x, y, x + S(7), y + S(5), RGB(43, 43, 54), S(2));
			LineColor(dc, x + S(7), y + S(5), x + S(14), y, RGB(43, 43, 54), S(2));
		}
	}
	PaintCurb(dc, l, t, r, S(14));
	SelectClipRgn(dc, NULL);
	DeleteObject(clip);
}

static void PaintLeftPage(HDC dc)
{
	RECT box;
	int l = S(LP_L), t = S(LP_T), r = S(LP_R), b = S(LP_B);
	HRGN clip;

	// Checkered-flag backdrop in a framed panel.
	RoundRectColor(dc, l + S(6), t + S(10), r + S(6), b + S(10), S(28), COL_SHADOW, COL_SHADOW, 1);
	RoundRectColor(dc, l, t, r, b, S(28), COL_ASPHALT_DK, COL_OUTLINE, S(4));
	clip = CreateRoundRectRgn(l + S(3), t + S(3), r - S(2), b - S(2), S(26), S(26));
	SelectClipRgn(dc, clip);
	Checker(dc, l, t, r, b, S(32), RGB(34, 30, 48), RGB(44, 40, 62));
	SelectClipRgn(dc, NULL);
	DeleteObject(clip);

	SetRect(&box, l + S(46), t + S(42), r - S(46), b - S(140));
	PaintCover(dc, &box);

	OutlinedText(dc, g_fontTitle, COL_YELLOW, l, b - S(122), r, b - S(82), L"CRASH TEAM RACING",
		DT_CENTER | DT_SINGLELINE, S(2));
	Text(dc, g_fontSmall, COL_TEXT_SOFT, l, b - S(80), r, b - S(60), L"Native PC \x00B7 recompiled from your own disc",
		DT_CENTER | DT_SINGLELINE);
	SetRect(&g_rcCoverLink, (l + r) / 2 - S(90), b - S(48), (l + r) / 2 + S(90), b - S(24));
	Text(dc, g_fontButton, COL_ORANGE_LT, g_rcCoverLink.left, g_rcCoverLink.top, g_rcCoverLink.right,
		g_rcCoverLink.bottom, g_coverBmp != NULL ? L"Change cover\x2026" : L"Add cover art\x2026",
		DT_CENTER | DT_SINGLELINE);
}

static void DashedRoundRect(HDC dc, const RECT *rc, COLORREF color, int radius)
{
	LOGBRUSH lb;
	DWORD dashes[2];
	HPEN pen;
	HGDIOBJ op, ob;

	lb.lbStyle = BS_SOLID;
	lb.lbColor = color;
	lb.lbHatch = 0;
	dashes[0] = (DWORD)S(10);
	dashes[1] = (DWORD)S(7);
	pen = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT, (DWORD)S(3), &lb, 2, dashes);
	op = SelectObject(dc, pen);
	ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
	RoundRect(dc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
	SelectObject(dc, op);
	SelectObject(dc, ob);
	DeleteObject(pen);
}

static void PaintDiscIcon(HDC dc, int cx, int cy, int r, COLORREF ring)
{
	EllipseColor(dc, cx - r, cy - r, cx + r, cy + r, RGB(214, 218, 232), COL_OUTLINE, S(3));
	EllipseColor(dc, cx - r * 70 / 100, cy - r * 70 / 100, cx + r * 70 / 100, cy + r * 70 / 100, Mix(ring, COL_TEXT, 90),
		Mix(ring, COL_TEXT, 90), 1);
	EllipseColor(dc, cx - r / 4, cy - r / 4, cx + r / 4, cy + r / 4, COL_ASPHALT_LT, COL_OUTLINE, S(2));
}

// The progress bar is a stretch of track: the kart drives to the checkered flag.
static void PaintTrack(HDC dc, int l, int t, int r)
{
	int h = S(38);
	int finish = S(30);
	double pct = g_shownPct;
	int run = r - finish - l;
	int x = l + (int)(run * pct / 100.0);
	COLORREF lane = (g_phase == PHASE_FAILED) ? COL_BAD : (g_phase == PHASE_DONE ? COL_JUNGLE_LT : COL_ORANGE);
	HRGN clip;
	wchar_t pctText[16];

	RoundRectColor(dc, l, t, r, t + h, S(14), COL_ASPHALT_DK, COL_OUTLINE, S(3));
	clip = CreateRoundRectRgn(l + S(2), t + S(2), r - S(1), t + h - S(1), S(12), S(12));
	SelectClipRgn(dc, clip);
	// The stretch already driven, then the center line, then the finish.
	if (x > l)
	{
		FillRectColor(dc, l, t, x, t + h, Mix(COL_ASPHALT_DK, lane, 120));
		FillRectColor(dc, l, t + h - S(6), x, t + h, lane);
	}
	for (int dx = l + S(12); dx < r - finish; dx += S(26))
	{
		FillRectColor(dc, dx, t + h / 2 - S(1), dx + S(12), t + h / 2 + S(2), RGB(220, 220, 210));
	}
	FillRectColor(dc, l, t, l + S(5), t + h, COL_JUNGLE_LT);
	Checker(dc, r - finish, t, r, t + h, S(8), COL_OUTLINE, COL_CURB_WHITE);
	SelectClipRgn(dc, NULL);
	DeleteObject(clip);

	if (g_phase != PHASE_IDLE)
	{
		PaintKart(dc, x + S(14) > r - S(14) ? r - S(14) : (x < l + S(16) ? l + S(16) : x), t + h - S(4), S(3),
			(g_phase == PHASE_FAILED) ? COL_GREY_LT : COL_ORANGE);
	}
	if ((g_phase != PHASE_IDLE) && (g_phase != PHASE_FAILED))
	{
		swprintf(pctText, 16, L"%d%%", (int)(pct + 0.5));
		OutlinedText(dc, g_fontButton, COL_YELLOW, l, t - S(26), r, t - S(4), pctText, DT_RIGHT | DT_SINGLELINE, S(1));
	}
}

static void PaintHeading(HDC dc, const wchar_t *title, const wchar_t *sub)
{
	int t = S(RP_T);
	OutlinedText(dc, g_fontHeading, COL_YELLOW, S(RX), t + S(32), S(RR) - S(140), t + S(80), title,
		DT_LEFT | DT_SINGLELINE, S(3));
	Text(dc, g_fontSmall, COL_TEXT_SOFT, S(RX), t + S(82), S(RR) - S(140), t + S(102), sub, DT_LEFT | DT_SINGLELINE);
}

static void Caps(HDC dc, int y, const wchar_t *s)
{
	Text(dc, g_fontCaps, COL_ORANGE_LT, S(RX), S(RP_T) + S(y), S(RR), S(RP_T) + S(y + 18), s, DT_LEFT | DT_SINGLELINE);
}

static void PaintHome(HDC dc)
{
	int t = S(RP_T);
	int rx = S(RX), rr = S(RR);
	COLORREF statusColor = COL_TEXT;
	COLORREF edge;
	wchar_t pathLine[PATH_CAP];

	PaintHeading(dc, L"PLAY", L"Drop your disc, press Play, race.");
	Caps(dc, 118, L"YOUR GAME DISC");

	SetRect(&g_rcDrop, rx, t + S(140), rr, t + S(322));
	edge = g_disc.state == 1 ? COL_OK : (g_disc.state == 3 ? COL_BAD : (g_disc.state == 2 ? COL_WARN : COL_TEXT_FAINT));
	RoundRectColor(dc, g_rcDrop.left, g_rcDrop.top, g_rcDrop.right, g_rcDrop.bottom, S(18), COL_ASPHALT_LT,
		COL_ASPHALT_LT, 1);
	DashedRoundRect(dc, &g_rcDrop, edge, S(18));

	if (g_disc.state == 0)
	{
		PaintDiscIcon(dc, (rx + rr) / 2, t + S(184), S(26), COL_ORANGE);
		OutlinedText(dc, g_fontButton, COL_TEXT, rx, t + S(216), rr, t + S(240), L"Drop your CTR disc here",
			DT_CENTER | DT_SINGLELINE, S(1));
		Text(dc, g_fontSmall, COL_TEXT_SOFT, rx, t + S(240), rr, t + S(258),
			L"the folder with your .bin/.cue, or the .bin itself", DT_CENTER | DT_SINGLELINE);
	}
	else
	{
		PaintDiscIcon(dc, rx + S(48), t + S(200), S(28), edge);
		OutlinedText(dc, g_fontButton, edge, rx + S(92), t + S(162), rr - S(16), t + S(186), g_disc.title,
			DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, S(1));
		Text(dc, g_fontSmall, COL_TEXT_SOFT, rx + S(92), t + S(190), rr - S(16), t + S(226), g_disc.detail,
			DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
		Copy(pathLine, PATH_CAP, g_discSource[0] ? g_discSource : g_disc.bin);
		Text(dc, g_fontSmall, COL_TEXT_FAINT, rx + S(92), t + S(228), rr - S(16), t + S(246), pathLine,
			DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS);
	}

	Caps(dc, 342, L"RECOMPILE");
	if (g_phase == PHASE_FAILED)
	{
		statusColor = COL_BAD;
	}
	else if (g_phase == PHASE_DONE)
	{
		statusColor = COL_OK;
	}
	Text(dc, g_fontButton, statusColor, rx, t + S(362), rr - S(60), t + S(386), g_status,
		DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
	PaintTrack(dc, rx, t + S(394), rr);
	Text(dc, g_fontSmall, COL_TEXT_SOFT, rx, t + S(440), rr, t + S(480), g_detail, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

	Text(dc, g_fontSmall, COL_TEXT_FAINT, rx, t + S(580), rr, t + S(630),
		L"Your disc is never changed. Play links it into assets\\ctr-u.bin, recompiles the game with the bundled "
		L"compiler and starts it.",
		DT_LEFT | DT_WORDBREAK);
}

static void Card(HDC dc, int top, int bottom)
{
	RoundRectColor(dc, S(RX) - S(16), S(RP_T) + S(top), S(RR) + S(16), S(RP_T) + S(bottom), S(18), COL_CARD,
		COL_OUTLINE, S(2));
}

static void RowLabel(HDC dc, int y, const wchar_t *s)
{
	int t = S(RP_T);
	Text(dc, g_fontButton, COL_TEXT, S(RX), t + S(y), S(RX) + S(170), t + S(y + 26), s, DT_LEFT | DT_SINGLELINE);
}

static void PaintSettings(HDC dc)
{
	int t = S(RP_T);

	PaintHeading(dc, L"GARAGE", L"Settings \x00B7 saved as you change them");
	Card(dc, 164, 600);

	if (g_tab == TAB_DISPLAY)
	{
		RowLabel(dc, 186, L"Resolution");
	}
	else if (g_tab == TAB_GRAPHICS)
	{
		const wchar_t *hint = L"";

		RowLabel(dc, 186, L"Upscaling");
		RowLabel(dc, 230, L"FSR sharpness");
		RowLabel(dc, 274, L"Anti-aliasing");
		RowLabel(dc, 318, L"CRT effect");
		RowLabel(dc, 362, L"Brightness");
		switch (g_set.upscale)
		{
		case 1:
			hint = L"Smooth: the PSX picture is scaled up with bilinear filtering. Softer edges, no blocky pixels.";
			break;
		case 2:
			hint = L"AMD FSR 1: edge-aware upscaling (EASU) to your resolution, then contrast-adaptive sharpening "
				   L"(RCAS). The crispest picture at 1440p.";
			break;
		default:
			hint = L"Sharp pixels: the original PSX picture, every pixel kept square and crisp.";
			break;
		}
		Text(dc, g_fontSmall, COL_TEXT_SOFT, S(RX), t + S(462), S(RR), t + S(520), hint, DT_LEFT | DT_WORDBREAK);
		Text(dc, g_fontSmall, COL_TEXT_FAINT, S(RX), t + S(524), S(RR), t + S(592),
			L"FXAA smooths jagged polygon edges. The CRT effect brings back the look of a 1999 TV: scanlines, and with "
			L"Curved CRT an aperture mask, a curved screen and dark corners.",
			DT_LEFT | DT_WORDBREAK);
	}
	else
	{
		Text(dc, g_fontSmall, COL_TEXT_SOFT, S(RX), t + S(486), S(RR), t + S(592),
			L"Pads use the PlayStation layout: A / \x2715 accelerate, X / \x25A1 brake, B / \x25CB item, "
			L"R1 hop. Plug one in any time, up to four players.",
			DT_LEFT | DT_WORDBREAK);
	}
	Text(dc, g_fontSmall, COL_TEXT_FAINT, S(RX), t + S(610), S(RR), t + S(636),
		L"Saved in ctr_native.ini. Used the next time you press Play.", DT_LEFT | DT_SINGLELINE);
}

static void Paint(HDC dc)
{
	PaintBackdrop(dc);
	PaintLeftPage(dc);
	PaintPanel(dc, S(RP_L), S(RP_T), S(RP_R), S(RP_B));
	if (g_page == PAGE_HOME)
	{
		PaintHome(dc);
	}
	else
	{
		PaintSettings(dc);
	}
}

static void PaintBuffered(HDC dc)
{
	RECT rc;
	HDC mem;
	HBITMAP bmp;
	HGDIOBJ old;

	GetClientRect(g_wnd, &rc);
	mem = CreateCompatibleDC(dc);
	bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
	old = SelectObject(mem, bmp);
	Paint(mem);
	BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
}

static int IsToggle(UINT id)
{
	return (id >= ID_FULLSCREEN) && (id <= ID_VIBRATION);
}

static int IsTab(UINT id)
{
	return (id >= ID_TAB_DISPLAY) && (id <= ID_TAB_CONTROLS);
}

static int ToggleValue(UINT id)
{
	switch (id)
	{
	case ID_FULLSCREEN:
		return g_set.fullscreen;
	case ID_WIDESCREEN:
		return g_set.widescreen;
	case ID_KEYBOARD:
		return g_set.keyboard;
	case ID_CONTROLLER:
		return g_set.controller;
	case ID_XBOX:
		return g_set.xbox;
	case ID_PLAYSTATION:
		return g_set.playstation;
	case ID_VSYNC:
		return g_set.vsync;
	case ID_INTSCALE:
		return g_set.intscale;
	case ID_TEXFILTER:
		return g_set.texfilter;
	case ID_VIBRATION:
		return g_set.vibration;
	default:
		return 0;
	}
}

static int *ToggleField(UINT id)
{
	switch (id)
	{
	case ID_FULLSCREEN:
		return &g_set.fullscreen;
	case ID_WIDESCREEN:
		return &g_set.widescreen;
	case ID_VSYNC:
		return &g_set.vsync;
	case ID_INTSCALE:
		return &g_set.intscale;
	case ID_TEXFILTER:
		return &g_set.texfilter;
	case ID_KEYBOARD:
		return &g_set.keyboard;
	case ID_CONTROLLER:
		return &g_set.controller;
	case ID_XBOX:
		return &g_set.xbox;
	case ID_PLAYSTATION:
		return &g_set.playstation;
	case ID_VIBRATION:
		return &g_set.vibration;
	default:
		return NULL;
	}
}

static const wchar_t *ToggleHint(UINT id)
{
	switch (id)
	{
	case ID_FULLSCREEN:
		return L"The whole screen, no window border";
	case ID_WIDESCREEN:
		return L"Fill a 16:9 or 16:10 screen. Off: the original 4:3 with bars";
	case ID_KEYBOARD:
		return L"Arrows steer \x00B7 C gas \x00B7 X brake \x00B7 V item \x00B7 Shift hop \x00B7 Enter start";
	case ID_CONTROLLER:
		return L"Gamepads, up to four players";
	case ID_XBOX:
		return L"Xbox 360, One and Series pads (XInput)";
	case ID_PLAYSTATION:
		return L"DualShock 3 and 4, DualSense";
	case ID_VSYNC:
		return L"No tearing; waits for the screen's refresh";
	case ID_INTSCALE:
		return L"Whole-number scaling: every scanline the same height";
	case ID_TEXFILTER:
		return L"Smooths 3D textures (the PSX drew them unfiltered)";
	case ID_VIBRATION:
		return L"Rumble on pads that have it";
	default:
		return L"";
	}
}

// Settings rows: label and hint on the left, a toggle switch on the right.
static void DrawToggle(const DRAWITEMSTRUCT *di)
{
	wchar_t text[96];
	RECT rc = di->rcItem;
	int on = ToggleValue(di->CtlID);
	int disabled = (di->itemState & ODS_DISABLED) != 0;
	int sw = S(54), sh = S(28);
	int sx = rc.right - sw - S(4);
	int sy = rc.top + (rc.bottom - rc.top - sh) / 2;
	int knob = sh - S(8);
	COLORREF track = disabled ? COL_GREY : (on ? COL_JUNGLE : RGB(74, 74, 90));

	GetWindowTextW(di->hwndItem, text, 96);
	FillRectColor(di->hDC, rc.left, rc.top, rc.right, rc.bottom, COL_CARD);
	Text(di->hDC, g_fontButton, disabled ? COL_TEXT_FAINT : COL_TEXT, rc.left, rc.top + S(2), sx - S(8),
		rc.top + S(24), text, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
	Text(di->hDC, g_fontSmall, disabled ? COL_GREY : COL_TEXT_SOFT, rc.left, rc.top + S(24), sx - S(8), rc.bottom,
		ToggleHint(di->CtlID), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

	RoundRectColor(di->hDC, sx, sy, sx + sw, sy + sh, sh, track, COL_OUTLINE, S(2));
	{
		int kx = on ? sx + sw - knob - S(4) : sx + S(4);
		EllipseColor(di->hDC, kx, sy + S(4), kx + knob, sy + S(4) + knob, disabled ? COL_GREY_LT : COL_CURB_WHITE,
			COL_OUTLINE, S(2));
	}
	if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT))
	{
		RECT f;
		SetRect(&f, sx - S(3), sy - S(3), sx + sw + S(3), sy + sh + S(3));
		DrawFocusRect(di->hDC, &f);
	}
}

// Chunky cartoon buttons: dark outline, drop shadow, a glossy top, a press that sinks.
static void DrawButton(const DRAWITEMSTRUCT *di)
{
	wchar_t text[64];
	int disabled = (di->itemState & ODS_DISABLED) != 0;
	int down = (di->itemState & ODS_SELECTED) != 0;
	COLORREF fill, gloss, under;
	RECT rc = di->rcItem;
	int drop = S(5);
	int r = S(16);
	int big = (di->CtlID == ID_PLAY);

	if (IsToggle(di->CtlID))
	{
		DrawToggle(di);
		return;
	}
	if (IsTab(di->CtlID))
	{
		int selected = ((int)di->CtlID - ID_TAB_DISPLAY) == g_tab;
		GetWindowTextW(di->hwndItem, text, 64);
		FillRectColor(di->hDC, rc.left, rc.top, rc.right, rc.bottom, COL_ASPHALT);
		RoundRectColor(di->hDC, rc.left, rc.top, rc.right, rc.bottom, S(14), selected ? COL_ORANGE : COL_ASPHALT_LT,
			COL_OUTLINE, S(3));
		if (selected)
		{
			OutlinedText(di->hDC, g_fontButton, COL_TEXT, rc.left, rc.top, rc.right, rc.bottom, text,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE, S(1));
		}
		else
		{
			Text(di->hDC, g_fontButton, COL_TEXT_SOFT, rc.left, rc.top, rc.right, rc.bottom, text,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		return;
	}

	under = (di->CtlID == ID_BROWSE) ? COL_ASPHALT_LT : COL_ASPHALT;
	switch (di->CtlID)
	{
	case ID_PLAY:
		fill = COL_ORANGE;
		gloss = COL_ORANGE_LT;
		break;
	case ID_SETTINGS:
		fill = COL_BLUE;
		gloss = COL_BLUE_LT;
		break;
	default:
		fill = COL_JUNGLE;
		gloss = COL_JUNGLE_LT;
		break;
	}
	if ((di->CtlID == ID_PLAY) && (g_busy != 0) && (g_phase != PHASE_PLAYING))
	{
		fill = COL_CURB_RED; // Cancel
		gloss = RGB(255, 110, 110);
	}
	if (disabled)
	{
		fill = COL_GREY;
		gloss = COL_GREY_LT;
	}

	GetWindowTextW(di->hwndItem, text, 64);
	FillRectColor(di->hDC, rc.left, rc.top, rc.right, rc.bottom, under);
	if (!down)
	{
		RoundRectColor(di->hDC, rc.left, rc.top + drop, rc.right, rc.bottom, r, COL_SHADOW, COL_SHADOW, 1);
	}
	{
		int top = rc.top + (down ? drop : 0);
		int bottom = rc.bottom - drop + (down ? drop : 0);
		HRGN clip;

		RoundRectColor(di->hDC, rc.left, top, rc.right, bottom, r, fill, COL_OUTLINE, S(3));
		clip = CreateRoundRectRgn(rc.left + S(3), top + S(3), rc.right - S(2), bottom - S(2), r - S(4), r - S(4));
		SelectClipRgn(di->hDC, clip);
		FillRectColor(di->hDC, rc.left, top, rc.right, top + (bottom - top) * 45 / 100, gloss);
		SelectClipRgn(di->hDC, NULL);
		DeleteObject(clip);
		if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT))
		{
			RECT f;
			SetRect(&f, rc.left + S(6), top + S(6), rc.right - S(6), bottom - S(6));
			DrawFocusRect(di->hDC, &f);
		}
		OutlinedText(di->hDC, big ? g_fontBig : g_fontButton, disabled ? RGB(220, 220, 226) : COL_TEXT, rc.left, top,
			rc.right, bottom, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, big ? S(2) : S(1));
	}
}

// ---------------------------------------------------------------------------
// Controls

static HWND MakeButton(const wchar_t *text, int id, int x, int y, int w, int h)
{
	return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, S(x), S(y), S(w), S(h), g_wnd,
		(HMENU)(INT_PTR)id, g_inst, NULL);
}

static void FillResolutions(void)
{
	int sel = -1;

	SendMessageW(g_cmbRes, CB_RESETCONTENT, 0, 0);
	for (int i = 0; i < RES_COUNT; i++)
	{
		wchar_t label[64];
		swprintf(label, 64, L"%d \x00D7 %d   (%ls)", s_resolutions[i].w, s_resolutions[i].h, s_resolutions[i].aspect);
		SendMessageW(g_cmbRes, CB_ADDSTRING, 0, (LPARAM)label);
		if ((s_resolutions[i].w == g_set.width) && (s_resolutions[i].h == g_set.height))
		{
			sel = i;
		}
	}
	g_customResIndex = -1;
	if (sel < 0)
	{
		wchar_t label[64];
		swprintf(label, 64, L"%d \x00D7 %d   (from ctr_native.ini)", g_set.width, g_set.height);
		g_customResIndex = (int)SendMessageW(g_cmbRes, CB_ADDSTRING, 0, (LPARAM)label);
		sel = g_customResIndex;
	}
	SendMessageW(g_cmbRes, CB_SETCURSEL, (WPARAM)sel, 0);
}

static HWND MakeCombo(int id, int y, const wchar_t *const *items, int count, int sel)
{
	HWND h = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, S(RX + 170),
		S(RP_T + y), S(RR - RX - 170), S(260), g_wnd, (HMENU)(INT_PTR)id, g_inst, NULL);
	SendMessageW(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
	for (int i = 0; i < count; i++)
	{
		SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)items[i]);
	}
	SendMessageW(h, CB_SETCURSEL, (WPARAM)sel, 0);
	return h;
}

static void SyncChecks(void)
{
	HWND toggles[] = {g_chkFull, g_chkWide, g_chkVsync, g_chkIntScale, g_chkTexFilter, g_chkKb, g_chkPad, g_chkXbox,
		g_chkPs, g_chkVibration};

	EnableWindow(g_chkXbox, g_set.controller);
	EnableWindow(g_chkPs, g_set.controller);
	EnableWindow(g_chkVibration, g_set.controller);
	EnableWindow(g_cmbSharp, g_set.upscale == 2);
	for (int i = 0; i < 10; i++)
	{
		InvalidateRect(toggles[i], NULL, FALSE);
	}
}

static void CreateControls(void)
{
	int t = RP_T;

	g_btnSettings = MakeButton(L"Settings", ID_SETTINGS, RR - 132, t + 38, 132, 46);

	g_btnBrowse = MakeButton(L"Browse\x2026", ID_BROWSE, (RX + RR) / 2 - 75, t + 266, 150, 44);
	g_btnPlay = MakeButton(L"PLAY", ID_PLAY, RX, t + 496, 220, 70);
	g_btnRebuild = MakeButton(L"Rebuild", ID_REBUILD, RX + 236, t + 506, 120, 52);
	g_btnLog = MakeButton(L"Show log", ID_LOG, RX + 368, t + 506, 96, 52);

	{
		int tw = (RR - RX - 16) / 3;
		g_tabDisplay = MakeButton(L"Display", ID_TAB_DISPLAY, RX, t + 110, tw, 42);
		g_tabGraphics = MakeButton(L"Graphics", ID_TAB_GRAPHICS, RX + tw + 8, t + 110, tw, 42);
		g_tabControls = MakeButton(L"Controls", ID_TAB_CONTROLS, RX + 2 * (tw + 8), t + 110, tw, 42);
	}

	// Display
	g_cmbRes = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
		S(RX + 170), S(t + 182), S(RR - RX - 170), S(320), g_wnd, (HMENU)(INT_PTR)ID_RESOLUTION, g_inst, NULL);
	SendMessageW(g_cmbRes, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
	g_chkFull = MakeButton(L"Fullscreen", ID_FULLSCREEN, RX, t + 230, RR - RX, 44);
	g_chkWide = MakeButton(L"Widescreen", ID_WIDESCREEN, RX, t + 282, RR - RX, 44);
	g_chkVsync = MakeButton(L"VSync", ID_VSYNC, RX, t + 334, RR - RX, 44);
	g_chkIntScale = MakeButton(L"Pixel-perfect scaling", ID_INTSCALE, RX, t + 386, RR - RX, 44);

	// Graphics
	g_cmbUpscale = MakeCombo(ID_UPSCALE, 182, s_upscaleNames, 3, g_set.upscale);
	g_cmbSharp = MakeCombo(ID_SHARPNESS, 226, s_sharpNames, 3, g_set.sharpness);
	g_cmbAa = MakeCombo(ID_AA, 270, s_aaNames, 2, g_set.aa);
	g_cmbCrt = MakeCombo(ID_CRT, 314, s_crtNames, 3, g_set.crt);
	g_cmbBright = MakeCombo(ID_BRIGHTNESS, 358, s_brightNames, 5, g_set.brightness + 2);
	g_chkTexFilter = MakeButton(L"Texture filtering", ID_TEXFILTER, RX, t + 404, RR - RX, 44);

	// Controls
	g_chkKb = MakeButton(L"Keyboard", ID_KEYBOARD, RX, t + 182, RR - RX, 44);
	g_chkPad = MakeButton(L"Controller", ID_CONTROLLER, RX, t + 234, RR - RX, 44);
	g_chkXbox = MakeButton(L"Xbox input", ID_XBOX, RX + 22, t + 284, RR - RX - 22, 44);
	g_chkPs = MakeButton(L"PlayStation input", ID_PLAYSTATION, RX + 22, t + 334, RR - RX - 22, 44);
	g_chkVibration = MakeButton(L"Vibration", ID_VIBRATION, RX + 22, t + 384, RR - RX - 22, 44);

	FillResolutions();
	SyncChecks();
}

static void ShowPage(int page)
{
	int home = (page == PAGE_HOME);
	HWND homeCtl[] = {g_btnBrowse, g_btnPlay, g_btnRebuild};
	HWND tabs[] = {g_tabDisplay, g_tabGraphics, g_tabControls};
	HWND displayCtl[] = {g_cmbRes, g_chkFull, g_chkWide, g_chkVsync, g_chkIntScale};
	HWND graphicsCtl[] = {g_cmbUpscale, g_cmbSharp, g_cmbAa, g_cmbCrt, g_cmbBright, g_chkTexFilter};
	HWND controlsCtl[] = {g_chkKb, g_chkPad, g_chkXbox, g_chkPs, g_chkVibration};

	g_page = page;
	for (int i = 0; i < 3; i++)
	{
		ShowWindow(homeCtl[i], home ? SW_SHOW : SW_HIDE);
		ShowWindow(tabs[i], home ? SW_HIDE : SW_SHOW);
		InvalidateRect(tabs[i], NULL, FALSE);
	}
	ShowWindow(g_btnLog, (home && (g_showLog || FileExists(g_buildLog) || FileExists(g_gameLog))) ? SW_SHOW : SW_HIDE);
	for (int i = 0; i < 5; i++)
	{
		ShowWindow(displayCtl[i], (!home && (g_tab == TAB_DISPLAY)) ? SW_SHOW : SW_HIDE);
		ShowWindow(controlsCtl[i], (!home && (g_tab == TAB_CONTROLS)) ? SW_SHOW : SW_HIDE);
	}
	for (int i = 0; i < 6; i++)
	{
		ShowWindow(graphicsCtl[i], (!home && (g_tab == TAB_GRAPHICS)) ? SW_SHOW : SW_HIDE);
	}
	SetWindowTextW(g_btnSettings, home ? L"Settings" : L"\x2190 Back");
	InvalidateRect(g_wnd, NULL, FALSE);
}

static void UpdateButtons(void)
{
	int busy = g_busy != 0;

	EnableWindow(g_btnBrowse, !busy);
	EnableWindow(g_btnRebuild, !busy && (g_srcRoot[0] != 0));
	SetWindowTextW(g_btnPlay, busy ? ((g_phase == PHASE_PLAYING) ? L"RACING" : L"CANCEL") : L"PLAY");
	EnableWindow(g_btnPlay, !(busy && (g_phase == PHASE_PLAYING || g_phase == PHASE_LAUNCH)));
	if (g_page == PAGE_HOME)
	{
		ShowWindow(g_btnLog, (g_showLog || FileExists(g_buildLog) || FileExists(g_gameLog)) ? SW_SHOW : SW_HIDE);
	}
	InvalidateRect(g_btnPlay, NULL, FALSE);
}

static void BrowseDisc(void)
{
	BROWSEINFOW bi;
	PIDLIST_ABSOLUTE pidl;
	wchar_t path[MAX_PATH];

	memset(&bi, 0, sizeof(bi));
	bi.hwndOwner = g_wnd;
	bi.lpszTitle = L"Pick the folder with your Crash Team Racing disc image (.bin/.cue), or the .bin file itself.";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_BROWSEINCLUDEFILES;
	pidl = SHBrowseForFolderW(&bi);
	if (pidl == NULL)
	{
		return;
	}
	if (SHGetPathFromIDListW(pidl, path))
	{
		ChooseDisc(path, 1);
		g_phase = PHASE_IDLE;
		Copy(g_status, 512, g_disc.state == 1 || g_disc.state == 2 ? L"Ready" : L"");
		Copy(g_detail, 512, L"");
		InvalidateRect(g_wnd, NULL, FALSE);
	}
	CoTaskMemFree(pidl);
}

static void BrowseCover(void)
{
	OPENFILENAMEW ofn;
	wchar_t path[PATH_CAP] = L"";

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_wnd;
	ofn.lpstrFilter = L"Pictures (*.png, *.jpg, *.bmp)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All files\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = PATH_CAP;
	ofn.lpstrTitle = L"Pick a cover picture";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
	if (GetOpenFileNameW(&ofn))
	{
		SetCover(path, 1);
	}
}

static void ShowLog(void)
{
	const wchar_t *log = g_buildLog;

	if (FileExists(g_gameLog))
	{
		WIN32_FILE_ATTRIBUTE_DATA a, b;
		if (!FileExists(g_buildLog) ||
			(GetFileAttributesExW(g_gameLog, GetFileExInfoStandard, &a) &&
				GetFileAttributesExW(g_buildLog, GetFileExInfoStandard, &b) &&
				(CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) > 0)))
		{
			log = g_gameLog;
		}
	}
	if (FileExists(log))
	{
		ShellExecuteW(g_wnd, L"open", log, NULL, NULL, SW_SHOWNORMAL);
	}
}

static void OnDrop(HDROP drop)
{
	wchar_t path[PATH_CAP];
	UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);

	for (UINT i = 0; i < count; i++)
	{
		if (DragQueryFileW(drop, i, path, PATH_CAP) == 0)
		{
			continue;
		}
		if (IsImage(path))
		{
			SetCover(path, 1);
		}
		else if (!g_busy)
		{
			ChooseDisc(path, 1);
			if (g_page != PAGE_HOME)
			{
				ShowPage(PAGE_HOME);
			}
			g_phase = PHASE_IDLE;
			Copy(g_status, 512, g_disc.state == 1 || g_disc.state == 2 ? L"Ready" : L"");
			Copy(g_detail, 512, L"");
		}
	}
	DragFinish(drop);
	InvalidateRect(g_wnd, NULL, FALSE);
}

static void OnCommand(int id, int code)
{
	switch (id)
	{
	case ID_PLAY:
		if (g_busy)
		{
			InterlockedExchange(&g_cancel, 1);
			if (g_job != NULL)
			{
				TerminateJobObject(g_job, 1);
			}
		}
		else
		{
			StartTask(TASK_PLAY);
		}
		break;
	case ID_REBUILD:
		StartTask(TASK_CLEAN);
		break;
	case ID_LOG:
		ShowLog();
		break;
	case ID_BROWSE:
		BrowseDisc();
		break;
	case ID_SETTINGS:
		ShowPage(g_page == PAGE_HOME ? PAGE_SETTINGS : PAGE_HOME);
		break;
	case ID_TAB_DISPLAY:
	case ID_TAB_GRAPHICS:
	case ID_TAB_CONTROLS:
		g_tab = id - ID_TAB_DISPLAY;
		ShowPage(PAGE_SETTINGS);
		break;
	case ID_RESOLUTION:
		if (code == CBN_SELCHANGE)
		{
			int sel = (int)SendMessageW(g_cmbRes, CB_GETCURSEL, 0, 0);
			if ((sel >= 0) && (sel < RES_COUNT))
			{
				g_set.width = s_resolutions[sel].w;
				g_set.height = s_resolutions[sel].h;
				SaveSettings();
			}
		}
		break;
	case ID_UPSCALE:
	case ID_SHARPNESS:
	case ID_AA:
	case ID_CRT:
	case ID_BRIGHTNESS:
		if (code == CBN_SELCHANGE)
		{
			int up = (int)SendMessageW(g_cmbUpscale, CB_GETCURSEL, 0, 0);
			int sharp = (int)SendMessageW(g_cmbSharp, CB_GETCURSEL, 0, 0);
			int aa = (int)SendMessageW(g_cmbAa, CB_GETCURSEL, 0, 0);
			int crt = (int)SendMessageW(g_cmbCrt, CB_GETCURSEL, 0, 0);
			int bright = (int)SendMessageW(g_cmbBright, CB_GETCURSEL, 0, 0);
			g_set.upscale = (up >= 0 && up < 3) ? up : 0;
			g_set.sharpness = (sharp >= 0 && sharp < 3) ? sharp : 1;
			g_set.aa = (aa >= 0 && aa < 2) ? aa : 0;
			g_set.crt = (crt >= 0 && crt < 3) ? crt : 0;
			g_set.brightness = (bright >= 0 && bright < 5) ? bright - 2 : 0;
			SyncChecks();
			SaveSettings();
			InvalidateRect(g_wnd, NULL, FALSE);
		}
		break;
	default:
		if (IsToggle((UINT)id) && (code == BN_CLICKED))
		{
			int *field = ToggleField((UINT)id);
			if (field != NULL)
			{
				*field = !*field;
			}
			if (!g_set.keyboard && !g_set.controller)
			{
				// Something has to drive the kart.
				g_set.keyboard = 1;
			}
			SyncChecks();
			SaveSettings();
		}
		break;
	}
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_CREATE:
		g_wnd = hwnd;
		CreateControls();
		DragAcceptFiles(hwnd, TRUE);
		ShowPage(PAGE_HOME);
		UpdateButtons();
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		PaintBuffered(dc);
		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_PRINTCLIENT:
		Paint((HDC)wp);
		return 0;

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
		SetBkColor((HDC)wp, COL_CARD);
		SetTextColor((HDC)wp, COL_TEXT);
		return (LRESULT)g_cardBrush;

	case WM_DRAWITEM:
		DrawButton((const DRAWITEMSTRUCT *)lp);
		return TRUE;

	case WM_COMMAND:
		OnCommand(LOWORD(wp), HIWORD(wp));
		return 0;

	case WM_DROPFILES:
		OnDrop((HDROP)wp);
		return 0;

	case WM_LBUTTONUP: {
		POINT pt;
		pt.x = (short)LOWORD(lp);
		pt.y = (short)HIWORD(lp);
		if (PtInRect(&g_rcCoverLink, pt) || PtInRect(&g_rcCover, pt))
		{
			BrowseCover();
		}
		else if ((g_page == PAGE_HOME) && PtInRect(&g_rcDrop, pt) && !g_busy)
		{
			BrowseDisc();
		}
		return 0;
	}

	case WM_SETCURSOR:
		if (LOWORD(lp) == HTCLIENT)
		{
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hwnd, &pt);
			if (PtInRect(&g_rcCoverLink, pt) || PtInRect(&g_rcCover, pt) ||
				((g_page == PAGE_HOME) && PtInRect(&g_rcDrop, pt) && !g_busy))
			{
				SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
				return TRUE;
			}
		}
		break;

	case WM_TIMER: {
		double target = TargetPct();
		if (target < g_shownPct)
		{
			target = g_shownPct;
		}
		g_shownPct += (target - g_shownPct) * 0.25;
		if (target - g_shownPct < 0.05)
		{
			g_shownPct = target;
		}
		{
			RECT rc;
			SetRect(&rc, S(RX), S(RP_T + 356), S(RR), S(RP_T + 486));
			InvalidateRect(hwnd, &rc, FALSE);
		}
		return 0;
	}

	case WM_APP_STATUS: {
		wchar_t *text = (wchar_t *)lp;
		if (text != NULL)
		{
			Copy(wp == 0 ? g_status : g_detail, 512, text);
			free(text);
		}
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}

	case WM_APP_STEP: {
		DWORD now = GetTickCount();
		int done = (int)wp;
		int total = (int)lp;
		if ((g_phase == PHASE_BUILD) && (done > g_stepDone) && (g_stepDone > 0))
		{
			double ms = (double)(now - g_stepTick) / (done - g_stepDone);
			g_avgStepMs = g_avgStepMs * 0.8 + ms * 0.2;
		}
		if ((done != g_stepDone) || (total != g_stepTotal))
		{
			g_stepTick = now;
		}
		g_stepDone = done;
		g_stepTotal = total;
		return 0;
	}

	case WM_APP_PHASE:
		g_phase = (int)wp;
		g_phaseTick = g_stepTick = GetTickCount();
		g_stepDone = 0;
		g_stepTotal = 0;
		if ((g_phase == PHASE_COPY) || (g_phase == PHASE_CONFIGURE))
		{
			g_shownPct = 0;
		}
		UpdateButtons();
		return 0;

	case WM_APP_MINIMIZE:
		ShowWindow(hwnd, SW_MINIMIZE);
		return 0;

	case WM_APP_RESTORE:
		ShowWindow(hwnd, SW_RESTORE);
		SetForegroundWindow(hwnd);
		return 0;

	case WM_APP_DONE:
		KillTimer(hwnd, 1);
		g_phase = wp ? PHASE_DONE : PHASE_FAILED;
		g_shownPct = wp ? 100.0 : g_shownPct;
		g_showLog = !wp;
		UpdateButtons();
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;

	case WM_CLOSE:
		if (g_busy && (g_phase != PHASE_PLAYING))
		{
			if (MessageBoxW(hwnd, L"The game is still being built. Stop and close?", APP_TITLE,
					MB_YESNO | MB_ICONQUESTION) != IDYES)
			{
				return 0;
			}
			InterlockedExchange(&g_cancel, 1);
			if (g_job != NULL)
			{
				TerminateJobObject(g_job, 1);
			}
		}
		DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	WNDCLASSEXW wc;
	RECT rc;
	MSG msg;
	HDC screen;
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
	RECT work;

	(void)prev;
	(void)cmd;
	g_inst = inst;

	SetProcessDPIAware();
	OleInitialize(NULL);
	Gdip_Init();

	screen = GetDC(NULL);
	g_scale = (float)GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
	ReleaseDC(NULL, screen);
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	while ((g_scale > 1.0f) && ((S(CLIENT_W) > work.right - work.left) || (S(CLIENT_H) + 40 > work.bottom - work.top)))
	{
		g_scale -= 0.125f;
	}

	g_fontTitle = MakeFont(L"Segoe UI Black", 26, FW_BLACK, 0);
	g_fontHeading = MakeFont(L"Segoe UI Black", 38, FW_BLACK, 0);
	g_fontBody = MakeFont(L"Segoe UI", 15, FW_NORMAL, 0);
	g_fontSmall = MakeFont(L"Segoe UI", 13, FW_NORMAL, 0);
	g_fontCaps = MakeFont(L"Segoe UI Black", 12, FW_BLACK, 0);
	g_fontButton = MakeFont(L"Segoe UI Semibold", 16, FW_BOLD, 0);
	g_fontBig = MakeFont(L"Segoe UI Black", 28, FW_BLACK, 0);
	g_fontCover = MakeFont(L"Segoe UI Black", 84, FW_BLACK, 0);
	g_cardBrush = CreateSolidBrush(COL_CARD);

	FindPaths();
	LoadSettings();
	LoadInitialCover();
	DetectPlacedDisc();
	if (g_disc.state == 1 || g_disc.state == 2)
	{
		Copy(g_status, 512, L"Ready");
		Copy(g_detail, 512, L"Press Play: the game is recompiled from source, then it starts.");
	}
	else
	{
		Copy(g_status, 512, L"Waiting for your disc");
	}

	g_job = CreateJobObjectW(NULL, NULL);
	if (g_job != NULL)
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
		memset(&limits, 0, sizeof(limits));
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
	}

	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
	wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
	if (wc.hIcon == NULL)
	{
		wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
	}
	wc.lpszClassName = L"CTRLauncher";
	RegisterClassExW(&wc);

	SetRect(&rc, 0, 0, S(CLIENT_W), S(CLIENT_H));
	AdjustWindowRect(&rc, style, FALSE);
	CreateWindowExW(0, wc.lpszClassName, APP_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
		rc.bottom - rc.top, NULL, NULL, inst, NULL);
	if (g_wnd == NULL)
	{
		return 1;
	}
	ShowWindow(g_wnd, show);
	UpdateWindow(g_wnd);

	while (GetMessageW(&msg, NULL, 0, 0) > 0)
	{
		if (!IsDialogMessageW(g_wnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	if (g_job != NULL)
	{
		CloseHandle(g_job); // kills a build still running
	}
	OleUninitialize();
	return 0;
}
