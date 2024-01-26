#ifndef SVC7B_H
#define SVC7B_H

#include "3ds/types.h"

extern u32 KProcessHandleDataOffset;

void kmemcpy(void *dst, void *src, u32 size);
void kSetCurrentKProcess(u32 ptr);
u32 kGetCurrentKProcess(void);
u32 kGetKProcessByHandle(u32 handle);
void kDoKernelHax(void);

#endif
