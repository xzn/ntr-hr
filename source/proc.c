#include "global.h"

static u32 currentPid = 0;
u32 getCurrentProcessId() {
	if (currentPid != 0)
		return currentPid;
	svcGetProcessId(&currentPid, 0xffff8001);
	return currentPid;
}

static Handle hCurrentProcess = 0;
u32 getCurrentProcessHandle() {
	u32 handle = 0;
	u32 ret;

	if (hCurrentProcess != 0) {
		return hCurrentProcess;
	}
	ret = svcOpenProcess(&handle, getCurrentProcessId());
	if (ret != 0) {
		return 0;
	}
	hCurrentProcess = handle;
	return hCurrentProcess;
}


u32 mapRemoteMemory(Handle hProcess, u32 addr, u32 size) {
	u32 outAddr = 0;
	u32 ret;

	u32 newKP = kGetKProcessByHandle(hProcess);
	u32 oldKP = kGetCurrentKProcess();

	kSetCurrentKProcess(newKP);
	ret = svcControlMemory(&outAddr, addr, 0, size, MEMOP_ALLOC, MEMPERM_READWRITE);
	kSetCurrentKProcess(oldKP);

	if (ret != 0) {
		return ret;
	}
	if (outAddr != addr) {
		nsDbgPrint("outAddr: %08x, addr: %08x", outAddr, addr);
		return 0;
	}
	return 0;
}

u32 protectRemoteMemory(Handle hProcess, void* addr, u32 size) {
	return svcControlProcessMemory(hProcess, (u32)addr, 0, size, 6, 7);
}

u32 protectMemory(void *addr, u32 size) {
	return protectRemoteMemory(getCurrentProcessHandle(), addr, size);
}

u32 copyRemoteMemory(Handle hDst, void* ptrDst, Handle hSrc, void* ptrSrc, u32 size) {
	u8 dmaConfig[sizeof(DmaConfig)] = {-1, 0, 4};
	u32 hdma = 0;
	u32 ret;

	ret = svcFlushProcessDataCache(hSrc, (u32)ptrSrc, size);
	if (ret != 0) {
		nsDbgPrint("svcFlushProcessDataCache src failed: %08x\n", ret);
		return ret;
	}
	ret = svcFlushProcessDataCache(hDst, (u32)ptrDst, size);
	if (ret != 0) {
		nsDbgPrint("svcFlushProcessDataCache dst failed: %08x\n", ret);
		return ret;
	}

	ret = svcStartInterProcessDma(&hdma, hDst, (u32)ptrDst, hSrc, (u32)ptrSrc, size, (DmaConfig *)dmaConfig);
	if (ret != 0) {
        nsDbgPrint("svcStartInterProcessDma failed: %08x\n", ret);
		return ret;
	}
	ret = svcWaitSynchronization(hdma, COPY_REMOTE_MEMORY_TIMEOUT);
	if (ret != 0) {
		showDbg("copyRemoteMemory time out (or error) %08x", ret, 0);
		svcCloseHandle(hdma);
		return 1;
	}

	svcCloseHandle(hdma);
	ret = svcInvalidateProcessDataCache(hDst, (u32)ptrDst, size);
	if (ret != 0) {
        nsDbgPrint("svcInvalidateProcessDataCache failed: %08x\n", ret);
		return ret;
	}
	return 0;
}
