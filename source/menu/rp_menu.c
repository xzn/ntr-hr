#include "constants.h"
#include "global.h"

#include "3ds/ipc.h"
#include "3ds/services/hid.h"
#include "main.h"
#include "rp.h"
#include "sys/socket.h"
#include "netinet/in.h"
#include "arpa/inet.h"

#include <memory.h>
#include <errno.h>

static u32 rpStarted;

enum {
	REMOTE_PLAY_ADVMENU_CORE_COUNT,
	REMOTE_PLAY_ADVMENU_THREAD_PRIORITY,
	REMOTE_PLAY_ADVMENU_SEPARATE_SCREEN_CONFIG,

	REMOTE_PLAY_ADVMENU_COUNT,
};

enum {
	REMOTE_PLAY_ADVMENU_SCREEN_CHROMASS,
	REMOTE_PLAY_ADVMENU_SCREEN_DOWNSAMPLE,
	REMOTE_PLAY_ADVMENU_SCREEN_DITHER,
	REMOTE_PLAY_ADVMENU_SCREEN_FPS_LIMIT,
	REMOTE_PLAY_ADVMENU_SCREEN_SKIP_DUP_FRAME,

	REMOTE_PLAY_ADVMENU_SCREEN_COUNT,
};

enum {
	REMOTE_PLAY_MENU_COMP_FMT_PROT,
	REMOTE_PLAY_MENU_QUALITY,
	REMOTE_PLAY_MENU_PRIORITY_SCREEN,
	REMOTE_PLAY_MENU_PRIORITY_FACTOR,
	REMOTE_PLAY_MENU_QOS,
	REMOTE_PLAY_MENU_VIEWER_IP,
	REMOTE_PLAY_MENU_VIEWER_PORT,
	REMOTE_PLAY_MENU_ADV,

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
		showMsg("Cannot open socket: %d", errno);
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

	u8 data[DATA_HDR_SIZE] = {0};

	u32 controlCount = 10;
	s32 ret;
	Handle hProcess;
	u32 pid = RP_NWM_PROCESS; // nwm process
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

		svcSleepThread(150000000);
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

		svcSleepThread(50000000);
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

enum CompressionProtocol {
	CompressionProtocolUDP,
	CompressionProtocolReliableStream,
	CompressionProtocolRSDelta,
	CompressionProtocolCount,
};

enum CompressionFormat {
	CompressionFormatJPEG,
	CompressionFormatLossless,
	CompressionFormatCount,
};

static void getCompFmtProtFromFlag(int flag, enum CompressionFormat *fmt, int *lossless_data, enum CompressionProtocol *prot) {
	if (fmt) {
		if (flag & RP_CONFIG_FLAG_LOSSLESS) {
			*fmt = CompressionFormatLossless;
		} else {
			*fmt = CompressionFormatJPEG;
		}
	}

	if (lossless_data)
		*lossless_data = (flag & RP_CONFIG_FLAG_LOSSLESS_DATA) >> RP_CONFIG_FLAG_LOSSLESS_DATA_SHIFT;

	if (prot) {
		if (flag & RP_CONFIG_FLAG_RELIABLE_STREAM) {
			if (flag & RP_CONFIG_FLAG_RELIABLE_STREAM_DELTA) {
				*prot = CompressionProtocolRSDelta;
			} else {
				*prot = CompressionProtocolReliableStream;
			}
		} else {
			*prot = CompressionProtocolUDP;
		}
	}
}

static void updateFlagFromCompFmtProt(int *flag, enum CompressionFormat *fmt, int *lossless_data, enum CompressionProtocol *prot) {
	if (!flag)
		return;

	if (fmt) {
		switch (*fmt) {
			default:
			case CompressionFormatJPEG:
				*flag &= ~RP_CONFIG_FLAG_LOSSLESS;
				break;

			case CompressionFormatLossless:
				*flag |= RP_CONFIG_FLAG_LOSSLESS;
				break;
		}
	}

	if (lossless_data) {
		*flag &= ~RP_CONFIG_FLAG_LOSSLESS_DATA;
		*flag |= ((*lossless_data) & RP_CONFIG_FLAG_LOSSLESS_DATA_MASK) << RP_CONFIG_FLAG_LOSSLESS_DATA_SHIFT;
	}

	if (prot) {
		switch (*prot) {
			default:
			case CompressionProtocolUDP:
				*flag &= ~RP_CONFIG_FLAG_RELIABLE_STREAM;
				*flag &= ~RP_CONFIG_FLAG_RELIABLE_STREAM_DELTA;
				break;

			case CompressionProtocolReliableStream:
				*flag |= RP_CONFIG_FLAG_RELIABLE_STREAM;
				*flag &= ~RP_CONFIG_FLAG_RELIABLE_STREAM_DELTA;
				break;

			case CompressionProtocolRSDelta:
				*flag |= RP_CONFIG_FLAG_RELIABLE_STREAM;
				*flag |= RP_CONFIG_FLAG_RELIABLE_STREAM_DELTA;
				break;
		}
	}
}

static const char *getCompFmtProtName(enum CompressionFormat fmt, enum CompressionProtocol prot) {
	switch (fmt) {
		default:
		case CompressionFormatJPEG:
			switch (prot) {
				default:
				case CompressionProtocolUDP:
					return "JPEG UDP";
					break;

				case CompressionProtocolReliableStream:
					return "JPEG RS";
					break;

				case CompressionProtocolRSDelta:
					return "JPEG RS, Delta";
					break;
			}
			break;

		case CompressionFormatLossless:
			switch (prot) {
				default:
				case CompressionProtocolUDP:
					return "Uncompressed UDP";
					break;

				case CompressionProtocolReliableStream:
					return "Lossless RS";
					break;

				case CompressionProtocolRSDelta:
					return "Lossless RS, Delta";
					break;
			}
			break;
	}
}

static const char *getCompFmtProtDesc(enum CompressionFormat fmt, enum CompressionProtocol prot) {
	switch (fmt) {
		default:
		case CompressionFormatJPEG:
			switch (prot) {
				default:
				case CompressionProtocolUDP:
					return "Compatibility and low latency.\nMay drop frames.";
					break;

				case CompressionProtocolReliableStream:
					return "Avoid dropping frames.\nNeed NTRViewer-HR.";
					break;

				case CompressionProtocolRSDelta:
					return "JPEG delta encoding.\nNeed NTRViewer-HR.";
					break;
			}
			break;

		case CompressionFormatLossless:
			switch (prot) {
				default:
				case CompressionProtocolUDP:
					return "Uncompressed low latency.\nNeed NTRViewer-HR.";
					break;

				case CompressionProtocolReliableStream:
					return "Lossless, avoid dropping frames.\nNeed NTRViewer-HR.";
					break;

				case CompressionProtocolRSDelta:
					return "Lossless delta encoding.\nNeed NTRViewer-HR.";
					break;
			}
			break;
	}
}

#define CompProtCount (ntrConfig->isNew3DS ? CompressionProtocolCount : 1)

static int getOptionFromCompFmtProt(enum CompressionFormat fmt, enum CompressionProtocol prot) {
	return fmt * CompProtCount + prot;
}

static void getCompFmtProtFromOption(int option, enum CompressionFormat *fmt, enum CompressionProtocol *prot) {
	*prot = option % CompProtCount;
	*fmt = option / CompProtCount;
}

#define CompFmtProtMin (0)
#define CompFmtProtMax (CompProtCount * CompressionFormatCount - 1)

static const char *getChromaSSName(int i) {
	switch (i) {
		default:
		case RP_CHROMASS_420:
			return "On";
		case RP_CHROMASS_422:
			return "Half";
		case RP_CHROMASS_444:
			return "Off";
	}
}

static const char *getChromaSSDesc(int i) {
	switch (i) {
		default:
		case RP_CHROMASS_420:
			return "On: YUV420 (Default) (Fastest)";
		case RP_CHROMASS_422:
			return "Half: YUV422";
		case RP_CHROMASS_444:
			return "Off: YUV444 (No chroma subsampling)\nBest quality.";
	}
}

static const char *getDownsampleName(int i) {
	switch (i) {
		default:
		case RP_DOWNSAMPLE_NONE:
			return "None";
		case RP_DOWNSAMPLE_CHECKER:
			return "Checkerboard";
		case RP_DOWNSAMPLE_EVEN_ODD:
			return "Even/Odd";
		case RP_DOWNSAMPLE_QUARTER:
			return "1/2x1/2";
	}
}

static const char *getDownsampleDesc(int i) {
	switch (i) {
		default:
		case RP_DOWNSAMPLE_NONE:
			return "Choose downsample method to improve\nframerate at the cost of quality.";
		case RP_DOWNSAMPLE_CHECKER:
			return "Checkerboard: Alternate checkerboard\npattern every other frame.\nNeed NTRViewer-HR.";
		case RP_DOWNSAMPLE_EVEN_ODD:
			return "Even/Odd: Alternate even/odd row\nevery other frame.\nNeed NTRViewer-HR.";
		case RP_DOWNSAMPLE_QUARTER:
			return "1/2x1/2: Quarter resoluion.\nNeed NTRViewer-HR.";
	}
}

static const char *getDitherName(int i) {
	return i ? "On" : "Off";
}

static const char *getDitherDesc() {
	return "Lossless reduced color bias dither.\nIgnored when downsampling.";
}

static const char *getFpsLimitName(int i) {
	switch (i) {
		default:
		case RP_FPS_LIMIT_NONE:
			return "60";
		case RP_FPS_LIMIT_1:
			return "1";
		case RP_FPS_LIMIT_2:
			return "2";
		case RP_FPS_LIMIT_3:
			return "3";
		case RP_FPS_LIMIT_4:
			return "4";
		case RP_FPS_LIMIT_5:
			return "5";
		case RP_FPS_LIMIT_6:
			return "6";
		case RP_FPS_LIMIT_10:
			return "10";
		case RP_FPS_LIMIT_12:
			return "12";
		case RP_FPS_LIMIT_15:
			return "15";
		case RP_FPS_LIMIT_20:
			return "20";
		case RP_FPS_LIMIT_30:
			return "30";
	}
}

static const char *getSkipDupFrameName(int i) {
	return i ? "On" : "Off";
}

static const char *getSkipDupFrameDesc() {
	return "Whether to skip duplicate frames.";
}

static int getOtherScreenIndex(int screen_index) {
	switch (screen_index) {
		case RP_SCREEN_TOP:
			return RP_SCREEN_BOT;
		case RP_SCREEN_BOT:
			return RP_SCREEN_TOP;
	}
	return RP_SCREEN_TOP;
}

static const char *getScreenName(int screen_index) {
	switch (screen_index) {
		case RP_SCREEN_TOP:
			return "  Top Screen:";
		case RP_SCREEN_BOT:
			return "  Bottom Screen:";
	}
	return "";
}

const u8 changed_color[3] = { 63, 255, 63 };

// REMOTE_PLAY_ADVMENU_COUNT + (screen name + REMOTE_PLAY_ADVMENU_SCREEN_COUNT) * both screens + REMOTE_PLAY_ADVMENU_BACK
#define REMOTE_PLAY_ADVMENU_COUNT_MAX (REMOTE_PLAY_ADVMENU_COUNT + (1 + REMOTE_PLAY_ADVMENU_SCREEN_COUNT) * RP_SCREEN_COUNT + 1)

static int remotePlayAdvMenu(RP_CONFIG *config) {
	s32 select = 0;

	while (1) {
		u32 count = REMOTE_PLAY_ADVMENU_COUNT;
		const char *captions[REMOTE_PLAY_ADVMENU_COUNT_MAX] = { 0 };
		const char *descs[REMOTE_PLAY_ADVMENU_COUNT_MAX] = { 0 };
		const u8 (*colors[REMOTE_PLAY_ADVMENU_COUNT_MAX])[3] = { 0 };

		char coreCountCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(coreCountCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Number of Encoding Cores: %"PRId32, config->coreCount);
		captions[REMOTE_PLAY_ADVMENU_CORE_COUNT] = coreCountCaption;
		colors[REMOTE_PLAY_ADVMENU_CORE_COUNT] = config->coreCount != rpConfig->coreCount ? &changed_color : 0;

		char encoderPriorityCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(encoderPriorityCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Encoder Priority: %"PRId32, config->threadPriority);
		captions[REMOTE_PLAY_ADVMENU_THREAD_PRIORITY] = encoderPriorityCaption;
		descs[REMOTE_PLAY_ADVMENU_THREAD_PRIORITY] = "Higher value means lower priority.\nLower priority means less game/audio\nstutter possibly.";
		colors[REMOTE_PLAY_ADVMENU_THREAD_PRIORITY] = config->threadPriority != rpConfig->threadPriority ? &changed_color : 0;

		char separateScreenConfigCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(separateScreenConfigCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Per Screen Options: %s", config->separateScreenConfig ? "On" : "Off");
		captions[REMOTE_PLAY_ADVMENU_SEPARATE_SCREEN_CONFIG] = separateScreenConfigCaption;
		colors[REMOTE_PLAY_ADVMENU_SEPARATE_SCREEN_CONFIG] = config->separateScreenConfig != rpConfig->separateScreenConfig ? &changed_color : 0;

		char chromaSSCaption[RP_SCREEN_COUNT][LOCAL_OPT_TEXT_BUF_SIZE] = { 0 };
		char downsampleCaption[RP_SCREEN_COUNT][LOCAL_OPT_TEXT_BUF_SIZE] = { 0 };
		char noDitherCaption[RP_SCREEN_COUNT][LOCAL_OPT_TEXT_BUF_SIZE] = { 0 };
		char fpsLimitCaption[RP_SCREEN_COUNT][LOCAL_OPT_TEXT_BUF_SIZE] = { 0 };
		char noSkipFrameCaption[RP_SCREEN_COUNT][LOCAL_OPT_TEXT_BUF_SIZE] = { 0 };

		int screen_config_count = 1;
		if (config->separateScreenConfig) {
			screen_config_count = RP_SCREEN_COUNT;
		}
		for (int i = 0; i < screen_config_count; ++i) {
			if (screen_config_count > 1) {
				captions[count] = getScreenName(i);
				++count;
			}

			xsnprintf(chromaSSCaption[i], LOCAL_OPT_TEXT_BUF_SIZE, "Chroma Subsampling: %s", getChromaSSName(config->screens[i].chromaSs));
			captions[count + REMOTE_PLAY_ADVMENU_SCREEN_CHROMASS] = chromaSSCaption[i];
			descs[count + REMOTE_PLAY_ADVMENU_SCREEN_CHROMASS] = getChromaSSDesc(config->screens[i].chromaSs);
			colors[count + REMOTE_PLAY_ADVMENU_SCREEN_CHROMASS] = config->screens[i].chromaSs != rpConfig->screens[i].chromaSs ? &changed_color : 0;

			xsnprintf(downsampleCaption[i], LOCAL_OPT_TEXT_BUF_SIZE, "Downsample: %s", getDownsampleName(config->screens[i].downsample));
			captions[count + REMOTE_PLAY_ADVMENU_SCREEN_DOWNSAMPLE] = downsampleCaption[i];
			descs[count + REMOTE_PLAY_ADVMENU_SCREEN_DOWNSAMPLE] = getDownsampleDesc(config->screens[i].downsample);
			colors[count + REMOTE_PLAY_ADVMENU_SCREEN_DOWNSAMPLE] = config->screens[i].downsample != rpConfig->screens[i].downsample ? &changed_color : 0;

			xsnprintf(noDitherCaption[i], LOCAL_OPT_TEXT_BUF_SIZE, "Dither: %s", getDitherName(config->screens[i].dither));
			captions[count + REMOTE_PLAY_ADVMENU_SCREEN_DITHER] = noDitherCaption[i];
			descs[count + REMOTE_PLAY_ADVMENU_SCREEN_DITHER] = getDitherDesc();
			colors[count + REMOTE_PLAY_ADVMENU_SCREEN_DITHER] = config->screens[i].dither != rpConfig->screens[i].dither ? &changed_color : 0;

			xsnprintf(fpsLimitCaption[i], LOCAL_OPT_TEXT_BUF_SIZE, "Max FPS: %s", getFpsLimitName(config->screens[i].fpsLimit));
			captions[count + REMOTE_PLAY_ADVMENU_SCREEN_FPS_LIMIT] = fpsLimitCaption[i];
			colors[count + REMOTE_PLAY_ADVMENU_SCREEN_FPS_LIMIT] = config->screens[i].fpsLimit != rpConfig->screens[i].fpsLimit ? &changed_color : 0;

			xsnprintf(noSkipFrameCaption[i], LOCAL_OPT_TEXT_BUF_SIZE, "Skip Dup Frames: %s", getSkipDupFrameName(config->screens[i].skipDupFrame));
			captions[count + REMOTE_PLAY_ADVMENU_SCREEN_SKIP_DUP_FRAME] = noSkipFrameCaption[i];
			descs[count + REMOTE_PLAY_ADVMENU_SCREEN_SKIP_DUP_FRAME] = getSkipDupFrameDesc();
			colors[count + REMOTE_PLAY_ADVMENU_SCREEN_SKIP_DUP_FRAME] = config->screens[i].skipDupFrame != rpConfig->screens[i].skipDupFrame ? &changed_color : 0;

			count += REMOTE_PLAY_ADVMENU_SCREEN_COUNT;
		}

		const u32 REMOTE_PLAY_ADVMENU_BACK = count;
		++count;

		captions[REMOTE_PLAY_ADVMENU_BACK] = "Back";

		u32 keys;
		select = showMenuEx3("Remote Play (Advanced Options)", count, captions, descs, select, &keys, colors);

		if (keys == KEY_B) {
			return 0;
		}

		switch (select) {
			case REMOTE_PLAY_ADVMENU_CORE_COUNT: { /* core count */
				int coreCount = config->coreCount;
				if (keys == KEY_X)
					coreCount = rpConfig->coreCount;
				else
					menu_adjust_value_with_key(&coreCount, keys, 1, 1);

				coreCount = CLAMP(coreCount, RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX);

				if (coreCount != (int)config->coreCount) {
					config->coreCount = coreCount;
				}
				break;
			}

			case REMOTE_PLAY_ADVMENU_THREAD_PRIORITY: { /* encoder priority */
				int threadPriority = config->threadPriority;
				if (keys == KEY_X)
					threadPriority = rpConfig->threadPriority;
				else
					menu_adjust_value_with_key(&threadPriority, keys, 5, 10);

				threadPriority = CLAMP(threadPriority, RP_THREAD_PRIO_MIN, RP_THREAD_PRIO_MAX);

				if (threadPriority != (int)config->threadPriority) {
					config->threadPriority = threadPriority;
				}
				break;
			}

			case REMOTE_PLAY_ADVMENU_SEPARATE_SCREEN_CONFIG: {
				int separateScreenConfig = config->separateScreenConfig;
				if (keys == KEY_X)
					separateScreenConfig = rpConfig->separateScreenConfig;
				else
					menu_adjust_value_with_key(&separateScreenConfig, keys, 1, 1);

				separateScreenConfig = CWRAP(separateScreenConfig, false, true);

				if (separateScreenConfig != (int)config->separateScreenConfig) {
					config->separateScreenConfig = separateScreenConfig;
				}
				break;
			}

			default: {
				int screen_select = select - REMOTE_PLAY_ADVMENU_COUNT;
				int screen_config_count = REMOTE_PLAY_ADVMENU_SCREEN_COUNT;
				int screen_count = 1;
				if (config->separateScreenConfig) {
					++screen_config_count;
					screen_count = RP_SCREEN_COUNT;

				}
				int screen_index = screen_select / screen_config_count;
				screen_select %= screen_config_count;

				if (screen_index >= screen_count) {
					if (select == (int)REMOTE_PLAY_ADVMENU_BACK) {
						if (keys == KEY_A) { /* back */
							if (!config->separateScreenConfig) {
								for (int i = 1; i < RP_SCREEN_COUNT; ++i) {
									config->screens[i].chromaSs = CLAMP(config->screens[0].chromaSs, RP_CHROMASS_MIN, RP_CHROMASS_MAX);
									config->screens[i].downsample = CLAMP(config->screens[0].downsample, RP_DOWNSAMPLE_MIN, RP_DOWNSAMPLE_MAX);
									config->screens[i].dither = CLAMP(config->screens[0].dither, false, true);
									config->screens[i].fpsLimit = CLAMP(config->screens[0].fpsLimit, RP_FPS_LIMIT_MIN, RP_FPS_LIMIT_MAX);
									config->screens[i].skipDupFrame = CLAMP(config->screens[0].skipDupFrame, false, true);
								}
							}
							return 0;
						}
					}
				} else {
					if (config->separateScreenConfig) {
						if (screen_select == 0) {
							if (keys & KEY_DOWN) {
								select += 1;
								if (select >= (int)count) {
									select = 0;
								}
							}
							if (keys & KEY_UP) {
								select -= 1;
								if (select < 0) {
									select = count - 1;
								}
							}
							continue;
						} else {
							--screen_select;
						}
					}
					switch (screen_select) {
						case REMOTE_PLAY_ADVMENU_SCREEN_CHROMASS: { /* chroma subsample */
							int chromaSs = config->screens[screen_index].chromaSs;
							if (keys == KEY_X)
								chromaSs = rpConfig->screens[screen_index].chromaSs;
							else
								menu_adjust_value_with_key(&chromaSs, keys, 1, 1);

							chromaSs = CWRAP(chromaSs, RP_CHROMASS_MIN, RP_CHROMASS_MAX);

							if (chromaSs != (int)config->screens[screen_index].chromaSs) {
								config->screens[screen_index].chromaSs = chromaSs;
								if (!config->separateScreenConfig) {
									config->screens[getOtherScreenIndex(screen_index)].chromaSs = chromaSs;
								}
							}
							break;
						}

						case REMOTE_PLAY_ADVMENU_SCREEN_DOWNSAMPLE: { /* downsample */
							int downsample = config->screens[screen_index].downsample;
							if (keys == KEY_X)
								downsample = rpConfig->screens[screen_index].downsample;
							else {
								int ret = menu_adjust_value_with_key(&downsample, keys, 1, 1);
								// unimplemented
								if (downsample == RP_DOWNSAMPLE_CHECKER) {
									if (ret < 0) {
										--downsample;
									} else if (ret > 0) {
										++downsample;
									}
								}
							}

							downsample = CWRAP(downsample, RP_DOWNSAMPLE_MIN, RP_DOWNSAMPLE_MAX);

							if (downsample != (int)config->screens[screen_index].downsample) {
								config->screens[screen_index].downsample = downsample;
								if (!config->separateScreenConfig) {
									config->screens[getOtherScreenIndex(screen_index)].downsample = downsample;
								}
							}
							break;
						}

						case REMOTE_PLAY_ADVMENU_SCREEN_DITHER: { /* dither */
							int dither = config->screens[screen_index].dither;
							if (keys == KEY_X)
								dither = rpConfig->screens[screen_index].dither;
							else
								menu_adjust_value_with_key(&dither, keys, 1, 1);

							dither = CWRAP(dither, false, true);

							if (dither != (int)config->screens[screen_index].dither) {
								config->screens[screen_index].dither = dither;
								if (!config->separateScreenConfig) {
									config->screens[getOtherScreenIndex(screen_index)].dither = dither;
								}
							}
							break;
						}

						case REMOTE_PLAY_ADVMENU_SCREEN_FPS_LIMIT: { /* fps limit */
							int fpsLimit = config->screens[screen_index].fpsLimit;
							if (keys == KEY_X)
								fpsLimit = rpConfig->screens[screen_index].fpsLimit;
							else
								menu_adjust_value_with_key(&fpsLimit, keys, 1, 1);

							fpsLimit = CWRAP(fpsLimit, RP_FPS_LIMIT_MIN, RP_FPS_LIMIT_MAX);

							if (fpsLimit != (int)config->screens[screen_index].fpsLimit) {
								config->screens[screen_index].fpsLimit = fpsLimit;
								if (!config->separateScreenConfig) {
									config->screens[getOtherScreenIndex(screen_index)].fpsLimit = fpsLimit;
								}
							}
							break;
						}

						case REMOTE_PLAY_ADVMENU_SCREEN_SKIP_DUP_FRAME: { /* skip dup frame */
							int skipDupFrame = config->screens[screen_index].skipDupFrame;
							if (keys == KEY_X)
								skipDupFrame = rpConfig->screens[screen_index].skipDupFrame;
							else
								menu_adjust_value_with_key(&skipDupFrame, keys, 1, 1);

							skipDupFrame = CWRAP(skipDupFrame, false, true);

							if (skipDupFrame != (int)config->screens[screen_index].skipDupFrame) {
								config->screens[screen_index].skipDupFrame = skipDupFrame;
								if (!config->separateScreenConfig) {
									config->screens[getOtherScreenIndex(screen_index)].skipDupFrame = skipDupFrame;
								}
							}
							break;
						}
					}
				}
			}
		}
	}
}

static int remotePlayMenuConfirm(void) {
	u32 keys = 0;
	acquireVideo();
	while(1) {
		if (waitKeysOverride & KEY_B)
			break;
		blank();
		print("Remote Play Confirm", 10, 10, 255, 0, 255);
		print("You made changes to remote play\nsettings. Do you wish to apply them?", 10, 44, 255, 0, 0);
		print(plgTranslate("[A] Yes  [B] Cancel  [X] No"), 10, 220, 0, 0, 255);
		updateScreen();
		keys = waitKeys();
		if (keys & (KEY_B | KEY_A | KEY_X)) {
			break;
		}
	}
	releaseVideo();

	return keys & KEY_A ? 1 : keys & KEY_X ? -1 : 0;
}

static int remotePlayApply(RP_CONFIG *config) {
	u32 daddr = config->dstAddr;
	if (daddr == 0) {
		showMsg("IP address cannot be empty.");
		return 0;
	}

	releaseVideo();
	rpStartupFromMenu(config);
	tryInitRemotePlay(daddr);
	acquireVideo();

	return 1;
}

int remotePlayMenu(u32 localaddr) {
	u32 select = 0;
	RP_CONFIG config = *rpConfig;
	u8 *dstAddr4 = (u8 *)&config.dstAddr;

	/* default values */
	if (config.quality == 0) {
		config.mode = 0x0102;
		config.quality = RP_QUALITY_DEFAULT;
		config.qos = RP_QOS_DEFAULT;
		config.dstPort = RP_DST_PORT_DEFAULT;
		config.coreCount = RP_CORE_COUNT_DEFAULT;
		config.threadPriority = RP_THREAD_PRIO_DEFAULT;
	}
	if (config.dstAddr == 0 && localaddr != 0) {
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

		enum CompressionFormat compFmt;
		int losslessData;
		enum CompressionProtocol compProt;
		getCompFmtProtFromFlag(config.dstPort & RP_CONFIG_FLAGS_DATA_MASK, &compFmt, &losslessData, &compProt);

		int rpLosslessData;
		getCompFmtProtFromFlag(rpConfig->dstPort & RP_CONFIG_FLAGS_DATA_MASK, NULL, &rpLosslessData, NULL);

		char compFmtProtCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(compFmtProtCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Fmt/Prot: %s", getCompFmtProtName(compFmt, compProt));

		char priorityScreenCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(priorityScreenCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Priority Screen: %s", (config.mode & 0xff00) == 0 ? "Bottom" : "Top");

		char priorityFactorCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(priorityFactorCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Priority Factor: %"PRId32, config.mode & 0xff);

		char qualityCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		switch (compFmt) {
			default:
			case CompressionFormatJPEG:
				xsnprintf(qualityCaption, LOCAL_OPT_TEXT_BUF_SIZE, "JPEG Quality: %"PRId32, config.quality);
				break;
			case CompressionFormatLossless:
				xsnprintf(qualityCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Color Quality Bias: %d", losslessData);
				break;
		}

		char qosCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(qosCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Bandwidth Limit: %"PRId32" Mbps", config.qos / 1024 / 128);

		char dstAddrCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(dstAddrCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Viewer IP: %d.%d.%d.%d", dstAddr4[0], dstAddr4[1], dstAddr4[2], dstAddr4[3]);

		char dstPortCaption[LOCAL_OPT_TEXT_BUF_SIZE];
		xsnprintf(dstPortCaption, LOCAL_OPT_TEXT_BUF_SIZE, "Viewer Port: %"PRId32, config.dstPort & RP_CONFIG_PORT_MASK);

		const char *captions[REMOTE_PLAY_MENU_COUNT];
		captions[REMOTE_PLAY_MENU_PRIORITY_SCREEN] = priorityScreenCaption;
		captions[REMOTE_PLAY_MENU_PRIORITY_FACTOR] = priorityFactorCaption;
		captions[REMOTE_PLAY_MENU_QUALITY] = qualityCaption;
		captions[REMOTE_PLAY_MENU_QOS] = qosCaption;
		captions[REMOTE_PLAY_MENU_VIEWER_IP] = dstAddrCaption;
		captions[REMOTE_PLAY_MENU_VIEWER_PORT] = dstPortCaption;
		captions[REMOTE_PLAY_MENU_COMP_FMT_PROT] = compFmtProtCaption;
		captions[REMOTE_PLAY_MENU_ADV] = "Advanced Options";
		captions[REMOTE_PLAY_MENU_APPLY] = "Apply";

		const char *descs[REMOTE_PLAY_MENU_COUNT] = { 0 };
		descs[REMOTE_PLAY_MENU_PRIORITY_FACTOR] = "0: Priority screen only.";
		descs[REMOTE_PLAY_MENU_COMP_FMT_PROT] = getCompFmtProtDesc(compFmt, compProt);

		const u8 (*colors[REMOTE_PLAY_MENU_COUNT])[3] = { 0 };
		colors[REMOTE_PLAY_MENU_PRIORITY_SCREEN] = !!(config.mode & 0xff00) != !!(rpConfig->mode & 0xff00) ? &changed_color : 0;
		colors[REMOTE_PLAY_MENU_PRIORITY_FACTOR] = (config.mode & 0xff) != (rpConfig->mode & 0xff) ? &changed_color : 0;
		switch (compFmt) {
			default:
			case CompressionFormatJPEG:
				colors[REMOTE_PLAY_MENU_QUALITY] = config.quality != rpConfig->quality ? &changed_color : 0;
				break;
			case CompressionFormatLossless:
				colors[REMOTE_PLAY_MENU_QUALITY] = losslessData != rpLosslessData ? &changed_color : 0;
				break;
		}
		colors[REMOTE_PLAY_MENU_QOS] = config.qos != rpConfig->qos ? &changed_color : 0;
		colors[REMOTE_PLAY_MENU_VIEWER_IP] = config.dstAddr != rpConfig->dstAddr ? &changed_color : 0;
		colors[REMOTE_PLAY_MENU_VIEWER_PORT] = (config.dstPort & RP_CONFIG_PORT_MASK) != (rpConfig->dstPort & RP_CONFIG_PORT_MASK) ? &changed_color : 0;
		colors[REMOTE_PLAY_MENU_COMP_FMT_PROT] = (config.dstPort & RP_CONFIG_FLAGS) != (rpConfig->dstPort & RP_CONFIG_FLAGS) ? &changed_color : 0;
		colors[REMOTE_PLAY_MENU_ADV] = memcmp(RP_CONFIG_ADV_CFG(&config), RP_CONFIG_ADV_CFG(rpConfig), RP_CONFIG_ADV_CFG_SIZE) != 0 ? &changed_color : 0;

		u32 keys;
		select = showMenuEx3(titleCurrent, REMOTE_PLAY_MENU_COUNT, captions, descs, select, &keys, colors);

		if (keys == KEY_B) {
			if (waitKeysOverride & KEY_B) {
				return 0;
			}

			if (memcmp(rpConfig, &config, sizeof(config)) != 0) {
				int confirm = remotePlayMenuConfirm();
				if (confirm < 0) {
					return 0;
				}
				if (confirm > 0) {
					if (!remotePlayApply(&config)) {
						continue;
					}
					return 1;
				}
				continue;
			}

			return 0;
		}

		switch (select) {
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
				int dstPort = config.dstPort;
				int dstFlag = dstPort & RP_CONFIG_FLAGS_DATA_MASK;
				enum CompressionFormat fmt;
				int losslessData;
				getCompFmtProtFromFlag(dstFlag, &fmt, &losslessData, NULL);

				switch (fmt) {
					default:
					case CompressionFormatJPEG: {
						int quality = config.quality;
						if (keys == KEY_X)
							quality = rpConfig->quality;
						else
							menu_adjust_value_with_key(&quality, keys, 5, 10);

						quality = CLAMP(quality, RP_QUALITY_MIN, RP_QUALITY_MAX);

						if (quality != (int)config.quality) {
							config.quality = quality;
						}
					}
					break;
					case CompressionFormatLossless: {
						if (keys == KEY_X) {
							int dstPort = rpConfig->dstPort;
							int dstFlag = dstPort & RP_CONFIG_FLAGS_DATA_MASK;
							getCompFmtProtFromFlag(dstFlag, NULL, &losslessData, NULL);
						} else
							menu_adjust_value_with_key(&losslessData, keys, 1, 1);

						losslessData = CLAMP(losslessData, RP_COLOR_BIAS_MIN, RP_COLOR_BIAS_MAX);
						dstFlag &= ~RP_CONFIG_FLAG_LOSSLESS_DATA;
						dstFlag |= losslessData << RP_CONFIG_FLAG_LOSSLESS_DATA_SHIFT;

						dstPort &= RP_CONFIG_PORT_MASK;
						dstPort |= dstFlag;

						if (dstPort != (int)config.dstPort) {
							config.dstPort = dstPort;
						}
					}
					break;
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
				int dstFlag = dstPort & RP_CONFIG_FLAGS_DATA_MASK;
				dstPort &= RP_CONFIG_PORT_MASK;
				if (keys == KEY_X) {
					dstPort = rpConfig->dstPort;
					dstPort &= RP_CONFIG_PORT_MASK;
				} else {
					menu_adjust_value_with_key(&dstPort, keys, 10, 100);
				}

				dstPort = CLAMP(dstPort, RP_PORT_MIN, RP_PORT_MAX) | dstFlag;

				if (dstPort != (int)config.dstPort) {
					config.dstPort = dstPort;
				}
				break;
			}

			case REMOTE_PLAY_MENU_COMP_FMT_PROT: { /* reliable stream */
				int dstPort = config.dstPort;
				int dstFlag = dstPort & RP_CONFIG_FLAGS_DATA_MASK;
				dstPort &= RP_CONFIG_PORT_MASK;
				if (keys == KEY_X) {
					int dstPort = rpConfig->dstPort;
					dstFlag &= ~RP_CONFIG_FLAGS;
					dstFlag |= dstPort & RP_CONFIG_FLAGS;
				} else {
					enum CompressionFormat fmt;
					enum CompressionProtocol prot;
					getCompFmtProtFromFlag(dstFlag, &fmt, NULL, &prot);
					int option = getOptionFromCompFmtProt(fmt, prot);
					menu_adjust_value_with_key(&option, keys, 1, 1);
					option = CWRAP(option, CompFmtProtMin, CompFmtProtMax);
					getCompFmtProtFromOption(option, &fmt, &prot);
					updateFlagFromCompFmtProt(&dstFlag, &fmt, NULL, &prot);
				}

				dstPort |= dstFlag;

				if (dstPort != (int)config.dstPort) {
					config.dstPort = dstPort;
				}
				break;
			}

			case REMOTE_PLAY_MENU_ADV: if (keys == KEY_A) { /* advanced */
				remotePlayAdvMenu(&config);
			}
				break;

			case REMOTE_PLAY_MENU_APPLY: if (keys == KEY_A) { /* apply */
				if (!remotePlayApply(&config))
					break;

				return 1;
			}
				break;
		}
	}

	return 0;
}

static int rpUpdatingParams;
static int rpUpdateParamsFromMenu(RP_CONFIG *config) {
	if (ATSR(&rpUpdatingParams))
		return -1;

	s32 ret = 0;
	if (ntrConfig->isNew3DS) {
		Handle hClient = rpGetPortHandle();
		if (!hClient) {
			ret = -1;
			goto final;
		}

		u32* cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(SVC_NWM_CMD_PARAMS_UPDATE, sizeof(RP_CONFIG) / sizeof(u32), 0);
		*(RP_CONFIG *)&cmdbuf[1] = *config;

		ret = svcSendSyncRequest(hClient);
		if (ret != 0) {
			nsDbgPrint("Send port request failed: %08"PRIx32"\n", ret);
			ret = -1;
			goto final;
		}
	} else {
		Handle hProcess = 0;
		u32 pid = RP_NWM_PROCESS; // nwm process
		ret = svcOpenProcess(&hProcess, pid);
		if (ret != 0) {
			showDbg("Open nwm process failed: %08"PRIx32, ret);
			hProcess = 0;
			goto final;
		}

		ret = copyRemoteMemory(hProcess, rpConfig, CUR_PROCESS_HANDLE, config, sizeof(RP_CONFIG));
		if (ret != 0) {
			nsDbgPrint("Update remote play config failed: %08"PRIx32"\n", ret);
		}

		if (hProcess)
			svcCloseHandle(hProcess);

		if (ret)
			goto final;
	}
	*rpConfig = *config;

final:
	ACR(&rpUpdatingParams);
	return ret;
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

	int dstFlag = config->dstPort & RP_CONFIG_FLAGS_DATA_MASK;
	enum CompressionFormat compFmt;
	int losslessData;
	enum CompressionProtocol compProt;
	getCompFmtProtFromFlag(dstFlag, &compFmt, &losslessData, &compProt);
	compProt = CLAMP(compProt, 0, CompProtCount - 1);
	compFmt = CLAMP(compFmt, 0, CompressionFormatCount - 1);
	losslessData = CLAMP(losslessData, RP_COLOR_BIAS_MIN, RP_COLOR_BIAS_MAX);
	if (ALC(&nfcPatched) && compProt != CompressionProtocolUDP) {
		showMsg("NFC patch is applied, Reliable Stream\nwill be disabled for compatibility.");
		compProt = CompressionProtocolUDP;
	}
	updateFlagFromCompFmtProt(&dstFlag, &compFmt, &losslessData, &compProt);
	config->dstPort = CLAMP(config->dstPort & RP_CONFIG_PORT_MASK, RP_PORT_MIN, RP_PORT_MAX) | dstFlag;

	if (config->threadPriority == 0) {
		config->threadPriority = RP_THREAD_PRIO_DEFAULT;
	}
	config->threadPriority = CLAMP(config->threadPriority, RP_THREAD_PRIO_MIN, RP_THREAD_PRIO_MAX);

	if (config->coreCount == 0) {
		config->coreCount = RP_CORE_COUNT_DEFAULT;
	}
	config->coreCount = CLAMP(config->coreCount, RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX);

	config->separateScreenConfig = CLAMP(config->separateScreenConfig, false, true);

	for (int i = 0; i < RP_SCREEN_COUNT; ++i) {
		config->screens[i].chromaSs = CLAMP(config->screens[i].chromaSs, RP_CHROMASS_MIN, RP_CHROMASS_MAX);
		config->screens[i].downsample = CLAMP(config->screens[i].downsample, RP_DOWNSAMPLE_MIN, RP_DOWNSAMPLE_MAX);
		config->screens[i].dither = CLAMP(config->screens[i].dither, false, true);
		config->screens[i].fpsLimit = CLAMP(config->screens[i].fpsLimit, RP_FPS_LIMIT_MIN, RP_FPS_LIMIT_MAX);
		config->screens[i].skipDupFrame = CLAMP(config->screens[i].skipDupFrame, false, true);
	}

	if (!config->separateScreenConfig) {
		for (int i = 1; i < RP_SCREEN_COUNT; ++i) {
			config->screens[i].chromaSs = CLAMP(config->screens[0].chromaSs, RP_CHROMASS_MIN, RP_CHROMASS_MAX);
			config->screens[i].downsample = CLAMP(config->screens[0].downsample, RP_DOWNSAMPLE_MIN, RP_DOWNSAMPLE_MAX);
			config->screens[i].dither = CLAMP(config->screens[0].dither, false, true);
			config->screens[i].fpsLimit = CLAMP(config->screens[0].fpsLimit, RP_FPS_LIMIT_MIN, RP_FPS_LIMIT_MAX);
			config->screens[i].skipDupFrame = CLAMP(config->screens[0].skipDupFrame, false, true);
		}
	}

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
		} else if (memcmp(buf, desiredHeader, RP_NWM_HDR_SIZE) == 0) {
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
		} else if (memcmp(buf, desiredHeader, RP_NWM_HDR_SIZE) == 0) {
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
	rpClampParamsInMenu(config);

	if (ATSR(&rpStarted)) {
		if (ntrConfig->ex.nsUseDbg)
			nsDbgPrint("Remote play already started, updating params.\n");
		return rpUpdateParamsFromMenu(config);
	}

	Handle hProcess = 0;
	u32 pid = RP_NWM_PROCESS; // nwm process
	s32 ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		showDbg("Open nwm process failed: %08"PRIx32, ret);
		hProcess = 0;
		goto final;
	}

	NS_CONFIG cfg = { 0 };

	u32 remotePC = rpGetNwmRemotePC(&cfg, hProcess);

	if (!remotePC) {
		showDbg("Unable to get nwm remote pc.");
		ret = -1;
		goto final;
	}

	cfg.rpConfig = *rpConfig = *config;
	cfg.ntrConfig = *ntrConfig;
	cfg.ntrConfig.ex.nsUseDbg |= nsDbgNext();

	ret = nsAttachProcess(hProcess, remotePC, &cfg, 1, 1);

final:
	if (ret != 0) {
		showDbg("Starting remote play failed: %08"PRIx32". Retry maybe...", ret);
		ACR(&rpStarted);
	} else {
		clearPayloadBin();
		nsContinueProcess(hProcess);

		if (ntrConfig->isNew3DS) {
			nsDbgPrint("Locking CPU clock to 804 MHz and L2 cache to enabled...\n");
			setCpuClockLock(3, 1);
		}
	}

	if (hProcess)
		svcCloseHandle(hProcess);
	return ret;
}

void rpCheckReliableStreamForNFC(void) {
	if (ALC(&rpStarted)) {
		RP_CONFIG config = *rpConfig;
		int dstFlag = config.dstPort & RP_CONFIG_FLAGS_DATA_MASK;
		enum CompressionProtocol compProt;
		getCompFmtProtFromFlag(dstFlag, NULL, NULL, &compProt);
		if (compProt != CompressionProtocolUDP) {
			showMsg("Reliable Stream will be disabled for\ncompatibility.");
			compProt = CompressionProtocolUDP;
			updateFlagFromCompFmtProt(&dstFlag, NULL, NULL, &compProt);
		}
		config.dstPort = (config.dstPort & RP_CONFIG_PORT_MASK) | dstFlag;
		rpStartupFromMenu(&config);
	}
}
