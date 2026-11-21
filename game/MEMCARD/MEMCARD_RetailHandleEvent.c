#include <common.h>

// NOTE: Matching units name the card's resident storage by retail's own addressing
// (tools/matching/namespaces/memcard-event/retail_symbols.h); any other build uses the live sdata fields.
#ifndef MEMCARD_FILEIO_STAGE
#define MEMCARD_FILEIO_STAGE   sdata->memcard_stage
#define MEMCARD_FILEIO_BUFFER  sdata->memcard_ptrStart
#define MEMCARD_FILEIO_RETRIES sdata->memcard_remainingAttempts
#define MEMCARD_FILEIO_SIZE    sdata->memcardFileSize
#define MEMCARD_FILEIO_SLOT    sdata->memcardSlot
#endif
#ifndef MEMCARD_CARD_FREE_BYTES
#define MEMCARD_CARD_FREE_BYTES sdata->memoryCard_SizeRemaining
#endif
#ifndef MEMCARD_CARD_STATUS_FLAGS
#define MEMCARD_CARD_STATUS_FLAGS sdata->memcardStatusFlags
#endif
#ifndef MEMCARD_EVENT_ICON_SIZE
#define MEMCARD_EVENT_ICON_SIZE       sdata->memcardIconSize
#define MEMCARD_EVENT_CHECKSUM_INDEX  sdata->crc16_checkpoint_byteIndex
#define MEMCARD_EVENT_CHECKSUM_STATUS sdata->crc16_checkpoint_status
#define MEMCARD_EVENT_ICON_HAND       data.memcardIcon_PsyqHand
#endif

// NOTE: The card's file is the icon block (128 bytes per icon frame, the frame count in the header's third byte),
// then the save data at file-size steps. A load reads the data, checksums it, then reads and checks the next part.
// Retail's register and block choices need a few constraints: the load paths share their ReadFile call with the
// first part's retry (two entries, so GCC 2.8.1 keeps the check's and the retry's own offset arithmetic instead of
// cross-jumping one into the other), the call's size stays in $a1 and each offset's icon size loads into $a0.
int MEMCARD_HandleEvent(void)
{
	int event;
	int stage;
	u32 status;
	u32 flags;
	u32 mask;
	int offset;
	register int size CTR_PSX_REGISTER("$5");

	switch (MEMCARD_FILEIO_STAGE)
	{
	case MC_STAGE_GETINFO:
		event = MEMCARD_GetNextSwEvent();
		if (event == MC_RETURN_IOE)
		{
			status = MEMCARD_CARD_STATUS_FLAGS;
			if ((status & 1) != 0)
			{
				MEMCARD_FILEIO_STAGE = MC_STAGE_NEWCARD;
				MEMCARD_SkipEvents();
				while (_card_load(MEMCARD_FILEIO_SLOT) != 1)
				{
				}
				event = MC_RETURN_PENDING;
				goto done;
			}
			if ((status & 2) == 0)
			{
				MEMCARD_CARD_FREE_BYTES = 0;
				event = MC_RETURN_UNFORMATTED;
			}
			MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
			goto done;
		}
		if (event == MC_RETURN_NEWCARD)
		{
			MEMCARD_SkipEvents();
			while (_card_clear(MEMCARD_FILEIO_SLOT) != 1)
			{
			}
			event = MEMCARD_WaitForHwEvent();
			if (event != MC_RETURN_IOE)
			{
				MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
				goto done;
			}
			MEMCARD_FILEIO_STAGE = MC_STAGE_NEWCARD;
			MEMCARD_SkipEvents();
			while (_card_load(MEMCARD_FILEIO_SLOT) != 1)
			{
			}
			event = MC_RETURN_PENDING;
			goto done;
		}
		if (event != MC_RETURN_PENDING)
		{
			MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
			MEMCARD_CARD_FREE_BYTES = 0;
		}
		goto done;

	case MC_STAGE_NEWCARD:
		event = MEMCARD_GetNextSwEvent();
		if (event == MC_RETURN_IOE)
		{
			MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
			MEMCARD_CARD_STATUS_FLAGS = (MEMCARD_CARD_STATUS_FLAGS | 2) & ~1;
			MEMCARD_GetFreeBytes(MEMCARD_FILEIO_SLOT);
			event = MC_RETURN_NEWCARD;
			goto done;
		}
		if (event == MC_RETURN_NEWCARD)
		{
			event = MC_RETURN_UNFORMATTED;
			flags = MEMCARD_CARD_STATUS_FLAGS;
			mask = ~3;
			MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
		store_flags:
			MEMCARD_CARD_STATUS_FLAGS = flags & mask;
			goto done;
		}
		if (event != MC_RETURN_PENDING)
		{
			MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
		}
		goto done;

	case MC_STAGE_LOAD_PART0_START:
		event = MEMCARD_GetNextSwEvent();
		if (event == MC_RETURN_IOE)
		{
			MEMCARD_EVENT_ICON_SIZE = ((MEMCARD_FILEIO_BUFFER[2] & 0xf) + 1) << 7;
			MEMCARD_FILEIO_STAGE = MC_STAGE_LOAD_PART2_READ;
			event = MEMCARD_ReadFile(MEMCARD_EVENT_ICON_SIZE, MEMCARD_FILEIO_SIZE);
			// Bit 4: the card's file has no room for the second copy (its block count, 8 KB blocks).
			if ((((MEMCARD_EVENT_ICON_SIZE + MEMCARD_FILEIO_SIZE + 0x1fff) >> 13) < MEMCARD_FILEIO_BUFFER[3]) &&
			    (((MEMCARD_EVENT_ICON_SIZE + MEMCARD_FILEIO_SIZE * 2 + 0x1fff) >> 13) >= 2))
			{
				mask = ~4;
				flags = MEMCARD_CARD_STATUS_FLAGS;
				goto store_flags;
			}
			else
			{
				MEMCARD_CARD_STATUS_FLAGS |= 4;
			}
			goto done;
		}
		if (event == MC_RETURN_PENDING)
		{
			goto done;
		}
		if (MEMCARD_FILEIO_RETRIES <= 0)
		{
			goto close;
		}
		MEMCARD_FILEIO_RETRIES--;
		offset = 0;
		size = 0x80;
	read_next_part:
		CTR_PSX_MEMORY_BARRIER();
	read_retry:
		event = MEMCARD_ReadFile(offset, size);
		goto done;

	case MC_STAGE_LOAD_PART3_CHECK:
	case MC_STAGE_LOAD_PART5_CHECK:
	check:
		event = MEMCARD_ChecksumLoad(MEMCARD_FILEIO_BUFFER, MEMCARD_FILEIO_SIZE);
		if (event == MC_RETURN_IOE)
		{
			goto close;
		}
		if (event == MC_RETURN_PENDING)
		{
			goto done;
		}
		if ((MEMCARD_CARD_STATUS_FLAGS & 4) != 0)
		{
			goto close;
		}
		if (MEMCARD_FILEIO_STAGE >= MC_STAGE_LOAD_PART5_CHECK)
		{
			goto close;
		}
		stage = MEMCARD_FILEIO_STAGE;
		{
			int icon CTR_PSX_LOCAL_REGISTER_HINT("$4") = MEMCARD_EVENT_ICON_SIZE;
			offset = icon + (stage - 3) * MEMCARD_FILEIO_SIZE;
		}
		size = MEMCARD_FILEIO_SIZE;
		MEMCARD_FILEIO_STAGE = stage + 1;
		goto read_next_part;

	case MC_STAGE_LOAD_PART2_READ:
	case MC_STAGE_LOAD_PART4_READ:
		event = MEMCARD_GetNextSwEvent();
		if (event == MC_RETURN_IOE)
		{
			MEMCARD_EVENT_CHECKSUM_INDEX = 0;
			MEMCARD_EVENT_CHECKSUM_STATUS = 0;
			MEMCARD_FILEIO_STAGE++;
			event = MC_RETURN_PENDING;
			if ((MEMCARD_CARD_STATUS_FLAGS & 8) != 0)
			{
				goto check;
			}
			goto done;
		}
		if (event == MC_RETURN_PENDING)
		{
			goto done;
		}
		if (MEMCARD_FILEIO_RETRIES <= 0)
		{
			goto close;
		}
		MEMCARD_FILEIO_RETRIES--;
		size = MEMCARD_FILEIO_SIZE;
		{
			int icon CTR_PSX_LOCAL_REGISTER_HINT("$4") = MEMCARD_EVENT_ICON_SIZE;
			offset = icon + (MEMCARD_FILEIO_STAGE - 4) * size;
		}
		goto read_retry;

	case MC_STAGE_SAVE_PART0_START:
	case MC_STAGE_SAVE_PART1_ICON:
	case MC_STAGE_SAVE_PART2_WRITE:
		event = MEMCARD_GetNextSwEvent();
		if (event == MC_RETURN_IOE)
		{
			stage = MEMCARD_FILEIO_STAGE;
			if ((stage == MC_STAGE_SAVE_PART0_START) ||
			    ((stage < MC_STAGE_SAVE_PART2_WRITE) && ((MEMCARD_CARD_STATUS_FLAGS & 4) == 0)))
			{
				MEMCARD_FILEIO_STAGE = stage + 1;
				event = MEMCARD_WriteFile(MEMCARD_EVENT_ICON_SIZE + (stage - 9) * MEMCARD_FILEIO_SIZE, MEMCARD_FILEIO_BUFFER,
				                          MEMCARD_FILEIO_SIZE);
				goto done;
			}
			MEMCARD_CloseFile();
			MEMCARD_GetFreeBytes(MEMCARD_FILEIO_SLOT);
			goto done;
		}
		if (event == MC_RETURN_PENDING)
		{
			goto done;
		}
		if (MEMCARD_FILEIO_RETRIES <= 0)
		{
			goto close;
		}
		MEMCARD_FILEIO_RETRIES--;
		if (MEMCARD_FILEIO_STAGE == MC_STAGE_SAVE_PART0_START)
		{
			event = MEMCARD_WriteFile(0, (const u8 *)MEMCARD_EVENT_ICON_HAND, MEMCARD_EVENT_ICON_SIZE);
			goto done;
		}
		event = MEMCARD_WriteFile(MEMCARD_EVENT_ICON_SIZE + (MEMCARD_FILEIO_STAGE - 10) * MEMCARD_FILEIO_SIZE,
		                          MEMCARD_FILEIO_BUFFER, MEMCARD_FILEIO_SIZE);
		goto done;

	close:
		MEMCARD_CloseFile();
	done:
		return event;

	case MC_STAGE_ERASE_FAIL:
		MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
		return MC_RETURN_TIMEOUT;

	case MC_STAGE_ERASE_PASS:
		MEMCARD_FILEIO_STAGE = MC_STAGE_IDLE;
		return MC_RETURN_IOE;

	default:
		return MC_RETURN_TIMEOUT;
	}
}
