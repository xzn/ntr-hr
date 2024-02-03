#include "global.h"

#include "3ds/ipc.h"

#include <memory.h>

int plgOverlayStatus;
int plgHasVRAMAccess;
int plgHasOverlay;

static u32 *plgOverlayThreadStack;
static Handle *plgOverlayEvent;
static u32 rpPortIsTop;

typedef u32 (*SetBufferSwapTypedef)(u32 isDisplay1, u32 a2, u32 addr, u32 addrB, u32 width, u32 a6, u32 a7);
typedef u32 (*SetBufferSwapTypedef2)(u32 r0, u32 *params, u32 isBottom, u32 arg);
static RT_HOOK SetBufferSwapHook;

static void plgSetBufferSwapCommon(u32 isDisplay1, u32 addr, u32 addrB, u32 stride, u32 format) {
	if (plgLoaderEx->remotePlayBoost && plgOverlayEvent) {
		ASR(&rpPortIsTop, isDisplay1 ? 0 : 1);
		s32 ret;
		ret = svcSignalEvent(*plgOverlayEvent);
		if (ret != 0) {
			nsDbgPrint("plgOverlayEvent signal failed: %08"PRIx32"\n", ret);
		}
	}

	plgSetBufferSwapHandle(isDisplay1, addr, addrB, stride, format);
}

static u32 plgSetBufferSwapCallback(u32 isDisplay1, u32 a2, u32 addr, u32 addrB, u32 stride, u32 format, u32 a7) {
	if (addr)
		plgSetBufferSwapCommon(isDisplay1, addr, addrB, stride, format);
	u32 ret = ((SetBufferSwapTypedef)SetBufferSwapHook.callCode)(isDisplay1, a2, addr, addrB, stride, format, a7);
	return ret;
}

// taken from CTRPF
static u32 plgSetBufferSwapCallback2(u32 r0, u32 *params, u32 isDisplay1, u32 arg) {
	if (params)
	{
		// u32 isBottom = params[0];
		u32 addr = params[1];
		// void *addrB = params[2]; possible, not confirmed
		u32 stride = params[3];
		u32 format = params[4] & 0xF;

		if (addr)
			plgSetBufferSwapCommon(isDisplay1, addr, 0, stride, format);
	}

	u32 ret = ((SetBufferSwapTypedef2)SetBufferSwapHook.callCode)(r0, params, isDisplay1, arg);
	return ret;
}

static int rpPortSend(u32 isTop) {
	Handle hClient = rpGetPortHandle();
	if (!hClient)
		return -1;

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(SVC_NWM_CMD_OVERLAY_CALLBACK, 1, 2);
	cmdbuf[1] = isTop;
	cmdbuf[2] = IPC_Desc_CurProcessId();

	s32 ret = svcSendSyncRequest(hClient);
	if (ret != 0) {
		nsDbgPrint("Send port request failed: %08"PRIx32"\n", ret);
		return -1;
	}
	return 0;
}

static void plgOverlayThread(void *fp) {
	if (!fp) {
		while (1) {
			rpPortSend(-1);
			svcSleepThread(1000000000);
		}
	}

	int ret;
	while (1) {
		ret = svcWaitSynchronization(*plgOverlayEvent, 1000000000);
		if (ret != 0) {
			if (ret == RES_TIMEOUT) {
				rpPortSend(-1);
				continue;
			}
			svcSleepThread(1000000000);
			continue;
		}
		if (rpPortSend(ALR(&rpPortIsTop)) != 0) {
			svcSleepThread(1000000000);
		}
	}

	svcExitThread();
}

static u32 plgSearchReverse(u32 endAddr, u32 startAddr, u32 pat) {
	if (endAddr == 0) {
		return 0;
	}
	while (endAddr >= startAddr) {
		if (*(u32 *)endAddr == pat) {
			return endAddr;
		}
		endAddr -= 4;
	}
	return 0;
}

static u32 plgSearchBytes(u32 startAddr, u32 endAddr, const u32 *pat, int patlen) {
	u32 lastPage = 0;
	u32 pat0 = pat[0];

	while (1) {
		if (endAddr) {
			if (startAddr >= endAddr) {
				return 0;
			}
		}
		u32 currentPage = rtGetPageOfAddress(startAddr);
		if (currentPage != lastPage) {
			lastPage = currentPage;
			if (rtCheckMemory(currentPage, 0x1000, MEMPERM_READ) != 0) {
				return 0;
			}
		}
		if (*(u32 *)startAddr == pat0) {
			if (memcmp((void *)startAddr, pat, patlen) == 0) {
				return startAddr;
			}
		}
		startAddr += 4;
	}
	return 0;
}

static void plgCreateOverlayThread(u32 fp) {
	plgOverlayThreadStack = (void *)plgRequestMemory(STACK_SIZE);
	if (!plgOverlayThreadStack) {
		return;
	}
	plgOverlayEvent = plgOverlayThreadStack;
	s32 ret;
	ret = svcCreateEvent(plgOverlayEvent, RESET_ONESHOT);
	if (ret != 0) {
		nsDbgPrint("Create plgOverlayEvent failed: %08"PRIx32"\n", ret);
		*plgOverlayEvent = 0;
		return;
	}
	Handle hThread;
	ret = svcCreateThread(&hThread, plgOverlayThread, fp, &plgOverlayThreadStack[(STACK_SIZE / 4) - 10], 0x18, -2);
	if (ret != 0) {
		nsDbgPrint("Create plgOverlayThread failed: %08"PRIx32"\n", ret);
	}
}

void plgInitScreenOverlay(void) {
	if (plgLoaderEx->CTRPFCompat) {
		plgOverlayStatus = 2;
		return;
	}

	if (plgOverlayStatus) {
		return;
	}
	plgOverlayStatus = 2;

	if (rtCheckMemory(0x1F000000, 0x00600000, MEMPERM_READWRITE) == 0)
		plgHasVRAMAccess = 1;

	static const u32 pat[] = { 0xe1833000, 0xe2044cff, 0xe3c33cff, 0xe1833004, 0xe1824f93 };
	static const u32 pat2[] = { 0xe8830e60, 0xee078f9a, 0xe3a03001, 0xe7902104 };
	static const u32 pat3[] = { 0xee076f9a, 0xe3a02001, 0xe7901104, 0xe1911f9f, 0xe3c110ff };

	u32 addr, fp, fp2;
	addr = plgSearchBytes(0x00100000, 0, pat, sizeof(pat));
	if (!addr) {
		addr = plgSearchBytes(0x00100000, 0, pat2, sizeof(pat2));
	}
	fp = plgSearchReverse(addr, addr - 0x400, 0xe92d5ff0);
	if (!fp) {
		addr = plgSearchBytes(0x00100000, 0, pat3, sizeof(pat3));
		fp = plgSearchReverse(addr, addr - 0x400, 0xe92d47f0);
	}

	// taken from CTRPF
	static const u32 pat4[] = { 0xE3A00000, 0xEE070F9A, 0xE3A00001, 0xE7951104 };

	if (fp) {
		fp2 = 0;
	} else {
		addr = plgSearchBytes(0x00100000, 0, pat4, sizeof(pat4));
		fp2 = plgSearchReverse(addr, addr - 0x400, 0xE92D4070);
	}

	nsDbgPrint("Overlay addr: %"PRIx32"; fp: %"PRIx32"; fp2: %"PRIx32"\n", addr, fp, fp2);

	plgCreateOverlayThread(fp || fp2);

	if (fp) {
		rtInitHook(&SetBufferSwapHook, fp, (u32)plgSetBufferSwapCallback);
		rtEnableHook(&SetBufferSwapHook);
		plgOverlayStatus = 1;
	} else if (fp2) {
		rtInitHook(&SetBufferSwapHook, fp2, (u32)plgSetBufferSwapCallback2);
		rtEnableHook(&SetBufferSwapHook);
		plgOverlayStatus = 1;
	}
}

void plgInitScreenOverlayDirectly(u32 funcAddr) {
	plgCreateOverlayThread(1);
	rtInitHook(&SetBufferSwapHook, funcAddr, (u32)plgSetBufferSwapCallback);
	rtEnableHook(&SetBufferSwapHook);
}
