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
// The card's event handles and status flags, $gp words (MEMCARD_RetailInitCard.c, MEMCARD_RetailHandleEvent.c).
#define MEMCARD_CARD_SW_IOE ctr_cardSwIOE
extern __typeof__(sdata->SwCARD_EvSpIOE) ctr_cardSwIOE asm("2408($28)");
#define MEMCARD_CARD_SW_ERROR ctr_cardSwERROR
extern __typeof__(sdata->SwCARD_EvSpERROR) ctr_cardSwERROR asm("2392($28)");
#define MEMCARD_CARD_SW_TIMOUT ctr_cardSwTIMOUT
extern __typeof__(sdata->SwCARD_EvSpTIMOUT) ctr_cardSwTIMOUT asm("2420($28)");
#define MEMCARD_CARD_SW_NEW ctr_cardSwNEW
extern __typeof__(sdata->SwCARD_EvSpNEW) ctr_cardSwNEW asm("2388($28)");
#define MEMCARD_CARD_HW_IOE ctr_cardHwIOE
extern __typeof__(sdata->HwCARD_EvSpIOE) ctr_cardHwIOE asm("2396($28)");
#define MEMCARD_CARD_HW_ERROR ctr_cardHwERROR
extern __typeof__(sdata->HwCARD_EvSpERROR) ctr_cardHwERROR asm("2376($28)");
#define MEMCARD_CARD_HW_TIMOUT ctr_cardHwTIMOUT
extern __typeof__(sdata->HwCARD_EvSpTIMOUT) ctr_cardHwTIMOUT asm("2416($28)");
#define MEMCARD_CARD_HW_NEW ctr_cardHwNEW
extern __typeof__(sdata->HwCARD_EvSpNEW) ctr_cardHwNEW asm("2384($28)");
#define MEMCARD_CARD_STATUS_FLAGS ctr_cardStatusFlags
extern __typeof__(sdata->memcardStatusFlags) ctr_cardStatusFlags asm("2400($28)");
#endif
