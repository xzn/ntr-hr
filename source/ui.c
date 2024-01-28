#include "global.h"

int __attribute__((weak)) showMsgVerbose(const char *, const char *, int, const char *) {
	// TODO
	return 0;
}

int __attribute__((weak)) showMsgRaw(const char *) {
	// TODO
	return 0;
}

void disp(u32 t, u32 cl) {
	u32 i;

	for (i = 0; i < t; ++i){
		REG(LCD_TOP_FILLCOLOR) = cl;
		svcSleepThread(5000000);
	}
	REG(LCD_TOP_FILLCOLOR) = 0;
}

u32 getKey(void) {
	return (REG(IoBasePad) ^ 0xFFF) & 0xFFF;
}
