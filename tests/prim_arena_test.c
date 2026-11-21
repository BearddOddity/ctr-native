// Extended primitive memory (platform/native_memory.c): both draw buffers move into the MEMPACK window's free low
// bytes, retail's allocation stays where MEMPACK put it, nothing overlaps the MEMPACK pool, the GPU link bridge maps
// the whole buffer, the usage report counts frames past the PS1 size, and --ps1-prim-limit keeps retail's buffers.
#include "../platform/native_memory.c"

#include <stdarg.h>

// The game's resident data (game/zGlobal_SDATA.c in the game build): zeroed is enough here.
struct sData sdata_static;
static int s_logLines;
static char s_lastLog[512];

void Platform_Log(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(s_lastLog, sizeof(s_lastLog), fmt, args);
	va_end(args);
	s_logLines++;
}

static int s_failures;

#define EXPECT(cond)                                                                                                   \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(cond))                                                                                                   \
		{                                                                                                              \
			fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #cond);                                        \
			s_failures++;                                                                                              \
		}                                                                                                              \
	} while (0)

// What MainDB_PrimMem leaves before native rebinds it.
static void RetailPrimMem(struct PrimMem *primMem, u8 *allocation, u32 size)
{
	memset(primMem, 0, sizeof(*primMem));
	primMem->capacityBytes = size;
	primMem->allocationStart = allocation;
	primMem->cursor = allocation;
	primMem->start = allocation;
	primMem->end = allocation + ((size >> 2) << 2);
	primMem->guardEnd = (u8 *)primMem->end - 0x100;
}

int main(void)
{
	const struct PlatformMempackArena *arena = Platform_InitMempackArena();
	struct PrimMem *db0 = &sdata_static.gameTracker.db[0].primMem;
	struct PrimMem *db1 = &sdata_static.gameTracker.db[1].primMem;
	u8 *pool = (u8 *)arena->start;

	RetailPrimMem(db0, pool, 0x17c00);
	RetailPrimMem(db1, pool + 0x17c00, 0x17c00);
	NativePrimMem_Bind(db0);
	NativePrimMem_Bind(db1);

	// Moved below the pool, retail's block kept, the two buffers apart.
	EXPECT((u8 *)db0->start >= (u8 *)arena->base);
	EXPECT((u8 *)db1->end <= pool);
	EXPECT((u8 *)db0->end <= (u8 *)db1->start);
	EXPECT(db0->allocationStart == pool);
	EXPECT(db1->allocationStart == pool + 0x17c00);
	EXPECT(db0->capacityBytes == 0x17c00);
	EXPECT(db0->cursor == db0->start);
	EXPECT((u8 *)db0->guardEnd == (u8 *)db0->end - 0x100);
	EXPECT((u32)((u8 *)db0->end - (u8 *)db0->start) >= 2u * NATIVE_PRIM_RETAIL_MAX);
	EXPECT(NativePrimMem_LinkBytes(db0) == (u32)((u8 *)db0->end - (u8 *)db0->start));
	EXPECT(s_logLines == 1);

	// A buffer that is not a draw buffer keeps retail's bounds.
	struct PrimMem other;
	RetailPrimMem(&other, pool + 0x40000, 0x1000);
	NativePrimMem_Bind(&other);
	EXPECT(other.start == pool + 0x40000);
	EXPECT(NativePrimMem_LinkBytes(&other) == 0x1000);

	// One frame past the PS1 size, one within it, one at the end.
	db0->cursor = (u8 *)db0->start + 0x17b00 + 0x800;
	NativePrimMem_NoteFrame(db0);
	db1->cursor = (u8 *)db1->start + 0x1000;
	NativePrimMem_NoteFrame(db1);
	db0->cursor = (u8 *)db0->guardEnd - 0x10;
	NativePrimMem_NoteFrame(db0);
	EXPECT(s_primStats.frames == 3);
	EXPECT(s_primStats.overRetailFrames == 2);
	EXPECT(s_primStats.fullFrames == 1);
	EXPECT(strstr(s_lastLog, "within 1 KiB of its end") != NULL);
	NativePrimMem_Report();
	EXPECT(strstr(s_lastLog, "3 frames:") != NULL);
	EXPECT(strstr(s_lastLog, "2 frame(s) drew more than the PS1 buffer") != NULL);
	EXPECT(s_primStats.frames == 0);

	// --ps1-prim-limit: retail's buffers, and a full one is still reported.
	char arg0[] = "ctr_native";
	char arg1[] = "--ps1-prim-limit";
	char *argv[] = {arg0, arg1, NULL};
	NativePrimMem_ConfigureFromArgs(2, argv);
	RetailPrimMem(db0, pool, 0x17c00);
	NativePrimMem_Bind(db0);
	EXPECT(db0->start == pool);
	EXPECT(NativePrimMem_LinkBytes(db0) == 0x17c00);
	db0->cursor = (u8 *)db0->guardEnd - 0x40;
	NativePrimMem_NoteFrame(db0);
	EXPECT(s_primStats.fullFrames == 1);
	EXPECT(s_primStats.overRetailFrames == 0);

	if (s_failures != 0)
	{
		fprintf(stderr, "prim arena test: %d failure(s)\n", s_failures);
		return 1;
	}
	printf("prim arena test: passed\n");
	return 0;
}
