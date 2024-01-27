#include "global.h"

#include <memory.h>

extern int _BootArgs[];

static void initBootVars(void) {
	ntrConfig = (void *)_BootArgs[0];
	showDbgFunc = (void *)ntrConfig->showDbgFunc;
	arm11BinStart = ntrConfig->arm11BinStart;
	arm11BinSize = ntrConfig->arm11BinSize;
	KProcessHandleDataOffset = ntrConfig->KProcessHandleDataOffset;
}

static void doKernelHax(void) {
	showMsgRaw("Doing kernel hax...");
	kDoKernelHax();
	showMsgRaw("Kernel hax done.");

	disp(100, 0x1ff0000);
}

static int injectToHomeMenu(void) {
	NS_CONFIG cfg = { 0 };
	Handle hProcess = 0;
	int ret = 0;
	ret = svcOpenProcess(&hProcess, ntrConfig->HomeMenuPid);
	if (ret != 0) {
		showDbgRaw("Failed to open home menu process: %d", ret, 0);
		goto final;
	}

	memcpy(&cfg.ntrConfig, ntrConfig, offsetof(NTR_CONFIG, ex));
	cfg.ntrConfig.ex.nsUseDbg = nsDbgNext();
	if (cfg.ntrConfig.ex.nsUseDbg) {
		disp(100, 0x17f7f7f);
	}

	ret = nsAttachProcess(hProcess, ntrConfig->HomeMenuInjectAddr, &cfg);

	svcCloseHandle(hProcess);

	if (ret != 0) {
		showDbgRaw("Attach to home menu process failed: %d", ret, 0);
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
