#include <common.h>

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_perf.h>
#include <platform/native_replay_scheduler.h>
#include <platform/native_savestate.h>
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
static struct NativePerfFrameInfo MainPerf_FrameInfo(struct GameTracker *gGT)
{
	struct NativePerfFrameInfo info;

	info.frameCounter = sdata->frameCounter;
	info.timer = gGT->timer;
	info.levelID = gGT->levelID;
	info.gameMode1 = gGT->gameMode1;
	info.loadingStage = sdata->Loading.stage;
	info.boolDemoMode = gGT->boolDemoMode;
	info.numPlyrCurrGame = gGT->numPlyrCurrGame;
	info.elapsedTimeMS = gGT->elapsedTimeMS;
	info.vsyncTillFlip = sdata->vsyncTillFlip;
	info.vSync_between_drawSync = gGT->vSync_between_drawSync;
	info.frameTimer_VsyncCallback = gGT->frameTimer_VsyncCallback;

	return info;
}

static struct NativeReplaySchedulerFrameInfo MainReplayScheduler_FrameInfo(struct GameTracker *gGT)
{
	struct NativeReplaySchedulerFrameInfo info;

	info.frameTimer = gGT->frameTimer_VsyncCallback;
	info.frameCounter = sdata->frameCounter;
	info.timer = gGT->timer;
	info.framesInThisLEV = gGT->framesInThisLEV;
	info.elapsedTimeMS = gGT->elapsedTimeMS;
	info.msInThisLEV = gGT->msInThisLEV;
	info.elapsedEventTime = gGT->elapsedEventTime;
	info.mainGameState = sdata->mainGameState;
	info.loadingStage = sdata->Loading.stage;
	info.levelID = gGT->levelID;
	info.mixRandomNumber = (u32)sdata->randomNumber;
	info.audioRNG = sdata->audioRNG;
	info.deadcoed0 = (u32)gGT->deadcoed_struct.state0;
	info.deadcoed1 = (u32)gGT->deadcoed_struct.state1;
	info.advRng0 = sdata->advRng.state0;
	info.advRng1 = sdata->advRng.state1;

	return info;
}
#endif

// The globals main reads, as retail reaches them. The matching build's namespace header
// (tools/matching/namespaces/main-main/retail_symbols.h) names each as a symbol of its own, so the gp-relative and
// absolute loads come out as retail's; every other build reads them through sdata.
#ifndef MAIN_GAME_TRACKER
#define MAIN_GAME_TRACKER     (sdata->gGT)
#define MAIN_GAMEPADS         (sdata->gGamepads)
#define MAIN_LNG_STRINGS      (sdata->lngStrings)
#define MAIN_SOUND_PAUSED     (sdata->boolSoundPaused)
#define MAIN_PAUSE_STATE      (sdata->pause_state)
#define MAIN_FRAME_COUNTER    (sdata->frameCounter)
#define MAIN_XA_STATE         (sdata->XA_State)
#define MAIN_GAME_STATE       (sdata->mainGameState)
#define MAIN_GAME_STATE_WRITE (sdata->mainGameState)
#define MAIN_LOADING_STAGE    (sdata->Loading.stage)
#define MAIN_MENU_STATE       (sdata->mainMenuState)
#define MAIN_BIGFILE          (sdata->ptrBigfile1)
#define MAIN_DRAW_SYNC_PTR    (sdata->MainDrawCb_DrawSyncPtr)
#endif

// Retail has no StateZero of its own: its code sits inside main (0x8003c614-0x8003c984). The PS1 build writes it in
// place there (GNU89 extern inline: no copy of its own, so it has to come before main); the native build keeps it a
// function of its own, so a mod can replace it.
#ifdef CTR_NATIVE
#define MAIN_STATE_ZERO_LINKAGE
#else
#define MAIN_STATE_ZERO_LINKAGE extern __inline__
#endif

// NOTE(aalhendi): Source split of retail main's state-0 body
// 0x8003c614-0x8003c984.
// By separating this, it can be overwritten dynamically (oxide fix).
MAIN_STATE_ZERO_LINKAGE void StateZero(void)
{
	u16 *clockEffect;
	u32 vramSize;

	memset(MAIN_GAME_TRACKER, 0, sizeof(*MAIN_GAME_TRACKER));

	// Set Video Mode to NTSC
	SetVideoMode(0);
	ResetCallback();

#define MEMPACK_SIZE 0x200000 // 2mb

	MEMPACK_Init(MEMPACK_SIZE);
	LOAD_InitCD();
	RaceFlag_SetFullyOffScreen();

	ResetGraph(0);
	SetGraphDebug(0);

	MainInit_VRAMClear();

	SetDispMask(1);

	SetDefDrawEnv(&MAIN_GAME_TRACKER->db[0].drawEnv, 0, 0, 0x200, 0xd8);
	SetDefDrawEnv(&MAIN_GAME_TRACKER->db[1].drawEnv, 0, 0x128, 0x200, 0xd8);
	SetDefDispEnv(&MAIN_GAME_TRACKER->db[0].dispEnv, 0, 0x128, 0x200, 0xd8);
	SetDefDispEnv(&MAIN_GAME_TRACKER->db[1].dispEnv, 0, 0, 0x200, 0xd8);

	MAIN_GAME_TRACKER->db[0].dispEnv.screen.x = 0;
	MAIN_GAME_TRACKER->db[0].dispEnv.screen.y = 0xc;
	MAIN_GAME_TRACKER->db[0].dispEnv.screen.w = 0x100;
	MAIN_GAME_TRACKER->db[0].dispEnv.screen.h = 0xd8;

	MAIN_GAME_TRACKER->db[1].dispEnv.screen.x = 0;
	MAIN_GAME_TRACKER->db[1].dispEnv.screen.y = 0xc;
	MAIN_GAME_TRACKER->db[1].dispEnv.screen.w = 0x100;
	MAIN_GAME_TRACKER->db[1].dispEnv.screen.h = 0xd8;

	MAIN_GAME_TRACKER->db[0].drawEnv.isbg = 1;
	MAIN_GAME_TRACKER->db[0].drawEnv.r0 = 0;
	MAIN_GAME_TRACKER->db[0].drawEnv.g0 = 0;
	MAIN_GAME_TRACKER->db[0].drawEnv.b0 = 0;

	MAIN_GAME_TRACKER->db[1].drawEnv.isbg = 1;
	MAIN_GAME_TRACKER->db[1].drawEnv.r0 = 0;
	MAIN_GAME_TRACKER->db[1].drawEnv.g0 = 0;
	MAIN_GAME_TRACKER->db[1].drawEnv.b0 = 0;

	// default number of lives in battle
	// this is left over from prototypes, useless in retail
	MAIN_GAME_TRACKER->battleLifeLimit = 5;

	// 30 second counter?
	MAIN_GAME_TRACKER->constVal_9000 = 9000;

	// Retail sets the weapons before the lap count (3).
	MAIN_GAME_TRACKER->battleSetup.enabledWeapons |= BATTLE_DEFAULT_WEAPON_FLAGS;
	MAIN_GAME_TRACKER->numLaps = 3;
	MAIN_GAME_TRACKER->numPlyrCurrGame = 1;
	MAIN_GAME_TRACKER->numPlyrNextGame = 1;
	*(u32 *)&MAIN_GAME_TRACKER->battleSetup.teamOfEachPlayer = 0x3020100;

	// traffic light countdown timer, set to negative one second
	MAIN_GAME_TRACKER->trafficLightsTimer = 0xfffffc40;

	Timer_Init();
	EnterCriticalSection();
	MAIN_DRAW_SYNC_PTR = (void *)(u32)DrawSyncCallback(&MainDrawCb_DrawSync);
	ExitCriticalSection();

	MEMCARD_InitCard();
	VSync(0);
	GAMEPAD_Init(MAIN_GAMEPADS);
	VSync(0);
	GAMEPAD_GetNumConnected(MAIN_GAMEPADS);

#ifdef CTR_NATIVE
#define BIGPATH "\\BIGFILE.BIG;1"
#else
#define BIGPATH rdata.s_PathTo_Bigfile
#endif

	// Get CD Position fo BIGFILE
	MAIN_BIGFILE = LOAD_ReadDirectory(BIGPATH);

// Defrag to save heap space,
// required because MEMPACK_Init moves heap
#if 0
	// NOTE(aalhendi): Retail main does not rewrite BIGFILE overlay sizes here.
	extern char RB_NewEndFile[4];

	// Dont load full overlay file, cut off the end
	struct BigEntry *firstEntry = BIG_GETENTRY(sdata->ptrBigfile1);
	firstEntry[231].size = 28 * 0x800;
	// firstEntry[231].size = (u32)RB_NewEndFile - (u32)OVR_Region3;
	// printf("Size: %08x\n", firstEntry[231].size);

	// Cut off Region1 overlays at 2 sectors (not 3),
	// This protects Region2 RAM so it is not overwritten
	// during Region1 disc streaming, saves loading time
	for (int i = 221; i <= 225; i++)
		firstEntry[i].size = 2 * 0x800;
#endif

	// English=1
	LOAD_LangFile((int)MAIN_BIGFILE, 1);
	GAMEPROG_NewGame_OnBoot();
	MAIN_GAME_TRACKER->overlayIndex_null_notUsed = 0;

	MAIN_GAME_TRACKER->levelID = NAUGHTY_DOG_CRATE;
#ifdef MAIN_NDI_WORD
	// Retail copies the four bytes of "ndi" as one word.
	*(int *)MAIN_GAME_TRACKER->levelName = MAIN_NDI_WORD;
#else
	memcpy(MAIN_GAME_TRACKER->levelName, sdata->s_ndi, sizeof(sdata->s_ndi));
#endif
	// gGT->levelID = OXIDE_TRUE_ENDING;

	InitGeom();
	SetGeomOffset(0x100, 0x78); // width/2, height/2
	SetGeomScreen(0x140);       // "distance" to screen, alters FOV

	// NOTE(aalhendi): Retail calls 0x8006ae74 here; keep the verified depth
	// scale setup on every target.
	RenderBucket_InitDepthGTE();
	// NOTE(aalhendi): Retail bakes these authored matrix tables in place.
	Vector_BakeMatrixTable();

	MAIN_GAME_TRACKER->swapchainIndex = 0;
	MAIN_GAME_TRACKER->backBuffer = &MAIN_GAME_TRACKER->db[0];

	MAIN_GAME_TRACKER->overlayIndex_EndOfRace = 0xff;
	MAIN_GAME_TRACKER->overlayIndex_LOD = OVERLAY_INDEX_NONE;
	MAIN_GAME_TRACKER->overlayIndex_Threads = OVERLAY_INDEX_NONE;

	PutDispEnv(&MAIN_GAME_TRACKER->db[1].dispEnv);
	PutDrawEnv(&MAIN_GAME_TRACKER->db[1].drawEnv);
	DrawSync(0);

	// Load Intro TIM for "SCEA Presents" from VRAM file
	LOAD_VramFile(MAIN_BIGFILE, 0x1fd, NULL, &vramSize, -1);
	MainInit_VRAMDisplay();

	// \SOUNDS\KART.HWL;1
	howl_InitGlobals(data.kartHwlPath);

	VSyncCallback(MainDrawCb_Vsync);

	Music_SetIntro();
	CseqMusic_StopAll();
	CseqMusic_Start(CSEQ_SONG_LEVEL, 0, NULL, 0, 0);
	Music_Start(0);

	// "Start your engines, for Sony Computer..."
	CDSYS_XAPlay(CDSYS_XA_TYPE_EXTRA, 0x50);

	while (MAIN_XA_STATE != 0)
	{
		// WARNING: Read-only address (ram, 0x8008d888) is written
#ifdef CTR_NATIVE
		// NOTE(aalhendi): Retail hardware interrupts keep XA/audio moving while
		// this loop spins. Native owns VBlank in VSync(), so pump it here.
		VSync(0);
#endif
		CDSYS_XAPauseAtEnd();
	}

	DecalGlobal_Clear(MAIN_GAME_TRACKER);

	// This loads UI textures (shared.vrm)
	// This includes traffic lights, font, and more
	LOAD_VramFile(MAIN_BIGFILE, 0x102, NULL, &vramSize, -1);

	MAIN_GAME_STATE_WRITE = 3;

	// start loading
	MAIN_LOADING_STAGE = LOAD_TEN_STAGES_0;

	clockEffect = &MAIN_GAME_TRACKER->clockEffectEnabled;
	MAIN_GAME_TRACKER->gameMode1 |= LOADING;
	MAIN_GAME_TRACKER->clockEffectEnabled = *clockEffect & 0xfffe;
}

#ifdef CTR_NATIVE
u32 CTR_Main(void)
#else
u32 main(void)
#endif
{
	u32 AddBitsConfig0;
	u32 RemBitsConfig0;
	u32 AddBitsConfig8;
	u32 RemBitsConfig8;
	int iVar8;
	int stage;
	u32 uVar12;

	// GCC puts retail's call to __main at main's entry itself. Native has no __main.

	// Retail reads the game tracker and the gamepads again at each use: it keeps neither in a register.
#ifdef CTR_NATIVE
	// The native build never leaves the state loop (state 5 can't happen there).
	for (;;)
#else
	while (MAIN_GAME_STATE != 5)
#endif
	{
		LOAD_NextQueuedFile();
		CDSYS_XAPauseAtEnd();

		// Retail lays the cases out in this order: 0, 2, 1, 3, 4.
		switch (MAIN_GAME_STATE)
		{
		// Initialize Game (happens once)
		case 0:
			StateZero();
			break;

		// Reset stage, reset music
		case 2:
			Audio_SetState_Safe(1);
			MEMPACK_PopState();

			// ignore threads, because we PopState,
			// so the threadpool will reset anyway
			LevInstDef_RePack(MAIN_GAME_TRACKER->level1->ptr_mesh_info, 0);

			MAIN_GAME_STATE_WRITE = 1;
			break;

		// Happens on first frame that loading ends
		case 1:
		{
			int menuLev;
			ElimBG_Deactivate(MAIN_GAME_TRACKER);
			menuLev = MAIN_MENU_LEVEL;

			MainStats_RestartRaceCountLoss();
			Voiceline_ClearTimeStamp();

			// Disable End-Of-Race menu
			MAIN_GAME_TRACKER->gameMode1 &= ~END_OF_RACE;

			if (MAIN_GAME_TRACKER->levelID != menuLev && RaceFlag_IsFullyOnScreen())
			{
				RaceFlag_BeginTransition(2);
			}
			else if (MAIN_GAME_TRACKER->levelID == menuLev && RaceFlag_IsFullyOffScreen())
			{
				RaceFlag_SetFullyOnScreen();
			}

			DropRain_Reset(MAIN_GAME_TRACKER);
			GAMEPROG_GetPtrHighScoreTrack();
			MainInit_FinalizeInit(MAIN_GAME_TRACKER);
			GAMEPAD_GetNumConnected(MAIN_GAMEPADS);

			MAIN_SOUND_PAUSED = 0;
			VehBirth_EngineAudio_AllPlayers();

			// 9 = intro cutscene
			// 10 = traffic lights
			// 11 = racing

			// Arcade-Style track starts with intro cutscene
			uVar12 = 9;

			if (
			    // If Level ID is less than 18, it's one of the race tracks
			    ((unsigned)MAIN_GAME_TRACKER->levelID < NITRO_COURT) || (
			                                        // Battle-Style track starts with traffic lights
			                                        uVar12 = 10,
			                                        // Level ID >= 18 and < 23
			                                        // Battle tracks
			                                        (unsigned)(MAIN_GAME_TRACKER->levelID - NITRO_COURT) < 7))
			{
				Audio_SetState_Safe(uVar12);
			}
			MAIN_GAME_STATE_WRITE = 3;
			MAIN_GAME_TRACKER->clockEffectEnabled &= 0xfffe;
		}
			break;

		// Main Gameplay Update
		// Makes up all normal interaction with the game
		case 3:

			// if loading, or gameplay interrupted
			if (MAIN_LOADING_STAGE != LOAD_IDLE)
			{
				if (RaceFlag_IsFullyOnScreen() == 1 || (MAIN_GAME_TRACKER->levelID == NAUGHTY_DOG_CRATE) || (MAIN_PAUSE_STATE != 0))
				{
					MAIN_GAME_TRACKER->gameMode1 |= LOADING;
				}

				// elapsed milliseconds per frame, locked 32 here
				// impacts speed of flag wave during "loading...", but does not impact speed of flying text
				MAIN_GAME_TRACKER->elapsedTimeMS = 32;
				stage = MAIN_LOADING_STAGE;

				// if restarting race
				if (stage == LOAD_RESTART)
				{
					if (RaceFlag_IsFullyOnScreen() == 1)
					{
						// reinitialize world,
						// does not reinitialize pools
						MAIN_GAME_STATE_WRITE = 2;

						// no loading, and no interruption
						MAIN_LOADING_STAGE = LOAD_IDLE;

						// Turn off the "Loading..." flag
						MAIN_GAME_TRACKER->gameMode1 &= ~LOADING;
						break;
					}

					// if not fully on-screen, do not BREAK,
					// keep rendering the scene
				}

				// if waiting for checkered flag to cover screen,
				// right before loading the next requested level
				else if (stage == -4)
				{
					if (RaceFlag_IsFullyOnScreen() == 1)
					{
						struct GameTracker *gt = MAIN_GAME_TRACKER;
						int lev;
						AddBitsConfig0 = sdata->Loading.OnBegin.AddBitsConfig0;
						RemBitsConfig0 = sdata->Loading.OnBegin.RemBitsConfig0;
						AddBitsConfig8 = sdata->Loading.OnBegin.AddBitsConfig8;
						RemBitsConfig8 = sdata->Loading.OnBegin.RemBitsConfig8;
						sdata->Loading.OnBegin.AddBitsConfig0 = 0;
						sdata->Loading.OnBegin.RemBitsConfig0 = 0;
						sdata->Loading.OnBegin.AddBitsConfig8 = 0;
						sdata->Loading.OnBegin.RemBitsConfig8 = 0;

						gt->hudFlags &= HUD_FLAG_CLEAR_INTRO_RACE_TITLE_BARS_MASK;
#ifdef CTR_NATIVE
						lev = (s16)sdata->Loading.Lev_ID_To_Load;
#else
						// retail reads the level as a signed halfword
						lev = *(short *)&sdata->Loading.Lev_ID_To_Load;
#endif
						gt->gameMode1 |= AddBitsConfig0;
						gt->gameMode2 |= AddBitsConfig8;
						gt->gameMode1 &= ~RemBitsConfig0;
						gt->gameMode2 &= ~RemBitsConfig8;
						MainRaceTrack_StartLoad(lev);
					}

					else if (RaceFlag_IsFullyOffScreen() == 1)
					{
						RaceFlag_BeginTransition(1);
					}

					// do not BREAK,
					// keep rendering the scene
				}

				// if loading VLC
				else if (stage == LOAD_VLC)
				{
					// if VLC is not loaded, quit
					// we know when it's done from a load callback
					if (sdata->bool_IsLoaded_VlcTable != 1)
					{
						break;
					}

					// Retail writes the finish out here again, not a jump to the one below.
					MAIN_LOADING_STAGE = LOAD_IDLE;
					MAIN_GAME_STATE_WRITE = 1;
					MAIN_GAME_TRACKER->gameMode1 &= ~LOADING;
					break;
				}

				// if something is being loaded
				else
				{
					// If just finished loading stage 9
					if ((MAIN_LOADING_STAGE = LOAD_TenStages(MAIN_GAME_TRACKER, stage, MAIN_BIGFILE)) == LOAD_FINISHED)
					{
						if ((MAIN_GAME_TRACKER->levelID == MAIN_MENU_LEVEL) || (MAIN_GAME_TRACKER->levelID == SCRAPBOOK))
						{
							MainLoadVLC();

							// start loading VLC (scroll up to stage == LOAD_VLC)
							MAIN_LOADING_STAGE = LOAD_VLC;
							break;
						}

						// loading is finished,
						// initialize world and pools,
						// remove LOADING... flag from gGT
						MAIN_LOADING_STAGE = LOAD_IDLE;
						MAIN_GAME_STATE_WRITE = 1;
						MAIN_GAME_TRACKER->gameMode1 &= ~LOADING;
						break;
					}

					// else, do not BREAK,
					// keep rendering the scene
					// which is the checkered flag
				}
			}

			// =========== Main Game Loop ======================

			if ((
			        // Check value of traffic lights
			        (-960 < MAIN_GAME_TRACKER->trafficLightsTimer) &&
			        // if not drawing intro race cutscene and if not paused
			        ((MAIN_GAME_TRACKER->gameMode1 & (START_OF_RACE | PAUSE_ALL)) == 0)) &&
			    (
			        // amount of milliseconds on Traffic Lights - elapsed milliseconds per frame, ~32
			        iVar8 = MAIN_GAME_TRACKER->trafficLightsTimer - MAIN_GAME_TRACKER->elapsedTimeMS,
			        // decrease amount of time on Traffic Lights
			        MAIN_GAME_TRACKER->trafficLightsTimer = iVar8,
			        // if countdown has gone down far enough for traffic lights to go off-screen
			        iVar8 < -960))
			{
				// set a floor value, so countdown can't go farther negative
				MAIN_GAME_TRACKER->trafficLightsTimer = 0xfffffc40;
			}

			// frame counter (retail: a halfword)
			MAIN_FRAME_COUNTER++;

			// Process all gamepad input
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			{
				struct NativeReplaySchedulerFrameInfo replayFrameInfo = MainReplayScheduler_FrameInfo(MAIN_GAME_TRACKER);

				if (NativeReplayScheduler_BeginFrame(&replayFrameInfo) != 0)
				{
					return 0;
				}
				NativeSaveState_BeginFrame();
			}
			{
				struct NativePerfFrameInfo perfFrameInfo = MainPerf_FrameInfo(MAIN_GAME_TRACKER);

				NativePerf_BeginFrame(&perfFrameInfo);
			}
#endif
			GAMEPAD_ProcessAnyoneVars(MAIN_GAMEPADS);

			// Start new frame (ClearOTagR)
#ifdef CTR_NATIVE
			MainFrame_ResetDB(MAIN_GAME_TRACKER);
#else
			// retail passes the gamepads too
			((void (*)(struct GameTracker *, struct GamepadSystem *))MainFrame_ResetDB)(MAIN_GAME_TRACKER, MAIN_GAMEPADS);
#endif

			if (
			    // If you're in Demo Mode
			    (MAIN_GAME_TRACKER->boolDemoMode != 0) &&

			    (
			        // Turn off HUD
			        MAIN_GAME_TRACKER->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK,
			        // if game is not loading
			        MAIN_LOADING_STAGE == LOAD_IDLE))
			{
				// All this code is for the 30-second timer within Demo Mode
				// To see 30-second timer in Main Menu, go to FUN_00001604 in 230.c
				// pressing (or holding) any button sets it to zero

				MAIN_GAME_TRACKER->demoCountdownTimer--;

				// check to see if time ran out
				if (MAIN_GAME_TRACKER->demoCountdownTimer < 1)
				{
					// leave demo mode, go to main menu
					MAIN_GAME_TRACKER->boolDemoMode = 0;
					MAIN_GAME_TRACKER->numPlyrNextGame = 1;
					MAIN_MENU_STATE = MAIN_MENU_TITLE;
					MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
				}

				// if time remains on the timer: any button pressed by anyone leaves demo mode
				else if (MAIN_GAMEPADS->anyoneHeldCurr != 0)
				{
					MAIN_GAME_TRACKER->boolDemoMode = 0;
					MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
				}

				// Retail writes the call twice, once per height: near the top of the screen for one player,
				// halfway down for more.
				if (MAIN_GAME_TRACKER->numPlyrCurrGame == 1)
				{
					DecalFont_DrawMultiLine(MAIN_LNG_STRINGS[LNG_DEMO_MODE_PRESS_ANY_BUTTON_TO_EXIT], 0x100, 0x23, 0x200, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
				}
				else
				{
					DecalFont_DrawMultiLine(MAIN_LNG_STRINGS[LNG_DEMO_MODE_PRESS_ANY_BUTTON_TO_EXIT], 0x100, 100, 0x200, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
				}
			}

			if ((MAIN_GAME_TRACKER->gameMode1 & LOADING) == 0)
			{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
				NativePerf_BeginScope(NATIVE_PERF_BUCKET_GAME_LOGIC);
#endif
				MainFrame_GameLogic(MAIN_GAME_TRACKER, MAIN_GAMEPADS);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
				NativePerf_EndScope(NATIVE_PERF_BUCKET_GAME_LOGIC);
#endif
			}

			// If you are in demo mode
			if (MAIN_GAME_TRACKER->boolDemoMode != '\0')
			{
				// Turn off HUD
				MAIN_GAME_TRACKER->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK;
			}

			// reset vsync calls between drawsync
			MAIN_GAME_TRACKER->vSync_between_drawSync = 0;


#ifdef CTR_NATIVE
			Platform_BeginFrame();
#endif
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDER_FRAME);
#endif
			MainFrame_RenderFrame(MAIN_GAME_TRACKER, MAIN_GAMEPADS);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDER_FRAME);
#endif
#ifdef CTR_NATIVE
			Platform_EndFrame();
#endif


			// if mask is talking in Adventure Hub
			if (sdata->boolDraw3D_AdvMask != 0)
			{
				AH_MaskHint_Update();
			}
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			{
				struct NativeReplaySchedulerFrameInfo replayFrameInfo = MainReplayScheduler_FrameInfo(MAIN_GAME_TRACKER);

				if (NativeReplayScheduler_EndFrame(&replayFrameInfo) != 0)
				{
					return 0;
				}
			}
			{
				struct NativePerfFrameInfo perfFrameInfo = MainPerf_FrameInfo(MAIN_GAME_TRACKER);

				NativePerf_EndFrame(&perfFrameInfo);
			}
#endif
			break;

#ifndef CTR_NATIVE
		// In theory, this is left over from the demos,
		// which would "timeout" and restart after sitting idle
		case 4:

			// erase all data past the
			// last 3 bookmarks, if there
			// that many exist
			MEMPACK_PopState();
			MEMPACK_PopState();
			MEMPACK_PopState();

			CTR_ErrorScreen(0, 0, 0);
			Music_Stop();

			// clear backup, destroy music, destroy all fx
			howl_StopAudio(1, 1, 1);
			Bank_DestroyAll();
			howl_Disable();

			// retail passes the gamepads
			((void (*)(struct GamepadSystem *))GAMEPAD_SetMainMode)(MAIN_GAMEPADS);

			// Set vsync to 2 FPS
			VSync(30);

			// reboot game
			MAIN_GAME_STATE_WRITE = 0;
#endif
		}
	}

#ifndef CTR_NATIVE
	// wont happen under normal conditions
	MainKillGame_StopCTR();
	return 0;
#endif
}
