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

typedef u32 (*OverlayFnTypedef)(u32 isDisplay1, u32 addr, u32 addrB, u32 width, u32 format);
void plgSetBufferSwapHandle(u32 isDisplay1, u32 addr, u32 addrB, u32 stride, u32 format) {
	s32 ret;

	if (!plgHasOverlay)
		return;

	if ((addr >= 0x1f000000) && (addr < 0x1f600000)) {
		if (!plgHasVRAMAccess) {
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

Result __sync_init(void);
void mainThread(void *) {
	s32 ret;
	ret = __sync_init();
	if (ret != 0) {
		nsDbgPrint("sync init failed: %08"PRIx32"\n", ret);
		goto final;
	}

	ret = srvInit();
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
