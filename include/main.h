#ifndef MAIN_H
#define MAIN_H

#include "3ds/types.h"
#include "ntr_config.h"
#include "ns.h"

void mainThread(void *);
void mainInit(void);
int main(void);

static NTR_CONFIG *const ntrConfig = &nsConfig->ntrConfig;

typedef void (*showDbgFunc_t)(const char *);
extern showDbgFunc_t showDbgFunc;
int showMsgDbgFunc(const char *msg);

extern u32 arm11BinStart;
extern u32 arm11BinSize;
int loadPayloadBin(char *name);
void unloadPayloadBin(void);

int plgEnsurePoolSize(u32 size);
u32 plgRequestMemory(u32 size);

void setCpuClockLock(int v);
extern u32 hasHIDAccess;

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
#define GAME_PLUGIN_MENU_BUF_SIZE 3000
typedef struct {
	u8 state[MAX_GAME_PLUGIN_MENU_ENTRY];
	u16 offsetInBuffer[MAX_GAME_PLUGIN_MENU_ENTRY];
	u16 bufOffset, count;
	u8 buf[GAME_PLUGIN_MENU_BUF_SIZE];
} GAME_PLUGIN_MENU;

static PLGLOADER_INFO *const plgLoader = (PLGLOADER_INFO *)PLG_POOL_ADDR;
static PLGLOADER_EX_INFO *const plgLoaderEx = &ntrConfig->ex.plg;

void rpSetGamePid(u32 gamePid);
int nsDbgNext(void);
u32 plgRegisterCallback(u32 type, void* callback, u32);
u32 plgSetValue(u32 index, u32 value);
u32 plgGetIoBase(u32 IoBase);

#endif
