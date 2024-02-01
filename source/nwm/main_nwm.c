#include "global.h"

#include "3ds/srv.h"

sendPacketTypedef nwmSendPacket;
static RT_HOOK nwmValParamHook;

static Handle nwmReadyEvent;

void __system_initSyscalls(void);
void nsThreadInit() {
	__system_initSyscalls();
}

void mainThread(void *) {
	s32 ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		goto final;
	}

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
	if (nwmReadyEvent) {
		ret = svcSignalEvent(nwmReadyEvent);
		if (ret != 0) {
			showDbg("nwm payload init sync error: %08"PRIx32"\n", ret);
		}
	}

	svcExitThread();
}

static u32 nwmReadyDone;

static int nwmValParamCallback(u8 *buf, int) {
	if (!ATSR(&nwmReadyDone)) {
		if (nwmReadyEvent) {
			if (svcWaitSynchronization(nwmReadyEvent, NWM_INIT_READY_TIMEOUT) != 0) {
				disp(100, DBG_CL_MSG);
			}
			svcCloseHandle(nwmReadyEvent);
			nwmReadyEvent = 0;
		}

		rpStartup(buf);

		ACR(&nwmReadyDone);
	}
	return 0;
}

void mainPre(void) {
	if (svcCreateEvent(&nwmReadyEvent, RESET_ONESHOT) != 0) {
		nwmReadyEvent = 0;
		disp(100, DBG_CL_MSG);
	}
	nwmSendPacket = (sendPacketTypedef)nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, nsConfig->startupInfo[11], (u32)nwmValParamCallback);
	rtEnableHook(&nwmValParamHook);
}
