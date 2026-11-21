#include <common.h>

void LOAD_AppendQueue(struct BigHeader *bigfile, int type, int fileIndex, void *destinationPtr, void (*callback)(struct LoadQueueSlot *))
{
	register struct LoadQueueSlot *lqs CTR_PSX_REGISTER("$2");
	register struct LoadQueueSlot *queueBase CTR_PSX_REGISTER("$3");
	s16 queueLength = sdata->queueLength;
	u16 nextQueueLength = (u16)sdata->queueLength;
	s32 slotOffset;

	// NOTE(aalhendi): Retail reads the length as signed for indexing and unsigned for the final increment.
	if (queueLength >= LOAD_QUEUE_SLOT_COUNT)
	{
		return;
	}

#ifndef CTR_NATIVE
	// NOTE(aalhendi): GCC places this page load in the branch delay slot; the low relocation follows it.
	queueBase = (struct LoadQueueSlot *)(OFFSETOF_SDATA(queueSlots) & 0xffff0000u);
#endif
	CTR_PSX_ADD_SYMBOL_LOW_IN_PLACE(queueBase, RETAIL_QUEUE_SLOTS_ASM_NAME, &sdata->queueSlots[0]);
	slotOffset = queueLength * sizeof(*lqs);
	CTR_PSX_ADD_POINTER_OFFSET_OFFSET_FIRST(lqs, queueBase, slotOffset);
	lqs->flags = 0;
	lqs->ptrBigfileCdPos_UNUSED = bigfile;
	lqs->type_UNUSED = type;
	lqs->subfileIndex = fileIndex;
	lqs->ptrDestination = destinationPtr;
	lqs->size_UNUSED = 0;
	lqs->callbackFuncPtr = callback;

	sdata->queueLength = nextQueueLength + 1;
}

// NOTE: Retail restores ra before the queueReady store, filling the load-delay slot; the matching unit names the
// flag as a $gp byte of its own (GAME_LOAD_QUEUE_READY in retail_bindings.h), which lets GCC 2.8.1 schedule it so.
void LOAD_CDRequestCallback(struct LoadQueueSlot *lqs)
{
	if (lqs->callbackFuncPtr != NULL)
	{
		lqs->callbackFuncPtr(lqs);
	}

	GAME_LOAD_QUEUE_READY = 1;
}

// NOTE: The matching unit names the queue's fields as symbols of their own (tools/matching/namespaces/load-queue);
// other builds read the canonical sData and Data fields.
#ifndef LOAD_QUEUE_LENGTH
#define LOAD_QUEUE_READY_FLAG (sdata->queueReady)
#define LOAD_QUEUE_RETRY      (sdata->queueRetry)
#define LOAD_QUEUE_LENGTH     (sdata->queueLength)
#define LOAD_QUEUE_XA_STATE   (&sdata->XA_State)
#define LOAD_QUEUE_SLOTS      (&sdata->queueSlots[0])
#define LOAD_QUEUE_CURR_SLOT  (data.currSlot)
#endif

void LOAD_NextQueuedFile()
{
	struct LoadQueueSlot *curr;
	int i;

	if ((LOAD_QUEUE_READY_FLAG != 0) && (LOAD_QUEUE_XA_STATE[0] == 0) && (LOAD_QUEUE_LENGTH != 0))
	{
		int retry = LOAD_QUEUE_RETRY;
		LOAD_QUEUE_READY_FLAG = 0;
		i = LOAD_QUEUE_FIRST_PENDING_SLOT;

		if (retry == 0)
		{
			LOAD_QUEUE_CURR_SLOT = LOAD_QUEUE_SLOTS[0];
			for (; i < LOAD_QUEUE_LENGTH; i++)
			{
				LOAD_QUEUE_SLOTS[i - 1] = LOAD_QUEUE_SLOTS[i];
			}
		}
		else
		{
			LOAD_QUEUE_RETRY = 0;
		}

		switch (LOAD_QUEUE_CURR_SLOT.type_UNUSED)
		{
		case LT_RAW:
			LOAD_QUEUE_CURR_SLOT.ptrDestination =
			    LOAD_ReadFile_ex(LOAD_QUEUE_CURR_SLOT.ptrBigfileCdPos_UNUSED, LT_SETADDR, LOAD_QUEUE_CURR_SLOT.subfileIndex,
			                     LOAD_QUEUE_CURR_SLOT.ptrDestination, &LOAD_QUEUE_CURR_SLOT.size_UNUSED, LOAD_CDRequestCallback);
			break;

		case LT_DRAM:
			LOAD_QUEUE_CURR_SLOT.ptrDestination =
			    LOAD_DramFile(LOAD_QUEUE_CURR_SLOT.ptrBigfileCdPos_UNUSED, LOAD_QUEUE_CURR_SLOT.subfileIndex,
			                  LOAD_QUEUE_CURR_SLOT.ptrDestination, &LOAD_QUEUE_CURR_SLOT.size_UNUSED,
			                  (int)LOAD_QUEUE_CURR_SLOT.callbackFuncPtr);
			break;

		case LT_VRAM:
			LOAD_QUEUE_CURR_SLOT.ptrDestination =
			    LOAD_VramFile(LOAD_QUEUE_CURR_SLOT.ptrBigfileCdPos_UNUSED, LOAD_QUEUE_CURR_SLOT.subfileIndex,
			                  LOAD_QUEUE_CURR_SLOT.ptrDestination, &LOAD_QUEUE_CURR_SLOT.size_UNUSED,
			                  (int)LOAD_QUEUE_CURR_SLOT.callbackFuncPtr);
			break;
		}

		LOAD_QUEUE_LENGTH--;
	}

	if (GAME_LOAD_VRAM_FINISHED != 0)
	{
		if ((u32)(GAME_TRACKER->frameTimer_VsyncCallback - GAME_LOAD_VRAM_FINISHED) >= LOAD_QUEUE_VRAM_CALLBACK_DELAY_FRAMES)
		{
			curr = &LOAD_QUEUE_CURR_SLOT;
			if (curr->callbackFuncPtr != NULL)
			{
				curr->callbackFuncPtr(curr);
			}

			GAME_LOAD_VRAM_FINISHED = 0;
#if defined(CTR_NATIVE)
			// NOTE(aalhendi): CTR_NATIVE marks Mempack allocations with a host-only
			// flag while the retail path uses LT_SETADDR for the same ownership.
			if ((curr->flags & (LT_SETADDR | LT_MEMPACK)) != 0)
#else
			if ((curr->flags & LT_SETADDR) != 0)
#endif
			{
				MEMPACK_PopState();
			}

			LOAD_QUEUE_READY_FLAG = 1;
		}
	}
}
