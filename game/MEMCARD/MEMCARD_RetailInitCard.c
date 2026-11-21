#include <common.h>

// NOTE: Retail stores each OpenEvent handle after the next call's arguments are set up and fills that call's delay
// slot with the zero callback argument. With the handles named as $gp words of their own (the matching unit forces
// in tools/matching/namespaces/memcard-card/retail_symbols.h) GCC 2.8.1 schedules them that way; other builds use
// the live sdata fields.
#ifndef MEMCARD_CARD_SW_IOE
#define MEMCARD_CARD_SW_IOE       sdata->SwCARD_EvSpIOE
#define MEMCARD_CARD_SW_ERROR     sdata->SwCARD_EvSpERROR
#define MEMCARD_CARD_SW_TIMOUT    sdata->SwCARD_EvSpTIMOUT
#define MEMCARD_CARD_SW_NEW       sdata->SwCARD_EvSpNEW
#define MEMCARD_CARD_HW_IOE       sdata->HwCARD_EvSpIOE
#define MEMCARD_CARD_HW_ERROR     sdata->HwCARD_EvSpERROR
#define MEMCARD_CARD_HW_TIMOUT    sdata->HwCARD_EvSpTIMOUT
#define MEMCARD_CARD_HW_NEW       sdata->HwCARD_EvSpNEW
#define MEMCARD_CARD_STATUS_FLAGS sdata->memcardStatusFlags
#endif

void MEMCARD_InitCard(void)
{
	EnterCriticalSection();
	MEMCARD_CARD_SW_IOE = OpenEvent(SwCARD, EvSpIOE, EvMdNOINTR, NULL);
	MEMCARD_CARD_SW_ERROR = OpenEvent(SwCARD, EvSpERROR, EvMdNOINTR, NULL);
	MEMCARD_CARD_SW_TIMOUT = OpenEvent(SwCARD, EvSpTIMOUT, EvMdNOINTR, NULL);
	MEMCARD_CARD_SW_NEW = OpenEvent(SwCARD, EvSpNEW, EvMdNOINTR, NULL);
	MEMCARD_CARD_HW_IOE = OpenEvent(HwCARD, EvSpIOE, EvMdNOINTR, NULL);
	MEMCARD_CARD_HW_ERROR = OpenEvent(HwCARD, EvSpERROR, EvMdNOINTR, NULL);
	MEMCARD_CARD_HW_TIMOUT = OpenEvent(HwCARD, EvSpTIMOUT, EvMdNOINTR, NULL);
	MEMCARD_CARD_HW_NEW = OpenEvent(HwCARD, EvSpNEW, EvMdNOINTR, NULL);
	EnableEvent(MEMCARD_CARD_SW_IOE);
	EnableEvent(MEMCARD_CARD_SW_ERROR);
	EnableEvent(MEMCARD_CARD_SW_TIMOUT);
	EnableEvent(MEMCARD_CARD_SW_NEW);
	EnableEvent(MEMCARD_CARD_HW_IOE);
	EnableEvent(MEMCARD_CARD_HW_ERROR);
	EnableEvent(MEMCARD_CARD_HW_TIMOUT);
	EnableEvent(MEMCARD_CARD_HW_NEW);
	ExitCriticalSection();

	InitCARD(0);
	StartCARD();
	_bu_init();
	// NOTE(aalhendi): Bit 0 makes the first successful card-info event enter
	// the new-card path; with both bits 0 and 1 clear it is unformatted.
	MEMCARD_CARD_STATUS_FLAGS = 1;
}
