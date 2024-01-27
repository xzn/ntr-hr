#ifndef RT_H
#define RT_H

#include "3ds/types.h"

typedef struct _RT_HOOK {
	u32 model;
	u32 isEnabled;
	u32 funcAddr;
	u32 bakCode[16];
	u32 jmpCode[16];
	u32 callCode[16];
} RT_HOOK;

typedef u32 RT_LOCK;
void rtInitLock(RT_LOCK *lock);
void rtAcquireLock(RT_LOCK *lock);
void rtReleaseLock(RT_LOCK *lock);

void rtGenerateJumpCode(u32 dst, u32* buf);
void rtInitHook(RT_HOOK *hook, u32 funcAddr, u32 callbackAddr);
void rtEnableHook(RT_HOOK *hook);
void rtDisableHook(RT_HOOK *hook);

u32 rtAlignToPageSize(u32 size);
u32 rtGetPageOfAddress(u32 addr);
u32 rtCheckRemoteMemoryRegionSafeForWrite(Handle hProcess, u32 addr, u32 size);
u32 rtGetThreadReg(Handle hProcess, u32 tid, u32 *ctx);
u32 rtFlushInstructionCache(void *ptr, u32 size);

int rtRecvSocket(u32 sockfd, u8 *buf, int size);
int rtSendSocket(u32 sockfd, u8 *buf, int size);

#endif
