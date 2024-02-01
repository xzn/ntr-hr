#include "global.h"

#include "3ds/srv.h"

typedef u32 (*sendPacketTypedef)(u8 *, u32);
static sendPacketTypedef nwmSendPacket;
static RT_HOOK nwmValParamHook;

void mainThread(void *) {
	s32 ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		goto final;
	}

	// TODO
	if (ntrConfig->ex.nsUseDbg) {
		ret = nsStartup();
		if (ret != 0) {
			disp(100, DBG_CL_USE_DBG_FAIL);
		} else {
			disp(100, DBG_CL_USE_DBG);
		}
	}
	disp(100, DBG_CL_INFO);

final:
	svcExitThread();
}

static int nwmValParamCallback(u8 *, int) {
	return 0;
}

void mainPost(void) {
	nwmSendPacket = (sendPacketTypedef)nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, nsConfig->startupInfo[11], (u32)nwmValParamCallback);
	rtEnableHook(&nwmValParamHook);
}
