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
