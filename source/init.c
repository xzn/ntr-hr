#include "global.h"

#include <memory.h>

NTR_CONFIG *ntrConfig;
showDbgFunc_t showDbgFunc;

static int initNSConfig(void) {
	u32 ret;

	nsConfig = (void *)NS_CONFIG_ADDR;
	ret = protectMemory((void*)NS_CONFIG_ADDR, NS_CONFIG_MAX_SIZE);

	if (ret != 0) {
		showDbgRaw("Init nsConfig failed for process %x", getCurrentProcessId(), 0);
		return ret;
	}
	return 0;
}

static void loadParams(void) {
	ntrConfig = &nsConfig->ntrConfig;

	KProcessHandleDataOffset = ntrConfig->KProcessHandleDataOffset;
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

void startupInit(void) {
	if (initNSConfig() != 0)
		goto fail;
	loadParams();
	if (setUpReturn() != 0)
		goto fail;

	return;

fail:
	while (1) {
		disp(100, 0x10000ff);
		svcSleepThread(1000000000);
	}
}
