#include "global.h"

enum {
	CALLBACK_TYPE_OVERLAY = 101,
};

typedef int (*drawStringTypeDef)(char *str, int x, int y, u8 r, u8 g, u8 b, int newLine);
typedef char *(*translateTypeDef)(char *str);

static translateTypeDef plgTranslateCallback;
static drawStringTypeDef plgDrawStringCallback;

static void plgInitScreenOverlay(void) {
	// TODO
}

int main(void) {
	if (startupInit(0) != 0)
		return 0;

	// TODO
	if (ntrConfig->ex.nsUseDbg) {
		nsStartup();
		disp(100, 0x17f7f7f);
	}

	if (ntrConfig->ex.plg.plgMemSizeTotal != 0) {
		plgLoaderInfo = (void *)PLG_LOADER_ADDR;
		disp(100, 0x100ff00);

		initSharedFunc();
	}

	return 0;
}

u32 plgRegisterCallback(u32 type, void*, u32) {
	if (type == CALLBACK_TYPE_OVERLAY) {
		plgInitScreenOverlay();
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
