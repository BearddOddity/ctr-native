#include <common.h>
#include <psn00bsdk/include/sys/fcntl.h>

// NOTE: Matching units name the card's resident storage by retail's own addressing
// (tools/matching/namespaces/memcard-card/retail_symbols.h); any other build uses the live sdata fields.
#ifndef MEMCARD_FILEIO_STAGE
#define MEMCARD_FILEIO_STAGE   sdata->memcard_stage
#define MEMCARD_FILEIO_BUFFER  sdata->memcard_ptrStart
#define MEMCARD_FILEIO_FD      sdata->memcard_fd
#define MEMCARD_FILEIO_RETRIES sdata->memcard_remainingAttempts
#define MEMCARD_FILEIO_SIZE    sdata->memcardFileSize
#define MEMCARD_FILEIO_SLOT    sdata->memcardSlot
#define MEMCARD_FILEIO_PATH    sdata->s_memcardFileCurr
#endif
#ifndef MEMCARD_CARD_PATH
#define MEMCARD_CARD_FREE_BYTES sdata->memoryCard_SizeRemaining
#define MEMCARD_CARD_ANY_FILE   sdata->s_AnyFile
#define MEMCARD_CARD_PATH       sdata->s_memcardFileCurr
#define MEMCARD_CARD_GHOST_FILE sdata->s_memcardFindGhostFile
#endif

// NOTE: Retail counts every file's 8 KB blocks against the 0x1e000 bytes of a card, so "Out of room" can come
// before the card is full; kept for byte-exact matching. The matching unit names the path and the wildcard by their
// absolute addresses (memcard-card/retail_symbols.h), as retail reaches them.
void MEMCARD_GetFreeBytes(int slotIdx)
{
	struct DIRENTRY entry;
	int used;

	MEMCARD_StringSet(MEMCARD_CARD_PATH, slotIdx, MEMCARD_CARD_ANY_FILE);
	used = 0;
	if (firstfile(MEMCARD_CARD_PATH, &entry) == &entry)
	{
		do
		{
			used += (entry.size + 0x1fff) & ~0x1fff;
		} while (nextfile(&entry) == &entry);
	}
	MEMCARD_CARD_FREE_BYTES = 0x1e000 - used;
}

// NOTE: The matching unit reads the card stage as a $gp word of its own (retail_symbols.h), so the scheduler may
// load it before the stack prologue, as retail does.
s32 MEMCARD_GetInfo(int slotIdx)
{
	if (MEMCARD_FILEIO_STAGE != MC_STAGE_IDLE)
		return MC_RETURN_TIMEOUT;

	MEMCARD_SkipEvents();
	MEMCARD_FILEIO_SLOT = slotIdx;
	MEMCARD_FILEIO_STAGE = MC_STAGE_GETINFO;
	while (_card_info(slotIdx) != 1)
	{
	}
	MEMCARD_FILEIO_RETRIES = 8;
	return MC_RETURN_PENDING;
}
// NOTE: Retail checks the card stage before the stack prologue and requires format() to return exactly 1. The
// early return and the stage, slot and retry count as their own $gp words (memcard-card namespace) keep GCC 2.8.1
// from moving the load and the delay-slot store.
s32 MEMCARD_Format(int slotIdx)
{
	if (MEMCARD_FILEIO_STAGE != MC_STAGE_IDLE)
		return MC_RETURN_TIMEOUT;

	if (format(MEMCARD_StringInit(slotIdx, 0)) == 1)
	{
		MEMCARD_FILEIO_SLOT = slotIdx;
		MEMCARD_FILEIO_STAGE = MC_STAGE_NEWCARD;
		MEMCARD_SkipEvents();
		while (_card_load(MEMCARD_FILEIO_SLOT) != 1)
		{
		}
		MEMCARD_FILEIO_RETRIES = 8;
		return MC_RETURN_PENDING;
	}
	return MC_RETURN_TIMEOUT;
}

int MEMCARD_IsFile(int slotIdx, char *save_name)
{
	char name[64];
	int fd;
	register int invalidFd CTR_PSX_REGISTER("$16");

	MEMCARD_StringSet(name, slotIdx, save_name);

	fd = open(name, FASYNC | FWRITE);
	// NOTE(aalhendi): Retail loads -1, stores the descriptor, then branches on the result.
	CTR_PSX_LOAD_IMMEDIATE(invalidFd, -1);
	sdata->memcard_fd = fd;
	CTR_PSX_MEMORY_BARRIER();

	if (fd == invalidFd)
		return MC_RETURN_NODATA;

	close(fd);
	sdata->memcard_fd = invalidFd;
	return MC_RETURN_IOE;
}

// NOTE(aalhendi): Retail accepts firstfile() only when it returns the supplied
// DIRENTRY buffer, then stores the ghost filename at its absolute address.
char *MEMCARD_FindFirstGhost(int slotIdx, char *srcString)
{
	struct DIRENTRY entry;
	struct DIRENTRY *found;

	if (MEMCARD_FILEIO_STAGE != MC_STAGE_IDLE)
		return NULL;

	MEMCARD_StringSet(MEMCARD_CARD_PATH, slotIdx, srcString);
	found = firstfile(MEMCARD_CARD_PATH, &entry);
	if (found != &entry)
		return NULL;

	MEMCARD_FILEIO_STAGE = MC_STAGE_GHOST_FOUND;
	strcpy(MEMCARD_CARD_GHOST_FILE, found->name);
	return MEMCARD_CARD_GHOST_FILE;
}

// NOTE(aalhendi): nextfile() uses the same returned-buffer contract; a
// different pointer ends ghost enumeration and restores the idle stage.
char *MEMCARD_FindNextGhost(void)
{
	struct DIRENTRY entry;
	struct DIRENTRY *found;

	if (MEMCARD_FILEIO_STAGE != MC_STAGE_GHOST_FOUND)
		return NULL;

	found = nextfile(&entry);
	if (found != &entry)
	{
		MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
		return NULL;
	}

	strcpy(MEMCARD_CARD_GHOST_FILE, found->name);
	return MEMCARD_CARD_GHOST_FILE;
}

// NOTE(aalhendi): Erase enters the pass stage before the SDK call and changes
// to the fail stage unless erase() returns exactly 1, then updates free space.
s32 MEMCARD_EraseFile(int slotIdx, char *srcString)
{
	char name[64];

	if (MEMCARD_FILEIO_STAGE != MC_STAGE_IDLE)
		return MC_RETURN_TIMEOUT;

	MEMCARD_StringSet(name, slotIdx, srcString);
	MEMCARD_FILEIO_STAGE = MC_STAGE_ERASE_PASS;
	if (erase(name) != 1)
		MEMCARD_FILEIO_STAGE = MC_STAGE_ERASE_FAIL;

	MEMCARD_GetFreeBytes(slotIdx);
	return MC_RETURN_PENDING;
}
