#include "global.h"

static u32 currentPid = 0;
u32 getCurrentProcessId(void) {
	if (currentPid != 0)
		return currentPid;
	svcGetProcessId(&currentPid, CUR_PROCESS_HANDLE);
	return currentPid;
}

static Handle hCurrentProcess = 0;
u32 getCurrentProcessHandle(void) {
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

u32 getProcessTIDByHandle(u32 hProcess, u32 tid[]) {
	u8 bufKProcess[0x100], bufKCodeSet[0x100];
	u32 pKCodeSet, pKProcess;

	pKProcess = kGetKProcessByHandle(hProcess);
	if (pKProcess == 0) {
		tid[0] = tid[1] = 0;
		return 1;
	}

	kmemcpy(bufKProcess, (void *)pKProcess, 0x100);
	pKCodeSet = *(u32 *)&bufKProcess[KProcessCodesetOffset];
	kmemcpy(bufKCodeSet, (void *)pKCodeSet, 0x100);
	u32 *pTid = (u32 *)&bufKCodeSet[0x5c];
	tid[0] = pTid[0];
	tid[1] = pTid[1];

	return 0;
}

u32 mapRemoteMemory(Handle hProcess, u32 addr, u32 size, u32 op) {
	u32 outAddr = 0;
	u32 ret;

	u32 newKP = kGetKProcessByHandle(hProcess);
	u32 oldKP = kGetCurrentKProcess();

	kSetCurrentKProcess(newKP);
	ret = svcControlMemory(&outAddr, addr, addr, size, op, MEMPERM_READWRITE);
	kSetCurrentKProcess(oldKP);

	if (ret != 0) {
		return ret;
	}
	if (outAddr != addr) {
		showDbg("outAddr: %08"PRIx32", addr: %08"PRIx32"\n", outAddr, addr);
		return -1;
	}
	return 0;
}

u32 mapRemoteMemoryInLoader(Handle hProcess, u32 addr, u32 size, u32 op) {
	u32 outAddr = 0;
	u32 ret;
	u32 newKP = kGetKProcessByHandle(hProcess);
	u32 oldKP = kGetCurrentKProcess();

	u32 oldPid = kSwapProcessPid(newKP, 1);

	kSetCurrentKProcess(newKP);
	ret = svcControlMemory(&outAddr, addr, addr, size, op, MEMPERM_READWRITE);
	kSetCurrentKProcess(oldKP);
	kSwapProcessPid(newKP, oldPid);
	if (ret != 0) {
		return ret;
	}
	if (op == MEMOP_ALLOC && outAddr != addr) {
		showDbg("outAddr: %08"PRIx32", addr: %08"PRIx32"\n", outAddr, addr);
		return -1;
	}
	return 0;
}

u32 protectRemoteMemory(Handle hProcess, void* addr, u32 size, u32 perm) {
	return svcControlProcessMemory(hProcess, (u32)addr, 0, size, MEMOP_PROT, perm);
}

u32 protectMemory(void *addr, u32 size, u32 perm) {
	return protectRemoteMemory(getCurrentProcessHandle(), addr, size, perm);
}

u32 copyRemoteMemory(Handle hDst, void* ptrDst, Handle hSrc, void* ptrSrc, u32 size) {
	u8 dmaConfig[sizeof(DmaConfig)] = {-1, 0, 4};
	u32 hdma = 0;
	u32 ret;

	ret = svcFlushProcessDataCache(hSrc, (u32)ptrSrc, size);
	if (ret != 0) {
		nsDbgPrint("svcFlushProcessDataCache src failed: %08"PRIx32"\n", ret);
		return ret;
	}
	ret = svcFlushProcessDataCache(hDst, (u32)ptrDst, size);
	if (ret != 0) {
		nsDbgPrint("svcFlushProcessDataCache dst failed: %08"PRIx32"\n", ret);
		return ret;
	}

	ret = svcStartInterProcessDma(&hdma, hDst, (u32)ptrDst, hSrc, (u32)ptrSrc, size, (DmaConfig *)dmaConfig);
	if (ret != 0) {
        nsDbgPrint("svcStartInterProcessDma failed: %08"PRIx32"\n", ret);
		return ret;
	}
	ret = svcWaitSynchronization(hdma, COPY_REMOTE_MEMORY_TIMEOUT);
	if (ret != 0) {
		showDbg("copyRemoteMemory time out (or error) %08"PRIx32, ret);
		svcCloseHandle(hdma);
		return 1;
	}

	svcCloseHandle(hdma);
	ret = svcInvalidateProcessDataCache(hDst, (u32)ptrDst, size);
	if (ret != 0) {
        nsDbgPrint("svcInvalidateProcessDataCache failed: %08"PRIx32"\n", ret);
		return ret;
	}
	return 0;
}

void showDbgMemInfo(u32 addr) {
	MemInfo memInfo;
	PageInfo pageInfo;
	s32 res = svcQueryMemory(&memInfo, &pageInfo, addr);
	if (res != 0) {
		showDbg("svcQueryMemory failed for addr %08"PRIx32": %08"PRIx32, addr, res);
	} else {
		showDbg("addr %08"PRIx32": base %08"PRIx32", size: %08"PRIx32", perm: %"PRIu32", state: %"PRIu32", page: %08"PRIx32,
			addr, memInfo.base_addr, memInfo.size, memInfo.perm, memInfo.state, pageInfo.flags);
	}
}
