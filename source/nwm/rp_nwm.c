#include "global.h"

#include "3ds/os.h"
#include "3ds/allocator/mappable.h"
#include "3ds/services/gspgpu.h"

#include <memory.h>
#include <arpa/inet.h>

static Handle hRPThreadMain;

#define rp_nwm_hdr_size (0x2a + 8)
#define rp_data_hdr_size (4)
static u8 rpNwmHdr[rp_nwm_hdr_size];

Result __sync_init(void);
void __system_initSyscalls(void);
static void rpThreadStart(void *) {
	s32 res;
	res = __sync_init();
	if (res != 0) {
		nsDbgPrint("sync init failed: %08"PRIx32"\n", res);
		return;
	}
	__system_initSyscalls();
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	res = gspInit(1);
	if (res != 0) {
		nsDbgPrint("gsp init failed: %08"PRIx32"\n", res);
		return;
	}
	nsDbgPrint("gsp initted\n");

	svcExitThread();
}

static int rpInited;

static void printNwMHdr(void) {
	u8 *buf = rpNwmHdr;
	nsDbgPrint("nwm hdr: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x .. .. %02x %02x %02x %02x %02x %02x %02x %02x\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
		buf[14], buf[15], buf[16], buf[17], buf[18], buf[19], buf[20], buf[21]
	);
}

static int rpDstAddrChanged;
static void updateDstAddr(u32 dstAddr) {
	rpConfig->dstAddr = dstAddr;

	Handle hProcess;
	u32 pid = ntrConfig->HomeMenuPid;
	s32 ret = svcOpenProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08"PRIx32"\n", ret);
		goto final;
	}

	ret = copyRemoteMemory(
		hProcess,
		&rpConfig->dstAddr,
		CUR_PROCESS_HANDLE,
		&rpConfig->dstAddr,
		sizeof(rpConfig->dstAddr));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory failed: %08"PRIx32"\n", ret);
	}

	svcCloseHandle(hProcess);

final:
	rpDstAddrChanged = 0;
}

static u32 rpSrcAddr;
void rpStartup(u8 *buf) {
	u8 protocol = buf[0x17 + 0x8];
	u16 src_port = *(u16 *)(&buf[0x22 + 0x8]);
	u16 dst_port = *(u16 *)(&buf[0x22 + 0xa]);

	int tcp_hit = (protocol == 0x6 && src_port == htons(NS_MENU_LISTEN_PORT));
	int udp_hit = (protocol == 0x11 && src_port == htons(NWM_INIT_SRC_PORT) && dst_port == htons(NWM_INIT_DST_PORT));

	if (tcp_hit || udp_hit) {
		u32 saddr = *(u32 *)&buf[0x1a + 0x8];
		u32 daddr = *(u32 *)&buf[0x1e + 0x8];

		if (rpInited) {
			int needUpdate = 0;

			if ((tcp_hit && rpDstAddrChanged) || udp_hit) {
				if (rpConfig->dstAddr != daddr) {
					updateDstAddr(daddr);

					u8 *daddr4 = (u8 *)&daddr;
					nsDbgPrint("Remote play updated dst IP: %d.%d.%d.%d\n",
						(int)daddr4[0], (int)daddr4[1], (int)daddr4[2], (int)daddr4[3]
					);

					needUpdate = 1;
				}
			}
			if (rpSrcAddr != saddr) {
				rpSrcAddr = saddr;

				u8 *saddr4 = (u8 *)&saddr;
				nsDbgPrint("Remote play updated src IP: %d.%d.%d.%d\n",
					(int)saddr4[0], (int)saddr4[1], (int)saddr4[2], (int)saddr4[3]
				);

				needUpdate = 1;
			}

			if (needUpdate) {
				memcpy(rpNwmHdr, buf, 0x22 + 8);
				printNwMHdr();
			}

			return;
		}

		rpInited = 1;
		u8 *saddr4 = (u8 *)&saddr;
		u8 *daddr4 = (u8 *)&daddr;
		nsDbgPrint("Remote play src IP: %d.%d.%d.%d, dst IP: %d.%d.%d.%d\n",
			(int)saddr4[0], (int)saddr4[1], (int)saddr4[2], (int)saddr4[3],
			(int)daddr4[0], (int)daddr4[1], (int)daddr4[2], (int)daddr4[3]
		);

		memcpy(rpNwmHdr, buf, 0x22 + 8);
		printNwMHdr();
		updateDstAddr(daddr);
		rpSrcAddr = saddr;

		u32 *threadStack = (u32 *)plgRequestMemory(RP_THREAD_STACK_SIZE);
		s32 ret = svcCreateThread(&hRPThreadMain, rpThreadStart, 0, &threadStack[(RP_THREAD_STACK_SIZE / 4) - 10], RP_THREAD_PRIO_DEFAULT, 2);
		if (ret != 0) {
			nsDbgPrint("Create remote play thread failed: %08"PRIx32"\n", ret);
		}
	}
}
