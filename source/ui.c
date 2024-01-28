#include "global.h"

u32 allowDirectScreenAccess;

static int showMsgDbgFunc(const char *msg) {
	if (showDbgFunc) {
		showDbgFunc(msg);
		svcSleepThread(1000000000);
		return 0;
	}
	return -1;
}

int showMsgVerbose(const char *msg, const char *, int, const char *) {
	if (showMsgDbgFunc(msg) == 0)
		return 0;

	// TODO
	return 0;
}

int showMsgRaw(const char *msg) {
	if (showMsgDbgFunc(msg) == 0)
		return 0;

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

int initDirectScreenAccess(void) {
	// TODO
	return 0;
}

u32 getKey(void) {
	return (REG(IoBasePad) ^ 0xFFF) & 0xFFF;
}
