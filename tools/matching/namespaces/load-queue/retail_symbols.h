#ifndef CTR_LOAD_QUEUE_RETAIL_SYMBOLS_H
#define CTR_LOAD_QUEUE_RETAIL_SYMBOLS_H
#include "../../retail_bindings.h"

// NOTE: Typed aliases for LOAD_NextQueuedFile (LOAD_Queue.c). Retail reads the queue's ready and retry bytes
// unsigned and its length as $gp words, and reaches the XA state, the slots and the current slot by their absolute
// addresses (lui/addiu): arrays of unknown size, never small data. As symbols of their own, GCC 2.8.1 schedules them
// as retail does (the ready byte before the frame is opened). They name canonical resident storage and define no data.
#define LOAD_QUEUE_READY_FLAG ctr_loadQueueReadyFlag
extern u8 ctr_loadQueueReadyFlag asm("308($28)");
#define LOAD_QUEUE_RETRY ctr_loadQueueRetry
extern u8 ctr_loadQueueRetry asm("309($28)");
#define LOAD_QUEUE_LENGTH ctr_loadQueueLength
extern __typeof__(sdata->queueLength) ctr_loadQueueLength asm("310($28)");
#define LOAD_QUEUE_XA_STATE ctr_loadQueueXaState
extern XAState ctr_loadQueueXaState[] asm("sdata_static+1948");
// A pointer, not the array: indexing a pointer lets GCC fold slot[i - 1]'s constant into the address (slots - 24),
// so both copy pointers become induction variables, as in retail.
#define LOAD_QUEUE_SLOTS ((struct LoadQueueSlot *)ctr_loadQueueSlots)
extern struct LoadQueueSlot ctr_loadQueueSlots[] asm(RETAIL_QUEUE_SLOTS_ASM_NAME);
#define LOAD_QUEUE_CURR_SLOT ctr_loadQueueCurrSlot
extern struct LoadQueueSlot ctr_loadQueueCurrSlot asm(RETAIL_CURR_SLOT_ASM_NAME);
#endif
