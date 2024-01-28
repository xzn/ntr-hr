#include "global.h"

#include <memory.h>

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

	disp(100, 0x1ff0000);
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
		disp(100, 0x17f7f7f);
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
		disp(100, 0x10000ff);

	return 0;
}

int showMsgVerbose(const char *msg, const char *, int, const char *) {
	return showMsgDbgFunc(msg);
}

int showMsgRaw(const char *msg) {
	return showMsgDbgFunc(msg);
}

void nsDbgPrintRaw(const char *, ...) {}
