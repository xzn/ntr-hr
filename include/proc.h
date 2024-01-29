#ifndef PROC_H
#define PROC_H

#include "3ds/types.h"

u32 getCurrentProcessId();
u32 getCurrentProcessHandle();
u32 getProcessTIDByHandle(u32 hProcess, u32 tid[]);

u32 mapRemoteMemory(Handle hProcess, u32 addr, u32 size, u32 op);
u32 mapRemoteMemoryInLoader(Handle hProcess, u32 addr, u32 size, u32 op);
u32 protectRemoteMemory(Handle hProcess, void* addr, u32 size, u32 perm);
u32 protectMemory(void *addr, u32 size, u32 perm);
u32 copyRemoteMemory(Handle hDst, void* ptrDst, Handle hSrc, void* ptrSrc, u32 size);

void showDbgMemInfo(u32 addr);

#endif
