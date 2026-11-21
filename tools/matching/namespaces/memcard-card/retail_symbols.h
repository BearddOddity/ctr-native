#ifndef CTR_MEMCARD_CARD_RETAIL_SYMBOLS_H
#define CTR_MEMCARD_CARD_RETAIL_SYMBOLS_H
#include "../memcard-fileio/retail_symbols.h"

// NOTE: Typed aliases for the retail card operations (MEMCARD_RetailCard.c). Retail reads the card stage, slot and
// retry count as $gp words, but the wildcard and the card's file names by their absolute addresses (lui/addiu):
// arrays of unknown size, never small data. They name canonical resident storage and define no data.
#define MEMCARD_CARD_FREE_BYTES ctr_cardFreeBytes
extern __typeof__(sdata->memoryCard_SizeRemaining) ctr_cardFreeBytes asm("2368($28)");
#define MEMCARD_CARD_ANY_FILE ctr_cardAnyFile
extern char ctr_cardAnyFile[] asm("sdata_static+1200");
#define MEMCARD_CARD_PATH ctr_cardPath
extern char ctr_cardPath[] asm("sdata_static+49464");
// The ghost name follows the card path (s_memcardFindGhostFile): retail reaches it from the path's address.
#define MEMCARD_CARD_GHOST_FILE (&ctr_cardPath[0x20])
#endif
