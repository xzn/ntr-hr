#ifndef SHARED_FUNC_H
#define SHARED_FUNC_H

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

#define nsDbgPrint(fmt, ...) do { \
    u64 nsDbgPrint_ticks__ = svc_getSystemTick(); \
	u64 nsDbgPrint_mono_us__ = nsDbgPrint_ticks__ / SYSTICK_PER_US; \
	u32 nsDbgPrint_pid__ = getCurrentProcessId(); \
    nsDbgPrintShared("[%d.%d][%x]%s:%d:%s " fmt, (u32)(nsDbgPrint_mono_us__ / 1000000), (u32)(nsDbgPrint_mono_us__ % 1000000), nsDbgPrint_pid__, __FILE__, __LINE__, __func__, ## __VA_ARGS__); \
} while (0)
void nsDbgPrintShared(const char* fmt, ...);

#define showDbg(fmt, v1, v2) do { \
	u8 showDbg_buf__[400]; \
 \
	nsDbgPrint(fmt, v1, v2); \
	xsprintf(showDbg_buf__, fmt, v1, v2); \
	showMsgExtra(showDbg_buf__, __FILE__, __LINE__, __func__); \
} while (0)
void showDbgShared(u8* fmt, u32 v1, u32 v2);

u32 controlVideo(u32 cmd, u32 arg1, u32 arg2, u32 arg3);
#define CONTROLVIDEO_ACQUIREVIDEO 1
#define CONTROLVIDEO_RELEASEVIDEO 2
#define CONTROLVIDEO_GETFRAMEBUFFER 3
#define CONTROLVIDEO_SETFRAMEBUFFER 4
#define CONTROLVIDEO_UPDATESCREEN 5

s32 showMenuEx(u8* title, u32 entryCount, u8* captions[], u8* descriptions[], u32 selectOn);

s32 showMenuEx2(u8* title, u32 entryCount, u8* captions[], u8* descriptions[], u32 selectOn, u32 *keyPressed);

u32 copyRemoteMemory(Handle hDst, void* ptrDst, Handle hSrc, void* ptrSrc, u32 size);

#endif