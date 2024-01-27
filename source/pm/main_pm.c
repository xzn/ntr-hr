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

static int pmLoadPluginsForGame(PLGLOADER_INFO *, u32[2], PLGLOADER_EX_INFO *) {
	// TODO
	return 0;
}

static Handle loaderMemGameHandle;
static u32 loaderMemPoolSize;

static void pmFreeLoaderMemPool(int closeHandle) {
	if (loaderMemGameHandle) {
		u32 ret;
		ret = mapRemoteMemoryInLoader(loaderMemGameHandle, PLG_LOADER_ADDR, loaderMemPoolSize, MEMOP_FREE);
		if (ret != 0) {
			nsDbgPrint("Free loader mem failed: %08x\n", ret);
		}
		if (closeHandle)
			svcCloseHandle(loaderMemGameHandle);
		loaderMemGameHandle = 0;
		loaderMemPoolSize = 0;
	}
}

static int pmInjectToGame(Handle hGameProcess) {
	Handle hProcess = getMenuProcess();
	if (hProcess == 0)
		return -1;

	PLGLOADER_INFO *plgLoader = (void *)PLG_LOADER_ADDR;
	s32 ret;
	ret = copyRemoteMemory(CUR_PROCESS_HANDLE, plgLoader, hProcess, plgLoader, sizeof(PLGLOADER_INFO));
	if (ret != 0) {
		nsDbgPrint("Loading plugin info failed:%08x\n", ret);
		return ret;
	}

	PLGLOADER_EX_INFO *plgLoaderEx = &ntrConfig->ex.plg;
	ret = copyRemoteMemory(CUR_PROCESS_HANDLE, plgLoaderEx, hProcess, plgLoaderEx, sizeof(PLGLOADER_EX_INFO));
	if (ret != 0) {
		nsDbgPrint("Loading plugin extended info failed:%08x\n", ret);
		return ret;
	}

	u32 tid[2];
	getProcessTIDByHandle(hProcess, tid);
	if (!(tid[0] == plgLoader->tid[0] && tid[1] == plgLoader->tid[1])) {
		return -1;
	}

	pmFreeLoaderMemPool(1);

	u32 pid = 0;
	ret = svcGetProcessId(&pid, hProcess);
	if (ret != 0) {
		nsDbgPrint("Get game process ID failed:%08x\n", ret);
		return ret;
	}
	plgLoader->gamePluginPid = pid;
	ret = copyRemoteMemory(
		hProcess, &plgLoader->gamePluginPid,
		CUR_PROCESS_HANDLE, &plgLoader->gamePluginPid,
		sizeof(plgLoader->gamePluginPid));
	if (ret != 0) {
		nsDbgPrint("Save game process ID failed:%08x\n", ret);
		return ret;
	}
	rpSetGamePid(pid);

	ret = pmLoadPluginsForGame(plgLoader, tid, plgLoaderEx);
	if (ret != 0) {
		nsDbgPrint("Loading plugins for game %08x%08x failed\n", tid[1], tid[0]);
	}

	NS_CONFIG cfg = { 0 };
	cfg.ntrConfig = *ntrConfig;
	cfg.ntrConfig.ex.nsUseDbg = nsDbgNext();

	int needInject =
		cfg.ntrConfig.ex.nsUseDbg ||
		!plgLoaderEx->noPlugins ||
		(plgLoaderEx->remotePlayBoost && plgLoaderEx->noCTRPFCompat);
	int loaderMem = !plgLoaderEx->noLoaderMem;

	if (needInject) {
		if (plgLoaderEx->plgMemSizeTotal) {
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
					pmFreeLoaderMemPool(0);
					return ret;
				}
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
		}

		if (arm11BinStart == 0) {
			ret = loadPayloadBin(NTR_BIN_GAME, 1);
			if (ret != 0) {
				nsDbgPrint("Load game payload failed: %08x\n", ret);
				goto error_alloc;
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
	pmFreeLoaderMemPool(1);
	return 0;
}

static RT_HOOK svcRunHook;
typedef u32 (*svcRunTypeDef)(u32 hProcess, u32 *startInfo);

static u32 svcRunCallback(Handle hProcess, u32 *startInfo) {
	pmInjectToGame(hProcess);
	return ((svcRunTypeDef)svcRunHook.callCode)(hProcess, startInfo);
}

int main(void) {
	if (startupInit(1) != 0)
		return 0;

	if (ntrConfig->ex.nsUseDbg)
		nsStartup();

	Handle fsUserHandle = *(u32 *)ntrConfig->HomeFSUHandleAddr;
	fsUseSession(fsUserHandle);

	rtInitHook(&svcRunHook, ntrConfig->PMSvcRunAddr, (u32)svcRunCallback);
	rtEnableHook(&svcRunHook);

	return 0;
}
