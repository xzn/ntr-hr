#ifndef UI_H
#define UI_H

#include "3ds/types.h"

int showMsgRaw(const char *msg);
#define showMsg(msg) showMsgVerbose(msg, __FILE__, __LINE__, __func__)
int showMsgVerbose(const char *msg, const char *file_name, int line_number, const char *func_name);

#define showDbg(fmt, ...) do { \
	char showDbg_buf__[LOCAL_DBG_BUF_SIZE]; \
	nsDbgPrint(fmt, ## __VA_ARGS__); \
	xsnprintf(showDbg_buf__, sizeof(showDbg_buf__), fmt, ## __VA_ARGS__); \
	showMsgVerbose(showDbg_buf__, __FILE__, __LINE__, __func__); \
} while (0)

#define showDbgRaw(fmt, ...) do { \
	char showDbg_buf__[LOCAL_DBG_BUF_SIZE]; \
	nsDbgPrintRaw(fmt, ## __VA_ARGS__); \
	xsnprintf(showDbg_buf__, sizeof(showDbg_buf__), fmt, ## __VA_ARGS__); \
	showMsg(showDbg_buf__); \
} while (0)

void disp(u32 t, u32 cl);

extern u32 allowDirectScreenAccess;
int initDirectScreenAccess(void);
void acquireVideo(void);
void releaseVideo(void);
void updateScreen(void);

int print(char *s, int x, int y, u8 r, u8 g, u8 b);

u32 getKey(void);
u32 waitKey(void);

#endif
