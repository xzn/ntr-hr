#ifndef RP_H
#define RP_H

#include "3ds/types.h"
#include "constants.h"

typedef struct {
	u32 chromaSs;
	u32 downsample;
	u32 fpsLimit;
	u32 noSkipFrame;
} RP_SCREEN_CONFIG;

typedef struct {
	u32 mode; // screen priority
	u32 quality;
	u32 qos; // in bytes per second
	u32 dstPort;
	u32 dstAddr;
	u32 gamePid;
	u32 coreCount;
	u32 threadPriority;
	u32 separateScreenConfig;
	RP_SCREEN_CONFIG screens[RP_SCREEN_COUNT];
	u32 audioEnable; // NTR-HR+: stream game audio (0 = off, stock behavior)
	u32 fullWidth;
} RP_CONFIG;

#define RP_CONFIG_ADV_CFG(config) ((void *)&(config)->coreCount)
#define RP_CONFIG_ADV_CFG_OFFSET offsetof(RP_CONFIG, coreCount)
#define RP_CONFIG_ADV_CFG_SIZE (sizeof(RP_CONFIG) - RP_CONFIG_ADV_CFG_OFFSET)

int rpStartupFromMenu(RP_CONFIG *config);
void rpStartup(u8 *buf);
void rpCheckReliableStreamForNFC(void);

typedef u32 (*sendPacketTypedef)(u8 *, u32);
extern sendPacketTypedef nwmSendPacket;

#endif
