#include "global.h"

#include <memory.h>

extern int _BootArgs[];

static u32 arm11BinStart;
static u32 arm11BinSize;

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
	NS_CONFIG cfg;
	Handle hProcess = 0;
	int ret = 0;
	ret = svcOpenProcess(&hProcess, ntrConfig->HomeMenuPid);
	if (ret != 0) {
		showDbgRaw("Failed to open home menu process: %d", ret, 0);
		goto final;
	}

	memset(&cfg, 0, sizeof(NS_CONFIG));
	memcpy(&cfg.ntrConfig, ntrConfig, sizeof(NTR_CONFIG));
	ret = nsAttachProcess(hProcess, ntrConfig->HomeMenuInjectAddr, &cfg, arm11BinSize, arm11BinSize);

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
