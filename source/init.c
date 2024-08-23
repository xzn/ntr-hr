#include "global.h"

#include "3ds/services/fs.h"
#include "3ds/ipc.h"

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
int __attribute__((weak)) setUpReturn(void) {
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

/* Use a buffer to jump around a race condition when unhooking */
static u32 farAddr[4];
int setUpReturn2(void) {
	s32 ret = rtCheckMemory((u32)farAddr, 16, MEMPERM_READWRITE | MEMPERM_EXECUTE);
	if (ret != 0)
		return ret;
	memcpy(farAddr, nsConfig->startupInfo, 8);
	rtGenerateJumpCode(*oldPC + 8, farAddr + 2);
	ret = rtFlushInstructionCache(farAddr, 16);
	if (ret != 0)
		return ret;

	rtGenerateJumpCode((u32)farAddr, (u32 *)*oldPC);
	ret = rtFlushInstructionCache((void *)*oldPC, 8);
	if (ret != 0)
		return ret;

	rtGenerateJumpCode((u32)farAddr, (void *)_ReturnToUser);
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

static u32 plgMemoryPoolBegin;
static u32 plgMemoryPoolEnd;

u32 plgRequestMemoryFromPool(u32 size, int pool) {
	if (pool == 0) {
		if (!plgMemoryPoolEnd) {
			plgMemoryPoolEnd = PLG_MEM_ADDR;
		}

		u32 ret, outAddr, addr;
		addr = plgMemoryPoolEnd;
		size = rtAlignToPageSize(size);
		ret = svcControlMemory(&outAddr, addr, addr, size, MEMOP_ALLOC, MEMPERM_READWRITE);
		if (ret != 0) {
			nsDbgPrint("Failed to allocate memory from pool for plugin at addr %08"PRIx32" for size %08"PRIx32": %08"PRIx32"\n", addr, size, ret);
			return 0;
		}

		plgMemoryPoolEnd += size;

		return addr;
	} else {
		if (!plgMemoryPoolBegin) {
			plgMemoryPoolBegin = PLG_MEM_ADDR;
		}

		u32 ret, outAddr, addr;
		size = rtAlignToPageSize(size);
		addr = plgMemoryPoolBegin - size;
		ret = svcControlMemory(&outAddr, addr, addr, size, MEMOP_ALLOC, MEMPERM_READWRITE);
		if (ret != 0) {
			nsDbgPrint("Failed to allocate memory from pool for payload at addr %08"PRIx32" for size %08"PRIx32": %08"PRIx32"\n", addr, size, ret);
			return 0;
		}

		plgMemoryPoolBegin = addr;

		return addr;
	}
}

u32 plgRequestMemory(u32 size) {
	return plgRequestMemoryFromPool(size, 0);
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

void rpSetGamePid(u32 gamePid) {
	Handle hClient = rpGetPortHandle();
	if (!hClient)
		return;

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(SVC_NWM_CMD_GAME_PID_UPDATE, 1, 0);
	cmdbuf[1] = gamePid;

	s32 ret = svcSendSyncRequest(hClient);
	if (ret != 0) {
		nsDbgPrint("Send port request failed: %08"PRIx32"\n", ret);
	}
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

void __attribute__((weak)) mainPre(void) {}
void __attribute__((weak)) mainPost(void) {}

static int mainEntered;
int __attribute__((weak)) main(void) {
	startupInit();

	if (!ATSR(&mainEntered)) {
		mainPre();

		Handle hThread;
		u32 *threadStack = (void *)(NS_CONFIG_ADDR + NS_CONFIG_MAX_SIZE);
		svcCreateThread(&hThread, mainThread, 0, &threadStack[(STACK_SIZE / 4) - 10], 0x10, 1);

		mainPost();
	}

	return 0;
}

static Handle rpSessionClient;
Handle rpGetPortHandle(void) {
	Handle hClient = rpSessionClient;
	s32 ret;
	if (hClient == 0) {
		ret = svcConnectToPort(&hClient, SVC_PORT_NWM);
		if (ret != 0) {
			return 0;
		}
		rpSessionClient = hClient;
	}
	return hClient;
}

void __attribute__((weak)) handlePortThreadPre(void) {}

#define clientCountMax 8
// Plus server port
#define sessionCountMax (clientCountMax + 1)
void handlePortThread(void *arg) {
	s32 ret;
	Handle hServer = 0, hClient = 0;
	const char *portName = (const char *)arg;
	ret = svcCreatePort(&hServer, &hClient, portName, clientCountMax);
	if (ret != 0) {
		showDbg("Create port failed: %08"PRIx32"\n", ret);
		svcExitThread();
	}

	u32 *cmdbuf = getThreadCommandBuffer();
	handlePortThreadPre();

	Handle sessionHandles[sessionCountMax] = { hServer };
	int sessionCount = 1;
	int replyHandleIndex = 0;
	cmdbuf[0] = 0xFFFF0000;

	while (1) {
		s32 receivedHandleIndex = -1;
		ret = svcReplyAndReceive(
			&receivedHandleIndex, sessionHandles, sessionCount,
			replyHandleIndex ? sessionHandles[replyHandleIndex] : 0);
		if (ret != 0) {
			if (ret == (s32)RES_HANDLE_CLOSED) {
				int closedHandleIndex = receivedHandleIndex;
				if (closedHandleIndex == 0) {
					for (int i = 0; i < sessionCount; ++i) {
						svcCloseHandle(sessionHandles[i]);
						showDbg("Port server handle unexpectedly closed for (%s)\n", portName);
						goto final;
					}
				}
				if (closedHandleIndex < 0) {
					closedHandleIndex = replyHandleIndex;
				}
				svcCloseHandle(sessionHandles[closedHandleIndex]);
				--sessionCount;
				sessionHandles[closedHandleIndex] = sessionHandles[sessionCount];
				sessionHandles[sessionCount] = 0;
			}

			replyHandleIndex = 0;
			cmdbuf[0] = 0xFFFF0000;
			continue;
		}

		if (receivedHandleIndex == 0) {
			replyHandleIndex = 0;
			cmdbuf[0] = 0xFFFF0000;

			Handle hSession;
			ret = svcAcceptSession(&hSession, hServer);
			if (ret != 0) {
				continue;
			}

			if (sessionCount >= sessionCountMax) {
				showDbg("Client count exceeded for (%s)\n", portName);
				svcCloseHandle(hSession);
				continue;
			}
			sessionHandles[sessionCount] = hSession;
			++sessionCount;
			continue;
		}

		u32 cmd_id = cmdbuf[0] >> 16;
		u32 norm_param_count = (cmdbuf[0] >> 6) & 0x3F;
		u32 trans_param_size = cmdbuf[0] & 0x3F;

		handlePortCmd(cmd_id, norm_param_count, trans_param_size, cmdbuf + 1);

		replyHandleIndex = receivedHandleIndex;
		cmdbuf[0] = IPC_MakeHeader(cmd_id, 0, 0);
	}
final:

	if (hServer)
		svcCloseHandle(hServer);
	if (hClient)
		svcCloseHandle(hClient);

	svcExitThread();
}
