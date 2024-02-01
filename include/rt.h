#ifndef RT_H
#define RT_H

#include "3ds/types.h"
#include "3ds/svc.h"

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

void rtGenerateJumpCodeThumbR3(u32 dst, u32 *buf);
void rtGenerateJumpCode(u32 dst, u32 *buf);
void rtInitHookThumb(RT_HOOK *hook, u32 funcAddr, u32 callbackAddr);
void rtInitHook(RT_HOOK *hook, u32 funcAddr, u32 callbackAddr);
void rtEnableHook(RT_HOOK *hook);
void rtDisableHook(RT_HOOK *hook);

u32 rtAlignToPageSize(u32 size);
u32 rtGetPageOfAddress(u32 addr);
u32 rtCheckRemoteMemory(Handle hProcess, u32 addr, u32 size, MemPerm perm);
u32 rtCheckMemory(u32 addr, u32 size, MemPerm perm);
u32 rtGetThreadReg(Handle hProcess, u32 tid, u32 *ctx);
u32 rtFlushInstructionCache(void *ptr, u32 size);

int rtRecvSocket(u32 sockfd, u8 *buf, int size);
int rtSendSocket(u32 sockfd, u8 *buf, int size);

Handle rtOpenFile(char *fileName);
Handle rtOpenFile16(u16 *fileName);
u32 rtGetFileSize(Handle file);
u32 rtLoadFileToBuffer(Handle file, void *pBuf, u32 bufSize);
void rtCloseFile(Handle file);

#endif
