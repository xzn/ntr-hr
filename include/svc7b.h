#ifndef SVC7B_H
#define SVC7B_H

#include "3ds/types.h"

extern u32 KProcessHandleDataOffset;
extern u32 KProcessPIDOffset;
extern u32 KProcessCodesetOffset;

void kmemcpy(void *dst, void *src, u32 size);
void kSetCurrentKProcess(u32 ptr);
u32 kGetCurrentKProcess(void);
u32 kGetKProcessByHandle(u32 handle);
u32 kSwapProcessPid(u32 kProcess, u32 newPid);
void kDoKernelHax(NTR_CONFIG *ntrCfg);

#endif
