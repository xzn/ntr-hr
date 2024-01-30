#include "global.h"

#include "3ds/services/fs.h"

#include <memory.h>

showDbgFunc_t showDbgFunc;

int showMsgDbgFunc(const char *msg) {
	if (showDbgFunc) {
		showDbgFunc(msg);
		svcSleepThread(1000000000);
		return 0;
	}
	return -1;
}

static int initNSConfig(void) {
	u32 ret;

	ret = rtCheckMemory((u32)nsConfig, NS_CONFIG_MAX_SIZE, MEMPERM_READWRITE);

	if (ret != 0) {
		showMsgRaw("Init nsConfig failed for process %"PRIx32, getCurrentProcessId());
		return ret;
	}
	return 0;
}

void loadParams(NTR_CONFIG *ntrCfg) {
	KProcessHandleDataOffset = ntrCfg->KProcessHandleDataOffset;
	KProcessPIDOffset = ntrCfg->KProcessPIDOffset;
	KProcessCodesetOffset = ntrCfg->KProcessCodesetOffset;
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

int plgLoaderInfoAlloc(void) {
	s32 ret = plgEnsurePoolSize(sizeof(PLGLOADER_INFO));
	if (ret != 0) {
		showDbg("plgLoader alloc failed.");
	}
	return ret;
}

void startupInit(void) {
	if (initNSConfig() != 0)
		goto fail;
	loadParams(ntrConfig);
	if (setUpReturn() != 0)
		goto fail;

	return;

fail:
	while (1) {
		disp(100, DBG_CL_FATAL);
		svcSleepThread(1000000000);
	}
}

static u32 plgPoolEnd;

int plgEnsurePoolSize(u32 size) {
	if (!plgPoolEnd) {
		plgPoolEnd = PLG_POOL_ADDR;
	}

	size = rtAlignToPageSize(size);
	u32 end = PLG_POOL_ADDR + size;
	if (end <= plgPoolEnd) {
		return 0;
	}

	u32 ret, outAddr, addr;
	addr = plgPoolEnd;
	size = end - plgPoolEnd;
	ret = svcControlMemory(&outAddr, addr, addr, size, MEMOP_ALLOC, MEMPERM_READWRITE);
	if (ret != 0) {
		nsDbgPrint("Failed to extend memory from pool at addr %08"PRIx32": %08"PRIx32"\n", addr, ret);
		return -1;
	}

	plgPoolEnd = end;

	return 0;
}

static u32 plgMemoryPoolEnd;

u32 plgRequestMemory(u32 size) {
	if (!plgMemoryPoolEnd) {
		plgMemoryPoolEnd = PLG_MEM_ADDR;
	}

	u32 ret, outAddr, addr;
	addr = plgMemoryPoolEnd;
	size = rtAlignToPageSize(size);
	ret = svcControlMemory(&outAddr, addr, addr, size, MEMOP_ALLOC, MEMPERM_READWRITE);
	if (ret != 0) {
		nsDbgPrint("Failed to allocate memory from pool for plugin at addr %08"PRIx32": %08"PRIx32"\n", addr, ret);
		return 0;
	}

	plgMemoryPoolEnd += size;

	return addr;
}

u32 arm11BinStart;
u32 arm11BinSize;

u32 __attribute__((weak)) payloadBinAlloc(u32 size) {
	u32 offset = rtAlignToPageSize(sizeof(PLGLOADER_INFO));
	if (plgEnsurePoolSize(offset + size) == 0)
		return PLG_POOL_ADDR + offset;
	return 0;
}

int __attribute__((weak)) payloadBinFree(u32, u32) {
	return -1;
}

int loadPayloadBin(char *name) {
	int fileLoaded = 0;
	Result ret;

	char fileName[PATH_MAX];
	ret = strnjoin(fileName, PATH_MAX, ntrConfig->ntrFilePath, name);
	if (ret != 0) {
		showDbg("Payload file name too long.");
		goto final;
	}

	Handle file = rtOpenFile(fileName);
	if (file == 0) {
		showDbg("Failed to open payload file.");
		goto final;
	}

	u32 fileSize = rtGetFileSize(file);
	if (fileSize == 0) {
		showDbg("Failed to get payload file size/payload empty.");
		goto file_final;
	}

	u32 addr;
		addr = payloadBinAlloc(fileSize);
	if (addr == 0) {
		showDbg("Failed to allocate memory for payload.");
		goto file_final;
	}

	u32 bytesRead = rtLoadFileToBuffer(file, (u32 *)addr, fileSize);
	if (bytesRead != fileSize) {
		showDbg("Failed to read payload.");
		payloadBinFree(addr, fileSize);
		goto file_final;
	}

	fileLoaded = 1;

file_final:
	rtCloseFile(file);
	if (!fileLoaded)
		goto final;

	arm11BinStart = addr;
	arm11BinSize = rtAlignToPageSize(fileSize);

final:
	return fileLoaded ? 0 : -1;
}

void unloadPayloadBin(void) {
	if (arm11BinStart) {
		payloadBinFree(arm11BinStart, arm11BinSize);
		arm11BinStart = 0;
		arm11BinSize = 0;
	}
}

void rpSetGamePid(u32) {
	// TODO
}

static u32 plgRegisterMenuEntryStub(u32, const char *, const void *) { return -1; }

static u32 plgGetSharedServiceHandle(const char* servName, u32* handle) {
	if (strcmp(servName, "fs:USER") == 0) {
		Handle fsuHandle = *fsGetSessionHandle();
		if (fsuHandle == 0) {
			s32 res = fsInit();
			if (res == 0) {
				fsuHandle = *fsGetSessionHandle();
			}
		}
		*handle = fsuHandle;
		return 0;
	}
	return 1;
}

static u32 controlVideoStub(u32, u32, u32, u32) {
	return 0;
}

static s32 showMenuExStub(const char *, u32, const char *[], const char *[],  u32) {
	return -1;
}

static void showDbgRawA2Stub(const char *, u32, u32) {}

static void nsDbgPrintRawStub(const char *, ...) {}

#define INIT_SHARED_FUNC(name, id) (nsConfig->sharedFunc[id] = (u32)name)
void initSharedFunc(void) {
	INIT_SHARED_FUNC(showDbgRawA2Stub, 0);
	INIT_SHARED_FUNC(nsDbgPrintRawStub, 1);
	INIT_SHARED_FUNC(plgRegisterMenuEntryStub, 2);
	INIT_SHARED_FUNC(plgGetSharedServiceHandle, 3);
	INIT_SHARED_FUNC(plgRequestMemory, 4);
	INIT_SHARED_FUNC(plgRegisterCallback, 5);
	INIT_SHARED_FUNC(xsprintf, 6);
	INIT_SHARED_FUNC(controlVideoStub, 7);
	INIT_SHARED_FUNC(plgGetIoBase, 8);
	INIT_SHARED_FUNC(copyRemoteMemory, 9);
	INIT_SHARED_FUNC(plgSetValue, 10);
	INIT_SHARED_FUNC(showMenuExStub, 11);
}

void __attribute__((weak)) mainInit(void) {}

int __attribute__((weak)) main(void) {
	startupInit();

	mainInit();

	Handle hThread;
	u32 *threadStack = (void *)(NS_CONFIG_ADDR + NS_CONFIG_MAX_SIZE);
	svcCreateThread(&hThread, mainThread, 0, &threadStack[(STACK_SIZE / 4) - 10], 0x10, 1);

	return 0;
}
