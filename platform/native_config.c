#include "platform/native_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// NOTE: kept free of SDL and game headers so tests can build it alone.

struct NativeConfig g_nativeConfig;

void NativeConfig_SetDefaults(struct NativeConfig *config)
{
#ifdef USE_16BY9
	config->width = 1280;
	config->height = 720;
	config->widescreen = 1;
#else
	config->width = 800;
	config->height = 600;
	config->widescreen = 0;
#endif
	config->fullscreen = 0;
	config->trueWidescreen = 1;
	config->keyboard = 1;
	config->controller = 1;
	config->xboxControllers = 1;
	config->playstationControllers = 1;
	config->vibration = 1;
	config->vsync = 0;
	config->upscale = NATIVE_UPSCALE_SHARP;
	config->sharpness = 1;
	config->antialias = NATIVE_AA_OFF;
	config->crt = NATIVE_CRT_OFF;
	config->textureFilter = 0;
	config->brightness = 0;
	config->integerScale = 0;
	config->dither = NATIVE_DITHER_PS1;
	config->renderScale = 1;
	config->skipIntro = 0;
	config->unlockAll = 0;
	config->cheats = 0;
	config->fov = NATIVE_CONFIG_FOV_DEFAULT;
	config->cameraDistance = 100;
}

const char *const g_nativeCheatKeys[NATIVE_CHEAT_COUNT] = {
	"cheat_wumpa", "cheat_masks", "cheat_turbos", "cheat_bombs", "cheat_invisible", "cheat_engine",
	"cheat_icy", "cheat_turbo_pads", "cheat_turbo_count", "cheat_one_lap", "cheat_super_hard", "cheat_hard_adventure",
};

static int NativeConfig_EqualsNoCase(const char *a, const char *b)
{
	while ((*a != '\0') && (*b != '\0'))
	{
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
		{
			return 0;
		}
		a++;
		b++;
	}
	return (*a == '\0') && (*b == '\0');
}

static int NativeConfig_ParseBool(const char *value, int *out)
{
	if (NativeConfig_EqualsNoCase(value, "1") || NativeConfig_EqualsNoCase(value, "true") ||
		NativeConfig_EqualsNoCase(value, "yes") || NativeConfig_EqualsNoCase(value, "on"))
	{
		*out = 1;
		return 1;
	}
	if (NativeConfig_EqualsNoCase(value, "0") || NativeConfig_EqualsNoCase(value, "false") ||
		NativeConfig_EqualsNoCase(value, "no") || NativeConfig_EqualsNoCase(value, "off"))
	{
		*out = 0;
		return 1;
	}
	return 0;
}

static int NativeConfig_ParseInt(const char *value, int *out)
{
	char *end = NULL;
	long parsed = strtol(value, &end, 10);

	if ((end == value) || (*end != '\0') || (parsed < 0) || (parsed > 100000))
	{
		return 0;
	}
	*out = (int)parsed;
	return 1;
}

// A word from a list (or its index as a number).
static int NativeConfig_ParseChoice(const char *value, const char *const *words, int count, int *out)
{
	int number;

	for (int i = 0; i < count; i++)
	{
		if (NativeConfig_EqualsNoCase(value, words[i]))
		{
			*out = i;
			return 1;
		}
	}
	if (NativeConfig_ParseInt(value, &number) && (number < count))
	{
		*out = number;
		return 1;
	}
	return 0;
}

static int NativeConfig_ParseSigned(const char *value, int *out)
{
	char *end = NULL;
	long parsed = strtol(value, &end, 10);

	if ((end == value) || (*end != '\0') || (parsed < -1000) || (parsed > 1000))
	{
		return 0;
	}
	*out = (int)parsed;
	return 1;
}

static const char *const s_upscaleWords[] = {"sharp", "smooth", "fsr"};
static const char *const s_aaWords[] = {"off", "fxaa"};
static const char *const s_crtWords[] = {"off", "scanlines", "curved"};
static const char *const s_sharpWords[] = {"low", "medium", "high"};
static const char *const s_ditherWords[] = {"off", "ps1", "fine"};

static int NativeConfig_ParseResolution(const char *value, int *width, int *height)
{
	int w = 0;
	int h = 0;
	char tail = 0;

	if (sscanf(value, "%d%*[xX]%d%c", &w, &h, &tail) != 2)
	{
		return 0;
	}
	if ((w <= 0) || (h <= 0))
	{
		return 0;
	}
	*width = w;
	*height = h;
	return 1;
}

static void NativeConfig_SetKey(struct NativeConfig *config, const char *key, const char *value)
{
	if (NativeConfig_EqualsNoCase(key, "width"))
	{
		NativeConfig_ParseInt(value, &config->width);
	}
	else if (NativeConfig_EqualsNoCase(key, "height"))
	{
		NativeConfig_ParseInt(value, &config->height);
	}
	else if (NativeConfig_EqualsNoCase(key, "resolution"))
	{
		NativeConfig_ParseResolution(value, &config->width, &config->height);
	}
	else if (NativeConfig_EqualsNoCase(key, "fullscreen"))
	{
		NativeConfig_ParseBool(value, &config->fullscreen);
	}
	else if (NativeConfig_EqualsNoCase(key, "widescreen"))
	{
		NativeConfig_ParseBool(value, &config->widescreen);
	}
	else if (NativeConfig_EqualsNoCase(key, "keyboard"))
	{
		NativeConfig_ParseBool(value, &config->keyboard);
	}
	else if (NativeConfig_EqualsNoCase(key, "controller"))
	{
		NativeConfig_ParseBool(value, &config->controller);
	}
	else if (NativeConfig_EqualsNoCase(key, "xbox"))
	{
		NativeConfig_ParseBool(value, &config->xboxControllers);
	}
	else if (NativeConfig_EqualsNoCase(key, "playstation"))
	{
		NativeConfig_ParseBool(value, &config->playstationControllers);
	}
	else if (NativeConfig_EqualsNoCase(key, "vibration"))
	{
		NativeConfig_ParseBool(value, &config->vibration);
	}
	else if (NativeConfig_EqualsNoCase(key, "vsync"))
	{
		NativeConfig_ParseBool(value, &config->vsync);
	}
	else if (NativeConfig_EqualsNoCase(key, "upscale"))
	{
		NativeConfig_ParseChoice(value, s_upscaleWords, 3, &config->upscale);
	}
	else if (NativeConfig_EqualsNoCase(key, "sharpness"))
	{
		NativeConfig_ParseChoice(value, s_sharpWords, 3, &config->sharpness);
	}
	else if (NativeConfig_EqualsNoCase(key, "antialiasing") || NativeConfig_EqualsNoCase(key, "aa"))
	{
		NativeConfig_ParseChoice(value, s_aaWords, 2, &config->antialias);
	}
	else if (NativeConfig_EqualsNoCase(key, "crt"))
	{
		NativeConfig_ParseChoice(value, s_crtWords, 3, &config->crt);
	}
	else if (NativeConfig_EqualsNoCase(key, "texture_filter"))
	{
		NativeConfig_ParseBool(value, &config->textureFilter);
	}
	else if (NativeConfig_EqualsNoCase(key, "brightness"))
	{
		NativeConfig_ParseSigned(value, &config->brightness);
	}
	else if (NativeConfig_EqualsNoCase(key, "integer_scale"))
	{
		NativeConfig_ParseBool(value, &config->integerScale);
	}
	else if (NativeConfig_EqualsNoCase(key, "render_scale"))
	{
		int number;
		if (NativeConfig_EqualsNoCase(value, "native"))
		{
			config->renderScale = NATIVE_CONFIG_RENDER_SCALE_NATIVE;
		}
		else if (NativeConfig_ParseInt(value, &number))
		{
			config->renderScale = number;
		}
	}
	else if (NativeConfig_EqualsNoCase(key, "dither"))
	{
		NativeConfig_ParseChoice(value, s_ditherWords, 3, &config->dither);
	}
	else if (NativeConfig_EqualsNoCase(key, "true_widescreen"))
	{
		NativeConfig_ParseBool(value, &config->trueWidescreen);
	}
	else if (NativeConfig_EqualsNoCase(key, "fov"))
	{
		NativeConfig_ParseInt(value, &config->fov);
	}
	else if (NativeConfig_EqualsNoCase(key, "camera_distance"))
	{
		NativeConfig_ParseInt(value, &config->cameraDistance);
	}
	else if (NativeConfig_EqualsNoCase(key, "skip_intro"))
	{
		NativeConfig_ParseBool(value, &config->skipIntro);
	}
	else if (NativeConfig_EqualsNoCase(key, "unlock_all"))
	{
		NativeConfig_ParseBool(value, &config->unlockAll);
	}
	else
	{
		for (int i = 0; i < NATIVE_CHEAT_COUNT; i++)
		{
			int on;
			if (NativeConfig_EqualsNoCase(key, g_nativeCheatKeys[i]) && NativeConfig_ParseBool(value, &on))
			{
				config->cheats = on ? (config->cheats | (1 << i)) : (config->cheats & ~(1 << i));
			}
		}
	}
	// Unknown keys (the launcher's own, newer settings) are left alone.
}

static char *NativeConfig_Trim(char *s)
{
	char *end;

	while ((*s != '\0') && isspace((unsigned char)*s))
	{
		s++;
	}
	end = s + strlen(s);
	while ((end > s) && isspace((unsigned char)end[-1]))
	{
		end--;
	}
	*end = '\0';
	return s;
}

int NativeConfig_ParseText(struct NativeConfig *config, const char *text)
{
	char line[512];
	const char *p = text;

	while (*p != '\0')
	{
		size_t len = 0;

		while ((p[len] != '\0') && (p[len] != '\n'))
		{
			len++;
		}
		if (len >= sizeof(line))
		{
			len = sizeof(line) - 1;
		}
		memcpy(line, p, len);
		line[len] = '\0';
		p += len;
		while ((*p != '\0') && (*p != '\n'))
		{
			p++;
		}
		if (*p == '\n')
		{
			p++;
		}

		char *s = NativeConfig_Trim(line);
		if ((*s == '\0') || (*s == ';') || (*s == '#') || (*s == '['))
		{
			continue;
		}

		char *eq = strchr(s, '=');
		if (eq == NULL)
		{
			continue;
		}
		*eq = '\0';
		// A ';' or '#' after a space ends the value: "crt = off ; scanlines | curved".
		for (char *c = eq + 1; *c != '\0'; c++)
		{
			if (((*c == ';') || (*c == '#')) && isspace((unsigned char)c[-1]))
			{
				*c = '\0';
				break;
			}
		}
		char *key = NativeConfig_Trim(s);
		char *value = NativeConfig_Trim(eq + 1);
		NativeConfig_SetKey(config, key, value);
	}

	NativeConfig_Clamp(config);
	return 1;
}

int NativeConfig_LoadFile(struct NativeConfig *config, const char *path)
{
	FILE *file;
	char text[8192];
	size_t size;

	if ((path == NULL) || (path[0] == '\0'))
	{
		return 0;
	}

	file = fopen(path, "rb");
	if (file == NULL)
	{
		return 0;
	}

	size = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[size] = '\0';
	return NativeConfig_ParseText(config, text);
}

const char *NativeConfig_PathFromArgs(int argc, char **argv)
{
	for (int i = 1; i + 1 < argc; i++)
	{
		if (strcmp(argv[i], "--config") == 0)
		{
			return argv[i + 1];
		}
	}
	return NULL;
}

void NativeConfig_ApplyArgs(struct NativeConfig *config, int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
	{
		const char *arg = argv[i];
		const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;

		if ((strcmp(arg, "--width") == 0) && (next != NULL))
		{
			NativeConfig_ParseInt(next, &config->width);
			i++;
		}
		else if ((strcmp(arg, "--height") == 0) && (next != NULL))
		{
			NativeConfig_ParseInt(next, &config->height);
			i++;
		}
		else if ((strcmp(arg, "--resolution") == 0) && (next != NULL))
		{
			NativeConfig_ParseResolution(next, &config->width, &config->height);
			i++;
		}
		else if (strcmp(arg, "--fullscreen") == 0)
		{
			config->fullscreen = 1;
		}
		else if (strcmp(arg, "--windowed") == 0)
		{
			config->fullscreen = 0;
		}
		else if (strcmp(arg, "--widescreen") == 0)
		{
			config->widescreen = 1;
		}
		else if (strcmp(arg, "--no-widescreen") == 0)
		{
			config->widescreen = 0;
		}
		else if (strcmp(arg, "--no-keyboard") == 0)
		{
			config->keyboard = 0;
		}
		else if (strcmp(arg, "--no-controller") == 0)
		{
			config->controller = 0;
		}
		else if (strcmp(arg, "--no-xbox") == 0)
		{
			config->xboxControllers = 0;
		}
		else if (strcmp(arg, "--no-playstation") == 0)
		{
			config->playstationControllers = 0;
		}
		else if (strcmp(arg, "--vsync") == 0)
		{
			config->vsync = 1;
		}
		else if (strcmp(arg, "--no-vsync") == 0)
		{
			config->vsync = 0;
		}
		else if ((strcmp(arg, "--upscale") == 0) && (next != NULL))
		{
			NativeConfig_ParseChoice(next, s_upscaleWords, 3, &config->upscale);
			i++;
		}
		else if ((strcmp(arg, "--aa") == 0) && (next != NULL))
		{
			NativeConfig_ParseChoice(next, s_aaWords, 2, &config->antialias);
			i++;
		}
		else if ((strcmp(arg, "--crt") == 0) && (next != NULL))
		{
			NativeConfig_ParseChoice(next, s_crtWords, 3, &config->crt);
			i++;
		}
		else if (strcmp(arg, "--stretch") == 0)
		{
			config->trueWidescreen = 0;
		}
		else if ((strcmp(arg, "--fov") == 0) && (next != NULL))
		{
			NativeConfig_ParseInt(next, &config->fov);
			i++;
		}
		else if (strcmp(arg, "--skip-intro") == 0)
		{
			config->skipIntro = 1;
		}
		else if ((strcmp(arg, "--no-extras") == 0) || (strcmp(arg, "--record") == 0) || (strcmp(arg, "--replay") == 0))
		{
			// A replay plays back the inputs alone: the game must play as the disc does, on both sides.
			config->skipIntro = 0;
			config->unlockAll = 0;
			config->cheats = 0;
			config->fov = NATIVE_CONFIG_FOV_DEFAULT;
			config->cameraDistance = 100;
			config->trueWidescreen = 0;
		}
		else if ((strcmp(arg, "--render-scale") == 0) && (next != NULL))
		{
			int number;
			if (NativeConfig_ParseInt(next, &number))
			{
				config->renderScale = number;
			}
			i++;
		}
	}

	NativeConfig_Clamp(config);
}

void NativeConfig_ApplyEnv(struct NativeConfig *config)
{
	// The dev launcher in Revenant-SDK (games/ctr-crash-team-racing/launcher) sets these.
	const char *value;
	int number;

	if (((value = getenv("CTR_NATIVE_WIDTH")) != NULL) && NativeConfig_ParseInt(value, &number))
	{
		config->width = number;
	}
	if (((value = getenv("CTR_NATIVE_HEIGHT")) != NULL) && NativeConfig_ParseInt(value, &number))
	{
		config->height = number;
	}
	if ((value = getenv("CTR_NATIVE_FULLSCREEN")) != NULL)
	{
		NativeConfig_ParseBool(value, &config->fullscreen);
	}
	if ((value = getenv("CTR_NATIVE_ASPECT")) != NULL)
	{
		if (strcmp(value, "4:3") == 0)
		{
			config->widescreen = 0;
		}
		else if ((strcmp(value, "16:9") == 0) || (strcmp(value, "16:10") == 0))
		{
			config->widescreen = 1;
		}
	}
	if ((value = getenv("CTR_NATIVE_KEYBOARD")) != NULL)
	{
		NativeConfig_ParseBool(value, &config->keyboard);
	}
	if ((value = getenv("CTR_NATIVE_GAMEPAD")) != NULL)
	{
		NativeConfig_ParseBool(value, &config->controller);
	}
	if (((value = getenv("CTR_NATIVE_RENDER_SCALE")) != NULL) && NativeConfig_ParseInt(value, &number))
	{
		config->renderScale = number;
	}
	NativeConfig_Clamp(config);
}

void NativeConfig_Clamp(struct NativeConfig *config)
{
	if (config->width < NATIVE_CONFIG_MIN_WIDTH)
	{
		config->width = NATIVE_CONFIG_MIN_WIDTH;
	}
	if (config->width > NATIVE_CONFIG_MAX_WIDTH)
	{
		config->width = NATIVE_CONFIG_MAX_WIDTH;
	}
	if (config->height < NATIVE_CONFIG_MIN_HEIGHT)
	{
		config->height = NATIVE_CONFIG_MIN_HEIGHT;
	}
	if (config->height > NATIVE_CONFIG_MAX_HEIGHT)
	{
		config->height = NATIVE_CONFIG_MAX_HEIGHT;
	}
	if (config->brightness < -2)
	{
		config->brightness = -2;
	}
	if (config->brightness > 2)
	{
		config->brightness = 2;
	}
	if ((config->fov < NATIVE_CONFIG_FOV_MIN) || (config->fov > NATIVE_CONFIG_FOV_MAX))
	{
		config->fov = (config->fov < NATIVE_CONFIG_FOV_MIN) ? NATIVE_CONFIG_FOV_MIN : NATIVE_CONFIG_FOV_MAX;
	}
	if (config->cameraDistance < NATIVE_CONFIG_CAMERA_DISTANCE_MIN)
	{
		config->cameraDistance = NATIVE_CONFIG_CAMERA_DISTANCE_MIN;
	}
	if (config->cameraDistance > NATIVE_CONFIG_CAMERA_DISTANCE_MAX)
	{
		config->cameraDistance = NATIVE_CONFIG_CAMERA_DISTANCE_MAX;
	}
	if (config->renderScale < 0)
	{
		config->renderScale = 1;
	}
	if (config->renderScale > NATIVE_CONFIG_MAX_RENDER_SCALE)
	{
		config->renderScale = NATIVE_CONFIG_MAX_RENDER_SCALE;
	}
}
