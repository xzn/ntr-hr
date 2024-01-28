#ifndef MAIN_H
#define MAIN_H

#include "3ds/types.h"
#include "ntr_config.h"

int main(void);

extern NTR_CONFIG *ntrConfig;

typedef void (*showDbgFunc_t)(char *);
extern showDbgFunc_t showDbgFunc;

extern u32 arm11BinStart;
extern u32 arm11BinSize;
int loadPayloadBin(char *name);
void unloadPayloadBin(void);

u32 plgPoolAlloc(u32 size);
int plgPoolFree(u32 addr, u32 size); // Must only free last alloc'ed
u32 plgRequestMemory(u32 size);

void setCpuClockLock(int v);

#define MAX_PLUGIN_COUNT 32
typedef struct {
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

	// Plugin's PLGLOADER_INFO ends here
} PLGLOADER_INFO;

#define MAX_GAME_PLUGIN_MENU_ENTRY 64
typedef struct {
	u8 state[MAX_GAME_PLUGIN_MENU_ENTRY];
	u16 offsetInBuffer[MAX_GAME_PLUGIN_MENU_ENTRY];
	u16 bufOffset, count;
	u8 buf[3000];
} GAME_PLUGIN_MENU;

extern PLGLOADER_INFO *plgLoaderInfo;

void rpSetGamePid(u32 gamePid);
int nsDbgNext(void);
u32 plgRegisterCallback(u32 type, void* callback, u32);
u32 plgSetValue(u32 index, u32 value);
u32 plgGetIoBase(u32 IoBase);

#endif
