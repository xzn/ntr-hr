#include "global.h"

#include "3ds/services/fs.h"
#include "3ds/srv.h"

#include <memory.h>
#include <ctype.h>

static FS_Archive sdmcArchive;

static Handle hMenuProcess = 0;
static Handle getMenuProcess(void) {
	if (hMenuProcess == 0) {
		s32 ret = svcOpenProcess(&hMenuProcess, ntrConfig->HomeMenuPid);
		if (ret != 0) {
			hMenuProcess = 0;
			nsDbgPrint("Open menu process failed: %08"PRIx32"\n", ret);
		}
	}
	return hMenuProcess;
}

static size_t wstrlen(const u16 *str) {
	size_t len = 0;
	while (*str) {
		++len;
		++str;
	}
	return len;
}

/* return len of str */
static size_t wstrncpy(u16 *str, size_t len, const char *path) {
	if (!len)
		return 0;

	size_t ret = 0;
	while (1) {
		*str = *path;
		--len;
		if (!len) {
			*str = 0;
			break;
		}
		if (!*str)
			break;
		++ret;
		++str;
		++path;
	}
	return ret;
}

static int wstricmp(const u16 *str, const char *suffix) {
	while (*str || *suffix) {
		if (toupper((u8)*str) != toupper((u8)*suffix)) {
			if (*str < (u8)*suffix)
				return -1;
			if (*str > (u8)*suffix)
				return 1;
		}
		++str;
		++suffix;
	}
	return 0;
}

static int plgGetFileNamePluginType(const u16 *name) {
	size_t len = wstrlen(name);

	char suffix[] = ".plg";
	size_t suffix_len = sizeof(suffix) - 1;

	if (len <= suffix_len)
		return -1;
	if (name[0] == '.')
		return -1;
	if (wstricmp(name + len - suffix_len, suffix) != 0)
		return -1;
	return 0;
}

static int pathnjoin(u16 plgPath[PATH_MAX], const char *path, const u16 *entry) {
	size_t count = wstrncpy(plgPath, PATH_MAX, path);
	size_t len = wstrlen(entry);
	if (count + 1 + len >= PATH_MAX) {
		return -1;
	}
	plgPath += count;
	*plgPath = '/';
	++plgPath;

	memcpy(plgPath, entry, len * sizeof(*entry));
	plgPath += len;
	*plgPath = 0;
	return 0;
}

static void plgLoadPluginFromFile(const char *path, const u16 *name) {
	u16 plgPath[PATH_MAX];

	if (plgGetFileNamePluginType(name) != 0) {
		return;
	}

	if (pathnjoin(plgPath, path, name) != 0)
		return;

	Handle file = rtOpenFile16(plgPath);
	if (file == 0) {
		return;
	}

	u32 fileSize = rtGetFileSize(file);
	if (fileSize == 0) {
		goto fail_file;
	}

	s32 ret = plgEnsurePoolSize(plgLoaderEx->memSizeTotal + fileSize);
	if (ret != 0) {
		goto fail_file;
	}
	u32 addr = (u32)plgLoader + plgLoaderEx->memSizeTotal;

	u32 bytesRead = rtLoadFileToBuffer(file, (void *)addr, fileSize);
	if (bytesRead != fileSize) {
		goto fail_file;
	}
	rtCloseFile(file);

	u32 alignedSize = rtAlignToPageSize(fileSize);
	plgLoader->plgBufferPtr[plgLoader->plgCount] = addr;
	plgLoader->plgSize[plgLoader->plgCount] = alignedSize;
	plgLoaderEx->memSizeTotal += alignedSize;
	++plgLoader->plgCount;

	return;

fail_file:
	rtCloseFile(file);
}

static void plgAddPluginsFromDirectory(const char *dir) {
	char path[PATH_MAX];
	if (strnjoin(path, PATH_MAX, "/plugin/", dir) != 0)
		return;

	Handle hDir;
	s32 res;
	res = FSUSER_OpenDirectory(&hDir, sdmcArchive, fsMakePath(PATH_ASCII, path));
	if (res != 0) {
		return;
	}
	while (plgLoader->plgCount < MAX_PLUGIN_COUNT) {
		u32 nRead;
		FS_DirectoryEntry dirEntry;
		res = FSDIR_Read(hDir, &nRead, 1, &dirEntry);
		if (res != 0 || nRead == 0)
			break;

		plgLoadPluginFromFile(path, dirEntry.name);
	}
	FSDIR_Close(hDir);
}

static int pmLoadPluginsForGame(void) {
	plgLoaderEx->memSizeTotal = rtAlignToPageSize(sizeof(PLGLOADER_INFO));
	if (plgLoaderEx->noPlugins)
		return 0;

	plgAddPluginsFromDirectory("game");
	char buf[32];
	xsnprintf(buf, sizeof(buf), "%08"PRIx32"%08"PRIx32, plgLoader->tid[1], plgLoader->tid[0]);
	plgAddPluginsFromDirectory(buf);

	if (!plgLoader->plgCount)
		plgLoaderEx->memSizeTotal = 0;
	return 0;
}

static int pmUnloadPluginsForGame(void) {
	plgLoaderEx->memSizeTotal = 0;
	return 0;
}

static Handle loaderMemGameHandle;
static u32 loaderMemPoolSize;

static int pmFreeLoaderMemPool(int keepHandle) {
	u32 ret = 0;
	if (loaderMemGameHandle) {
		ret = mapRemoteMemoryInLoader(loaderMemGameHandle, (u32)plgLoader, loaderMemPoolSize, MEMOP_FREE);
		if (ret != 0) {
			nsDbgPrint("Free loader mem failed: %08"PRIx32"\n", ret);
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
		ret = mapRemoteMemoryInLoader(hGameProcess, (u32)plgLoader, plgLoaderEx->memSizeTotal, MEMOP_ALLOC);
	else
		ret = mapRemoteMemory(hGameProcess, (u32)plgLoader, plgLoaderEx->memSizeTotal, MEMOP_ALLOC);
	if (ret != 0) {
		nsDbgPrint("Alloc plugin memory failed: %08"PRIx32"\n", ret);
		return ret;
	}

	if (loaderMem) {
		loaderMemPoolSize = plgLoaderEx->memSizeTotal;
		ret = svcDuplicateHandle(&loaderMemGameHandle, hGameProcess);
		if (ret != 0) {
			nsDbgPrint("Dupping process handle failed: %08"PRIx32"\n", ret);
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

	ret = protectRemoteMemory(hGameProcess, plgLoader, plgLoaderEx->memSizeTotal, MEMPERM_READWRITE | MEMPERM_EXECUTE);
	if (ret != 0) {
		nsDbgPrint("protectRemoteMemory failed: %08"PRIx32"\n", ret);
		goto error_alloc;
	}

	ret = copyRemoteMemory(hGameProcess, plgLoader, CUR_PROCESS_HANDLE, plgLoader, plgLoaderEx->memSizeTotal);
	if (ret != 0) {
		nsDbgPrint("Copy plugin loader to game failed: %08"PRIx32"\n", ret);
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
		nsDbgPrint("Loading plugin info failed:%08"PRIx32"\n", ret);
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
		nsDbgPrint("Get game process ID failed:%08"PRIx32"\n", ret);
		return ret;
	}

	nsDbgPrint("Game pid :%"PRIx32"\n", pid);
	rpSetGamePid(pid);

	plgLoader->gamePluginPid = pid;
	ret = pmSaveToMenu(&plgLoader->gamePluginPid, sizeof(plgLoader->gamePluginPid));
	if (ret != 0) {
		nsDbgPrint("Save game process ID failed:%08"PRIx32"\n", ret);
		return ret;
	}

	ret = pmLoadFromMenu(plgLoaderEx, sizeof(PLGLOADER_EX_INFO));
	if (ret != 0) {
		nsDbgPrint("Loading plugin extended info failed:%08"PRIx32"\n", ret);
		return ret;
	}

	ret = pmLoadPluginsForGame();
	if (ret != 0) {
		nsDbgPrint("Loading plugins for game %08"PRIx32"%08"PRIx32" failed\n", tid[1], tid[0]);
	}

	NS_CONFIG cfg = { 0 };
	cfg.ntrConfig = *ntrConfig;
	cfg.ntrConfig.ex.nsUseDbg = nsDbgNext();

	int needInject =
		cfg.ntrConfig.ex.nsUseDbg ||
		plgLoaderEx->memSizeTotal ||
		(plgLoaderEx->remotePlayBoost && !plgLoaderEx->CTRPFCompat);
	int loaderMem = !plgLoaderEx->noLoaderMem;

	if (needInject) {
		if (plgLoaderEx->memSizeTotal) {
			ret = pmInitGamePlg(hGameProcess, loaderMem);
			pmUnloadPluginsForGame();
			if (ret != 0) {
				return ret;
			}
		}

		ret = nsAttachProcess(hGameProcess, PROC_START_ADDR, &cfg);
		if (ret != 0) {
			nsDbgPrint("Attach game process failed: %08"PRIx32"\n", ret);
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

static Handle pmReadyEvent;

void mainPre(void) {
	if (svcCreateEvent(&pmReadyEvent, RESET_ONESHOT) != 0) {
		pmReadyEvent = 0;
		disp(100, DBG_CL_MSG);
	}
	rtInitHook(&svcRunHook, ntrConfig->PMSvcRunAddr, (u32)svcRunCallback);
	rtEnableHook(&svcRunHook);
}

void mainPost(void) {
	if (pmReadyEvent) {
		if (svcWaitSynchronization(pmReadyEvent, PM_INIT_READY_TIMEOUT) != 0) {
			disp(100, DBG_CL_MSG);
		}
	}
}

void mainThread(void *) {
	s32 res = srvInit();
	if (res != 0) {
		showDbg("srvInit failed: %08"PRIx32, res);
		goto final;
	}

	if (plgLoaderInfoAlloc() != 0)
		goto final;

	if (ntrConfig->ex.nsUseDbg) {
		res = nsStartup();
		if (res != 0) {
			disp(100, DBG_CL_USE_DBG_FAIL);
		} else {
			disp(100, DBG_CL_USE_INJECT);
		}
	}

	res = fsInit();
	if (res != 0) {
		showDbg("fs init failed: %08"PRIx32, res);
		goto final;
	}

	res = FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL));
	if (res != 0) {
		showDbg("Open sdmc failed: %08"PRIx32, res);
		goto fs_fail;
	}

	res = loadPayloadBin(NTR_BIN_GAME);
	if (res != 0) {
		showDbg("Load game payload failed: %08"PRIx32, res);
		goto fs_fail;
	}

	goto final;

fs_fail:
	fsExit();

	disp(100, DBG_CL_FATAL);

final:
	if (pmReadyEvent) {
		res = svcSignalEvent(pmReadyEvent);
		if (res != 0) {
			showDbg("pm payload init sync error: %08"PRIx32"\n", res);
		}
	}

	svcExitThread();
}

u32 payloadBinAlloc(u32 size) {
	return plgRequestMemory(size);
}

int payloadBinFree(u32, u32) {
	return -1;
}
