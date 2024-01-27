#include "global.h"

#include "3ds/services/fs.h"

#include <memory.h>

NTR_CONFIG *ntrConfig;
showDbgFunc_t showDbgFunc;
PLGLOADER_INFO *plgLoaderInfo;

static int initNSConfig(void) {
	u32 ret;

	nsConfig = (void *)NS_CONFIG_ADDR;
	ret = protectMemory((void*)NS_CONFIG_ADDR, NS_CONFIG_MAX_SIZE);

	if (ret != 0) {
		showDbgRaw("Init nsConfig failed for process %x", getCurrentProcessId(), 0);
		return ret;
	}
	return 0;
}

static void loadParams(void) {
	ntrConfig = &nsConfig->ntrConfig;

	KProcessHandleDataOffset = ntrConfig->KProcessHandleDataOffset;
	KProcessPIDOffset = ntrConfig->KProcessPIDOffset;
	KProcessCodesetOffset = ntrConfig->KProcessCodesetOffset;
}

void _ReturnToUser(void);
static int setUpReturn(void) {
	u32 oldPC = nsConfig->startupInfo[2];

	u32 ret;
	memcpy((void *)oldPC, nsConfig->startupInfo, 8);
	ret = rtFlushInstructionCache((void *)oldPC, 8);
	if (ret != 0)
		return ret;
	rtGenerateJumpCode(oldPC, (void *)_ReturnToUser);
	ret = rtFlushInstructionCache((void *)_ReturnToUser, 8);
	return ret;
}

static int plgLoaderInfoAlloc(void) {
	plgLoaderInfo = (void *)plgPoolAlloc(sizeof(PLGLOADER_INFO));
	if (plgLoaderInfo != (void *)PLG_LOADER_ADDR) {
		showMsg("Plugin loader info at wrong address.");
		plgPoolFree((u32)plgLoaderInfo, sizeof(PLGLOADER_INFO));
		plgLoaderInfo = 0;
		return -1;
	}
	return 0;
}

int startupInit(int initPlgLoaderInfo) {
	if (initNSConfig() != 0)
		goto fail;
	loadParams();
	if (setUpReturn() != 0)
		goto fail;

	if (initPlgLoaderInfo && plgLoaderInfoAlloc() != 0)
		return -1;

	return 0;

fail:
	while (1) {
		disp(100, 0x10000ff);
		svcSleepThread(1000000000);
	}
}

static u32 plgPoolEnd;

u32 plgPoolAlloc(u32 size) {
	if (plgPoolEnd == 0) {
		plgPoolEnd = LOCAL_POOL_ADDR;
	}

	u32 addr = plgPoolEnd;
	u32 outAddr;
	u32 alignedSize = rtAlignToPageSize(size);
	s32 ret = svcControlMemory(&outAddr, addr, 0, alignedSize, MEMOP_ALLOC, MEMPERM_READWRITE);
	if (ret != 0) {
		nsDbgPrint("Failed to allocate memory from pool: %08x\n", ret);
		return 0;
	}

	plgPoolEnd += alignedSize;

	return addr;
}

int plgPoolFree(u32 addr, u32 size) {
	if (!addr || !size)
		return -1;

	u32 alignedSize = rtAlignToPageSize(size);
	u32 addrEnd = addr + alignedSize;
	if (addrEnd != plgPoolEnd) {
		showDbg("addr end %08x different from pool end %08x\n", addrEnd, plgPoolEnd);
		return -1;
	}
	s32 ret = svcControlMemory(NULL, addr, 0, alignedSize, MEMOP_FREE, 0);
	if (ret != 0) {
		nsDbgPrint("Failed to free memory to pool: %08x\n", ret);
		return ret;
	}

	plgPoolEnd = addr;
	return ret;
}

static u32 plgMemoryPoolEnd;

u32 plgRequestMemory(u32 size) {
	if (!plgMemoryPoolEnd) {
		plgMemoryPoolEnd = PLG_POOL_ADDR;
	}

	u32 ret, outAddr, addr;
	addr = plgMemoryPoolEnd;
	size = rtAlignToPageSize(size);
	ret = svcControlMemory(&outAddr, addr, 0, size, MEMOP_ALLOC, MEMPERM_READWRITE);
	if (ret != 0) {
		nsDbgPrint("Failed to allocate memory from pool for plugin: %08x\n", ret);
		return 0;
	}

	plgMemoryPoolEnd += size;

	return addr;
}

u32 arm11BinStart;
u32 arm11BinSize;

int loadPayloadBin(char *name, int pluginMem) {
	int fileLoaded = 0;
	Result ret;

	char fileName[PATH_MAX];
	ret = strnjoin(fileName, PATH_MAX, ntrConfig->ntrFilePath, name);
	if (ret != 0) {
		showDbg("Payload file name too long.", 0, 0);
		goto final;
	}

	Handle file;
	ret = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL), fsMakePath(PATH_ASCII, fileName), FS_OPEN_READ, 0);
	if (ret != 0) {
		showDbg("Failed to open payload file: %08x.", ret, 0);
		goto final;
	}

	u64 fileSize;
	ret = FSFILE_GetSize(file, &fileSize);
	if (ret != 0) {
		showDbg("Failed to get payload file size: %08x.", ret, 0);
		goto file_final;
	}

	u32 addr;
	if (pluginMem)
		addr = plgRequestMemory(fileSize);
	else
		addr = plgPoolAlloc(fileSize);
	if (addr == 0) {
		showMsg("Failed to get allocate memory for payload.");
		goto file_final;
	}

	u32 bytesRead;
	ret = FSFILE_Read(file, &bytesRead, 0, (void *)addr, fileSize);
	if (ret != 0) {
		showDbg("Failed to read payload: %08x.", ret, 0);
		plgPoolFree(addr, fileSize);
		goto file_final;
	}

	fileLoaded = 1;

file_final:
	FSFILE_Close(file);
	if (!fileLoaded)
		goto final;

	arm11BinStart = addr;
	arm11BinSize = fileSize;

final:
	return fileLoaded ? 0 : -1;
}

void unloadPayloadBin(void) {
	if (arm11BinStart) {
		plgPoolFree(arm11BinStart, arm11BinSize);
		arm11BinStart = 0;
		arm11BinSize = 0;
	}
}

void rpSetGamePid(u32) {
}
