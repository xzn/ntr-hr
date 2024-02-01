#include "global.h"

#include "3ds/srv.h"
#include "3ds/ipc.h"

#include <memory.h>

typedef enum {
	CALLBACK_TYPE_OVERLAY = 101,
} CALLBACK_TYPE;

#define MAX_PLUGIN_ENTRY 64
typedef struct {
	CALLBACK_TYPE type;
	char *title;
	void *callback;
} PLUGIN_ENTRY;
static PLUGIN_ENTRY plgEntries[MAX_PLUGIN_ENTRY];
static u32 plgEntriesCount;

typedef int (*drawStringTypeDef)(char *str, int x, int y, u8 r, u8 g, u8 b, int newLine);
typedef char *(*translateTypeDef)(char *str);

static translateTypeDef plgTranslateCallback;
static drawStringTypeDef plgDrawStringCallback;

static int plgOverlayStatus;
static int isVRAMAccessible;
static int plgHasOverlay;

static u32 *plgOverlayThreadStack;
static Handle *plgOverlayEvent;
static u32 rpPortIsTop;

typedef u32 (*OverlayFnTypedef)(u32 isDisplay1, u32 addr, u32 addrB, u32 width, u32 format);
typedef u32 (*SetBufferSwapTypedef)(u32 isDisplay1, u32 a2, u32 addr, u32 addrB, u32 width, u32 a6, u32 a7);
typedef u32 (*SetBufferSwapTypedef2)(u32 r0, u32 *params, u32 isBottom, u32 arg);
static RT_HOOK SetBufferSwapHook;

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
			if (rtCheckMemory(lastPage + 0x1000, 0x1000, MEMPERM_READ) != 0) {
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

static void plgSetBufferSwapCommon(u32 isDisplay1, u32 addr, u32 addrB, u32 stride, u32 format) {
	ASR(&rpPortIsTop, isDisplay1 ? 0 : 1);
	s32 ret;
	if (plgOverlayEvent) {
		ret = svcSignalEvent(*plgOverlayEvent);
		if (ret != 0) {
			nsDbgPrint("plgOverlayEvent signal failed: %08"PRIx32"\n", ret);
		}
	}

	if (!plgHasOverlay)
		return;

	if ((addr >= 0x1f000000) && (addr < 0x1f600000)) {
		if (!isVRAMAccessible) {
			return;
		}
	}

	u32 height = isDisplay1 ? 320 : 400;
	int isDirty = 0;

	svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, (u32)addr, stride * height);
	if ((isDisplay1 == 0) && (addrB) && (addrB != addr)) {
		svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, (u32)addrB, stride * height);
	}

	for (u32 i = 0; i < plgEntriesCount; ++i) {
		if (plgEntries[i].type == CALLBACK_TYPE_OVERLAY) {
			ret = ((OverlayFnTypedef)plgEntries[i].callback)(isDisplay1, addr, addrB, stride, format);
			if (ret == 0) {
				isDirty = 1;
			}
		}
	}

	if (isDirty) {
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)addr, stride * height);
		if ((isDisplay1 == 0) && (addrB) && (addrB != addr)) {
			svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)addrB, stride * height);
		}
	}
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

static Handle rpSessionClient;
static int rpPortSend(u32 isTop) {
	Handle hClient = rpSessionClient;
	s32 ret;
	if (hClient == 0) {
		ret = svcConnectToPort(&hClient, SVC_PORT_NWM);
		if (ret != 0) {
			return -1;
		}
		rpSessionClient = hClient;
	}

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(isTop + 1, 1, 0);
	cmdbuf[1] = getCurrentProcessId();

	ret = svcSendSyncRequest(hClient);
	if (ret != 0) {
		nsDbgPrint("Send port request failed: %08"PRIx32"\n", ret);
		return -1;
	}
	return 0;
}

static void plgOverlayThread(void *fp) {
	if (!fp) {
		while (1) {
			rpPortSend(2);
			svcSleepThread(1000000000);
		}
	}

	int ret;
	while (1) {
		ret = svcWaitSynchronization(*plgOverlayEvent, 1000000000);
		if (ret != 0) {
			if (ret == RES_TIMEOUT) {
				rpPortSend(2);
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

static void plgInitScreenOverlay(void) {
	if (plgLoaderEx->CTRPFCompat) {
		plgOverlayStatus = 2;
		return;
	}

	if (plgOverlayStatus) {
		return;
	}
	plgOverlayStatus = 2;

	if (rtCheckMemory(0x1F000000, 0x00600000, MEMPERM_READWRITE) == 0)
		isVRAMAccessible = 1;

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

	plgOverlayThreadStack = (void *)plgRequestMemory(STACK_SIZE);
	plgOverlayEvent = plgOverlayThreadStack;
	s32 ret;
	ret = svcCreateEvent(plgOverlayEvent, RESET_ONESHOT);
	if (ret != 0) {
		nsDbgPrint("Create plgOverlayEvent failed: %08"PRIx32"\n", ret);
	}
	Handle hThread;
	ret = svcCreateThread(&hThread, plgOverlayThread, fp || fp2, &plgOverlayThreadStack[(STACK_SIZE / 4) - 10], 0x18, -2);
	if (ret != 0) {
		nsDbgPrint("Create plgOverlayThread failed: %08"PRIx32"\n", ret);
	}

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

void mainPost(void) {
	if (plgLoaderEx->remotePlayBoost)
		plgInitScreenOverlay();

	if (plgLoaderEx->memSizeTotal != 0) {
		disp(100, DBG_CL_USE_INJECT);

		initSharedFunc();
		for (u32 i = 0; i < plgLoader->plgCount; ++i) {
			typedef void (*funcType)(void);
			((funcType)plgLoader->plgBufferPtr[i])();
		}
	}
}

void mainThread(void *) {
	s32 ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		goto final;
	}

	if (ntrConfig->ex.nsUseDbg) {
		ret = nsStartup();
		if (ret != 0) {
			disp(100, DBG_CL_USE_DBG_FAIL);
		} else {
			disp(100, DBG_CL_USE_DBG);
		}
	}

final:
	svcExitThread();
}

u32 plgRegisterCallback(u32 type, void *callback, u32) {
	if (type == CALLBACK_TYPE_OVERLAY) {
		plgInitScreenOverlay();

		if (plgOverlayStatus != 1) {
			return -1;
		}

		if (plgEntriesCount >= MAX_PLUGIN_ENTRY) {
			return -1;
		}

		plgEntries[plgEntriesCount++] = (PLUGIN_ENTRY){
			.type = CALLBACK_TYPE_OVERLAY,
			.title = "ov",
			.callback = callback
		};

		plgHasOverlay = 1;

		return 0;
	}

	return -1;
}

enum {
	IO_BASE_PAD = 1,
	IO_BASE_LCD,
	IO_BASE_PDC,
	IO_BASE_GSPHEAP,
	IO_BASE_HOME_MENU_PID
};

enum {
	VALUE_CURRENT_LANGUAGE = 6,
	VALUE_DRAWSTRING_CALLBACK,
	VALUE_TRANSLATE_CALLBACK
};

u32 plgSetValue(u32 index, u32 value) {
	switch (index) {
		case VALUE_CURRENT_LANGUAGE:
			plgLoader->currentLanguage = value;
			break;

		case VALUE_DRAWSTRING_CALLBACK:
			plgDrawStringCallback = (void*)value;
			break;

		case VALUE_TRANSLATE_CALLBACK:
			plgTranslateCallback = (void*)value;
			break;

		default:
			break;
	}
	return 0;
}

u32 plgGetIoBase(u32 IoBase) {
	switch (IoBase) {
		case IO_BASE_LCD:
			return IoBaseLcd;

		case IO_BASE_PAD:
			return IoBasePad;

		case IO_BASE_PDC:
			return IoBasePdc;

		case IO_BASE_GSPHEAP:
			return 0x14000000;

		case IO_BASE_HOME_MENU_PID:
			return ntrConfig->HomeMenuPid;

		default:
			return plgLoader->currentLanguage;
	}
}
