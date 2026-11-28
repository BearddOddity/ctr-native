#ifndef PLATFORM_NATIVE_EXTRAS_H
#define PLATFORM_NATIVE_EXTRAS_H

// Extras: the opt-in changes the launcher's Extras tab turns on (ctr_native.ini, platform/native_config.c).
// Every one is off by default, and the game then plays exactly as the disc does.

// Once a frame, at the top of the game's state loop: puts the chosen cheat codes and unlocks back in place
// (the game clears its cheats on the way into the main menu, and a profile load replaces the unlocks).
void NativeExtras_Frame(void);

// 1 when the intro (the Naughty Dog crate, the "Sony Computer Entertainment presents" voice) is skipped.
int NativeExtras_SkipIntro(void);

// The race camera's distance to the screen (the GTE's H) for the field of view picked: the PS1's 90 degrees
// gives h back. Every reader of the view's H (projection, frustum, skids, effects) then agrees on it.
int NativeExtras_DistanceToScreen(int h);

// The race camera's distance behind the kart, scaled by the camera distance picked (100% = the PS1's).
int NativeExtras_CameraDistance(int distance);

// True widescreen. The world's 3D is drawn narrower by the 4:3 picture's stretch, the HUD's 3D (the UI views)
// as it was. Called where a view's H goes into the GTE, after the HUD and at the frame's end (ui = 1).
struct PushBuffer;
void NativeExtras_WideForView(const struct PushBuffer *pb);
void NativeExtras_WideOff(void);
// A view's width for its frustum: wider by the same stretch, so what now shows at the sides isn't culled.
int NativeExtras_FrustumWidth(const struct PushBuffer *pb, int width);

#endif
