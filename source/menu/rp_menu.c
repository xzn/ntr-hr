#include "global.h"

#include "3ds/ipc.h"
#include "3ds/services/hid.h"
#include "sys/socket.h"
#include "netinet/in.h"
#include "arpa/inet.h"

#include <memory.h>

#define RP_QUALITY_DEFAULT (75)
#define RP_QUALITY_MIN (10)
#define RP_QUALITY_MAX (100)

// 2.0 MBps or 16 Mbps
#define RP_QOS_DEFAULT (2 * 1024 * 1024)
// 0.5 MBps or 4 Mbps
#define RP_QOS_MIN (1 * 1024 * 1024 / 2)
// 2.5 MBps or 20 Mbps
#define RP_QOS_MAX (5 * 1024 * 1024 / 2)

#define RP_PORT_MIN (1024)
#define RP_PORT_MAX (65535)

#define RP_THREAD_PRIO_MIN (0x10)
#define RP_THREAD_PRIO_MAX (0x3f)

#define RP_CORE_COUNT_DEFAULT RP_CORE_COUNT_MAX

static u32 rpStarted;

enum {
	REMOTE_PLAY_MENU_CORE_COUNT,
	REMOTE_PLAY_MENU_THREAD_PRIORITY,
	REMOTE_PLAY_MENU_PRIORITY_SCREEN,
	REMOTE_PLAY_MENU_PRIORITY_FACTOR,
	REMOTE_PLAY_MENU_QUALITY,
	REMOTE_PLAY_MENU_QOS,
	REMOTE_PLAY_MENU_VIEWER_IP,
	REMOTE_PLAY_MENU_VIEWER_PORT,

	REMOTE_PLAY_MENU_APPLY,

	REMOTE_PLAY_MENU_COUNT,
};

static int menu_adjust_value_with_key(int *val, u32 keys, int step_1, int step_2) {
	int ret = 0;
	if (keys == KEY_DLEFT)
		ret = -1;
	else if (keys == KEY_DRIGHT)
		ret = 1;
	else if (keys == KEY_Y)
		ret = -step_1;
	else if (keys == KEY_A)
		ret = step_1;
	else if (keys == KEY_L)
		ret = -step_2;
	else if (keys == KEY_R)
		ret = step_2;

	if (ret)
		*val += ret;
	return ret;
}

static void ipAddrMenu(u32 *addr) {
	int posDigit = 0;
	int posOctet = 0;
	u32 localaddr = *addr;
	u32 keys = 0;
	while (1) {
		blank();

		char ipText[LOCAL_OPT_TEXT_BUF_SIZE];
		u8 *addr4 = (u8 *)&localaddr;

		xsprintf(ipText, "Viewer IP: %03d.%03d.%03d.%03d", addr4[0], addr4[1], addr4[2], addr4[3]);
		print(ipText, 34, 30, 0, 0, 0);

		int posCaret = posOctet * 4 + posDigit;
		print("^", 34 + (11 + posCaret) * 8, 42, 0, 0, 0);

		updateScreen();
		while((keys = waitKeys()) == 0);

		if (keys == KEY_DRIGHT) {
			++posDigit;
			if (posDigit >= 3) {
				posDigit = 0;
				++posOctet;
				if (posOctet >= 4) {
					posOctet = 0;
				}
			}
		}
		else if (keys == KEY_DLEFT) {
			--posDigit;
			if (posDigit < 0) {
				posDigit = 2;
				--posOctet;
				if (posOctet < 0) {
					posOctet = 3;
				}
			}
		}
		else if (keys == KEY_DUP) {
			int addr1 = addr4[posOctet];
			addr1 += posDigit == 0 ? 100 : posDigit == 1 ? 10 : 1;
			if (addr1 > 255) addr1 = 255;
			addr4[posOctet] = addr1;
		}
		else if (keys == KEY_DDOWN) {
			int addr1 = addr4[posOctet];
			addr1 -= posDigit == 0 ? 100 : posDigit == 1 ? 10 : 1;
			if (addr1 < 0) addr1 = 0;
			addr4[posOctet] = addr1;
		}
		else if (keys == KEY_A) {
			*addr = localaddr;
			return;
		}
		else if (keys == KEY_B) {
			return;
		}
	}
}

static void tryInitRemotePlay(u32 dstAddr) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		showMsg("Cannot open socket.");
		return;
	}

	struct sockaddr_in saddr, caddr;
	memset(&saddr, 0, sizeof(struct sockaddr_in));
	memset(&caddr, 0, sizeof(struct sockaddr_in));

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = dstAddr;
	saddr.sin_port = htons(NWM_INIT_DST_PORT);

	caddr.sin_family = AF_INET;
	caddr.sin_addr.s_addr = htonl(INADDR_ANY);
	caddr.sin_port = htons(NWM_INIT_SRC_PORT);

	if (bind(fd, (struct sockaddr *)&caddr, sizeof(struct sockaddr_in)) < 0) {
		showMsg("Socket bind failed.");
		goto socket_exit;
	}

	u8 data[1] = {0};

	u32 controlCount = 10;
	s32 ret;
	Handle hProcess;
	u32 pid = 0x1a; // nwm process
	ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		showDbg("Open remote play process failed: %08"PRIx32, ret);
		goto socket_exit;
	}

	while (1) {
		if (sendto(fd, data, sizeof(data), 0, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) < 0) {
			if (!--controlCount) {
				showMsg("Remote play send failed.");
				goto nwm_exit;
			}
		}

		svcSleepThread(500000000);
		ret = rtCheckRemoteMemory(hProcess, NS_CONFIG_ADDR, 0x1000, MEMPERM_READ);
		if (ret != 0) {
			if (!--controlCount) {
				showMsg("Remote play init timeout.");
				goto nwm_exit;
			}
		} else {
			break;
		}
	}

	while (1) {
		if (sendto(fd, data, sizeof(data), 0, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) < 0) {
			if (!--controlCount) {
				showMsg("Remote play send failed.");
				goto nwm_exit;
			}
		}

		svcSleepThread(500000000);
		if (ALR(&rpConfig->dstAddr) != dstAddr) {
			if (!--controlCount) {
				showMsg("Remote play update timeout.");
				goto nwm_exit;
			}
		} else {
			break;
		}
	}

nwm_exit:
	svcCloseHandle(hProcess);
socket_exit:
	closesocket(fd);
}

int remotePlayMenu(u32 localaddr) {
	if (!ntrConfig->isNew3DS) {
		showDbg("Remote Play is available on New 3DS only.");
		return 0;
	}

	u32 select = 0;
	RP_CONFIG config = *rpConfig;
	u8 *dstAddr4 = (u8 *)&config.dstAddr;

	/* default values */
	if (config.quality == 0) {
		config.mode = 0x0103;
		config.quality = RP_QUALITY_DEFAULT;
		config.qos = RP_QOS_DEFAULT;
		config.dstPort = RP_DST_PORT_DEFAULT;
		config.coreCount = RP_CORE_COUNT_DEFAULT;
		config.threadPriority = RP_THREAD_PRIO_DEFAULT;
	}
	if (config.dstAddr == 0) {
		config.dstAddr = localaddr;
		dstAddr4[3] = 1;
	}
	*rpConfig = config;

	char title[LOCAL_OPT_TEXT_BUF_SIZE], titleNotStarted[LOCAL_OPT_TEXT_BUF_SIZE];
	u8 *localaddr4 = (u8 *)&localaddr;
	xsnprintf(title, LOCAL_OPT_TEXT_BUF_SIZE, "Remote Play: %d.%d.%d.%d", localaddr4[0], localaddr4[1], localaddr4[2], localaddr4[3]);
	xsnprintf(titleNotStarted, LOCAL_OPT_TEXT_BUF_SIZE, "Remote Play (Standby): %d.%d.%d.%d", localaddr4[0], localaddr4[1], localaddr4[2], localaddr4[3]);

	while (1) {
		u8 started = ALR(&rpStarted);
		char *titleCurrent = title;
		if (!started) {
			titleCurrent = titleNotStarted;
		}

		char coreCountCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(coreCountCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Number of Encoding Cores: %"PRId32, config.coreCount);

		char encoderPriorityCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(encoderPriorityCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Encoder Priority: %"PRId32, config.threadPriority);

		char priorityScreenCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(priorityScreenCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Priority Screen: %s", (config.mode & 0xff00) == 0 ? "Bottom" : "Top");

		char priorityFactorCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(priorityFactorCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Priority Factor: %"PRId32, config.mode & 0xff);

		char qualityCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(qualityCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Quality: %"PRId32, config.quality);

		char qosCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		u32 qosMB = config.qos / 1024 / 1024;
		u32 qosKB = config.qos / 1024 % 1024 * 125 / 128;
		xsnprintf(qosCaption, LOCAL_OPT_TEXT_BUF_SIZE, "QoS: %"PRId32".%"PRId32" MBps", qosMB, qosKB);

		char dstAddrCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(dstAddrCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Viewer IP: %d.%d.%d.%d", dstAddr4[0], dstAddr4[1], dstAddr4[2], dstAddr4[3]);

		char dstPortCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(dstPortCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Port: %"PRId32, config.dstPort);

		const char *captions[REMOTE_PLAY_MENU_COUNT];
		captions[REMOTE_PLAY_MENU_CORE_COUNT] = coreCountCaption,
		captions[REMOTE_PLAY_MENU_THREAD_PRIORITY] = encoderPriorityCaption;
		captions[REMOTE_PLAY_MENU_PRIORITY_SCREEN] = priorityScreenCaption;
		captions[REMOTE_PLAY_MENU_PRIORITY_FACTOR] = priorityFactorCaption;
		captions[REMOTE_PLAY_MENU_QUALITY] = qualityCaption;
		captions[REMOTE_PLAY_MENU_QOS] = qosCaption;
		captions[REMOTE_PLAY_MENU_VIEWER_IP] = dstAddrCaption;
		captions[REMOTE_PLAY_MENU_VIEWER_PORT] = dstPortCaption;
		captions[REMOTE_PLAY_MENU_APPLY] = "Apply";

		const char *descs[REMOTE_PLAY_MENU_COUNT] = { 0 };
		descs[REMOTE_PLAY_MENU_THREAD_PRIORITY] = "Higher value means lower priority.\nLower priority means less game/audio\nstutter possibly.";

		u32 keys;
		select = showMenuEx2(titleCurrent, REMOTE_PLAY_MENU_COUNT, captions, descs, select, &keys);

		if (keys == KEY_B) {
			return 0;
		}

		switch (select) {
			case REMOTE_PLAY_MENU_CORE_COUNT: { /* core count */
				int coreCount = config.coreCount;
				if (keys == KEY_X)
					coreCount = rpConfig->coreCount;
				else
					menu_adjust_value_with_key(&coreCount, keys, 1, 1);

				coreCount = CLAMP(coreCount, RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX);

				if (coreCount != (int)config.coreCount) {
					config.coreCount = coreCount;
				}
				break;
			}

			case REMOTE_PLAY_MENU_THREAD_PRIORITY: { /* encoder priority */
				int threadPriority = config.threadPriority;
				if (keys == KEY_X)
					threadPriority = rpConfig->threadPriority;
				else
					menu_adjust_value_with_key(&threadPriority, keys, 5, 10);

				threadPriority = CLAMP(threadPriority, RP_THREAD_PRIO_MIN, RP_THREAD_PRIO_MAX);

				if (threadPriority != (int)config.threadPriority) {
					config.threadPriority = threadPriority;
				}
				break;
			}

			case REMOTE_PLAY_MENU_PRIORITY_SCREEN: { /* screen priority */
				u32 mode = !!(config.mode & 0xff00);
				if (keys == KEY_X)
					mode = !!(rpConfig->mode & 0xff00);
				else {
					int dummy = 0;
					dummy = menu_adjust_value_with_key(&dummy, keys, 1, 1);
					if (dummy) {
						mode = !mode;
					}
				}

				if (mode != !!(config.mode & 0xff00)) {
					u32 factor = config.mode & 0xff;
					config.mode = (mode << 8) | factor;
				}
				break;
			}

			case REMOTE_PLAY_MENU_PRIORITY_FACTOR: { /* priority factor */
				int factor = config.mode & 0xff;
				if (keys == KEY_X)
					factor = rpConfig->mode & 0xff;
				else
					menu_adjust_value_with_key(&factor, keys, 5, 10);

				factor = CLAMP(factor, 0, UINT8_MAX);

				if (factor != (int)(config.mode & 0xff)) {
					u32 mode = config.mode & 0xff00;
					config.mode = mode | factor;
				}
				break;
			}

			case REMOTE_PLAY_MENU_QUALITY: { /* quality */
				int quality = config.quality;
				if (keys == KEY_X)
					quality = rpConfig->quality;
				else
					menu_adjust_value_with_key(&quality, keys, 5, 10);

				quality = CLAMP(quality, RP_QUALITY_MIN, RP_QUALITY_MAX);

				if (quality != (int)config.quality) {
					config.quality = quality;
				}
				break;
			}

			case REMOTE_PLAY_MENU_QOS: { /* qos */
#define QOS_FACTOR (128 * 1024)
				int qos = config.qos;
				int qos_remainder = qos % QOS_FACTOR;
				qos /= QOS_FACTOR;

				if (keys == KEY_X)
					qos = rpConfig->qos;
				else {
					int ret = menu_adjust_value_with_key(&qos, keys, 4, 8);
					if (ret < 0 && qos_remainder > 0) {
						++qos;
					}
					qos = CLAMP(qos, RP_QOS_MIN / QOS_FACTOR, RP_QOS_MAX / QOS_FACTOR);
					qos *= QOS_FACTOR;
				}

				if (qos != (int)config.qos) {
					config.qos = qos;
				}
				break;
			}

			case REMOTE_PLAY_MENU_VIEWER_IP: { /* dst addr */
				u32 dstAddr = config.dstAddr;
				if (keys == KEY_X)
					dstAddr = rpConfig->dstAddr;
				else{
					int dummy = 0;
					dummy = menu_adjust_value_with_key(&dummy, keys, 1, 1);
					if (dummy) {
						ipAddrMenu(&dstAddr);
					}
				}

				if (dstAddr != config.dstAddr) {
					config.dstAddr = dstAddr;
				}
				break;
			}

			case REMOTE_PLAY_MENU_VIEWER_PORT: { /* dst port */
				int dstPort = config.dstPort;
				if (keys == KEY_X)
					dstPort = rpConfig->dstPort;
				else
					menu_adjust_value_with_key(&dstPort, keys, 10, 100);

				dstPort = CLAMP(dstPort, RP_PORT_MIN, RP_PORT_MAX);

				if (dstPort != (int)config.dstPort) {
					config.dstPort = dstPort;
				}
				break;
			}

			case REMOTE_PLAY_MENU_APPLY: if (keys == KEY_A) { /* apply */
				releaseVideo();

				int updateDstAddr = !started || rpConfig->dstAddr != config.dstAddr;
				u32 daddrUpdated = config.dstAddr;
				rpStartupFromMenu(&config);

				if (updateDstAddr) {
					tryInitRemotePlay(daddrUpdated);
				}

				acquireVideo();

				return 1;
			}
				break;
		}
	}

	return 0;
}

static int rpUpdateParamsFromMenu(RP_CONFIG *config) {
	Handle hClient = rpGetPortHandle();
	if (!hClient)
		return -1;

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(SVC_NWM_CMD_PARAMS_UPDATE, sizeof(RP_CONFIG) / sizeof(u32), 0);
	*(RP_CONFIG *)&cmdbuf[1] = *config;

	s32 ret = svcSendSyncRequest(hClient);
	if (ret != 0) {
		nsDbgPrint("Send port request failed: %08"PRIx32"\n", ret);
		return -1;
	}
	*rpConfig = *config;
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
		config->dstPort = rpConfig->dstPort;
		if (config->dstPort == 0) {
			config->dstPort = RP_DST_PORT_DEFAULT;
		}
	}
	config->dstPort = CLAMP(config->dstPort, RP_PORT_MIN, RP_PORT_MAX);

	if (config->threadPriority == 0) {
		config->threadPriority = rpConfig->threadPriority;
		if (config->threadPriority == 0) {
			config->threadPriority = RP_THREAD_PRIO_DEFAULT;
		}
	}
	config->threadPriority = CLAMP(config->threadPriority, RP_THREAD_PRIO_MIN, RP_THREAD_PRIO_MAX);

	if (config->coreCount == 0) {
		config->coreCount = rpConfig->coreCount;
		if (config->coreCount == 0) {
			config->coreCount = RP_CORE_COUNT_DEFAULT;
		}
	}
	config->coreCount = CLAMP(config->coreCount, RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX);

	config->gamePid = plgLoader->gamePluginPid;
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
			nsDbgPrint("Read nwm memory at %08"PRIx32" failed: %08"PRIx32"\n", remotePC, ret);
		} if (memcmp(buf, desiredHeader, RP_NWM_HDR_SIZE) == 0) {
			isFirmwareSupported = 1;
			remotePC = cfg->startupInfo[11] = 0x120464; // nwmvalparamhook
			cfg->startupInfo[12] = 0x00120DC8 + 1; // nwmSendPacket
		}
	}

	if (!isFirmwareSupported) {
		remotePC = 0x123394;
		ret = copyRemoteMemory(CUR_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, RP_NWM_HDR_SIZE);
		if (ret != 0) {
			nsDbgPrint("Read nwm memory at %08"PRIx32" failed: %08"PRIx32"\n", remotePC, ret);
		} if (memcmp(buf, desiredHeader, RP_NWM_HDR_SIZE) == 0) {
			isFirmwareSupported = 1;
			remotePC = cfg->startupInfo[11] = 0x120630; // nwmvalparamhook
			cfg->startupInfo[12] = 0x00120f94 + 1; // nwmSendPacket
		}
	}

	if (isFirmwareSupported)
		return remotePC;
	return 0;
}

int rpStartupFromMenu(RP_CONFIG *config) {
	if (!ntrConfig->isNew3DS) {
		showDbg("Remote Play is available on New 3DS only.");
		return -1;
	}

	rpClampParamsInMenu(config);

	if (ATSR(&rpStarted)) {
		nsDbgPrint("Remote play already started, updating params.\n");
		return rpUpdateParamsFromMenu(config);
	}

	Handle hProcess;
	u32 pid = 0x1a; // nwm process
	s32 ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		showDbg("Open nwm process failed: %08"PRIx32, ret);
		hProcess = 0;
		goto final;
	}

	NS_CONFIG cfg = { 0 };

	u32 remotePC = rpGetNwmRemotePC(&cfg, hProcess);

	if (!remotePC) {
		ret = -1;
		goto final;
	}

	cfg.rpConfig = *rpConfig = *config;
	cfg.ntrConfig = *ntrConfig;
	cfg.ntrConfig.ex.nsUseDbg |= nsDbgNext();

	ret = nsAttachProcess(hProcess, remotePC, &cfg, 1);

final:
	if (hProcess)
		svcCloseHandle(hProcess);

	if (ret != 0) {
		showDbg("Starting remote play failed: %08"PRIx32". Retry maybe...", ret);
		ACR(&rpStarted);
	} else {
		setCpuClockLock(3);
	}
	return ret;
}
