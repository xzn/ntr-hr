#include "global.h"

u32 rtGenerateJumpCode(u32 dst, u32* buf) {
	buf[0] = 0xe51ff004;
	buf[1] = dst;
	return 8;
}

u32 rtGetPageOfAddress(u32 addr) {
	return (addr / 0x1000) * 0x1000;
}

u32 rtCheckRemoteMemoryRegionSafeForWrite(Handle hProcess, u32 addr, u32 size) {
	u32 ret = 0;
	u32 startPage, endPage;

	startPage = rtGetPageOfAddress(addr);
	endPage = rtGetPageOfAddress(addr + size - 1);
	size = endPage - startPage + 0x1000;

	ret = protectRemoteMemory(hProcess, (void *)startPage, size);
	return ret;
}

u32 rtGetThreadReg(Handle hProcess, u32 tid, u32 *ctx) {
	u32 hThread;
	u32 pKThread, pContext;
	u32 ret;

	ret = svcOpenThread(&hThread, hProcess, tid);
	if (ret != 0) {
		return ret;
	}
	pKThread = kGetKProcessByHandle(hThread);
	kmemcpy(ctx, (void *)pKThread, 160);
	pContext = ctx[0x8c / 4] - 0x10c;
	kmemcpy(ctx, (void *)pContext, 0x10c);
	svcCloseHandle(hThread);
	return 0;
}
