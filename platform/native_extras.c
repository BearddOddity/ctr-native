#include "platform/native_extras.h"

#include "platform/native_config.h"

#include <math.h>

// The game's own flag for each NATIVE_CHEAT_*, as its main-menu cheat codes set it (game/230/MM_CheatCodes.c).
static const u32 s_nativeCheatFlags[NATIVE_CHEAT_COUNT] = {
	CHEAT_WUMPA, CHEAT_MASK, CHEAT_TURBO, CHEAT_BOMBS, CHEAT_INVISIBLE, CHEAT_ENGINE,
	CHEAT_ICY, CHEAT_TURBOPAD, CHEAT_TURBOCOUNT, CHEAT_ONELAP, CHEAT_SUPERHARD, CHEAT_ADV,
};

// The cheats the game itself turns off for Adventure and Time Trial (game/230/MM_MenuFlow.c): kept off there.
#define NATIVE_EXTRAS_ITEM_CHEATS (CHEAT_WUMPA | CHEAT_TURBO | CHEAT_MASK | CHEAT_ENGINE | CHEAT_BOMBS)

int NativeExtras_SkipIntro(void)
{
	return g_nativeConfig.skipIntro != 0;
}

void NativeExtras_Frame(void)
{
	struct GameTracker *gGT = GAME_TRACKER;
	u32 flags = 0;

	if (gGT == NULL)
	{
		return;
	}

	if (g_nativeConfig.unlockAll)
	{
		// What the game's unlock codes give: the hidden characters, Turbo Track, the locked battle arenas and the
		// scrapbook. They are saved with the profile, as the codes' are.
		GAME_PROGRESS.unlocks[0] |= UNLOCK_CHARACTERS | GAME_UNLOCK_TRACKS_MASK;
		UNLOCK_ADV_BIT(GAME_PROGRESS.unlocks, GAME_UNLOCK_BIT_SCRAPBOOK);
	}

	if (g_nativeConfig.cheats == 0)
	{
		return;
	}
	for (int i = 0; i < NATIVE_CHEAT_COUNT; i++)
	{
		if (g_nativeConfig.cheats & (1 << i))
		{
			flags |= s_nativeCheatFlags[i];
		}
	}
	if ((gGT->gameMode1 & (ADVENTURE_MODE | TIME_TRIAL)) != 0)
	{
		flags &= ~(u32)NATIVE_EXTRAS_ITEM_CHEATS;
	}
	gGT->gameMode2 |= flags;
}

int NativeExtras_DistanceToScreen(int h)
{
	const int fov = g_nativeConfig.fov;

	if ((fov == NATIVE_CONFIG_FOV_DEFAULT) || (h <= 0))
	{
		return h;
	}
	// The PS1's view is 90 degrees wide: half its width over H is tan(45) = 1. H scales by 1 / tan(fov / 2).
	const double half = (double)fov * 0.5 * 3.14159265358979323846 / 180.0;
	const int scaled = (int)((double)h / tan(half) + 0.5);
	return scaled < 1 ? 1 : scaled;
}

int NativeExtras_CameraDistance(int distance)
{
	if (g_nativeConfig.cameraDistance == 100)
	{
		return distance;
	}
	return (distance * g_nativeConfig.cameraDistance) / 100;
}

extern s32 g_nativeGteWideX;

// 16.16: 4:3 over the shape the frame is shown in; 1.0 when that is no wider than 4:3 or widescreen stretches.
static s32 NativeExtras_WideScale(void)
{
	int w = 4;
	int h = 3;

	if (!g_nativeConfig.widescreen || !g_nativeConfig.trueWidescreen)
	{
		return 0x10000;
	}
	NativeRenderer_GetPresentAspect(&w, &h);
	if ((w <= 0) || (h <= 0) || (w * 3 <= h * 4))
	{
		return 0x10000;
	}
	return (s32)(((s64)4 * h << 16) / ((s64)3 * w));
}

static int NativeExtras_IsUiView(const struct PushBuffer *pb)
{
	struct GameTracker *gGT = GAME_TRACKER;

	return (pb == NULL) || ((gGT != NULL) && (pb == &gGT->pushBuffer_UI)) || (pb == &GAME_SDATA_NAME(pushBuffer_DecalMP));
}

void NativeExtras_WideForView(const struct PushBuffer *pb)
{
	g_nativeGteWideX = NativeExtras_IsUiView(pb) ? 0x10000 : NativeExtras_WideScale();
}

void NativeExtras_WideOff(void)
{
	g_nativeGteWideX = 0x10000;
}

int NativeExtras_FrustumWidth(const struct PushBuffer *pb, int width)
{
	const s32 scale = NativeExtras_IsUiView(pb) ? 0x10000 : NativeExtras_WideScale();

	return (int)(((s64)width << 16) / scale);
}
