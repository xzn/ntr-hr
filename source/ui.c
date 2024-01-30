#include "global.h"

int showMsgVerbose(const char *file_name, int line_number, const char *func_name, const char *fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	s32 ret = showMsgVA(file_name, line_number, func_name, fmt, arp);
	va_end(arp);
	return ret;
}

int showMsgRaw(const char *fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	s32 ret = showMsgVA(NULL, 0, NULL, fmt, arp);
	va_end(arp);
	return ret;
}

int __attribute__((weak)) showMsgVA(const char *, int , const char *, const char *, va_list) {
	disp(100, DBG_CL_MSG);
	svcSleepThread(1000000000);

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
