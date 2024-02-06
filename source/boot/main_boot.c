#include "global.h"

#include "3ds/services/fs.h"

#include <memory.h>
#include <string.h>

extern int _BootArgs[];
static NTR_CONFIG *ntrCfg;
static Handle fsUserHandle;

static void initBootVars(void) {
	ntrCfg = (void *)_BootArgs[0];
	showDbgFunc = (void *)ntrCfg->showDbgFunc;
	fsUserHandle = ntrCfg->fsUserHandle;
	arm11BinStart = ntrCfg->arm11BinStart;
	arm11BinSize = ntrCfg->arm11BinSize;
	loadParams(ntrCfg);
}

static void doKernelHax(void) {
	showMsgRaw("Doing kernel hax...");
	kDoKernelHax(ntrCfg);
	showMsgRaw("Kernel hax done.");

	disp(100, DBG_CL_INFO);
}

static void dbgDumpCode(u32 base, u32 size, char *fileName) {
	u32 off = 0;
	u8 tmpBuffer[0x1000];
	Handle handle;
	u32 t;

	fsUseSession(fsUserHandle);
	Result res;
	res = FSUSER_OpenFileDirectly(&handle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL), fsMakePath(PATH_ASCII, fileName), FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
	if (res != 0) {
		showMsgRaw("Saving dump failed");
		return;
	}

	while(off < size) {
		kmemcpy(tmpBuffer, (u8 *)base + off, 0x1000);
		FSFILE_Write(handle, &t, off, tmpBuffer, 0x1000, 0);
		off += 0x1000;
	}
	FSFILE_Close(handle);
}

static int injectToHomeMenu(void) {
	NS_CONFIG cfg = { 0 };
	Handle hProcess = 0;
	s32 ret = 0;
	ret = svcOpenProcess(&hProcess, ntrCfg->HomeMenuPid);
	if (ret != 0) {
		showDbgRaw("Failed to open home menu process: %08"PRIx32, ret);
		goto final;
	}

	memcpy(&cfg.ntrConfig, ntrCfg, offsetof(NTR_CONFIG, ex));
	cfg.ntrConfig.ex.nsUseDbg = nsDbgNext();
	if (cfg.ntrConfig.ex.nsUseDbg) {
		dbgDumpCode(0xdff80000, 0x80000, "/axiwram.dmp");
		disp(100, DBG_CL_USE_DBG);
	}

	ret = nsAttachProcess(hProcess, ntrCfg->HomeMenuInjectAddr, &cfg, 0);

	svcCloseHandle(hProcess);

	if (ret != 0) {
		showDbgRaw("Attach to home menu process failed: %08"PRIx32, ret);
		goto final;
	}

final:
	return ret;
}

int main(void) {
	initBootVars();

	doKernelHax();

	if (injectToHomeMenu() != 0)
		disp(100, DBG_CL_FATAL);

	return 0;
}

int showMsgVA(const char *, int , const char *, const char *fmt, va_list va) {
	size_t fmt_len = strlen(fmt);
	char buf[fmt_len + 1];
	memcpy(buf, fmt, sizeof(buf));
	size_t buf_len = fmt_len;
	while (buf_len && buf[--buf_len] == '\n') {
		buf[buf_len] = 0;
	}

	char msg[LOCAL_MSG_BUF_SIZE];
	xvsnprintf(msg, LOCAL_MSG_BUF_SIZE, buf, va);
	return showMsgDbgFunc(msg);
}

void nsDbgPrintRaw(const char *fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	showMsgVA(NULL, 0, NULL, fmt, arp);
	va_end(arp);
}

void nsDbgPrintVerbose(const char *, int, const char *, const char* fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	showMsgVA(NULL, 0, NULL, fmt, arp);
	va_end(arp);
}

int nsCheckPCSafeToWrite(u32 hProcess, u32 remotePC) {
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
		if (remotePC >= pc - 24 && remotePC < pc + 8)
			return -1;
	}

	return 0;
}
