// Launcher options: read from the environment at start-up, so a launcher can set them without new arguments.
//
//   CTR_NATIVE_WIDTH, CTR_NATIVE_HEIGHT   window size in pixels (320x240 up to 7680x4320)
//   CTR_NATIVE_FULLSCREEN                 1 = start fullscreen
//   CTR_NATIVE_ASPECT                     "4:3" (pillarboxed, as the PlayStation drew it) or "16:9" (stretched to fill)
//   CTR_NATIVE_KEYBOARD                   0 = ignore the keyboard
//   CTR_NATIVE_GAMEPAD                    0 = ignore game controllers
//
// Which controllers SDL talks to (Xbox through XInput, PlayStation through HIDAPI) is set with SDL's own hints,
// which SDL reads from the environment too: SDL_XINPUT_ENABLED, SDL_JOYSTICK_HIDAPI_PS4, SDL_JOYSTICK_HIDAPI_PS5, ...
// Anything unset keeps the build's own defaults.

struct NativeOptions
{
	int width;
	int height;
	int fullscreen;
	int aspectW;
	int aspectH;
	int keyboard;
	int gamepad;
};

static struct NativeOptions s_nativeOptions = {0, 0, 0, 0, 0, 1, 1};

static int NativeOptions_Int(const char *name, int fallback, int lo, int hi)
{
	const char *text = getenv(name);
	char *end = NULL;
	long value;

	if ((text == NULL) || (text[0] == '\0'))
	{
		return fallback;
	}

	value = strtol(text, &end, 10);
	if ((end == text) || (value < lo) || (value > hi))
	{
		fprintf(stderr, "[CTR Native] %s=%s ignored (%d to %d)\n", name, text, lo, hi);
		return fallback;
	}

	return (int)value;
}

static void NativeOptions_Load(int defaultWidth, int defaultHeight)
{
	const char *aspect = getenv("CTR_NATIVE_ASPECT");

	s_nativeOptions.width = NativeOptions_Int("CTR_NATIVE_WIDTH", defaultWidth, 320, 7680);
	s_nativeOptions.height = NativeOptions_Int("CTR_NATIVE_HEIGHT", defaultHeight, 240, 4320);
	s_nativeOptions.fullscreen = NativeOptions_Int("CTR_NATIVE_FULLSCREEN", 0, 0, 1);
	s_nativeOptions.keyboard = NativeOptions_Int("CTR_NATIVE_KEYBOARD", 1, 0, 1);
	s_nativeOptions.gamepad = NativeOptions_Int("CTR_NATIVE_GAMEPAD", 1, 0, 1);

	if ((aspect != NULL) && (strcmp(aspect, "4:3") == 0))
	{
		s_nativeOptions.aspectW = 4;
		s_nativeOptions.aspectH = 3;
	}
	else if ((aspect != NULL) && (strcmp(aspect, "16:9") == 0))
	{
		s_nativeOptions.aspectW = 16;
		s_nativeOptions.aspectH = 9;
	}

	printf("[CTR Native] Window %dx%d%s, aspect %s, keyboard %s, controllers %s\n",
		s_nativeOptions.width, s_nativeOptions.height, s_nativeOptions.fullscreen ? " fullscreen" : "",
		s_nativeOptions.aspectW ? (s_nativeOptions.aspectW == 4 ? "4:3" : "16:9") : "window",
		s_nativeOptions.keyboard ? "on" : "off", s_nativeOptions.gamepad ? "on" : "off");
	fflush(stdout);
}
