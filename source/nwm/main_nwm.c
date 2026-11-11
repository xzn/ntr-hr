#include "global.h"

#include "3ds/srv.h"
#include "3ds/ipc.h"
#include "3ds/result.h"
#include "3ds/services/ptmsysm.h"
#include "3ds/allocator/mappable.h"
#include "3ds/os.h"

#include "poll.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>

sendPacketTypedef nwmSendPacket;
static RT_HOOK nwmValParamHook;

static Handle nwmReadyEvent;

extern char *fake_heap_start;
extern char *fake_heap_end;
void __system_initSyscalls(void);
Result __sync_init(void);
void mainThread(void *) {
	__system_initSyscalls();
	int success = false;
	s32 ret;
	ret = __sync_init();
	if (ret != 0) {
		nsDbgPrint("sync init failed: %08"PRIx32"\n", ret);
		goto final;
	}

	if (ntrConfig->isNew3DS) {
		fake_heap_start = (void *)plgRequestMemory(NWM_HEAP_SIZE);
		if (!fake_heap_start) {
			goto final;
		}
		fake_heap_end = fake_heap_start + NWM_HEAP_SIZE;
		mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	}

	ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		goto final;
	}

	ret = nsStartup();
	success = true;

final:
	if (nwmReadyEvent) {
		ret = svcSignalEvent(nwmReadyEvent);
		if (ret != 0) {
			showDbg("nwm payload init sync error: %08"PRIx32"\n", ret);
		}
	}

	if (success) {
		if (ret != 0) {
			disp(100, DBG_CL_USE_DBG_FAIL);
		} else {
			disp(100, DBG_CL_USE_DBG);
		}
		disp(100, DBG_CL_INFO);
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

static RT_HOOK nwmRecvNotificationHook;
static Handle *nwmSrvHandle = (Handle *)0x001547fc;

void nwmPause(bool shutdown);
void nwmUnpause();

static Result nwmSrvReceiveNotification(u32 *notificationIdOut) {
	Result rc = 0;
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0xB, 0, 0); // 0xB0000

	rc = svcSendSyncRequest(*nwmSrvHandle);
	rc = R_SUCCEEDED(rc) ? (Result)cmdbuf[1] : rc;
	if (notificationIdOut)
		*notificationIdOut = R_SUCCEEDED(rc) ? cmdbuf[2] : 0;

	return rc;
}

static Result nwmRecvNotificationCallback(u32* notificationIdOut) {
	Result res = nwmSrvReceiveNotification(notificationIdOut);
	if (res != 0)
		return res;

	// showDbg("notification %"PRIx32" received", *notificationIdOut);
	switch (*notificationIdOut) {
		case PTMNOTIFID_FULLY_WAKING_UP:
			nwmUnpause();
			break;
		case PTMNOTIFID_SLEEP_ALLOWED:
			nwmPause(false);
			break;
		case PTMNOTIFID_SHUTDOWN:
			nwmPause(true);
			return -1;
		default:
			break;
	}
	return res;
}

typedef Result (*srvSubscribeTypedef)(u32 notificationId);

static void nwmNotificationHook(void) {
	srvSubscribeTypedef nwmSrvSubscribe;
	{
		u8 desiredHeader[] = {
			0x1c, 0xb5, 0x04, 0x46, 0x1f, 0xf0, 0x0e, 0xee,
		};
		u32 remotePC = 0x0010455c;
		u8 buf[sizeof(desiredHeader)] = { 0 };

		s32 ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, CUR_PROCESS_HANDLE, (void *)remotePC, sizeof(desiredHeader));
		if (ret != 0) {
			nsDbgPrint("Read NWM memory at %08"PRIx32" failed: %08"PRIx32"\n", remotePC, ret);
			goto final;
		}

		if (memcmp(buf, desiredHeader, sizeof(desiredHeader)) != 0) {
			nsDbgPrint("Unexpected NWM memory content\n");
			goto final;
		}

		nwmSrvSubscribe = (srvSubscribeTypedef)(remotePC + 1);
	}

	u8 desiredHeader[] = {
		0x1c, 0xb5, 0x04, 0x46, 0x22, 0xf0, 0xf2, 0xee,
	};
	u32 remotePC = 0x00101394;
	u8 buf[sizeof(desiredHeader)] = { 0 };

	s32 ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, CUR_PROCESS_HANDLE, (void *)remotePC, sizeof(desiredHeader));
	if (ret != 0) {
		nsDbgPrint("Read NWM memory at %08"PRIx32" failed: %08"PRIx32"\n", remotePC, ret);
		goto final;
	}

	if (memcmp(buf, desiredHeader, sizeof(desiredHeader)) != 0) {
		nsDbgPrint("Unexpected NWM memory content\n");
		goto final;
	}

	u32 notifications[] = {
		PTMNOTIFID_SHUTDOWN,
	};
	for (unsigned i = 0; i < sizeof(notifications) / sizeof(*notifications); ++i) {
		ret = nwmSrvSubscribe(notifications[i]);
		if (ret != 0) {
			nsDbgPrint("NWM %"PRIx32" notification subscribe failed: %08"PRIx32"\n", notifications[i], ret);
		}
	}

	rtInitHookThumb(&nwmRecvNotificationHook, remotePC, (u32)nwmRecvNotificationCallback);
	rtEnableHook(&nwmRecvNotificationHook);

final:
	return;
}

void mainPre(void) {
	if (svcCreateEvent(&nwmReadyEvent, RESET_ONESHOT) != 0) {
		nwmReadyEvent = 0;
		disp(100, DBG_CL_MSG);
	}
	nwmSendPacket = (sendPacketTypedef)nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, nsConfig->startupInfo[11], (u32)nwmValParamCallback);
	rtEnableHook(&nwmValParamHook);

	nwmNotificationHook();
}

void _ReturnToUser(void);
int setUpReturn(void) {
	u32 *buf = (void *)_ReturnToUser;
	buf[0] = 0xe3b00000; // return 0;
	buf[1] = 0xe12fff1e;

	return rtFlushInstructionCache((void *)_ReturnToUser, 8);
}

u32 __apt_appid;
u32 __system_runflags;

u8 nsHasPoll2 = 1;
static s32 nwm_recv_sock = -1;

void nsPoll2End() {
	if (nwm_recv_sock >= 0) {
		closesocket(nwm_recv_sock);
		nwm_recv_sock = -1;
	}
}

int nsPoll2(int s) {
	if (nwm_recv_sock < 0) {
		nwm_recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (nwm_recv_sock < 0) {
			showDbg("nwm socket failed: %08"PRIx32, (u32)errno);
			return -1;
		}

		struct sockaddr_in sai = {0};
		sai.sin_family = AF_INET;
		sai.sin_port = htons(RP_SRC_PORT);
		sai.sin_addr.s_addr = htonl(INADDR_ANY);

		int ret = bind(nwm_recv_sock, (struct sockaddr *)&sai, sizeof(sai));
		if (ret < 0) {
			showDbg("nwm_recv_sock bind failed: %08"PRIx32, (u32)errno);
			nsPoll2End();
			return -1;
		}

		s32 tmp = fcntl(nwm_recv_sock, F_GETFL);
		fcntl(nwm_recv_sock, F_SETFL, tmp | O_NONBLOCK);
	}

	struct pollfd pi[2];
	int nready;

	pi[0].fd = s;
	pi[1].fd = nwm_recv_sock;
	pi[1].events = pi[0].events = POLLIN;
	pi[1].revents = pi[0].revents = 0;
	nready = poll2(pi, 2, 1000);
	if (nready == 0)
		return 0;
	else if (nready < 0) {
		nsDbgPrint("socket poll failed: %08"PRIx32, (u32)errno);
		svcSleepThread(100000000);
		return -1;
	}
	if (pi[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
		nsDbgPrint("socket poll error: revents %04x\n", pi[1].revents);
		nsPoll2End();
		svcSleepThread(100000000);
		return -1;
	}
	if (pi[1].revents & (POLLIN)) {
		if (nsControlRecv(nwm_recv_sock) < 0) {
			// nsDbgPrint("nsControlRecv failed\n");
			nsPoll2End();
			return -1;
		}
	}
	if (pi[0].revents & (POLLERR | POLLNVAL)) {
		nsDbgPrint("socket poll error: revents %04x\n", pi[0].revents);
		return 1;
	}
	if (pi[0].revents & (POLLIN | POLLHUP))
		return 1;

	return 0;
}
