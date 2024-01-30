#ifndef UI_H
#define UI_H

#include "3ds/types.h"

int showMsgVA(const char *file_name, int line_number, const char *func_name, const char* fmt, va_list va);
int showMsgRaw(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define showMsg(fmt, ...) showMsgVerbose(__FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
int showMsgVerbose(const char *file_name, int line_number, const char *func_name, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

#define showDbg(fmt, ...) do { \
	if (!showDbgFunc) \
		nsDbgPrint(fmt, ## __VA_ARGS__); \
	showMsg(fmt, ## __VA_ARGS__); \
} while (0)

#define showDbgRaw(fmt, ...) do { \
	if (!showDbgFunc) \
		nsDbgPrintRaw(fmt, ## __VA_ARGS__); \
	showMsgRaw(fmt, ## __VA_ARGS__); \
} while (0)

void disp(u32 t, u32 cl);

extern u32 hasDirectScreenAccess;
int initDirectScreenAccess(void);
void acquireVideo(void);
void releaseVideo(void);
void updateScreen(void);
int canUseUI(void);

const char *plgTranslate(const char *msg);

s32 showMenu(const char *title, u32 entriesCount, const char *captions[]);
s32 showMenuEx(const char *title, u32 entriesCount, const char *captions[], const char *descriptions[], u32 selectOn);
s32 showMenuEx2(const char *title, u32 entriesCount, const char *captions[], const char *descriptions[], u32 selectOn, u32 *keysPressed);

u32 waitKeys(void);

#endif
