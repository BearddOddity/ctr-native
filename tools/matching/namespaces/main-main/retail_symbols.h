#ifndef CTR_MAIN_MAIN_RETAIL_SYMBOLS_H
#define CTR_MAIN_MAIN_RETAIL_SYMBOLS_H

#include <common.h>

// The globals main reads, each named as a symbol of its own so its loads and stores take retail's forms: a plain
// four-byte symbol is reached through $gp, a name bound to an address expression through lui and a low half. These
// declarations name existing storage and allocate no game data; matching.json's main-main target links the plain ones.
extern struct GameTracker *ctr_mainGameTracker;
extern struct GamepadSystem *ctr_mainGamepads;
extern char **ctr_mainLngStrings;
extern volatile int ctr_mainSoundPaused;
extern short ctr_mainPauseState asm("sdata_static+202");
extern unsigned short ctr_mainFrameCounter asm("sdata_static+2564");
extern volatile int ctr_mainXaState asm("sdata_static+1948");
// The state is read through a plain int (the switch reuses the loop test's load) and written through a volatile
// view of the same symbol (its stores stay out of delay slots).
extern int ctr_mainGameState;
extern volatile int ctr_mainGameStateWrite asm("ctr_mainGameState");
extern int ctr_mainLoadingStage;
extern short ctr_mainMenuState asm("sdata_static+2576");
extern struct BigHeader *ctr_mainBigfile;
extern void *volatile ctr_mainDrawSyncPtr;
// "ndi", copied into the level name as one word; reached absolutely as a 16-byte block
extern struct { int w[4]; } ctr_mainNdi;

#define MAIN_GAME_TRACKER     ctr_mainGameTracker
#define MAIN_GAMEPADS         ctr_mainGamepads
#define MAIN_LNG_STRINGS      ctr_mainLngStrings
#define MAIN_SOUND_PAUSED     ctr_mainSoundPaused
#define MAIN_PAUSE_STATE      ctr_mainPauseState
#define MAIN_FRAME_COUNTER    ctr_mainFrameCounter
#define MAIN_XA_STATE         ctr_mainXaState
#define MAIN_GAME_STATE       ctr_mainGameState
#define MAIN_GAME_STATE_WRITE ctr_mainGameStateWrite
#define MAIN_LOADING_STAGE    ctr_mainLoadingStage
#define MAIN_MENU_STATE       ctr_mainMenuState
#define MAIN_BIGFILE          ctr_mainBigfile
#define MAIN_DRAW_SYNC_PTR    ctr_mainDrawSyncPtr
#define MAIN_NDI_WORD         (ctr_mainNdi.w[0])

#endif
