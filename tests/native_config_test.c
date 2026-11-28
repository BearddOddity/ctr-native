#define _POSIX_C_SOURCE 200809L
// ctr_native.ini parsing: what the CTR Launcher writes is what the game reads.
#include "platform/native_config.h"

#include <stdio.h>
#include <stdlib.h>

static int s_failures;

#define EXPECT(cond)                                                    \
	do                                                                  \
	{                                                                   \
		if (!(cond))                                                    \
		{                                                               \
			fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #cond); \
			s_failures++;                                               \
		}                                                               \
	} while (0)

int main(void)
{
	struct NativeConfig c;

	// The launcher's file (WritePrivateProfileString layout, CRLF).
	NativeConfig_SetDefaults(&c);
	NativeConfig_ParseText(&c,
		"[video]\r\nwidth=2560\r\nheight=1440\r\nfullscreen=1\r\nwidescreen=0\r\n"
		"[input]\r\nkeyboard=0\r\ncontroller=1\r\nxbox=0\r\nplaystation=1\r\n"
		"[launcher]\r\nbuild_stamp=whatever\r\n");
	EXPECT(c.width == 2560);
	EXPECT(c.height == 1440);
	EXPECT(c.fullscreen == 1);
	EXPECT(c.widescreen == 0);
	EXPECT(c.keyboard == 0);
	EXPECT(c.controller == 1);
	EXPECT(c.xboxControllers == 0);
	EXPECT(c.playstationControllers == 1);

	// Hand edits: spaces, comments, words, a resolution key, junk values kept out.
	NativeConfig_SetDefaults(&c);
	NativeConfig_ParseText(&c,
		"; comment\n# another\n  resolution = 1920x1080  \nFullscreen = yes\nwidescreen = on\nwidth = lots\n"
		"keyboard = maybe\n");
	EXPECT(c.width == 1920);
	EXPECT(c.height == 1080);
	EXPECT(c.fullscreen == 1);
	EXPECT(c.widescreen == 1);
	EXPECT(c.keyboard == 1);

	// Graphics settings, as words or numbers.
	NativeConfig_SetDefaults(&c);
	EXPECT(c.upscale == NATIVE_UPSCALE_SHARP && c.antialias == NATIVE_AA_OFF && c.crt == NATIVE_CRT_OFF);
	NativeConfig_ParseText(&c,
		"[graphics]\nupscale=fsr\nsharpness=high\nantialiasing=fxaa\ncrt=curved\nvsync=1\n"
		"texture_filter=1\nbrightness=-1\ninteger_scale=1\n[input]\nvibration=0\n");
	EXPECT(c.upscale == NATIVE_UPSCALE_FSR);
	EXPECT(c.sharpness == 2);
	EXPECT(c.antialias == NATIVE_AA_FXAA);
	EXPECT(c.crt == NATIVE_CRT_CURVED);
	EXPECT(c.vsync == 1);
	EXPECT(c.textureFilter == 1);
	EXPECT(c.brightness == -1);
	EXPECT(c.integerScale == 1);
	EXPECT(c.vibration == 0);
	NativeConfig_ParseText(&c, "upscale=1\ncrt=bogus\nbrightness=9\n");
	EXPECT(c.upscale == NATIVE_UPSCALE_SMOOTH);
	EXPECT(c.crt == NATIVE_CRT_CURVED);
	EXPECT(c.brightness == 2);

	// A comment after a value, as the README shows them.
	NativeConfig_SetDefaults(&c);
	NativeConfig_ParseText(&c, "widescreen = 1        ; 0 = 4:3 pillarboxed\nupscale = fsr # sharp | smooth\n"
		"crt = scanlines;curved\n");
	EXPECT(c.widescreen == 1);
	EXPECT(c.upscale == NATIVE_UPSCALE_FSR);
	EXPECT(c.crt == NATIVE_CRT_OFF);

	// Clamped to sane window sizes.
	NativeConfig_SetDefaults(&c);
	NativeConfig_ParseText(&c, "width=10\nheight=99999\n");
	EXPECT(c.width == NATIVE_CONFIG_MIN_WIDTH);
	EXPECT(c.height == NATIVE_CONFIG_MAX_HEIGHT);

	// Command line wins over the file.
	{
		char *argv[] = {"ctr_native", "--config", "x.ini", "--resolution", "1280x800", "--windowed", "--no-widescreen",
			"--no-xbox", "--no-keyboard", NULL};
		NativeConfig_SetDefaults(&c);
		c.fullscreen = 1;
		NativeConfig_ApplyArgs(&c, 9, argv);
		EXPECT(c.width == 1280);
		EXPECT(c.height == 800);
		EXPECT(c.fullscreen == 0);
		EXPECT(c.widescreen == 0);
		EXPECT(c.xboxControllers == 0);
		EXPECT(c.keyboard == 0);
		EXPECT(NativeConfig_PathFromArgs(9, argv) != NULL);
	}

	// The dev launcher's environment variables.
	{
		NativeConfig_SetDefaults(&c);
#if defined(_WIN32)
		_putenv("CTR_NATIVE_WIDTH=1600");
		_putenv("CTR_NATIVE_HEIGHT=900");
		_putenv("CTR_NATIVE_ASPECT=4:3");
		_putenv("CTR_NATIVE_GAMEPAD=0");
#else
		setenv("CTR_NATIVE_WIDTH", "1600", 1);
		setenv("CTR_NATIVE_HEIGHT", "900", 1);
		setenv("CTR_NATIVE_ASPECT", "4:3", 1);
		setenv("CTR_NATIVE_GAMEPAD", "0", 1);
#endif
		NativeConfig_ApplyEnv(&c);
		EXPECT(c.width == 1600);
		EXPECT(c.height == 900);
		EXPECT(c.widescreen == 0);
		EXPECT(c.controller == 0);
	}

	// Extras: off by default; the launcher's [extras] keys turn them on; a replay turns them all off again.
	{
		char *argv[] = {"ctr_native", "--replay", "x.ctrreplay"};
		NativeConfig_SetDefaults(&c);
		EXPECT(c.skipIntro == 0 && c.unlockAll == 0 && c.cheats == 0);
		NativeConfig_ParseText(&c, "[extras]\nskip_intro=1\nunlock_all=yes\ncheat_wumpa=1\ncheat_one_lap=1\n"
			"cheat_bombs=0\ncheat_icy=maybe\n");
		EXPECT(c.skipIntro == 1);
		EXPECT(c.unlockAll == 1);
		EXPECT(c.cheats == ((1 << NATIVE_CHEAT_WUMPA) | (1 << NATIVE_CHEAT_ONE_LAP)));
		NativeConfig_ApplyArgs(&c, 3, argv);
		EXPECT(c.skipIntro == 0 && c.unlockAll == 0 && c.cheats == 0);
	}

	// Camera: the PS1's 90 degrees and 100% by default, hand edits kept in range.
	NativeConfig_SetDefaults(&c);
	EXPECT(c.fov == 90 && c.cameraDistance == 100);
	NativeConfig_ParseText(&c, "[camera]\nfov=105\ncamera_distance=150\n");
	EXPECT(c.fov == 105 && c.cameraDistance == 150);
	NativeConfig_ParseText(&c, "fov=400\ncamera_distance=1\n");
	EXPECT(c.fov == 120 && c.cameraDistance == 50);

	// True widescreen: on by default, stretch by hand.
	NativeConfig_SetDefaults(&c);
	EXPECT(c.trueWidescreen == 1);
	NativeConfig_ParseText(&c, "true_widescreen=0\n");
	EXPECT(c.trueWidescreen == 0);

	// Dithering: the PS1's by default.
	NativeConfig_SetDefaults(&c);
	EXPECT(c.dither == NATIVE_DITHER_PS1);
	NativeConfig_ParseText(&c, "dither=off\n");
	EXPECT(c.dither == NATIVE_DITHER_OFF);
	NativeConfig_ParseText(&c, "dither=fine\n");
	EXPECT(c.dither == NATIVE_DITHER_FINE);

	// Native resolution.
	NativeConfig_SetDefaults(&c);
	NativeConfig_ParseText(&c, "render_scale=native\n");
	EXPECT(c.renderScale == NATIVE_CONFIG_RENDER_SCALE_NATIVE);
	NativeConfig_ParseText(&c, "render_scale=0\n");
	EXPECT(c.renderScale == 0);

	// A missing file leaves the defaults.
	NativeConfig_SetDefaults(&c);
	EXPECT(NativeConfig_LoadFile(&c, "this-file-does-not-exist.ini") == 0);
	EXPECT(c.keyboard == 1 && c.controller == 1);

	if (s_failures == 0)
	{
		printf("native_config: ok\n");
	}
	return s_failures != 0;
}
