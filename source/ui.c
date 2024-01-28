#include "global.h"

u32 allowDirectScreenAccess;

int showMsgVerbose(char*, const char *, int, const char *) {
	// TODO
	return 0;
}

int showMsgRaw(char* msg) {
	if (showDbgFunc) {
		showDbgFunc(msg);
		svcSleepThread(1000000000);
		return 0;
	}
	// TODO
	return 0;
}

void showDbgRaw(char* fmt, u32 v1, u32 v2) {
	char buf[LOCAL_DBG_BUF_SIZE];

	nsDbgPrintRaw(fmt, v1, v2);
	xsnprintf(buf, sizeof(buf), fmt, v1, v2);
	showMsgRaw(buf);
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
