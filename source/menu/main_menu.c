#include "global.h"

#include "3ds/services/fs.h"
#include "3ds/services/soc.h"
#include "3ds/services/hid.h"
#include "3ds/srv.h"
#include "3ds/ipc.h"

#include <memory.h>

enum {
	MENU_HOTKEY_X_Y = KEY_X | KEY_Y,
	MENU_HOTKEY_L_START = KEY_L | KEY_START,
	MENU_HOTKEY_DEFAULT = MENU_HOTKEY_X_Y,
};

static u32 NTRMenuHotkey = MENU_HOTKEY_DEFAULT;
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

	ret = nsAttachProcess(hProcess, remotePC, &cfg, 0);
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

static int qtmPatched = 0;
static void disableQtmHeadTrackingForCurrentBoot(void) {
	if (qtmPatched) {
		showMsg("Already patched QTM");
		return;
	}

#define RP_QTM_HDR_SIZE (20)
	u8 desiredHeader[RP_QTM_HDR_SIZE] = {
		0x08, 0x00, 0x84, 0xe2,
		0x0e, 0x00, 0x90, 0xe8,
		0x14, 0x00, 0x94, 0xe5,
		0x0c, 0x00, 0x85, 0xe5,
		0x0e, 0x00, 0x85, 0xe8
	};
	u8 buf[RP_QTM_HDR_SIZE] = { 0 };

	Handle hProcess;
	s32 ret;
	u32 pid = 0x15; // QTM process

	ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		showDbg("Open QTM process failed: %08"PRIx32, ret);
		hProcess = 0;
		goto final;
	}

	u32 remotePC = 0x00119a54;
	ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, RP_QTM_HDR_SIZE);
	if (ret != 0) {
		showDbg("Read QTM memory at %08"PRIx32" failed: %08"PRIx32, remotePC, ret);
		goto final;
	}
	if (memcmp(buf, desiredHeader, RP_QTM_HDR_SIZE) != 0) {
		showDbg("Unexpected QTM memory content");
		goto final;
	}
	ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0);
	if (ret != 0) {
		showDbg("Locking QTM failed: %08"PRIx32"\n", ret);
		goto final;
	}

	u8 replacementMem[RP_QTM_HDR_SIZE] = {
		0x01, 0x00, 0xA0, 0xE3,
		0x00, 0x10, 0xA0, 0xE3,
		0x00, 0x20, 0xA0, 0xE3,
		0x00, 0x30, 0xA0, 0xE3,
		0x0F, 0x00, 0x85, 0xE8,
	};

	ret = protectRemoteMemory(hProcess, (void *)PAGE_OF_ADDR(remotePC), 0x1000, MEMPERM_READWRITE | MEMPERM_EXECUTE);
	if (ret != 0) {
		showDbg("QTM protectRemoteMemory failed: %08"PRIx32, ret);
		goto final_unlock;
	}

	ret = copyRemoteMemory(hProcess, (void *)remotePC, CUR_PROCESS_HANDLE, replacementMem, RP_QTM_HDR_SIZE);
	if (ret != 0) {
		showDbg("Write QTM memory at %08"PRIx32" failed: %08"PRIx32, remotePC, ret);
		goto final_unlock;
	}

	showMsg("Patch QTM successful");
	qtmPatched = 1;

final_unlock:
	ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 0, 0);
	if (ret != 0) {
		showDbg("Unlocking QTM process failed: %08"PRIx32"\n", ret);
	}

final:
	if (hProcess)
		svcCloseHandle(hProcess);
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

enum {
	PLUGIN_MENU_ENABLE_PLUGINS,
	PLUGIN_MENU_CTRPF_COMPAT,
	PLUGIN_MENU_REMOTE_PLAY_BOOST,
	PLUGIN_MENU_NO_LOADER_MEM,
	PLUGIN_MENU_COUNT,
};

static int pluginLoaderMenu(void) {
	const char *enablePlugins = plgTranslate("Game Plugin Loader (Disabled)");
	const char *disablePlugins = plgTranslate("Game Plugin Loader (Enabled)");
	const char *enableCTRPFCompatText = plgTranslate("CTRPF Compat Mode (Disabled)");
	const char *disableCTRPFCompatText = plgTranslate("CTRPF Compat Mode (Enabled)");
	const char *enableRPCallbackText = plgTranslate("Remote Play Boost (Disabled)");
	const char *disableRPCallbackText = plgTranslate("Remote Play Boost (Enabled)");
	const char *enableLoaderMemText = plgTranslate("Loader Mem Compat (Disabled)");
	const char *disableLoaderMemText = plgTranslate("Loader Mem Compat (Enabled)");

	const char *entries[PLUGIN_MENU_COUNT];
	const char *descs[PLUGIN_MENU_COUNT];
	descs[PLUGIN_MENU_ENABLE_PLUGINS] = plgTranslate("Whether game plugins will be loaded.");
	descs[PLUGIN_MENU_CTRPF_COMPAT] = plgTranslate("Avoid crash in CTRPF based plugins by\ndisabling all overlay features.");
	descs[PLUGIN_MENU_REMOTE_PLAY_BOOST] = plgTranslate("Improve Remote Play performance by\nusing overlay callback for screen\nupdate timing.\nIncompatible with CTRPF.");
	descs[PLUGIN_MENU_NO_LOADER_MEM] = plgTranslate("Keep enabled (default) for higher\ngame plugin compatibility.\nDisabling can help prevent game\nhanging but some plugins will crash.");

	s32 r = 0;
	while (1) {
		entries[PLUGIN_MENU_ENABLE_PLUGINS] = plgLoaderEx->noPlugins ? enablePlugins : disablePlugins;
		entries[PLUGIN_MENU_CTRPF_COMPAT] = plgLoaderEx->CTRPFCompat ? disableCTRPFCompatText : enableCTRPFCompatText;
		entries[PLUGIN_MENU_REMOTE_PLAY_BOOST] = plgLoaderEx->remotePlayBoost ? disableRPCallbackText : enableRPCallbackText;
		entries[PLUGIN_MENU_NO_LOADER_MEM] = plgLoaderEx->noLoaderMem ? enableLoaderMemText : disableLoaderMemText;

		r = showMenuEx("Plugin Loader", PLUGIN_MENU_COUNT, entries, descs, r);
		switch (r) {
			default:
				return 0;

			case PLUGIN_MENU_ENABLE_PLUGINS:
				plgLoaderEx->noPlugins = !plgLoaderEx->noPlugins;
				break;

			case PLUGIN_MENU_CTRPF_COMPAT:
				plgLoaderEx->CTRPFCompat = !plgLoaderEx->CTRPFCompat;
				if (plgLoaderEx->CTRPFCompat)
					plgLoaderEx->remotePlayBoost = 0;
				break;

			case PLUGIN_MENU_REMOTE_PLAY_BOOST:
				plgLoaderEx->remotePlayBoost = !plgLoaderEx->remotePlayBoost;
				if (plgLoaderEx->remotePlayBoost)
					plgLoaderEx->CTRPFCompat = 0;
				break;

			case PLUGIN_MENU_NO_LOADER_MEM:
				plgLoaderEx->noLoaderMem = !plgLoaderEx->noLoaderMem;
				break;

		}
	}
	return 0;
}

static int nfcPatched = 0;
static void rpDoNFCPatch(void) {
	if (nfcPatched) {
		showMsg("Already patched NFC");
		return;
	}

	int pid = 0x1a; // nwm process
	Handle hProcess;
	int ret;
	if ((ret = svcOpenProcess(&hProcess, pid))) {
		showMsg("Failed to open nwm process");
		return;
	}

	u32 addr = 0x0105AE4;
	u16 buf;
	if ((ret = rtCheckRemoteMemory(hProcess, addr, sizeof(buf), MEMPERM_READ))) {
		showMsg("Failed to protect nwm memory");
		goto final;
	}

	if ((ret = copyRemoteMemory(CUR_PROCESS_HANDLE, &buf, hProcess, (void *)addr, sizeof(buf)))) {
		showMsg("Failed to read nwm memory");
		goto final;
	}

	if (buf == 0x4620) {
		nsDbgPrint("patching NFC (11.4) firm\n");
		addr = 0x0105B00;
	} else {
		nsDbgPrint("patching NFC (<= 11.3) firm\n");
	}

	if ((ret = rtCheckRemoteMemory(hProcess, addr, sizeof(buf), MEMPERM_READWRITE | MEMPERM_EXECUTE))) {
		showMsg("Failed to protect nwm memory for write");
		goto final;
	}

	buf = 0x4770;
	if ((ret = copyRemoteMemory(hProcess, (void *)addr, CUR_PROCESS_HANDLE, &buf, sizeof(buf)))) {
		showMsg("Failed to write nwm memory");
		goto final;
	}

	showMsg("NFC patch success");
	nfcPatched = 1;

final:
	svcCloseHandle(hProcess);
	return;
}

enum {
	HOTKEY_ENTRY_X_Y,
	HOTKEY_ENTRY_L_START,

	HOTKEY_ENTRIES_COUNT,
};

static int setHotkeyMenu() {
	char const *entries[HOTKEY_ENTRIES_COUNT];
	entries[HOTKEY_ENTRY_X_Y] = "NTR Menu: X+Y";
	entries[HOTKEY_ENTRY_L_START] = "NTR Menu: L+START";

	u32 menu_hotkey_map[HOTKEY_ENTRIES_COUNT];
	menu_hotkey_map[HOTKEY_ENTRY_X_Y] = MENU_HOTKEY_X_Y;
	menu_hotkey_map[HOTKEY_ENTRY_L_START] = MENU_HOTKEY_L_START;
	int r = 0;

	for (int i = 0; i < HOTKEY_ENTRIES_COUNT; ++i) {
		if (menu_hotkey_map[i] == NTRMenuHotkey) {
			r = i;
			break;
		}
	}

	r = showMenuEx(NTR_CFW_VERSION, HOTKEY_ENTRIES_COUNT, entries, NULL, r);
	if (r >= 0 && r < HOTKEY_ENTRIES_COUNT) {
		NTRMenuHotkey = menu_hotkey_map[r];
		return 1;
	}
	return 0;
}

enum {
	MENU_ENTRY_REMOTETPLAY,
	MENU_ENTRY_PLUGIN_LOADER,
	MENU_ENTRY_HOTKEY,
	MENU_ENTRY_NFC_PATCH,
	MENU_ENTRY_QTM_PATCH,

	MENU_ENTRIES_COUNT,
	MENU_ENTRY_GAME_PLUGIN = MENU_ENTRIES_COUNT,

	MENU_ENTRIES_COUNT_GAME,
	MENU_ENTRIES_COUNT_MAX = MENU_ENTRIES_COUNT_GAME,
};

static void showMainMenu(void) {
	const char *entries[MENU_ENTRIES_COUNT_MAX];
	entries[MENU_ENTRY_REMOTETPLAY] = plgTranslate("Remote Play (New 3DS)");
	entries[MENU_ENTRY_PLUGIN_LOADER] = plgTranslate("Plugin Loader");
	entries[MENU_ENTRY_HOTKEY] = plgTranslate("Set Menu Hotkey");
	entries[MENU_ENTRY_NFC_PATCH] = plgTranslate("NFC Patch");
	entries[MENU_ENTRY_QTM_PATCH] = plgTranslate("QTM Disable");
	u32 count = MENU_ENTRIES_COUNT;

	if (loadGamePluginMenu() == 0) {
		entries[MENU_ENTRY_GAME_PLUGIN] = plgTranslate("Game Plugin");
		count = MENU_ENTRIES_COUNT_GAME;
	}

	const char *descs[MENU_ENTRIES_COUNT_MAX] = { 0 };
	descs[MENU_ENTRY_PLUGIN_LOADER] = "Changes in here need game restart to\ntake effect.";
	descs[MENU_ENTRY_NFC_PATCH] = "Allow remote play to continue in games\nsuch as USUM.";
	descs[MENU_ENTRY_QTM_PATCH] = "Disable head tracking for current boot\nto speed up remote play.";

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
		r = showMenuEx(title, count, entries, descs, r);

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

			case MENU_ENTRY_HOTKEY:
				if (setHotkeyMenu() != 0) {
					goto done;
				}
				break;

			case MENU_ENTRY_NFC_PATCH:
				releaseVideo();
				rpDoNFCPatch();
				acquireVideo();
				break;

			case MENU_ENTRY_QTM_PATCH:
				releaseVideo();
				disableQtmHeadTrackingForCurrentBoot();
				acquireVideo();
				break;

			default:
				goto done;
		}
	}
done:
	releaseVideo();
}

static char sbuf_msg_title[LOCAL_TITLE_BUF_SIZE];
static char sbuf_msg_msg[LOCAL_MSG_BUF_SIZE];

void handlePortCmd(u32 cmd_id, u32, u32, u32 *) {
	switch (cmd_id) {
		case SVC_MENU_CMD_DBG_PRINT:
			nsDbgPrint2(*sbuf_msg_title ? sbuf_msg_title : NULL, sbuf_msg_msg);
			break;

		case SVC_MENU_CMD_SHOW_MSG:
			showMsgRaw2(*sbuf_msg_title ? sbuf_msg_title : NULL, sbuf_msg_msg);
			break;
	}
}

void handlePortThreadPre(void) {
	u32 *sbuf = getThreadStaticBuffers();
	sbuf[0] = IPC_Desc_StaticBuffer(LOCAL_TITLE_BUF_SIZE, 0);
	sbuf[1] = (u32)sbuf_msg_title;
	sbuf[2] = IPC_Desc_StaticBuffer(LOCAL_MSG_BUF_SIZE, 1);
	sbuf[3] = (u32)sbuf_msg_msg;
}

static void createSvcHandleThread(void) {
	u32 *threadSvcStack = (u32 *)plgRequestMemory(SMALL_STACK_SIZE);
	Handle hSvcThread;
	s32 ret = svcCreateThread(&hSvcThread, handlePortThread, (u32)SVC_PORT_MENU, &threadSvcStack[(SMALL_STACK_SIZE / 4) - 10], 0x10, 1);
	if (ret != 0) {
		nsDbgPrint("Create menu service thread failed: %08"PRIx32"\n", ret);
	}
}

void nsDbgPrintVerboseVA(const char *file_name, int line_number, const char *func_name, const char* fmt, va_list arp) {
	nsDbgPrintVerboseVABuf(file_name, line_number, func_name, fmt, arp);
}

Result __sync_init(void);
void mainThread(void *) {
	Result ret;
	ret = __sync_init();
	if (ret != 0) {
		nsDbgPrint("sync init failed: %08"PRIx32"\n", ret);
		goto final;
	}

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

	createSvcHandleThread();

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
