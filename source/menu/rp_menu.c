#include "global.h"

#include <memory.h>

#define RP_THREAD_PRIO_DEFAULT (0x10)
#define RP_DST_PORT_DEFAULT (8001)
#define RP_CORE_COUNT_DEFAULT RP_CORE_COUNT_MAX

#define RP_QUALITY_MIN (10)
#define RP_QUALITY_MAX (100)

// 0.5 MBps or 4 Mbps
#define RP_QOS_MIN (1 * 1024 * 1024 / 2)
// 2.5 MBps or 20 Mbps
#define RP_QOS_MAX (5 * 1024 * 1024 / 2)

#define RP_PORT_MIN (1024)
#define RP_PORT_MAX (65535)

#define RP_THREAD_PRIO_MIN (0x10)
#define RP_THREAD_PRIO_MAX (0x3f)

#define RP_CORE_COUNT_MIN (1)
#define RP_CORE_COUNT_MAX (3)

static u32 remotePlayStarted;
static RP_CONFIG rpConfigInMenu;

static int rpUpdateParamsFromMenu(RP_CONFIG *) {
	// TODO
	return 0;
}

static void rpClampParamsInMenu(RP_CONFIG *config) {
	if (!((config->quality >= RP_QUALITY_MIN) && (config->quality <= RP_QUALITY_MAX))) {
		nsDbgPrint("Out-of-range quality for remote play, limiting to between %d and %d\n", RP_QUALITY_MIN, RP_QUALITY_MAX);
		config->quality = CLAMP(config->quality, RP_QUALITY_MIN, RP_QUALITY_MAX);
	}

	config->qos = CLAMP(config->qos, RP_QOS_MIN, RP_QOS_MAX);

	config->dstAddr = 0; /* always update from nwm callback */

	if (config->dstPort == 0) {
		config->dstPort = rpConfigInMenu.dstPort;
		if (config->dstPort == 0) {
			config->dstPort = RP_DST_PORT_DEFAULT;
		}
	}
	config->dstPort = CLAMP(config->dstPort, RP_PORT_MIN, RP_PORT_MAX);

	if (config->threadPriority == 0) {
		config->threadPriority = rpConfigInMenu.threadPriority;
		if (config->threadPriority == 0) {
			config->threadPriority = RP_THREAD_PRIO_DEFAULT;
		}
	}
	config->threadPriority = CLAMP(config->threadPriority, RP_THREAD_PRIO_MIN, RP_THREAD_PRIO_MAX);

	if (config->coreCount == 0) {
		config->coreCount = rpConfigInMenu.coreCount;
		if (config->coreCount == 0) {
			config->coreCount = RP_CORE_COUNT_DEFAULT;
		}
	}
	config->coreCount = CLAMP(config->coreCount, RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX);
}

static u32 rpGetNwmRemotePC(NS_CONFIG *cfg, Handle hProcess) {
	int isFirmwareSupported = 0;
	u32 remotePC;
	s32 ret;

#define RP_NWM_HDR_SIZE (16)
	u8 desiredHeader[RP_NWM_HDR_SIZE] = { 0x04, 0x00, 0x2D, 0xE5, 0x4F, 0x00, 0x00, 0xEF, 0x00, 0x20, 0x9D, 0xE5, 0x00, 0x10, 0x82, 0xE5 };
	u8 buf[RP_NWM_HDR_SIZE] = { 0 };

	{
		remotePC = 0x001231d0;
		ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, RP_NWM_HDR_SIZE);
		if (ret != 0) {
			nsDbgPrint("Read nwm memory at %08x failed: %08x\n", remotePC, ret);
		} if (memcmp(buf, desiredHeader, RP_NWM_HDR_SIZE) == 0) {
			isFirmwareSupported = 1;
			cfg->startupInfo[11] = 0x120464; // nwmvalparamhook
			cfg->startupInfo[12] = 0x00120DC8 + 1; // nwmSendPacket
		}
	}

	if (!isFirmwareSupported) {
		remotePC = 0x123394;
		ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, RP_NWM_HDR_SIZE);
		if (ret != 0) {
			nsDbgPrint("Read nwm memory at %08x failed: %08x\n", remotePC, ret);
		} if (memcmp(buf, desiredHeader, RP_NWM_HDR_SIZE) == 0) {
			isFirmwareSupported = 1;
			cfg->startupInfo[11] = 0x120630; // nwmvalparamhook
			cfg->startupInfo[12] = 0x00120f94 + 1; // nwmSendPacket
		}
	}

	if (isFirmwareSupported)
		return remotePC;
	return 0;
}

int rpStartupFromMenu(RP_CONFIG *config) {
	if (!ntrConfig->isNew3DS) {
		showDbg("Remote Play is available on New 3DS only.", 0, 0);
		return -1;
	}

	rpClampParamsInMenu(config);

	if (ATSR(remotePlayStarted)) {
		nsDbgPrint("Remote play already started, updating params.\n");
		return rpUpdateParamsFromMenu(config);
	}

	s32 ret = 0;
	ret = loadPayloadBin(NTR_BIN_NWM);
	if (ret != 0) {
		showMsg("Loading nwm payload failed.");
		goto final;
	}

	Handle hProcess;
	u32 pid = 0x1a; // nwm process
	ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("Open nwm process failed: %08x\n", ret);
		hProcess = 0;
		goto final;
	}

	NS_CONFIG cfg = { 0 };

	u32 remotePC = rpGetNwmRemotePC(&cfg, hProcess);

	if (!remotePC)
		goto final;

	cfg.rpConfig = rpConfigInMenu = *config;
	cfg.ntrConfig = *ntrConfig;
	cfg.ntrConfig.ex.nsUseDbg |= nsDbgNext();

	ret = nsAttachProcess(hProcess, remotePC, &cfg);

final:
	unloadPayloadBin();

	if (hProcess)
		svcCloseHandle(hProcess);

	if (ret != 0) {
		showDbg("Starting remote play failed: %d. Retry maybe...", ret, 0);
		ACR(remotePlayStarted);
	} else {
		setCpuClockLock(3);
	}
	return ret;
}
