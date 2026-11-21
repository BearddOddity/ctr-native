#include <common.h>
#include <ctr_gte_transfer.h>

// NOTE: Retail writes the final GTE register in the return's delay slot; the matching unit permits that with
// -mpsx-ctc2-return-delay=on (metadata/retail/ntsc-u-926/matching.json). Retail's values pass through v1.
void RenderBucket_InitDepthGTE(void)
{
	register u32 value CTR_PSX_REGISTER("$3");

	CTR_GteWriteControl(0, 27);
	CTR_GteWriteControl(0, 28);
	value = 0x555;
	CTR_GteWriteControl(value, 29);
	value = 0x400;
	CTR_GteWriteControl(value, 30);
}
