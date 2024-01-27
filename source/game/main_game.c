#include "global.h"

#include <memory.h>

enum {
	CALLBACK_TYPE_OVERLAY = 101,
};

typedef int (*drawStringTypeDef)(char *str, int x, int y, u8 r, u8 g, u8 b, int newLine);
typedef char *(*translateTypeDef)(char *str);

static translateTypeDef plgTranslateCallback;
static drawStringTypeDef plgDrawStringCallback;

static int plgOverlayStatus;
static int isVRAMAccessible;
static int plgHasOverlay;

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
			if (rtCheckRemoteMemoryRegionSafeForWrite(getCurrentProcessHandle(), lastPage + 0x1000, 0x1000) != 0) {
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

static void plgSetBufferSwapCommon(u32 isDisplay1, u32 addr, u32 addrB, u32 stride, u32) {
	// TODO
	// Remote play callback

	if (ntrConfig->ex.plg.plgMemSizeTotal == 0 || !plgHasOverlay)
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

	// Plugin callback

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

static void plgInitScreenOverlay(void) {
	if (!ntrConfig->ex.plg.noCTRPFCompat) {
		plgOverlayStatus = 2;
		return;
	}

	if (plgOverlayStatus) {
		return;
	}
	plgOverlayStatus = 2;

	if (rtCheckRemoteMemoryRegionSafeForWrite(getCurrentProcessHandle(), 0x1F000000, 0x00600000) == 0)
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

	// TODO
	// remote play callback

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

int main(void) {
	if (startupInit(0) != 0)
		return 0;

	if (ntrConfig->ex.nsUseDbg) {
		nsStartup();
		disp(100, 0x17f7f7f);
	}

	if (ntrConfig->ex.plg.remotePlayBoost)
		plgInitScreenOverlay();

	if (ntrConfig->ex.plg.plgMemSizeTotal != 0) {
		plgLoaderInfo = (void *)PLG_LOADER_ADDR;
		disp(100, 0x100ff00);

		initSharedFunc();
		for (u32 i = 0; i < plgLoaderInfo->plgCount; ++i) {
			typedef void (*funcType)(void);
			((funcType)plgLoaderInfo->plgBufferPtr[i])();
		}
	}

	return 0;
}

u32 plgRegisterCallback(u32 type, void*, u32) {
	if (type == CALLBACK_TYPE_OVERLAY) {
		plgInitScreenOverlay();
		plgHasOverlay = 1;
		// TODO
		return 0;
	}

	return -1;
}

enum {
	VALUE_CURRENT_LANGUAGE = 6,
	VALUE_DRAWSTRING_CALLBACK,
	VALUE_TRANSLATE_CALLBACK
};

u32 plgSetValue(u32 index, u32 value) {
	if (index == VALUE_CURRENT_LANGUAGE) {
		plgLoaderInfo->currentLanguage = value;
	}
	if (index == VALUE_DRAWSTRING_CALLBACK) {
		plgDrawStringCallback = (void*)value;
	}
	if (index == VALUE_TRANSLATE_CALLBACK){
		plgTranslateCallback = (void*)value;
	}
	return 0;
}

u32 plgGetIoBase(u32) {
	// TODO
	return 0;
}
