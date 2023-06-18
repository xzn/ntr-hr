#ifndef SHARED_FUNC_H
#define SHARED_FUNC_H

#include "ctr/types.h"

#if IS_PLUGIN
#define INIT_SHARED_FUNC(name,id) rtGenerateJumpCode(((NS_CONFIG*)(NS_CONFIGURE_ADDR))->sharedFunc[id], (void*) name);rtFlushInstructionCache((void*) name, 8);
#else
#define INIT_SHARED_FUNC(name,id) (g_nsConfig->sharedFunc[id] = (u32) name)
#endif

void initSharedFunc();

u32 plgRegisterMenuEntry(u32 catalog, char* title, void* callback) ;
u32 plgGetSharedServiceHandle(char* servName, u32* handle);
u32 plgRequestMemory(u32 size);
u32 plgRequestMemorySpecifyRegion(u32 size, int sysRegion);
u32 plgRegisterCallback(u32 type, void* callback, u32 param0);

#define CALLBACK_OVERLAY 101


u32 plgGetIoBase(u32 IoType);

#define IO_BASE_PAD		1
#define IO_BASE_LCD		2
#define IO_BASE_PDC		3
#define IO_BASE_GSPHEAP		4
#define IO_BASE_HOME_MENU_PID 5
#define VALUE_CURRENT_LANGUAGE 6
#define VALUE_DRAWSTRING_CALLBACK 7
#define VALUE_TRANSLATE_CALLBACK 8

u32 plgSetValue(u32 index, u32 value);


void showDbg(u8* fmt, u32 v1, u32 v2);
void nsDbgPrint (const char*	fmt,	...	);

u32 controlVideo(u32 cmd, u32 arg1, u32 arg2, u32 arg3);
#define CONTROLVIDEO_ACQUIREVIDEO 1
#define CONTROLVIDEO_RELEASEVIDEO 2
#define CONTROLVIDEO_GETFRAMEBUFFER 3
#define CONTROLVIDEO_SETFRAMEBUFFER 4
#define CONTROLVIDEO_UPDATESCREEN 5

s32 showMenuEx(u8* title, u32 entryCount, u8* captions[], u8* descriptions[], u32 selectOn);

u32 copyRemoteMemory(Handle hDst, void* ptrDst, Handle hSrc, void* ptrSrc, u32 size);

#endif