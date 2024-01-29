#include "global.h"

#include "3ds/services/fs.h"

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

static u32 plgListPluginsInDir(const char *path, u16 buf[LOCAL_DIR_LIST_BUF_COUNT], u16 *entries[MAX_PLUGIN_COUNT])  {
	Handle hDir;
	s32 res;
	res = FSUSER_OpenDirectory(&hDir, sdmcArchive, fsMakePath(PATH_ASCII, path));
	if (res != 0) {
		return 0;
	}
	u32 entriesCount = 0;
	u32 bufOffset = 0;
	while (entriesCount < MAX_PLUGIN_COUNT) {
		u32 nRead;
		FS_DirectoryEntry dirEntry;
		res = FSDIR_Read(hDir, &nRead, 1, &dirEntry);
		if (res != 0 || nRead == 0)
			break;
		size_t count = wstrlen(dirEntry.name);
		if (bufOffset + count >= LOCAL_DIR_LIST_BUF_COUNT)
			break;
		entries[entriesCount] = &buf[bufOffset];
		memcpy(buf + bufOffset, dirEntry.name, count);
		bufOffset += count;
		buf[bufOffset] = 0;
		++bufOffset;

		++entriesCount;
	}
	FSDIR_Close(hDir);
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

static u32 plgAddPluginsFromDirectory(const char *dir) {
	char path[PATH_MAX];
	u16 plgPath[PATH_MAX];
	if (xsnprintf(path, PATH_MAX, "/plugin/%s", dir) != 0)
		return 0;

	u16 buf[LOCAL_DIR_LIST_BUF_COUNT];
	u16 *entries[MAX_PLUGIN_COUNT];
	u32 cnt = plgListPluginsInDir(path, buf, entries);

	u32 outCnt = 0;
	for (u32 i = 0; i < cnt; ++i) {
		if (plgGetFileNamePluginType(entries[i]) != 0) {
			continue;
		}

		if (pathnjoin(plgPath, path, entries[i]) != 0)
			continue;

		Handle file = rtOpenFile16(plgPath);
		if (file == 0) {
			continue;
		}

		u32 fileSize = rtGetFileSize(file);
		if (fileSize == 0) {
			goto fail_file;
		}

		u32 addr = plgPoolAlloc(fileSize);
		if (addr == 0) {
			goto fail_file;
		}

		u32 bytesRead = rtLoadFileToBuffer(file, (void *)addr, fileSize);
		if (bytesRead != fileSize) {
			plgPoolFree(addr, fileSize);
			goto fail_file;
		}
		rtCloseFile(file);

		plgLoader->plgBufferPtr[plgLoader->plgCount] = addr;
		plgLoader->plgSize[plgLoader->plgCount] = fileSize;
		++plgLoader->plgCount;

		++outCnt;
		continue;

fail_file:
		rtCloseFile(file);
	}

	return outCnt;
}

static u32 plgLoaderPluginsBegin(void) {
	return (u32)((u8 *)plgLoader + rtAlignToPageSize(sizeof(PLGLOADER_INFO)));
}

static u32 plgLoaderPluginsEnd(void) {
	return (u32)((u8 *)plgLoader + rtAlignToPageSize(plgLoaderEx->plgMemSizeTotal));
}

static int pmLoadPluginsForGame(void) {
	plgLoaderEx->plgMemSizeTotal = 0;
	if (plgLoaderEx->noPlugins)
		return 0;

	if (plgPoolAlloc(0) != plgLoaderPluginsBegin()) {
		showDbg("Plugin loader memory pool at wrong address.");
		return -1;
	}

	plgAddPluginsFromDirectory("game");
	char buf[32];
	xsnprintf(buf, sizeof(buf), "%08"PRIx32"%08"PRIx32, plgLoader->tid[1], plgLoader->tid[0]);
	plgAddPluginsFromDirectory(buf);
	return 0;
}

static int pmUnloadPluginsForGame(void) {
	u32 addr = plgLoaderPluginsBegin();
	u32 addrEnd = plgLoaderPluginsEnd();
	if (addrEnd != plgPoolAlloc(0)) {
		showDbg("Plugin loader memory pool unexpected size.");
		return -1;
	}
	plgPoolFree(addr, addrEnd - addr);

	plgLoaderEx->plgMemSizeTotal = 0;
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
		ret = mapRemoteMemoryInLoader(hGameProcess, (u32)plgLoader, plgLoaderEx->plgMemSizeTotal, MEMOP_ALLOC);
	else
		ret = mapRemoteMemory(hGameProcess, (u32)plgLoader, plgLoaderEx->plgMemSizeTotal, MEMOP_ALLOC);
	if (ret != 0) {
		nsDbgPrint("Alloc plugin memory failed: %08"PRIx32"\n", ret);
		return ret;
	}

	if (loaderMem) {
		loaderMemPoolSize = plgLoaderEx->plgMemSizeTotal;
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

	ret = protectRemoteMemory(hGameProcess, plgLoader, plgLoaderEx->plgMemSizeTotal);
	if (ret != 0) {
		nsDbgPrint("protectRemoteMemory failed: %08"PRIx32"\n", ret);
		goto error_alloc;
	}
	ret = copyRemoteMemory(hGameProcess, plgLoader, CUR_PROCESS_HANDLE, plgLoader, plgLoaderEx->plgMemSizeTotal);
	if (ret != 0) {
		nsDbgPrint("Copy plugin loader ingo failed: %08"PRIx32"\n", ret);
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

	ret = pmLoadFromMenu(plgLoaderEx, sizeof(PLGLOADER_EX_INFO));
	if (ret != 0) {
		nsDbgPrint("Loading plugin extended info failed:%08"PRIx32"\n", ret);
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
	plgLoader->gamePluginPid = pid;
	ret = pmSaveToMenu(&plgLoader->gamePluginPid, sizeof(plgLoader->gamePluginPid));
	if (ret != 0) {
		nsDbgPrint("Save game process ID failed:%08"PRIx32"\n", ret);
		return ret;
	}
	rpSetGamePid(pid);

	ret = pmLoadPluginsForGame();
	if (ret != 0) {
		nsDbgPrint("Loading plugins for game %08"PRIx32"%08"PRIx32" failed\n", tid[1], tid[0]);
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

int main(void) {
	startupInit();

	if (plgLoaderInfoAlloc() != 0)
		return 0;

	if (ntrConfig->ex.nsUseDbg) {
		s32 ret = nsStartup();
		if (ret != 0) {
			disp(100, 0x1ff00ff);
		} else {
			disp(100, 0x100ff00);
		}
	}

	s32 res = fsInit();
	if (res != 0) {
		nsDbgPrint("fs init failed: %08"PRIx32"\n", res);
		return 0;
	}

	res = FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL));
	if (res != 0) {
		nsDbgPrint("Open sdmc failed: %08"PRIx32"\n", res);
		goto fs_fail;
	}

	res = loadPayloadBin(NTR_BIN_GAME);
	if (res != 0) {
		nsDbgPrint("Load game payload failed: %08"PRIx32"\n", res);
		goto fs_fail;
	}

	rtInitHook(&svcRunHook, ntrConfig->PMSvcRunAddr, (u32)svcRunCallback);
	rtEnableHook(&svcRunHook);

	return 0;

fs_fail:
	fsExit();

	disp(100, 0x10000ff);
	return 0;
}

u32 payloadBinAlloc(u32 size) {
	return plgRequestMemory(size);
}

int payloadBinFree(u32, u32) {
	return -1;
}
