#ifndef CTR_MEMCARD_EVENT_RETAIL_SYMBOLS_H
#define CTR_MEMCARD_EVENT_RETAIL_SYMBOLS_H
#include "../memcard-card/retail_symbols.h"

// NOTE: Typed aliases for MEMCARD_HandleEvent (MEMCARD_RetailHandleEvent.c): the card state machine's $gp words as
// symbols of their own, so GCC 2.8.1 may load the stage before the prologue as retail does. They name canonical
// resident storage and define no data.
#define MEMCARD_EVENT_ICON_SIZE ctr_eventIconSize
extern __typeof__(sdata->memcardIconSize) ctr_eventIconSize asm("2412($28)");
#define MEMCARD_EVENT_CHECKSUM_INDEX ctr_eventChecksumIndex
extern __typeof__(sdata->crc16_checkpoint_byteIndex) ctr_eventChecksumIndex asm("2372($28)");
#define MEMCARD_EVENT_CHECKSUM_STATUS ctr_eventChecksumStatus
extern __typeof__(sdata->crc16_checkpoint_status) ctr_eventChecksumStatus asm("2404($28)");
// The hand icon, by its absolute address (lui/addiu), as retail reaches it.
#define MEMCARD_EVENT_ICON_HAND ctr_eventIconHand
extern __typeof__(data.memcardIcon_PsyqHand) ctr_eventIconHand asm("data+19968");
#endif
