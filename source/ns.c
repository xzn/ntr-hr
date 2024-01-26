#include "global.h"

#include <memory.h>

void nsDbgPrintRaw(const char*, ...) {
}

static int nsCheckPCSafeToWrite(u32 hProcess, u32 remotePC) {
	s32 ret, i;
	u32 tids[LOCAL_TID_BUF_COUNT];
	s32 tidCount;
	u32 ctx[400];

	ret = svcGetThreadList(&tidCount, tids, LOCAL_TID_BUF_COUNT, hProcess);
	if (ret != 0) {
		return -1;
	}

	for (i = 0; i < tidCount; ++i) {
		u32 tid = tids[i];
		memset(ctx, 0x33, sizeof(ctx));
		if (rtGetThreadReg(hProcess, tid, ctx) != 0)
			return -1;
		u32 pc = ctx[15];
		if (remotePC >= pc - 16 && remotePC < pc)
			return -1;
	}

	return 0;
}

u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg, u32 binStart, u32 binSize) {
	u32 size = 0;
	u32* buf = 0;
	u32 baseAddr = NS_CONFIG_ADDR;
	u32 stackSize = STACK_SIZE;
	u32 totalSize;
	u32 ret;
	u32 tmp[20];
	u32 arm11StartAddress;
	u32 offset = NS_CONFIG_MAX_SIZE + stackSize;
	u32 pcDone = 0;
	u32 pcTries;

	arm11StartAddress = baseAddr + offset;
	buf = (u32*)binStart;
	size = binSize;
	nsDbgPrint("buf: %08x, size: %08x\n", buf, size);


	if (!buf) {
		showDbg("arm11 not loaded", 0, 0);
		return -1;
	}

	totalSize = size + offset;

	ret = mapRemoteMemory(hProcess, baseAddr, totalSize);

	if (ret != 0) {
		showDbg("mapRemoteMemory failed: %08x", ret, 0);
	}
	// set rwx
	ret = protectRemoteMemory(hProcess, (void *)baseAddr, totalSize);
	if (ret != 0) {
		showDbg("protectRemoteMemory failed: %08x", ret, 0);
		goto final;
	}
	// load arm11.bin code at arm11StartAddress
	ret = copyRemoteMemory(hProcess, (void *)arm11StartAddress, 0xffff8001, buf, size);
	if (ret != 0) {
		showDbg("copyRemoteMemory payload failed: %08x", ret, 0);
		goto final;
	}

	ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, remotePC, 8);
	if (ret != 0) {
		showDbg("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x", ret, 0);
		goto final;
	}

	cfg->initMode = NS_INITMODE_FROMHOOK;

	// store original 8-byte code
	ret = copyRemoteMemory(0xffff8001, &(cfg->startupInfo[0]), hProcess, (void *)remotePC, 8);
	if (ret != 0) {
		showDbg("copyRemoteMemory original code to be hooked failed: %08x", ret, 0);
		goto final;
	}
	cfg->startupInfo[2] = remotePC;

	// copy cfg structure to remote process
	ret = copyRemoteMemory(hProcess, (void *)baseAddr, 0xffff8001, cfg, sizeof(NS_CONFIG));
	if (ret != 0) {
		showDbg("copyRemoteMemory ns_config failed: %08x", ret, 0);
		goto final;
	}

	// write hook instructions to remote process
	tmp[0] = 0xe51ff004;
	tmp[1] = arm11StartAddress;
	pcTries = 20;
	while (!pcDone && pcTries) {
		ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0);
		if (ret != 0) {
			showDbg("locking remote process failed: %08x", ret, 0);
			goto final;
		}

		ret = nsCheckPCSafeToWrite(hProcess, remotePC);
		if (ret != 0) {
			goto lock_failed;
		}

		ret = copyRemoteMemory(hProcess, (void *)remotePC, 0xffff8001, &tmp, 8);
		if (ret != 0) {
			showDbg("copyRemoteMemory hook instruction failed: %08x", ret, 0);
			goto lock_failed;
		}

		pcDone = 1;
		goto lock_final;

lock_failed:
		svcSleepThread(50000000);
		--pcTries;

lock_final:
		ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 0, 0);
		if (ret != 0) {
			showDbg("unlocking remote process failed: %08x", ret, 0);
			goto final;
		}
	}

final:
	return ret;
}
