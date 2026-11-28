#include <common.h>


enum
{
	CAM_FOLLOW_DRIVER_QUAD_FLAGS_SKIP_TERRAIN_HEIGHT = 0x4100,
};


#ifdef CTR_NATIVE
static u32 CAM_SkyboxGlow_PrimAddr(const void *prim)
{
	return CtrGpu_PrimToOTLink24(prim);
}

static u32 CAM_SkyboxGlow_PackXY(s32 x, s32 y)
{
	return ((u32)(u16)x) | ((u32)(u16)y << 16);
}

static u32 CAM_SkyboxGlow_LerpColor(u32 from, u32 to, s32 ratio)
{
	s32 r = (u8)from + ((((s32)(u8)to - (s32)(u8)from) * ratio) >> 12);
	s32 g = (u8)(from >> 8) + ((((s32)(u8)(to >> 8) - (s32)(u8)(from >> 8)) * ratio) >> 12);
	s32 b = (u8)(from >> 16) + ((((s32)(u8)(to >> 16) - (s32)(u8)(from >> 16)) * ratio) >> 12);

	return ((u32)(u8)r) | ((u32)(u8)g << 8) | ((u32)(u8)b << 16);
}

static s32 CAM_SkyboxGlow_FixedRatio(s32 numerator, s32 denominator)
{
	// NOTE(aalhendi): Clipping can have negative numerators; preserve the
	// CPU's wrapping shift without signed-left-shift undefined behavior.
	return CTR_MipsSll(numerator, 12) / denominator;
}

static s32 CAM_SkyboxGlow_ScreenX(s32 screenWidth, s32 ratio)
{
	return (screenWidth * ratio) >> 12;
}

static s32 CAM_SkyboxGlow_Div2TowardZero(s32 value)
{
	// prevent implicit unsigned promotion by casting to s32 before the rightsift -penta3
	return (s32)(value + ((u32)value >> 31)) >> 1;
}

static s32 CAM_SkyboxGlow_CalcCenterY(struct PushBuffer *pb)
{
	s32 pitch = (pb->rot.x - 0x800) * 0x78;
	s32 height = (s16)pb->rect.h;

	if (pitch < 0)
	{
		pitch += 0x3ff;
	}

	return (pitch >> 10) + CAM_SkyboxGlow_Div2TowardZero(height);
}

static s32 CAM_SkyboxGlow_CalcTilt(struct PushBuffer *pb)
{
	s32 cosine = MATH_Cos(pb->rot.z);
	if (cosine == 0)
	{
		cosine = 1;
	}

	{
		s32 ratio = CTR_MipsSll(MATH_Sin(pb->rot.z), 12) / cosine;
		s32 shifted = (s32)((u32)ratio << 8);
		shifted >>= 12;

		return CAM_SkyboxGlow_Div2TowardZero(-shifted);
	}
}

static void CAM_SkyboxGlow_EmitG3(struct PrimMem *primMem, u32 *ot, u32 color0, u32 xy0, u32 color1, u32 xy1, u32 color2, u32 xy2)
{
	POLY_G3 *poly = primMem->cursor;

	CtrGpu_WriteColorCode(&poly->r0, CtrGpu_PackColorCode(color0, 0x30));
	CtrGpu_WritePackedXY(&poly->x0, xy0);
	CtrGpu_WriteColorCode(&poly->r1, color1 & 0xffffff);
	CtrGpu_WritePackedXY(&poly->x1, xy1);
	CtrGpu_WriteColorCode(&poly->r2, color2 & 0xffffff);
	CtrGpu_WritePackedXY(&poly->x2, xy2);
	poly->tag = CtrGpu_PackOTTag(*ot, 0x06000000);
	*ot = CAM_SkyboxGlow_PrimAddr(poly);

	primMem->cursor = poly + 1;
}

static void CAM_SkyboxGlow_EmitG4(struct PrimMem *primMem, u32 *ot, u32 color0, u32 xy0, u32 color1, u32 xy1, u32 color2, u32 xy2, u32 color3, u32 xy3)
{
	POLY_G4 *poly = primMem->cursor;

	CtrGpu_WriteColorCode(&poly->r0, CtrGpu_PackColorCode(color0, 0x38));
	CtrGpu_WritePackedXY(&poly->x0, xy0);
	CtrGpu_WriteColorCode(&poly->r1, color1 & 0xffffff);
	CtrGpu_WritePackedXY(&poly->x1, xy1);
	CtrGpu_WriteColorCode(&poly->r2, color2 & 0xffffff);
	CtrGpu_WritePackedXY(&poly->x2, xy2);
	CtrGpu_WriteColorCode(&poly->r3, color3 & 0xffffff);
	CtrGpu_WritePackedXY(&poly->x3, xy3);
	poly->tag = CtrGpu_PackOTTag(*ot, 0x08000000);
	*ot = CAM_SkyboxGlow_PrimAddr(poly);

	primMem->cursor = poly + 1;
}

static void CAM_SkyboxGlow_EmitF3(struct PrimMem *primMem, u32 *ot, u32 color, u32 xy0, u32 xy1, u32 xy2)
{
	POLY_F3 *poly = primMem->cursor;

	CtrGpu_WriteColorCode(&poly->r0, CtrGpu_PackColorCode(color, 0x20));
	CtrGpu_WritePackedXY(&poly->x0, xy0);
	CtrGpu_WritePackedXY(&poly->x1, xy1);
	CtrGpu_WritePackedXY(&poly->x2, xy2);
	poly->tag = CtrGpu_PackOTTag(*ot, 0x04000000);
	*ot = CAM_SkyboxGlow_PrimAddr(poly);

	primMem->cursor = poly + 1;
}

static void CAM_SkyboxGlow_EmitF4(struct PrimMem *primMem, u32 *ot, u32 color, u32 xy0, u32 xy1, u32 xy2, u32 xy3)
{
	POLY_F4 *poly = primMem->cursor;

	CtrGpu_WriteColorCode(&poly->r0, CtrGpu_PackColorCode(color, 0x28));
	CtrGpu_WritePackedXY(&poly->x0, xy0);
	CtrGpu_WritePackedXY(&poly->x1, xy1);
	CtrGpu_WritePackedXY(&poly->x2, xy2);
	CtrGpu_WritePackedXY(&poly->x3, xy3);
	poly->tag = CtrGpu_PackOTTag(*ot, 0x05000000);
	*ot = CAM_SkyboxGlow_PrimAddr(poly);

	primMem->cursor = poly + 1;
}

static s32 CAM_SkyboxGlow_HasClearGradient(s32 gradientIndex)
{
	return gradientIndex == 0 && sdata->gGT->level1->clearColor[2].enable != 0;
}

static u32 CAM_SkyboxGlow_ClearGradientColor(void)
{
	return *(u32 *)&sdata->gGT->level1->clearColor[2].rgb[0] & 0xffffff;
}

#endif

#ifndef CTR_NATIVE
// The retail shape of CAM_SkyboxGlow (PS1 build only): the gradient colors and the two centre lines live in the
// scratchpad, and each clipped color is worked out one channel at a time, as retail does. The native build keeps the
// helper version above.
struct SkyboxGlowScratch
{
	union
	{
		u8 c[4];
		u32 w;
	} color[5];
	s32 centerY1;
	s32 centerY2;
};

#define SKYGLOW ((struct SkyboxGlowScratch *)0x1f800000)
#define SKYGLOW_LERP(dst, a, b, ratio)                                                                         \
	{                                                                                                          \
		SKYGLOW->color[dst].c[0] = SKYGLOW->color[a].c[0] + (((SKYGLOW->color[b].c[0] - SKYGLOW->color[a].c[0]) * (ratio)) >> 12); \
		SKYGLOW->color[dst].c[1] = SKYGLOW->color[a].c[1] + (((SKYGLOW->color[b].c[1] - SKYGLOW->color[a].c[1]) * (ratio)) >> 12); \
		SKYGLOW->color[dst].c[2] = SKYGLOW->color[a].c[2] + (((SKYGLOW->color[b].c[2] - SKYGLOW->color[a].c[2]) * (ratio)) >> 12); \
	}
#endif

void CAM_SkyboxGlow(struct SkyboxGlowGradient *grad, struct PushBuffer *pb, struct PrimMem *primMem, u32 *ptrOT)
{
#ifdef CTR_NATIVE
	s32 tilt = CAM_SkyboxGlow_CalcTilt(pb);
	s32 centerY1 = CAM_SkyboxGlow_CalcCenterY(pb);
	s32 centerY2 = CAM_SkyboxGlow_CalcCenterY(pb);
	s32 screenWidth = (s16)pb->rect.w;
	u32 screenWidthXY = (u16)screenWidth;

	{
		s32 i;
		for (i = 0; i < 3; i++, grad++)
		{
			s32 toLeft = centerY2 - grad->pointTo - tilt;
			s32 toRight = centerY2 - grad->pointTo + tilt;
			s32 fromLeft = centerY1 - grad->pointFrom - tilt;
			s32 fromRight = centerY1 - grad->pointFrom + tilt;
			u32 colorFrom = grad->colorFrom & 0xffffff;
			u32 colorTo = grad->colorTo & 0xffffff;
			u32 mask = 0;

			if (toLeft >= 0)
			{
				mask |= 1;
			}
			if (toRight >= 0)
			{
				mask |= 2;
			}
			if (fromLeft >= 0)
			{
				mask |= 4;
			}
			if (fromRight >= 0)
			{
				mask |= 8;
			}

			switch (mask)
			{
			case 1:
			{
				s32 leftRatio = CAM_SkyboxGlow_FixedRatio(fromLeft, fromLeft - toLeft);
				s32 rightRatio = CAM_SkyboxGlow_FixedRatio(toLeft, toLeft - toRight);
				u32 leftColor = CAM_SkyboxGlow_LerpColor(colorFrom, colorTo, leftRatio);
				u32 rightX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, rightRatio);

				CAM_SkyboxGlow_EmitG3(primMem, ptrOT, leftColor, 0, colorTo, CAM_SkyboxGlow_PackXY(0, toLeft), colorTo, rightX);
				break;
			}
			case 2:
			{
				s32 rightRatio = CAM_SkyboxGlow_FixedRatio(fromRight, fromRight - toRight);
				s32 leftRatio = CAM_SkyboxGlow_FixedRatio(toLeft, toLeft - toRight);
				u32 rightColor = CAM_SkyboxGlow_LerpColor(colorFrom, colorTo, rightRatio);
				u32 leftX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, leftRatio);

				CAM_SkyboxGlow_EmitG3(primMem, ptrOT, colorTo, leftX, rightColor, screenWidthXY, colorTo, CAM_SkyboxGlow_PackXY(screenWidth, toRight));
				break;
			}
			case 3:
			{
				s32 leftRatio = CAM_SkyboxGlow_FixedRatio(fromLeft, fromLeft - toLeft);
				s32 rightRatio = CAM_SkyboxGlow_FixedRatio(fromRight, fromRight - toRight);
				u32 leftColor = CAM_SkyboxGlow_LerpColor(colorFrom, colorTo, leftRatio);
				u32 rightColor = CAM_SkyboxGlow_LerpColor(colorFrom, colorTo, rightRatio);

				CAM_SkyboxGlow_EmitG4(primMem, ptrOT, leftColor, 0, rightColor, screenWidthXY, colorTo, CAM_SkyboxGlow_PackXY(0, toLeft), colorTo,
				                      CAM_SkyboxGlow_PackXY(screenWidth, toRight));
				break;
			}
			case 5:
			{
				s32 fromRatio = CAM_SkyboxGlow_FixedRatio(fromLeft, fromLeft - fromRight);
				s32 toRatio = CAM_SkyboxGlow_FixedRatio(toLeft, toLeft - toRight);
				u32 fromX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, fromRatio);
				u32 toX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, toRatio);

				CAM_SkyboxGlow_EmitG4(primMem, ptrOT, colorFrom, CAM_SkyboxGlow_PackXY(0, fromLeft), colorFrom, fromX, colorTo,
				                      CAM_SkyboxGlow_PackXY(0, toLeft), colorTo, toX);

				if (CAM_SkyboxGlow_HasClearGradient(i))
				{
					CAM_SkyboxGlow_EmitF3(primMem, ptrOT, CAM_SkyboxGlow_ClearGradientColor(), fromX, 0, CAM_SkyboxGlow_PackXY(0, fromLeft));
				}
				break;
			}
			case 7:
			{
				s32 fromRatio = CAM_SkyboxGlow_FixedRatio(fromLeft, fromLeft - fromRight);
				s32 rightRatio = CAM_SkyboxGlow_FixedRatio(fromRight, fromRight - toRight);
				u32 fromX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, fromRatio);
				u32 rightColor = CAM_SkyboxGlow_LerpColor(colorFrom, colorTo, rightRatio);

				CAM_SkyboxGlow_EmitG3(primMem, ptrOT, colorFrom, CAM_SkyboxGlow_PackXY(0, fromLeft), colorFrom, fromX, rightColor, screenWidthXY);
				CAM_SkyboxGlow_EmitG4(primMem, ptrOT, colorFrom, CAM_SkyboxGlow_PackXY(0, fromLeft), rightColor, screenWidthXY, colorTo,
				                      CAM_SkyboxGlow_PackXY(0, toLeft), colorTo, CAM_SkyboxGlow_PackXY(screenWidth, toRight));

				if (CAM_SkyboxGlow_HasClearGradient(i))
				{
					CAM_SkyboxGlow_EmitF3(primMem, ptrOT, CAM_SkyboxGlow_ClearGradientColor(), fromX, 0, CAM_SkyboxGlow_PackXY(0, fromLeft));
				}
				break;
			}
			case 10:
			{
				s32 fromRatio = CAM_SkyboxGlow_FixedRatio(fromLeft, fromLeft - fromRight);
				s32 toRatio = CAM_SkyboxGlow_FixedRatio(toLeft, toLeft - toRight);
				u32 fromX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, fromRatio);
				u32 toX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, toRatio);
				u32 fromRightXY = CAM_SkyboxGlow_PackXY(screenWidth, fromRight);

				CAM_SkyboxGlow_EmitG4(primMem, ptrOT, colorFrom, fromX, colorFrom, fromRightXY, colorTo, toX, colorTo,
				                      CAM_SkyboxGlow_PackXY(screenWidth, toRight));

				if (CAM_SkyboxGlow_HasClearGradient(i))
				{
					CAM_SkyboxGlow_EmitF3(primMem, ptrOT, CAM_SkyboxGlow_ClearGradientColor(), fromX, screenWidthXY, fromRightXY);
				}
				break;
			}
			case 11:
			{
				s32 fromRatio = CAM_SkyboxGlow_FixedRatio(fromLeft, fromLeft - fromRight);
				s32 leftRatio = CAM_SkyboxGlow_FixedRatio(fromLeft, fromLeft - toLeft);
				u32 fromX = (u16)CAM_SkyboxGlow_ScreenX(screenWidth, fromRatio);
				u32 leftColor = CAM_SkyboxGlow_LerpColor(colorFrom, colorTo, leftRatio);
				u32 fromRightXY = CAM_SkyboxGlow_PackXY(screenWidth, fromRight);

				CAM_SkyboxGlow_EmitG3(primMem, ptrOT, leftColor, 0, colorFrom, fromX, colorFrom, fromRightXY);
				CAM_SkyboxGlow_EmitG4(primMem, ptrOT, leftColor, 0, colorFrom, fromRightXY, colorTo, CAM_SkyboxGlow_PackXY(0, toLeft), colorTo,
				                      CAM_SkyboxGlow_PackXY(screenWidth, toRight));

				if (CAM_SkyboxGlow_HasClearGradient(i))
				{
					CAM_SkyboxGlow_EmitF3(primMem, ptrOT, CAM_SkyboxGlow_ClearGradientColor(), fromX, screenWidthXY, fromRightXY);
				}
				break;
			}
			case 15:
			{
				u32 fromRightXY = CAM_SkyboxGlow_PackXY(screenWidth, fromRight);

				CAM_SkyboxGlow_EmitG4(primMem, ptrOT, colorFrom, CAM_SkyboxGlow_PackXY(0, fromLeft), colorFrom, fromRightXY, colorTo,
				                      CAM_SkyboxGlow_PackXY(0, toLeft), colorTo, CAM_SkyboxGlow_PackXY(screenWidth, toRight));

				if (CAM_SkyboxGlow_HasClearGradient(i))
				{
					CAM_SkyboxGlow_EmitF4(primMem, ptrOT, CAM_SkyboxGlow_ClearGradientColor(), 0, screenWidthXY, CAM_SkyboxGlow_PackXY(0, fromLeft),
					                      fromRightXY);
				}
				break;
			}
			}
		}
	}
#else
	s32 cosine;
	register s32 width CTR_PSX_REGISTER("$22");
	s32 toLeft;
	register s32 i CTR_PSX_REGISTER("$23");
	s32 toRight;
	register s32 tilt CTR_PSX_REGISTER("$21");
	s32 fromRight;
	u32 widthXY;
	u32 xy;
	u32 mask;
	register struct SkyboxGlowGradient *g CTR_PSX_REGISTER("$30");
	struct Level *level;
	s32 ratio1;
	register u32 x1 CTR_PSX_REGISTER("$12");
	s32 fromLeft;
	u32 *p;

	tilt = MATH_Sin(pb->rot.z) << 12;
	cosine = MATH_Cos(pb->rot.z);
	if (cosine == 0)
	{
		cosine = 1;
	}
	tilt = tilt / cosine;
	tilt = (tilt << 8) >> 12;
	tilt = -tilt / 2;

	SKYGLOW->centerY1 = ((pb->rot.x - 0x800) * 0x78) / 0x400 + (s16)pb->rect.h / 2;
	SKYGLOW->centerY2 = ((pb->rot.x - 0x800) * 0x78) / 0x400 + (s16)pb->rect.h / 2;

	width = pb->rect.w;
	widthXY = width & 0xffff;
	g = grad;

	for (i = 0; i < 3; i++, g++)
	{
		toLeft = SKYGLOW->centerY2 - g->pointTo - tilt;
		mask = (u32)~toLeft >> 31;
		toRight = SKYGLOW->centerY2 - g->pointTo + tilt;
		SKYGLOW->color[1].w = SKYGLOW->color[0].w = g->colorFrom & 0xffffff;
		fromLeft = SKYGLOW->centerY1 - g->pointFrom - tilt;
		fromRight = SKYGLOW->centerY1 - g->pointFrom + tilt;
		SKYGLOW->color[3].w = SKYGLOW->color[2].w = g->colorTo & 0xffffff;

		if (toRight >= 0)
		{
			mask |= 2;
		}
		if (fromLeft >= 0)
		{
			mask |= 4;
		}
		if (fromRight >= 0)
		{
			mask |= 8;
		}

		switch (mask)
		{
		case 1:
			ratio1 = (fromLeft << 12) / (fromLeft - toLeft);
			SKYGLOW_LERP(0, 0, 2, ratio1);
			ratio1 = (toLeft << 12) / (toLeft - toRight);
			SKYGLOW_LERP(3, 2, 3, ratio1);

			p = (u32 *)primMem->cursor;
			p[4] = toLeft << 16;
			p[1] = (SKYGLOW->color[0].w << 8 >> 8) | 0x30000000;
			p[3] = SKYGLOW->color[2].w;
			p[5] = SKYGLOW->color[3].w;
			p[2] = 0;
			p[6] = (width * ratio1 >> 12) & 0xffff;
			p[0] = *ptrOT | 0x6000000;
			xy = (u32)p << 8 >> 8;
			p = p + 7;
			goto LinkPrim;

		case 2:
			ratio1 = (fromRight << 12) / (fromRight - toRight);
			SKYGLOW_LERP(1, 1, 3, ratio1);
			ratio1 = (toLeft << 12) / (toLeft - toRight);
			SKYGLOW_LERP(2, 2, 3, ratio1);

			p = (u32 *)primMem->cursor;
			p[1] = (SKYGLOW->color[2].w << 8 >> 8) | 0x30000000;
			p[4] = widthXY;
			p[5] = SKYGLOW->color[3].w;
			p[2] = (width * ratio1 >> 12) & 0xffff;
			p[6] = widthXY | (toRight << 16);
			p[0] = *ptrOT | 0x6000000;
			p[3] = SKYGLOW->color[1].w;
			xy = (u32)p << 8 >> 8;
			p = p + 7;
			goto LinkPrim;

		case 3:
			ratio1 = (fromLeft << 12) / (fromLeft - toLeft);
			SKYGLOW_LERP(0, 0, 2, ratio1);
			ratio1 = (fromRight << 12) / (fromRight - toRight);
			SKYGLOW_LERP(1, 1, 3, ratio1);

			p = (u32 *)primMem->cursor;
			p[2] = 0;
			p[1] = (SKYGLOW->color[0].w << 8 >> 8) | 0x38000000;
			p[4] = widthXY;
			p[3] = SKYGLOW->color[1].w;
			p[5] = SKYGLOW->color[2].w;
			p[7] = SKYGLOW->color[3].w;
			p[6] = toLeft << 16;
			p[8] = widthXY | (toRight << 16);
			p[0] = *ptrOT | 0x8000000;
			xy = (u32)p << 8 >> 8;
			p = p + 9;
		LinkPrim:
			*ptrOT = xy;
			primMem->cursor = p;
			break;

		case 5:
			ratio1 = (fromLeft << 12) / (fromLeft - fromRight);
			x1 = (width * ratio1 >> 12) & 0xffff;
			ratio1 = (toLeft << 12) / (toLeft - toRight);

			p = (u32 *)primMem->cursor;
			p[1] = (SKYGLOW->color[0].w << 8 >> 8) | 0x38000000;
			p[3] = SKYGLOW->color[1].w;
			p[4] = x1;
			p[5] = SKYGLOW->color[2].w;
			p[2] = fromLeft << 16;
			p[7] = SKYGLOW->color[3].w;
			p[8] = (width * ratio1 >> 12) & 0xffff;
			p[6] = toLeft << 16;
			p[0] = *ptrOT | 0x8000000;
			*ptrOT = (u32)p << 8 >> 8;
			primMem->cursor = p + 9;

			if ((i == 0) && ((level = GAME_TRACKER->level1)->clearColor[2].enable != 0))
			{
				p[10] = (*(u32 *)&level->clearColor[2] << 8 >> 8) | 0x20000000;
				p[11] = x1;
				p[12] = 0;
				p[13] = fromLeft << 16;
				p[9] = *ptrOT | 0x4000000;
				*ptrOT = (u32)(p + 9) << 8 >> 8;
				p = p + 14;
			LinkClear:
				primMem->cursor = p;
			}
			break;

		case 7:
			ratio1 = (fromLeft << 12) / (fromLeft - fromRight);
			x1 = width * ratio1;
			SKYGLOW_LERP(4, 0, 1, ratio1);
			ratio1 = (fromRight << 12) / (fromRight - toRight);
			SKYGLOW_LERP(1, 1, 3, ratio1);
			x1 = (x1 >> 12) & 0xffff;

			p = (u32 *)primMem->cursor;
			p[1] = (SKYGLOW->color[0].w << 8 >> 8) | 0x30000000;
			p[2] = fromLeft << 16;
			p[3] = SKYGLOW->color[4].w;
			p[5] = SKYGLOW->color[1].w;
			p[6] = widthXY;
			p[4] = x1;
			p[0] = *ptrOT | 0x6000000;
			*ptrOT = (u32)p << 8 >> 8;
			primMem->cursor = p + 7;

			p[8] = (SKYGLOW->color[0].w << 8 >> 8) | 0x38000000;
			p[10] = SKYGLOW->color[1].w;
			p[12] = SKYGLOW->color[2].w;
			p[14] = SKYGLOW->color[3].w;
			p[13] = toLeft << 16;
			p[15] = widthXY | (toRight << 16);
			p[9] = fromLeft << 16;
			p[11] = widthXY;
			p[7] = *ptrOT | 0x8000000;
			*ptrOT = (u32)(p + 7) << 8 >> 8;
			primMem->cursor = p + 16;

			if ((i == 0) && ((level = GAME_TRACKER->level1)->clearColor[2].enable != 0))
			{
				p[17] = (*(u32 *)&level->clearColor[2] << 8 >> 8) | 0x20000000;
				p[18] = x1;
				p[19] = 0;
				p[20] = fromLeft << 16;
				p[16] = *ptrOT | 0x4000000;
				*ptrOT = (u32)(p + 16) << 8 >> 8;
				p = p + 21;
				goto LinkClear;
			}
			break;

		case 10:
			xy = widthXY | (fromRight << 16);
			ratio1 = (fromLeft << 12) / (fromLeft - fromRight);
			x1 = (width * ratio1 >> 12) & 0xffff;
			ratio1 = (toLeft << 12) / (toLeft - toRight);

			p = (u32 *)primMem->cursor;
			p[1] = (SKYGLOW->color[0].w << 8 >> 8) | 0x38000000;
			p[3] = SKYGLOW->color[1].w;
			p[5] = SKYGLOW->color[2].w;
			p[7] = SKYGLOW->color[3].w;
			p[2] = x1;
			p[4] = xy;
			p[8] = widthXY | (toRight << 16);
			p[6] = (width * ratio1 >> 12) & 0xffff;
			p[0] = *ptrOT | 0x8000000;
			*ptrOT = (u32)p << 8 >> 8;
			primMem->cursor = p + 9;

			if ((i == 0) && ((level = GAME_TRACKER->level1)->clearColor[2].enable != 0))
			{
				p[10] = (*(u32 *)&level->clearColor[2] << 8 >> 8) | 0x20000000;
				p[12] = widthXY;
				p[13] = xy;
				p[11] = x1;
				p[9] = *ptrOT | 0x4000000;
				*ptrOT = (u32)(p + 9) << 8 >> 8;
				p = p + 14;
				goto LinkClear;
			}
			break;

		case 11:
			ratio1 = (fromLeft << 12) / (fromLeft - fromRight);
			x1 = width * ratio1;
			SKYGLOW_LERP(4, 0, 1, ratio1);
			ratio1 = (fromLeft << 12) / (fromLeft - toLeft);
			SKYGLOW_LERP(0, 0, 2, ratio1);
			xy = widthXY | (fromRight << 16);
			x1 = (x1 >> 12) & 0xffff;

			p = (u32 *)primMem->cursor;
			p[1] = (SKYGLOW->color[0].w << 8 >> 8) | 0x30000000;
			p[3] = SKYGLOW->color[4].w;
			p[5] = SKYGLOW->color[1].w;
			p[2] = 0;
			p[6] = xy;
			p[0] = *ptrOT | 0x6000000;
			p[4] = x1;
			*ptrOT = (u32)p << 8 >> 8;
			primMem->cursor = p + 7;

			p[8] = (SKYGLOW->color[0].w << 8 >> 8) | 0x38000000;
			p[10] = SKYGLOW->color[1].w;
			p[12] = SKYGLOW->color[2].w;
			p[14] = SKYGLOW->color[3].w;
			p[13] = toLeft << 16;
			p[11] = xy;
			p[15] = widthXY | (toRight << 16);
			p[9] = 0;
			p[7] = *ptrOT | 0x8000000;
			*ptrOT = (u32)(p + 7) << 8 >> 8;
			primMem->cursor = p + 16;

			if ((i == 0) && ((level = GAME_TRACKER->level1)->clearColor[2].enable != 0))
			{
				p[17] = (*(u32 *)&level->clearColor[2] << 8 >> 8) | 0x20000000;
				p[18] = x1;
				p[19] = widthXY;
				p[20] = xy;
				p[16] = *ptrOT | 0x4000000;
				*ptrOT = (u32)(p + 16) << 8 >> 8;
				p = p + 21;
				goto LinkClear;
			}
			break;

		case 15:
			xy = widthXY | (fromRight << 16);
			x1 = fromLeft << 16;

			p = (u32 *)primMem->cursor;
			p[1] = (SKYGLOW->color[0].w << 8 >> 8) | 0x38000000;
			p[3] = SKYGLOW->color[1].w;
			p[2] = x1;
			p[5] = SKYGLOW->color[2].w;
			p[7] = SKYGLOW->color[3].w;
			p[4] = xy;
			p[6] = toLeft << 16;
			p[8] = widthXY | (toRight << 16);
			p[0] = *ptrOT | 0x8000000;
			*ptrOT = (u32)p << 8 >> 8;
			primMem->cursor = p + 9;

			if ((i == 0) && ((level = GAME_TRACKER->level1)->clearColor[2].enable != 0))
			{
				p[10] = (*(u32 *)&level->clearColor[2] << 8 >> 8) | 0x28000000;
				p[12] = widthXY;
				p[11] = 0;
				p[13] = x1;
				p[14] = xy;
				p[9] = *ptrOT | 0x5000000;
				*ptrOT = (u32)(p + 9) << 8 >> 8;
				p = p + 15;
				goto LinkClear;
			}
			break;
		}
	}
#endif
}
#ifndef CTR_NATIVE
#undef SKYGLOW_LERP
#undef SKYGLOW
#endif

void CAM_ClearScreen(struct GameTracker *gGT)
{
	TILE *tile = gGT->backBuffer->primMem.cursor;

	{
		s32 loop;
		for (loop = 0; loop < gGT->numPlyrCurrGame; loop++)
		{
			struct PushBuffer *pb = &gGT->pushBuffer[loop];

			register s32 w CTR_PSX_REGISTER("$11") = pb->rect.w;
			s32 x = pb->rect.x;
			s32 y = pb->rect.y;
			s32 h = pb->rect.h;

			// Camera pitch moves the split around the midpoint at 0x800.
			s32 pitchShift = ((s32)pb->rot.x - 0x800) >> 3;
			s32 halfHeight = h / 2;
			register s32 splitLine CTR_PSX_REGISTER("$6") = pitchShift + halfHeight;

			register s32 clampedSplitLine CTR_PSX_REGISTER("$4") = (splitLine < 0) ? 0 : splitLine;
			// NOTE(aalhendi): Keep retail input/result lifetimes distinct;
			// native evaluates these as ordinary C values.
			CTR_PSX_DEPEND_VALUE(splitLine, pitchShift);
			CTR_PSX_DEPEND_VALUE(splitLine, halfHeight);
			CTR_PSX_DEPEND_VALUE(clampedSplitLine, splitLine);
			CTR_PSX_OBSERVE_VALUE(w);

			{
				s32 minHeight = (clampedSplitLine < h) ? clampedSplitLine : h;
				register s32 topHeight CTR_PSX_REGISTER("$6") = minHeight;
				u32 *endOT;
				CTR_PSX_DEPEND_VALUE(topHeight, minHeight);

				endOT = &pb->ptrOT[0x3FF];

				// NOTE(aalhendi): PushBuffer_Init supplies nonnegative X/width.
				// Native writes aligned packet words within the complete TILE.
				// OT heads contain only packed 24-bit links.

				// top half
				if ((gGT->level1->clearColor[0].enable != 0) && (0 < topHeight))
				{
					s32 swap = gGT->swapchainIndex;

					CTR_WriteU32AlignedLE((u8 *)tile + offsetof(TILE, w), (u32)w | ((u32)topHeight << 16));
					CTR_WriteU32AlignedLE((u8 *)tile + offsetof(TILE, x0), (u32)x | ((u32)(y + swap * 0x128) << 16));

					CTR_WriteU32AlignedLE((u8 *)tile + offsetof(TILE, r0), (u32)(u8)gGT->level1->clearColor[0].rgb[0] |
					                                                           ((u32)(u8)gGT->level1->clearColor[0].rgb[1] << 8) |
					                                                           ((u32)(u8)gGT->level1->clearColor[0].rgb[2] << 16) | 0x02000000u);

					tile->tag = *endOT | 0x03000000u;
					*endOT = CtrGpu_PrimToOTLink24(tile);

					tile++;
				}

				// bottom half
				if ((gGT->level1->clearColor[1].enable != 0) && (topHeight < h))
				{
					s32 swap = gGT->swapchainIndex;

					CTR_WriteU32AlignedLE((u8 *)tile + offsetof(TILE, w), (u32)w | ((u32)(h - topHeight) << 16));
					CTR_WriteU32AlignedLE((u8 *)tile + offsetof(TILE, x0), (u32)x | ((u32)(y + swap * 0x128 + topHeight) << 16));

					CTR_WriteU32AlignedLE((u8 *)tile + offsetof(TILE, r0), (u32)(u8)gGT->level1->clearColor[1].rgb[0] |
					                                                           ((u32)(u8)gGT->level1->clearColor[1].rgb[1] << 8) |
					                                                           ((u32)(u8)gGT->level1->clearColor[1].rgb[2] << 16) | 0x02000000u);

					tile->tag = *endOT | 0x03000000u;
					*endOT = CtrGpu_PrimToOTLink24(tile);

					tile++;
				}
			}
			// Preserve width after the bottom packet word is built.
			CTR_PSX_OBSERVE_VALUE(w);
		}
	}

	gGT->backBuffer->primMem.cursor = tile;
}

void CAM_Init(struct CameraDC *cDC, s32 cameraID, struct Driver *d, struct PushBuffer *pb)
{
	PROC_BirthWithObject(0x30f, CAM_ThTick, GAME_CAMERA_THREAD_NAME, NULL)->inst = (struct Instance *)cDC;

	memset(cDC, 0, sizeof(struct CameraDC));

	// needed or L2 breaks
	cDC->cameraID = cameraID;

	cDC->driverToFollow = d;
	cDC->pushBuffer = pb;

	// NOTE(aalhendi): Retail writes this after memset; the explicit store is
	// part of the camera initialization sequence.
	cDC->cameraMode = 0;

	cDC->flags |= CAMERA_FLAG_DIRECTION_CHANGED;
}

s32 CAM_Path_GetNumPoints(void)
{
	struct GameTracker *gGT;
	struct Level *level1;
	struct SpawnType1 *ptrSpawnType1;
	s16 *introCam;
	u16 totalFrames;
	u16 nodeCount;
	s32 sentinel;

	totalFrames = 0;

	gGT = GAME_TRACKER;
	level1 = gGT->level1;
	if (level1 == NULL)
	{
		goto done;
	}

	ptrSpawnType1 = level1->ptrSpawnType1;
	if (ptrSpawnType1->count < 3)
	{
		goto done;
	}

	{
		void **ptrs = ST1_GETPOINTERS(ptrSpawnType1);
		introCam = ptrs[ST1_CAMERA_PATH];
		if (introCam == NULL)
		{
			goto done;
		}

		goto checkNode;
		while (1)
		{
			totalFrames += nodeCount;
			{
				register s32 signedCount CTR_PSX_REGISTER("$2");

				// NOTE(aalhendi): The unsigned sum and signed packet stride have distinct retail register lifetimes.
				signedCount = (s16)nodeCount;
				CTR_PSX_OBSERVE_VALUE(signedCount);
				introCam += signedCount * 6;
			}
		checkNode:
			sentinel = introCam[0];
			// NOTE(aalhendi): Retail tests the signed header, then reloads its unsigned count before advancing past both header words.
			CTR_PSX_DEPEND_MEMORY(introCam, sentinel);
			nodeCount = ((u16 *)introCam)[0];
			introCam += 2;
			if (sentinel == 0)
			{
				break;
			}
		}

	done:
		return (s16)totalFrames;
	}
}

u8 CAM_Path_Move(s32 frameIndex, s16 *position, s16 *rotation, s16 *pathFlagsOut)
{
	s16 frame = (s16)frameIndex;
	s16 numPos = CAM_Path_GetNumPoints();

	if (frame < 0)
	{
		goto failed;
	}
	if (frame >= numPos)
	{
		goto failed;
	}

	{
		void **ptrs = ST1_GETPOINTERS(GAME_TRACKER->level1->ptrSpawnType1);
		u16 *cursor = ptrs[ST1_CAMERA_PATH];
		u16 pathNumNode = *cursor++;
		u16 pathFlags = *cursor++;

		while ((s16)pathNumNode <= frame)
		{
			frame = (s16)(frame - (s16)pathNumNode);
			// NOTE(aalhendi): The unsigned header remains live across the signed frame subtraction and payload stride.
			CTR_PSX_FORGET_VALUE(pathNumNode);
			cursor = cursor + (s16)pathNumNode * 6;
			pathNumNode = *cursor++;
			pathFlags = *cursor++;
		}

		// advance pointer to pos+rot
		{
			// NOTE(aalhendi): Keep the retail frame base, signed index and output cursor distinct; native uses the same C accesses without register
			// constraints.
			register u16 *frameData CTR_PSX_REGISTER("$4") = cursor;
			register s32 sampleIndex CTR_PSX_REGISTER("$2");
			register u16 *sample CTR_PSX_REGISTER("$3");
			CTR_PSX_DEPEND_VALUE(frame, frameData);
			sampleIndex = frame;
			CTR_PSX_OBSERVE_VALUE(sampleIndex);
			sample = frameData + sampleIndex * 6;
			CTR_PSX_OBSERVE_VALUE(sample);

			*pathFlagsOut = pathFlags;

			// position of frame
			position[0] = sample[0];
			position[1] = sample[1];
			position[2] = sample[2];

			// rotation of frame
			sample += 3;
			rotation[0] = (((s16)sample[0] >> 4) + 0x800U) & 0xfff;
			rotation[1] = (u16)sample[1] >> 4;
			rotation[2] = (u16)sample[2] >> 4;
			return 1;
		}
	}
failed:
	return 0;
}

void CAM_StartOfRace(struct CameraDC *cDC)
{
	struct GameTracker *gGT = GAME_TRACKER;
	struct Level *level1 = gGT->level1;

	// Initialize the fly-in when the level has camera restart data.
	if (2 < level1->cnt_restart_points)
	{
		s32 flyInData = (s32)level1->ptr_restart_points;
		cDC->trackPathProgress = 0;
		cDC->transitionBlend = 0;

		cDC->transitionFrameCount = 0x1E;
		cDC->nearOrFar = 0;

		// when camera reaches player, be zoomed in
		cDC->cameraMode = 0;
		cDC->trackPathNode = (struct CheckpointNode *)(flyInData + 0x18);

		// Split-screen skips most of the fly-in.
		if (gGT->numPlyrCurrGame > 1)
		{
			cDC->transitionFrame = 1;
		}
		else
		{
			cDC->transitionFrame = 0xA5;
		}
	}

	cDC->cameraMode = 0;

	cDC->flags &= ~(CAMERA_FLAG_BATTLE_END_OF_RACE | CAMERA_FLAG_ARCADE_END_OF_RACE_ACTIVE);
}

void CAM_EndOfRace_Battle(struct CameraDC *cDC, struct Driver *d)
{
	u16 height;
	struct PushBuffer *pb;
	s32 dx;
	s32 dz;

	cDC->transitionTo.pos.x = 0xffe5;
	cDC->flags |= CAMERA_FLAG_BATTLE_END_OF_RACE;
	// NOTE(aalhendi): Each height occupies a 32-bit table slot; the camera consumes its low halfword.
	height = (u16)GAME_BATTLE_CAMERA_HEIGHTS[(s32)GAME_TRACKER->numPlyrCurrGame];
	cDC->transitionTo.pos.z = 0xc0;
	cDC->transitionTo.pos.y = height;

	pb = cDC->pushBuffer;
	dx = CTR_MipsSubLo(pb->pos.x, CTR_MipsSra(d->posCurr.x, 8));
	dz = CTR_MipsSubLo(pb->pos.z, CTR_MipsSra(d->posCurr.z, 8));
	cDC->spin360Angle = ratan2(dx, dz);

	cDC->transitionBlend = 0;
	cDC->transitionFrame = 60;
	cDC->transitionFrameCount = 60;
}

void CAM_EndOfRace(struct CameraDC *cDC, struct Driver *d)
{
	struct GameTracker *gGT = GAME_TRACKER;


	// If not in Battle Mode and track path points exist and game is on 1P or 2P mode
	if (((gGT->gameMode1 & BATTLE_MODE) == 0) && (1 < gGT->level1->ptrSpawnType1->count) && (gGT->numPlyrCurrGame < 3))
	{
		// Activate end-of-race cDC flag in CameraDC struct
		cDC->flags |= CAMERA_FLAG_ARCADE_END_OF_RACE_REQUESTED;
	}
	else
	{
		// Call function to initialize end-of-race cDC for Battle Mode
		CAM_EndOfRace_Battle(cDC, d);
	}
	return;
}

static s32 CAM_MulLo(s32 a, s32 b)
{
	return (s32)(u32)((s64)a * (s64)b);
}

void CAM_ProcessTransition(SVec3 *currPos, SVec3 *currRot, SVec3 *startPos, SVec3 *startRot, SVec3 *endPos, SVec3 *endRot, s32 frame)
{
	s32 deltaRot;

	currPos->x = startPos->x + (s16)(CTR_MipsMulLo((s32)endPos->x - (s32)startPos->x, frame) >> 0xc);
	currPos->y = startPos->y + (s16)(CTR_MipsMulLo((s32)endPos->y - (s32)startPos->y, frame) >> 0xc);
	currPos->z = startPos->z + (s16)(CTR_MipsMulLo((s32)endPos->z - (s32)startPos->z, frame) >> 0xc);

	deltaRot = ((s32)endRot->x - (s32)startRot->x) & 0xfff;
	if (0x7ff < deltaRot)
	{
		deltaRot -= 0x1000;
	}

	currRot->x = (startRot->x + (s16)(CTR_MipsMulLo(deltaRot, frame) >> 0xc)) & 0xfff;
	deltaRot = ((s32)endRot->y - (s32)startRot->y) & 0xfff;
	if (0x7ff < deltaRot)
	{
		deltaRot -= 0x1000;
	}
	currRot->y = (startRot->y + (s16)(CTR_MipsMulLo(deltaRot, frame) >> 0xc)) & 0xfff;
	deltaRot = ((s32)endRot->z - (s32)startRot->z) & 0xfff;
	if (0x7ff < deltaRot)
	{
		deltaRot -= 0x1000;
	}
	currRot->z = (startRot->z + (s16)(CTR_MipsMulLo(deltaRot, frame) >> 0xc)) & 0xfff;
}


void CAM_FindClosestQuadblock(struct ScratchpadStruct *sps, struct CameraDC *cDC, struct Driver *d, const Vec3 *pos)
{
	struct QuadBlock *quad;
	register u16 inputX CTR_PSX_REGISTER("$4");
	register struct CameraDC *incomingCamera CTR_PSX_REGISTER("$5");
	register struct CameraDC *camera CTR_PSX_REGISTER("$17");
	s16 minXResult;

	(void)d;
	incomingCamera = cDC;

	// NOTE(aalhendi): Retail consumes the low halfword of each VECTOR lane.
	{
		sps->Union.QuadBlockColl.pos.x = (s16)pos->x;
		sps->Union.QuadBlockColl.pos.y = (s16)pos->y;
		sps->Union.QuadBlockColl.pos.z = (s16)pos->z;

		sps->Input1.pos.x = (s16)pos->x;
		sps->Input1.pos.y = (s16)pos->y;
		// NOTE(aalhendi): Keep the halfword reload and incoming/saved camera
		// lifetimes distinct; retail copies the camera after reading input X.
		CTR_PSX_RELOAD(sps->Input1.pos.x);
		inputX = (u16)sps->Input1.pos.x;
		CTR_PSX_DEPEND_VALUE(incomingCamera, inputX);
		camera = incomingCamera;
		CTR_PSX_DEPEND_VALUE(pos, incomingCamera);
		CTR_PSX_DEPEND_VALUE(pos, camera);
		CTR_PSX_DEPEND_VALUE(pos, d);
		sps->Input1.pos.z = (s16)pos->z;
		sps->Input1.pos.y -= 0x800;
		sps->Union.QuadBlockColl.hitPos.x = inputX;
		sps->Union.QuadBlockColl.pos.y = (s16)CTR_MipsAddLo((u16)sps->Union.QuadBlockColl.pos.y, 0x100);
		sps->Union.QuadBlockColl.hitPos.y = sps->Input1.pos.y;
		sps->Union.QuadBlockColl.hitPos.z = sps->Input1.pos.z;

		minXResult = sps->Input1.pos.x < sps->Union.QuadBlockColl.pos.x ? sps->Input1.pos.x : sps->Union.QuadBlockColl.pos.x;
		// NOTE(aalhendi): Keep X through its signed minimum, then release it
		// before the Y-bound load. Native keeps the same values and stores.
		CTR_PSX_DEPEND_VALUE(camera, inputX);
		CTR_PSX_DEPEND_VALUE(sps, camera);
		sps->bbox.min.x = minXResult;
		sps->bbox.min.y = sps->Input1.pos.y < sps->Union.QuadBlockColl.pos.y ? sps->Input1.pos.y : sps->Union.QuadBlockColl.pos.y;
		sps->bbox.min.z = sps->Input1.pos.z < sps->Union.QuadBlockColl.pos.z ? sps->Input1.pos.z : sps->Union.QuadBlockColl.pos.z;
		sps->bbox.max.x = sps->Union.QuadBlockColl.pos.x < sps->Input1.pos.x ? sps->Input1.pos.x : sps->Union.QuadBlockColl.pos.x;
		sps->bbox.max.y = sps->Union.QuadBlockColl.pos.y < sps->Input1.pos.y ? sps->Input1.pos.y : sps->Union.QuadBlockColl.pos.y;
		sps->bbox.max.z = sps->Union.QuadBlockColl.pos.z < sps->Input1.pos.z ? sps->Input1.pos.z : sps->Union.QuadBlockColl.pos.z;

		sps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_CAMERA_SEARCH;
		sps->Union.QuadBlockColl.quadFlagsIgnored = 0;
		sps->boolDidTouchQuadblock = 0;
		sps->numTrianglesTested = 0;
		sps->Union.QuadBlockColl.searchFlags = 0;
		camera->quadBlockSearchHit = false;

		if ((GAME_TRACKER->level1 != NULL) && (GAME_TRACKER->level1->ptr_mesh_info != NULL) && (GAME_TRACKER->level1->ptr_mesh_info->bspRoot != NULL))
		{
			sps->ptr_mesh_info = GAME_TRACKER->level1->ptr_mesh_info;

			if (camera->ptrQuadBlock != NULL)
			{
				COLL_FIXED_QUADBLK_TestTriangles(camera->ptrQuadBlock, sps);
			}

			if (sps->boolDidTouchQuadblock == 0)
			{
				COLL_SearchBSP_CallbackPARAM(sps->ptr_mesh_info->bspRoot, &sps->bbox, COLL_FIXED_BSPLEAF_TestQuadblocks, sps);
			}

			if (sps->boolDidTouchQuadblock != 0)
			{
				camera->quadBlockSearchHit = true;
				quad = sps->hit.ptrQuadblock;
				camera->ptrQuadBlock = quad;
				GAME_TRACKER->unk1cac[0] = quad - GAME_TRACKER->level1->ptr_mesh_info->ptrQuadBlockArray;
			}
			else
			{
				GAME_TRACKER->unk1cac[0] = -1;
			}
		}
		else
		{
			sps->ptr_mesh_info = NULL;
		}
	}
}

struct ScratchpadStruct *CAM_StartLine_FlyIn_FixY(SVec3 *posRot)
{
	struct ScratchpadStruct *setupSps = &GAME_COLLISION_WORK;
	struct ScratchpadStruct *sps;
	struct mesh_info *meshInfo = GAME_TRACKER->level1->ptr_mesh_info;
	SVec3 pos;
	SVec3 posTop;
	SVec3 posBottom;
	s16 i;

	setupSps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_COLLISION_SURFACE;
	setupSps->Union.QuadBlockColl.quadFlagsIgnored = 0;
	setupSps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_HIGH_LOD;
	setupSps->ptr_mesh_info = meshInfo;

	pos.x = posRot->x;
	pos.y = posRot->y;
	pos.z = posRot->z;

	// NOTE(aalhendi): Separate setup and query lifetimes preserve retail allocation.
	i = 0;
	sps = &GAME_COLLISION_WORK;
	for (; i < 8; i++)
	{
		s32 probeOffset = i * 0x400;

		// NOTE(aalhendi): Probe endpoints wrap as halfwords, including negative Y.
		u16 x = (u16)pos.x;
		u16 y = (u16)pos.y;
		u16 z = (u16)pos.z;
		u16 topY = (u16)CTR_MipsSubLo(y, CTR_MipsAddLo(probeOffset, 0x400));
		u16 bottomY = (u16)CTR_MipsSubLo(y, CTR_MipsSubLo(probeOffset, 0x100));

		posTop.x = x;
		posTop.y = topY;
		posTop.z = z;
		posBottom.x = x;
		posBottom.y = bottomY;
		posBottom.z = z;

		COLL_SearchBSP_CallbackQUADBLK(&posTop, &posBottom, sps, 0);

		if (sps->boolDidTouchQuadblock != 0)
		{
			pos.x = sps->Union.QuadBlockColl.hitPos.x;
			pos.y = sps->Union.QuadBlockColl.hitPos.y;
			pos.z = sps->Union.QuadBlockColl.hitPos.z;
			break;
		}
	}

	posRot->y = pos.y;
	return &GAME_COLLISION_WORK;
}

#include "CAM/CAM_FollowDriver_AngleAxis.c"
#include "CAM/CAM_StartLine_FlyIn.c"

#include "CAM/CAM_FollowDriver_TrackPath.c"

void CAM_LookAtPosition(struct CameraScratchWork *scratchWork, Vec3 *positions, SVec3 *desiredPos, SVec3 *desiredRot)
{
	// NOTE(aalhendi): Publish the scratch lanes before yaw, then reload them
	// for distance and pitch. This preserves retail call order and lifetimes.
	scratchWork->camera.dir.x = desiredPos->x - (positions->x >> 8);
	scratchWork->camera.dir.y = CTR_MipsSubLo(desiredPos->y, CTR_MipsAddLo(positions->y >> 8, GAME_SPIN360_DRIVER_HEIGHTS[(s32)GAME_TRACKER->numPlyrCurrGame]));
	scratchWork->camera.dir.z = desiredPos->z - (positions->z >> 8);
	desiredRot->y = (s16)ratan2(scratchWork->camera.dir.x, scratchWork->camera.dir.z);

	{
		s32 distance = SquareRoot0_stub(CTR_MipsAddLo(CTR_MipsMulLo(scratchWork->camera.dir.x, scratchWork->camera.dir.x),
		                                              CTR_MipsMulLo(scratchWork->camera.dir.z, scratchWork->camera.dir.z)));

		// rotations
		desiredRot->x = 0x800 - (s16)ratan2(scratchWork->camera.dir.y, distance);
		desiredRot->z = 0;
	}
}

void CAM_FollowDriver_Spin360(struct CameraDC *cDC, struct CameraScratchWork *scratchWork, struct Driver *d, SVec3 *desiredPos, SVec3 *desiredRot)
{
	s32 ratio;
	s32 angle = cDC->spin360Angle;
	s32 nextAngle;

	// NOTE(aalhendi): transitionTo.pos holds the spin step, height and radius.
	// Keep the signed angle and result separate, then publish/reload the
	// low halfword. Publish Y before the trig calls, as retail does.

	// rotate other way for odd number
	if ((d->driverID & 1) != 0)
	{
		nextAngle = angle - cDC->transitionTo.pos.x;
	}
	else
	{
		// rotate one way
		nextAngle = angle + cDC->transitionTo.pos.x;
	}
	cDC->spin360Angle = (s16)nextAngle;

	desiredPos->y = (s16)CTR_MipsAddLo((u16)cDC->transitionTo.pos.y, CTR_MipsSra(d->posCurr.y, 8));
	ratio = MATH_Sin(cDC->spin360Angle);
	desiredPos->x = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.x, 8), CTR_MipsSra(CTR_MipsMulLo(ratio, cDC->transitionTo.pos.z), 0xc));

	ratio = MATH_Cos(cDC->spin360Angle);
	desiredPos->z = (s16)CTR_MipsAddLo(CTR_MipsSra(d->posCurr.z, 8), CTR_MipsSra(CTR_MipsMulLo(ratio, cDC->transitionTo.pos.z), 0xc));

	CAM_LookAtPosition(scratchWork, &d->posCurr, desiredPos, desiredRot);
}

void CAM_SetDesiredPosRot(struct CameraDC *cDC, const SVec3 *pos, const SVec3 *rot)
{
	// NOTE(aalhendi): Preserve position-then-rotation halfword transfer order.
	cDC->transitionTo.pos.x = pos->x;
	cDC->transitionTo.pos.y = pos->y;
	cDC->transitionTo.pos.z = pos->z;
	cDC->transitionTo.rot.x = rot->x;
	cDC->transitionTo.rot.y = rot->y;
	cDC->transitionTo.rot.z = rot->z;

	// 1 second, 30 frames
	cDC->transitionFrameCount = 0x1e;

	cDC->transitionFrame = 0;
	cDC->transitionBlend = 0x1000;

	cDC->flags |= CAMERA_FLAG_TRANSITION_AWAY;
}

#ifndef GAME_CAMERA_ZOOM_DATA
#define GAME_CAMERA_ZOOM_DATA (&data.NearCam4x3)
#define GAME_EOR_CAMERA_SIZES (data.EndOfRace_Camera_Size)
#endif

// CAM_FollowDriver_Normal and CAM_ThTick are written in retail's shape: GCC 2.8.1 builds them to the retail bytes.
// The PS1 build keeps their GTE transfers as the Psy-Q headers write them, and a few empty asm statements that hold a
// value in a register or make GCC read memory again (they emit nothing); the native build has its own form of each.
#ifdef CTR_NATIVE
#define CAM_PSYQ_SetRotMatrix(m) gte_SetRotMatrix(m)
#define CAM_PSYQ_ldv0(v)         CTR_GteLoadSVec3V0(v)
#define CAM_PSYQ_rtv0()          gte_rtv0()
#define CAM_PSYQ_stlvnl(v)       CTR_GteStoreMAC(CTR_VECTOR_DATA(v))
#define CAM_PSYQ_stsv(v)         CTR_GteStoreS16Triplet(CTR_VECTOR_DATA(v))
#define CAM_PSYQ_mfc2(v, n)      ((v) = (s32)MFC2(n))
#define CAM_HOLD(v)              ((void)0)
#define CAM_HOLD_CONST(v, c)     ((v) = (c))
#define CAM_BARRIER()            ((void)0)
#define CAM_MUL(a, b)            CTR_MipsMulLo((a), (b))
#define CAM_ADD(a, b)            CTR_MipsAddLo((a), (b))
#else
#define CAM_PSYQ_SetRotMatrix(r0)                                                                                     \
	__asm__ volatile("lw $12,0(%0);lw $13,4(%0);ctc2 $12,$0;ctc2 $13,$1;"                                            \
	                 "lw $12,8(%0);lw $13,12(%0);lw $14,16(%0);ctc2 $12,$2;ctc2 $13,$3;ctc2 $14,$4"                  \
	                 :                                                                                                \
	                 : "r"(r0)                                                                                        \
	                 : "$12", "$13", "$14")
#define CAM_PSYQ_ldv0(r0)   __asm__ volatile("lwc2 $0,0(%0);lwc2 $1,4(%0)" : : "r"(r0))
#define CAM_PSYQ_rtv0()     __asm__ volatile("nop;nop;.word 0x4a486012")
#define CAM_PSYQ_stlvnl(r0) __asm__ volatile("swc2 $25,0(%0);swc2 $26,4(%0);swc2 $27,8(%0)" : : "r"(r0) : "memory")
#define CAM_PSYQ_stsv(r0)                                                                                             \
	__asm__ volatile("mfc2 $12,$9\n\tmfc2 $13,$10\n\tmfc2 $14,$11\n\t"                                             \
	                 "sh $12,0(%0)\n\tsh $13,2(%0)\n\tsh $14,4(%0)"                                                  \
	                 :                                                                                                \
	                 : "r"(r0)                                                                                        \
	                 : "$12", "$13", "$14", "memory")
#define CAM_PSYQ_mfc2(v, n) __asm__ volatile("mfc2 %0,$" #n : "=r"(v))
#define CAM_HOLD(v)          __asm__("" : "=r"(v) : "0"(v))
#define CAM_HOLD_CONST(v, c) __asm__("" : "=r"(v) : "0"(c) : "memory")
#define CAM_BARRIER()        __asm__("")
#define CAM_MUL(a, b)        ((a) * (b))
#define CAM_ADD(a, b)        ((a) + (b))
#define CAM_S16AT(p, off)    (*(s16 *)((u8 *)(p) + (off)))
#endif

#define pb ((struct PushBuffer *)pushBufferPos)
void CAM_FollowDriver_Normal(struct CameraDC *cDC, struct Driver *d, SVec3 *pushBufferPos, struct CameraScratchWork *sw, struct ZoomData *zoom)
{
	struct GameTracker *gGT;
	s32 blend;
	struct CameraScratchWork *w;
	u32 backupFlags;
	u32 oldFlags;
	s32 reverse;
	s32 x;
	s32 y;
	s16 rotX;
	s16 frame;
	s16 flyInDone;
	SVec3 local_40;
	SVec3 local_38;
	struct FlyInData flyInData;
	struct QuadBlock *quad;
	struct SpawnType1 *st1;
	u8 *cameraPath;

	// backup flags
	oldFlags = cDC->flags;
	cDC->flags = oldFlags & ~CAMERA_FLAG_REVERSE;

	// a human, not in the intro cutscene, holding R2
	if (((d->actionsFlagSet & ACTION_BOT) == 0) && ((GAME_TRACKER->gameMode1 & START_OF_RACE) == 0) &&
	    ((GAMEPADS->gamepad[d->driverID].buttonsHeldCurrFrame & 0x200) != 0))
	{
		cDC->flags |= CAMERA_FLAG_REVERSE;
	}

	if (oldFlags != cDC->flags)
	{
		cDC->flags |= CAMERA_FLAG_DIRECTION_CHANGED;
	}

	// 0 = forwards, 0x800 = backwards
	reverse = ((cDC->flags & CAMERA_FLAG_REVERSE) != 0) << 0xb;

	if ((cDC->flags & CAMERA_FLAG_DIRECTION_CHANGED) != 0)
	{
		cDC->cameraMoveSpeed = __builtin_abs(d->speedApprox);
	}
	else
	{
		s32 x;
		x = __builtin_abs(d->speedApprox);
		if (x < cDC->cameraMoveSpeed)
		{
			cDC->cameraMoveSpeed = (zoom->percentage1 * cDC->cameraMoveSpeed + (0x100 - zoom->percentage2) * x) >> 8;
		}
		else
		{
			cDC->cameraMoveSpeed = (zoom->percentage1 * cDC->cameraMoveSpeed + (0x100 - zoom->percentage1) * x) >> 8;
		}
	}

	rotX = 0;
	if (GAME_TRACKER->numPlyrCurrGame != 2)
	{
		rotX = -0x64;
	}
	sw->camera.rot.x = rotX;
	sw->camera.rot.y = (d->rotCurr.w + (d->angle + 0x800) + reverse) & 0xfff;
	sw->camera.rot.z = -cDC->desiredRot.x << 1;
	ConvertRotToMatrix(&sw->camera.matrix, &sw->camera.rot);

	if (((cDC->flags & CAMERA_FLAG_FIRE_SPEED_ZOOM) != 0) && (cDC->fireSpeedZoom.timer < (d->fireSpeedCap >> 4)))
	{
		cDC->fireSpeedZoom.timer = d->fireSpeedCap >> 4;
	}
	sw->camera.rot.x = 0;
	sw->camera.rot.y = 0;
	sw->camera.rot.z = VehCalc_MapToRange(cDC->cameraMoveSpeed, zoom->speedMin, zoom->speedMax, zoom->distMin, zoom->distMax);

	if (cDC->fireSpeedZoom.timer != 0)
	{
		cDC->fireSpeedZoom.distanceOffset += GAME_TRACKER->elapsedTimeMS * 0x40;
		if (cDC->fireSpeedZoom.distanceOffset > 0x6000)
		{
			cDC->fireSpeedZoom.distanceOffset = 0x6000;
		}
		cDC->fireSpeedZoom.timer -= GAME_TRACKER->elapsedTimeMS;
		if (cDC->fireSpeedZoom.timer < 0)
		{
			cDC->fireSpeedZoom.timer = 0;
		}
	}
	else if (cDC->fireSpeedZoom.distanceOffset != 0)
	{
		cDC->fireSpeedZoom.distanceOffset -= GAME_TRACKER->elapsedTimeMS * 0x10;
		if (cDC->fireSpeedZoom.distanceOffset < 0)
		{
			cDC->fireSpeedZoom.distanceOffset = 0;
		}
	}
	sw->camera.rot.z += cDC->fireSpeedZoom.distanceOffset >> 8;

	CAM_PSYQ_SetRotMatrix(&sw->camera.matrix);
	CAM_PSYQ_ldv0(&sw->camera.rot);
	CAM_PSYQ_rtv0();
	CAM_PSYQ_stlvnl(&sw->camera.pos);

	sw->camera.rot.x = 0;
	sw->camera.rot.y = 0x40;
	sw->camera.rot.z = 0;
	CAM_PSYQ_ldv0(&sw->camera.rot);
	CAM_PSYQ_rtv0();
	CAM_PSYQ_stlvnl(&sw->sideOffset);

	sw->camera.delta.x = d->posCurr.x >> 8;
	sw->camera.delta.y = d->posCurr.y >> 8;
	sw->camera.delta.z = d->posCurr.z >> 8;
	sw->camera.delta.x += sw->sideOffset.x;
	sw->camera.delta.z += sw->sideOffset.z;
	sw->camera.pos.x += sw->camera.delta.x;

	// mask-grab
	if ((cDC->flags & CAMERA_FLAG_MASK_GRAB) != 0)
	{
		sw->camera.pos.y = (d->quadBlockHeight >> 8) + cDC->maskGrabHeightOffset + zoom->vertDistance;
	}
	else
	{
		sw->camera.pos.y += sw->camera.delta.y + zoom->vertDistance;
	}
	sw->camera.pos.z += sw->camera.delta.z;

	rotX = 0;
	if (GAME_TRACKER->numPlyrCurrGame != 2)
	{
		rotX = -0x64;
	}
	sw->camera.rot.x = rotX;
	sw->camera.rot.y = (d->turnAngleCurr + (d->rotCurr.w + (d->angle + 0x800)) + reverse) & 0xfff;
	sw->camera.rot.z = 0;
	ConvertRotToMatrix(&sw->camera.matrix, &sw->camera.rot);

	sw->camera.rot.x = 0;
	sw->camera.rot.y = 0;
	if ((d->actionsFlagSet & ACTION_WARP) != 0)
	{
		cDC->damagePitchOffset += 8;
		if (cDC->damagePitchOffset > 0)
		{
			cDC->damagePitchOffset = 0;
		}
	}
	else
	{
		cDC->damagePitchOffset -= 8;
		if (cDC->damagePitchOffset < -0x20)
		{
			cDC->damagePitchOffset = -0x20;
		}
	}
	sw->camera.rot.z = cDC->damagePitchOffset;

	CAM_PSYQ_SetRotMatrix(&sw->camera.matrix);
	CAM_PSYQ_ldv0(&sw->camera.rot);
	CAM_PSYQ_rtv0();
	CAM_PSYQ_stsv(&sw->camera.rot);

	sw->camera.delta.x += sw->camera.rot.x;
	sw->camera.delta.y += sw->camera.rot.y + zoom->angle[2];
	sw->camera.delta.z += sw->camera.rot.z;
	cDC->desiredRot.x = (zoom->angle[1] * cDC->desiredRot.x + (0x100 - zoom->angle[1]) * d->rotCurr.z) >> 8;

	if (d->kartState == KS_MASK_GRABBED)
	{
		sw->camera.pos.x = pb->pos.x;
		sw->camera.pos.y = pb->pos.y;
		sw->camera.pos.z = pb->pos.z;
		cDC->heightSmoothing.currentOffset = 0;
		cDC->heightSmoothing.startOffset = 0;
		cDC->heightSmoothing.framesRemaining = 0;
	}
	if (d->kartState == KS_ENGINE_REVVING)
	{
		cDC->heightSmoothing.currentOffset = 0;
		cDC->heightSmoothing.startOffset = 0;
		cDC->heightSmoothing.framesRemaining = 0;
	}

	if ((d->kartState != KS_BLASTED) && ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0) && (cDC->BlastedLerp.boolLerpPending != 0))
	{
		cDC->BlastedLerp.desiredPos.x = cDC->cameraPos.x - sw->camera.pos.x;
		cDC->BlastedLerp.desiredPos.y = cDC->cameraPos.y - sw->camera.pos.y;
		cDC->BlastedLerp.desiredPos.z = cDC->cameraPos.z - sw->camera.pos.z;
		cDC->BlastedLerp.desiredRot.x = cDC->lookAtPos.x - sw->camera.delta.x;
		cDC->BlastedLerp.desiredRot.y = cDC->lookAtPos.y - sw->camera.delta.y;
		cDC->BlastedLerp.desiredRot.z = cDC->lookAtPos.z - sw->camera.delta.z;
		cDC->BlastedLerp.framesRemaining = 8;
		cDC->BlastedLerp.boolLerpPending = 0;
	}

	if (((cDC->flags & CAMERA_FLAG_ARCADE_END_OF_RACE_REQUESTED) != 0) || (cDC->cameraMode != 0))
	{
		if (cDC->BlastedLerp.boolLerpPending != 0)
		{
			cDC->BlastedLerp.desiredPos.x = cDC->cameraPos.x - sw->camera.pos.x;
			cDC->BlastedLerp.desiredPos.y = cDC->cameraPos.y - sw->camera.pos.y;
			cDC->BlastedLerp.desiredPos.z = cDC->cameraPos.z - sw->camera.pos.z;
			cDC->BlastedLerp.desiredRot.x = cDC->lookAtPos.x - sw->camera.delta.x;
			cDC->BlastedLerp.desiredRot.y = cDC->lookAtPos.y - sw->camera.delta.y;
			cDC->BlastedLerp.desiredRot.z = cDC->lookAtPos.z - sw->camera.delta.z;
			cDC->BlastedLerp.framesRemaining = 8;
			cDC->BlastedLerp.boolLerpPending = 0;
		}
	}
	else if ((d->kartState == KS_BLASTED) || (cDC->BlastedLerp.boolLerpPending != 0))
	{
		if (cDC->BlastedLerp.boolLerpPending == 0)
		{
			cDC->BlastedLerp.unkOffset[0] = cDC->lookAtPos.y - cDC->cameraPos.y;
			cDC->BlastedLerp.unkOffset[1] = cDC->cameraPos.y - (d->quadBlockHeight >> 8);
		}
		cDC->BlastedLerp.boolLerpPending = 1;
		if (sw->camera.pos.y > cDC->cameraPos.y)
		{
			s32 x;
			x = cDC->BlastedLerp.unkOffset[1] + (d->quadBlockHeight >> 8);
			if (x < sw->camera.pos.y)
			{
				sw->camera.pos.y = x;
			}
		}
	}

	if ((cDC->BlastedLerp.boolLerpPending == 0) && (cDC->BlastedLerp.framesRemaining != 0))
	{
		sw->camera.pos.x += (cDC->BlastedLerp.desiredPos.x * cDC->BlastedLerp.framesRemaining) >> 3;
		sw->camera.pos.y += (cDC->BlastedLerp.desiredPos.y * cDC->BlastedLerp.framesRemaining) >> 3;
		sw->camera.pos.z += (cDC->BlastedLerp.desiredPos.z * cDC->BlastedLerp.framesRemaining) >> 3;
		sw->camera.delta.x += (cDC->BlastedLerp.desiredRot.x * cDC->BlastedLerp.framesRemaining) >> 3;
		sw->camera.delta.y += (cDC->BlastedLerp.desiredRot.y * cDC->BlastedLerp.framesRemaining) >> 3;
		sw->camera.delta.z += (cDC->BlastedLerp.desiredRot.z * cDC->BlastedLerp.framesRemaining) >> 3;
		cDC->BlastedLerp.framesRemaining--;
	}

	CAM_FindClosestQuadblock((struct ScratchpadStruct *)sw, cDC, d, &sw->camera.pos);

	if ((((struct ScratchpadStruct *)sw)->boolDidTouchQuadblock != 0) && (((quad = ((struct ScratchpadStruct *)sw)->hit.ptrQuadblock)->quadFlags & 0x4100) == 0))
	{
		// Mud, Water, or FastWater
		if ((quad->terrain_type == 0xe) || (quad->terrain_type == 4) || (quad->terrain_type == 0xd))
		{
			sw->collision.terrainHeightFloor = 0;
		}
		y = sw->collision.terrainHeightFloor + zoom->vertDistance;
		if (sw->camera.pos.y < y)
		{
			sw->camera.pos.y = y;
		}
		if (cDC->heightSmoothing.framesRemaining != 0)
		{
			sw->camera.pos.y = ((8 - cDC->heightSmoothing.framesRemaining) * sw->camera.pos.y +
			                    cDC->heightSmoothing.framesRemaining * (cDC->heightSmoothing.startOffset + (d->posCurr.y >> 8))) >> 3;
			cDC->heightSmoothing.framesRemaining--;
		}
	}
	else
	{
		if (sw->camera.pos.y < cDC->heightSmoothing.currentOffset + (d->posCurr.y >> 8))
		{
			cDC->heightSmoothing.framesRemaining = 8;
			cDC->heightSmoothing.startOffset = cDC->heightSmoothing.currentOffset;
			sw->camera.pos.y = cDC->heightSmoothing.startOffset + (d->posCurr.y >> 8);
			goto heightDone;
		}
		cDC->heightSmoothing.framesRemaining = 8;
		cDC->heightSmoothing.startOffset = sw->camera.pos.y - (d->posCurr.y >> 8);
	}
	cDC->heightSmoothing.currentOffset = sw->camera.pos.y - (d->posCurr.y >> 8);
heightDone:

	// if mask grabs you when you're underwater
	if (((GAME_TRACKER->level1->configFlags & 2) != 0) && (sw->camera.pos.y < zoom->vertDistance))
	{
		sw->camera.pos.y = zoom->vertDistance;
	}

	if (cDC->BlastedLerp.boolLerpPending != 0)
	{
		sw->camera.delta.y = sw->camera.pos.y + cDC->BlastedLerp.unkOffset[0];
	}

	if (d->kartState == KS_MASK_GRABBED)
	{
		pb->rot.z -= pb->rot.z >> 3;
		sw->camera.dir.x = pb->pos.x - (d->posCurr.x >> 8);
		sw->camera.dir.y = pb->pos.y - ((d->posCurr.y >> 8) + zoom->angle[2]);
		sw->camera.dir.z = pb->pos.z - (d->posCurr.z >> 8);
		if (pb->rot.x < 0x800)
		{
			pb->rot.x += 0x10;
			if (pb->rot.x > 0x800)
			{
				pb->rot.x = 0x800;
			}
		}
	}
	else
	{
		sw->camera.dir.x = sw->camera.pos.x - sw->camera.delta.x;
		sw->camera.dir.y = sw->camera.pos.y - sw->camera.delta.y;
		sw->camera.dir.z = sw->camera.pos.z - sw->camera.delta.z;
		pb->rot.y = ratan2(sw->camera.dir.x, sw->camera.dir.z);
		x = SquareRoot0_stub(CAM_ADD(CAM_MUL(sw->camera.dir.x, sw->camera.dir.x), CAM_MUL(sw->camera.dir.z, sw->camera.dir.z)));
		pb->rot.x = 0x800 - ratan2(sw->camera.dir.y, x);
		pb->rot.z = CAM_MUL(zoom->angle[0], cDC->desiredRot.x) >> 8;
	}

	sw->camera.posCopy.x = sw->camera.pos.x - pb->pos.x;
	sw->camera.posCopy.y = sw->camera.pos.y - pb->pos.y;
	sw->camera.posCopy.z = sw->camera.pos.z - pb->pos.z;

	cDC->pushBufferPosCorrection.x -= sw->camera.pos.x - cDC->cameraPos.x;
	cDC->pushBufferPosCorrection.y -= sw->camera.pos.y - cDC->cameraPos.y;
	cDC->pushBufferPosCorrection.z -= sw->camera.pos.z - cDC->cameraPos.z;

	if (cDC->pushBufferPosCorrection.x > 2)
	{
		cDC->pushBufferPosCorrection.x = 2;
	}
	if (cDC->pushBufferPosCorrection.x < -2)
	{
		cDC->pushBufferPosCorrection.x = -2;
	}
	if (cDC->pushBufferPosCorrection.y > 2)
	{
		cDC->pushBufferPosCorrection.y = 2;
	}
	if (cDC->pushBufferPosCorrection.y < -2)
	{
		cDC->pushBufferPosCorrection.y = -2;
	}
	if (cDC->pushBufferPosCorrection.z > 2)
	{
		cDC->pushBufferPosCorrection.z = 2;
	}
	if (cDC->pushBufferPosCorrection.z < -2)
	{
		cDC->pushBufferPosCorrection.z = -2;
	}

	if (d->kartState != KS_MASK_GRABBED)
	{
		pb->pos.x += sw->camera.posCopy.x + cDC->pushBufferPosCorrection.x;
		pb->pos.y += sw->camera.posCopy.y + cDC->pushBufferPosCorrection.y;
		pb->pos.z += sw->camera.posCopy.z + cDC->pushBufferPosCorrection.z;
	}

	cDC->cameraPos.x = sw->camera.pos.x;
	cDC->cameraPos.y = sw->camera.pos.y;
	cDC->cameraPos.z = sw->camera.pos.z;
	cDC->lookAtPos.x = sw->camera.delta.x;
	cDC->lookAtPos.y = sw->camera.delta.y;
	cDC->lookAtPos.z = sw->camera.delta.z;

	backupFlags = cDC->flags;
	cDC->flags = backupFlags & ~CAMERA_FLAG_MASK_GRAB;

	// transitioning, end-race battle, intro-race
	if (((backupFlags & (CAMERA_FLAG_TRANSITION_AWAY | CAMERA_FLAG_BATTLE_END_OF_RACE)) != 0) || ((GAME_TRACKER->gameMode1 & START_OF_RACE) != 0))
	{
		if ((backupFlags & CAMERA_FLAG_BATTLE_END_OF_RACE) != 0)
		{
			CAM_FollowDriver_Spin360(cDC, sw, d, &local_40, &local_38);
			blend = 0x1000 - cDC->transitionBlend;
		}
		else if ((backupFlags & CAMERA_FLAG_TRANSITION_AWAY) != 0)
		{
			local_40.x = cDC->transitionTo.pos.x;
			local_40.y = cDC->transitionTo.pos.y;
			local_40.z = cDC->transitionTo.pos.z;
			local_38.x = cDC->transitionTo.rot.x;
			local_38.y = cDC->transitionTo.rot.y;
			local_38.z = cDC->transitionTo.rot.z;
			blend = cDC->transitionBlend;
		}
		else
		{
			st1 = GAME_TRACKER->level1->ptrSpawnType1;
			flyInDone = 0;
			if (3 < st1->count)
			{
				cameraPath = (ST1_GETPOINTERS(st1))[ST1_CAMERA_PATH];
				flyInData.frameCount2 = 0x8e;
				flyInData.frameCount1 = 0x96;
				flyInData.ptrStart = cameraPath;
				flyInData.ptrEnd = cameraPath + 0x354;
				frame = 0xa5 - cDC->transitionFrame;
				if (frame > 0x96)
				{
					frame = 0x96;
				}
				CAM_StartLine_FlyIn(&flyInData, 0x96, frame, &local_40, &local_38);
				blend = cDC->transitionBlend;
			}
			else
			{
				flyInDone = 1;
				blend = 0x1000;
			}
			if (cDC->transitionFrame < 1)
			{
				flyInDone = 1;
			}
			if ((GAMEPADS->gamepad[d->driverID].buttonsTapped & BTN_TRIANGLE) != 0)
			{
				flyInDone = 1;
				cDC->flags |= CAMERA_FLAG_RESET_RAIN_POS | CAMERA_FLAG_DIRECTION_CHANGED;
			}
			if (flyInDone)
			{
				gGT = GAME_TRACKER;
				gGT->hudFlags |= 1;
				gGT->gameMode1 &= ~START_OF_RACE;
				GAME_TRACKER->hudFlags |= 0x20;
			}
		}

		CAM_ProcessTransition(&pb->pos, &pb->rot, &local_40, &local_38, &pb->pos, &pb->rot, blend);

		w = sw;
		w->camera.pos.x = pb->pos.x;
		w->camera.pos.y = pb->pos.y;
		w->camera.pos.z = pb->pos.z;
		CAM_FindClosestQuadblock((struct ScratchpadStruct *)w, cDC, d, &w->camera.pos);

		if ((cDC->transitionFrameCount != 0) && (cDC->transitionFrame <= cDC->transitionFrameCount))
		{
			s32 x;
			x = cDC->transitionFrameCount / 2;
			if (cDC->transitionFrame < x)
			{
				cDC->transitionBlend = MATH_Sin(0x400 - (cDC->transitionFrame << 10) / x) / 2 + 0x800;
			}
			else
			{
				cDC->transitionBlend = 0x800 - MATH_Cos(((cDC->transitionFrame - cDC->transitionFrameCount) << 10) / x) / 2;
			}
		}

		if ((cDC->flags & CAMERA_FLAG_TRANSITION_AWAY) != 0)
		{
			if ((cDC->flags & CAMERA_FLAG_TRANSITION_BACK) != 0)
			{
				if (cDC->transitionFrame > 0)
				{
					cDC->transitionFrame--;
				}
				else
				{
					cDC->flags &= ~(CAMERA_FLAG_TRANSITION_AWAY | CAMERA_FLAG_TRANSITION_BACK | CAMERA_FLAG_TRANSITION_HOLD);
				}
			}
			else if (cDC->transitionFrame < cDC->transitionFrameCount)
			{
				cDC->transitionFrame++;
			}
			else
			{
				cDC->flags |= CAMERA_FLAG_TRANSITION_HOLD;
			}
		}
		else if (((GAME_TRACKER->gameMode1 & PAUSE_ALL) == 0) && (cDC->transitionFrame > 0))
		{
			cDC->transitionFrame--;
		}
	}
}
#undef pb

// NOTE(aalhendi): The stack vectors have word-aligned XY pairs. Retail loads
// each pair with Z before transferring them to the GTE, using t4/t5. This
// writes the full second rotation word, not a row update preserving its high
// half. Native keeps the existing typed register transfers.
static inline void CAM_LoadProjectionInputs(const SVec3 *vector, const SVec3 *row)
{
#ifdef CTR_NATIVE
	CTC2(CTR_PackS16Pair(row->x, row->y), 0);
	CTC2((s32)row->z, 1);
	CTR_GteLoadSVec3V0(vector);
#else
	__asm__ volatile("lw $12,0(%1)\n\t"
	                 "lh $13,4(%1)\n\t"
	                 "ctc2 $12,$0\n\t"
	                 "ctc2 $13,$1\n\t"
	                 "lw $12,0(%0)\n\t"
	                 "lh $13,4(%0)\n\t"
	                 "mtc2 $12,$0\n\t"
	                 "mtc2 $13,$1"
	                 :
	                 : "r"(vector), "r"(row)
	                 : "$12", "$13", "memory");
#endif
}

s32 CAM_MapRange_PosPoints(SVec3 *pos1, SVec3 *pos2, SVec3 *currPos)
{
	SVec3 pathDelta;
	pathDelta.x = pos1->x - pos2->x;
	pathDelta.y = pos1->y - pos2->y;
	pathDelta.z = pos1->z - pos2->z;

	MATH_VectorNormalize(&pathDelta);

	{
		SVec3 currDelta;
		currDelta.x = currPos->x - pos1->x;
		currDelta.y = currPos->y - pos1->y;
		currDelta.z = currPos->z - pos1->z;

		CAM_LoadProjectionInputs(&currDelta, &pathDelta);
		CTR_GteLoadDelay();
		gte_mvmva(0, 0, 0, 3, 0);

		return MFC2_S(25) >> 12;
	}
}

void CAM_ThTick(struct Thread *t)
{
	struct CameraScratchWork *sw;
	struct CameraDC *cDC;
	struct Driver *d;
	struct PushBuffer *pb;
	struct ZoomData *zoom;
	struct SpawnType1 *st1;
	struct CheckpointNode *rp;
	s16 *p;
	s16 *found;
	s16 mode;
	s16 n;
	u16 zts;
	u32 f;
	s16 sm;
	u16 cam;
	s16 tx2;
	s32 two;
	u16 tx3;
	s32 A;
	s32 C;
	SVec3 vec;
	struct GameTracker *gGT;

	gGT = GAME_TRACKER;
	sw = CTR_SCRATCHPAD_PTR(struct CameraScratchWork, 0x108);
	cDC = (struct CameraDC *)t->inst;
	d = cDC->driverToFollow;
	pb = cDC->pushBuffer;

	two = 2;
	do
	{
	if ((cDC->flags & CAMERA_FLAG_FROZEN) != 0)
	{
		return;
	}
	CAM_HOLD(two);

	if (((gGT->gameMode1 & (PAUSE_ALL | START_OF_RACE | MAIN_MENU | GAME_CUTSCENE)) == 0) && (d->instSelf->thread->funcThTick == 0) &&
	    ((d->actionsFlagSet & ACTION_BOT) == 0) && (d->kartState != KS_WARP_PAD) && (d->kartState != KS_FREEZE) &&
	    ((GAME_TRACKER->gameMode2 & 4) == 0) && ((GAMEPADS->gamepad[cDC->cameraID].buttonsTapped & BTN_L2_one) != 0))
	{
		zts = cDC->zoomToggleState + 1;
		cDC->zoomToggleState = zts;
		if (1 < zts)
		{
			cDC->zoomToggleState = 0;
		}
		if ((s16)cDC->zoomToggleState != two)
		{
			cDC->nearOrFar = cDC->zoomToggleState;
			cDC->cameraMode = 0;
		}
		else
		{
			cDC->cameraMode = 0xf;
		}
	}

	CAM_BARRIER();
	} while (0);
	zoom = &GAME_CAMERA_ZOOM_DATA[(s16)cDC->nearOrFar * 2];
	if (GAME_TRACKER->numPlyrCurrGame == 2)
	{
		zoom++;
	}

	if ((cDC->flags & CAMERA_FLAG_ARCADE_END_OF_RACE_REQUESTED) != 0)
	{
		st1 = GAME_TRACKER->level1->ptrSpawnType1;
		if (2 < st1->count)
		{
			found = 0;
			p = (s16 *)(ST1_GETPOINTERS(st1))[ST1_CAMERA_EOR];
			n = *p++;
			vec.x = d->posCurr.x >> 8;
			vec.y = d->posCurr.y >> 8;
			vec.z = d->posCurr.z >> 8;
			if (n != 0)
			{
				do
				{
					s16 *entry;
					s16 mm;
					s16 rs;
					rs = *p++;
					entry = p;
					mm = __builtin_abs(*p++);
					p = (s16 *)((u8 *)p + GAME_EOR_CAMERA_SIZES[mm]);
					
					if (d->checkpoint.currentIndex == rs)
					{
						found = entry;
					}
					else
					{
						rp = (struct CheckpointNode *)(rs * 12 + (s32)GAME_TRACKER->level1->ptr_restart_points);
						if ((d->checkpoint.currentIndex == rp->nextIndex_forward) || (d->checkpoint.currentIndex == rp->nextIndex_left) ||
						    (d->checkpoint.currentIndex == rp->nextIndex_backward) || (d->checkpoint.currentIndex == rp->nextIndex_right))
						{
							found = entry;
						}
					}
					n--;
				} while (n != 0);
			}

			if ((found != 0) && (found != cDC->currEOR))
			{
				cDC->currEOR = (void *)found;
				sm = *found++;
				cDC->cameraMode = __builtin_abs(sm);
				cDC->flags = (cDC->flags | 9) & ~0x1000;
				if (sm < 0) cDC->flags |= 0x1000;
				cDC->flags |= 0x1000;
				
				switch (cDC->cameraMode)
				{
				case 0:
					pb->pos.x = d->instSelf->matrix.t[0];
					pb->pos.y = d->instSelf->matrix.t[1];
					pb->pos.z = d->instSelf->matrix.t[2];
					pb->rot.x = d->rotCurr.x;
					pb->rot.y = d->rotCurr.y;
					pb->rot.z = d->rotCurr.z;
					cDC->heightSmoothing.startOffset = 0;
					break;
				case 3:
					pb->pos.x = found[0];
					pb->pos.y = found[1];
					pb->pos.z = found[2];
					pb->rot.x = found[3];
					pb->rot.y = found[4];
					pb->rot.z = found[5];
#ifdef CTR_NATIVE
					CTR_WriteU16LE(&cDC->action, (u16)found[0]);
					CTR_WriteU16LE((u8 *)&cDC->action + 2, (u16)found[1]);
					cDC->mode = found[2];
					CTR_WriteU16LE(&cDC->unk0xC, (u16)found[3]);
					CTR_WriteU16LE((u8 *)&cDC->unk0xC + 2, (u16)found[4]);
					cDC->desiredRot.x = found[5];
#else
					// retail stores the six halfwords at cDC+4..+0x10
					CAM_S16AT(cDC, 4) = found[0];
					CAM_S16AT(cDC, 6) = found[1];
					CAM_S16AT(cDC, 8) = found[2];
					CAM_S16AT(cDC, 0xc) = found[3];
					CAM_S16AT(cDC, 0xe) = found[4];
					CAM_S16AT(cDC, 0x10) = found[5];
#endif
					break;
				case 4:
					pb->pos.x = found[0];
					pb->pos.y = found[1];
					pb->pos.z = found[2];
					break;
				case 7:
					cDC->transitionTo.pos.x = *found++;
					sm = *found;
					f = ~CAMERA_FLAG_TRACK_PATH_FACE_DRIVER;
					f &= cDC->flags;
					cDC->flags = f;
					cDC->transitionTo.pos.y = sm;
					if (found[1] != 0)
					{
						cDC->flags = f | CAMERA_FLAG_TRACK_PATH_FACE_DRIVER;
					}
					break;
				case 8:
				case 14:
					cDC->driverOffset_CamEyePos.x = found[0];
					cDC->driverOffset_CamEyePos.y = found[1];
					cDC->driverOffset_CamEyePos.z = found[2];
					cDC->driverOffset_CamLookAtPos.x = found[3];
					cDC->driverOffset_CamLookAtPos.y = found[4];
					cDC->driverOffset_CamLookAtPos.z = found[5];
					cDC->angleAxisLerpRatio = VehCalc_MapToRange(
					    cDC->driverOffset_CamEyePos.x * cDC->driverOffset_CamEyePos.x + cDC->driverOffset_CamEyePos.y * cDC->driverOffset_CamEyePos.y +
					        cDC->driverOffset_CamEyePos.z * cDC->driverOffset_CamEyePos.z,
					    0x10000, 0x190000, 0x80, 0xf0);
					break;
				case 9:
				case 13:
					cDC->trackPathNode = &GAME_TRACKER->level1->ptr_restart_points[*found++];
					cDC->trackPathProgress = 0;
					cDC->transitionTo.pos.x = *found++;
					cDC->transitionTo.pos.y = *found++;
					cDC->transitionTo.pos.z = *found++;
					cDC->transitionTo.rot.x = *found++;
					cDC->transitionTo.rot.y = *found++;
					cDC->transitionTo.rot.z = *found++;
					cDC->eorModeData.trackPathSpeed = *found;
					break;
				case 10:
					tx2 = *found++;
					goto setTransition;
				case 11:
					tx3 = pb->distanceToScreen_CURR;
					pb->pos.x = found[0];
					pb->pos.y = found[1];
					pb->pos.z = found[2];
					found += 3;
					tx2 = *found++ - tx3;
				setTransition:
					cDC->transitionTo.pos.x = tx2;
					cDC->transitionTo.pos.y = found[0];
					cDC->transitionTo.pos.z = found[1];
					break;
				case 12:
					cDC->transitionTo.pos.x = found[0];
					cDC->transitionTo.pos.y = found[1];
					cDC->transitionTo.pos.z = found[2];
					cDC->transitionTo.rot.x = found[3];
					cDC->transitionTo.rot.y = found[4];
					cDC->transitionTo.rot.z = found[5];
					cDC->eorModeData.pointPath.endPos.x = found[6];
					cDC->eorModeData.pointPath.endPos.y = found[7];
					cDC->eorModeData.pointPath.endPos.z = found[8];
					cDC->eorModeData.pointPath.speed = found[9];
					break;
				case 15:
				case 16:
					break;
				}
			}
		}
	}

	pb->distanceToScreen_PREV = pb->distanceToScreen_CURR;
	if (cDC->cameraMode == 0)
	{
		goto normal;
	}
	cDC->BlastedLerp.framesRemaining = 0;
	mode = cDC->cameraMode;
	cam = cDC->cameraMode;
	if (mode != 0)
	{
		goto other;
	}
normal:
	CAM_FollowDriver_Normal(cDC, d, &pb->pos, sw, zoom);
	goto done;
other:
	if (mode == 4)
	{
		goto lookAt;
	}
	if (mode == 10)
	{
		CAM_FollowDriver_Spin360(cDC, sw, d, &pb->pos, &pb->rot);
		sw->camera.pos.x = pb->pos.x;
		sw->camera.pos.y = pb->pos.y;
		sw->camera.pos.z = pb->pos.z;
		CAM_FindClosestQuadblock((struct ScratchpadStruct *)sw, cDC, d, &sw->camera.pos);
		goto done;
	}
	if (mode == 11)
	{
		CAM_LookAtPosition(sw, &d->posCurr, &pb->pos, &pb->rot);
		C = SquareRoot0_stub(CAM_ADD(CAM_MUL(sw->camera.dir.x, sw->camera.dir.x), CAM_MUL(sw->camera.dir.z, sw->camera.dir.z))) - cDC->transitionTo.pos.y;
		C = (C * cDC->transitionTo.pos.x) / cDC->transitionTo.pos.z;
		if (C < 0)
		{
			C = 0;
		}
		if (cDC->transitionTo.pos.x < C)
		{
			C = cDC->transitionTo.pos.x;
		}
		pb->distanceToScreen_PREV = pb->distanceToScreen_CURR + C;
		sw->camera.pos.x = pb->pos.x;
		sw->camera.pos.y = pb->pos.y;
		sw->camera.pos.z = pb->pos.z;
		CAM_FindClosestQuadblock((struct ScratchpadStruct *)sw, cDC, d, &sw->camera.pos);
		goto done;
	}
	if (mode == 12)
	{
		s32 past;
		s32 speed;
		s32 aspeed;
		s32 len;
		s32 ratio;
		s32 np;
		s16 *base;
		if (cDC->cameraModePrev != 12)
		{
			cDC->transitionFrame = 0;
		}
		vec.x = d->posCurr.x >> 8;
		vec.y = d->posCurr.y >> 8;
		vec.z = d->posCurr.z >> 8;
		past = CAM_MapRange_PosPoints(&cDC->transitionTo.pos, &cDC->transitionTo.rot, &vec);
		speed = cDC->eorModeData.pointPath.speed;
		vec.x = cDC->eorModeData.pointPath.endPos.x - cDC->transitionTo.rot.x;
		vec.y = cDC->eorModeData.pointPath.endPos.y - cDC->transitionTo.rot.y;
		vec.z = cDC->eorModeData.pointPath.endPos.z - cDC->transitionTo.rot.z;
		aspeed = __builtin_abs(speed);
		if (speed < 0)
		{
			vec.x = -vec.x;
			vec.y = -vec.y;
			vec.z = -vec.z;
		}
		len = SquareRoot0_stub(CAM_ADD(CAM_ADD(CAM_MUL(vec.x, vec.x), CAM_MUL(vec.y, vec.y)), CAM_MUL(vec.z, vec.z)));
		np = cDC->trackPathProgress + (((cDC->transitionFrame << 0xc) / 0x1e) * aspeed >> 0xc);
		ratio = (cDC->trackPathProgress << 0xc) / len;
		cDC->trackPathProgress = np;
		if (0 < past)
		{
			cDC->trackPathProgress = 0;
			cDC->transitionFrame = 0;
		}
		else if (ratio > 0x1000)
		{
			if (cDC->transitionFrame != 0)
			{
				cDC->transitionFrame--;
			}
		}
		else if (cDC->transitionFrame < 0x1e)
		{
			cDC->transitionFrame++;
		}
		base = &cDC->transitionTo.rot.x;
		if (speed < 0)
		{
			base = &cDC->eorModeData.pointPath.endPos.x;
		}
		pb->pos.x = base[0] + (vec.x * ratio >> 0xc);
		pb->pos.y = base[1] + (vec.y * ratio >> 0xc);
		pb->pos.z = base[2] + (vec.z * ratio >> 0xc);
		CAM_LookAtPosition(sw, &d->posCurr, &pb->pos, &pb->rot);
		goto findQuad;
	}
	if (mode == 7)
	{
		pb->pos.x = d->posCurr.x >> 8;
		pb->pos.y = cDC->transitionTo.pos.x + (d->posCurr.y >> 8);
		pb->pos.z = d->posCurr.z >> 8;
		pb->rot.x = cDC->transitionTo.pos.y + 0x400;
		pb->rot.y = 0;
		pb->rot.z = 0;
		if ((cDC->flags & CAMERA_FLAG_TRACK_PATH_FACE_DRIVER) != 0)
		{
			pb->rot.y = d->angle + 0x800;
		}
		goto findQuad;
	}
	if ((u16)(cam - 0xf) < 2)
	{
		pb->pos.x = sdata->FirstPersonCamera.posOffset.x + (d->posCurr.x >> 8);
		pb->pos.y = sdata->FirstPersonCamera.posOffset.y + (d->posCurr.y >> 8);
		pb->pos.z = sdata->FirstPersonCamera.posOffset.z + (d->posCurr.z >> 8);
		if (cDC->cameraMode == 0x10)
		{
			pb->rot.x = sdata->FirstPersonCamera.rotOffset.x + d->rotCurr.x;
			pb->rot.y = sdata->FirstPersonCamera.rotOffset.y + d->angle;
		}
		else
		{
			pb->rot.x = sdata->FirstPersonCamera.rotOffset.x + d->rotCurr.x;
			pb->rot.y = sdata->FirstPersonCamera.rotOffset.y + d->rotCurr.y;
		}
		pb->rot.z = sdata->FirstPersonCamera.rotOffset.z + d->rotCurr.z;
		goto findQuad;
	}
	if ((mode == 8) || (mode == 0xe))
	{
		struct Driver *bd = d;
		if ((d->botData.botFlags & 2) != 0)
		{
			if ((cDC->botFlagsPrevFrame & 2) == 0)
			{
				cDC->flags |= CAMERA_FLAG_RESET_RAIN_POS | CAMERA_FLAG_DIRECTION_CHANGED;
			}
			CAM_FollowDriver_Normal(cDC, d, &pb->pos, sw, zoom);
		}
		else
		{
			if ((cDC->botFlagsPrevFrame & 2) != 0)
			{
				cDC->flags |= CAMERA_FLAG_RESET_RAIN_POS | CAMERA_FLAG_DIRECTION_CHANGED;
			}
			CAM_FollowDriver_AngleAxis(cDC, d, CameraScratchWork_AsAngleAxis(sw), &pb->pos, &pb->rot);
		}
		cDC->botFlagsPrevFrame = bd->botData.botFlags;
		goto done;
	}
	{
	s32 k;
	CAM_HOLD_CONST(k, 9);
	if ((cDC->cameraMode == k) || (cDC->cameraMode == 0xd))
	{
		if ((GAME_TRACKER->level1->cnt_restart_points != 0) && ((GAME_TRACKER->gameMode1 & PAUSE_ALL) == 0))
		{
			s32 oy;
			MATRIX *mat;
			SVec3 *look;
			A = CAM_FollowDriver_TrackPath(cDC, &sw->trackPathPos, cDC->eorModeData.trackPathSpeed, 1);
			C = -0xc0;
			look = &sw->trackPathLookaheadPos;
			if (-1 < cDC->eorModeData.trackPathSpeed)
			{
				C = 0xc0;
			}
			C = (((CAM_FollowDriver_TrackPath(cDC, look, C, 0) - A) + 0x800) & 0xfff) - 0x800;
			mat = &sw->camera.matrix;
			sw->camera.rot.x = 0x800;
			sw->camera.rot.y = A + (C >> 1);
			sw->camera.rot.z = 0;
			sw->trackPathPos.x = (sw->trackPathPos.x + sw->trackPathLookaheadPos.x) >> 1;
			sw->trackPathPos.y = (sw->trackPathPos.y + sw->trackPathLookaheadPos.y) >> 1;
			sw->trackPathPos.z = (sw->trackPathPos.z + sw->trackPathLookaheadPos.z) >> 1;
			ConvertRotToMatrix(mat, &sw->camera.rot);
			CAM_PSYQ_SetRotMatrix(mat);
			CAM_PSYQ_ldv0(&cDC->transitionTo.pos);
			CAM_PSYQ_rtv0();
			CAM_PSYQ_mfc2(A, 25);
			CAM_PSYQ_mfc2(oy, 26);
			CAM_PSYQ_mfc2(C, 27);
			pb->pos.x = sw->trackPathPos.x + A;
			pb->pos.y = sw->trackPathPos.y + oy;
			pb->pos.z = sw->trackPathPos.z + C;
			pb->rot.x = sw->camera.rot.x + cDC->transitionTo.rot.x;
			pb->rot.y = sw->camera.rot.y + cDC->transitionTo.rot.y;
			pb->rot.z = sw->camera.rot.z + cDC->transitionTo.rot.z;
		}
		if (cDC->cameraMode == 0xd)
		{
			goto lookAt;
		}
	}
	}
	goto findQuad;

lookAt:
	CAM_LookAtPosition(sw, &d->posCurr, &pb->pos, &pb->rot);
findQuad:
	sw->camera.pos.x = pb->pos.x;
	sw->camera.pos.y = pb->pos.y;
	sw->camera.pos.z = pb->pos.z;
	CAM_FindClosestQuadblock((struct ScratchpadStruct *)sw, cDC, d, &sw->camera.pos);

done:
	cDC->cameraModePrev = cDC->cameraMode;

	if (cDC->ptrQuadBlock != 0)
	{
		struct PVS *pvs;
		pvs = cDC->ptrQuadBlock->pvs;
		if ((pvs != 0) && (pvs->visLeafSrc != 0))
		{
			cDC->visLeafSrc = pvs->visLeafSrc;
			GAME_TRACKER->unk1cac[1] = cDC->ptrQuadBlock - GAME_TRACKER->level1->ptr_mesh_info->ptrQuadBlockArray;
		}
		if (cDC->ptrQuadBlock != 0)
		{
			pvs = cDC->ptrQuadBlock->pvs;
			if ((pvs != 0) && (pvs->visFaceSrc != 0))
			{
				cDC->visFaceSrc = pvs->visFaceSrc;
			}
			if (cDC->ptrQuadBlock != 0)
			{
				pvs = cDC->ptrQuadBlock->pvs;
				if ((pvs != 0) && (pvs->visInstSrc != 0))
				{
					cDC->visInstSrc = pvs->visInstSrc;
				}
				if ((cDC->ptrQuadBlock != 0) && ((pvs = cDC->ptrQuadBlock->pvs) != 0) && (pvs->visExtraSrc != 0))
				{
					if ((GAME_TRACKER->level1->configFlags & 4) == 0)
					{
						cDC->visOVertSrc = pvs->visExtraSrc;
					}
					else
					{
						cDC->visSCVertSrc = pvs->visExtraSrc;
					}
				}
			}
		}
	}

	if (cDC->quadBlockSearchHit == false)
	{
		cDC->visLeafSrc = 0;
		cDC->visFaceSrc = 0;
	}

	if ((cDC->flags & CAMERA_FLAG_RESET_RAIN_POS) != 0)
	{
		GAME_TRACKER->rainBuffer[cDC->cameraID].cameraPos.x = pb->pos.x;
		GAME_TRACKER->rainBuffer[cDC->cameraID].cameraPos.y = pb->pos.y;
		GAME_TRACKER->rainBuffer[cDC->cameraID].cameraPos.z = pb->pos.z;
		cDC->flags &= ~CAMERA_FLAG_RESET_RAIN_POS;
	}
	cDC->flags &= ~(CAMERA_FLAG_DIRECTION_CHANGED | CAMERA_FLAG_FIRE_SPEED_ZOOM);
}
