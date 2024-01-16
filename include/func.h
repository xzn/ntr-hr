#ifndef FUNC_H
#define FUNC_H


void setCpuClockLock(int v);
void lockCpuClock(void);
void setExitFlag(void);
void checkExitFlag(void);

void dumpRemoteProcess(u32 pid, char* fileName, u32 startAdd);
void dumpCode(u32 base, u32 size, char* fileName);
void processManager() ;
u32 protectMemory(void* addr, u32 size);
u32 protectRemoteMemory(Handle hProcess, void* addr, u32 size);
u32 copyRemoteMemory(Handle hDst, void* ptrDst, Handle hSrc, void* ptrSrc, u32 size);
u32 getProcessInfo(u32 pid, char* pname, u32 pname_size, u32 tid[], u32* kpobj);
u32 mapRemoteMemory(Handle hProcess, u32 addr, u32 size);
u32 controlMemoryInSysRegion(u32* outAddr, u32 addr0, u32 addr1, u32 size, u32 op, u32 perm);
u32 mapRemoteMemoryInSysRegion(Handle hProcess, u32 addr, u32 size, u32 op);
u32 writeRemoteProcessMemory(int pid, u32 addr, u32 size, u32* buf);

u32 getProcessTIDByHandle(u32 hProcess, u32 tid[]);
u32 getCurrentProcessId();
u32 getCurrentProcessHandle();
// Result getMemRegion(u32 *region, Handle hProcess);

int isInDebugMode(void);

void debounceKey(void);
void updateScreen(void);

#define showMsg(msg) showMsgExtra(msg, __FILE__, __LINE__, __func__)
int showMsgExtra(char* msg, const char *file_name, int line_number, const char *func_name);
int showMsgDirect(char* msg);
int showMsgNoPause(char* msg);
void acquireVideo(void) ;
void releaseVideo(void);
u32 waitKey(void);
u32 getKey(void);
void blinkColor(u32 c);
u32 initDirectScreenAccess(void);
void delayUi(void);
int drawString(char* str, int x, int y, u8 r, u8 g, u8 b, int newLine);
void print(char* s, int x, int y, u8 r, u8 g, u8 b);
void paint_letter(char letter, int x, int y, u8 r, u8 g, u8 b, int screen);
void mdelay(u32 m);
void disp(u32 t, u32 cl);

void mystrcat(char *a, char *b);
void myitoa(u32 a, char* b);
void dbg(char* key, u32 value);

void kernelCallback(u32 msr);
void kmemcpy(void* dst, void* src, u32 size) ;
void kSetCurrentKProcess(u32 ptr);
u32 kGetCurrentKProcess(void);
u32 kGetKProcessByHandle(u32 handle);
u32 kSwapProcessPid(u32 kProcess, u32 newPid) ;
void kRemotePlayCallback(void);
void kDoKernelHax(void);
void InvalidateEntireInstructionCache(void);
void InvalidateEntireDataCache(void);
void magicKillProcessByHandle(Handle hProcess);

void initFromInjectPM(void);
void initFromInjectGame(void);
void plgInitFromInjectHOME(void);
void screenshotMain(void);
void plgShowMainMenu(void);

typedef int(*drawStringTypeDef)  (char* str, int x, int y, u8 r, u8 g, u8 b, int newLine);
typedef char* (*translateTypeDef) (char* str);
char* plgTranslate(char* origText);

int plgTryUpdateConfig(void);
u32 plgRequestTempBuffer(u32 size);

extern Handle fsUserHandle;
extern FS_archive sdmcArchive;

extern u32 allowDirectScreenAccess;

extern u32 arm11BinStart;
extern u32 arm11BinSize;
extern u32 arm11BinProcess;

#define MAX_PLUGIN_COUNT 32

typedef struct _PLGLOADER_INFO {
	u32 plgCount;
	u32 plgBufferPtr[MAX_PLUGIN_COUNT];
	u32 plgSize[MAX_PLUGIN_COUNT];
	u32 arm11BinStart;
	u32 arm11BinSize;
	u32 tid[2];
	u32 gamePluginPid;
	u32 gamePluginMenuAddr;
	u32 currentLanguage;
	u32 nightShiftLevel;
} PLGLOADER_INFO;

#define MAX_GAME_PLUGIN_MENU_ENTRY 64
typedef struct _GAME_PLUGIN_MENU {
	u8 state[MAX_GAME_PLUGIN_MENU_ENTRY];
	u16 offsetInBuffer[MAX_GAME_PLUGIN_MENU_ENTRY];
	u16 bufOffset, count;
	u8 buf[3000];
} GAME_PLUGIN_MENU;




#define CURRENT_PROCESS_HANDLE	0xffff8001


#define REG(x)   (*(volatile u32*)(x))
#define REG8(x)  (*(volatile  u8*)(x))
#define REG16(x) (*(volatile u16*)(x))
#define SW(addr, data)  *(u32*)(addr) = data

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void memcpy_ctr(void* dst, void* src, size_t size);


#endif