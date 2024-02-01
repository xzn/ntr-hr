#include "global.h"

#include "3ds/services/soc.h"
#include "3ds/services/hid.h"

#include <memory.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

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

static NS_CONTEXT *const nsContext = (NS_CONTEXT *)NS_CTX_ADDR;

static u8 *const nsDbgBuf = nsContext->debugBuf;
static u8 *const nsDbgBufEnd = nsContext->debugBuf + DEBUG_BUF_SIZE;

static void nsDbgPutc(void *, void const *src, size_t len) {
	const char *s = src;
	while (len) {
		if (nsConfig->debugPtr == nsDbgBufEnd)
			return;
		*nsConfig->debugPtr++ = *s++;
		--len;
	}
}

static void nsDbgLn() {
	if (nsConfig->debugPtr == nsDbgBuf) {
		return;
	} else if (nsConfig->debugPtr == nsDbgBufEnd) {
		if (nsDbgBufEnd[-1] != '\n') {
			nsDbgBufEnd[-1] = '\n';
		}
	} else {
		if (nsDbgBuf[-1] != '\n') {
			nsDbgBuf[0] = '\n';
			++nsConfig->debugPtr;
		}
	}
}

static struct ostrm const ostrm = { .func = nsDbgPutc };

static void nsDbgPrintVAInternal(const char *fmt, va_list arp) {
	xvprintf(&ostrm, fmt, arp);
}

static void nsDbgPrintRawInternal(const char *fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	nsDbgPrintVAInternal(fmt, arp);
	va_end(arp);
}

static void nsDbgPrintVerboseVA(const char *file_name, int line_number, const char *func_name, const char* fmt, va_list arp) {
	if (ALC(&nsConfig->debugReady)) {
		rtAcquireLock(&nsConfig->debugBufferLock);
		nsDbgLn();

		if (file_name && func_name) {
			u64 ticks = svcGetSystemTick();
			u64 mono_us = ticks * 1000000000ULL / SYSCLOCK_ARM11;
			u32 pid = getCurrentProcessId();

			nsDbgPrintRawInternal(DBG_VERBOSE_TITLE, (u32)(mono_us / 1000000), (u32)(mono_us % 1000000), pid, file_name, line_number, func_name);
			nsDbgPrintRawInternal(" ");
		}

		nsDbgPrintVAInternal(fmt, arp);

		rtReleaseLock(&nsConfig->debugBufferLock);
	}
}

void __attribute__((weak)) nsDbgPrintRaw(const char *fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	nsDbgPrintVerboseVA(NULL, 0, NULL, fmt, arp);
	va_end(arp);
}

void __attribute__((weak)) nsDbgPrintVerbose(const char *file_name, int line_number, const char *func_name, const char* fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	nsDbgPrintVerboseVA(file_name, line_number, func_name, fmt, arp);
	va_end(arp);
}

int __attribute__((weak)) nsCheckPCSafeToWrite(u32, u32) {
	return 0;
}

u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg) {
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
	buf = (u32 *)arm11BinStart;
	size = arm11BinSize;
	nsDbgPrint("arm11bin buf: %08"PRIx32", size: %08"PRIx32", remotePC: %08"PRIx32"\n", (u32)buf, size, remotePC);

	if (!buf) {
		showDbg("arm11 not loaded");
		return -1;
	}

	totalSize = size + offset;

	ret = mapRemoteMemory(hProcess, baseAddr, totalSize, MEMOP_ALLOC);
	if (ret != 0) {
		showDbg("mapRemoteMemory failed: %08"PRIx32, ret);
		return ret;
	}
	// set rwx
	ret = protectRemoteMemory(hProcess, (void *)baseAddr, totalSize, MEMPERM_READWRITE | MEMPERM_EXECUTE);
	if (ret != 0) {
		showDbg("protectRemoteMemory failed: %08"PRIx32, ret);
		goto final;
	}
	// load arm11.bin code at arm11StartAddress
	ret = copyRemoteMemory(hProcess, (void *)arm11StartAddress, CUR_PROCESS_HANDLE, buf, size);
	if (ret != 0) {
		showDbg("copyRemoteMemory payload failed: %08"PRIx32, ret);
		goto final;
	}

	ret = rtCheckRemoteMemory(hProcess, remotePC, 8, MEMPERM_READWRITE | MEMPERM_EXECUTE);
	if (ret != 0) {
		showDbg("rtCheckRemoteMemory failed: %08"PRIx32, ret);
		goto final;
	}

	cfg->initMode = NS_INITMODE_FROMHOOK;

	// store original 8-byte code
	ret = copyRemoteMemory(CUR_PROCESS_HANDLE, &(cfg->startupInfo[0]), hProcess, (void *)remotePC, 8);
	if (ret != 0) {
		showDbg("copyRemoteMemory original code to be hooked failed: %08"PRIx32, ret);
		goto final;
	}
	cfg->startupInfo[2] = remotePC;

	// copy cfg structure to remote process
	ret = copyRemoteMemory(hProcess, (void *)baseAddr, CUR_PROCESS_HANDLE, cfg, sizeof(NS_CONFIG));
	if (ret != 0) {
		showDbg("copyRemoteMemory ns_config failed: %08"PRIx32, ret);
		goto final;
	}

	// write hook instructions to remote process
	tmp[0] = 0xe51ff004;
	tmp[1] = arm11StartAddress;
	pcTries = 20;
	while (!pcDone && pcTries) {
		int failed = 0;

		ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0);
		if (ret != 0) {
			showDbg("locking remote process failed: %08"PRIx32, ret);
			goto final;
		}

		ret = nsCheckPCSafeToWrite(hProcess, remotePC);
		if (ret != 0) {
			goto lock_failed;
		}

		ret = copyRemoteMemory(hProcess, (void *)remotePC, CUR_PROCESS_HANDLE, &tmp, 8);
		if (ret != 0) {
			showDbg("copyRemoteMemory hook instruction failed: %08"PRIx32, ret);
			goto lock_failed;
		}

		pcDone = 1;
		goto lock_final;

lock_failed:
		failed = 1;

lock_final:
		ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 0, 0);
		if (ret != 0) {
			showDbg("unlocking remote process failed: %08"PRIx32"", ret);
			goto final;
		}

		if (failed) {
			svcSleepThread(50000000);
			--pcTries;
		}
	}

	if (!pcTries) {
		ret = -1;
		goto final;
	}

	return 0;

final:
	s32 res = mapRemoteMemory(hProcess, baseAddr, totalSize, MEMOP_FREE);
	if (res != 0) {
		nsDbgPrint("mapRemoteMemory free failed: %08"PRIx32"\n", res);
	}
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
		pac->dataLen = nsConfig->debugPtr - nsDbgBuf;
		nsSendPacketHeader();
		if (pac->dataLen > 0) {
			nsSendPacketData(nsDbgBuf, pac->dataLen);
		}
		nsConfig->debugPtr = nsDbgBuf;
		rtReleaseLock(&nsConfig->debugBufferLock);
	}
}

static void nsHandleRemotePlay(void) {
	NS_PACKET* pac = &nsContext->packetBuf;
	RP_CONFIG config = {};
	config.mode = pac->args[0];
	config.quality = pac->args[1];
	config.qos = pac->args[2];
	if (pac->args[3] == 1404036572) /* guarding magic */
		config.dstPort = pac->args[4];
	rpStartupFromMenu(&config);
}

void nsHandleMenuPacket(void) {
	NS_PACKET* pac = &nsContext->packetBuf;

	if (pac->cmd == NS_CMD_REMOTEPLAY) {
		nsHandleRemotePlay();
	} else {
		nsHandleDbgPrintPacket();
	}
}

static void nsMainLoop(u32 listenPort) {
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
			showDbg("bind failed: %08"PRIx32, (u32)errno);
			goto end_listen;
		}

		if (listenPort == NS_MENU_LISTEN_PORT) {
			tmp = fcntl(listen_sock, F_GETFL);
			fcntl(listen_sock, F_SETFL, tmp | O_NONBLOCK);
		}

		ret = listen(listen_sock, 1);
		if (ret < 0) {
			showDbg("listen failed: %08"PRIx32, (u32)errno);
			goto end_listen;
		}

		while (1) {
			sockfd = accept(listen_sock, NULL, NULL);
			if (sockfd < 0) {
				int serr = errno;
				if (serr == EWOULDBLOCK || serr == EAGAIN) {
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
					nsDbgPrint("rtRecvSocket failed: %08"PRIx32"\n", ret);
					break;
				}
				NS_PACKET *pac = &nsContext->packetBuf;
				if (pac->magic != 0x12345678) {
					nsDbgPrint("broken protocol: %08"PRIx32", %08"PRIx32"\n", pac->magic, pac->seq);
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

int nsStartup(void) {
	u32 socuSharedBufferSize;
	u32 bufferSize;
	socuSharedBufferSize = NS_SOC_SHARED_BUF_SIZE;
	u32 offset = STACK_SIZE + rtAlignToPageSize(sizeof(NS_CONTEXT));
	bufferSize = socuSharedBufferSize + offset;
	u32 base = NS_SOC_ADDR;
	s32 ret, res;
	u32 outAddr;

	ret = svcControlMemory(&outAddr, base, base, bufferSize, MEMOP_ALLOC, MEMPERM_READWRITE);
	if (ret != 0) {
		showDbg("svcControlMemory alloc failed: %08"PRIx32, ret);
		goto fail;
	}

	ret = socInit((u32 *)(base + offset), socuSharedBufferSize);
	if (ret != 0) {
		showDbg("socInit failed: %08"PRIx32, ret);
		goto fail_alloc;
	}

	*nsContext = (NS_CONTEXT){ 0 };

	u32 listenPort = NS_MENU_LISTEN_PORT;
	u32 affinity = 0x10;
	if (nsConfig->initMode == NS_INITMODE_FROMHOOK) {
		listenPort = NS_HOOK_LISTEN_PORT + getCurrentProcessId();
		affinity = 0x3f;
	}

	if (!ALC(&nsConfig->debugReady)) {
		rtInitLock(&nsConfig->debugBufferLock);
		nsConfig->debugPtr = nsDbgBuf;
		ASL(&nsConfig->debugReady, 1);
	}

	Handle hThread;
	u32 *threadStack = (u32 *)base;
	ret = svcCreateThread(&hThread, nsThread, listenPort, &threadStack[(STACK_SIZE / 4) - 10], affinity, -2);
	if (ret != 0) {
		showDbg("svcCreateThread failed: %08"PRIx32, ret);
		goto fail_soc;
	}

	return 0;

fail_soc:
	res = socExit();
	if (res != 0) {
		nsDbgPrint("socExit failed: %08"PRIx32"\n", ret);
	}

fail_alloc:
	res = svcControlMemory(&outAddr, base, base, bufferSize, MEMOP_FREE, 0);
	if (res != 0) {
		nsDbgPrint("svcControlMemory free failed: %08"PRIx32"\n", ret);
	}

fail:
	return ret;
}

Handle envGetHandle(const char*) {
	return 0;
}

void envDestroyHandles(void) {}

int nsDbgNext(void) {
	if ((getKeys() & KEY_DLEFT)) {
		return 1;
	}
	return 0;
}

void __attribute__((weak)) nsHandlePacket(void) {
	nsHandleDbgPrintPacket();
}
