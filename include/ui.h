#ifndef UI_H
#define UI_H

#include "3ds/types.h"

int showMsgRaw(const char *msg);
#define showMsg(msg) showMsgVerbose(msg, __FILE__, __LINE__, __func__)
int showMsgVerbose(const char *msg, const char *file_name, int line_number, const char *func_name);

#define showDbg(fmt, ...) do { \
	char showDbg_buf__[LOCAL_MSG_BUF_SIZE]; \
	nsDbgPrint(fmt, ## __VA_ARGS__); \
	xsnprintf(showDbg_buf__, sizeof(showDbg_buf__), fmt, ## __VA_ARGS__); \
	showMsgVerbose(showDbg_buf__, __FILE__, __LINE__, __func__); \
} while (0)

#define showDbgRaw(fmt, ...) do { \
	char showDbg_buf__[LOCAL_MSG_BUF_SIZE]; \
	nsDbgPrintRaw(fmt, ## __VA_ARGS__); \
	xsnprintf(showDbg_buf__, sizeof(showDbg_buf__), fmt, ## __VA_ARGS__); \
	showMsg(showDbg_buf__); \
} while (0)

void disp(u32 t, u32 cl);

extern u32 allowDirectScreenAccess;
int initDirectScreenAccess(void);

const char *plgTranslate(const char *msg);

s32 showMenu(const char *title, u32 entriesCount, const char *captions[]);
s32 showMenuEx(const char *title, u32 entriesCount, const char *captions[], const char *descriptions[], u32 selectOn);
s32 showMenuEx2(const char *title, u32 entriesCount, const char *captions[], const char *descriptions[], u32 selectOn, u32 *keyPressed);

u32 getKey(void);
u32 waitKey(void);

#endif
