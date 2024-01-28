#include "global.h"

#include "3ds/services/fs.h"

#include <memory.h>

static Handle hMenuProcess = 0;
static Handle getMenuProcess(void) {
	if (hMenuProcess == 0) {
		s32 ret = svcOpenProcess(&hMenuProcess, ntrConfig->HomeMenuPid);
		if (ret != 0) {
			hMenuProcess = 0;
			nsDbgPrint("Open menu process failed: %08x\n", ret);
		}
	}
	return hMenuProcess;
}

static int pmLoadPluginsForGame(void) {
	// TODO
	return 0;
}

static void pmUnloadPluginsForGame(void) {
}

static Handle loaderMemGameHandle;
static u32 loaderMemPoolSize;

static int pmFreeLoaderMemPool(int keepHandle) {
	u32 ret = 0;
	if (loaderMemGameHandle) {
		ret = mapRemoteMemoryInLoader(loaderMemGameHandle, (u32)plgLoader, loaderMemPoolSize, MEMOP_FREE);
		if (ret != 0) {
			nsDbgPrint("Free loader mem failed: %08x\n", ret);
		}
		if (!keepHandle)
			svcCloseHandle(loaderMemGameHandle);
		loaderMemGameHandle = 0;
		loaderMemPoolSize = 0;
	}
	return ret;
}

static int pmAllocLoaderMemPool(Handle hGameProcess, int loaderMem) {
	s32 ret;
	if (loaderMem)
		ret = mapRemoteMemoryInLoader(hGameProcess, (u32)plgLoader, plgLoaderEx->plgMemSizeTotal, MEMOP_ALLOC);
	else
		ret = mapRemoteMemory(hGameProcess, (u32)plgLoader, plgLoaderEx->plgMemSizeTotal);
	if (ret != 0) {
		nsDbgPrint("Alloc plugin memory failed: %08x\n", ret);
		return ret;
	}

	if (loaderMem) {
		loaderMemPoolSize = plgLoaderEx->plgMemSizeTotal;
		ret = svcDuplicateHandle(&loaderMemGameHandle, hGameProcess);
		if (ret != 0) {
			nsDbgPrint("Dupping process handle failed: %08x\n", ret);
			loaderMemGameHandle = hGameProcess;
			pmFreeLoaderMemPool(1);
			return ret;
		}
	}
	return 0;
}

static int pmLoadFromMenu(void *addr, u32 size) {
	s32 ret;
	ret = copyRemoteMemory(CUR_PROCESS_HANDLE, addr, hMenuProcess, addr, size);
	return ret;
}

static int pmSaveToMenu(void *addr, u32 size) {
	s32 ret;
	ret = copyRemoteMemory(hMenuProcess, addr, CUR_PROCESS_HANDLE, addr, size);
	return ret;
}

static int pmInitGamePlg(Handle hGameProcess, int loaderMem) {
	s32 ret = pmAllocLoaderMemPool(hGameProcess, loaderMem);
	if (ret != 0) {
		return ret;
	}

	ret = protectRemoteMemory(hGameProcess, plgLoader, plgLoaderEx->plgMemSizeTotal);
	if (ret != 0) {
		nsDbgPrint("protectRemoteMemory failed: %08x\n", ret);
		goto error_alloc;
	}
	ret = copyRemoteMemory(hGameProcess, plgLoader, CUR_PROCESS_HANDLE, plgLoader, plgLoaderEx->plgMemSizeTotal);
	if (ret != 0) {
		nsDbgPrint("Copy plugin loader ingo failed: %08x\n", ret);
		goto error_alloc;
	}
	return 0;

error_alloc:
	pmFreeLoaderMemPool(0);
	return ret;
}

static int pmInjectToGame(Handle hGameProcess) {
	if (getMenuProcess() == 0)
		return -1;

	s32 ret;
	ret = pmLoadFromMenu(plgLoader, sizeof(PLGLOADER_INFO));
	if (ret != 0) {
		nsDbgPrint("Loading plugin info failed:%08x\n", ret);
		return ret;
	}

	ret = pmLoadFromMenu(plgLoaderEx, sizeof(PLGLOADER_EX_INFO));
	if (ret != 0) {
		nsDbgPrint("Loading plugin extended info failed:%08x\n", ret);
		return ret;
	}

	u32 tid[2];
	getProcessTIDByHandle(hGameProcess, tid);
	if (!(tid[0] == plgLoader->tid[0] && tid[1] == plgLoader->tid[1])) {
		return -1;
	}

	pmFreeLoaderMemPool(0);

	u32 pid = 0;
	ret = svcGetProcessId(&pid, hGameProcess);
	if (ret != 0) {
		nsDbgPrint("Get game process ID failed:%08x\n", ret);
		return ret;
	}
	plgLoader->gamePluginPid = pid;
	ret = pmSaveToMenu(&plgLoader->gamePluginPid, sizeof(plgLoader->gamePluginPid));
	if (ret != 0) {
		nsDbgPrint("Save game process ID failed:%08x\n", ret);
		return ret;
	}
	rpSetGamePid(pid);

	ret = pmLoadPluginsForGame();
	if (ret != 0) {
		nsDbgPrint("Loading plugins for game %08x%08x failed\n", tid[1], tid[0]);
	}

	NS_CONFIG cfg = { 0 };
	cfg.ntrConfig = *ntrConfig;
	cfg.ntrConfig.ex.nsUseDbg = nsDbgNext();

	int needInject =
		cfg.ntrConfig.ex.nsUseDbg ||
		plgLoaderEx->plgMemSizeTotal ||
		(plgLoaderEx->remotePlayBoost && plgLoaderEx->noCTRPFCompat);
	int loaderMem = !plgLoaderEx->noLoaderMem;

	if (needInject) {
		if (plgLoaderEx->plgMemSizeTotal) {
			ret = pmInitGamePlg(hGameProcess, loaderMem);
			pmUnloadPluginsForGame();
			if (ret != 0) {
				return ret;
			}
		}

		ret = nsAttachProcess(hGameProcess, PROC_START_ADDR, &cfg);
		if (ret != 0) {
			nsDbgPrint("Attach game process failed: %08x\n", ret);
			goto error_alloc;
		}
	}
	return 0;

error_alloc:
	pmFreeLoaderMemPool(0);
	return ret;
}

static RT_HOOK svcRunHook;
typedef u32 (*svcRunTypeDef)(u32 hProcess, u32 *startInfo);

static u32 svcRunCallback(Handle hProcess, u32 *startInfo) {
	pmInjectToGame(hProcess);
	return ((svcRunTypeDef)svcRunHook.callCode)(hProcess, startInfo);
}

int main(void) {
	startupInit();

	if (plgLoaderInfoAlloc() != 0)
		return 0;

	if (ntrConfig->ex.nsUseDbg)
		nsStartup();

	s32 res = fsInit();
	if (res != 0) {
		nsDbgPrint("fs init failed: %08x\n", res);
		return 0;
	}

	res = loadPayloadBin(NTR_BIN_GAME);
	if (res != 0) {
		nsDbgPrint("Load game payload failed: %08x\n", res);
		fsExit();
		return 0;
	}

	rtInitHook(&svcRunHook, ntrConfig->PMSvcRunAddr, (u32)svcRunCallback);
	rtEnableHook(&svcRunHook);

	return 0;
}

u32 payloadBinAlloc(u32 size) {
	return plgRequestMemory(size);
}

int payloadBinFree(u32, u32) {
	return -1;
}
