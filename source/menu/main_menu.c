#include "global.h"

#include "3ds/services/fs.h"
#include "3ds/services/soc.h"
#include "3ds/services/hid.h"
#include "3ds/srv.h"

#include <memory.h>

static u32 NTRMenuHotkey = KEY_X | KEY_Y;
static int cpuClockLockValue = -1;

static void lockCpuClock(void) {
	if (cpuClockLockValue < 0) {
		return;
	}
	svcKernelSetState(10, cpuClockLockValue);
}

void setCpuClockLock(int v) {
	cpuClockLockValue = v;
}

typedef u32 (*FSReadTypeDef)(u32 a1, u32 a2, u32 a3, u32 a4, u32 buffer, u32 size);
typedef u32 (*aptPrepareToStartApplicationTypeDef)(u32 a1, u32 a2, u32 a3);

static RT_HOOK HomeFSReadHook;
static RT_HOOK HomeCardUpdateInitHook;
static RT_HOOK aptPrepareToStartApplicationHook;

static u32 HomeFSReadCallback(u32 a1, u32 a2, u32 a3, u32 a4, u32 buffer, u32 size) {
	u32 ret;
	ret = ((FSReadTypeDef)HomeFSReadHook.callCode)(a1, a2, a3, a4, buffer, size);
	if (size == 0x36C0) {
		if (*(u32*)buffer == 0x48444d53) { // 'SMDH'
			nsDbgPrint("patching smdh\n");
			*(u32*)(buffer + 0x2018) = 0x7fffffff;
		}
	}
	return ret;
}

static u32 HomeCardUpdateInitCallback(void) {
	return 0xc821180b; // card update is not needed
}

static u32 gamePluginMenuSelect;

static u32 aptPrepareToStartApplicationCallback(u32 a1, u32 a2, u32 a3) {
	u32* tid = (u32*)a1;
	nsDbgPrint("Starting app (title ID): %08"PRIx32"%08"PRIx32"\n", tid[1], tid[0]);
	*plgLoader = (PLGLOADER_INFO){ 0 };
	plgLoader->tid[0] = tid[0];
	plgLoader->tid[1] = tid[1];
	gamePluginMenuSelect = 0;

	rpSetGamePid(0);

	s32 res = ((aptPrepareToStartApplicationTypeDef)aptPrepareToStartApplicationHook.callCode)(a1, a2, a3);
	return res;
}

static int injectPM(void) {
	u32 pid = ntrConfig->PMPid;
	s32 ret;
	Handle hProcess;
	u32 remotePC = ntrConfig->PMSvcRunAddr;

	ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08"PRIx32"\n", ret);
		return ret;
	}
	NS_CONFIG cfg = { 0 };
	cfg.ntrConfig = *ntrConfig;

	ret = nsAttachProcess(hProcess, remotePC, &cfg);
	svcCloseHandle(hProcess);
	return ret;
}

static int injectToPM(void) {
	rtInitHook(&aptPrepareToStartApplicationHook, ntrConfig->HomeAptStartAppletAddr, (u32)aptPrepareToStartApplicationCallback);
	rtEnableHook(&aptPrepareToStartApplicationHook);

	int tries = 5;
	while (injectPM() != 0) {
		if (--tries == 0) {
			showDbg("Injecting to PM process failed.");
			return -1;
		}
		svcSleepThread(1000000000);
	}
	return 0;
}

static GAME_PLUGIN_MENU gamePluginMenu;
static u32 gamePid;
static u32 gamePluginMenuAddr;
static Handle hGameProcess;

static void closeGameProcess(void) {
	if (hGameProcess != 0) {
		svcCloseHandle(hGameProcess);
		hGameProcess = 0;
	}
	gamePluginMenuAddr = gamePid = 0;
}

static int openGameProcess(void) {
	closeGameProcess();

	gamePid = plgLoader->gamePluginPid;
	gamePluginMenuAddr = plgLoader->gamePluginMenuAddr;
	if (gamePid == 0 || gamePluginMenuAddr == 0) {
		gamePluginMenuAddr = gamePid = 0;
		return -1;
	}
	u32 ret = 0;
	ret = svcOpenProcess(&hGameProcess, gamePid);
	if (ret != 0) {
		return ret;
	}
	return 0;
}

static int loadGamePluginMenu(void)  {
	s32 ret = openGameProcess();
	if (ret != 0) {
		return ret;
	}
	ret = copyRemoteMemory(CUR_PROCESS_HANDLE, &gamePluginMenu, hGameProcess, (void *)gamePluginMenuAddr, sizeof(GAME_PLUGIN_MENU));
	closeGameProcess();
	return ret;
}

static int storeGamePluginMenuState(void) {
	s32 ret = openGameProcess();
	if (ret != 0) {
		return ret;
	}
	ret = copyRemoteMemory(hGameProcess, (u8 *)gamePluginMenuAddr + offsetof(GAME_PLUGIN_MENU, state), CUR_PROCESS_HANDLE, gamePluginMenu.state, sizeof(gamePluginMenu.state));
	closeGameProcess();
	return ret;
}

static void showGamePluginMenu(void) {
	char const*entries[MAX_GAME_PLUGIN_MENU_ENTRY];
	char const*descs[MAX_GAME_PLUGIN_MENU_ENTRY];
	char *buf;

	while (1) {
		if (gamePluginMenu.count <= 0) {
			return;
		}

		for (u32 i = 0; i < gamePluginMenu.count; ++i) {
			buf = (char *)&gamePluginMenu.buf[gamePluginMenu.offsetInBuffer[i]];
			descs[i] = 0;
			entries[i] = buf;
			size_t remainLenMax = GAME_PLUGIN_MENU_BUF_SIZE - (buf - (char *)gamePluginMenu.buf);
			size_t bufLen = strnlen(buf, remainLenMax);
			if (bufLen == remainLenMax) {
				--bufLen;
				buf[bufLen] = 0;
			}
			for (size_t j = 0; j < bufLen; ++j) {
				if (buf[j] == 0xff) {
					buf[j] = 0;
					descs[i] = &buf[j + 1];
					break;
				}
			}
		}

		int r;
		r = showMenuEx(
			plgTranslate("Game Plugin Config"), gamePluginMenu.count, entries, descs,
			gamePluginMenuSelect);
		if (r < 0)
			return;

		gamePluginMenu.state[r] = 1;
		gamePluginMenuSelect = r;

		storeGamePluginMenuState();
		releaseVideo();
		svcSleepThread(500000000);
		acquireVideo();
		int ret = loadGamePluginMenu();

		if (ret != 0) {
			return;
		}
	}
}

static int pluginLoaderMenu(void) {
	// TODO
	return 0;
}

static int remotePlayMenu(u32) {
	// TODO
	return 0;
}

enum {
	MENU_ENTRY_REMOTETPLAY,
	MENU_ENTRY_PLUGIN_LOADER,

	MENU_ENTRIES_COUNT,
	MENU_ENTRY_GAME_PLUGIN = MENU_ENTRIES_COUNT,

	MENU_ENTRIES_COUNT_GAME,
	MENU_ENTRIES_COUNT_MAX = MENU_ENTRIES_COUNT_GAME,
};

static void showMainMenu(void) {
	const char *entries[MENU_ENTRIES_COUNT_MAX];
	entries[MENU_ENTRY_REMOTETPLAY] = plgTranslate("Remote Play (New 3DS)");
	entries[MENU_ENTRY_PLUGIN_LOADER] = plgTranslate("Plugin Loader");
	u32 count = MENU_ENTRIES_COUNT;

	if (loadGamePluginMenu() == 0) {
		entries[MENU_ENTRY_GAME_PLUGIN] = plgTranslate("Game Plugin");
		count = MENU_ENTRIES_COUNT_GAME;
	}

	u32 localAddr = gethostid();

	char title[LOCAL_TITLE_BUF_SIZE];
	if (openGameProcess() == 0) {
		closeGameProcess();
		xsnprintf(title, LOCAL_TITLE_BUF_SIZE, "%s (Game PID: %"PRIx32")", NTR_CFW_VERSION, plgLoader->gamePluginPid);
	} else {
		strncpy(title, NTR_CFW_VERSION, LOCAL_TITLE_BUF_SIZE);
	}

	acquireVideo();
	s32 r = 0;
	while (1) {
		r = showMenuEx(title, count, entries, NULL, r);

		switch (r) {
			case MENU_ENTRY_REMOTETPLAY:
				if (remotePlayMenu(localAddr) != 0) {
					goto done;
				}
				break;

			case MENU_ENTRY_PLUGIN_LOADER:
				if (pluginLoaderMenu() != 0) {
					goto done;
				}
				break;

			case MENU_ENTRY_GAME_PLUGIN:
				showGamePluginMenu();
				goto done;

			default:
				goto done;
		}
	}
done:
	releaseVideo();
}

void mainThread(void *) {
	Result ret;
	ret = initDirectScreenAccess();
	if (ret != 0) {
		disp(100, DBG_CL_MSG);
		svcSleepThread(1000000000);
	}

	if (plgLoaderInfoAlloc() != 0)
		goto final;
	*plgLoader = (PLGLOADER_INFO){ 0 };

	ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		goto final;
	}

	// This handle may be short-lived so there may be a race condition here...
	Handle fsUserHandle = ntrConfig->HomeFSUHandleAddr ?
		*(u32 *)ntrConfig->HomeFSUHandleAddr : 0;
	if (fsUserHandle == 0) {
		ret = fsInit();
		if (ret != 0) {
			showDbg("Failed to initialize fs.");
			goto final;
		}
	} else {
		fsUseSession(fsUserHandle);
	}
	ret = loadPayloadBin(NTR_BIN_PM);
	if (ret != 0) {
		showDbg("Loading pm payload failed.");
		goto final;
	}

	rtInitHook(&HomeFSReadHook, ntrConfig->HomeFSReadAddr, (u32)HomeFSReadCallback);
	rtEnableHook(&HomeFSReadHook);
	rtInitHook(&HomeCardUpdateInitHook, ntrConfig->HomeCardUpdateInitAddr, (u32)HomeCardUpdateInitCallback);
	rtEnableHook(&HomeCardUpdateInitHook);

	ret = injectToPM();
	unloadPayloadBin();

	if (ret != 0)
		goto final;

	ret = loadPayloadBin(NTR_BIN_NWM);
	if (ret != 0) {
		showDbg("Loading nwm payload failed.");
		goto final;
	}

	nsConfig->initMode = NS_INITMODE_FROMBOOT;
	ret = nsStartup();
	if (ret != 0) {
		disp(100, DBG_CL_USE_DBG_FAIL);
		goto final;
	}

	plgInitScreenOverlayDirectly(*oldPC);

	int waitCnt = 0;
	while (1) {
		if (getKeys() == NTRMenuHotkey) {
			if (canUseUI())
				showMainMenu();
		}
		svcSleepThread(100000000);
		waitCnt += 1;
		if (waitCnt % 10 == 0) {
			lockCpuClock();
			waitCnt = 0;
		}
	}

final:
	svcExitThread();
}

void plgSetBufferSwapHandle(u32, u32, u32, u32, u32) {}

void nsHandlePacket(void) {
	nsHandleMenuPacket();
}

int setUpReturn(void) {
	return setUpReturn2();
}
