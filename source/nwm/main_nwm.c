#include "global.h"

#include "3ds/srv.h"
#include "3ds/allocator/mappable.h"
#include "3ds/os.h"

sendPacketTypedef nwmSendPacket;
static RT_HOOK nwmValParamHook;

static Handle nwmReadyEvent;

void __system_initSyscalls(void);
void nsThreadInit() {
	__system_initSyscalls();
}

extern char *fake_heap_start;
extern char *fake_heap_end;
Result __sync_init(void);
void mainThread(void *) {
	s32 ret;
	ret = __sync_init();
	if (ret != 0) {
		nsDbgPrint("sync init failed: %08"PRIx32"\n", ret);
		goto final;
	}
	fake_heap_start = (void *)plgRequestMemory(NWM_HEAP_SIZE);
	if (!fake_heap_start) {
		goto final;
	}
	fake_heap_end = fake_heap_start + NWM_HEAP_SIZE;
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

	ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		goto final;
	}

	ret = nsStartup();
	if (ret != 0) {
		disp(100, DBG_CL_USE_DBG_FAIL);
	} else {
		disp(100, DBG_CL_USE_DBG);
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

void _ReturnToUser(void);
int setUpReturn(void) {
	u32 *buf = (void *)_ReturnToUser;
	buf[0] = 0xe3b00000; // return 0;
	buf[1] = 0xe12fff1e;

	return rtFlushInstructionCache((void *)_ReturnToUser, 8);;
}

u32 __apt_appid;
u32 __system_runflags;
