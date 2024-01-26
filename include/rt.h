#ifndef RT_H
#define RT_H

#include "3ds/types.h"

typedef u32 RT_LOCK;

u32 rtGenerateJumpCode(u32 dst, u32* buf);
u32 rtGetPageOfAddress(u32 addr);
u32 rtCheckRemoteMemoryRegionSafeForWrite(Handle hProcess, u32 addr, u32 size);
u32 rtGetThreadReg(Handle hProcess, u32 tid, u32 *ctx);

#endif
