#include "global.h"

#include "3ds/ipc.h"
#include "3ds/result.h"
#include "3ds/services/ptmsysm.h"
#include "3ds/services/fs.h"
#include "3ds/services/soc.h"
#include "3ds/services/hid.h"
#include "3ds/services/gspgpu.h"
#include "3ds/srv.h"
#include "3ds/ipc.h"
#include "main.h"

#include <memory.h>

enum {
	MENU_HOTKEY_X_Y = KEY_X | KEY_Y,
	MENU_HOTKEY_L_START = KEY_L | KEY_START,
	MENU_HOTKEY_DEFAULT = MENU_HOTKEY_X_Y,
};

static u32 NTRMenuHotkey = MENU_HOTKEY_DEFAULT;
static int cpuClockLockValue = -1;
static int cpuClockLocked;

static void lockCpuClock(void) {
	if (cpuClockLockValue < 0) {
		return;
	}
	svcKernelSetState(10, cpuClockLockValue);
	if (!cpuClockLocked) {
		cpuClockLockValue = -1;
	}
}

void setCpuClockLock(int v, int lock) {
	cpuClockLockValue = v;
	cpuClockLocked = lock;
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

	if (ntrConfig->isNew3DS)
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

	ret = nsAttachProcess(hProcess, remotePC, &cfg, 0, 0);
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
static int qtmPayloadAddr = 0;
static int qtmDisabled = 0;

// When enabling this patch, the qtm camera service usage is more or less disabled.
// This increases performance on New 3DS for remote play by up to 20%.
// Performance on New 3DS is still ~5% slower than New 2DS despite the improvement.
// Still trying to figure out where that last difference comes from...
// Note: head tracking is always active even if 3D is turned off, whether by slider or
// through system settings. Hence this patch is necessary for performance.
static void rpDoQTMPatchAndToggle(void) {
#define RP_QTM_HDR_SIZE (4)
	u8 desiredHeader[RP_QTM_HDR_SIZE] = {
		0x32, 0x00, 0x00, 0xef,
	};

	u32 remotePC = 0x00119a48;
	Handle hProcess;
	s32 ret;
	u32 pid = 0x15; // QTM process

	u8 buf[RP_QTM_HDR_SIZE] = { 0 };

	ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		showDbg("Open QTM process failed: %08"PRIx32, ret);
		hProcess = 0;
		goto final;
	}

	if (!qtmPatched) {
		ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, RP_QTM_HDR_SIZE);
		if (ret != 0) {
			showDbg("Read QTM memory at %08"PRIx32" failed: %08"PRIx32, remotePC, ret);
			goto final;
		}

		if (memcmp(buf, desiredHeader, RP_QTM_HDR_SIZE) != 0) {
			showDbg("Unexpected QTM memory content");
			goto final;
		}
	}

	ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0);
	if (ret != 0) {
		showDbg("Locking QTM failed: %08"PRIx32"\n", ret);
		goto final;
	}

#define RP_QTM_PAYLOAD_SIZE (32)
	u8 payload[RP_QTM_PAYLOAD_SIZE] = {
		0x01, 0x01, 0xA0, 0xE3, // mov r0, #0x40000000
		0x00, 0x10, 0xA0, 0xE3, // mov r1, #0
		0x0A, 0x00, 0x00, 0xEF, // svc #0xa
		0x00, 0x20, 0xA0, 0xE3, // mov r2, #0
		0x00, 0x30, 0xA0, 0xE3, // mov r3, #0
		0x0F, 0x00, 0x85, 0xE8, // stm r5, {r0, r1, r2, r3}
		0x70, 0x80, 0xBD, 0xE8, // ldmia sp!, {r4, r5, r6, pc}
		0x00, 0xF0, 0x20, 0xE3, // nop
	};

	if (!qtmPatched) {
		ret = rtCheckRemoteMemory(hProcess, remotePC, RP_QTM_HDR_SIZE, MEMPERM_READWRITE | MEMPERM_EXECUTE);
		if (ret != 0) {
			showDbg("QTM protectRemoteMemory failed: %08"PRIx32, ret);
			goto final_unlock;
		}

		u32 qtmBinEnd = 0x001ac000;

		u32 qtmPayloadAddrMin = qtmBinEnd - 0x800;
		u32 qtmPayloadAddrTry = qtmBinEnd - RP_QTM_PAYLOAD_SIZE;

		while (1) {
retry:
			if (qtmPayloadAddrTry < qtmPayloadAddrMin) {
				showDbg("Unable to find free space to install QTM payload\n");
				goto final_unlock;
			}

			u8 tmp[RP_QTM_PAYLOAD_SIZE] = { 0 };

			ret = copyRemoteMemory(CUR_PROCESS_HANDLE, tmp, hProcess, (void *)qtmPayloadAddrTry, RP_QTM_PAYLOAD_SIZE);
			if (ret != 0) {
				showDbg("Read QTM memory at %08"PRIx32" failed: %08"PRIx32, qtmPayloadAddrTry, ret);
				goto final_unlock;
			}

			for (unsigned i = 0; i < RP_QTM_PAYLOAD_SIZE / sizeof(u32); ++i) {
				if (((u32 *)tmp)[i]) {
					qtmPayloadAddrTry -= RP_QTM_PAYLOAD_SIZE;
					goto retry;
				}
			}

			break;
		}

		ret = rtCheckRemoteMemory(hProcess, qtmPayloadAddrTry, RP_QTM_PAYLOAD_SIZE, MEMPERM_READWRITE | MEMPERM_EXECUTE);
		if (ret != 0) {
			showDbg("QTM protectRemoteMemory for payload failed: %08"PRIx32, ret);
			goto final_unlock;
		}

		ret = copyRemoteMemory(hProcess, (void *)qtmPayloadAddrTry, CUR_PROCESS_HANDLE, payload, RP_QTM_PAYLOAD_SIZE);
		if (ret != 0) {
			showDbg("Write QTM memory for payload at %08"PRIx32" failed: %08"PRIx32, qtmPayloadAddrTry, ret);
			goto final_unlock;
		}

		qtmPayloadAddr = qtmPayloadAddrTry;
		qtmPatched = 1;
	}

	u32 pcDone = 0;
	u32 pcTries = 20;

	while (!pcDone && pcTries) {
		ret = nsCheckPCSafeToWrite(hProcess, remotePC);
		if (ret != 0) {
			ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 0, 0);
			if (ret != 0) {
				showDbg("Unlocking QTM process failed: %08"PRIx32"\n", ret);
				goto final;
			}

			svcSleepThread(50000000);
			--pcTries;

			ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0);
			if (ret != 0) {
				showDbg("Locking QTM failed: %08"PRIx32"\n", ret);
				goto final;
			}

			continue;
		}

		if (!qtmDisabled) {
			u32 branchDistance = qtmPayloadAddr - remotePC;

			u32 replacementInst = (branchDistance / 4 - 2) | 0xea000000; // b inst

			ret = copyRemoteMemory(hProcess, (void *)remotePC, CUR_PROCESS_HANDLE, &replacementInst, RP_QTM_HDR_SIZE);
			if (ret != 0) {
				showDbg("Write QTM memory at %08"PRIx32" failed: %08"PRIx32, remotePC, ret);
				goto final_unlock;
			}

			qtmDisabled = 1;
			showMsg("Patch QTM success");
		} else {
			ret = copyRemoteMemory(hProcess, (void *)remotePC, CUR_PROCESS_HANDLE, desiredHeader, RP_QTM_HDR_SIZE);
			if (ret != 0) {
				showDbg("Restore QTM memory at %08"PRIx32" failed: %08"PRIx32, remotePC, ret);
				goto final_unlock;
			}

			qtmDisabled = 0;
			showMsg("Restore QTM success");
		}

		pcDone = 1;
	}

	if (!pcDone)
		showDbg("Patching QTM process failed\n");

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
	PLUGIN_MENU_OVERLAY_STATS,
	PLUGIN_MENU_NO_LOADER_MEM,
	PLUGIN_MENU_COUNT,
};

static void pluginLoaderConfigPropagateProcess(int pid) {
	void *addr = plgLoaderEx;
	u32 len = sizeof(*plgLoaderEx);

	Handle hProcess;
	s32 ret = svcOpenProcess(&hProcess, pid);
	if (ret >= 0) {
		ret = rtCheckRemoteMemory(hProcess, (u32)addr, len, 0);
		if (ret == 0) {
			ret = copyRemoteMemory(hProcess, addr, CUR_PROCESS_HANDLE, addr, len);
		}
		svcCloseHandle(hProcess);
	}
}

static void pluginLoaderConfigPropagate(void) {
	if (plgLoader->gamePluginPid)
		pluginLoaderConfigPropagateProcess(plgLoader->gamePluginPid);

	pluginLoaderConfigPropagateProcess(RP_NWM_PROCESS); // nwm process
}

static int pluginLoaderMenu(void) {
	if (ntrConfig->memMode) {
		showMsg("Plugin loader is not available\nfor extended memory games.\nTry regular (non-HR) NTR 3.6\nversion if needed.");
		return 0;
	}

	const char *enablePlugins = plgTranslate("Game Plugin Loader (Disabled)");
	const char *disablePlugins = plgTranslate("Game Plugin Loader (Enabled)");
	const char *enableCTRPFCompatText = plgTranslate("CTRPF Compat Mode (Disabled)");
	const char *disableCTRPFCompatText = plgTranslate("CTRPF Compat Mode (Enabled)");
	const char *enableRPCallbackText = plgTranslate("Remote Play Boost (Disabled)");
	const char *disableRPCallbackText = plgTranslate("Remote Play Boost (Enabled)");
	const char *enableOvStatsText = plgTranslate("Overlay Stats (Disabled)");
	const char *disableOvStatsText = plgTranslate("Overlay Stats (Enabled)");
	const char *enableLoaderMemText = plgTranslate("Loader Mem Compat (Disabled)");
	const char *disableLoaderMemText = plgTranslate("Loader Mem Compat (Enabled)");

	const char *entries[PLUGIN_MENU_COUNT];
	const char *descs[PLUGIN_MENU_COUNT];
	descs[PLUGIN_MENU_ENABLE_PLUGINS] = plgTranslate("Whether game plugins will be loaded.");
	descs[PLUGIN_MENU_CTRPF_COMPAT] = plgTranslate("Avoid crash in CTRPF based plugins by\ndisabling all overlay features.");
	descs[PLUGIN_MENU_REMOTE_PLAY_BOOST] = plgTranslate("Improve Remote Play performance by\nusing overlay callback for screen\nupdate timing.\nIncompatible with CTRPF.");
	descs[PLUGIN_MENU_OVERLAY_STATS] = plgTranslate("Display remote play statistics.");
	descs[PLUGIN_MENU_NO_LOADER_MEM] = plgTranslate("Keep enabled (default) for higher\ngame plugin compatibility.\nDisabling can help prevent game\nhanging but some plugins will crash.");

	s32 r = 0;
	while (1) {
		entries[PLUGIN_MENU_ENABLE_PLUGINS] = plgLoaderEx->noPlugins ? enablePlugins : disablePlugins;
		entries[PLUGIN_MENU_CTRPF_COMPAT] = plgLoaderEx->CTRPFCompat ? disableCTRPFCompatText : enableCTRPFCompatText;
		entries[PLUGIN_MENU_REMOTE_PLAY_BOOST] = plgLoaderEx->remotePlayBoost ? disableRPCallbackText : enableRPCallbackText;
		entries[PLUGIN_MENU_OVERLAY_STATS] = plgLoaderEx->overlayStats ? disableOvStatsText : enableOvStatsText;
		entries[PLUGIN_MENU_NO_LOADER_MEM] = plgLoaderEx->noLoaderMem ? enableLoaderMemText : disableLoaderMemText;

		r = showMenuEx("Plugin Loader", PLUGIN_MENU_COUNT, entries, descs, r);
		switch (r) {
			default:
				pluginLoaderConfigPropagate();
				return 0;

			case PLUGIN_MENU_ENABLE_PLUGINS:
				plgLoaderEx->noPlugins = !plgLoaderEx->noPlugins;
				break;

			case PLUGIN_MENU_CTRPF_COMPAT:
				plgLoaderEx->CTRPFCompat = !plgLoaderEx->CTRPFCompat;
				if (plgLoaderEx->CTRPFCompat) {
					plgLoaderEx->remotePlayBoost = 0;
					plgLoaderEx->overlayStats = 0;
				}
				break;

			case PLUGIN_MENU_REMOTE_PLAY_BOOST:
				plgLoaderEx->remotePlayBoost = !plgLoaderEx->remotePlayBoost;
				if (plgLoaderEx->remotePlayBoost)
					plgLoaderEx->CTRPFCompat = 0;
				break;

			case PLUGIN_MENU_OVERLAY_STATS:
				plgLoaderEx->overlayStats = !plgLoaderEx->overlayStats;
				if (plgLoaderEx->overlayStats)
					plgLoaderEx->CTRPFCompat = 0;
				break;

			case PLUGIN_MENU_NO_LOADER_MEM:
				plgLoaderEx->noLoaderMem = !plgLoaderEx->noLoaderMem;
				break;

		}
	}
	return 0;
}

int nfcPatched = 0;
static void rpDoNFCPatch(void) {
	if (nfcPatched) {
		showMsg("Already patched NFC");
		return;
	}

	int pid = RP_NWM_PROCESS; // nwm process
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

	ASL(&nfcPatched, 1);
	showMsg("NFC patch success");

	rpCheckReliableStreamForNFC();

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

	r = showMenuEx("NTR Menu Hotkey", HOTKEY_ENTRIES_COUNT, entries, NULL, r);
	if (r >= 0 && r < HOTKEY_ENTRIES_COUNT) {
		NTRMenuHotkey = menu_hotkey_map[r];
		return 1;
	}
	return 0;
}

enum {
	CPU_MODE_ENTRY_268,
	CPU_MODE_ENTRY_804,
	CPU_MODE_ENTRY_268_L2,
	CPU_MODE_ENTRY_804_L2,
	CPU_MODE_ENTRIES_OPT_COUNT,

	CPU_MODE_ENTRY_APPLY = CPU_MODE_ENTRIES_OPT_COUNT,
	CPU_MODE_ENTRY_APPLY_LOCK,

	CPU_MODE_ENTRIES_COUNT,
};

#define CPU_MODE_ENTRY_268_TEXT "268 MHz"
#define CPU_MODE_ENTRY_804_TEXT "804 MHz"
#define CPU_MODE_ENTRY_268_L2_TEXT "268 MHz + L2 Cache"
#define CPU_MODE_ENTRY_804_L2_TEXT "804 MHz + L2 Cache"
#define CPU_MODE_ENTRY_SELECTED_TEXT " (Current)"
#define CPU_MODE_ENTRY_LOCKED_TEXT " (Locked)"
#define CPU_MODE_ENTRY_CHECK_TEXT(v, t) \
	(entries[v] = (cpuClockLocked && cpuClockLockValue == (v) ? t CPU_MODE_ENTRY_LOCKED_TEXT : selected == (v) ? t CPU_MODE_ENTRY_SELECTED_TEXT : t))

static void ntrCPUModeMenu() {
	if (!ntrConfig->isNew3DS) {
		showMsg("CPU mode is New 3DS only.");
		return;
	}

	char const *entries[CPU_MODE_ENTRIES_COUNT];
	int selected;
	if (cpuClockLocked) {
		selected = cpuClockLockValue;
	} else {
		s64 clk_current = 0, clk_higher = 0, l2_status = 0;
		svcGetSystemInfo(&clk_current, 0x10001, 0);
		svcGetSystemInfo(&clk_higher, 0x10001, 1);
		svcGetSystemInfo(&l2_status, 0x10001, 2);
		bool clk_status = clk_current == clk_higher;
		selected = (clk_status ? 1 : 0) + (l2_status ? 2 : 0);
	}

	entries[CPU_MODE_ENTRY_APPLY] = "Apply";
	entries[CPU_MODE_ENTRY_APPLY_LOCK] = "Apply & Lock";

	int r = 0;
	while (1) {
		CPU_MODE_ENTRY_CHECK_TEXT(CPU_MODE_ENTRY_268, CPU_MODE_ENTRY_268_TEXT);
		CPU_MODE_ENTRY_CHECK_TEXT(CPU_MODE_ENTRY_804, CPU_MODE_ENTRY_804_TEXT);
		CPU_MODE_ENTRY_CHECK_TEXT(CPU_MODE_ENTRY_268_L2, CPU_MODE_ENTRY_268_L2_TEXT);
		CPU_MODE_ENTRY_CHECK_TEXT(CPU_MODE_ENTRY_804_L2, CPU_MODE_ENTRY_804_L2_TEXT);

		r = showMenuEx("CPU Clock & L2 Cache", CPU_MODE_ENTRIES_COUNT, entries, NULL, r);
		if (r < 0) {
			break;
		}
		if (r < CPU_MODE_ENTRIES_OPT_COUNT) {
			cpuClockLockValue = selected = r;
			cpuClockLocked = 0;
			lockCpuClock();
		} else if (r == CPU_MODE_ENTRY_APPLY) {
			cpuClockLocked = 0;
			break;
		} else if (r == CPU_MODE_ENTRY_APPLY_LOCK) {
			cpuClockLockValue = selected;
			cpuClockLocked = 1;
			lockCpuClock();
			break;
		}
	}
};

enum {
	MENU_ENTRY_REMOTETPLAY,
	MENU_ENTRY_PLUGIN_LOADER,
	MENU_ENTRY_HOTKEY,
	MENU_ENTRY_CPU_MODE,
	MENU_ENTRY_NFC_PATCH,
	MENU_ENTRY_QTM_PATCH,

	MENU_ENTRIES_COUNT,
	MENU_ENTRY_GAME_PLUGIN = MENU_ENTRIES_COUNT,

	MENU_ENTRIES_COUNT_GAME,
	MENU_ENTRIES_COUNT_MAX = MENU_ENTRIES_COUNT_GAME,
};

static void showMainMenu(void) {
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
		const char *entries[MENU_ENTRIES_COUNT_MAX];
		entries[MENU_ENTRY_REMOTETPLAY] = plgTranslate("Remote Play (New 3DS)");
		entries[MENU_ENTRY_PLUGIN_LOADER] = plgTranslate("Plugin Loader");
		entries[MENU_ENTRY_HOTKEY] = plgTranslate("Set Menu Hotkey");
		entries[MENU_ENTRY_CPU_MODE] = plgTranslate("CPU Clock & Cache");
		entries[MENU_ENTRY_NFC_PATCH] = plgTranslate("NFC Patch");
		entries[MENU_ENTRY_QTM_PATCH] = qtmDisabled ? plgTranslate("QTM Enable") : plgTranslate("QTM Disable");
		u32 count = MENU_ENTRIES_COUNT;

		if (loadGamePluginMenu() == 0) {
			entries[MENU_ENTRY_GAME_PLUGIN] = plgTranslate("Game Plugin");
			count = MENU_ENTRIES_COUNT_GAME;
		}

		const char *descs[MENU_ENTRIES_COUNT_MAX] = { 0 };
		descs[MENU_ENTRY_PLUGIN_LOADER] = "Changes in here may need game restart\nto take effect.";
		descs[MENU_ENTRY_NFC_PATCH] = "Allow remote play to continue in games\nsuch as USUM.";
		descs[MENU_ENTRY_QTM_PATCH] = qtmDisabled ?
			"Restore head tracking." :
			"Disable head tracking for current boot\nto speed up remote play.\nNew 3DS only; no effect on New 2DS.";

		r = showMenuEx(title, count, entries, descs, r);

		switch (r) {
			case MENU_ENTRY_REMOTETPLAY: {
				u32 localAddr = gethostid();
				if (remotePlayMenu(localAddr) != 0) {
					goto done;
				}
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

			case MENU_ENTRY_CPU_MODE:
				ntrCPUModeMenu();
				break;

			case MENU_ENTRY_NFC_PATCH:
				releaseVideo();
				rpDoNFCPatch();
				acquireVideo();
				break;

			case MENU_ENTRY_QTM_PATCH:
				if (!ntrConfig->isNew3DS) {
					showMsg("QTM patch is New 3DS only.");
				} else {
					releaseVideo();
					rpDoQTMPatchAndToggle();
					acquireVideo();
				}
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

static u32 csnd_cap_buf_addr;
static u32 csnd_cap_buf_pa;

static int ensureCsndCapBuf(void)
{
	if (!csnd_cap_buf_addr) {
		csnd_cap_buf_addr = plgRequestMemoryLinear(CSND_CAP_BUF_SIZE);
		if (!csnd_cap_buf_addr) {
			nsDbgPrint("plgRequestMemoryLinear failed\n");
			return -1;
		}
	}

	if (!csnd_cap_buf_pa) {
		csnd_cap_buf_pa = svcConvertVAToPA((void *)csnd_cap_buf_addr, false);
		if (!csnd_cap_buf_pa) {
			nsDbgPrint("svcConvertVAToPA failed\n");
			return -1;
		}
	}

	return 0;
}

u32 handlePortCmd(u32 cmd_id, u32, u32, u32 *cmd1) {
	switch (cmd_id) {
		case SVC_MENU_CMD_DBG_PRINT:
			nsDbgPrint2(*sbuf_msg_title ? sbuf_msg_title : NULL, sbuf_msg_msg);
			break;

		case SVC_MENU_CMD_SHOW_MSG:
			showMsgRaw2(*sbuf_msg_title ? sbuf_msg_title : NULL, sbuf_msg_msg);
			break;

		case SVC_MENU_CMD_GET_CSND_CAP_BUF:
		{
			u32 cmd0;
			int res = ensureCsndCapBuf();
			if (res) {
				cmd1[1] = 0;
			} else {
				cmd1[1] = csnd_cap_buf_pa;
			}

			cmd0 = IPC_MakeHeader(cmd_id, 2, 0);
			*cmd1 = res;
			return cmd0;
		}
	}

	return 0;
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

static RT_HOOK menuRecvNotificationHook;

static Handle menuPtmSysmHandle;
#define PTM_SYSM_NAME "ptm:sysm"

static Handle *menuSrvHandle;

static Result menuSrvReceiveNotification(u32 *notificationIdOut) {
	Result rc = 0;
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0xB, 0, 0); // 0xB0000

	rc = svcSendSyncRequest(*menuSrvHandle);
	rc = R_SUCCEEDED(rc) ? (Result)cmdbuf[1] : rc;
	if (notificationIdOut)
		*notificationIdOut = R_SUCCEEDED(rc) ? cmdbuf[2] : 0;

	return rc;
}

static Result menuPTMSYSM_ReplyToSleepQuery(bool deny)
{
	Result ret;
	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(0x0402,1,2); // 0x04020042
	cmdbuf[1] = (u32)deny;
	cmdbuf[2] = IPC_Desc_CurProcessId();
	if(R_FAILED(ret = svcSendSyncRequest(menuPtmSysmHandle)))return ret;

	return (Result)cmdbuf[1];
}

static Result menuRecvNotificationCallback(u32* notificationIdOut) {
	Result res = menuSrvReceiveNotification(notificationIdOut);
	if (res != 0)
		return res;

	// showDbg("notification %"PRIx32" received", *notificationIdOut);
	switch (*notificationIdOut) {
		case PTMNOTIFID_SLEEP_REQUESTED:
			menuPTMSYSM_ReplyToSleepQuery(false);
			// fallthru
		case PTMNOTIFID_SHUTDOWN:
			waitKeysOverride = KEY_B;
			break;
		case PTMNOTIFID_SLEEP_DENIED:
		case PTMNOTIFID_FULLY_AWAKE:
			waitKeysOverride = 0;
			break;
		default:
			break;
	}
	return res;
}

typedef Result (*srvSubscribeTypedef)(u32 notificationId);
typedef Result (*srvGetServiceHandleTypedef)(Handle *out, const char *name, s32 name_len, u32 blocking_policy);

static u32 searchBytes(u32 startAddr, u32 endAddr, u8 *pat, u32 patlen, u32 step) {
	while (1) {
		if (startAddr + patlen > endAddr) {
			return 0;
		}
		if (memcmp((u32 *)startAddr, pat, patlen) == 0) {
			return startAddr;
		}
		startAddr += step;
	}
	return 0;
}

u32 findNearestSTMFD(u32 base, u32 pos) {
	if (pos < base) {
		return 0;
	}
	pos = pos - pos % 4;
	u32 term = pos - 0x1000;
	if (term < base) {
		term = base;
	}
	while (pos >= term) {
		if (*(u16 *)(pos + 2) == 0xe92d) {
			return pos;
		}
		pos -= 4;
	}
	return 0;
}

static void menuNotificationHook(void) {
	u32 codeStartAddress;
	u32 codeTotalSize;

	s64 textStartAddress, textTotalRoundedSize, rodataTotalRoundedSize, dataTotalRoundedSize;
	Handle processHandle = CUR_PROCESS_HANDLE;

	svcGetProcessInfo(&textTotalRoundedSize, processHandle, 0x10002);
	svcGetProcessInfo(&rodataTotalRoundedSize, processHandle, 0x10003);
	svcGetProcessInfo(&dataTotalRoundedSize, processHandle, 0x10004);

	svcGetProcessInfo(&textStartAddress, processHandle, 0x10005);

	codeTotalSize = (u32)(textTotalRoundedSize + rodataTotalRoundedSize + dataTotalRoundedSize);
	codeStartAddress = (u32)textStartAddress; //should be 0x00100000, rarely 0x14000000

	srvSubscribeTypedef menuSrvSubscribe;
	{
		u8 desiredPat[] = {
			0x10, 0x80, 0xbd, 0xe8, 0x40, 0x00, 0x09, 0x00,
		};

		u32 remotePC = searchBytes(codeStartAddress, codeStartAddress + codeTotalSize, desiredPat, sizeof(desiredPat), 4);
		if (remotePC != 0) {
			menuSrvHandle = (Handle *)*(u32 *)(remotePC + sizeof(desiredPat));
		}
		remotePC = findNearestSTMFD(remotePC - 0x2c, remotePC);

		if (remotePC == 0) {
			nsDbgPrint("Unexpected menu memory content\n");
			goto final;
		}

		menuSrvSubscribe = (srvSubscribeTypedef)remotePC;
	}

	srvGetServiceHandleTypedef menuSrvGetServiceHandle;
	{
		u8 desiredPat[] = {
			0x20, 0xc0, 0x9f, 0xe5, 0x04, 0xc0, 0x9c, 0xe5, 0x00, 0x00, 0x5c, 0xe3, 0x18, 0x00, 0x9f, 0xd5,
		};
		u32 remotePC = searchBytes(codeStartAddress, codeStartAddress + codeTotalSize, desiredPat, sizeof(desiredPat), 4);

		if (remotePC == 0) {
			nsDbgPrint("Unexpected menu memory content\n");
			goto final;
		}

		menuSrvGetServiceHandle = (srvGetServiceHandleTypedef)remotePC;
	}

	u32 remotePC = 0;
	u32 searchStart = codeStartAddress;
	u32 searchEnd = codeStartAddress + codeTotalSize;
	while (1) {
		u8 desiredPat[] = {
			0x0b, 0x08, 0xa0, 0xe3,
		};
		remotePC = searchBytes(searchStart, searchEnd, desiredPat, sizeof(desiredPat), 4);
		if (remotePC != 0) {
			u8 desiredPat[] = {
				0x08, 0x00, 0x94, 0xe5,
			};
			if (
				memcmp((void *)(remotePC + 0x1c), desiredPat, sizeof(desiredPat)) != 0 ||
				memcmp((void *)(remotePC + 0x2c), &menuSrvHandle, sizeof(menuSrvHandle)) != 0
			) {
				nsDbgPrint("Unexpected menu memory content\n");
				goto final_hook;
			}
		} else {
			break;
		}
		remotePC = findNearestSTMFD(remotePC - 0xc, remotePC);
		if (remotePC != 0) {
			break;
		}
final_hook:
		if (remotePC != 0) {
			searchStart = remotePC + 4;
		} else {
			break;
		}
	}

	if (remotePC == 0) {
		nsDbgPrint("Unexpected menu memory content\n");
		goto final;
	}

	u32 notifications[] = {
		PTMNOTIFID_SLEEP_REQUESTED,
		PTMNOTIFID_SHUTDOWN,
		PTMNOTIFID_SLEEP_DENIED,
		PTMNOTIFID_FULLY_AWAKE,
	};
	s32 ret;
	for (unsigned i = 0; i < sizeof(notifications) / sizeof(*notifications); ++i) {
		ret = menuSrvSubscribe(notifications[i]);
		if (ret != 0) {
			nsDbgPrint("menu %"PRIx32" notification subscribe failed: %08"PRIx32"\n", notifications[i], ret);
			goto final;
		}
	}

	ret = menuSrvGetServiceHandle(&menuPtmSysmHandle, PTM_SYSM_NAME, strlen(PTM_SYSM_NAME), 1);
	if (ret != 0) {
		nsDbgPrint("menu get ptm sysm handle failed: %08"PRIx32"\n", ret);
		goto final;
	}

	rtInitHook(&menuRecvNotificationHook, remotePC, (u32)menuRecvNotificationCallback);
	rtEnableHook(&menuRecvNotificationHook);

final:
	return;
}

static u32 rpGetCsndRemotePC(Handle hProcess) {
#define RP_CSND_HDR_SIZE (20)
	u8 desiredHeader[RP_CSND_HDR_SIZE] = {
		0x03, 0x4a, 0x91, 0x42, 0x01, 0xd1, 0x00, 0x20,
		0x70, 0x47, 0x80, 0x68, 0x40, 0x18, 0x70, 0x47,
		0xff, 0xff, 0x00, 0x00,
	};

	u8 buf[RP_CSND_HDR_SIZE] = { 0 };

	u32 remotePC = 0x001034c8;
	s32 ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, RP_CSND_HDR_SIZE);
	if (ret != 0) {
		nsDbgPrint("Read csnd memory at %08"PRIx32" failed: %08"PRIx32"\n", remotePC, ret);
	} else if (memcmp(buf, desiredHeader, RP_CSND_HDR_SIZE) == 0) {
		return remotePC;
	}

	return 0;
}

static int injectCSND(Handle hProcess, u32 remotePC) {
	NS_CONFIG cfg = { 0 };
	cfg.ntrConfig = *ntrConfig;
	cfg.startupInfo[11] = remotePC;

	return nsAttachProcess(hProcess, remotePC, &cfg, 1, 0);
}

static int injectToCSND(void) {
	u32 pid = 0x17; // CSND process on New3DS
	s32 ret;
	Handle hProcess;

	ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08"PRIx32"\n", ret);
		return ret;
	}

	u32 remotePC = rpGetCsndRemotePC(hProcess);
	if (!remotePC) {
		nsDbgPrint("Remote PC for CSND not found.\n");
		goto final;
	}

	int tries = 5;
	while ((ret = injectCSND(hProcess, remotePC)) != 0) {
		if (--tries == 0) {
			nsDbgPrint("Injecting to CSND process failed.\n");
			goto final;
		}
		svcSleepThread(1000000000);
	}

final:
	svcCloseHandle(hProcess);
	return ret;
}

void __system_initSyscalls(void);
Result __sync_init(void);
void mainThread(void *) {
	__system_initSyscalls();
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

	if (!ntrConfig->memMode) {
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
	}

	if (ntrConfig->isNew3DS) {
		ret = loadPayloadBin(NTR_BIN_CSND);
		if (ret != 0) {
			showDbg("Loading csnd payload failed.");
			goto final;
		}

		ret = injectToCSND();
		unloadPayloadBin();
		if (ret != 0)
			goto final;
	}

	if (ntrConfig->isNew3DS) {
		ret = loadPayloadBin(NTR_BIN_NWM);
	} else if (ntrConfig->memMode) {
		ret = loadPayloadBin(NTR_BIN_NWM_MEM3);
	} else {
		ret = loadPayloadBin(NTR_BIN_NWM_O3DS);
	}
	if (ret != 0) {
		showDbg("Loading nwm payload failed.");
		goto final;
	}

	if (ntrConfig->isNew3DS)
		createSvcHandleThread();

	nsConfig->initMode = NS_INITMODE_FROMBOOT;
	ret = nsStartup();
	if (ret != 0) {
		disp(100, DBG_CL_USE_DBG_FAIL);
		goto final;
	}

	plgInitScreenOverlayDirectly(*oldPC);

	menuNotificationHook();

	int waitCnt = 0;
	while (1) {
		if (getKeys() == NTRMenuHotkey) {
			if (canUseUI())
				showMainMenu();
		}
		svcSleepThread(100000000);
		waitCnt += 1;
		if (waitCnt % 8 == 0) {
			lockCpuClock();
			waitCnt = 0;
		}
	}

final:
	svcExitThread();
}

void plgSetBufferSwapHandle(u32 isDisplay1, u32 addr, u32 addrB, u32 stride, u32, u32 flushAlways) {
	u32 height = isDisplay1 ? GSP_SCREEN_HEIGHT_BOTTOM : GSP_SCREEN_HEIGHT_TOP;
	if (flushAlways) {
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)addr, stride * height);
		if ((isDisplay1 == 0) && (addrB) && (addrB != addr)) {
			svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)addrB, stride * height);
		}
	}
}

void nsHandlePacket(void) {
	nsHandleMenuPacket();
}

int setUpReturn(void) {
	return setUpReturn2();
}
