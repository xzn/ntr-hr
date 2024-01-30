#include "global.h"

#include <memory.h>
#include <string.h>

extern int _BootArgs[];
static NTR_CONFIG *ntrCfg;

static void initBootVars(void) {
	ntrCfg = (void *)_BootArgs[0];
	showDbgFunc = (void *)ntrCfg->showDbgFunc;
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
		disp(100, DBG_CL_USE_DBG);
	}

	ret = nsAttachProcess(hProcess, ntrCfg->HomeMenuInjectAddr, &cfg);

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
