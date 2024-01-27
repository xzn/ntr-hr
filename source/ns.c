#include "global.h"

#include "3ds/services/soc.h"

#include <memory.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

NS_CONFIG* nsConfig;

typedef enum {
	NS_CMD_HEARTBEAT,
	NS_CMD_REMOTEPLAY = 901,
} NS_CMD;

typedef struct {
	u32 magic;
	u32 seq;
	u32 type;
	NS_CMD cmd;
	u32 args[16];

	u32 dataLen;
} NS_PACKET;

typedef struct {
	u8 debugBuf[DEBUG_BUF_SIZE + 20];
	NS_PACKET packetBuf;
	u32 hSocket;
	u32 remainDataLen;
} NS_CONTEXT;

static NS_CONTEXT *nsContext;

static void nsDbgLn() {
	if (nsConfig->debugPtr == 0)
		return;
	if (nsConfig->debugPtr >= nsConfig->debugBufSize) {
		if (nsConfig->debugBuf[nsConfig->debugBufSize - 1] != '\n') {
			nsConfig->debugBuf[nsConfig->debugBufSize - 1] = '\n';
		}
	} else {
		if (nsConfig->debugBuf[nsConfig->debugPtr - 1] != '\n') {
			nsConfig->debugBuf[nsConfig->debugPtr] = '\n';
			nsConfig->debugPtr++;
		}
	}
}

void nsDbgPrintRaw(const char*, ...) {
}

static int nsCheckPCSafeToWrite(u32 hProcess, u32 remotePC) {
	s32 ret, i;
	u32 tids[LOCAL_TID_BUF_COUNT];
	s32 tidCount;
	u32 ctx[400];

	ret = svcGetThreadList(&tidCount, tids, LOCAL_TID_BUF_COUNT, hProcess);
	if (ret != 0) {
		return -1;
	}

	for (i = 0; i < tidCount; ++i) {
		u32 tid = tids[i];
		memset(ctx, 0x33, sizeof(ctx));
		if (rtGetThreadReg(hProcess, tid, ctx) != 0)
			return -1;
		u32 pc = ctx[15];
		if (remotePC >= pc - 16 && remotePC < pc)
			return -1;
	}

	return 0;
}

u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg, u32 binStart, u32 binSize) {
	u32 size = 0;
	u32* buf = 0;
	u32 baseAddr = NS_CONFIG_ADDR;
	u32 stackSize = STACK_SIZE;
	u32 totalSize;
	u32 ret;
	u32 tmp[20];
	u32 arm11StartAddress;
	u32 offset = NS_CONFIG_MAX_SIZE + stackSize;
	u32 pcDone = 0;
	u32 pcTries;

	arm11StartAddress = baseAddr + offset;
	buf = (u32*)binStart;
	size = binSize;
	nsDbgPrint("buf: %08x, size: %08x\n", buf, size);

	if (!buf) {
		showDbg("arm11 not loaded", 0, 0);
		return -1;
	}

	totalSize = size + offset;

	ret = mapRemoteMemory(hProcess, baseAddr, totalSize);

	if (ret != 0) {
		showDbg("mapRemoteMemory failed: %08x", ret, 0);
	}
	// set rwx
	ret = protectRemoteMemory(hProcess, (void *)baseAddr, totalSize);
	if (ret != 0) {
		showDbg("protectRemoteMemory failed: %08x", ret, 0);
		goto final;
	}
	// load arm11.bin code at arm11StartAddress
	ret = copyRemoteMemory(hProcess, (void *)arm11StartAddress, 0xffff8001, buf, size);
	if (ret != 0) {
		showDbg("copyRemoteMemory payload failed: %08x", ret, 0);
		goto final;
	}

	ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, remotePC, 8);
	if (ret != 0) {
		showDbg("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x", ret, 0);
		goto final;
	}

	cfg->initMode = NS_INITMODE_FROMHOOK;

	// store original 8-byte code
	ret = copyRemoteMemory(0xffff8001, &(cfg->startupInfo[0]), hProcess, (void *)remotePC, 8);
	if (ret != 0) {
		showDbg("copyRemoteMemory original code to be hooked failed: %08x", ret, 0);
		goto final;
	}
	cfg->startupInfo[2] = remotePC;

	// copy cfg structure to remote process
	ret = copyRemoteMemory(hProcess, (void *)baseAddr, 0xffff8001, cfg, sizeof(NS_CONFIG));
	if (ret != 0) {
		showDbg("copyRemoteMemory ns_config failed: %08x", ret, 0);
		goto final;
	}

	// write hook instructions to remote process
	tmp[0] = 0xe51ff004;
	tmp[1] = arm11StartAddress;
	pcTries = 20;
	while (!pcDone && pcTries) {
		ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0);
		if (ret != 0) {
			showDbg("locking remote process failed: %08x", ret, 0);
			goto final;
		}

		ret = nsCheckPCSafeToWrite(hProcess, remotePC);
		if (ret != 0) {
			goto lock_failed;
		}

		ret = copyRemoteMemory(hProcess, (void *)remotePC, 0xffff8001, &tmp, 8);
		if (ret != 0) {
			showDbg("copyRemoteMemory hook instruction failed: %08x", ret, 0);
			goto lock_failed;
		}

		pcDone = 1;
		goto lock_final;

lock_failed:
		svcSleepThread(50000000);
		--pcTries;

lock_final:
		ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 0, 0);
		if (ret != 0) {
			showDbg("unlocking remote process failed: %08x", ret, 0);
			goto final;
		}
	}

final:
	return ret;
}

static int nsSendPacketHeader() {
	nsContext->remainDataLen = nsContext->packetBuf.dataLen;
	return rtSendSocket(nsContext->hSocket, (u8 *)&nsContext->packetBuf, sizeof(NS_PACKET));
}

static int nsSendPacketData(u8* buf, u32 size) {
	if (nsContext->remainDataLen < size) {
		size = nsContext->remainDataLen;
	}
	nsContext->remainDataLen -= size;
	return rtSendSocket(nsContext->hSocket, buf, size);
}

void nsHandleDbgPrintPacket(void) {
	NS_PACKET* pac = &nsContext->packetBuf;

	if (pac->cmd == NS_CMD_HEARTBEAT) {
		rtAcquireLock(&nsConfig->debugBufferLock);
		nsDbgLn();
		pac->dataLen = nsConfig->debugPtr;
		nsSendPacketHeader();
		if (pac->dataLen > 0) {
			nsSendPacketData(nsConfig->debugBuf, pac->dataLen);
		}
		nsConfig->debugPtr = 0;
		rtReleaseLock(&nsConfig->debugBufferLock);
	}
}

static void nsHandleRemotePlay(void) {
	// TODO
}

void nsHandleMenuPacket(void) {
	NS_PACKET* pac = &nsContext->packetBuf;

	if (pac->cmd == NS_CMD_REMOTEPLAY) {
		nsHandleRemotePlay();
	} else {
		nsHandleDbgPrintPacket();
	}
}

void nsMainLoop(u32 listenPort) {
	while (1) {
		s32 listen_sock, ret, tmp, sockfd;
		struct sockaddr_in addr;

		while (1) {
			listen_sock = socket(AF_INET, SOCK_STREAM, 0);
			if (listen_sock > 0) {
				break;
			}
			svcSleepThread(1000000000);
		}

		addr.sin_family = AF_INET;
		addr.sin_port = htons(listenPort);
		addr.sin_addr.s_addr = INADDR_ANY;

		ret = bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
		if (ret < 0) {
			showDbg("bind failed: %08x", ret, 0);
			goto end_listen;
		}

		if (listenPort == NS_MENU_LISTEN_PORT) {
			tmp = fcntl(listen_sock, F_GETFL);
			fcntl(listen_sock, F_SETFL, tmp | O_NONBLOCK);
		}

		ret = listen(listen_sock, 1);
		if (ret < 0) {
			showDbg("listen failed: %08x", ret, 0);
			goto end_listen;
		}

		while (1) {
			sockfd = accept(listen_sock, NULL, NULL);
			if (sockfd < 0) {
				int serr = errno;
				if (serr == -EWOULDBLOCK || serr == -EAGAIN) {
					svcSleepThread(100000000);
					continue;
				}
				break;
			}
			nsContext->hSocket = sockfd;

			tmp = fcntl(sockfd, F_GETFL, 0);
			fcntl(sockfd, F_SETFL, tmp & ~O_NONBLOCK);

			while (1) {
				ret = rtRecvSocket(sockfd, (u8 *)&nsContext->packetBuf, sizeof(NS_PACKET));
				if (ret != sizeof(NS_PACKET)) {
					nsDbgPrint("rtRecvSocket failed: %08x\n", ret, 0);
					break;
				}
				NS_PACKET *pac = &nsContext->packetBuf;
				if (pac->magic != 0x12345678) {
					nsDbgPrint("broken protocol: %08x, %08x\n", pac->magic, pac->seq);
					break;
				}
				nsHandlePacket();
				pac->magic = 0;
			}
			closesocket(sockfd);
		}

end_listen:
		closesocket(listen_sock);
	}
}

static void nsThread(void *arg) {
	nsMainLoop((u32)arg);
	svcExitThread();
}

void nsStartup(void) {
	u32 socuSharedBufferSize;
	u32 bufferSize;
	socuSharedBufferSize = 0x10000;
	bufferSize = socuSharedBufferSize + rtAlignToPageSize(sizeof(NS_CONTEXT)) + STACK_SIZE;
	u32 base = NS_SOC_ADDR;
	s32 ret;
	u32 outAddr;

	ret = svcControlMemory(&outAddr, base, 0, bufferSize, MEMOP_ALLOC, MEMPERM_READWRITE);
	if (ret != 0) {
		showDbg("svcControlMemory failed: %08x", ret, 0);
		return;
	}

	ret = rtCheckRemoteMemoryRegionSafeForWrite(getCurrentProcessHandle(), base, bufferSize);
	if (ret != 0) {
		showDbg("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x", ret, 0);
		return;
	}

	ret = socInit((u32 *)outAddr, socuSharedBufferSize);
	if (ret != 0) {
		showDbg("socInit failed: %08x", ret, 0);
		return;
	}

	nsContext = (void*)(outAddr + socuSharedBufferSize);
	memset(nsContext, 0, sizeof(NS_CONTEXT));

	u32 listenPort = NS_MENU_LISTEN_PORT;
	if (nsConfig->initMode == NS_INITMODE_FROMHOOK) {
		listenPort = NS_HOOK_LISTEN_PORT + getCurrentProcessId();
	}


	nsConfig->debugBuf = nsContext->debugBuf;
	nsConfig->debugPtr = 0;
	nsConfig->debugBufSize = DEBUG_BUF_SIZE;
	rtInitLock(&nsConfig->debugBufferLock);
	nsConfig->debugReady = 1;

	Handle hThread;
	u32 *threadStack = (void *)(outAddr + socuSharedBufferSize + rtAlignToPageSize(sizeof(NS_CONTEXT)));
	ret = svcCreateThread(&hThread, nsThread, listenPort, &threadStack[(STACK_SIZE / 4) - 10], 0x10, -2);
	if (ret != 0) {
		showDbg("svcCreateThread failed: %08x", ret, 0);
		return;
	}
}

Handle envGetHandle(const char*) {
	return 0;
}
