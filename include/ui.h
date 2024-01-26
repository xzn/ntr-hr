#ifndef UI_H
#define UI_H

#include "3ds/types.h"

int showMsgRaw(char* msg);
#define showMsg(msg) showMsgVerbose(msg, __FILE__, __LINE__, __func__)
int showMsgVerbose(char* msg, const char *file_name, int line_number, const char *func_name);

#define showDbg(fmt, v1, v2) do { \
	char showDbg_buf__[LOCAL_DBG_BUF_SIZE]; \
	nsDbgPrint(fmt, v1, v2); \
	xsnprintf(showDbg_buf__, sizeof(showDbg_buf__), fmt, v1, v2); \
	showMsgVerbose(showDbg_buf__, __FILE__, __LINE__, __func__); \
} while (0)
void showDbgRaw(char* fmt, u32 v1, u32 v2);

void disp(u32 t, u32 cl);

#endif
