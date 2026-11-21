#include <platform.h>
#include "ctr_scratchpad.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#if defined(CTR_INTERNAL)
#include "platform/native_checkpoint.h"
#endif

#include <common.h>
#include <macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Native uses the NTSC-U 926 mempack window inside the retail 2 MiB address
// space so memory regressions fail here as they would on PSX.
#define CTR_NATIVE_MEMPACK_BUFFER_SIZE  0x200000u
#define CTR_NATIVE_MEMPACK_START_OFFSET 0xba9f0u
#define CTR_NATIVE_MEMPACK_SIZE         0x144e10u

CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_START_OFFSET + CTR_NATIVE_MEMPACK_SIZE + MEMPACK_PS1_END_GUARD_SIZE == CTR_NATIVE_MEMPACK_BUFFER_SIZE);

union NativeScratchpadStorage
{
	u8 bytes[CTR_SCRATCHPAD_SIZE];
	u32 words[CTR_SCRATCHPAD_SIZE / sizeof(u32)];
};

CTR_STATIC_ASSERT(sizeof(union NativeScratchpadStorage) == CTR_SCRATCHPAD_SIZE);

global_variable char s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
global_variable struct PlatformMempackArena s_mempackArena;
global_variable union NativeScratchpadStorage s_scratchpadMemory;
u8 *gCTRNativeScratchpadBase;

void Platform_InitScratchpad(void)
{
#if defined(CTR_NATIVE)
	gCTRNativeScratchpadBase = &s_scratchpadMemory.bytes[0];
	memset(&s_scratchpadMemory, 0, sizeof(s_scratchpadMemory));
#endif
}

void Platform_ConfigureMempackArena(void)
{
	s_mempackArena.base = &s_mempackMemory[0];
	s_mempackArena.start = &s_mempackMemory[CTR_NATIVE_MEMPACK_START_OFFSET];
	s_mempackArena.endOfMemory = &s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
	s_mempackArena.size = CTR_NATIVE_MEMPACK_SIZE;
	s_mempackArena.backingSize = CTR_NATIVE_MEMPACK_BUFFER_SIZE;
}

const struct PlatformMempackArena *Platform_InitMempackArena(void)
{
	memset(s_mempackMemory, 0, sizeof(s_mempackMemory));
	Platform_ConfigureMempackArena();
#if defined(CTR_INTERNAL)
	NativeCheckpoint_OnMempackArenaReset();
#endif

	return &s_mempackArena;
}

const struct PlatformMempackArena *Platform_GetMempackArena(void)
{
	return &s_mempackArena;
}

void *Platform_GetMempackBacking(void)
{
	return &s_mempackMemory[0];
}

int Platform_GetMempackBackingSize(void)
{
	return (int)sizeof(s_mempackMemory);
}

void Platform_RepairResidentPointers(s32 activeMempackIndex)
{
	if ((activeMempackIndex < 0) || (activeMempackIndex >= 4))
	{
		activeMempackIndex = 0;
	}

	// NOTE(aalhendi): Native keeps retail-shaped global data, but pointer aliases
	// must target this process's static storage. This also moves GCC's
	// initializer-only memcard helper global out of the live state graph so
	// checkpoints capture the actual memcard buffer.
	sdata = &sdata_static;
	sdata_static.gGT = &sdata_static.gameTracker;
	sdata_static.gGamepads = &sdata_static.gamepadSystem;
	sdata_static.PtrMempack = &sdata_static.mempack[activeMempackIndex];
	sdata_static.ptrToMemcardBuffer1 = &sdata_static.memcardBytes[0];
	sdata_static.ptrToMemcardBuffer2 = &sdata_static.memcardBytes[0];
}

// Extended primitive memory. Retail gives each draw buffer a primitive arena sized per level (MainInit_PrimMem:
// 95 to 150 KiB) and drops whatever no longer fits once the cursor reaches guardEnd. Native keeps that MEMPACK
// allocation, so every later allocation lands where retail puts it and the pool layout stays retail's, but points
// both draw buffers at the window's low bytes: on a PS1 the exe sits below the MEMPACK start, here nothing does.
// The window is one checkpoint region, so the prims are saved with it and their pointers relocate with it.
// `--ps1-prim-limit` keeps retail's buffers. NativePrimMem_Report says how much a session used.
#define NATIVE_PRIM_ARENA_OFFSET 0x1000u
#define NATIVE_PRIM_ARENA_BYTES  (((CTR_NATIVE_MEMPACK_START_OFFSET - NATIVE_PRIM_ARENA_OFFSET) / 2u) & ~0xffu)
#define NATIVE_PRIM_GUARD_BYTES  0x100u
#define NATIVE_PRIM_FULL_SLACK   0x400u
#define NATIVE_PRIM_RETAIL_MAX   0x25800u

CTR_STATIC_ASSERT(NATIVE_PRIM_ARENA_OFFSET + 2u * NATIVE_PRIM_ARENA_BYTES <= CTR_NATIVE_MEMPACK_START_OFFSET);
CTR_STATIC_ASSERT(NATIVE_PRIM_ARENA_BYTES >= 2u * NATIVE_PRIM_RETAIL_MAX);

struct NativePrimStats
{
	u32 frames;
	u32 peakBytes;
	u32 peakRetailBytes;
	s32 peakLevel;
	u32 overRetailFrames;
	u32 fullFrames;
	u32 worstOverRetailBytes;
	s32 announced;
};

global_variable s32 s_primRetailLimit;
global_variable struct NativePrimStats s_primStats;

internal u8 *NativePrimMem_Arena(s32 index)
{
	return (u8 *)&s_mempackMemory[NATIVE_PRIM_ARENA_OFFSET + (u32)index * NATIVE_PRIM_ARENA_BYTES];
}

internal s32 NativePrimMem_Index(const struct PrimMem *primMem)
{
	for (s32 i = 0; i < 2; i++)
	{
		if (primMem == &sdata_static.gameTracker.db[i].primMem)
		{
			return i;
		}
	}
	return -1;
}

internal b32 NativePrimMem_IsExtended(const struct PrimMem *primMem, s32 index)
{
	return (index >= 0) && (primMem->start == NativePrimMem_Arena(index));
}

// What retail's buffer of this size holds before its draws stop.
internal u32 NativePrimMem_RetailUsable(u32 capacityBytes)
{
	const u32 aligned = (capacityBytes >> 2) << 2;
	return (aligned > NATIVE_PRIM_GUARD_BYTES) ? aligned - NATIVE_PRIM_GUARD_BYTES : 0;
}

void NativePrimMem_ConfigureFromArgs(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
	{
		if ((argv[i] != NULL) && (strcmp(argv[i], "--ps1-prim-limit") == 0))
		{
			s_primRetailLimit = 1;
		}
	}
}

// Called by MainDB_PrimMem once retail's allocation is in place.
void NativePrimMem_Bind(struct PrimMem *primMem)
{
	const s32 index = NativePrimMem_Index(primMem);

	if (s_primRetailLimit || (index < 0) || (primMem->capacityBytes == 0))
	{
		return;
	}

	u8 *arena = NativePrimMem_Arena(index);
	primMem->start = arena;
	primMem->cursor = arena;
	primMem->end = arena + NATIVE_PRIM_ARENA_BYTES;
	primMem->guardEnd = arena + NATIVE_PRIM_ARENA_BYTES - NATIVE_PRIM_GUARD_BYTES;
	// allocationStart keeps MEMPACK's block: unused here, but the allocator's layout is retail's.

	if (!s_primStats.announced)
	{
		s_primStats.announced = 1;
		Platform_Log("[CTR Prim] primitive memory: %u KiB per draw buffer (PS1: %u KiB here, at most %u KiB)\n",
		             NATIVE_PRIM_ARENA_BYTES >> 10, primMem->capacityBytes >> 10, NATIVE_PRIM_RETAIL_MAX >> 10);
	}
}

// The bytes the GPU link bridge maps for this buffer.
u32 NativePrimMem_LinkBytes(const struct PrimMem *primMem)
{
	return NativePrimMem_IsExtended(primMem, NativePrimMem_Index(primMem)) ? NATIVE_PRIM_ARENA_BYTES : primMem->capacityBytes;
}

// Called by MainFrame_ResetDB for the buffer it is about to reuse: its cursor still holds what that frame drew.
void NativePrimMem_NoteFrame(const struct PrimMem *primMem)
{
	const u8 *start = (const u8 *)primMem->start;
	const u8 *cursor = (const u8 *)primMem->cursor;
	const u8 *guard = (const u8 *)primMem->guardEnd;

	if ((start == NULL) || (cursor <= start) || (guard <= start) || (primMem->capacityBytes == 0))
	{
		return;
	}

	const u32 used = (u32)(cursor - start);
	const u32 retailUsable = NativePrimMem_RetailUsable(primMem->capacityBytes);

	s_primStats.frames++;
	if (used > s_primStats.peakBytes)
	{
		s_primStats.peakBytes = used;
		s_primStats.peakRetailBytes = retailUsable;
		s_primStats.peakLevel = sdata_static.gameTracker.levelID;
	}
	if (NativePrimMem_IsExtended(primMem, NativePrimMem_Index(primMem)) && (used >= retailUsable))
	{
		s_primStats.overRetailFrames++;
		if (used - retailUsable > s_primStats.worstOverRetailBytes)
		{
			s_primStats.worstOverRetailBytes = used - retailUsable;
		}
	}
	if (used + NATIVE_PRIM_FULL_SLACK >= (u32)(guard - start))
	{
		if (s_primStats.fullFrames == 0)
		{
			Platform_Log("[CTR Prim] a draw buffer came within 1 KiB of its end (%u KiB used, level %d): draws past it are dropped\n",
			             used >> 10, sdata_static.gameTracker.levelID);
		}
		s_primStats.fullFrames++;
	}
}

void NativePrimMem_Report(void)
{
	if (s_primStats.frames == 0)
	{
		return;
	}

	Platform_Log("[CTR Prim] %u frames%s: peak %.1f KiB (level %d, where the PS1 buffer holds %.1f KiB); %u frame(s) drew more than the "
	             "PS1 buffer would hold (up to %.1f KiB more), %u came within 1 KiB of the end\n",
	             s_primStats.frames, s_primRetailLimit ? " at the PS1 limit (--ps1-prim-limit)" : "",
	             (f64)s_primStats.peakBytes / 1024.0, s_primStats.peakLevel, (f64)s_primStats.peakRetailBytes / 1024.0,
	             s_primStats.overRetailFrames, (f64)s_primStats.worstOverRetailBytes / 1024.0, s_primStats.fullFrames);
	memset(&s_primStats, 0, sizeof(s_primStats));
}
