#ifndef RP_H
#define RP_H

#include "3ds/types.h"

typedef struct {
	u32 mode;
	u32 quality;
	u32 qos; // in bytes per second
	u32 coreCount;
	u32 dstPort;
	u32 dstAddr;
	u32 threadPriority;
	u32 gamePid;
} RP_CONFIG;

int rpStartupFromMenu(RP_CONFIG *config);

#endif
