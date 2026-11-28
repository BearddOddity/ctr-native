#ifndef PLATFORM_NATIVE_EXTRAS_H
#define PLATFORM_NATIVE_EXTRAS_H

// Extras: the opt-in changes the launcher's Extras tab turns on (ctr_native.ini, platform/native_config.c).
// Every one is off by default, and the game then plays exactly as the disc does.

// Once a frame, at the top of the game's state loop: puts the chosen cheat codes and unlocks back in place
// (the game clears its cheats on the way into the main menu, and a profile load replaces the unlocks).
void NativeExtras_Frame(void);

// 1 when the intro (the Naughty Dog crate, the "Sony Computer Entertainment presents" voice) is skipped.
int NativeExtras_SkipIntro(void);

#endif
