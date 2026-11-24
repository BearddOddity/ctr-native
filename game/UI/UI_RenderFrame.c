#include <common.h>

// To do: add a header

// CTR_NATIVE only adds an ST1 map-metadata null guard below.
void UI_RenderFrame_Racing()
{
	s16 sVar1;
	s16 sVar2;
	u8 bVar3;
	int partTimeVariable1;
	u32 *ptrColor;
	char *pbVar6;
	int i;
	struct PushBuffer *pb;
	u32 partTimeVariable5;
	struct Icon *iconPtr;
	u32 *primMemCurr;
	char *fmt;
	POLY_G4 *TurboCounterBar;
	s16 sVar17;
	struct Driver *playerStruct;
	struct UiElement2D *hudStructPtr;
	struct UIMap *levPtrMap;
	char cVar22;
	SVec2 wumpaModelPos;
	SVec2 letterCtrPos;
	char string[24];
	SVec2 turboCountPos;
	s16 local_30[2];
	struct Thread *playerThread;
	struct DB *backBuffer;
	struct Thread *turboThread;
	struct Turbo *turboThreadObject;
	u32 mapPosX;
	u32 mapPosY;
	int rank;
#ifdef CTR_NATIVE
	struct GameTracker *gGT;
	int raceMode1;
	int numPlyr;
#else
	// Retail keeps none of the game tracker, its gameMode1 or its player count in a register: it reads each again
	// at each use.
#define gGT       GAME_TRACKER
#define raceMode1 (GAME_TRACKER->gameMode1)
#define numPlyr   (GAME_TRACKER->numPlyrCurrGame)
#endif


#ifdef CTR_NATIVE
	gGT = GAME_TRACKER;
	raceMode1 = gGT->gameMode1;
	numPlyr = gGT->numPlyrCurrGame;
#endif

	// Get pointer to array of HUD structs
	hudStructPtr = (struct UiElement2D *)GAME_HUD_LAYOUTS[numPlyr - 1];

	levPtrMap = 0;

#ifdef CTR_NATIVE
	// adding this here so the compiler doesn't complain (retail sets neither here)
	wumpaModelPos.x = 0;
	wumpaModelPos.y = 0;
	turboCountPos.x = 0;
	turboCountPos.y = 0;
#endif

	UI_WeaponBG_AnimateShine();

	// if time on clock is zero
	if (gGT->elapsedEventTime == 0)
	{
		// a counter of its own: retail keeps it in a register no call outlives
		for (rank = 0; rank < 8; rank++)
		{
			GAME_DATA_NAME(rankIconsTransitionTimer)[rank] = 0;

			pbVar6 = &GAME_SDATA_NAME(kartSpawnOrderArray)[rank];

			GAME_DATA_NAME(rankIconsDesired)[rank] = (u16)*pbVar6;
			GAME_DATA_NAME(rankIconsCurr)[rank] = (u16)*pbVar6;
		}
	}

	// If not drawing intro-race cutscene
	if ((raceMode1 & START_OF_RACE) == 0)
	{
		if ((gGT->hudFlags & HUD_FLAG_RENDER_LESS) != 0)
		{
			gGT->hudFlags &= HUD_FLAG_CLEAR_RENDER_LESS_MASK;
		}
		else
		{
			// If you press Triangle
			if ((GAMEPADS->gamepad[0].buttonsTapped & 0x40000) != 0)
			{
				// if & 8, remove bit 8,
				// if !& 8, add bit 8,
				// toggle map and speedometer
				GAME_SDATA_NAME(HudAndDebugFlags) ^= 8;
			}
		}
	}

	// numPlyrCurrGame is 0
	if ((numPlyr == '\0') &&

	    ((gGT->drivers[0]->actionsFlagSet & ACTION_BOT) != 0))
	{
		// force draw speedometer, and not map, why?
		GAME_SDATA_NAME(HudAndDebugFlags) = 8;
	}

#ifdef CTR_NATIVE
	// NOTE(aalhendi): Native can load levels before ST1 map metadata is present.
	if ((gGT->level1->ptrSpawnType1 != 0) && (gGT->level1->ptrSpawnType1->count != 0))
#else
	if (gGT->level1->ptrSpawnType1->count != 0)
#endif
	{
		void **pointers = ST1_GETPOINTERS(gGT->level1->ptrSpawnType1);
		levPtrMap = pointers[ST1_MAP];
	}

	// If you are not in Relic Race, and not in battle mode,
	// and not in time trial
	if ((raceMode1 & (RELIC_RACE | TIME_TRIAL | BATTLE_MODE)) == 0)
	{
		UI_DrawRankedDrivers();
	}

	// pointer to first Player thread
	playerThread = gGT->threadBuckets[PLAYER].thread;

	cVar22 = '\0';
	if (playerThread != 0)
	{
		// Loop through all player threads (a label and a goto: retail's loop gets no strength reduction, which a
		// do-while would give it)
	PlayerLoop:
		{
			// pointer to player structure
			playerStruct = (struct Driver *)playerThread->object;
			// if racer has travelled wrong way for too long (retail compares the distance signed, and
			// tests this first, with the jump meter after it)
			if (((s32)playerStruct->distanceDrivenBackwards >= 0x1f5) &&
			    ((playerStruct->actionsFlagSet & ACTION_DRIVING_WRONG_WAY) != 0))
			{
				if ((raceMode1 & PAUSE_ALL) == 0)
				{
					pb = &gGT->pushBuffer[playerStruct->driverID];

					// if "Time on clock" last 0xXX u8 is greater than 0x80 and less than 0xFF
					if ((gGT->elapsedEventTime & 0x80) != 0)
					{
						DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_WRONG_WAY],

						                   // midpointX
						                   pb->rect.x + (pb->rect.w >> 1),

						                   // midpointY, 0x1e higher
						                   pb->rect.y + (pb->rect.h >> 1) - 0x1e,

						                   FONT_BIG, (JUSTIFY_CENTER | ORANGE));
					}

					// The text will not show if the last u8 is more than 0x00 and less than 0x7F.
					// This is what makes the text flicker, rather than drawing solid

					cVar22 = '\x01';
				}
			}

			if ((raceMode1 & PAUSE_ALL) == 0)
			{
				// execute Jump meter and landing boost processes
				UI_JumpMeter_Update(playerStruct);
			}

			if (
			    // numPlyrCurrGame is less than 2 (1P mode)
			    (numPlyr < 2) &&
			    // if want to draw speedometer
			    ((GAME_SDATA_NAME(HudAndDebugFlags) & 8) != 0))
			{
				UI_DrawSpeedNeedle(hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].x, hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].y, playerStruct);
				UI_JumpMeter_Draw(hudStructPtr[UI_HUD_SLOT_JUMP_METER].x, hudStructPtr[UI_HUD_SLOT_JUMP_METER].y, playerStruct);
				UI_DrawSlideMeter(hudStructPtr[UI_HUD_SLOT_SLIDE_METER].x, hudStructPtr[UI_HUD_SLOT_SLIDE_METER].y, playerStruct);
				UI_DrawSpeedBG(hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].x, hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].y, playerStruct);
			}

			if ((playerStruct->actionsFlagSet & ACTION_RACE_FINISHED) == 0)
			{
				// If you're not in Battle Mode
				if ((raceMode1 & BATTLE_MODE) == 0)
				{
					// Draw powerslide meter
					UI_DrawSlideMeter(hudStructPtr[UI_HUD_SLOT_SLIDE_METER].x, hudStructPtr[UI_HUD_SLOT_SLIDE_METER].y, playerStruct);
				}

				// If you are not in Time Trial or Relic Race
				if ((raceMode1 & (TIME_TRIAL | RELIC_RACE)) == 0)
				{
					UI_DrawNumWumpa(hudStructPtr[UI_HUD_SLOT_WUMPA_COUNT].x, hudStructPtr[UI_HUD_SLOT_WUMPA_COUNT].y, playerStruct);
				}
			}

			// If you're in a Relic Race
			if ((raceMode1 & RELIC_RACE) != 0)
			{
				UI_DrawNumTimebox(hudStructPtr[UI_HUD_SLOT_TIMEBOX].x, hudStructPtr[UI_HUD_SLOT_TIMEBOX].y, playerStruct);
			}

			// If game is not paused
			if ((raceMode1 & PAUSE_ALL) == 0)
			{
				if (playerStruct->PickupWumpaHUD.numCollected != 0)
				{
					struct Icon **iconPtrArray;

					wumpaModelPos.x = hudStructPtr[UI_HUD_SLOT_FRUIT_MODEL].x;
					wumpaModelPos.y = hudStructPtr[UI_HUD_SLOT_FRUIT_MODEL].y;

					// retail tests a running cooldown first
					if (playerStruct->PickupWumpaHUD.cooldown != 0)
					{
						UI_Lerp2D_HUD(CTR_VECTOR_DATA(&(wumpaModelPos)), (int)playerStruct->PickupWumpaHUD.startX, (int)playerStruct->PickupWumpaHUD.startY,
						              hudStructPtr[UI_HUD_SLOT_FRUIT_MODEL].x, hudStructPtr[UI_HUD_SLOT_FRUIT_MODEL].y, playerStruct->PickupWumpaHUD.cooldown,
						              5);

						// subtract one from timer
						playerStruct->PickupWumpaHUD.cooldown--;
					}

					// cooldown between items is over
					else
					{
						// deduct from number of queued items to pick up
						playerStruct->PickupWumpaHUD.numCollected--;

						if (LOAD_IsOpen_RacingOrBattle() &&

						    // If you're not in Adventure Arena
						    ((raceMode1 & ADVENTURE_ARENA) == 0))
						{
							RB_Player_ModifyWumpa(playerStruct, 1);
						}

						// OtherFX_Play to get wumpa fruit
						OtherFX_Play(0x42, 1);

						// more queued: start the timer again
						if (playerStruct->PickupWumpaHUD.numCollected != 0)
						{
							playerStruct->PickupWumpaHUD.cooldown = 5;
						}
					}

					iconPtrArray = ICONGROUP_GETICONS(gGT->iconGroup[0xB]);

					// "wumpaposter" icon group
					DecalHUD_DrawPolyFT4(iconPtrArray[0], (int)wumpaModelPos.x, (int)wumpaModelPos.y,

					                     // pointer to PrimMem struct
					                     &gGT->backBuffer->primMem,

					                     // pointer to OT memory
					                     gGT->pushBuffer_UI.ptrOT,

					                     0, hudStructPtr[UI_HUD_SLOT_WEAPON].scale);
				}

				if (playerStruct->PickupLetterHUD.cooldown != 0)
				{
					struct Instance *curr;

					// Retail makes the letter visible and places it in each branch of its own.
					// C-Letter
					if (playerStruct->PickupLetterHUD.modelID == STATIC_C)
					{
						curr = sdata->ptrHudC;
						curr->flags &= ~HIDE_MODEL;
						letterCtrPos.x = hudStructPtr[UI_HUD_SLOT_TOKEN_OR_CTR].x;
						letterCtrPos.y = hudStructPtr[UI_HUD_SLOT_TOKEN_OR_CTR].y;
					}

					// T-letter
					else if (playerStruct->PickupLetterHUD.modelID == STATIC_T)
					{
						curr = sdata->ptrHudT;
						curr->flags &= ~HIDE_MODEL;
						letterCtrPos.x = hudStructPtr[UI_HUD_SLOT_TOKEN_OR_CTR].x + 0x1d;
						letterCtrPos.y = hudStructPtr[UI_HUD_SLOT_TOKEN_OR_CTR].y - 1;
					}

					// R-Letter
					else
					{
						curr = sdata->ptrHudR;
						curr->flags &= ~HIDE_MODEL;
						letterCtrPos.x = hudStructPtr[UI_HUD_SLOT_TOKEN_OR_CTR].x + 0x3a;
						letterCtrPos.y = hudStructPtr[UI_HUD_SLOT_TOKEN_OR_CTR].y;
					}

					// reduce frame counter
					playerStruct->PickupLetterHUD.cooldown--;

					// PickupLetterHUD.startX and PickupLetterHUD.startY are start position of animation

					// Interpolate from start pos to end pos
					UI_Lerp2D_HUD(CTR_VECTOR_DATA(&(letterCtrPos)), playerStruct->PickupLetterHUD.startX, playerStruct->PickupLetterHUD.startY,
					              (int)letterCtrPos.x, (int)letterCtrPos.y, (int)playerStruct->PickupLetterHUD.cooldown, 10);

					curr->matrix.t[0] = UI_ConvertX_2((int)letterCtrPos.x, 0x200);
					curr->matrix.t[1] = UI_ConvertY_2((int)letterCtrPos.y, 0x200);
					curr->matrix.t[2] = 0x200;
				}
			}

			// if you are in relic mode (retail tests this case first)
			if ((raceMode1 & RELIC_RACE) != 0)
			{
				// If you smashed a time crate, this variable is set to 10
				if (playerStruct->PickupTimeboxHUD.cooldown != 0)
				{
					// DAT_8008d530
					// -%ld

					// Make string with number of time crate
					// print "-x" where x is the amount of seconds
					sprintf(&string[0], &GAME_SDATA_NAME(s_subtractLongInt)[0], gGT->timeCrateTypeSmashed);

					// 4b4 and 4b6 are WindowStartPos(x,y) from PushBuffer, inside Driver
					UI_Lerp2D_HUD(CTR_VECTOR_DATA(&(wumpaModelPos)), playerStruct->PickupTimeboxHUD.startX, playerStruct->PickupTimeboxHUD.startY, 0x14, 8,
					              playerStruct->PickupTimeboxHUD.cooldown, 10);

					// Decrease remaining number of frames for this to be on screen
					playerStruct->PickupTimeboxHUD.cooldown--;

					// Put string on the screen
					// This happens for 10 frames
					DecalFont_DrawLine(&string[0], (int)wumpaModelPos.x, (int)wumpaModelPos.y, FONT_BIG, PERIWINKLE);
				}
			}

			// If you're not in a Relic Race
			else
			{
				if ((playerStruct->actionsFlagSet & ACTION_RACE_FINISHED) == 0)
				{
					// Draw weapon and number of wumpa fruit in HUD
					UI_Weapon_DrawSelf(hudStructPtr[UI_HUD_SLOT_WEAPON].x, hudStructPtr[UI_HUD_SLOT_WEAPON].y, hudStructPtr[UI_HUD_SLOT_WEAPON].scale,
					                   playerStruct);
				}
			}

			// === Naughty Dog Bug ===
			// This block will never execute in Life Limit,
			// even though original code has a block inside this IF
			// that checks for Life Limit

			// if you're in battle mode, while not paused
			// and you do not have a life limit
			if ((raceMode1 & (LIFE_LIMIT | BATTLE_MODE | PAUSE_ALL)) == BATTLE_MODE)
			{
				// if the animation is not done (retail tests this case first)
				if (playerStruct->BattleHUD.cooldown != 0)
				{
					wumpaModelPos.x = hudStructPtr[UI_HUD_SLOT_BATTLE_SCORE].x + 0x20;
					wumpaModelPos.y = hudStructPtr[UI_HUD_SLOT_BATTLE_SCORE].y;

					partTimeVariable1 = playerStruct->BattleHUD.scoreDelta;

					if ((raceMode1 & LIFE_LIMIT) != 0)
					{
						fmt = &GAME_SDATA_NAME(s_subtractLongInt)[0];
					}
					else if (partTimeVariable1 > 0)
					{
						fmt = &GAME_SDATA_NAME(s_additionLongInt)[0];
					}
					else
					{
						fmt = &GAME_SDATA_NAME(s_subtractInt)[0];

						if (partTimeVariable1 < 0)
						{
							partTimeVariable1 = -partTimeVariable1;
						}
					}

					sprintf((char *)&string[0], fmt, partTimeVariable1);

					UI_Lerp2D_HUD(CTR_VECTOR_DATA(&(wumpaModelPos)), (int)playerStruct->BattleHUD.startX, (int)playerStruct->BattleHUD.startY,
					              (int)(hudStructPtr[UI_HUD_SLOT_BATTLE_SCORE].x + 0x20), (int)(hudStructPtr[UI_HUD_SLOT_BATTLE_SCORE].y + 8),
					              playerStruct->BattleHUD.cooldown, 5);

					// subtract one from the number of frames that the animation lasts
					playerStruct->BattleHUD.cooldown--;

					// print the string that shows the change in your score
					DecalFont_DrawLine((char *)&string[0], (int)wumpaModelPos.x, (int)wumpaModelPos.y, FONT_SMALL, RED);
				}

				// the animation for adding points is over
				else
				{
					// Delete the change that in score that was queued
					playerStruct->BattleHUD.scoreDelta = 0;
				}
			}

			if ((raceMode1 & BATTLE_MODE) == 0)
			{
				if ((playerStruct->actionsFlagSet & ACTION_RACE_FINISHED) == 0)
				{
					UI_DrawLapCount(hudStructPtr[UI_HUD_SLOT_LAP_COUNT].x, hudStructPtr[UI_HUD_SLOT_LAP_COUNT].y,
					                (u32)hudStructPtr[UI_HUD_SLOT_LAP_COUNT].scale, playerStruct);
				}
			}
			else
			{
				// Draw how many points or lifes the player has
				UI_DrawBattleScores((int)hudStructPtr[UI_HUD_SLOT_BATTLE_SCORE].x, (int)hudStructPtr[UI_HUD_SLOT_BATTLE_SCORE].y, playerStruct);
			}

			if (((raceMode1 & (ARCADE_MODE | ADVENTURE_MODE)) != 0) && ((playerStruct->actionsFlagSet & ACTION_RACE_FINISHED) != 0))
			{
				AA_EndEvent_DisplayTime((u32)playerStruct->driverID, 0);
			}

			partTimeVariable5 = raceMode1;

			// If you are in Relic Race, and not in battle mode, and not in time trial
			if ((partTimeVariable5 & 0x4020020) == 0)
			{
				if (((playerStruct->actionsFlagSet & ACTION_RACE_FINISHED) == 0) || ((
				                                                                        // if numPlyrCurrGame is 2
				                                                                        numPlyr == '\x02' &&

				                                                                        // AND

				                                                                        // Not Arcade Mode (must be VS or Battle)
				                                                                        ((partTimeVariable5 & 0x400000) == 0))))
				{
					sVar17 = 0;
					partTimeVariable5 = 0;
				}
				else
				{
					// if numPlyrCurrGame is less than 3
					if (numPlyr < 3)
					{
						goto LAB_80053af4;
					}

					bVar3 = (gGT->timer & 1) == 0;
					sVar17 = (u16)bVar3 << 2;
					partTimeVariable5 = ((u32)bVar3 << 0x12) >> 0x10;
				}

				sVar1 = hudStructPtr[UI_HUD_SLOT_RANK].x;
				sVar2 = hudStructPtr[UI_HUD_SLOT_RANK].y;
				UI_DrawPosSuffix(sVar1, sVar2, playerStruct, (s16)partTimeVariable5);

				if (numPlyr > 2)
				{
					// retail reads the icon and each color word straight from their tables, in the call itself
					DecalHUD_DrawPolyGT4(
					    // icon pointer, specifically for the big rank icons that start at 0x19
					    gGT->ptrIcons[(int)playerStruct->driverRank + 0x19],

					    // position
					    (int)hudStructPtr[UI_HUD_SLOT_BIG1].x, (int)hudStructPtr[UI_HUD_SLOT_BIG1].y,

					    &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT,

					    // color data
					    ColorCode_Load(&GAME_DATA_NAME(ptrColor)[sVar17][0]), ColorCode_Load(&GAME_DATA_NAME(ptrColor)[sVar17][1]),
					    ColorCode_Load(&GAME_DATA_NAME(ptrColor)[sVar17][2]), ColorCode_Load(&GAME_DATA_NAME(ptrColor)[sVar17][3]),

					    0, FP(1.0));
				}
			}

			// If you're in end-of-race and Battle
			else if ((partTimeVariable5 & 0x200020) == 0x200020)
			{
				partTimeVariable5 = (u32)((gGT->timer & 1) == 0) << 2;

				// Draw the "st", "nd", "rd" suffix after "1st, 2nd, 3rd, etc"
				UI_DrawPosSuffix(hudStructPtr[UI_HUD_SLOT_RANK].x, hudStructPtr[UI_HUD_SLOT_RANK].y, playerStruct, (s16)partTimeVariable5);

				// retail sets this call's arguments up again here and shares only the call itself;
				// the icon is the rank icon of each battle team after battle is over
				DecalHUD_DrawPolyGT4(
				    gGT->ptrIcons[gGT->battleSetup.finishedRankOfEachTeam[playerStruct->BattleHUD.teamID] + 0x19],
				    (int)hudStructPtr[UI_HUD_SLOT_BIG1].x, (int)hudStructPtr[UI_HUD_SLOT_BIG1].y, &gGT->backBuffer->primMem,
				    gGT->pushBuffer_UI.ptrOT, ColorCode_Load(&GAME_DATA_NAME(ptrColor)[partTimeVariable5][0]),
				    ColorCode_Load(&GAME_DATA_NAME(ptrColor)[partTimeVariable5][1]),
				    ColorCode_Load(&GAME_DATA_NAME(ptrColor)[partTimeVariable5][2]),
				    ColorCode_Load(&GAME_DATA_NAME(ptrColor)[partTimeVariable5][3]), 0, FP(1.0));
			}
		LAB_80053af4:

			// draw lock-on target for driver, if
			// a missile or warpball is chasing them
			UI_TrackerSelf(playerStruct);

			// If you're in Battle
			if ((raceMode1 & BATTLE_MODE) != 0)
			{
				// Draw arrows over the heads of other players (not AIs)
				UI_BattleDrawHeadArrows(playerStruct);
			}

			if ((playerStruct->numWumpas >= DRIVER_WUMPA_JUICED_COUNT) && ((playerStruct->actionsFlagSet & ACTION_RACE_FINISHED) == 0))
			{
				// draw shining background behind wumpa fruit
				UI_Weapon_DrawBG(hudStructPtr[UI_HUD_SLOT_RACING_WEAPON_BG].x, hudStructPtr[UI_HUD_SLOT_RACING_WEAPON_BG].y,
				                 hudStructPtr[UI_HUD_SLOT_RACING_WEAPON_BG].scale, playerStruct);

				// If your weapon is not "no weapon"
				if (playerStruct->heldItemID != HELD_ITEM_NONE)
				{
					// draw shining background behind weapon
					UI_Weapon_DrawBG(hudStructPtr[UI_HUD_SLOT_BATTLE_WEAPON_BG].x, hudStructPtr[UI_HUD_SLOT_BATTLE_WEAPON_BG].y,
					                 hudStructPtr[UI_HUD_SLOT_BATTLE_WEAPON_BG].scale, playerStruct);
				}
			}

			// go to next player
			// thread = thread->sibling
			playerThread = playerThread->siblingThread;

			hudStructPtr += UI_HUD_SLOT_COUNT;

		}
		if (playerThread != 0)
		{
			goto PlayerLoop;
		}
	}

	if (sdata->WrongWayDirection_bool != cVar22)
	{
		sdata->framesDrivingSameDirection = 0;
		sdata->WrongWayDirection_bool = cVar22;
	}

	sdata->framesDrivingSameDirection++;

	if (numPlyr == 1)
	{
		playerStruct = gGT->drivers[0];

		UI_DrawRaceClock(0x14, 8, UI_RACE_CLOCK_SHOW_CURRENT_TIME, playerStruct);

		turboThread = 0;
		turboThreadObject = 0;

		if ((gGT->gameMode2 & CHEAT_TURBOCOUNT) != 0)
		{
			// If number of boosts is not zero (retail reads the count again at each test, signed)
			if (playerStruct->numTurbos != 0)
			{
				// Read pointer from address
				turboThread = gGT->threadBuckets[TURBO].thread;

				while (
				    // Pointer != nullptr
				    turboThread != 0 &&

				    // Adds 0x30 to turboThread pointer, gets value (new address) then adds 4.
				    // If   [something]   != pointer that holds boost counter
				    (turboThreadObject = (struct Turbo *)turboThread->object, turboThreadObject->driver != playerStruct))
				{
					turboThread = turboThread->siblingThread;
				}
			}

			// GAME_SDATA_NAME(TurboDisplayPos_Only1P)
			// Position of counter
			// 0 = offscreen
			// 1 = just barely on screen
			// ...
			// 10 = clearly on screen

			// Retail stores the position only when it moves: +1 sliding on (3+ boosts, not fully on screen, a turbo
			// thread found), -1 sliding off (no turbo thread, or fewer than 3 boosts), each tested in this order.
			if ((playerStruct->numTurbos >= 3) && (GAME_SDATA_NAME(TurboDisplayPos_Only1P) < 10))
			{
				// If pointer == nullptr
				if (turboThread == 0)
				{
					goto LAB_80053c98;
				}

				// Animate counter to move onto screen
				GAME_SDATA_NAME(TurboDisplayPos_Only1P) = GAME_SDATA_NAME(TurboDisplayPos_Only1P) + 1;
			}
			else
			{
				// If pointer == nullptr
				if (turboThread == 0)
				{
				LAB_80053c98:
					// If counter is on screen
					if (GAME_SDATA_NAME(TurboDisplayPos_Only1P) > 0)
					{
						goto LAB_80053cd4;
					}
				}

				if (
				    // If number boosts is less than 3
				    (playerStruct->numTurbos < 3) &&

				    // If turbo counter is on screen
				    (GAME_SDATA_NAME(TurboDisplayPos_Only1P) > 0))
				{
				LAB_80053cd4:

					// Animate counter to move off screen
					GAME_SDATA_NAME(TurboDisplayPos_Only1P) = GAME_SDATA_NAME(TurboDisplayPos_Only1P) - 1;
				}
			}

			// If display counter is on screen (fully or not fully)
			if (GAME_SDATA_NAME(TurboDisplayPos_Only1P) != 0)
			{
				// Interpolate the turbo counter slide in from the right
				UI_Lerp2D_Linear(CTR_VECTOR_DATA(&(turboCountPos)), 0x2c8, 0x20, 500, 0x20, GAME_SDATA_NAME(TurboDisplayPos_Only1P), 10);

				// The actual counter number will continue to
				// increase past 1000, but the on-screen text
				// will cap at 999

				// If you have less than 1000 boosts
				if (playerStruct->numTurbos < 1000)
				{
					// DAT_8008d54c
					// %d

					// build string for on-screen boost counter
					sprintf((char *)&string[0], &sdata->s_intSpace[0], playerStruct->numTurbos);
				}

				// If you have more than 1000 boosts
				else
				{
					// 8d544
					// "999" <-- ascii string, not 2-u8 value

					// Cap the on-screen counter at 999
					sprintf((char *)&string[0], &sdata->s_999[0]);
				}

				i = DecalFont_GetLineWidth(GAME_LANGUAGE_STRINGS[LNG_TURBOS], 1);

				// Draw the string
				DecalFont_DrawLine((char *)&string[0], (int)(((u32)turboCountPos.x - i) * 0x10000) >> 0x10, (int)turboCountPos.y, FONT_BIG,
				                   (JUSTIFY_RIGHT | ORANGE_RED));


				sprintf((char *)&string[0], &sdata->s_str[0], GAME_LANGUAGE_STRINGS[LNG_TURBOS]);

				// Draw the string
				DecalFont_DrawLine((char *)&string[0], (int)(s16)turboCountPos.x, (int)turboCountPos.y, FONT_BIG, (JUSTIFY_RIGHT | ORANGE));

				backBuffer = gGT->backBuffer;
				primMemCurr = backBuffer->primMem.cursor;
				TurboCounterBar = 0;

				if ((int)primMemCurr <= (int)backBuffer->primMem.guardEnd)
				{
					backBuffer->primMem.cursor = primMemCurr + 9;
					TurboCounterBar = (POLY_G4 *)primMemCurr;
				}

				if (TurboCounterBar == 0)
				{
					return;
				}

				CtrGpu_WriteColorCode(&TurboCounterBar->r0, 0x3800c8ff);
				CtrGpu_WriteColorCode(&TurboCounterBar->r1, 0x3800c8ff);
				CtrGpu_WriteColorCode(&TurboCounterBar->r2, 0x380000ff);
				CtrGpu_WriteColorCode(&TurboCounterBar->r3, 0x380000ff);
				TurboCounterBar->x0 = turboCountPos.x - 0xaa;
				TurboCounterBar->y0 = turboCountPos.y + 9;
				TurboCounterBar->x1 = turboCountPos.x + 0x32;
				TurboCounterBar->y1 = turboCountPos.y + 9;
				TurboCounterBar->x2 = turboCountPos.x - 0x96;
				TurboCounterBar->y2 = turboCountPos.y + 0x12;
				TurboCounterBar->x3 = turboCountPos.x + 0x32;
				TurboCounterBar->y3 = turboCountPos.y + 0x12;

				// pointer to OT memory
				primMemCurr = gGT->pushBuffer_UI.ptrOT;

				addPolyG4(primMemCurr, TurboCounterBar);
			}
		}
	}

	// if numPlyrCurrGame is not 1 (multiplayer)
	else
	{
		// if you have a time limit (battle)
		if ((raceMode1 & TIME_LIMIT) != 0)
		{
			// draw countdown clock
			UI_DrawLimitClock(0xd7, 0x68, 2);
		}
	}

	if (levPtrMap != 0)
	{
		if (((numPlyr == 1)

		     // if want to draw map, not speedometer
		     && (GAME_SDATA_NAME(HudAndDebugFlags) & 8) == 0) ||

		    (numPlyr == 3))
		{
			local_30[0] = 0;

			UI_Map_DrawDrivers(levPtrMap, gGT->threadBuckets[PLAYER].thread, local_30);
			UI_Map_DrawDrivers(levPtrMap, gGT->threadBuckets[ROBOT].thread, local_30);

			UI_Map_DrawGhosts(levPtrMap, gGT->threadBuckets[GHOST].thread);

			UI_Map_DrawTracking(levPtrMap, gGT->threadBuckets[TRACKING].thread);

			// Draw the map (top half and bottom half, X and Y, primMem, OT, white): retail sets the call up once
			// per player count and shares only the call itself
			if (numPlyr == 1)
			{
				UI_Map_DrawMap(gGT->ptrIcons[3], gGT->ptrIcons[4], 500, 195, &gGT->backBuffer->primMem,
				               gGT->pushBuffer_UI.ptrOT, 1);
			}
			else if (numPlyr == 3)
			{
				UI_Map_DrawMap(gGT->ptrIcons[3], gGT->ptrIcons[4], 440, 205, &gGT->backBuffer->primMem,
				               gGT->pushBuffer_UI.ptrOT, 1);
			}
		}
	}

	bVar3 = false;

	// loop counter
	i = 0;

	// if numPlyrCurrGame is not 0
	if (numPlyr != '\0')
	{
		// for(int i = 0; i < numPlyrCurrGame; i++)
		do
		{
			playerStruct = gGT->drivers[i];
			pb = &gGT->pushBuffer[playerStruct->driverID];

			if ((((playerStruct->actionsFlagSet & ACTION_RACE_FINISHED) != 0) && ((raceMode1 & (ARCADE_MODE | TIME_TRIAL)) == 0)) &&
			    ((
			        // cooldown is finished
			        gGT->timerEndOfRaceVS == 0 ||

			        // cooldown has not progressed far,
			        // so you still need to draw "Finished" and "Loser"
			        (0x96 < gGT->timerEndOfRaceVS))))
			{
				if (
				    // player structure + 0x482 is your rank in the race
				    // 0 = 1st place, 1 = 2nd place, 2 = 3rd place, etc

				    // Basically, out of all human players, if you did not come in last
				    ((int)playerStruct->driverRank < (int)numPlyr - 1) &&

				    // If you're not in Battle Mode
				    // (winner of battle wont use this function)
				    ((raceMode1 & BATTLE_MODE) == 0))
				{
					// retail sets each case's call up whole and shares only the call itself
					DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_FINISHED],
					                   pb->rect.x + (pb->rect.w >> 1),        // midpointX
					                   pb->rect.y + (pb->rect.h >> 1) - 0x1e, // midpointY
					                   FONT_BIG, (JUSTIFY_CENTER | ORANGE));
				}

				// If you came in last place, or you're in battle
				else
				{
					DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_LOSER],
					                   pb->rect.x + (pb->rect.w >> 1),        // midpointX
					                   pb->rect.y + (pb->rect.h >> 1) - 0x1e, // midpointY
					                   FONT_BIG, (JUSTIFY_CENTER | ORANGE));
				}

				if (
				    // If you press Cross or Start
				    ((GAMEPADS->gamepad[i].buttonsTapped & 0x1010) != 0) &&

				    // If you're in End-Of-Race menu
				    ((raceMode1 & END_OF_RACE) != 0))
				{
					// make "Finished" and "Loser" disappear, start
					// drawing the on-screen comments
					gGT->timerEndOfRaceVS = 0x96;
				}
			}

			// if item roll is not done
			if (playerStruct->itemRollTimer != 0)
			{
				bVar3 = true;
			}

			// increment the iteration counter
			i++;

			// for(int i = 0; i < numPlyrCurrGame; i++)
		} while (i < (int)numPlyr);
	}
	if ((
	        // If game is not paused
	        ((raceMode1 & PAUSE_ALL) == 0) &&

	        // item roll is done
	        (!bVar3)) &&

	    // If you're drawing Weapon Roulette (randomizing)
	    ((raceMode1 & ROLLING_ITEM) != 0))
	{
		// stop weapon shuffle sound
		OtherFX_Stop2(0x5d);

		// disable the randomizing effect in the HUD
		gGT->gameMode1 &= ~ROLLING_ITEM;
	}
#ifndef CTR_NATIVE
#undef gGT
#undef raceMode1
#undef numPlyr
#endif
}

void UI_RenderFrame_AdvHub(void)
{
	struct UiElement2D *hudStructPtr;
	struct GameTracker *gGT;
	struct Driver *player;

	gGT = GAME_TRACKER;
	player = gGT->drivers[0];
	hudStructPtr = GAME_HUD_LAYOUTS[gGT->numPlyrCurrGame - 1];

	UI_DrawNumRelic(hudStructPtr[UI_HUD_SLOT_RELIC].x + 0x10, hudStructPtr[UI_HUD_SLOT_RELIC].y - 10, player);
	UI_DrawNumKey(hudStructPtr[UI_HUD_SLOT_KEY].x + 0x10, hudStructPtr[UI_HUD_SLOT_KEY].y - 10, player);
	UI_DrawNumTrophy(hudStructPtr[UI_HUD_SLOT_TROPHY].x + 0x10, hudStructPtr[UI_HUD_SLOT_TROPHY].y - 10, player);
}

void UI_RenderFrame_CrystChall(void)
{
	struct Driver *player;
	struct UiElement2D *hudStructPtr;
	struct Instance *hudCrystal;
	s32 scaledPos;
	s8 numCrystals;
	SVec2 crystalPos;

	player = GAME_TRACKER->drivers[0];
	hudStructPtr = GAME_HUD_LAYOUTS[GAME_TRACKER->numPlyrCurrGame - 1];

	// If game is not paused
	if ((GAME_TRACKER->gameMode1 & PAUSE_ALL) == 0)
	{
		// execute Jump meter and landing boost processes
		UI_JumpMeter_Update(player);
	}

	UI_DrawSpeedNeedle(hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].x, hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].y, player);

	UI_JumpMeter_Draw(hudStructPtr[UI_HUD_SLOT_JUMP_METER].x, hudStructPtr[UI_HUD_SLOT_JUMP_METER].y, player);

	UI_DrawSlideMeter(hudStructPtr[UI_HUD_SLOT_SLIDE_METER].x, hudStructPtr[UI_HUD_SLOT_SLIDE_METER].y, player);

	UI_DrawSpeedBG(hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].x, hudStructPtr[UI_HUD_SLOT_SPEEDOMETER].y, player);

	UI_DrawNumCrystal(hudStructPtr[UI_HUD_SLOT_CRYSTAL].x + 0x10, hudStructPtr[UI_HUD_SLOT_CRYSTAL].y - 0x10, player);

	// Draw weapon and number of wumpa fruit in HUD
	UI_Weapon_DrawSelf(hudStructPtr[UI_HUD_SLOT_WEAPON].x, hudStructPtr[UI_HUD_SLOT_WEAPON].y, hudStructPtr[UI_HUD_SLOT_WEAPON].scale, player);

	DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_TIME], 0x14, 8, FONT_SMALL, ORANGE);

	// draw countdown clock
	UI_DrawLimitClock(0x14, 0x10, 1);


	// If game is paused
	if ((GAME_TRACKER->gameMode1 & PAUSE_ALL) != 0)
	{
		return;
	}

	if (player->PickupWumpaHUD.numCollected != 0)
	{
		hudCrystal = sdata->ptrHudCrystal;
		crystalPos.x = hudStructPtr[UI_HUD_SLOT_CRYSTAL].x;
		crystalPos.y = hudStructPtr[UI_HUD_SLOT_CRYSTAL].y;

		// make visible
#if defined(CTR_NATIVE)
		if (hudCrystal != NULL)
		{
#endif
			hudCrystal->flags &= ~HIDE_MODEL;
#if defined(CTR_NATIVE)
		}
#endif

		// Animate while cooldown is active; collect once it reaches zero.
		if (player->PickupWumpaHUD.cooldown != 0)
		{
			// interpolate position over course of 5 frames
			UI_Lerp2D_HUD(CTR_VECTOR_DATA(&(crystalPos)), (int)player->PickupWumpaHUD.startX, (int)player->PickupWumpaHUD.startY,
			              (int)hudStructPtr[UI_HUD_SLOT_CRYSTAL].x, (int)hudStructPtr[UI_HUD_SLOT_CRYSTAL].y, player->PickupWumpaHUD.cooldown, 5);

			// reduce cooldown between getting each wumpa (or crystal)
			player->PickupWumpaHUD.cooldown--;
		}
		else
		{
			// NOTE(aalhendi): Publish both pickup fields before callbacks; retain
			// the signed byte count for the completion comparison.
			player->PickupWumpaHUD.numCollected--;
			numCrystals = ++player->numCrystals;

			if (GAME_TRACKER->numCrystalsInLEV <= numCrystals)
			{
				player->funcPtrs[DRIVER_FUNC_INIT] = VehPhysProc_FreezeEndEvent_Init;

				player->actionsFlagSet |= ACTION_RACE_FINISHED;

				MainGameEnd_Initialize();
			}

			OtherFX_Play(0x42, 1);

			if (player->PickupWumpaHUD.numCollected != 0)
			{
				player->PickupWumpaHUD.cooldown = 5;
			}
		}

		// Convert the HUD position to 3D icon coordinates.

		hudCrystal = sdata->ptrHudCrystal;

		// posX
		scaledPos = (crystalPos.x + -0x100) * hudStructPtr[UI_HUD_SLOT_CRYSTAL].z;
		if (scaledPos < 0)
		{
			scaledPos = scaledPos + 0xff;
		}
#if defined(CTR_NATIVE)
		if (hudCrystal != NULL)
#endif
		{
			hudCrystal->matrix.t[0] = scaledPos >> 8;

			// posY
			scaledPos = (crystalPos.y + -0x6c) * hudStructPtr[UI_HUD_SLOT_CRYSTAL].z;
			if (scaledPos < 0)
			{
				scaledPos = scaledPos + 0xff;
			}
			hudCrystal->matrix.t[1] = scaledPos >> 8;

			// posZ
			hudCrystal->matrix.t[2] = hudStructPtr[UI_HUD_SLOT_CRYSTAL].z;
		}
	}
	else
	{
		struct Instance *hiddenCrystal;

		hiddenCrystal = sdata->ptrHudCrystal;
#if defined(CTR_NATIVE)
		// NOTE(aalhendi): Menu-storage can enter crystal HUD flow without
		// a published crystal HUD model.
		if (hiddenCrystal == NULL)
		{
			goto LAB_800545e8;
		}
#endif

		// make invisible
		hiddenCrystal->flags |= HIDE_MODEL;
		goto LAB_800545e8;
	}

LAB_800545e8:

	// quit if game is paused, or item is
	// rolling, or not drawing roulette
	if ((GAME_TRACKER->gameMode1 & PAUSE_ALL) != 0)
	{
		return;
	}
	if (player->itemRollTimer != 0)
	{
		return;
	}
	if ((GAME_TRACKER->gameMode1 & ROLLING_ITEM) == 0)
	{
		return;
	}

	// if not paused, item stopped rolling, and
	// weapon roulette sound is playing, then
	// stop the sound and remove flag
	OtherFX_Stop2(0x5d);
	GAME_TRACKER->gameMode1 &= ~(ROLLING_ITEM);

	return;
}

#include "UI_RenderFrameWumpa.c"
