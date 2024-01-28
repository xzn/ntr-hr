#include "global.h"

#include "3ds/services/fs.h"
#include "3ds/srv.h"

#include <memory.h>

static u32 NTRMenuHotkey = 0x0C00;
static int cpuClockLockValue = -1;

static void lockCpuClock(void) {
	if (cpuClockLockValue < 0) {
		return;
	}
	svcKernelSetState(10, cpuClockLockValue);
}

void setCpuClockLock(int v) {
	cpuClockLockValue = v;
}

typedef u32 (*FSReadTypeDef)(u32 a1, u32 a2, u32 a3, u32 a4, u32 buffer, u32 size);
typedef u32 (*aptPrepareToStartApplicationTypeDef)(u32 a1, u32 a2, u32 a3);

static RT_HOOK HomeFSReadHook;
static RT_HOOK HomeCardUpdateInitHook;
static RT_HOOK aptPrepareToStartApplicationHook;

static u32 HomeFSReadCallback(u32 a1, u32 a2, u32 a3, u32 a4, u32 buffer, u32 size) {
	u32 ret;
	ret = ((FSReadTypeDef)HomeFSReadHook.callCode)(a1, a2, a3, a4, buffer, size);
	if (size == 0x36C0) {
		if (*(u32*)buffer == 0x48444d53) { // 'SMDH'
			nsDbgPrint("patching smdh\n");
			*(u32*)(buffer + 0x2018) = 0x7fffffff;
		}
	}
	return ret;
}

static u32 HomeCardUpdateInitCallback(void) {
	return 0xc821180b; // card update is not needed
}

static u32 gamePluginMenuSelect;

static u32 aptPrepareToStartApplicationCallback(u32 a1, u32 a2, u32 a3) {
	u32* tid = (u32*)a1;
	nsDbgPrint("Starting app (title ID): %08"PRIx32"%08"PRIx32"\n", tid[1], tid[0]);
	*plgLoader = (PLGLOADER_INFO){ 0 };
	plgLoader->tid[0] = tid[0];
	plgLoader->tid[1] = tid[1];
	gamePluginMenuSelect = 0;

	rpSetGamePid(0);

	s32 res = ((aptPrepareToStartApplicationTypeDef)aptPrepareToStartApplicationHook.callCode)(a1, a2, a3);
	return res;
}

static int injectPM(void) {
	u32 pid = ntrConfig->PMPid;
	s32 ret;
	Handle hProcess;
	u32 remotePC = ntrConfig->PMSvcRunAddr;

	ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08"PRIx32"\n", ret);
		return ret;
	}
	NS_CONFIG cfg = { 0 };
	cfg.ntrConfig = *ntrConfig;

	ret = nsAttachProcess(hProcess, remotePC, &cfg);
	svcCloseHandle(hProcess);
	return ret;
}

static int injectToPM(void) {
	rtInitHook(&aptPrepareToStartApplicationHook, ntrConfig->HomeAptStartAppletAddr, (u32)aptPrepareToStartApplicationCallback);
	rtEnableHook(&aptPrepareToStartApplicationHook);

	int tries = 5;
	while (injectPM() != 0) {
		if (--tries == 0) {
			showDbg("Injecting to PM process failed.");
			return -1;
		}
		svcSleepThread(1000000000);
	}
	return 0;
}

static void showMainMenu(void) {
	// TODO
}

static void menuThread(void *) {
	Result ret;
	ret = initDirectScreenAccess();
	if (ret != 0) {
		disp(100, 0x10000ff);
	}

	Handle fsUserHandle = ntrConfig->HomeFSUHandleAddr ?
		*(u32 *)ntrConfig->HomeFSUHandleAddr : 0;
	if (fsUserHandle == 0) {
		ret = fsInit();
		if (ret != 0) {
			showDbg("Failed to initialize fs.");
			goto final;
		}
	} else {
		fsUseSession(fsUserHandle);
	}
	ret = loadPayloadBin(NTR_BIN_PM);
	if (ret != 0) {
		showDbg("Loading pm payload failed.");
		goto final;
	}

	rtInitHook(&HomeFSReadHook, ntrConfig->HomeFSReadAddr, (u32)HomeFSReadCallback);
	rtEnableHook(&HomeFSReadHook);
	rtInitHook(&HomeCardUpdateInitHook, ntrConfig->HomeCardUpdateInitAddr, (u32)HomeCardUpdateInitCallback);
	rtEnableHook(&HomeCardUpdateInitHook);

	nsConfig->initMode = NS_INITMODE_FROMBOOT;
	srvInit();

	nsStartup();
	ret = injectToPM();
	unloadPayloadBin();

	if (ret != 0)
		goto final;

	int waitCnt = 0;
	while (1) {
		if ((getKey()) == NTRMenuHotkey) {
			if (allowDirectScreenAccess)
				showMainMenu();
		}
		svcSleepThread(100000000);
		waitCnt += 1;
		if (waitCnt % 10 == 0) {
			lockCpuClock();
			waitCnt = 0;
		}
	}

final:
	svcExitThread();
}

void nsHandlePacket(void) {
	nsHandleMenuPacket();
}

int main(void) {
	startupInit();

	if (plgLoaderInfoAlloc() != 0)
		return 0;

	Handle hThread;
	u32 *threadStack = (void *)(NS_CONFIG_ADDR + NS_CONFIG_MAX_SIZE);
	svcCreateThread(&hThread, menuThread, 0, &threadStack[(STACK_SIZE / 4) - 10], 0x10, 1);

	return 0;
}
