#include "global.h"
#include "ctr/SOC.h"
#include "ctr/syn.h"
#include <sys/select.h>
#include "sys/socket.h"
#include "netinet/in.h"
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

NS_CONTEXT* g_nsCtx = 0;
NS_CONFIG* g_nsConfig;

void nsDbgPutc(char ch) {
	if (g_nsConfig->debugPtr >= g_nsConfig->debugBufSize) {
		return;
	}
	(g_nsConfig->debugBuf)[g_nsConfig->debugPtr] = ch;
	g_nsConfig->debugPtr++;
}

void nsDbgPrint(			/* Put a formatted string to the default device */
	const char*	fmt,	/* Pointer to the format string */
	...					/* Optional arguments */
	)
{
	va_list arp;

	va_start(arp, fmt);
	if (g_nsConfig) {
		if (g_nsConfig->debugReady) {
			rtAcquireLock(&(g_nsConfig->debugBufferLock));
			xvprintf(fmt, arp);
			rtReleaseLock(&(g_nsConfig->debugBufferLock));
		}
	}

	va_end(arp);
}

int nsSendPacketHeader(void) {
	g_nsCtx->remainDataLen = g_nsCtx->packetBuf.dataLen;
	rtSendSocket(g_nsCtx->hSocket, (u8*)&(g_nsCtx->packetBuf), sizeof(NS_PACKET));
}

int nsSendPacketData(u8* buf, u32 size) {
	if (g_nsCtx->remainDataLen < size) {
		showDbg("send remain < size: %08x, %08x", g_nsCtx->remainDataLen, size);
		return -1;
	}
	g_nsCtx->remainDataLen -= size;
	rtSendSocket(g_nsCtx->hSocket, buf, size);
}

int nsRecvPacketData(u8* buf, u32 size) {
	if (g_nsCtx->remainDataLen < size) {
		showDbg("recv remain < size: %08x, %08x", g_nsCtx->remainDataLen, size);
		return -1;
	}
	g_nsCtx->remainDataLen -= size;
	rtRecvSocket(g_nsCtx->hSocket, buf, size);
}

static void nsRemotePlayControl(Handle hProcess, NS_CONFIG *cfg) {
	int ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, startupInfo[8]),
		CURRENT_PROCESS_HANDLE,
		&cfg->startupInfo[8],
		3 * sizeof(cfg->startupInfo[8]));
	if (ret != 0) {
		nsDbgPrint("nsRemotePlayControl copyRemoteMemory failed : %08x\n", ret);
		return;
	}

	int control = 1;
	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, remotePlayUpdate),
		CURRENT_PROCESS_HANDLE,
		&control,
		sizeof(control));
	if (ret != 0) {
		nsDbgPrint("nsRemotePlayControl control copy failed : %08x\n", ret);
		return;
	}

	return;
}

u8 nsRemotePlayStarted;
int nsHandleRemotePlay(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);

	Handle hProcess;
	u32 ret;
	u32 pid = 0x1a;
	u32 remotePC = 0x001231d0;
	NS_CONFIG	cfg;

	memset(&cfg, 0, sizeof(NS_CONFIG));
	cfg.startupCommand = NS_STARTCMD_DEBUG;
	cfg.startupInfo[8] = pac->args[0];
	cfg.startupInfo[9] = pac->args[1];
	cfg.startupInfo[10] = pac->args[2];

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}

	if (nsRemotePlayStarted) {
		nsDbgPrint("RemotePlay already started, updating params\n");
		nsRemotePlayControl(hProcess, &cfg);
		svc_closeHandle(hProcess);
		return 0;
	}
	nsRemotePlayStarted = 1;

	u8 desiredHeader[16] = { 0x04, 0x00, 0x2D, 0xE5, 0x4F, 0x00, 0x00, 0xEF, 0x00, 0x20, 0x9D, 0xE5, 0x00, 0x10, 0x82, 0xE5 };
	u8 buf[16] = { 0 };
	if (!(ntrConfig->isNew3DS)) {
		nsDbgPrint("remoteplay is available on new3ds only\n");
		goto final;
	}

	int isFirmwareSupported = 0;
	int firmwareType = 0;
	for (firmwareType = 0; firmwareType <= 1; firmwareType++) {
		if (firmwareType == 0) {
			cfg.startupInfo[11] = 0x120464; //nwmvalparamhook
			cfg.startupInfo[12] = 0x00120DC8 + 1; //nwmSendPacket
			remotePC = 0x001231d0;
		}
		else if (firmwareType == 1) {
			cfg.startupInfo[11] = 0x120630; //nwmvalparamhook
			cfg.startupInfo[12] = 0x00120f94 + 1; //nwmSendPacket
			remotePC = 0x123394;

		}
		copyRemoteMemory(CURRENT_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, 16);
		svc_sleepThread(100000000);
		if (memcmp(buf, desiredHeader, sizeof(desiredHeader)) == 0) {
			isFirmwareSupported = 1;
			break;
		}
	}
	if (!isFirmwareSupported) {
		nsDbgPrint("remoteplay is not supported on current firmware\n");
		goto final;
	}
	setCpuClockLock(3);
	nsDbgPrint("cpu was locked on 804MHz, L2 Enabled\n");
	nsDbgPrint("starting remoteplay...\n");
	nsAttachProcess(hProcess, remotePC, &cfg, 1);

	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}

	return 0;
}

void nsHandleSaveFile(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 remain = pac->dataLen;
	u8 buf[0x220];
	u32 ret, hFile;
	u32 off = 0, tmp;

	nsRecvPacketData(buf, 0x200);
	buf[0x200] = 0;
	remain -= 0x200;

	FS_path testPath = (FS_path){ PATH_CHAR, strlen(buf) + 1, buf };
	ret = FSUSER_OpenFileDirectly(fsUserHandle, &hFile, sdmcArchive, testPath, 7, 0);
	if (ret != 0) {
		showDbg("openFile failed: %08x", ret, 0);
		return;
	}

	while (remain) {
		u32 t = 0x2000;
		if (t > remain) {
			t = remain;
		}
		nsRecvPacketData(g_nsCtx->gBuff, t);
		FSFILE_Write(hFile, &tmp, off, (u32*)g_nsCtx->gBuff, t, 0);

		remain -= t;
		off += t;
	}
	svc_closeHandle(hFile);
	nsDbgPrint("saved to %s successfully\n", buf);
}

int nsFindFreeBreakPoint(void) {
	int i;
	for (i = 1; i < MAX_BREAKPOINT; i++) {
		if (g_nsCtx->breakPoint[i].type == 0) {
			return i;
		}
	}
	return -1;
}
void nsEnableBreakPoint(int id) {
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	if (bp->isEnabled) {
		nsDbgPrint("bp %d already enabled\n", id);
		return;
	}
	bp->isEnabled = 1;
	if ((bp->type == NS_BPTYPE_CODE) || (bp->type == NS_BPTYPE_CODEONESHOT)) {
		rtEnableHook(&bp->hook);
	}
	nsDbgPrint("bp %d enabled\n", id);
}

void nsDisableBreakPoint(int id) {
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	if (!bp->isEnabled) {
		nsDbgPrint("bp %d already disabled\n", id);
		return;
	}
	bp->isEnabled = 0;
	if ((bp->type == NS_BPTYPE_CODE) || (bp->type == NS_BPTYPE_CODEONESHOT)) {
		rtDisableHook(&bp->hook);
	}
	nsDbgPrint("bp %d disabled\n", id);
}

void nsBreakPointCallback(u32 regs, u32 bpid, u32 regs2) {
	vu32 resumeFlag;
	NS_BREAKPOINT_STATUS * bpStatus = &(g_nsCtx->breakPointStatus);
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[bpid]);

	//wait if another breakpoint is triggered
	rtAcquireLock(&(g_nsCtx->breakPointTriggerLock));
	if (bp->type == NS_BPTYPE_CODEONESHOT) {
		nsDisableBreakPoint(bpid);
	}

	//update bp status
	rtAcquireLock(&(g_nsCtx->breakPointStatusLock));
	bpStatus->regs = (vu32*)regs;
	bpStatus->bpid = bpid;
	bpStatus->resumeFlag = 0;
	rtReleaseLock(&(g_nsCtx->breakPointStatusLock));

	//wait for resume flag
	while (1) {
		rtAcquireLock(&(g_nsCtx->breakPointStatusLock));
		resumeFlag = bpStatus->resumeFlag;
		rtReleaseLock(&(g_nsCtx->breakPointStatusLock));
		svc_sleepThread(100000000);
		if (resumeFlag) {
			break;
		}
	}
	bpStatus->bpid = 0;
	rtReleaseLock(&(g_nsCtx->breakPointTriggerLock));
}

u32 nsInitCodeBreakPoint(int id) {
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	RT_HOOK* hk = &(bp->hook);
	u32 ret;
	u32 retAddr = 0;
	static const u8 buf[] = {
		0xFF, 0x5F, 0x2D, 0xE9, 0x00, 0x00, 0x0F, 0xE1, 0x01, 0x00, 0x2D, 0xE9, 0x0D, 0x00, 0xA0, 0xE1,
		0x14, 0x10, 0x9F, 0xE5, 0x14, 0x20, 0x9F, 0xE5, 0x32, 0xFF, 0x2F, 0xE1, 0x01, 0x00, 0xBD, 0xE8,
		0x00, 0xF0, 0x29, 0xE1, 0xFF, 0x5F, 0xBD, 0xE8, 0x04, 0xF0, 0x9F, 0xE5
	};
	/*
	STMFD	SP!, {R0-R12, LR}
	MRS     R0, CPSR
	STMFD	SP!, {R0}
	MOV	R0, SP
	LDR	R1,=0xbbcc0001
	LDR	R2,=0xbbcc0002
	BLX	R2
	LDMFD SP!, {R0}
	MSR CPSR, R0
	LDMFD	SP!, {R0-R12, LR}
	LDR	PC, =0xbbcc0003
	*/

	u32 pos = (sizeof(buf) / 4);

	ret = rtCheckRemoteMemoryRegionSafeForWrite(getCurrentProcessHandle(), bp->addr, 8);
	if (ret != 0){
		nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed :%08x\n", ret);
		return ret;
	}

	if (bp->type == NS_BPTYPE_CODE) {
		retAddr = (u32)hk->callCode;
	}

	if (bp->type == NS_BPTYPE_CODEONESHOT) {
		retAddr = bp->addr;
	}

	memcpy(bp->stubCode, buf, sizeof(buf));
	bp->stubCode[pos] = id;
	bp->stubCode[pos + 1] = (u32)nsBreakPointCallback;
	bp->stubCode[pos + 2] = (u32)retAddr;

	rtFlushInstructionCache(bp->stubCode, 16 * 4);

	rtInitHook(&(bp->hook), bp->addr, (u32)(bp->stubCode));
	svc_sleepThread(100000000);
	return 0;
}

void nsInitBreakPoint(int id, u32 addr, int type) {
	u32 ret;
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	bp->flag = 0;
	bp->type = type;
	bp->isEnabled = 0;
	bp->addr = addr;

	if ((type == NS_BPTYPE_CODE) || (type == NS_BPTYPE_CODEONESHOT)) {
		ret = nsInitCodeBreakPoint(id);
		if (ret == 0) {
			nsDbgPrint("code breakpoint, id: %d, addr: %08x\n", id, addr);
			nsEnableBreakPoint(id);
			return;
		}
	}
	bp->type = NS_BPTYPE_UNUSED;
	nsDbgPrint("init breakpoint failed.\n");
}

void nsHandleQueryHandle(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 hProcess = 0;
	u32 ret, i;
	u32 pid = pac->args[0];
	u32 buf[400];

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openprocess failed.\n");
		return;
	}
	//showDbg("hprocess: %08x", hProcess, 0);
	u32 pKProcess = kGetKProcessByHandle(hProcess);
	u32 pHandleTable;
	kmemcpy(&pHandleTable, (void *)(pKProcess + KProcessHandleDataOffset), 4);
	//showDbg("pHandleTable: %08x", pHandleTable, 0);
	kmemcpy(buf, (void *)pHandleTable, sizeof(buf));
	for (i = 0; i < 400; i += 2) {
		u32 ptr = buf[i + 1];
		if (ptr) {
			u32 handleHigh = *((u16*)&(buf[i]));
			u32 handleLow = i / 2;
			u32 handle = (handleHigh << 15) | handleLow;
			nsDbgPrint("h: %08x, p: %08x\n", handle, ptr);
		}
	}
	nsDbgPrint("done");
	svc_closeHandle(hProcess);
}

void nsHandleBreakPoint(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 type = pac->args[0];
	u32 addr = pac->args[1];
	u32 method = pac->args[2];
	u32 bpid = pac->args[0];
	int id;

	if (method == 1) { // add
		id = nsFindFreeBreakPoint();
		nsDbgPrint("freeid: %d\n", id);
		if (id == -1) {
			return;
		}
		nsInitBreakPoint(id, addr, type);
	}
	if (method == 4) { // resume
		nsDbgPrint("set resume flag");
		g_nsCtx->breakPointStatus.resumeFlag = 1;
		g_nsCtx->isBreakPointHandled = 0;
	}

	if (bpid >= MAX_BREAKPOINT) {
		nsDbgPrint("invalid bpid\n");
		return;
	}

	if (method == 2) { // ena
		nsEnableBreakPoint(bpid);
	}

	if (method == 3) { // dis
		nsDisableBreakPoint(bpid);
	}
}

void nsHandleReload(void) {
	u32 ret, outAddr;
	u32 hFile, size;
	u64 size64;
	u8* fileName = "/arm11.bin";
	u32 tmp;

	typedef void(*funcType)();
	g_nsConfig->initMode = 1;
	closesocket(g_nsCtx->hSocket);
	closesocket(g_nsCtx->hListenSocket);
	FS_path testPath = (FS_path){ PATH_CHAR, strlen(fileName) + 1, fileName };
	ret = FSUSER_OpenFileDirectly(fsUserHandle, &hFile, sdmcArchive, testPath, 7, 0);
	if (ret != 0) {
		showDbg("openFile failed: %08x", ret, 0);
		return;
	}
	ret = FSFILE_GetSize(hFile, &size64);
	if (ret != 0) {
		showDbg("FSFILE_GetSize failed: %08x", ret, 0);
		return;
	}
	size = size64;
	size = rtAlignToPageSize(size);
	ret = svc_controlMemory((u32*)&outAddr, 0, 0, size, 0x10003, 3);
	if (ret != 0) {
		showDbg("svc_controlMemory failed: %08x", ret, 0);
		return;
	}

	ret = FSFILE_Read(hFile, &tmp, 0, (u32*)outAddr, size);
	if (ret != 0) {
		showDbg("FSFILE_Read failed: %08x", ret, 0);
		return;
	}
	ret = protectMemory((u32*)outAddr, size);
	if (ret != 0) {
		showDbg("protectMemory failed: %08x", ret, 0);
		return;
	}
	((funcType)outAddr)();
	svc_exitThread();
}

void nsHandleListProcess(void) {
	u32 pids[100];
	u8 pname[20];
	u32 tid[4];
	u32 pidCount;
	u32 i, ret;
	u32 kpobj;

	ret = svc_getProcessList(&pidCount, pids, 100);
	if (ret != 0) {
		nsDbgPrint("getProcessList failed: %08x\n", ret);
		return;
	}
	for (i = 0; i < pidCount; i++) {

		ret = getProcessInfo(pids[i], pname, tid, &kpobj);
		if (ret != 0) {
			nsDbgPrint("getProcessInfo failed: %08x\n", ret);
		}
		nsDbgPrint("pid: 0x%08x, pname: %8s, tid: %08x%08x, kpobj: %08x\n", pids[i], pname, tid[1], tid[0], kpobj);
	}
	nsDbgPrint("end of process list.\n");
}

void printMemLayout(Handle hProcess, u32 base, u32 limit) {
	u32 ret, isValid = 0, isLastValid = 0;
	u32 lastAddr = 0;
	while (base < limit) {
		ret = protectRemoteMemory(hProcess, (void*)base, 0x1000);
		isValid = (ret == 0);
		if (isValid != isLastValid) {
			if (isLastValid) {
				nsDbgPrint("%08x - %08x , size: %08x\n", lastAddr, base - 1, base - lastAddr);
			}

			lastAddr = base;
			isLastValid = isValid;
		}
		base += 0x1000;
	}
}

void nsHandleMemLayout(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 pid = pac->args[0];
	u32 isLastValid = 0, lastAddr = 0;
	u32 isValid;
	u32 base = 0x00100000;
	u32 ret, hProcess;

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}
	nsDbgPrint("valid memregions:\n");
	printMemLayout(hProcess, 0x00100000, 0x1F601000);
	printMemLayout(hProcess, 0x30000000, 0x40000000);
	nsDbgPrint("end of memlayout.\n");
	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}

}

void nsHandleWriteMem(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 pid = pac->args[0];
	u32 addr = pac->args[1];
	u32 size = pac->args[2];
	u32 isLastValid = 0, lastAddr = 0;
	u32 isValid;
	u32 base, remain;
	u32 ret, hProcess;

	if (pid == -1) {
		hProcess = getCurrentProcessHandle();
	}
	else {
		ret = svc_openProcess(&hProcess, pid);
		if (ret != 0) {
			nsDbgPrint("openProcess failed: %08x, pid: %08x\n", ret, pid);
			hProcess = 0;
			goto final;
		}
	}
	if (addr < 0x20000000) {
		ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr, size);
		if (ret != 0) {
			nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x\n", ret, 0);
			goto final;
		}
	}
	remain = size;
	while (remain) {
		u32 tmpsize = 0x1000;
		if (tmpsize > remain) {
			tmpsize = remain;
		}
		nsRecvPacketData(g_nsCtx->gBuff, tmpsize);
		if (pid == -1) {
			if (addr > 0x20000000) {
				kmemcpy((void*)addr, g_nsCtx->gBuff, tmpsize);
			}
			else {
				memcpy((void*)addr, g_nsCtx->gBuff, tmpsize);
			}

		}
		else {
			ret = copyRemoteMemory(hProcess, (void*)addr, 0xffff8001, g_nsCtx->gBuff, tmpsize);
			if (ret != 0) {
				nsDbgPrint("copyRemoteMemory failed: %08x, addr: %08x\n", ret, addr);
			}
		}
		addr += tmpsize;
		remain -= tmpsize;
	}
	nsDbgPrint("finished");
	final:
	if (hProcess != 0) {
		if (pid != -1) {
			svc_closeHandle(hProcess);
		}
	}
}

void nsHandleReadMem(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 pid = pac->args[0];
	u32 addr = pac->args[1];
	u32 size = pac->args[2];
	u32 isLastValid = 0, lastAddr = 0;
	u32 isValid;
	u32 base, remain;
	u32 ret, hProcess;

	if (pid == -1) {
		hProcess = getCurrentProcessHandle();
	}
	else {
		ret = svc_openProcess(&hProcess, pid);
		if (ret != 0) {
			nsDbgPrint("openProcess failed: %08x, pid: %08x\n", ret, pid);
			hProcess = 0;
			goto final;
		}
	}
	if (addr < 0x20000000) {
		ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr, size);
		if (ret != 0) {
			nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x\n", ret, 0);
			goto final;
		}
	}
	pac->dataLen = size;
	nsSendPacketHeader();
	remain = size;
	while (remain) {
		u32 tmpsize = 0x1000;
		if (tmpsize > remain) {
			tmpsize = remain;
		}
		if (pid == -1) {
			if (addr > 0x20000000) {
				kmemcpy(g_nsCtx->gBuff, (void*)addr, tmpsize);
			}
			else{
				memcpy(g_nsCtx->gBuff, (void*)addr, tmpsize);
			}

		}
		else {
			ret = copyRemoteMemory(0xffff8001, g_nsCtx->gBuff, hProcess, (void*)addr, tmpsize);
			if (ret != 0) {
				nsDbgPrint("copyRemoteMemory failed: %08x, addr: %08x\n", ret, addr);
			}
		}
		nsSendPacketData(g_nsCtx->gBuff, tmpsize);
		addr += tmpsize;
		remain -= tmpsize;
	}
	nsDbgPrint("finished");
	final:
	if (hProcess != 0) {
		if (pid != -1) {
			svc_closeHandle(hProcess);
		}

	}
}

u32 nsGetPCToAttachProcess(u32 hProcess) {
	u32 handle, ret;
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 pid = pac->args[0];
	u32 tids[100];
	u32 tidCount, i, j;
	u32 ctx[400];
	u32 pc[100], lr[100];
	u32 addr = 0;


	ret = svc_getThreadList(&tidCount, tids, 100, hProcess);
	if (ret != 0) {
		nsDbgPrint("getThreadList failed: %08x\n", ret);
		return 0;
	}
	for (i = 0; i < tidCount; i++) {
		u32 tid = tids[i];
		memset(ctx, 0x33, sizeof(ctx));
		rtGetThreadReg(hProcess, tid, ctx);
		pc[i] = ctx[15];
		lr[i] = ctx[14];
	}

	nsDbgPrint("recommend pc:\n");
	for (i = 0; i < tidCount; i++) {
		for (j = 0; j < tidCount; j++) {
			if ((i != j) && (pc[i] == pc[j])) break;
		}
		if (j >= tidCount) {
			nsDbgPrint("%08x\n", pc[i]);
			if (!addr) addr = pc[i];
		}
	}

	nsDbgPrint("recommend lr:\n");
	for (i = 0; i < tidCount; i++) {
		for (j = 0; j < tidCount; j++) {
			if ((i != j) && (lr[i] == lr[j])) break;
		}
		if (j >= tidCount) {
			nsDbgPrint("%08x\n", lr[i]);
			if (!addr) addr = lr[i];
		}
	}
	return addr;
}

void nsHandleListThread(void) {
	u32 handle, ret;
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	Handle hProcess;
	u32 pid = pac->args[0];
	u32 tids[100];
	u32 tidCount, i, j;
	u32 ctx[400];
	// u32 hThread;
	u32 pKThread;
	u32 pContext;

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}
	ret = svc_getThreadList(&tidCount, tids, 100, hProcess);
	if (ret != 0) {
		nsDbgPrint("getThreadList failed: %08x\n", ret);
		return;
	}
	for (i = 0; i < tidCount; i++) {
		u32 tid = tids[i];
		nsDbgPrint("tid: 0x%08x\n", tid);
		memset(ctx, 0x33, sizeof(ctx));
		rtGetThreadReg(hProcess, tid, ctx);
		nsDbgPrint("pc: %08x, lr: %08x\n", ctx[15], ctx[14]);
		for (j = 0; j < 32; j++) {

			nsDbgPrint("%08x ", ctx[j]);
		}
		nsDbgPrint("\n");
		// svc_closeHandle(hThread);

	}
	nsGetPCToAttachProcess(hProcess);

	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
}

u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg, int sysRegion) {
	u32 size = 0;
	u32* buf = 0;
	u32 baseAddr = NS_CONFIGURE_ADDR;
	u32 stackSize = 0x4000;
	u32 totalSize;
	u32 handle, ret, outAddr;
	u32 tmp[20];
	u32 arm11StartAddress;

	arm11StartAddress = baseAddr + 0x1000 + stackSize;
	buf = (u32*)arm11BinStart;
	size = arm11BinSize;
	nsDbgPrint("buf: %08x, size: %08x\n", buf, size);


	if (!buf) {
		nsDbgPrint("arm11 not loaded\n");
		return -1;
	}

	totalSize = size + stackSize + 0x1000;
	if (sysRegion) {
		// allocate buffer to remote memory
		ret = mapRemoteMemoryInSysRegion(hProcess, baseAddr, totalSize);
	}
	else {
		// allocate buffer to remote memory
		ret = mapRemoteMemory(hProcess, baseAddr, totalSize);
	}

	if (ret != 0) {
		nsDbgPrint("mapRemoteMemory failed: %08x\n", ret, 0);
	}
	// set rwx
	ret = protectRemoteMemory(hProcess, (void*)baseAddr, totalSize);
	if (ret != 0) {
		nsDbgPrint("protectRemoteMemory failed: %08x\n", ret, 0);
		goto final;
	}
	// load arm11.bin code at arm11StartAddress
	ret = copyRemoteMemory(hProcess, (void*)arm11StartAddress, arm11BinProcess, buf, size);
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(1) failed: %08x\n", ret, 0);
		goto final;
	}

	if (remotePC == 0) {
		remotePC = nsGetPCToAttachProcess(hProcess);
	}
	nsDbgPrint("remotePC: %08x\n", remotePC, 0);
	if (remotePC == 0) {
		goto final;
	}
	ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, remotePC, 8);
	if (ret != 0) {
		nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x\n", ret, 0);
		goto final;
	}



	cfg->initMode = NS_INITMODE_FROMHOOK;

	// store original 8-byte code
	ret = copyRemoteMemory(0xffff8001, &(cfg->startupInfo[0]), hProcess, (void*)remotePC, 8);
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(3) failed: %08x\n", ret, 0);
		goto final;
	}
	cfg->startupInfo[2] = remotePC;
	memcpy(&(cfg->ntrConfig), ntrConfig, sizeof(NTR_CONFIG));
	// copy cfg structure to remote process
	ret = copyRemoteMemory(hProcess, (void*)baseAddr, 0xffff8001, cfg, sizeof(NS_CONFIG));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(2) failed: %08x\n", ret, 0);
		goto final;
	}

	// write hook instructions to remote process
	tmp[0] = 0xe51ff004;
	tmp[1] = arm11StartAddress;
	ret = copyRemoteMemory(hProcess, (void*)remotePC, 0xffff8001, &tmp, 8);
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(4) failed: %08x\n", ret, 0);
		goto final;
	}

	final:
	return ret;
}

void nsHandleAttachProcess(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	Handle hProcess;
	u32 ret;
	u32 pid = pac->args[0];
	u32 remotePC = pac->args[1];
	NS_CONFIG	cfg;

	memset(&cfg, 0, sizeof(NS_CONFIG));
	if (pid == 2) {
		cfg.startupCommand = NS_STARTCMD_INJECTPM;
	}
	else {
		cfg.startupCommand = NS_STARTCMD_DEBUG;
	}
	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}
	nsAttachProcess(hProcess, remotePC, &cfg, 0);
	nsDbgPrint("will listen at port %d \n", pid + 5000);
	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
	return;
}

void nsPrintRegs(u32* regs) {
	u32 i;

	nsDbgPrint("cpsr:%08x ", regs[0]);
	nsDbgPrint("lr:%08x sp:%08x\n", regs[14], (u32)(regs)+14 * 4);
	for (i = 0; i <= 12; i++) {
		nsDbgPrint("r%d:%08x ", i, regs[1 + i]);
	}
	nsDbgPrint("\n");
}

void nsUpdateDebugStatus(void) {
	NS_BREAKPOINT_STATUS  bpStatus;
	u32 isActived = 0;

	rtAcquireLock(&(g_nsCtx->breakPointStatusLock));
	memcpy(&bpStatus, &(g_nsCtx->breakPointStatus), sizeof(NS_BREAKPOINT_STATUS));
	rtReleaseLock(&(g_nsCtx->breakPointStatusLock));

	if ((bpStatus.bpid != 0) && (bpStatus.resumeFlag != 1)) {
		isActived = 1;
	}

	if ((isActived) && (!g_nsCtx->isBreakPointHandled)) {
		nsDbgPrint("breakpoint %d hit\n", bpStatus.bpid);
		nsPrintRegs((u32*)bpStatus.regs);
	}
	g_nsCtx->isBreakPointHandled = isActived;
}

void nsHandlePacket(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	g_nsCtx->remainDataLen = pac->dataLen;
	if (pac->cmd == NS_CMD_SAYHELLO) {
		disp(100, 0x100ff00);
		nsDbgPrint("hello\n");
		return;
	}

	if (pac->cmd == NS_CMD_HEARTBEAT) {
		rtAcquireLock(&(g_nsConfig->debugBufferLock));
		pac->dataLen = g_nsConfig->debugPtr;
		nsSendPacketHeader();
		if (pac->dataLen > 0) {
			nsSendPacketData(g_nsConfig->debugBuf, pac->dataLen);
		}
		g_nsConfig->debugPtr = 0;
		rtReleaseLock(&(g_nsConfig->debugBufferLock));
		return;
	}

	if (pac->cmd == NS_CMD_SAVEFILE) {
		nsHandleSaveFile();
		return;
	}

	if (pac->cmd == NS_CMD_RELOAD) {
		nsHandleReload();
		return;
	}

	if (pac->cmd == NS_CMD_LSPROCESS) {
		nsHandleListProcess();
		return;
	}

	if (pac->cmd == NS_CMD_LSTHREAD) {
		nsHandleListThread();
		return;
	}

	if (pac->cmd == NS_CMD_ATTACHPROCESS) {
		nsHandleAttachProcess();
		return;
	}

	if (pac->cmd == NS_CMD_MEMLAYOUT) {
		nsHandleMemLayout();
		return;
	}

	if (pac->cmd == NS_CMD_READMEM) {
		nsHandleReadMem();
		return;
	}

	if (pac->cmd == NS_CMD_WRITEMEM) {
		nsHandleWriteMem();
		return;
	}

	if (pac->cmd == NS_CMD_BREAKPOINT) {
		nsHandleBreakPoint();
		return;
	}

	if (pac->cmd == NS_CMD_QUERYHANDLE) {
		nsHandleQueryHandle();
		return;
	}
	if (pac->cmd == NS_CMD_REMOTEPLAY) {
		if (g_nsCtx->listenPort == 8000) {
			nsHandleRemotePlay();
		}
		return;
	}
}

void nsMainLoop(void) {
	s32 listen_sock, ret, tmp, sockfd;
	struct sockaddr_in addr;

	while (1) {
		checkExitFlag();
		listen_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (listen_sock >= 0) {
			break;
		}
		svc_sleepThread(1000000000);
	}

	g_nsCtx->hListenSocket = listen_sock;

	addr.sin_family = AF_INET;
	addr.sin_port = rtIntToPortNumber(g_nsCtx->listenPort);
	addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		showDbg("bind failed: %08x\n", ret, 0);
		return;
	}
	ret = listen(listen_sock, 1);
	if (ret < 0) {
		showDbg("listen failed: %08x\n", ret, 0);
		return;
	}

	struct pollfd pi[2];
	fd_set rset;
	int maxfdp1;
	int nready;

	int rpProcess = getCurrentProcessId() == 0x1a;
	// int rpProcess = 0;
	if (rpProcess) {
		rp_recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (rp_recv_sock < 0) {
			showDbg("rp_recv_sock open failed: %d\n", rp_recv_sock, 0);
			disp(100, 0x100ffff);
			return;
		}

		struct sockaddr_in sai = {0};
		sai.sin_family = AF_INET;
		sai.sin_port = htons(RP_PORT);
		sai.sin_addr.s_addr = htonl(INADDR_ANY);

		int ret = bind(rp_recv_sock, (struct sockaddr *)&sai, sizeof(sai));
		if (ret < 0) {
			showDbg("rp_recv_sock bind failed: %d\n", ret, 0);
			disp(100, 0x1ffff00);
			return;
		}
	}

#define RP_PROCESS_SELECT2(s) \
	if (rpProcess) { \
		pi[0].fd = s; \
		pi[1].fd = rp_recv_sock; \
		pi[1].events = pi[0].events = POLLIN; \
		pi[1].revents = pi[0].revents = 0; \
		nready = poll2(pi, 2, -1); \
		if (nready <= 0) \
			continue; \
	} \
	if (rpProcess && pi[1].revents & (POLLIN|POLLHUP)) { \
		rpControlRecv(); \
	} \
	if (!rpProcess || pi[0].revents & (POLLIN|POLLHUP))

#define RP_PROCESS_SELECT(s) \
	if (rpProcess) { \
		FD_ZERO(&rset); \
		FD_SET(s, &rset); \
		FD_SET(rp_recv_sock, &rset); \
		maxfdp1 = (s > rp_recv_sock ? s : rp_recv_sock) + 1; \
		nready = select2(maxfdp1, &rset, NULL, NULL, NULL); \
		if (nready <= 0) \
			continue; \
	} \
 \
	if (rpProcess && FD_ISSET(rp_recv_sock, &rset)) { \
		rpControlRecv(); \
	} \
 \
	if (!rpProcess || FD_ISSET(s, &rset))

	while (1) {
		RP_PROCESS_SELECT(listen_sock) {
			sockfd = accept(listen_sock, NULL, NULL);
			g_nsCtx->hSocket = sockfd;
			if (sockfd < 0) {
				if (!rpProcess) {
					svc_sleepThread(100000000);
				}
				continue;
			}
			/*
			tmp = fcntl(sockfd, F_GETFL);
			fcntl(sockfd, F_SETFL, tmp | O_NONBLOCK);
			*/
			while (1) {
				RP_PROCESS_SELECT2(sockfd) {
					ret = rtRecvSocket(sockfd, (u8*)&(g_nsCtx->packetBuf), sizeof(NS_PACKET));
					if (ret != sizeof(NS_PACKET)) {
						nsDbgPrint("rtRecvSocket failed: %08x\n", ret, 0);
						break;
					}
					NS_PACKET* pac = &(g_nsCtx->packetBuf);
					if (pac->magic != 0x12345678) {
						nsDbgPrint("broken protocol: %08x, %08x\n", pac->magic, pac->seq);
						break;
					}
					nsUpdateDebugStatus();
					nsHandlePacket();
				}
			}
			closesocket(sockfd);
		}
	}
#undef RP_PROCESS_SELECT
#undef RP_PROCESS_SELECT2
}

void nsThreadStart(void) {
	nsMainLoop();
	svc_exitThread();
}

#define STACK_SIZE 0x4000

void nsInitDebug(void) {
	xfunc_out = (void*)nsDbgPutc;
	rtInitLock(&(g_nsConfig->debugBufferLock));
	g_nsConfig->debugBuf = (u8*)(NS_CONFIGURE_ADDR + 0x0900);
	g_nsConfig->debugBufSize = 0xf0;
	g_nsConfig->debugPtr = 0;
	g_nsConfig->debugReady = 1;
}

void nsInit(void) {
	u32 socuSharedBufferSize;
	u32 bufferSize;
	u32 ret, outAddr;
	u32* threadStack;
	u32 handle;
	socuSharedBufferSize = 0x10000;
	bufferSize = socuSharedBufferSize + rtAlignToPageSize(sizeof(NS_CONTEXT)) + STACK_SIZE;
	u32 base = 0x06f00000;



	//showDbg("nsInit", 0, 0);

	/*
	while(1) {
	ret = svc_controlMemory(&outAddr, base, 0, heapSize, 0x3, 3);
	base += 0x1000;
	if (ret == 0) {
	break;
	}
	}*/

	if (g_nsConfig->initMode == NS_INITMODE_FROMRELOAD) {
		outAddr = base;
	}
	else {
		if (g_nsConfig->initMode == NS_INITMODE_FROMHOOK) {
			ret = controlMemoryInSysRegion(&outAddr, base, 0, bufferSize, NS_DEFAULT_MEM_REGION + 3, 3);
		}
		else {
			ret = svc_controlMemory(&outAddr, base, 0, bufferSize, NS_DEFAULT_MEM_REGION + 3, 3);
		}
		if (ret != 0) {
			showDbg("svc_controlMemory failed: %08x", ret, 0);
			return;
		}
	}
	ret = rtCheckRemoteMemoryRegionSafeForWrite(getCurrentProcessHandle(), base, bufferSize);
	if (ret != 0) {
		showDbg("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x", ret, 0);
	}

	if (!srvHandle) {
		initSrv();
	}
	if (g_nsConfig->hSOCU == 0) {
		ret = SOC_Initialize((u32*)outAddr, socuSharedBufferSize);
		if (ret != 0) {
			showDbg("SOC_Initialize failed: %08x", ret, 0);
			return;
		}
		g_nsConfig->hSOCU = SOCU_handle;
	}
	else {
		SOCU_handle = g_nsConfig->hSOCU;
	}


	g_nsCtx = (void*)(outAddr + socuSharedBufferSize);

	memset(g_nsCtx, 0, sizeof(NS_CONTEXT));

	g_nsCtx->listenPort = 8000;
	if (g_nsConfig->initMode == NS_INITMODE_FROMHOOK) {
		g_nsCtx->listenPort = 5000 + getCurrentProcessId();
	}

	rtInitLock(&(g_nsCtx->breakPointTriggerLock));
	rtInitLock(&(g_nsCtx->breakPointStatusLock));

	g_nsConfig->debugBuf = g_nsCtx->debugBuf;
	g_nsConfig->debugPtr = 0;
	g_nsConfig->debugBufSize = DEBUG_BUFFER_SIZE;

	if (g_nsConfig->initMode == NS_INITMODE_FROMHOOK) {
		// hook entry point when attaching process
		nsInitBreakPoint(1, 0x00100000, NS_BPTYPE_CODEONESHOT);
	}
	if ((g_nsConfig->initMode == NS_INITMODE_FROMHOOK) || (g_nsConfig->initMode == NS_INITMODE_FROMRELOAD)) {
		nsMainLoop();
		return;
	}

	threadStack = (u32*)(outAddr + socuSharedBufferSize + rtAlignToPageSize(sizeof(NS_CONTEXT)));

	u32 affinity = 0x10;
	ret = svc_createThread(&handle, (void*)nsThreadStart, 0, &threadStack[(STACK_SIZE / 4) - 10], affinity, 0xFFFFFFFE);
	if (ret != 0) {
		showDbg("svc_createThread failed: %08x", ret, 0);
		return;
	}

}