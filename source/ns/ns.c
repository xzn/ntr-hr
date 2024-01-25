#include "global.h"
#include <ctr/SOC.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include "fastlz.h"
#include "jpeglib.h"
/* CSTATE_START from jpegint.h */
#define JPEG_CSTATE_START 100

#include <math.h>

#define SCALEBITS 16
#define ONE_HALF ((u32)1 << (SCALEBITS - 1))
#define FIX(x) ((u32)((x) * (1L << SCALEBITS) + 0.5))

NS_CONTEXT* g_nsCtx = 0;
NS_CONFIG* g_nsConfig;

#define rp_thread_count (3)
// #define rp_nwm_thread_id (0)

#define rp_work_count (2)
/* 2 for number of screens (top/bot) */
#define rp_cinfos_count (rp_work_count * rp_thread_count * 2)

j_compress_ptr cinfos[rp_cinfos_count];
static u32 cinfo_alloc_sizes[rp_cinfos_count];
struct jpeg_compress_struct cinfos_top[rp_work_count][rp_thread_count], cinfos_bot[rp_work_count][rp_thread_count];
struct rp_alloc_stats_check {
	struct rp_alloc_stats qual, comp /* , scan */ ;
} alloc_stats_top[rp_work_count][rp_thread_count], alloc_stats_bot[rp_work_count][rp_thread_count];
struct jpeg_error_mgr jerr;
static int jpeg_rows[rp_work_count];
static int jpeg_rows_last[rp_work_count];
static int jpeg_adjusted_rows[rp_work_count];
static int jpeg_adjusted_rows_last[rp_work_count];
static int jpeg_progress[rp_work_count][rp_thread_count];

static u32 rpMinIntervalBetweenPacketsInTick = 0;
static u32 rpMinIntervalBetweenPacketsInNS = 0;
static u32 rpThreadStackSize = 0x10000;
static u8 rpResetThreads = 0;

#define STACK_SIZE 0x4000
static u32 rpPortGamePid;
static u32 frameQueued[2];

struct rp_handles_t {
	struct rp_work_syn_t {
		Handle sem_end, sem_nwm, sem_send;
		int sem_count;
		u8 sem_set;
	} work[rp_work_count];

	struct rp_thread_syn_t {
		Handle sem_start, sem_work;
	} thread[rp_thread_count];

	Handle nwmEvent;
	Handle portEvent[2];
} *rp_syn;

static Handle hRPThreadMain, hRPThreadAux1;

void*  rpMalloc(j_common_ptr cinfo, u32 size)
{
	void* ret = cinfo->alloc.buf + cinfo->alloc.stats.offset;
	u32 totalSize = size;
	/* min align is 4
	   32 for cache line size */
	if (totalSize % 32 != 0) {
		totalSize += 32 - (totalSize % 32);
	}
	if (cinfo->alloc.stats.remaining < totalSize) {
		u32 alloc_size = cinfo->alloc.stats.offset + cinfo->alloc.stats.remaining;
		nsDbgPrint("bad alloc, size: %d/%d\n", totalSize, alloc_size);
		return 0;
	}
	cinfo->alloc.stats.offset += totalSize;
	cinfo->alloc.stats.remaining -= totalSize;

#if 0
	if (cinfo->alloc.stats.offset > cinfo->alloc.max_offset) {
		cinfo->alloc.max_offset = cinfo->alloc.stats.offset;
		nsDbgPrint("cinfo %08x alloc.max_offset: %d\n", cinfo, cinfo->alloc.max_offset);
	}
#endif

	return ret;
}

void  rpFree(j_common_ptr, void*) {}

// void  rpFree(j_common_ptr cinfo, void* ptr)
// {
// 	nsDbgPrint("free: %08x\n", ptr);
// 	return;
// }


static void nsDbgPutc(char ch) {

	if (g_nsConfig->debugPtr >= g_nsConfig->debugBufSize) {
		return;
	}
	(g_nsConfig->debugBuf)[g_nsConfig->debugPtr] = ch;
	g_nsConfig->debugPtr++;
}

static void nsDbgLn() {
	if (g_nsConfig->debugPtr == 0)
		return;
	if (g_nsConfig->debugPtr >= g_nsConfig->debugBufSize) {
		if ((g_nsConfig->debugBuf)[g_nsConfig->debugBufSize - 1] != '\n') {
			(g_nsConfig->debugBuf)[g_nsConfig->debugBufSize - 1] = '\n';
		}
	} else {
		if ((g_nsConfig->debugBuf)[g_nsConfig->debugPtr - 1] != '\n') {
			(g_nsConfig->debugBuf)[g_nsConfig->debugPtr] = '\n';
			g_nsConfig->debugPtr++;
		}
	}
}

void nsDbgPrintShared(const char* fmt, ...) {
	va_list arp;

	va_start(arp, fmt);
	if (g_nsConfig) {
		if (g_nsConfig->debugReady) {
			rtAcquireLock(&(g_nsConfig->debugBufferLock));
			nsDbgLn();
			xfvprintf(nsDbgPutc, fmt, arp);
			rtReleaseLock(&(g_nsConfig->debugBufferLock));
		}
	}

	va_end(arp);
}

int nsSendPacketHeader() {

	g_nsCtx->remainDataLen = g_nsCtx->packetBuf.dataLen;
	return rtSendSocket(g_nsCtx->hSocket, (u8*)&(g_nsCtx->packetBuf), sizeof(NS_PACKET));
}

int nsSendPacketData(u8* buf, u32 size) {
	if (g_nsCtx->remainDataLen < (s32)size) {
		if (buf != g_nsConfig->debugBuf) /* avoid dead-lock */
			showDbg("send remain < size: %08x, %08x", g_nsCtx->remainDataLen, size);
		else
			showMsg("send remain < size");
		return -1;
	}
	g_nsCtx->remainDataLen -= size;
	return rtSendSocket(g_nsCtx->hSocket, buf, size);
}

int nsRecvPacketData(u8* buf, u32 size) {
	if (g_nsCtx->remainDataLen < (s32)size) {
		showDbg("recv remain < size: %08x, %08x", g_nsCtx->remainDataLen, size);
		return -1;
	}
	g_nsCtx->remainDataLen -= size;
	return rtRecvSocket(g_nsCtx->hSocket, buf, size);
}

extern u8 *image_buf;
void allocImageBuf();

/*
void rpMain2() {
int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in addr;
int ret, i;

if (udp_sock < 0) {
nsDbgPrint("socket failed: %d", udp_sock);
return;
}
addr.sin_family = AF_INET;
addr.sin_port = rtIntToPortNumber(9001);
addr.sin_addr.s_addr = 0x6602a8c0;  // __builtin_bswap32(0xc0a80266);

nsDbgPrint("bind done 22");
allocImageBuf();
while (1) {
ret = sendto(udp_sock, image_buf, 1300, 0, &addr, sizeof(addr));
}

final:
closesocket(udp_sock);
}


u32 nwmSocPutFrameRaw(Handle handle, u8* frame, u32 size) {
u32* cmdbuf = getThreadCommandBuffer();
u32 ret;
cmdbuf[0] = 0x10042;
cmdbuf[1] = size;
cmdbuf[2] = (((u32)size) << 14) | 2;
cmdbuf[3] = frame;
ret = svc_sendSyncRequest(handle);
if (ret != 0) {
return ret;
}
return cmdbuf[1];
}*/

RT_HOOK nwmValParamHook;

#define rp_nwm_hdr_size (0x2a + 8)
#define rp_data_hdr_size (4)

#define RP_THREAD_PRIO_DEFAULT (0x10)
#define RP_DST_PORT_DEFAULT (8001)
static int rpInited = 0;
static RP_CONFIG rpConfig;
static u8 rpNwmHdr[rp_nwm_hdr_size];
static u8 *rpDataBuf[rp_work_count][rp_thread_count];
static u8 *rpPacketBufLast[rp_work_count][rp_thread_count];

#define rp_img_buffer_size (0x60000)
#define rp_nwm_buffer_size (0x28000)
#define rp_screen_work_count (2)
static u8* imgBuffer[2][rp_screen_work_count];
static u32 imgBuffer_work_next[2];
static u32 currentTopId = 0, currentBottomId = 0;

static u32 tl_fbaddr[2];
static u32 bl_fbaddr[2];
static u32 tl_format, bl_format;
static u32 tl_pitch, bl_pitch;
static u32 tl_current, bl_current;

static void rpShowNextFrameBothScreen(void) {
	/* Show at least one frame*/
	svc_signalEvent(rp_syn->portEvent[0]);
	svc_signalEvent(rp_syn->portEvent[1]);
	for (int i = 0; i < rp_screen_work_count; ++i) {
		++imgBuffer[0][i][0];
		++imgBuffer[1][i][0];
	}
}

typedef u32(*sendPacketTypedef) (u8*, u32);
sendPacketTypedef nwmSendPacket = 0;


uint16_t ip_checksum(void* vdata, size_t length) {
	// Cast the data pointer to one that can be indexed.
	char* data = (char*)vdata;
	size_t i;
	// Initialise the accumulator.
	uint32_t acc = 0;

	if (length % 2) {
		data[length] = 0;
		++length;
	}
	length /= 2;
	u16 *sdata = (u16 *)data;

#if 1
	// Handle complete 16-bit blocks.
	for (i = 0; i < length; ++i) {
		acc += ntohs(sdata[i]);
		// if (acc > 0xffff) {
		// 	acc -= 0xffff;
		// }
	}
	acc = (acc & 0xffff) + (acc >> 16);
	acc += (acc >> 16);
#else
	// Handle complete 16-bit blocks.
	for (i = 0; i + 1 < length; i += 2) {
		uint16_t word;
		memcpy(&word, data + i, 2);
		acc += ntohs(word);
		if (acc > 0xffff) {
			acc -= 0xffff;
		}
	}

	// Handle any partial block at the end of the data.
	if (length & 1) {
		uint16_t word = 0;
		memcpy(&word, data + length - 1, 1);
		acc += ntohs(word);
		if (acc > 0xffff) {
			acc -= 0xffff;
		}
	}
#endif

	// Return the checksum in network byte order.
	return htons(~acc);
}

int	initUDPPacket(u8 *rpNwmBufferCur, int dataLen) {
	dataLen += 8;
	*(u16*)(rpNwmBufferCur + 0x22 + 8) = htons(8000); // src port
	*(u16*)(rpNwmBufferCur + 0x24 + 8) = htons(rpConfig.dstPort); // dest port
	*(u16*)(rpNwmBufferCur + 0x26 + 8) = htons(dataLen);
	*(u16*)(rpNwmBufferCur + 0x28 + 8) = 0; // no checksum
	dataLen += 20;

	*(u16*)(rpNwmBufferCur + 0x10 + 8) = htons(dataLen);
	*(u16*)(rpNwmBufferCur + 0x12 + 8) = 0xaf01; // packet id is a random value since we won't use the fragment
	*(u16*)(rpNwmBufferCur + 0x14 + 8) = 0x0040; // no fragment
	*(u16*)(rpNwmBufferCur + 0x16 + 8) = 0x1140; // ttl 64, udp
	// *(u32*)(rpNwmBufferCur + 0x1e + 8) = __atomic_load_n(&rpConfig.dstAddr, __ATOMIC_RELAXED);

	*(u16*)(rpNwmBufferCur + 0x18 + 8) = 0;
	*(u16*)(rpNwmBufferCur + 0x18 + 8) = ip_checksum(rpNwmBufferCur + 0xE + 8, 0x14);

	dataLen += 22;
	*(u16*)(rpNwmBufferCur + 12) = htons(dataLen);

	return dataLen;
}

void updateCurrentDstAddr(u32 dstAddr) {
	g_nsConfig->rpConfig.dstAddr = dstAddr;
	do {
		Handle hProcess;
		u32 pid = ntrConfig->HomeMenuPid;
		int ret = svc_openProcess(&hProcess, pid);
		if (ret != 0) {
			nsDbgPrint("openProcess failed: %08x\n", ret, 0);
			break;
		}

		ret = copyRemoteMemory(
			hProcess,
			(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfig) + offsetof(RP_CONFIG, dstAddr),
			0xffff8001,
			&g_nsConfig->rpConfig.dstAddr,
			sizeof(g_nsConfig->rpConfig.dstAddr));
		if (ret != 0) {
			nsDbgPrint("copyRemoteMemory (1) failed: %08x\n", ret, 0);
		}

		svc_closeHandle(hProcess);
	} while (0);

}


#define GL_RGBA8_OES (0)
#define GL_RGB8_OES (1)
#define GL_RGB565_OES (2)
#define GL_RGB5_A1_OES (3)
#define GL_RGBA4_OES (4)
#define GL_RGB565_LE (5)

#define PACKET_SIZE (1448)
#define rp_packet_data_size (PACKET_SIZE - rp_data_hdr_size)


static inline int getBppForFormat(int format) {
	int bpp;

	format &= 0x0f;
	if (format == 0){
		bpp = 4;
	}
	else if (format == 1){
		bpp = 3;
	}
	else{
		bpp = 2;
	}
	return bpp;
}
typedef struct _BLIT_CONTEXT {
	int width, height, format, src_pitch;
	int x, y;
	u8* src;
	int bpp;
	// u32 bytesInColumn;
	// u32 blankInColumn;

	// u8* transformDst;

	u8 id;
	u8 isTop;
	// u8 frameCount;

	// int directCompress;
	j_compress_ptr cinfos[rp_thread_count];
	struct rp_alloc_stats_check *cinfos_alloc_stats[rp_thread_count];

	int irow_start[rp_thread_count];
	int irow_count[rp_thread_count];
	int capture_next;
} BLIT_CONTEXT;


int rpCtxInit(BLIT_CONTEXT* ctx, int width, int height, int format, u8* src) {
	int ret = 0;
	ctx->bpp = getBppForFormat(format);
	format &= 0x0f;
	// ctx->bytesInColumn = ctx->bpp * height;
	// ctx->blankInColumn = src_pitch - ctx->bytesInColumn;
	if (ctx->format != format) {
		ret = 1;
		for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
			if (ctx->cinfos[j]->global_state != JPEG_CSTATE_START) {
				memcpy(&ctx->cinfos[j]->alloc.stats, &ctx->cinfos_alloc_stats[j]->comp, sizeof(struct rp_alloc_stats));
				ctx->cinfos[j]->global_state = JPEG_CSTATE_START;
			}
		}
	}
	ctx->format = format;
	ctx->width = width;
	ctx->height = height;
	ctx->src_pitch = ctx->bpp * ctx->height;
	ctx->src = src;
	ctx->x = 0;
	ctx->y = 0;
	// ctx->frameCount = 0;
	return ret;
}

static u32 rpLastSendTick = 0;

// static u8 rp_nwm_work_skip[rp_work_count];
// static u8 rp_nwm_frame_skipped;
static int rp_nwm_work_next, rp_nwm_thread_next;
static int rp_nwm_syn_next[rp_work_count];
static u8 rpDataBufHdr[rp_work_count][rp_data_hdr_size];

static struct rpDataBufInfo_t {
	u8 *sendPos, *pos;
	// int filled;
	int flag;
} rpDataBufInfo[rp_work_count][rp_thread_count];

int rpDataBufFilled(struct rpDataBufInfo_t *info, u8 **pos, int *flag) {
	*flag = __atomic_load_n(&info->flag, __ATOMIC_CONSUME);
	*pos = __atomic_load_n(&info->pos, __ATOMIC_RELAXED);
	return info->sendPos < *pos || *flag;
}

void rpReadyNwm(int /* thread_id */, int work_next, int id, int isTop) {
	while (1) {
		s32 res = svc_waitSynchronization1(rp_syn->work[work_next].sem_nwm, 100000000 /* 1000000000 */);
		if (res) {
			// nsDbgPrint("(%d) svc_waitSynchronization1 sem_nwm (%d) failed: %d\n", thread_id, work_next, res);
			checkExitFlag();
			continue;
		}
		break;
	}

	for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
		struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][j];
		info->sendPos = info->pos = rpDataBuf[work_next][j] + rp_data_hdr_size;
		// info->filled = 0;
		info->flag = 0;
	}

	s32 count, res;
	res = svc_releaseSemaphore(&count, rp_syn->work[work_next].sem_send, 1);
	if (res) {
		nsDbgPrint("svc_releaseSemaphore sem_send (%d) failed: %d\n", work_next, res);
	}

	rpDataBufHdr[work_next][0] = id;
	rpDataBufHdr[work_next][1] = isTop;
	rpDataBufHdr[work_next][2] = 2;
	rpDataBufHdr[work_next][3] = 0;
}

int rpSendNextBuffer(u32 nextTick, u8 *data_buf_pos, int data_buf_flag) {
	int work_next = rp_nwm_work_next;
	int thread_id = rp_nwm_thread_next;

	u8 *rp_nwm_buf, *rp_nwm_packet_buf, *rp_data_buf;
	u32 packet_len, size;

	u8 rp_nwm_buf_tmp[2000];
	struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][thread_id];

	rp_data_buf = info->sendPos;
	rp_nwm_packet_buf = rp_data_buf - rp_data_hdr_size;
	rp_nwm_buf = rp_nwm_packet_buf - rp_nwm_hdr_size;

	// int data_buf_flag = __atomic_load_n(&info->flag, __ATOMIC_RELAXED);
	// u8 *data_buf_pos = __atomic_load_n(&info->pos, __ATOMIC_RELAXED);

	size = data_buf_pos - info->sendPos;
	size = size < rp_packet_data_size ? size : rp_packet_data_size;

	int thread_emptied = info->sendPos + size == data_buf_pos;
	int thread_done = thread_emptied && data_buf_flag;

	if (size < rp_packet_data_size && !thread_done)
		return -1;

	if (size < rp_packet_data_size && thread_id != (int)rpConfig.coreCount - 1) {
		int total_size = 0, remaining_size = rp_packet_data_size;
		int sizes[rpConfig.coreCount];

		rp_nwm_buf = rp_nwm_buf_tmp;
		rp_nwm_packet_buf = rp_nwm_buf + rp_nwm_hdr_size;
		rp_data_buf = rp_nwm_packet_buf + rp_data_hdr_size;

		u8 *data_buf_pos_next;
		int data_buf_flag_next;

		memcpy(rp_data_buf, info->sendPos, size);
		total_size += size;
		remaining_size -= size;

		sizes[thread_id] = size;
		info->sendPos += size;
		// __atomic_store_n(&info->filled, 0, __ATOMIC_RELAXED);
		__atomic_store_n(&info->flag, 0, __ATOMIC_RELAXED);

		int thread_next = (thread_id + 1) % rpConfig.coreCount;
		while (1) {
			struct rpDataBufInfo_t *info_next = &rpDataBufInfo[work_next][thread_next];

			// if (!__atomic_load_n(&info_next->filled, __ATOMIC_CONSUME)) {
			if (!rpDataBufFilled(info_next, &data_buf_pos_next, &data_buf_flag_next)) {
				// rewind sizes
				for (int j = thread_id; j < thread_next; ++j) {
					struct rpDataBufInfo_t *info_prev = &rpDataBufInfo[work_next][j];
					info_prev->sendPos -= sizes[j];
					// __atomic_store_n(&info_prev->filled, 1, __ATOMIC_RELAXED);
					__atomic_store_n(&info_prev->flag, data_buf_flag, __ATOMIC_RELAXED);
				}
				return -1;
			}

			// data_buf_flag_next = __atomic_load_n(&info_next->flag, __ATOMIC_RELAXED);
			// data_buf_pos_next = __atomic_load_n(&info_next->pos, __ATOMIC_RELAXED);

			int next_size = data_buf_pos_next - info_next->sendPos;
			next_size = next_size < remaining_size ? next_size : remaining_size;

			int thread_next_emptied = info_next->sendPos + next_size == data_buf_pos_next;
			/* thread_next_done should be equal to thread_next_emptied;
			   test the condition just because */
			int thread_next_done = thread_next_emptied && data_buf_flag_next;

			memcpy(rp_data_buf + total_size, info_next->sendPos, next_size);
			total_size += next_size;
			remaining_size -= next_size;

			sizes[thread_next] = next_size;
			info_next->sendPos += next_size;
			// if (thread_next_emptied) {
			// 	__atomic_store_n(&info_next->filled, 0, __ATOMIC_RELAXED);
			// 	if (
			// 		data_buf_flag_next != __atomic_load_n(&info_next->flag, __ATOMIC_RELAXED) ||
			// 		data_buf_pos_next < __atomic_load_n(&info_next->pos, __ATOMIC_RELAXED)
			// 	) {
			// 		__atomic_store_n(&info_next->filled, 1, __ATOMIC_RELAXED);

			// 		thread_next_done = thread_next_emptied = 0;
			// 	}
			// }

			if (thread_next_done) {
				__atomic_store_n(&info_next->flag, 0, __ATOMIC_RELAXED);
				thread_next = (thread_next + 1) % rpConfig.coreCount;
				if (thread_next == 0)
					break;
			}
			if (remaining_size == 0)
				break;
		}

		memcpy(rp_nwm_buf, rpNwmHdr, rp_nwm_hdr_size);
		packet_len = initUDPPacket(rp_nwm_buf, total_size + rp_data_hdr_size);
		memcpy(rp_nwm_packet_buf, rpDataBufHdr[work_next], rp_data_hdr_size);
		if (thread_next == 0) {
			rp_nwm_packet_buf[1] |= data_buf_flag_next;
		}
		++rpDataBufHdr[work_next][3];

		nwmSendPacket(rp_nwm_buf, packet_len);
		rpLastSendTick = nextTick;

		rp_nwm_thread_next = thread_next;

		if (rp_nwm_thread_next == 0) {
			s32 count, res;
			res = svc_releaseSemaphore(&count, rp_syn->work[work_next].sem_nwm, 1);
			if (res) {
				nsDbgPrint("svc_releaseSemaphore sem_nwm (%d) failed: %d\n", work_next, res);
			}

			work_next = (work_next + 1) % rp_work_count;
			rp_nwm_work_next = work_next;
			rp_nwm_syn_next[work_next] = 1;
		}

		return 0;
	}

	memcpy(rp_nwm_buf, rpNwmHdr, rp_nwm_hdr_size);
	packet_len = initUDPPacket(rp_nwm_buf, size + rp_data_hdr_size);
	memcpy(rp_nwm_packet_buf, rpDataBufHdr[work_next], rp_data_hdr_size);
	if (thread_done && thread_id == (int)rpConfig.coreCount - 1) {
		rp_nwm_packet_buf[1] |= data_buf_flag;
	}
	++rpDataBufHdr[work_next][3];

	nwmSendPacket(rp_nwm_buf, packet_len);
	rpLastSendTick = nextTick;

	info->sendPos += size;
	// if (thread_emptied) {
	// 	__atomic_store_n(&info->filled, 0, __ATOMIC_RELAXED);
	// 	if (
	// 		data_buf_flag != __atomic_load_n(&info->flag, __ATOMIC_RELAXED) ||
	// 		data_buf_pos < __atomic_load_n(&info->pos, __ATOMIC_RELAXED)
	// 	) {
	// 		__atomic_store_n(&info->filled, 1, __ATOMIC_RELAXED);

	// 		thread_done = thread_emptied = 0; /* looks redundant; clear just in case */
	// 	}
	// }

	if (thread_done) {
		__atomic_store_n(&info->flag, 0, __ATOMIC_RELAXED);
		thread_id = (thread_id + 1) % rpConfig.coreCount;
		rp_nwm_thread_next = thread_id;

		if (rp_nwm_thread_next == 0) {
			s32 count, res;
			res = svc_releaseSemaphore(&count, rp_syn->work[work_next].sem_nwm, 1);
			if (res) {
				nsDbgPrint("svc_releaseSemaphore sem_nwm (%d) failed: %d\n", work_next, res);
			}

			work_next = (work_next + 1) % rp_work_count;
			rp_nwm_work_next = work_next;
			rp_nwm_syn_next[work_next] = 1;
		}
	}
	return 0;
}

static int rpTrySendNextBufferMaybe(int work_flush, int may_skip) {
	int work_next = rp_nwm_work_next;
	int thread_id = rp_nwm_thread_next;

	while (1) {
		struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][thread_id];

		if (rp_nwm_syn_next[work_next]) {
			s32 res = svc_waitSynchronization1(rp_syn->work[work_next].sem_send, 0);
			if (res) {
				if (res != 0x09401BFE) {
					nsDbgPrint("svc_waitSynchronization1 sem_send (%d) failed: %d\n", work_next, res);
				}
				return -1;
			}
			rp_nwm_syn_next[work_next] = 0;
		}

		u8 *data_buf_pos;
		int data_buf_flag;
		int ret = 0;

		// if (!__atomic_load_n(&info->filled, __ATOMIC_CONSUME))
		// 	return;
		if (!rpDataBufFilled(info, &data_buf_pos, &data_buf_flag))
			return may_skip ? 0 : -1;

		u32 nextTick = svc_getSystemTick();
		s32 tickDiff = (s32)nextTick - (s32)rpLastSendTick;
		if (tickDiff < (s32)rpMinIntervalBetweenPacketsInTick) {
			if (work_flush) {
				u32 sleepValue = (((s32)rpMinIntervalBetweenPacketsInTick - tickDiff) * 1000) / SYSTICK_PER_US;
				svc_sleepThread(sleepValue);
				ret = rpSendNextBuffer(svc_getSystemTick(), data_buf_pos, data_buf_flag);
			}
		} else {
			ret = rpSendNextBuffer(nextTick, data_buf_pos, data_buf_flag);
		}
		if (ret != 0)
			return -1;
		if (work_flush) {
			if (work_next != rp_nwm_work_next)
				return 0;

			if (thread_id != rp_nwm_thread_next)
				thread_id = rp_nwm_thread_next;

			may_skip = 0;
			continue;
		}
		return 0;
	}
}

static int rpTrySendNextBuffer(int work_flush) {
	return rpTrySendNextBufferMaybe(work_flush, 0);
}

void rpSendBuffer(j_compress_ptr cinfo, u8* /*buf*/, u32 size, u32 flag) {
	int work_next = cinfo->user_work_next;
	int thread_id = cinfo->user_thread_id;

	struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][thread_id];

	u8 *data_buf_pos_next = info->pos + size;
	if (data_buf_pos_next > rpPacketBufLast[work_next][thread_id]) {
		nsDbgPrint("rpSendBuffer overrun\n");
		data_buf_pos_next = rpPacketBufLast[work_next][thread_id];
	}
	cinfo->client_data = data_buf_pos_next;

	__atomic_store_n(&info->pos, data_buf_pos_next, __ATOMIC_RELAXED);
	if (flag) {
		__atomic_store_n(&info->flag, flag, __ATOMIC_RELEASE);
	}

	// __atomic_store_n(&info->filled, 1, __ATOMIC_RELEASE);
	int ret = svc_signalEvent(rp_syn->nwmEvent);
	if (ret != 0) {
		nsDbgPrint("nwmEvent signal error: %08x\n", ret);
	}
}

#define rp_jpeg_samp_factor (2)

int rpInitJpegCompress() {
	for (int i = 0; i < rp_work_count; ++i) {
		for (int j = 0; j < rp_thread_count; ++j) {
			cinfos[i * rp_thread_count + j] = &cinfos_top[i][j];
			cinfos[(i + rp_work_count) * rp_thread_count + j] = &cinfos_bot[i][j];
		}
	}

	for (int i = 0; i < rp_cinfos_count; ++i) {
		j_compress_ptr cinfo = cinfos[i];

		cinfo->alloc.buf = (void *)plgRequestMemory(cinfo_alloc_sizes[i]);
		if (cinfo->alloc.buf) {
			cinfo->alloc.stats.offset = 0;
			cinfo->alloc.stats.remaining = cinfo_alloc_sizes[i];
		} else {
			return -1;
		}

		cinfo->err = jpeg_std_error(&jerr);
		cinfo->mem_pool_manual = TRUE;
		jpeg_create_compress(cinfo);
		jpeg_stdio_dest(cinfo, 0);

		cinfo->in_color_space = JCS_RGB;
		cinfo->defaults_skip_tables = TRUE;
		jpeg_set_defaults(cinfo);
		cinfo->dct_method = JDCT_IFAST;
		cinfo->skip_markers = TRUE;
		cinfo->skip_buffers = TRUE;
		cinfo->skip_init_dest = TRUE;

		cinfo->input_components = 3;
		cinfo->jpeg_color_space = JCS_YCbCr;
		cinfo->num_components = 3;
		cinfo->color_reuse = TRUE;

		cinfo->user_work_next = (i / rp_thread_count) % rp_work_count;
		cinfo->user_thread_id = i % rp_thread_count;
	}

	jpeg_std_huff_tables((j_common_ptr)cinfos[0]);
	for (int j = 1; j < rp_cinfos_count; ++j) {
		for (int i = 0; i < NUM_HUFF_TBLS; ++i) {
			cinfos[j]->dc_huff_tbl_ptrs[i] = cinfos[0]->dc_huff_tbl_ptrs[i];
			cinfos[j]->ac_huff_tbl_ptrs[i] = cinfos[0]->ac_huff_tbl_ptrs[i];
		}
	}

	jpeg_jinit_color_converter(cinfos[0]);
	for (int j = 1; j < rp_cinfos_count; ++j)
		cinfos[j]->cconvert = cinfos[0]->cconvert;
	jpeg_rgb_ycc_start(cinfos[0]);

	return 0;
}

#define rp_prep_buffer_count (rp_thread_count * 2)
#define rp_mcu_buffer_count (rp_thread_count * 3)
#define rp_rows_blk_halves_count (2)

typedef JSAMPARRAY pre_proc_buffer_t[MAX_COMPONENTS];
typedef JSAMPARRAY color_buffer_t[MAX_COMPONENTS];
typedef JBLOCKROW MCU_buffer_t[C_MAX_BLOCKS_IN_MCU];

#if 0
static pre_proc_buffer_t prep_buffers[rp_work_count][rp_prep_buffer_count];
static color_buffer_t color_buffers[rp_work_count][rp_thread_count];
static MCU_buffer_t MCU_buffers[rp_work_count][rp_mcu_buffer_count];
#else
static pre_proc_buffer_t prep_buffers[rp_work_count][rp_thread_count];
static color_buffer_t color_buffers[rp_work_count][rp_thread_count];
static MCU_buffer_t MCU_buffers[rp_work_count][rp_thread_count];
#endif

static u32 currentUpdating;
static u32 frameCount[2];
static u32 isPriorityTop;
static u32 priorityFactor;
static u32 priorityFactorLogScaled;
static int nextScreenCaptured[rp_work_count];
static int nextScreenSynced[rp_work_count] = { 0 };

static BLIT_CONTEXT blit_context[rp_work_count];
static int rpConfigChanged;
static int rpDstAddrChanged;

void rpCaptureNextScreen(int work_next, int wait_sync);

static void rpTryCaptureNextScreen(int *need_capture_next, int *capture_next, int work_next) {
	if (*need_capture_next) {
		if (__atomic_load_n(capture_next, __ATOMIC_RELAXED)) {
			work_next = (work_next + 1) % rp_work_count;
			rpCaptureNextScreen(work_next, 0);
			if (nextScreenCaptured[work_next]) {
				*need_capture_next = 0;
			}
		}
	}
}

void rpJPEGCompress0(j_compress_ptr cinfo,
	u8* src, u32 pitch,
	int irow_start, int irow_count,
	int work_next, int thread_id, int *capture_next
) {
	JDIMENSION in_rows_blk = DCTSIZE * cinfo->max_v_samp_factor;
	JDIMENSION in_rows_blk_half = in_rows_blk / 2;

	JSAMPIMAGE output_buf = prep_buffers[work_next][thread_id];
	JSAMPIMAGE color_buf = color_buffers[work_next][thread_id];

	JSAMPROW input_buf[in_rows_blk_half];

	int need_capture_next = thread_id == 0;

	int j_max = in_rows_blk * (irow_start + irow_count);
	j_max = j_max < (int)cinfo->image_height ? j_max : (int)cinfo->image_height;
	int j_max_half = in_rows_blk * (irow_start + irow_count / 2);
	j_max_half = j_max_half < (int)cinfo->image_height ? j_max_half : (int)cinfo->image_height;

	int j_start = in_rows_blk * irow_start;
	if (j_max_half == j_start)
		__atomic_store_n(capture_next, 1, __ATOMIC_RELAXED);
	rpTryCaptureNextScreen(&need_capture_next, capture_next, work_next);

	for (int j = j_start, progress = 0; j < j_max;) {
		for (int i = 0; i < (int)in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 0);

		for (int i = 0; i < (int)in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 1);

		if (j_max_half == j)
			__atomic_store_n(capture_next, 1, __ATOMIC_RELAXED);
		rpTryCaptureNextScreen(&need_capture_next, capture_next, work_next);

		JBLOCKROW *MCU_buffer = MCU_buffers[work_next][thread_id];
		for (int k = 0; k < (int)cinfo->MCUs_per_row; ++k) {
			jpeg_compress_data(cinfo, output_buf, MCU_buffer, k);
			jpeg_encode_mcu_huff(cinfo, MCU_buffer);

			// if (thread_id == rp_nwm_thread_id) {
			// 	rpTrySendNextBuffer(0);
			// }
		}

		__atomic_store_n(&jpeg_progress[work_next][thread_id], ++progress, __ATOMIC_RELAXED);
	}
}

void rpReadyWork(BLIT_CONTEXT* ctx, int work_next) {
	// nsDbgPrint("rpReadyWork %d\n", work_next);
	j_compress_ptr cinfo;

	if (ctx->format >= 3) {
		svc_sleepThread(1000000000);
		return;
	}

	int work_prev = work_next == 0 ? rp_work_count - 1 : work_next - 1;
	int progress[rpConfig.coreCount];
	for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
		__atomic_store_n(&jpeg_progress[work_next][j], 0, __ATOMIC_RELAXED);

		progress[j] = __atomic_load_n(&jpeg_progress[work_prev][j], __ATOMIC_RELAXED);
	}

	int mcu_size = DCTSIZE * rp_jpeg_samp_factor;
	int mcus_per_row = ctx->height / mcu_size;
	int mcu_rows = ctx->width / mcu_size;
	int mcu_rows_per_thread = (mcu_rows + (rpConfig.coreCount - 1)) / rpConfig.coreCount;
	jpeg_rows[work_next] = mcu_rows_per_thread;
	jpeg_rows_last[work_next] = mcu_rows - jpeg_rows[work_next] * (rpConfig.coreCount - 1);

	if (rpConfig.coreCount > 1) {
		if (jpeg_rows[work_prev]) {
			int rows = jpeg_rows[work_next];
			int rows_last = jpeg_rows_last[work_next];
			int progress_last = progress[rpConfig.coreCount - 1];
			if (progress_last < jpeg_adjusted_rows_last[work_prev]) {
				rows_last = (rows_last * (1 << 16) *
					progress_last / jpeg_rows_last[work_prev] + (1 << 15)) >> 16;
				if (rows_last > jpeg_rows_last[work_next])
					rows_last = jpeg_rows_last[work_next];
				if (rows_last == 0)
					rows_last = 1;
				rows = (mcu_rows - rows_last) / (rpConfig.coreCount - 1);
			} else {
				int progress_rest = 0;
				for (int j = 0; j < (int)rpConfig.coreCount - 1; ++j) {
					progress_rest += progress[j];
				}
				rows = (rows * (1 << 16) *
					progress_rest / jpeg_rows[work_prev] / (rpConfig.coreCount - 1) + (1 << 15)) >> 16;
				if (rows < jpeg_rows[work_next])
					rows = jpeg_rows[work_next];
				int rows_max = (mcu_rows - 1) / (rpConfig.coreCount - 1);
				if (rows > rows_max)
					rows = rows_max;
			}
			jpeg_adjusted_rows[work_next] = rows;
			jpeg_adjusted_rows_last[work_next] = mcu_rows - rows * (rpConfig.coreCount - 1);
		} else {
			jpeg_adjusted_rows[work_next] = jpeg_rows[work_next];
			jpeg_adjusted_rows_last[work_next] = jpeg_rows_last[work_next];
		}
	} else {
		jpeg_adjusted_rows[work_next] = jpeg_rows[work_next];
		jpeg_adjusted_rows_last[work_next] = jpeg_rows_last[work_next];
	}

	for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
		cinfo = ctx->cinfos[j];
		cinfo->image_width = ctx->height;
		cinfo->image_height = ctx->width;
		cinfo->input_components = ctx->format == 0 ? 4 : 3;
		cinfo->in_color_space = ctx->format == 0 ? JCS_EXT_XBGR : ctx->format == 1 ? JCS_EXT_BGR : JCS_RGB565;

		cinfo->restart_in_rows = jpeg_adjusted_rows[work_next];
		cinfo->restart_interval = cinfo->restart_in_rows * mcus_per_row;

		if (cinfo->global_state == JPEG_CSTATE_START) {
			jpeg_start_compress(cinfo, TRUE);
		} else {
			jpeg_suppress_tables(cinfo, FALSE);
			jpeg_start_pass_prep(cinfo, 0);
			jpeg_start_pass_huff(cinfo, j);
			jpeg_start_pass_coef(cinfo, 0);
			jpeg_start_pass_main(cinfo, 0);
			cinfo->next_scanline = 0;
		}

		ctx->irow_start[j] = cinfo->restart_in_rows * j;
		ctx->irow_count[j] = j == (int)rpConfig.coreCount - 1 ? jpeg_adjusted_rows_last[work_next] : cinfo->restart_in_rows;
	}
	ctx->capture_next = 0;

#if 0
	struct rp_work_t *work = rp_work[work_next];
	// struct rp_work_syn_t *syn = rp_syn->work[work_next];

	memset(work, 0, sizeof(struct rp_work_t));
	work->work_next = work_next;
	work->cinfo = cinfo;
	work->src = src;
	work->pitch = pitch;

	work->mcu_row = cinfo->MCUs_per_row;
	work->prep_reading_done_state = rp_prep_reading + work->mcu_row;
	work->in_rows_blk = DCTSIZE * cinfo->max_v_samp_factor;
	work->in_rows_blk_half = work->in_rows_blk / 2;
	work->in_rows_blk_half_n = cinfo->image_height / work->in_rows_blk_half;
	work->mcu_n = work->mcu_row * (cinfo->image_height / work->in_rows_blk);
#endif
}

void rpSendFramesBody(int thread_id, BLIT_CONTEXT* ctx, int work_next) {
	j_compress_ptr cinfo = ctx->cinfos[thread_id];

	cinfo->client_data = rpDataBufInfo[work_next][thread_id].pos;
	jpeg_init_destination(cinfo);

	if (thread_id == 0) {
		jpeg_write_file_header(cinfo);
		jpeg_write_frame_header(cinfo);
		jpeg_write_scan_header(cinfo);
	}

	rpJPEGCompress0(cinfo,
		ctx->src, ctx->src_pitch,
		ctx->irow_start[thread_id], ctx->irow_count[thread_id],
		work_next, thread_id, &ctx->capture_next);
	jpeg_finish_pass_huff(cinfo);

	if (thread_id != (int)rpConfig.coreCount - 1) {
		jpeg_emit_marker(cinfo, JPEG_RST0 + thread_id);
	} else {
		jpeg_write_file_trailer(cinfo);
	}
	jpeg_term_destination(cinfo);
}

void rpKernelCallback(int isTop) {
	// u32 fbP2VOffset = 0xc0000000;
	u32 current_fb;

	u32 myIoBasePdc = 0x10400000 | 0x80000000;
	if (isTop) {
		tl_fbaddr[0] = REG(myIoBasePdc + 0x468);
		tl_fbaddr[1] = REG(myIoBasePdc + 0x46c);
		tl_format = REG(myIoBasePdc + 0x470);
		tl_pitch = REG(myIoBasePdc + 0x490);
		current_fb = REG(myIoBasePdc + 0x478);
		current_fb &= 1;
		if (g_nsConfig->rpCaptureMode & 0x1)
			current_fb = !current_fb;
		tl_current = tl_fbaddr[current_fb];

		int full_width = !(tl_format & (7 << 4));
		/* for full-width top screen (800x240), output every other column */
		if (full_width)
			tl_pitch *= 2;
	} else {
		bl_fbaddr[0] = REG(myIoBasePdc + 0x568);
		bl_fbaddr[1] = REG(myIoBasePdc + 0x56c);
		bl_format = REG(myIoBasePdc + 0x570);
		bl_pitch = REG(myIoBasePdc + 0x590);
		current_fb = REG(myIoBasePdc + 0x578);
		current_fb &= 1;
		if (g_nsConfig->rpCaptureMode & 0x1)
			current_fb = !current_fb;
		bl_current = bl_fbaddr[current_fb];
	}
}

Handle rpHDma[rp_work_count], rpHandleHome, rpHandleGame, rpGamePid;
u32 rpGameFCRAMBase = 0;

void rpInitDmaHome() {
	svc_openProcess(&rpHandleHome, 0xf);
}

void rpCloseGameHandle(void) {
	if (rpHandleGame) {
		svc_closeHandle(rpHandleGame);
		rpHandleGame = 0;
		rpGameFCRAMBase = 0;
		rpGamePid = 0;

		rpShowNextFrameBothScreen();
	}
}

Handle rpGetGameHandle() {
	int i, res;
	Handle hProcess;
	u32 pids[100];
	s32 pidCount;
	u32 tid[2];

	u32 gamePid = __atomic_load_n(&g_nsConfig->rpGamePid, __ATOMIC_RELAXED);
	if (gamePid != rpGamePid) {
		rpCloseGameHandle();
		rpGamePid = gamePid;
	}

	if (rpHandleGame == 0) {
		if (gamePid != 0) {
			res = svc_openProcess(&hProcess, gamePid);
			if (res == 0) {
				rpHandleGame = hProcess;
			}
		}
		if (rpHandleGame == 0) {
			res = svc_getProcessList(&pidCount, pids, 100);
			if (res == 0) {
				for (i = 0; i < pidCount; ++i) {
					if (pids[i] < 0x28)
						continue;

					res = svc_openProcess(&hProcess, pids[i]);
					if (res == 0) {
						res = getProcessTIDByHandle(hProcess, tid);
						if (res == 0) {
							if ((tid[1] & 0xFFFF) == 0) {
								rpHandleGame = hProcess;
								gamePid = pids[i];
								break;
							}
						}
						svc_closeHandle(hProcess);
					}
				}
			}
		}
		if (rpHandleGame == 0) {
			return 0;
		}
	}
	if (rpGameFCRAMBase == 0) {
		if (svc_flushProcessDataCache(rpHandleGame, 0x14000000, 0x1000) == 0) {
			rpGameFCRAMBase = 0x14000000;
		}
		else if (svc_flushProcessDataCache(rpHandleGame, 0x30000000, 0x1000) == 0) {
			rpGameFCRAMBase = 0x30000000;
		}
		else {
			rpCloseGameHandle();
			return 0;
		}

		nsDbgPrint("game process: pid 0x%04x, fcram 0x%08x\n", gamePid, rpGameFCRAMBase);
	}
	return rpHandleGame;
}

int isInVRAM(u32 phys) {
	if (phys >= 0x18000000) {
		if (phys < 0x18000000 + 0x00600000) {
			return 1;
		}
	}
	return 0;
}

int isInFCRAM(u32 phys) {
	if (phys >= 0x20000000) {
		if (phys < 0x20000000 + 0x10000000) {
			return 1;
		}
	}
	return 0;
}

int rpCaptureScreen(int work_next, int isTop) {
	u32 phys = isTop ? tl_current : bl_current;
	void *dest = imgBuffer[isTop][imgBuffer_work_next[isTop]];
	Handle hProcess = rpHandleHome;

	u32 format = (isTop ? tl_format : bl_format) & 0x0f;
	u32 bpp; /* bytes per pixel */
	u32 burstSize = 16; /* highest power-of-2 factor of 240 */
	/* burstSize should be a power-of-2 (?), try making it as big as possible */
	if (format == 0){
		bpp = 4;
		burstSize *= bpp;
	}
	else if (format == 1){
		bpp = 3;
	}
	else{
		bpp = 2;
		burstSize *= bpp;
	}
	u32 transferSize = 240 * bpp;
	u32 pitch = isTop ? tl_pitch : bl_pitch;

	u32 bufSize = transferSize * (isTop ? 400 : 320);

	if (transferSize == pitch) {
		u32 mul = isTop ? 16 : 64; /* highest power-of-2 factor of either 400 or 320 */
		transferSize *= mul;
		while (transferSize >= (1 << 15)) { /* avoid overflow (transferSize is s16) */
			transferSize /= 2;
			mul /= 2;
		}
		burstSize *= mul;
		pitch = transferSize;
	}

	DmaConfig dmaConfig = {
		.channelId = -1,
		.flags = DMACFG_WAIT_AVAILABLE | DMACFG_USE_DST_CONFIG | DMACFG_USE_SRC_CONFIG,
		.dstCfg = {
			.deviceId = -1,
			.allowedAlignments = 15,
			.burstSize = burstSize,
			.burstStride = burstSize,
			.transferSize = transferSize,
			.transferStride = transferSize,
		},
		.srcCfg = {
			.deviceId = -1,
			.allowedAlignments = 15,
			.burstSize = burstSize,
			.burstStride = burstSize,
			.transferSize = transferSize,
			.transferStride = pitch,
		},
	};

	s32 res;

	if (bufSize > rp_img_buffer_size) {
		nsDbgPrint("bufSize exceeds imgBuffer: %d (%d)\n", bufSize, rp_img_buffer_size);
		goto final;
	}

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)dest, bufSize);
	if (rpHDma[work_next]) {
		svc_closeHandle(rpHDma[work_next]);
		rpHDma[work_next] = 0;
	}

	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		res = svc_startInterProcessDma(&rpHDma[work_next], CURRENT_PROCESS_HANDLE,
			dest, hProcess, (u8 *)0x1F000000 + (phys - 0x18000000), bufSize, &dmaConfig);
		if (res < 0) {
			nsDbgPrint("svc_startInterProcessDma home failed: %08x\n", res);
			goto final;
		}
		return 0;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			res = svc_startInterProcessDma(&rpHDma[work_next], CURRENT_PROCESS_HANDLE,
				dest, hProcess, (u8 *)rpGameFCRAMBase + (phys - 0x20000000), bufSize, &dmaConfig);
			if (res < 0) {
				// nsDbgPrint("svc_startInterProcessDma game failed: %08x\n", res);
				rpCloseGameHandle();

				svc_sleepThread(50000000);
				rpHDma[work_next] = 0;
				return -1;
			}
			return 0;
		}
		// nsDbgPrint("capture game screen failed: phys %08x\n", phys);
		svc_sleepThread(50000000);
		return -1;
	}
final:
	u32 pid = 0;
	svc_getProcessId(&pid, hProcess);
	nsDbgPrint("capture screen failed: phys %08x, hProc %08x, pid %04x\n", phys, hProcess, pid);
	svc_sleepThread(1000000000);
	rpHDma[work_next] = 0;
	return -1;
}

static u32 rpGetPrioScaled(u32 isTop) {
	return isTop == isPriorityTop ? 1 << SCALEBITS : priorityFactorLogScaled;
}

void rpCaptureNextScreen(int work_next, int wait_sync) {
	if (__atomic_load_n(&g_nsConfig->rpConfigLock, __ATOMIC_RELAXED)) {
		rpConfigChanged = 1;
		return;
	}

	struct rp_work_syn_t *syn = &rp_syn->work[work_next];
	s32 res;
	if (!nextScreenSynced[work_next]) {
		res = svc_waitSynchronization1(syn->sem_end, wait_sync ? 1000000000 : 0);
		if (res) {
			// if (wait_sync || res < 0)
				// nsDbgPrint("svc_waitSynchronization1 sem_end (%d) failed: %d\n", work_next, res);
			return;
		}

		nextScreenSynced[work_next] = 1;
	}

	currentUpdating = isPriorityTop;
	int screenBusyWait = 0;

	while (!rpResetThreads) {
		if (!__atomic_load_n(&rpPortGamePid, __ATOMIC_RELAXED)) {
			if (priorityFactor != 0) {
				if (frameCount[currentUpdating] >= priorityFactor) {
					if (frameCount[!currentUpdating] >= 1) {
						frameCount[currentUpdating] -= priorityFactor;
						frameCount[!currentUpdating] -= 1;
						currentUpdating = !currentUpdating;
					}
				}
			}
			screenBusyWait = 1;
			break;
		}

		s32 isTop = currentUpdating;

		if (priorityFactor == 0) {
			if ((res = svc_waitSynchronization1(rp_syn->portEvent[isTop], 100000000)) == 0) {
				break;
			}
			continue;
		}

		u32 prio[2];
		prio[isTop] = rpGetPrioScaled(isTop);
		prio[!isTop] = rpGetPrioScaled(!isTop);

		u32 factor[2];
		factor[isTop] = (u64)(1 << SCALEBITS) * (s64)frameQueued[isTop] / (s64)prio[isTop];
		factor[!isTop] = (u64)(1 << SCALEBITS) * (s64)frameQueued[!isTop] / (s64)prio[!isTop];

		if (factor[isTop] < priorityFactorLogScaled && factor[!isTop] < priorityFactorLogScaled) {
			frameQueued[0] += priorityFactorLogScaled;
			frameQueued[1] += priorityFactorLogScaled;
			continue;
		}

		isTop = factor[isTop] >= factor[!isTop] ? isTop : !isTop;

		if (frameQueued[isTop] >= prio[isTop]) {
			if ((res = svc_waitSynchronization1(rp_syn->portEvent[isTop], 0)) == 0) {
				currentUpdating = isTop;
				frameQueued[isTop] -= prio[isTop];
				break;
			}
		}

		if (frameQueued[!isTop] >= prio[!isTop]) {
			if ((res = svc_waitSynchronization1(rp_syn->portEvent[!isTop], 0)) == 0) {
				currentUpdating = !isTop;
				frameQueued[!isTop] -= prio[!isTop];
				break;
			}
		}

		res = svc_waitSynchronizationN(&isTop, rp_syn->portEvent, 2, 0, 100000000);
		if (res != 0) {
			if (res != 0x09401BFE) {
				nsDbgPrint("svc_waitSynchronizationN rp_syn->portEvent all error: %08x\n", res);
				break;
			}
			continue;
		}

		if (frameQueued[isTop] >= prio[isTop]) {
			currentUpdating = isTop;
			frameQueued[isTop] -= prio[isTop];
		} else {
			currentUpdating = isTop;
			frameQueued[isTop] = 0;
		}
		break;
	}
	if (rpResetThreads)
		return;

	int captured = g_nsConfig->rpCaptureMode & 0x2 ?
		1 :
		(rpKernelCallback(currentUpdating), rpCaptureScreen(work_next, currentUpdating) == 0);
	if (captured) {
		if (screenBusyWait)
			frameCount[currentUpdating] += 1;
		nextScreenCaptured[work_next] = captured;
		nextScreenSynced[work_next] = 0;
		__atomic_clear(&syn->sem_set, __ATOMIC_RELAXED);

		for (int j = 1; j < (int)rpConfig.coreCount; ++j) {
			s32 count;
			res = svc_releaseSemaphore(&count, rp_syn->thread[j].sem_start, 1);
			if (res) {
				nsDbgPrint("svc_releaseSemaphore sem_start failed: %d\n", res);
				// return;
			}
		}
	}
}

static void rpDoCopyScreen(BLIT_CONTEXT *ctx) {
	u32 phys = ctx->isTop ? tl_current : bl_current;
	u8 *dest = ctx->src;

	u32 pitch = ctx->isTop ? tl_pitch : bl_pitch;
	if ((int)pitch == ctx->src_pitch) {
		memcpy_ctr(dest, (void *)(phys | 0x80000000), ctx->width * pitch);
	} else {
		for (int i = 0; i < ctx->width; ++i) {
			memcpy_ctr(dest + i * ctx->src_pitch, (u8 *)(phys | 0x80000000) + i * pitch, ctx->src_pitch);
		}
	}
}

static int rp_work_next = 0;
static u8 rp_skip_frame[rp_work_count] = { 0 };

int rpSendFramesStart(int thread_id, int work_next) {
	BLIT_CONTEXT *ctx = &blit_context[work_next];
	struct rp_work_syn_t *syn = &rp_syn->work[work_next];

	// if (thread_id == rp_nwm_thread_id && (rp_nwm_work_next == work_next || rp_nwm_frame_skipped)) {
	// 	if (rpTrySendNextBufferMaybe(1, rp_nwm_frame_skipped) != 0) {
	// 		nsDbgPrint("flush nwm buffer failed: %d\n", rp_nwm_frame_skipped);
	// 	}
	// 	rp_nwm_frame_skipped = 0;
	// }

	u8 skip_frame = 0;
	if (!__atomic_test_and_set(&syn->sem_set, __ATOMIC_RELAXED)) {
		int format_changed = 0;
		ctx->isTop = currentUpdating;
		if (ctx->isTop) {
			// send top
			for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
				ctx->cinfos[j] = &cinfos_top[work_next][j];
				ctx->cinfos_alloc_stats[j] = &alloc_stats_top[work_next][j];
			}

			format_changed = rpCtxInit(ctx, 400, 240, tl_format, imgBuffer[1][imgBuffer_work_next[1]]);
			ctx->id = (u8)currentTopId;
		} else {
			// send bottom
			for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
				ctx->cinfos[j] = &cinfos_bot[work_next][j];
				ctx->cinfos_alloc_stats[j] = &alloc_stats_bot[work_next][j];
			}

			format_changed = rpCtxInit(ctx, 320, 240, bl_format, imgBuffer[0][imgBuffer_work_next[0]]);
			ctx->id = (u8)currentBottomId;
		}

		s32 res;
		if (g_nsConfig->rpCaptureMode & 0x2) {
			rpKernelCallback(ctx->isTop);
			rpDoCopyScreen(ctx);
		} else {
			res = svc_waitSynchronization1(rpHDma[work_next], 1000000000);
		}
		// if (res) {
			// nsDbgPrint("(%d) svc_waitSynchronization1 rpHDma (%d) failed: %d\n", thread_id, work_next, res);
		// }

		int imgBuffer_work_prev = imgBuffer_work_next[ctx->isTop];
		if (imgBuffer_work_prev == 0)
			imgBuffer_work_prev = rp_screen_work_count - 1;
		else
			--imgBuffer_work_prev;

		skip_frame = !format_changed && memcmp(ctx->src, imgBuffer[ctx->isTop][imgBuffer_work_prev], ctx->width * ctx->src_pitch) == 0;
		__atomic_store_n(&rp_skip_frame[work_next], skip_frame, __ATOMIC_RELAXED);
		if (!skip_frame) {
			imgBuffer_work_next[ctx->isTop] = (imgBuffer_work_next[ctx->isTop] + 1) % rp_screen_work_count;
			ctx->isTop ? ++currentTopId : ++currentBottomId;
			rpReadyWork(ctx, work_next);
			rpReadyNwm(thread_id, work_next, ctx->id, ctx->isTop);
		}

		s32 count;
		for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
			if (j != thread_id) {
				res = svc_releaseSemaphore(&count, rp_syn->thread[j].sem_work, 1);
				if (res) {
					nsDbgPrint("(%d) svc_releaseSemaphore sem_work[j] (%d) failed: %d\n", thread_id, j, work_next, res);
					// goto final;
				}
			}
		}
	} else {
		while (1) {
			s32 res = svc_waitSynchronization1(rp_syn->thread[thread_id].sem_work, 1000000000);
			if (res) {
				nsDbgPrint("(%d) svc_waitSynchronization1 sem_work (%d) failed: %d\n", thread_id, work_next, res);
				checkExitFlag();
				continue;
			}
			break;
		}
		skip_frame = __atomic_load_n(&rp_skip_frame[work_next], __ATOMIC_RELAXED);
	}

	// nsDbgPrint("(%d) skip_frame (%d): %d\n", thread_id, work_next, (int)skip_frame);
	if (!skip_frame)
		rpSendFramesBody(thread_id, ctx, work_next);
	// else if (thread_id == rp_nwm_thread_id) {
	// 	rp_nwm_frame_skipped = 1;
		// rp_nwm_work_skip[work_next] = 1;
		// while (rp_nwm_work_next != work_next) {
		// 	if (rpTrySendNextBuffer(1)) {
		// 		u32 sleepValue = rpMinIntervalBetweenPacketsInTick * 1000 / SYSTICK_PER_US;
		// 		svc_sleepThread(sleepValue);
		// 	}
		// }
		// s32 count, res;
		// res = svc_releaseSemaphore(&count, rp_syn->work[work_next].sem_nwm, 1);
		// if (res) {
		// 	nsDbgPrint("svc_releaseSemaphore sem_nwm (%d) failed: %d\n", work_next, res);
		// }
	// }

// final:
	if (__atomic_add_fetch(&syn->sem_count, 1, __ATOMIC_RELAXED) == (int)rpConfig.coreCount) {
		__atomic_store_n(&syn->sem_count, 0, __ATOMIC_RELAXED);
		s32 count;
		// nsDbgPrint("(%d) svc_releaseSemaphore sem_end (%d):\n", thread_id, work_next);
		s32 res = svc_releaseSemaphore(&count, syn->sem_end, 1);
		if (res) {
			nsDbgPrint("svc_releaseSemaphore sem_end (%d) failed: %d\n", work_next, res);
		}
	}

	return skip_frame;
}

#define LOG(x) FIX(log(x) / log(2))
static u32 log_scaled_tab[] = {
	LOG(1), LOG(2), LOG(3), LOG(4), LOG(5), LOG(6), LOG(7), LOG(8), LOG(9), LOG(10), LOG(11), LOG(12), LOG(13), LOG(14), LOG(15), LOG(16),
	LOG(17), LOG(18), LOG(19), LOG(20), LOG(21), LOG(22), LOG(23), LOG(24), LOG(25), LOG(26), LOG(27), LOG(28), LOG(29), LOG(30), LOG(31), LOG(32),
	LOG(33), LOG(34), LOG(35), LOG(36), LOG(37), LOG(38), LOG(39), LOG(40), LOG(41), LOG(42), LOG(43), LOG(44), LOG(45), LOG(46), LOG(47), LOG(48),
	LOG(49), LOG(50), LOG(51), LOG(52), LOG(53), LOG(54), LOG(55), LOG(56), LOG(57), LOG(58), LOG(59), LOG(60), LOG(61), LOG(62), LOG(63), LOG(64),
	LOG(65), LOG(66), LOG(67), LOG(68), LOG(69), LOG(70), LOG(71), LOG(72), LOG(73), LOG(74), LOG(75), LOG(76), LOG(77), LOG(78), LOG(79), LOG(80),
	LOG(81), LOG(82), LOG(83), LOG(84), LOG(85), LOG(86), LOG(87), LOG(88), LOG(89), LOG(90), LOG(91), LOG(92), LOG(93), LOG(94), LOG(95), LOG(96),
	LOG(97), LOG(98), LOG(99), LOG(100), LOG(101), LOG(102), LOG(103), LOG(104), LOG(105), LOG(106), LOG(107), LOG(108), LOG(109), LOG(110), LOG(111), LOG(112),
	LOG(113), LOG(114), LOG(115), LOG(116), LOG(117), LOG(118), LOG(119), LOG(120), LOG(121), LOG(122), LOG(123), LOG(124), LOG(125), LOG(126), LOG(127), LOG(128),
	LOG(129), LOG(130), LOG(131), LOG(132), LOG(133), LOG(134), LOG(135), LOG(136), LOG(137), LOG(138), LOG(139), LOG(140), LOG(141), LOG(142), LOG(143), LOG(144),
	LOG(145), LOG(146), LOG(147), LOG(148), LOG(149), LOG(150), LOG(151), LOG(152), LOG(153), LOG(154), LOG(155), LOG(156), LOG(157), LOG(158), LOG(159), LOG(160),
	LOG(161), LOG(162), LOG(163), LOG(164), LOG(165), LOG(166), LOG(167), LOG(168), LOG(169), LOG(170), LOG(171), LOG(172), LOG(173), LOG(174), LOG(175), LOG(176),
	LOG(177), LOG(178), LOG(179), LOG(180), LOG(181), LOG(182), LOG(183), LOG(184), LOG(185), LOG(186), LOG(187), LOG(188), LOG(189), LOG(190), LOG(191), LOG(192),
	LOG(193), LOG(194), LOG(195), LOG(196), LOG(197), LOG(198), LOG(199), LOG(200), LOG(201), LOG(202), LOG(203), LOG(204), LOG(205), LOG(206), LOG(207), LOG(208),
	LOG(209), LOG(210), LOG(211), LOG(212), LOG(213), LOG(214), LOG(215), LOG(216), LOG(217), LOG(218), LOG(219), LOG(220), LOG(221), LOG(222), LOG(223), LOG(224),
	LOG(225), LOG(226), LOG(227), LOG(228), LOG(229), LOG(230), LOG(231), LOG(232), LOG(233), LOG(234), LOG(235), LOG(236), LOG(237), LOG(238), LOG(239), LOG(240),
	LOG(241), LOG(242), LOG(243), LOG(244), LOG(245), LOG(246), LOG(247), LOG(248), LOG(249), LOG(250), LOG(251), LOG(252), LOG(253), LOG(254), LOG(255), LOG(256),
};

static void rpSendFrames() {
	if (g_nsConfig->rpConfig.coreCount != rpConfig.coreCount) {
		rpResetThreads = 1;
		return;
	}

	// rpCurrentMode = g_nsConfig->startupInfo[8];
	// rpQuality = g_nsConfig->startupInfo[9];
	// rpQosValueInBytes = g_nsConfig->startupInfo[10];

	if (rpConfig.dstPort != g_nsConfig->rpConfig.dstPort)
		rpConfig.dstPort = g_nsConfig->rpConfig.dstPort;
	if (rpConfig.dstPort == 0) {
		g_nsConfig->rpConfig.dstPort = rpConfig.dstPort = RP_DST_PORT_DEFAULT;
	}

	if (g_nsConfig->rpConfig.qosValueInBytes < 512 * 1024)
		g_nsConfig->rpConfig.qosValueInBytes = 512 * 1024;
	else if (g_nsConfig->rpConfig.qosValueInBytes > 5 * 1024 * 1024 / 2)
		g_nsConfig->rpConfig.qosValueInBytes = 5 * 1024 * 1024 / 2;
	rpMinIntervalBetweenPacketsInTick = (1000000 / (g_nsConfig->rpConfig.qosValueInBytes / PACKET_SIZE)) * SYSTICK_PER_US;
	rpMinIntervalBetweenPacketsInNS = rpMinIntervalBetweenPacketsInTick * 1000 / SYSTICK_PER_US;

	{
		u32 isTop = 1;
		priorityFactor = 0;
		u32 mode = (g_nsConfig->rpConfig.currentMode & 0xff00) >> 8;
		u32 factor = (g_nsConfig->rpConfig.currentMode & 0xff);
		if (mode == 0) {
			isTop = 0;
		}
		isPriorityTop = isTop;
		priorityFactor = factor;
		priorityFactorLogScaled = log_scaled_tab[factor];

		rpShowNextFrameBothScreen();
	}

	if (g_nsConfig->rpConfig.quality < 10)
		g_nsConfig->rpConfig.quality = 10;
	else if (g_nsConfig->rpConfig.quality > 100)
		g_nsConfig->rpConfig.quality = 100;
	if (rpConfig.quality != g_nsConfig->rpConfig.quality) {
		rpConfig.quality = g_nsConfig->rpConfig.quality;

		for (int j = 0; j < rp_cinfos_count; ++j)
			cinfos[j]->global_state = JPEG_CSTATE_START;
		jpeg_set_quality(cinfos[0], g_nsConfig->rpConfig.quality, TRUE);
		for (int j = 1; j < rp_cinfos_count; ++j)
			for (int i = 0; i < NUM_QUANT_TBLS; ++i)
				cinfos[j]->quant_tbl_ptrs[i] = cinfos[0]->quant_tbl_ptrs[i];

		for (int i = 0; i < rp_work_count; ++i) {
			for (int j = 0; j < rp_thread_count /* (int)rpConfig.coreCount */; ++j) {
				if (!alloc_stats_top[i][j].qual.offset) {
					memcpy(&alloc_stats_top[i][j].qual, &cinfos_top[i][j].alloc.stats, sizeof(struct rp_alloc_stats));
				} else {
					memcpy(&cinfos_top[i][j].alloc.stats, &alloc_stats_top[i][j].qual, sizeof(struct rp_alloc_stats));
				}

				if (!alloc_stats_bot[i][j].qual.offset) {
					memcpy(&alloc_stats_bot[i][j].qual, &cinfos_bot[i][j].alloc.stats, sizeof(struct rp_alloc_stats));
				} else {
					memcpy(&cinfos_bot[i][j].alloc.stats, &alloc_stats_bot[i][j].qual, sizeof(struct rp_alloc_stats));
				}
			}
		}

		jpeg_jinit_forward_dct(cinfos[0]);
		cinfos[0]->fdct_reuse = TRUE;

		for (int j = 1; j < rp_cinfos_count; ++j) {
			cinfos[j]->fdct = cinfos[0]->fdct;
			cinfos[j]->fdct_reuse = TRUE;
		}

		jpeg_start_pass_fdctmgr(cinfos[0]);

		for (int h = 0; h < rp_work_count; ++h) {
			for (int i = 0; i < (int)(sizeof(*prep_buffers) / sizeof(**prep_buffers)); ++i) {
				for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
					prep_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
						240, (JDIMENSION)(MAX_SAMP_FACTOR * DCTSIZE));
				}
			}
			for (int i = 0; i < (int)(sizeof(*color_buffers) / sizeof(**color_buffers)); ++i) {
				for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
					color_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
						240, (JDIMENSION)MAX_SAMP_FACTOR);
				}
			}
			for (int i = 0; i < (int)(sizeof(*MCU_buffers) / sizeof(**MCU_buffers)); ++i) {
				JBLOCKROW buffer = (JBLOCKROW)jpeg_alloc_large((j_common_ptr)cinfos[h], JPOOL_IMAGE, C_MAX_BLOCKS_IN_MCU * sizeof(JBLOCK));
				for (int b = 0; b < C_MAX_BLOCKS_IN_MCU; b++) {
					MCU_buffers[h][i][b] = buffer + b;
				}
			}
		}

		for (int i = 0; i < rp_work_count; ++i) {
			for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
				memcpy(&alloc_stats_top[i][j].comp, &cinfos_top[i][j].alloc.stats, sizeof(struct rp_alloc_stats));

				memcpy(&alloc_stats_bot[i][j].comp, &cinfos_bot[i][j].alloc.stats, sizeof(struct rp_alloc_stats));
			}
		}

		// for (int i = 0; i < sizeof(ctxs) / sizeof(*ctxs); ++i) {
		// 	ctxs[i]->cinfo->image_width = 240;
		// 	ctxs[i]->cinfo->image_height = i == 0 ? 400 : 320;
		// 	ctxs[i]->cinfo->input_components = 3;
		// 	ctxs[i]->cinfo->in_color_space = JCS_RGB;

		// 	jpeg_start_compress(ctxs[i]->cinfo, TRUE); /* alloc buffers */
		// 	ctxs[i]->cinfo->global_state == JPEG_CSTATE_START;
		// };
	}

	if (g_nsConfig->rpConfig.threadPriority != rpConfig.threadPriority) {
		rpConfig.threadPriority = g_nsConfig->rpConfig.threadPriority;

		s32 res = svcSetThreadPriority(hRPThreadMain, rpConfig.threadPriority);
		if (res != 0) {
			nsDbgPrint("set main encoding thread priority failed: %08x\n", res);
		}
		if (rpConfig.coreCount >= 2) {
			res = svcSetThreadPriority(hRPThreadAux1, rpConfig.threadPriority);
			if (res != 0) {
				nsDbgPrint("set secondary encoding thread priority failed: %08x\n", res);
			}
		}
	}

	rpConfigChanged = 0;
	__atomic_store_n(&g_nsConfig->rpConfigLock, 0, __ATOMIC_RELAXED);

	currentUpdating = isPriorityTop;
	frameCount[0] = frameCount[1] = 1;
	frameQueued[0] = frameQueued[1] = priorityFactorLogScaled;
	for (int i = 0; i < rp_work_count; ++i) {
		nextScreenCaptured[i] = 0;
	}

	rpLastSendTick = svc_getSystemTick();

	while (1) {
		checkExitFlag();

		if (g_nsConfig->rpConfig.dstAddr == 0) {
			updateCurrentDstAddr(rpConfig.dstAddr);
			rpDstAddrChanged = 1;
		}

		if (rpConfigChanged) {
			break;
		}

		if (!nextScreenCaptured[rp_work_next]) {
			rpCaptureNextScreen(rp_work_next, 1);
			continue;
		}
		nextScreenCaptured[rp_work_next] = 0;

		int ret =
			rpSendFramesStart(0, rp_work_next);

		if (ret == 0)
			rp_work_next = (rp_work_next + 1) % rp_work_count;
	}

	for (int i = 0; i < rp_work_count; ++i) {
		s32 res;
		while (1) {
			res = svc_waitSynchronization1(rp_syn->work[i].sem_end, 1000000000);
			if (res) {
				nsDbgPrint("svc_waitSynchronization1 sem_end (%d) join failed: %d\n", i, res);
				checkExitFlag();
				continue;
			}
			break;
		}

		s32 count;
		res = svc_releaseSemaphore(&count, rp_syn->work[i].sem_end, 1);
		if (res) {
			nsDbgPrint("svc_releaseSemaphore sem_end (%d) join failed: %d\n", i, res);
			// return;
		}
	}
}

static void rpAuxThreadStart(u32 thread_id) {
	int work_next = 0;
	while (!rpResetThreads) {
		checkExitFlag();

		int res;

		struct rp_thread_syn_t *syn = &rp_syn->thread[thread_id];

		res = svc_waitSynchronization1(syn->sem_start, 100000000 /* 1000000000 */);
		if (res) {
			// nsDbgPrint("(%d) svc_waitSynchronization1 sem_start (%d) failed: %d\n", thread_id, work_next, res);
			continue;
		}

		res =
			rpSendFramesStart(thread_id, work_next);

		if (res == 0)
			work_next = (work_next + 1) % rp_work_count;
	}
	svc_exitThread();
}

static void rpNwmThread(u32) {
	while (!rpResetThreads) {
		while (rpTrySendNextBuffer(1) == 0) svc_sleepThread(rpMinIntervalBetweenPacketsInNS);
		int ret = svc_waitSynchronization1(rp_syn->nwmEvent, 100000000);
		if (ret != 0) {
			if (ret != 0x09401BFE) {
				nsDbgPrint("nwmEvent wait error: %08x\n", ret);
				svc_sleepThread(1000000000);
			}
		}
	}
	svc_exitThread();
}

static void rpPortThread(u32);
static void rpThreadStart(void *) {
	u32 i, j, ret;

	for (i = 0; i < rp_work_count; ++i) {
		for (j = 0; j < rp_thread_count; ++j) {
			u8 *nwm_buf = (u8 *)plgRequestMemory(rp_nwm_buffer_size);
			if (!nwm_buf) {
				goto final;
			}
			rpDataBuf[i][j] = nwm_buf + rp_nwm_hdr_size;
			rpPacketBufLast[i][j] = nwm_buf + rp_nwm_buffer_size - rp_packet_data_size;
		}
	}

	for (j = 0; j < 2; ++j) {
		for (i = 0; i < rp_screen_work_count; ++i) {
			imgBuffer[j][i] = (u8 *)plgRequestMemory(rp_img_buffer_size);
			nsDbgPrint("imgBuffer[%d][%d]: %08x\n", j, i, imgBuffer[j][i]);
			if (!imgBuffer[j][i]) {
				goto final;
			}
		}
	}

	for (int i = 0; i < rp_cinfos_count; ++i) {
		if (i == 0)
			cinfo_alloc_sizes[i] = 0x18000;
		else if (i == 1)
			cinfo_alloc_sizes[i] = 0x10000;
		else
			cinfo_alloc_sizes[i] = 0x2000;
	}

	if (rpInitJpegCompress() != 0) {
		nsDbgPrint("rpInitJpegCompress failed\n");
		goto final;
	}

	rp_syn = (void *)plgRequestMemory(0x1000);
	ret = svc_createEvent(&rp_syn->portEvent[0], 0);
	if (ret != 0) {
		nsDbgPrint("svc_createEvent portEvent[0] failed: %08x\n", ret);
		goto final;
	}
	ret = svc_createEvent(&rp_syn->portEvent[1], 0);
	if (ret != 0) {
		nsDbgPrint("svc_createEvent portEvent[1] failed: %08x\n", ret);
		goto final;
	}
	ret = svc_createEvent(&rp_syn->nwmEvent, 0);
	if (ret != 0) {
		nsDbgPrint("svc_createEvent nwmEvent failed: %08x\n", ret);
		goto final;
	}

	u32 *threadSvcStack = (u32 *)plgRequestMemory(STACK_SIZE);
	Handle hThread;
	ret = svc_createThread(&hThread, (void*)rpPortThread, 0, &threadSvcStack[(STACK_SIZE / 4) - 10], 0x10, 1);
	if (ret != 0) {
		nsDbgPrint("Create remote play service thread Failed: %08x\n", ret);
	}

	rpInitDmaHome();

	// kRemotePlayCallback();
	u32 *threadAux1Stack = (u32 *)plgRequestMemory(rpThreadStackSize);
	u32 *threadAux2Stack = (u32 *)plgRequestMemory(rpThreadStackSize);
	u32 *threadNwmStack = (u32 *)plgRequestMemory(STACK_SIZE);

	while (1) {
		if (g_nsConfig->rpConfig.coreCount < 1)
			g_nsConfig->rpConfig.coreCount = 1;
		else if (g_nsConfig->rpConfig.coreCount > rp_thread_count)
			g_nsConfig->rpConfig.coreCount = rp_thread_count;
		if (rpConfig.coreCount != g_nsConfig->rpConfig.coreCount)
			rpConfig.coreCount = g_nsConfig->rpConfig.coreCount;
		rpResetThreads = 0;

		for (int i = 0; i < rp_work_count; ++i) {
			// rp_nwm_work_skip[i] = 0;
			for (int j = 0; j < (int)rpConfig.coreCount; ++j) {
				struct rpDataBufInfo_t *info = &rpDataBufInfo[i][j];
				info->sendPos = info->pos = rpDataBuf[i][j] + rp_data_hdr_size;
				// info->filled = 0;
				info->flag = 0;

				jpeg_progress[i][j] = 0;
			}
			jpeg_rows[i] = 0;
			jpeg_rows_last[i] = 0;
			jpeg_adjusted_rows[i] = 0;
			jpeg_adjusted_rows_last[i] = 0;

			rp_nwm_syn_next[i] = 1;
		}
		// rp_nwm_frame_skipped = 0;
		rp_nwm_work_next = rp_nwm_thread_next = 0;
		rp_work_next = 0;

		for (i = 0; i < rp_work_count; ++i) {
			ret = svc_createSemaphore(&rp_syn->work[i].sem_end, 1, 1);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_end (%d) failed: %08x\n", i, ret);
				goto final;
			}
			ret = svc_createSemaphore(&rp_syn->work[i].sem_nwm, 1, 1);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_nwm (%d) failed: %08x\n", i, ret);
				goto final;
			}
			ret = svc_createSemaphore(&rp_syn->work[i].sem_send, 1, 1);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_send (%d) failed: %08x\n", i, ret);
				goto final;
			}

			rp_syn->work[i].sem_count = 0;
			rp_syn->work[i].sem_set = 0;
		}
		for (j = 0; j < rpConfig.coreCount; ++j) {
			if (j != 0) {
				ret = svc_createSemaphore(&rp_syn->thread[j].sem_start, 0, rp_work_count);
				if (ret != 0) {
					nsDbgPrint("svc_createSemaphore sem_start (%d) failed: %08x\n", i, ret);
					goto final;
				}
			}
			ret = svc_createSemaphore(&rp_syn->thread[j].sem_work, 0, rp_work_count);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_work (%d) failed: %08x\n", i, ret);
				goto final;
			}
		}

		// Handle hThreadAux1;
		if (rpConfig.coreCount >= 2) {

			ret = svc_createThread(&hRPThreadAux1, (void*)rpAuxThreadStart, 1, &threadAux1Stack[(rpThreadStackSize / 4) - 10], RP_THREAD_PRIO_DEFAULT, 3);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Aux Thread Failed: %08x\n", ret);
				goto final;
			}
		}

		Handle hThreadAux2;
		if (rpConfig.coreCount >= 3) {
			ret = svc_createThread(&hThreadAux2, (void*)rpAuxThreadStart, 2, &threadAux2Stack[(rpThreadStackSize / 4) - 10], 0x3f, 1);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Aux Thread Failed: %08x\n", ret);
				goto final;
			}
		}

		Handle hThreadNwm;
		ret = svc_createThread(&hThreadNwm, (void*)rpNwmThread, 0, &threadNwmStack[(STACK_SIZE / 4) - 10], 0x8, 1);
		if (ret != 0) {
			nsDbgPrint("Create remote play network thread Failed: %08x\n", ret);
			goto final;
		}

		while (!rpResetThreads) {
			checkExitFlag();

			rpSendFrames();
		}

		if (rpConfig.coreCount >= 3) {
			svc_waitSynchronization1(hThreadAux2, S64_MAX);
			svc_closeHandle(hThreadAux2);
		}
		if (rpConfig.coreCount >= 2) {
			svc_waitSynchronization1(hRPThreadAux1, S64_MAX);
			svc_closeHandle(hRPThreadAux1);
		}

		svc_waitSynchronization1(hThreadNwm, S64_MAX);
		svc_closeHandle(hThreadNwm);

		for (i = 0; i < rp_work_count; ++i) {
			svc_closeHandle(rp_syn->work[i].sem_end);
			svc_closeHandle(rp_syn->work[i].sem_nwm);
			svc_closeHandle(rp_syn->work[i].sem_send);
		}
		for (j = 0; j < rpConfig.coreCount; ++j) {
			if (j != 0)
				svc_closeHandle(rp_syn->thread[j].sem_start);
			svc_closeHandle(rp_syn->thread[j].sem_work);
		}
	}
final:
	svc_exitThread();
}

// static u8 rp_hdr_tmp[22];

static void printNwMHdr(void) {
	u8 *buf = rpNwmHdr;
	nsDbgPrint("nwm hdr: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x .. .. %02x %02x %02x %02x %02x %02x %02x %02x\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
		buf[14], buf[15], buf[16], buf[17], buf[18], buf[19], buf[20], buf[21]
	);
}

static Handle rpSessionClient = 0;
int rpPortSend(u32 isTop) {
	Handle hClient = rpSessionClient;
	int ret;
	if (hClient == 0) {
		ret = svc_connectToPort(&hClient, "rp:ov");
		if (ret != 0) {
			// nsDbgPrint("svc_connectToPort failed: %08x\n", ret);
			return -1;
		}
		nsDbgPrint("svc_connectToPort: client %08x\n", hClient);
		rpSessionClient = hClient;
	}

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(isTop + 1, 1, 0);
	cmdbuf[1] = getCurrentProcessId();

	ret = svc_sendSyncRequest(hClient);
	if (ret != 0) {
		nsDbgPrint("svc_sendSyncRequest failed: %08x\n", ret);
		return -1;
	}
	return 0;
}

#define rpPortSessionsMax 4
static void rpPortThread(u32) {
	int ret;
	Handle hServer = 0, hClient = 0;
	ret = svcCreatePort(&hServer, &hClient, "rp:ov", rpPortSessionsMax);
	if (ret != 0) {
		nsDbgPrint("svcCreatePort failed: %08x\n", ret);
		svc_exitThread();
	}

	nsDbgPrint("svcCreatePort: server %08x, client %08x\n", hServer, hClient);

	u32 *cmdbuf = getThreadCommandBuffer();

	Handle hSessions[rpPortSessionsMax] = {0};
	Handle handleReply = 0;
	cmdbuf[0] = 0xFFFF0000;
	while (1) {
		Handle hHandles[rpPortSessionsMax + 1];
		u32 hHandlesMap[rpPortSessionsMax + 1];

		int i, hCount = 0;
		for (i = 0; i < rpPortSessionsMax; ++i) {
			if (hSessions[i] != 0) {
				hHandles[hCount] = hSessions[i];
				hHandlesMap[hCount] = i;
				++hCount;
			}
		}
		hHandles[hCount] = hServer;
		hHandlesMap[hCount] = rpPortSessionsMax;
		++hCount;

		s32 handleIndex = -1;
		ret = svcReplyAndReceive(&handleIndex, hHandles, hCount, handleReply);
		if (ret != 0) {
			if (ret == (int)0xC920181A) {
				nsDbgPrint("svcReplyAndReceive handle closed: %08x\n", hHandles[handleIndex]);
				handleReply = 0;
				cmdbuf[0] = 0xFFFF0000;
				svc_closeHandle(hHandles[handleIndex]);
				hSessions[hHandlesMap[handleIndex]] = 0;
				continue;
			}

			handleReply = 0;
			cmdbuf[0] = 0xFFFF0000;
			nsDbgPrint("svcReplyAndReceive error: %08x\n", ret);
			continue;
		}

		if (hHandlesMap[handleIndex] == rpPortSessionsMax) {
			handleReply = 0;
			cmdbuf[0] = 0xFFFF0000;
			Handle hSession;
			ret = svcAcceptSession(&hSession, hServer);
			if (ret != 0) {
				nsDbgPrint("hServer accept error: %08x\n", ret);
				continue;
			}

			for (i = 0; i < rpPortSessionsMax; ++i) {
				if (hSessions[i] == 0) {
					hSessions[i] = hSession;
					break;
				}
			}
			if (i >= rpPortSessionsMax) {
				nsDbgPrint("rpPortSessionsMax exceeded\n");
				svc_closeHandle(hSession);
			}
			continue;
		}

		handleReply = hHandles[handleIndex];
		u32 cmd_id = cmdbuf[0] >> 16;
		u32 norm_param_count = (cmdbuf[0] >> 6) & 0x3F;
		// nsDbgPrint("received command: %04x %d 0x%x\n", cmd_id, norm_param_count, norm_param_count > 0 ? cmdbuf[1] : 0);
		u32 gamePid = norm_param_count >= 1 ? cmdbuf[1] : 0;
		u32 isTop = cmd_id - 1;
		if (isTop > 1) {
			__atomic_store_n(&rpPortGamePid, 0, __ATOMIC_RELAXED);
		} else {
			if (__atomic_load_n(&rpPortGamePid, __ATOMIC_RELAXED) != gamePid)
				__atomic_store_n(&rpPortGamePid, gamePid, __ATOMIC_RELAXED);

			ret = svc_signalEvent(rp_syn->portEvent[isTop]);
			if (ret != 0) {
				nsDbgPrint("svc_signalEvent failed: %08x\n", ret);
			}
		}

		cmdbuf[0] = IPC_MakeHeader(cmd_id, 0, 0);
	}

	if (hServer)
		svc_closeHandle(hServer);
	if (hClient)
		svc_closeHandle(hClient);

	svc_exitThread();
}

static u32 current_nwm_src_addr;
static int nwmValParamCallback(u8* buf, int /*buflen*/) {
	// int i;
	u32* threadStack;
	int ret;
	// Handle hThread;
	// u8 buf_tmp[22];

	/*
	if (buf[31] != 6) {
	nsDbgPrint("buflen: %d\n", buflen);
	for (i = 0; i < buflen; i++) {
	nsDbgPrint("%02x ", buf[i]);
	}
	}*/

	// if (rpInited) {
	// 	return 0;
	// }

	u8 protocol = buf[0x17 + 0x8];
	u16 src_port = *(u16*)(&buf[0x22 + 0x8]);
	u16 dst_port = *(u16*)(&buf[0x22 + 0xa]);

	int tcp_hit = (protocol == 0x6 && src_port == htons(8000));
	int udp_hit = (protocol == 0x11 && src_port == htons(8001) && dst_port == htons(8001));
	if (tcp_hit || udp_hit) {
		// if (rpInited) {
		// 	memcpy(buf_tmp, buf, 22);
		// 	*(u16*)(buf_tmp + 12) = 0;
		// 	if (memcmp(rp_hdr_tmp, buf_tmp, 22) != 0) {
		// 		memcpy(rp_hdr_tmp, buf_tmp, 22);
		// 		printNwMHdr();
		// 	}
		// 	return 0;
		// }

		u32 saddr = *(u32 *)&buf[0x1a + 0x8];
		u32 daddr = *(u32 *)&buf[0x1e + 0x8];
		if (rpInited) {
			u8 needUpdate = 0;

			if ((tcp_hit && rpDstAddrChanged) || udp_hit) {
				updateCurrentDstAddr(daddr);
				rpDstAddrChanged = 0;
				if (daddr != rpConfig.dstAddr) {
					rpConfig.dstAddr = daddr;

					u8 *daddr4 = (u8 *)&daddr;
					nsDbgPrint("remote play updated dst IP: %d.%d.%d.%d\n",
						(int)daddr4[0], (int)daddr4[1], (int)daddr4[2], (int)daddr4[3]
					);

					needUpdate = 1;
				}
			}
			if (current_nwm_src_addr != saddr) {
				current_nwm_src_addr = saddr;
				needUpdate = 1;
			}

			if (needUpdate) {
				memcpy(rpNwmHdr, buf, 0x22 + 8);
				printNwMHdr();
			}


			return 0;
		}
		rpInited = 1;
		// rtDisableHook(&nwmValParamHook);

		u8 *saddr4 = (u8 *)&saddr;
		u8 *daddr4 = (u8 *)&daddr;
		nsDbgPrint("remote play src IP: %d.%d.%d.%d, dst IP: %d.%d.%d.%d\n",
			(int)saddr4[0], (int)saddr4[1], (int)saddr4[2], (int)saddr4[3],
			(int)daddr4[0], (int)daddr4[1], (int)daddr4[2], (int)daddr4[3]
		);

		memcpy(rpNwmHdr, buf, 0x22 + 8);
		// *(u16*)(rpNwmHdr + 12) = 0;
		// memcpy(rp_hdr_tmp, rpNwmHdr, 22);
		printNwMHdr();
		initUDPPacket(rpNwmHdr, PACKET_SIZE);

		updateCurrentDstAddr((rpConfig.dstAddr = daddr));
		rpDstAddrChanged = 0;
		threadStack = (u32 *)plgRequestMemory(rpThreadStackSize);
		ret = svc_createThread(&hRPThreadMain, (void*)rpThreadStart, 0, &threadStack[(rpThreadStackSize / 4) - 10], RP_THREAD_PRIO_DEFAULT, 2);
		if (ret != 0) {
			nsDbgPrint("Create RemotePlay Thread Failed: %08x\n", ret);
		}
	}
	return 0;
}

void rpMain(void) {
	nwmSendPacket = (sendPacketTypedef)g_nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, g_nsConfig->startupInfo[11], (u32)nwmValParamCallback);
	rtEnableHook(&nwmValParamHook);
}

u8 nsIsRemotePlayStarted = 0;

int rpSetExitFlag(void) {
	if (!__atomic_load_n(&nsIsRemotePlayStarted, __ATOMIC_RELAXED))
		return 0;

	Handle hProcess;
	u32 pid = 0x1a;
	int ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		return -1;
	}

	u32 exitFlag = 1;
	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, exitFlag),
		0xffff8001,
		&exitFlag,
		sizeof(exitFlag));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory (2) failed: %08x\n", ret, 0);
		svc_closeHandle(hProcess);
		return -1;
	}

	svc_closeHandle(hProcess);
	return 0;
}

void rpSetGamePid(u32 gamePid) {
	// if (!__atomic_load_n(&nsIsRemotePlayStarted, __ATOMIC_RELAXED))
	// 	return;

	Handle hProcess;
	u32 pid = 0x1a;
	int ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		return;
	}

	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpGamePid),
		0xffff8001,
		&gamePid,
		sizeof(gamePid));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory rpGamePid failed: %08x\n", ret, 0);
	}

	svc_closeHandle(hProcess);
}

static inline int nsRemotePlayControl(RP_CONFIG *config, u8 skipControl) {
	Handle hProcess;
	u32 pid = 0x1a;
	int ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		return -1;
	}

	if (!skipControl) {
		u32 control, controlCount = 1000;
		do {
			ret = copyRemoteMemory(
				0xffff8001,
				&control,
				hProcess,
				(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfigLock),
				sizeof(control));
			if (ret != 0) {
				showDbg("copyRemoteMemory (0) failed: %08x", ret, 0);
				ret = -1;
				goto exit_hProcess;
			}
			if (control) {
				if (!--controlCount) {
					showDbg("rpConfigLock wait timed out", 0, 0);
					ret = -1;
					goto exit_hProcess;
				}
				svc_sleepThread(1000000);
			} else {
				break;
			}
		} while (1);
	}


	if (config->dstPort == 0)
		config->dstPort = rpConfig.dstPort;
	if (config->coreCount == 0)
		config->coreCount = rpConfig.coreCount;
	rpConfig = *config;

	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfig),
		0xffff8001,
		&rpConfig,
		sizeof(rpConfig));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory (1) failed: %08x\n", ret, 0);
		ret = -1;
		goto exit_hProcess;
	}

	u32 control = 1;
	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfigLock),
		0xffff8001,
		&control,
		sizeof(control));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory (2) failed: %08x\n", ret, 0);
		ret = -1;
		goto exit_hProcess;
	}

exit_hProcess:
	svc_closeHandle(hProcess);
	return ret;
}

static int nsInitRemotePlay(RP_CONFIG *config, u32 skipControl) {
	if (!ntrConfig->isNew3DS) {
		showDbg("Remote Play is available on New 3DS only.", 0, 0);
		return -1;
	}

	if (!((config->quality >= 10) && (config->quality <= 100))) {
		nsDbgPrint("out-of-range quality for remote play, limiting to between 10 and 100\n");
		if (config->quality < 10)
			config->quality = 10;
		else if (config->quality > 100)
			config->quality = 100;
	}

	if (config->qosValueInBytes < 512 * 1024)
		config->qosValueInBytes = 512 * 1024;
	else if (config->qosValueInBytes > 5 * 1024 * 1024 / 2)
		config->qosValueInBytes = 5 * 1024 * 1024 / 2;

	config->dstAddr = 0; /* always update from nwm callback */

	if (config->threadPriority == 0) {
		if (rpConfig.threadPriority != 0)
			config->threadPriority = rpConfig.threadPriority;
		else
			config->threadPriority = RP_THREAD_PRIO_DEFAULT;
	} else if (config->threadPriority < 0x8) {
		config->threadPriority = 0x8;
	} else if (config->threadPriority > 0x3f) {
		config->threadPriority = 0x3f;
	}

	if (__atomic_test_and_set(&nsIsRemotePlayStarted, __ATOMIC_RELAXED)) {
		nsDbgPrint("remote play already started, updating params\n");
		return nsRemotePlayControl(config, skipControl);
	}

	Handle hProcess;
	u32 ret = 0;
	u32 pid = 0x1a;
	u32 remotePC = 0x001231d0;
	NS_CONFIG	cfg;

	memset(&cfg, 0, sizeof(NS_CONFIG));
	cfg.startupCommand = NS_STARTCMD_DEBUG;
	// cfg.startupInfo[8] = mode;
	// cfg.startupInfo[9] = quality;
	// cfg.startupInfo[10] = qosValue;

	if (config->dstPort == 0)
		config->dstPort = RP_DST_PORT_DEFAULT;
	if (config->coreCount == 0)
		config->coreCount = rp_thread_count;
	cfg.rpConfig = rpConfig = *config;

	cfg.rpConfigLock = 1;
	cfg.rpCaptureMode = g_nsConfig->rpCaptureMode;

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}

	u8 desiredHeader[16] = { 0x04, 0x00, 0x2D, 0xE5, 0x4F, 0x00, 0x00, 0xEF, 0x00, 0x20, 0x9D, 0xE5, 0x00, 0x10, 0x82, 0xE5 };
	u8 buf[16] = { 0 };

	int isFirmwareSupported = 0;
	int firmwareType = 0;
	for (firmwareType = 0; firmwareType <= 1; firmwareType++) {
		if (firmwareType == 0) {
			cfg.startupInfo[11] = 0x120464; //nwmvalparamhook
			cfg.startupInfo[12] = 0x00120DC8 + 1; //nwmSendPacket
			remotePC = 0x001231d0;
		}
		else if (firmwareType == 1) {
			cfg.startupInfo[11] = 0x120630; //nwmvalparamhook
			cfg.startupInfo[12] = 0x00120f94 + 1; //nwmSendPacket
			remotePC = 0x123394;

		}
		copyRemoteMemory(CURRENT_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, 16);
		svc_sleepThread(100000000);
		if (memcmp(buf, desiredHeader, sizeof(desiredHeader)) == 0) {
			isFirmwareSupported = 1;
			break;
		}
	}
	if (!isFirmwareSupported) {
		nsDbgPrint("remoteplay is not supported on current firmware\n");
		ret = -1;
		goto final;
	}
	setCpuClockLock(3);
	nsDbgPrint("cpu was locked on 804MHz, L2 Enabled\n");
	nsDbgPrint("starting remoteplay...\n");
	ret = nsAttachProcess(hProcess, remotePC, &cfg, 0);

	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
	if (ret) {
		showDbg("Starting remote play failed: %d. Retry maybe...", ret, 0);
		__atomic_clear(&nsIsRemotePlayStarted, __ATOMIC_RELAXED);
	}
	return ret;
}

void nsHandleRemotePlay(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	RP_CONFIG config = {};
	config.currentMode = pac->args[0];
	config.quality = pac->args[1];
	config.qosValueInBytes = pac->args[2];
	if (pac->args[3] == 1404036572) /* guarding magic */
		config.dstPort = pac->args[4];
	nsInitRemotePlay(&config, 0);
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
	saddr.sin_port = htons(8001);

	caddr.sin_family = AF_INET;
	caddr.sin_addr.s_addr = htonl(INADDR_ANY);
	caddr.sin_port = htons(8001);

	if (bind(fd, (struct sockaddr *)&caddr, sizeof(struct sockaddr_in)) < 0) {
		showMsg("Socket bind failed.");
		goto socket_exit;
	}

	u8 data[1] = {0};

	u32 control, controlCount = 10;
	s32 ret;
	Handle hProcess;
	u32 pid = 0x1a;
	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		showDbg("Open remote play process failed: %08x", ret, 0);
		goto socket_exit;
	}

	while (1) {
		svc_sleepThread(500000000);
		if (sendto(fd, data, sizeof(data), 0, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) < 0) {
			// showMsg("UDP send failed.");
			if (!--controlCount) {
				showMsg("Remote play send failed.");
				goto nwm_exit;
			}
		}

		ret = copyRemoteMemory(
			0xffff8001,
			&control,
			hProcess,
			(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfigLock),
			sizeof(control));
		if (ret != 0) {
			showDbg("Getting remote play status failed %08x", ret, 0);
			goto nwm_exit;
		}
		if (control) {
			if (!--controlCount) {
				showMsg("Remote play init timeout.");
				goto nwm_exit;
			}
			// showMsg("Remote play not started yet...");
		} else {
			break;
		}
	}

	// controlCount = 10;
	u32 rpDstAddr;
	while (1) {
		if (sendto(fd, data, sizeof(data), 0, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) < 0) {
			// showMsg("UDP send failed.");
			if (!--controlCount) {
				showMsg("Remote play send failed.");
				goto nwm_exit;
			}
		}

		ret = copyRemoteMemory(
			0xffff8001,
			&rpDstAddr,
			hProcess,
			(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfig) + offsetof(RP_CONFIG, dstAddr),
			sizeof(rpDstAddr));
		if (ret != 0) {
			showDbg("Getting remote play status failed %08x", ret, 0);
			goto nwm_exit;
		}
		if (rpDstAddr != dstAddr) {
			if (!--controlCount) {
				showMsg("Remote play update timeout.");
				goto nwm_exit;
			}
			// showMsg("Remote play not started yet...");
		} else {
			break;
		}

		svc_sleepThread(500000000);
	}

nwm_exit:
	svc_closeHandle(hProcess);
socket_exit:
	closesocket(fd);
}

static void rpDoNFCPatch(void) {
	int pid = 0x1a;
	Handle hProcess;
	int ret;
	if ((ret = svc_openProcess(&hProcess, pid))) {
		showMsg("Failed to open nwm process");
		return;
	}

	u32 addr = 0x0105AE4;
	u16 buf;
	if ((ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr, sizeof(buf)))) {
		showMsg("Failed to protect nwm memory");
		svc_closeHandle(hProcess);
		return;
	}

	if ((ret = copyRemoteMemory(CURRENT_PROCESS_HANDLE, &buf, hProcess, (void *)addr, sizeof(buf)))) {
		showMsg("Failed to read nwm memory");
		svc_closeHandle(hProcess);
		return;
	}

	if (buf == 0x4620) {
		nsDbgPrint("patching NFC (11.4) firm\n");
		addr = 0x0105B00;
	} else {
		nsDbgPrint("patching NFC (<= 11.3) firm\n");
	}

	if ((ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr, sizeof(buf)))) {
		showMsg("Failed to protect nwm memory for write");
		svc_closeHandle(hProcess);
		return;
	}

	buf = 0x4770;
	if ((ret = copyRemoteMemory(hProcess, (void *)addr, CURRENT_PROCESS_HANDLE, &buf, sizeof(buf)))) {
		showMsg("Failed to write nwm memory");
		svc_closeHandle(hProcess);
		return;
	}

	showMsg("NFC patch success");
	svc_closeHandle(hProcess);
	return;
}

static int menu_adjust_value_with_key(int *val, u32 key, int step_1, int step_2) {
	int ret = 0;
	if (key == BUTTON_DL)
		ret = -1;
	else if (key == BUTTON_DR)
		ret = 1;
	else if (key == BUTTON_Y)
		ret = -step_1;
	else if (key == BUTTON_A)
		ret = step_1;
	else if (key == BUTTON_L)
		ret = -step_2;
	else if (key == BUTTON_R)
		ret = step_2;

	if (ret)
		*val += ret;
	return ret;
}

static void ipAddrMenu(u32 *addr) {
	int posDigit = 0;
	int posOctet = 0;
	u32 localaddr = *addr;
	u32 key = 0;
	while (1) {
		blank(0, 0, 320, 240);

		char ipText[50];
		u8 *addr4 = (u8 *)&localaddr;

		xsprintf(ipText, "Viewer IP: %03d.%03d.%03d.%03d", addr4[0], addr4[1], addr4[2], addr4[3]);
		print(ipText, 34, 30, 0, 0, 0);

		int posCaret = posOctet * 4 + posDigit;
		print("^", 34 + (11 + posCaret) * 8, 42, 0, 0, 0);

		updateScreen();
		while((key = waitKey()) == 0);

		if (key == BUTTON_DR) {
			++posDigit;
			if (posDigit >= 3) {
				posDigit = 0;
				++posOctet;
				if (posOctet >= 4) {
					posOctet = 0;
				}
			}
		}
		else if (key == BUTTON_DL) {
			--posDigit;
			if (posDigit < 0) {
				posDigit = 2;
				--posOctet;
				if (posOctet < 0) {
					posOctet = 3;
				}
			}
		}
		else if (key == BUTTON_DU) {
			int addr1 = addr4[posOctet];
			addr1 += posDigit == 0 ? 100 : posDigit == 1 ? 10 : 1;
			if (addr1 > 255) addr1 = 255;
			addr4[posOctet] = addr1;
		}
		else if (key == BUTTON_DD) {
			int addr1 = addr4[posOctet];
			addr1 -= posDigit == 0 ? 100 : posDigit == 1 ? 10 : 1;
			if (addr1 < 0) addr1 = 0;
			addr4[posOctet] = addr1;
		}
		else if (key == BUTTON_A) {
			*addr = localaddr;
			return;
		}
		else if (key == BUTTON_B) {
			return;
		}
	}
}

static void rpChangeFrameCaptureMode(u32 captureMode) {
	if (__atomic_load_n(&nsIsRemotePlayStarted, __ATOMIC_RELAXED)) {
		s32 ret;
		Handle hProcess;

		ret = svc_openProcess(&hProcess, 0x1a);
		if (ret != 0) {
			nsDbgPrint("Open nwm process failed: %08x", ret, 0);
			goto update_final;
		}

		ret = copyRemoteMemory(hProcess,
			(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpCaptureMode),
			CURRENT_PROCESS_HANDLE, &captureMode,
			sizeof(g_nsConfig->rpCaptureMode));
		if (ret != 0) {
			showDbg("Update frame capture mode setting failed: %08x", ret, 0);
			svc_closeHandle(hProcess);
			return;
		}
		svc_closeHandle(hProcess);
	}
update_final:
	g_nsConfig->rpCaptureMode = captureMode;
	return;
}

int remotePlayMenu(u32 localaddr) {
	if (!ntrConfig->isNew3DS) {
		showDbg("Remote Play is available on New 3DS only.", 0, 0);
		return 0;
	}

	u32 daddrCurrent = REG(&g_nsConfig->rpConfig.dstAddr);

	rpConfig.dstAddr = daddrCurrent;
	u32 select = 0;
	RP_CONFIG config = rpConfig;
	u8 *dstAddr4 = (u8 *)&config.dstAddr;

	/* default values */
	if (config.quality == 0) {
		config.currentMode = 0x0103;
		config.quality = 75;
		config.qosValueInBytes = 2 * 1024 * 1024;
		config.dstPort = RP_DST_PORT_DEFAULT;
		config.coreCount = rp_thread_count;
		config.threadPriority = RP_THREAD_PRIO_DEFAULT;
	}
	if (config.dstAddr == 0) {
		config.dstAddr = localaddr;
		dstAddr4[3] = 1;
	}
	rpConfig = config;

	char title[50], titleNotStarted[50];
	u8 *localaddr4 = (u8 *)&localaddr;
	xsprintf(title, "Remote Play: %d.%d.%d.%d", (int)localaddr4[0], (int)localaddr4[1], (int)localaddr4[2], (int)localaddr4[3]);
	xsprintf(titleNotStarted, "Remote Play (Standby): %d.%d.%d.%d", (int)localaddr4[0], (int)localaddr4[1], (int)localaddr4[2], (int)localaddr4[3]);

	while (1) {
		u8 rpStarted = __atomic_load_n(&nsIsRemotePlayStarted, __ATOMIC_RELAXED);
		char *titleCurrent = title;
		if (!rpStarted) {
			titleCurrent = titleNotStarted;
		}

		char coreCountCaption[50];
		xsprintf(coreCountCaption, "Number of Encoding Cores: %d", config.coreCount);

		char encoderPriorityCaption[50];
		xsprintf(encoderPriorityCaption, "Encoder Priority: %d", (int)config.threadPriority);

		char priorityScreenCaption[50];
		xsprintf(priorityScreenCaption, "Priority Screen: %s", (config.currentMode & 0xff00) == 0 ? "Bottom" : "Top");

		char priorityFactorCaption[50];
		xsprintf(priorityFactorCaption, "Priority Factor: %d", (int)(config.currentMode & 0xff));

		char qualityCaption[50];
		xsprintf(qualityCaption, "Quality: %d", (int)config.quality);

		char qosCaption[50];
		u32 qosMB = config.qosValueInBytes / 1024 / 1024;
		u32 qosKB = config.qosValueInBytes / 1024 % 1024 * 125 / 128;
		xsprintf(qosCaption, "QoS: %d.%d MBps", (int)qosMB, (int)qosKB);

		char dstAddrCaption[50];
		xsprintf(dstAddrCaption, "Viewer IP: %d.%d.%d.%d", (int)dstAddr4[0], (int)dstAddr4[1], (int)dstAddr4[2], (int)dstAddr4[3]);

		char dstPortCaption[50];
		xsprintf(dstPortCaption, "Port: %d", (int)config.dstPort);

		char *frameModeCaption =
			g_nsConfig->rpCaptureMode == 3 ? "Capture Mode: memcpy Previous" :
			g_nsConfig->rpCaptureMode == 2 ? "Capture Mode: memcpy Current" :
			g_nsConfig->rpCaptureMode == 1 ? "Capture Mode: DMA Previous" :
			"Capture Mode: DMA Current";

		char *captions[] = {
			coreCountCaption,
			encoderPriorityCaption,
			priorityScreenCaption,
			priorityFactorCaption,
			qualityCaption,
			qosCaption,
			dstAddrCaption,
			dstPortCaption,
			"Apply (Above Options)",
			frameModeCaption,
			"NFC Patch"
		};
		u32 entryCount = sizeof(captions) / sizeof(*captions);

		char *descs[entryCount];
		memset(descs, 0, sizeof(*descs) * entryCount);
		descs[1] = "Higher value means lower priority.\nLower priority means less game/audio\nstutter possibly.";
		descs[9] = "Try memcpy Previous if you experience\nwobble/staircase artifact.";

		u32 key;
		select = showMenuEx2(titleCurrent, entryCount, captions, descs, select, &key);

		if (key == BUTTON_B) {
			return 0;
		}

		else if (select == 0) { /* core count */
			int coreCount = config.coreCount;
			if (key == BUTTON_X)
				coreCount = rpConfig.coreCount;
			else
				menu_adjust_value_with_key(&coreCount, key, 1, 1);

			if (coreCount < 1)
				coreCount = 1;
			else if (coreCount > rp_thread_count)
				coreCount = rp_thread_count;

			if (coreCount != (int)config.coreCount) {
				config.coreCount = coreCount;
			}
		}

		else if (select == 1) { /* encoder priority */
			int threadPriority = config.threadPriority;
			if (key == BUTTON_X)
				threadPriority = rpConfig.threadPriority;
			else
				menu_adjust_value_with_key(&threadPriority, key, 5, 10);

			if (threadPriority < 0x8)
				threadPriority = 0x8;
			else if (threadPriority > 0x3f)
				threadPriority = 0x3f;

			if (threadPriority != (int)config.threadPriority) {
				config.threadPriority = threadPriority;
			}
		}

		else if (select == 2) { /* screen priority */
			u32 mode = !!(config.currentMode & 0xff00);
			if (key == BUTTON_X)
				mode = !!(rpConfig.currentMode & 0xff00);
			else {
				int dummy = 0;
				dummy = menu_adjust_value_with_key(&dummy, key, 1, 1);
				if (dummy) {
					mode = !mode;
				}
			}

			if (mode != !!(config.currentMode & 0xff00)) {
				u32 factor = config.currentMode & 0xff;
				config.currentMode = (mode << 8) | factor;
			}
		}

		else if (select == 3) { /* priority factor */
			int factor = config.currentMode & 0xff;
			if (key == BUTTON_X)
				factor = rpConfig.currentMode & 0xff;
			else
				menu_adjust_value_with_key(&factor, key, 5, 10);

			if (factor < 0)
				factor = 0;
			else if (factor > 0xff)
				factor = 0xff;

			if (factor != (int)(config.currentMode & 0xff)) {
				u32 mode = config.currentMode & 0xff00;
				config.currentMode = mode | factor;
			}
		}

		else if (select == 4) { /* quality */
			int quality = config.quality;
			if (key == BUTTON_X)
				quality = rpConfig.quality;
			else
				menu_adjust_value_with_key(&quality, key, 5, 10);

			if (quality < 10)
				quality = 10;
			else if (quality > 100)
				quality = 100;

			if (quality != (int)config.quality) {
				config.quality = quality;
			}
		}

		else if (select == 5) { /* qos */
			int qos_factor = 128 * 1024;
			int qos = config.qosValueInBytes;
			int qos_remainder = qos % qos_factor;
			qos /= qos_factor;

			if (key == BUTTON_X)
				qos = rpConfig.qosValueInBytes;
			else {
				int ret = menu_adjust_value_with_key(&qos, key, 4, 8);
				if (ret < 0 && qos_remainder > 0) {
					++qos;
				}
				if (qos < 4)
					qos = 4;
				else if (qos > 20)
					qos = 20;
				qos *= qos_factor;
			}

			if (qos != (int)config.qosValueInBytes) {
				config.qosValueInBytes = qos;
			}
		}

		else if (select == 6) { /* dst addr */
			u32 dstAddr = config.dstAddr;
			if (key == BUTTON_X)
				dstAddr = rpConfig.dstAddr;
			else{
				int dummy = 0;
				dummy = menu_adjust_value_with_key(&dummy, key, 1, 1);
				if (dummy) {
					ipAddrMenu(&dstAddr);
				}
			}

			if (dstAddr != config.dstAddr) {
				config.dstAddr = dstAddr;
			}
		}

		else if (select == 7) { /* dst port */
			int dstPort = config.dstPort;
			if (key == BUTTON_X)
				dstPort = rpConfig.dstPort;
			else
				menu_adjust_value_with_key(&dstPort, key, 10, 100);

			if (dstPort < 1024)
				dstPort = 1024;
			else if (dstPort > 65535)
				dstPort = 65535;

			if (dstPort != (int)config.dstPort) {
				config.dstPort = dstPort;
			}
		}

		else if (select == 8 && key == BUTTON_A) { /* apply */
			releaseVideo();

			int updateDstAddr = !rpStarted || rpConfig.dstAddr != config.dstAddr || daddrCurrent == 0;
			u32 daddrUpdated = config.dstAddr;
			nsInitRemotePlay(&config, updateDstAddr);

			if (updateDstAddr) {
				tryInitRemotePlay(daddrUpdated);
			}

			acquireVideo();

			return 1;
		}

		else if (select == 9) { /* frame capture mode */
			int captureMode = g_nsConfig->rpCaptureMode;
			if (key == BUTTON_X)
				captureMode = 0;
			else
				menu_adjust_value_with_key(&captureMode, key, 1, 1);

			if (captureMode < 0)
				captureMode = 3;
			else if (captureMode > 3)
				captureMode = 0;

			if (captureMode != (int)g_nsConfig->rpCaptureMode) {
				releaseVideo();
				rpChangeFrameCaptureMode(captureMode);
				acquireVideo();
			}
		}

		else if (select == 10 && key == BUTTON_A) { /* nfc patch */
			releaseVideo();
			rpDoNFCPatch();
			acquireVideo();
		}
	}
}

void nsHandleSaveFile() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 remain = pac->dataLen;
	char buf[0x220];
	int ret;
	Handle hFile;
	u32 off = 0, tmp;

	if ((ret = nsRecvPacketData((u8 *)buf, 0x200)) < 0) {
		nsDbgPrint("nsHandleSaveFile nsRecvPacketData path error: %d\n", ret);
		return;
	}
	buf[0x200] = 0;
	remain -= 0x200;

	FS_path testPath = (FS_path){ PATH_CHAR, strlen(buf) + 1, buf };
	ret = FSUSER_OpenFileDirectly(fsUserHandle, &hFile, sdmcArchive, testPath, 7, 0);
	if (ret != 0) {
		showDbg("openFile failed: %08x", ret, 0);
		return;
	}

	while (remain) {
		u32 t = 0x2000;
		if (t > remain) {
			t = remain;
		}
		if ((ret = nsRecvPacketData(g_nsCtx->gBuff, t)) < 0) {
			svc_closeHandle(hFile);
			nsDbgPrint("nsHandleSaveFile nsRecvPacketData data error: %d\n", ret);
			return;
		}
		FSFILE_Write(hFile, &tmp, off, g_nsCtx->gBuff, t, 0);

		remain -= t;
		off += t;
	}
	svc_closeHandle(hFile);
	nsDbgPrint("saved to %s successfully\n", buf);
}

int nsFindFreeBreakPoint() {
	int i;
	for (i = 1; i < MAX_BREAKPOINT; i++) {
		if (g_nsCtx->breakPoint[i].type == 0) {
			return i;
		}
	}
	return -1;
}
void nsEnableBreakPoint(int id) {
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	if (bp->isEnabled) {
		nsDbgPrint("bp %d already enabled\n", id);
		return;
	}
	bp->isEnabled = 1;
	if ((bp->type == NS_BPTYPE_CODE) || (bp->type == NS_BPTYPE_CODEONESHOT)) {
		rtEnableHook(&bp->hook);
	}
	nsDbgPrint("bp %d enabled\n", id);
}

void nsDisableBreakPoint(int id) {
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	if (!bp->isEnabled) {
		nsDbgPrint("bp %d already disabled\n", id);
		return;
	}
	bp->isEnabled = 0;
	if ((bp->type == NS_BPTYPE_CODE) || (bp->type == NS_BPTYPE_CODEONESHOT)) {
		rtDisableHook(&bp->hook);
	}
	nsDbgPrint("bp %d disabled\n", id);
}

void nsBreakPointCallback(u32 regs, u32 bpid, u32 /*regs2*/) {
	vu32 resumeFlag;
	NS_BREAKPOINT_STATUS * bpStatus = &(g_nsCtx->breakPointStatus);
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[bpid]);

	//wait if another breakpoint is triggered
	rtAcquireLock(&(g_nsCtx->breakPointTriggerLock));
	if (bp->type == NS_BPTYPE_CODEONESHOT) {
		nsDisableBreakPoint(bpid);
	}

	//update bp status
	rtAcquireLock(&(g_nsCtx->breakPointStatusLock));
	bpStatus->regs = (vu32*)regs;
	bpStatus->bpid = bpid;
	bpStatus->resumeFlag = 0;
	rtReleaseLock(&(g_nsCtx->breakPointStatusLock));

	//wait for resume flag
	while (1) {
		rtAcquireLock(&(g_nsCtx->breakPointStatusLock));
		resumeFlag = bpStatus->resumeFlag;
		rtReleaseLock(&(g_nsCtx->breakPointStatusLock));
		svc_sleepThread(100000000);
		if (resumeFlag) {
			break;
		}
	}
	bpStatus->bpid = 0;
	rtReleaseLock(&(g_nsCtx->breakPointTriggerLock));
}

u32 nsInitCodeBreakPoint(int id) {
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	RT_HOOK* hk = &(bp->hook);
	u32 ret;
	u32 retAddr = 0;
	static const u8 buf[] = {
		0xFF, 0x5F, 0x2D, 0xE9, 0x00, 0x00, 0x0F, 0xE1, 0x01, 0x00, 0x2D, 0xE9, 0x0D, 0x00, 0xA0, 0xE1,
		0x14, 0x10, 0x9F, 0xE5, 0x14, 0x20, 0x9F, 0xE5, 0x32, 0xFF, 0x2F, 0xE1, 0x01, 0x00, 0xBD, 0xE8,
		0x00, 0xF0, 0x29, 0xE1, 0xFF, 0x5F, 0xBD, 0xE8, 0x04, 0xF0, 0x9F, 0xE5
	};
	/*
	STMFD	SP!, {R0-R12, LR}
	MRS     R0, CPSR
	STMFD	SP!, {R0}
	MOV	R0, SP
	LDR	R1,=0xbbcc0001
	LDR	R2,=0xbbcc0002
	BLX	R2
	LDMFD SP!, {R0}
	MSR CPSR, R0
	LDMFD	SP!, {R0-R12, LR}
	LDR	PC, =0xbbcc0003
	*/

	u32 pos = (sizeof(buf) / 4);

	ret = rtCheckRemoteMemoryRegionSafeForWrite(getCurrentProcessHandle(), bp->addr, 8);
	if (ret != 0){
		nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed :%08x\n", ret);
		return ret;
	}

	if (bp->type == NS_BPTYPE_CODE) {
		retAddr = (u32)hk->callCode;
	}

	if (bp->type == NS_BPTYPE_CODEONESHOT) {
		retAddr = bp->addr;
	}

	memcpy(bp->stubCode, buf, sizeof(buf));
	bp->stubCode[pos] = id;
	bp->stubCode[pos + 1] = (u32)nsBreakPointCallback;
	bp->stubCode[pos + 2] = (u32)retAddr;

	rtFlushInstructionCache(bp->stubCode, 16 * 4);

	rtInitHook(&(bp->hook), bp->addr, (u32)(bp->stubCode));
	svc_sleepThread(100000000);
	return 0;
}

void nsInitBreakPoint(int id, u32 addr, int type) {
	u32 ret;
	NS_BREAKPOINT* bp = &(g_nsCtx->breakPoint[id]);
	bp->flag = 0;
	bp->type = type;
	bp->isEnabled = 0;
	bp->addr = addr;

	if ((type == NS_BPTYPE_CODE) || (type == NS_BPTYPE_CODEONESHOT)) {
		ret = nsInitCodeBreakPoint(id);
		if (ret == 0) {
			nsDbgPrint("code breakpoint, id: %d, addr: %08x\n", id, addr);
			nsEnableBreakPoint(id);
			return;
		}
	}
	bp->type = NS_BPTYPE_UNUSED;
	nsDbgPrint("init breakpoint failed.\n");
}

void nsHandleQueryHandle() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 hProcess = 0;
	u32 ret, i;
	u32 pid = pac->args[0];
	u32 buf[400];

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openprocess failed.\n");
		return;
	}
	//showDbg("hprocess: %08x", hProcess, 0);
	u32 pKProcess = kGetKProcessByHandle(hProcess);
	u32 pHandleTable;
	kmemcpy(&pHandleTable, (u8 *)pKProcess + KProcessHandleDataOffset, 4);
	//showDbg("pHandleTable: %08x", pHandleTable, 0);
	kmemcpy(buf, (void *)pHandleTable, sizeof(buf));
	for (i = 0; i < 400; i += 2) {
		u32 ptr = buf[i + 1];
		if (ptr) {
			u32 handleHigh = *((u16*)&(buf[i]));
			u32 handleLow = i / 2;
			u32 handle = (handleHigh << 15) | handleLow;
			nsDbgPrint("h: %08x, p: %08x\n", handle, ptr);
		}
	}
	nsDbgPrint("done");
	svc_closeHandle(hProcess);
}

void nsHandleBreakPoint() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 type = pac->args[0];
	u32 addr = pac->args[1];
	u32 method = pac->args[2];
	u32 bpid = pac->args[0];
	int id;

	if (method == 1) { // add
		id = nsFindFreeBreakPoint();
		nsDbgPrint("freeid: %d\n", id);
		if (id == -1) {
			return;
		}
		nsInitBreakPoint(id, addr, type);
	}
	if (method == 4) { // resume
		nsDbgPrint("set resume flag");
		g_nsCtx->breakPointStatus.resumeFlag = 1;
		g_nsCtx->isBreakPointHandled = 0;
	}

	if (bpid >= MAX_BREAKPOINT) {
		nsDbgPrint("invalid bpid\n");
		return;
	}

	if (method == 2) { // ena
		nsEnableBreakPoint(bpid);
	}

	if (method == 3) { // dis
		nsDisableBreakPoint(bpid);
	}

}

void nsHandleReload() {
	u32 ret, outAddr;
	u32 hFile, size;
	u64 size64;
	char* fileName = "/arm11.bin";
	u32 tmp;

	typedef void(*funcType)();
	g_nsConfig->initMode = 1;
	closesocket(g_nsCtx->hSocket);
	closesocket(g_nsCtx->hListenSocket);
	FS_path testPath = (FS_path){ PATH_CHAR, strlen(fileName) + 1, fileName };
	ret = FSUSER_OpenFileDirectly(fsUserHandle, &hFile, sdmcArchive, testPath, 7, 0);
	if (ret != 0) {
		showDbg("openFile failed: %08x", ret, 0);
		return;
	}
	ret = FSFILE_GetSize(hFile, &size64);
	if (ret != 0) {
		showDbg("FSFILE_GetSize failed: %08x", ret, 0);
		return;
	}
	size = size64;
	size = rtAlignToPageSize(size);
	ret = svc_controlMemory(&outAddr, 0, 0, size, 0x10003, 3);
	if (ret != 0) {
		showDbg("svc_controlMemory failed: %08x", ret, 0);
		return;
	}

	ret = FSFILE_Read(hFile, &tmp, 0, (void *)outAddr, size);
	if (ret != 0) {
		showDbg("FSFILE_Read failed: %08x", ret, 0);
		return;
	}
	ret = protectMemory((void *)outAddr, size);
	if (ret != 0) {
		showDbg("protectMemory failed: %08x", ret, 0);
		return;
	}
	((funcType)outAddr)();
	svc_exitThread();
}

void nsHandleListProcess() {
	u32 pids[100];
	char pname[20];
	u32 tid[4];
	s32 pidCount;
	int i, ret;
	u32 kpobj;

	ret = svc_getProcessList(&pidCount, pids, 100);
	if (ret != 0) {
		nsDbgPrint("getProcessList failed: %08x\n", ret);
		return;
	}
	for (i = 0; i < pidCount; i++) {

		ret = getProcessInfo(pids[i], pname, sizeof(pname), tid, &kpobj);
		if (ret != 0) {
			nsDbgPrint("getProcessInfo failed: %08x\n", ret);
		}
		nsDbgPrint("pid: 0x%08x, pname: %8s, tid: %08x%08x, kpobj: %08x\n", pids[i], pname, tid[1], tid[0], kpobj);
	}
	nsDbgPrint("end of process list.\n");
}
void printMemLayout(Handle hProcess, u32 base, u32 limit) {
	u32 ret, isValid = 0, isLastValid = 0;
	u32 lastAddr = 0;
	while (base < limit) {
		ret = protectRemoteMemory(hProcess, (void*)base, 0x1000);
		isValid = (ret == 0);
		if (isValid != isLastValid) {
			if (isLastValid) {
				nsDbgPrint("%08x - %08x , size: %08x\n", lastAddr, base - 1, base - lastAddr);
			}

			lastAddr = base;
			isLastValid = isValid;
		}
		base += 0x1000;
	}
}
void nsHandleMemLayout() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 pid = pac->args[0];
	/* changing permissions for memory in pid 0 seems to crash 3DS and cause data corruption */
	if (pid == 0)
		return;
	// u32 isLastValid = 0, lastAddr = 0;
	// u32 isValid;
	// u32 base = 0x00100000;
	u32 ret, hProcess;

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}
	nsDbgPrint("valid memregions:\n");
	printMemLayout(hProcess, 0x00100000, 0x1F601000);
	printMemLayout(hProcess, 0x30000000, 0x40000000);
	nsDbgPrint("end of memlayout.\n");
	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}

}

void nsHandleWriteMem() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	int pid = pac->args[0];
	u32 addr = pac->args[1];
	u32 size = pac->args[2];
	// u32 isLastValid = 0, lastAddr = 0;
	// u32 isValid;
	u32 /*base, */remain;
	u32 ret, hProcess;

	if (pid == -1) {
		hProcess = getCurrentProcessHandle();
	}
	else {
		ret = svc_openProcess(&hProcess, pid);
		if (ret != 0) {
			nsDbgPrint("openProcess failed: %08x, pid: %08x\n", ret, pid);
			hProcess = 0;
			goto final;
		}
	}
	if (addr < 0x20000000) {
		ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr, size);
		if (ret != 0) {
			nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x\n", ret, 0);
			goto final;
		}
	}
	remain = size;
	while (remain) {
		u32 tmpsize = 0x1000;
		if (tmpsize > remain) {
			tmpsize = remain;
		}
		nsRecvPacketData(g_nsCtx->gBuff, tmpsize);
		if (pid == -1) {
			if (addr > 0x20000000) {
				kmemcpy((void*)addr, g_nsCtx->gBuff, tmpsize);
			}
			else {
				memcpy((void*)addr, g_nsCtx->gBuff, tmpsize);
			}

		}
		else {
			ret = copyRemoteMemory(hProcess, (void*)addr, 0xffff8001, g_nsCtx->gBuff, tmpsize);
			if (ret != 0) {
				nsDbgPrint("copyRemoteMemory failed: %08x, addr: %08x\n", ret, addr);
			}
		}
		addr += tmpsize;
		remain -= tmpsize;
	}
	nsDbgPrint("finished");
	final:
	if (hProcess != 0) {
		if (pid != -1) {
			svc_closeHandle(hProcess);
		}
	}
}

void nsHandleReadMem() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	int pid = pac->args[0];
	u32 addr = pac->args[1];
	u32 size = pac->args[2];
	// u32 isLastValid = 0, lastAddr = 0;
	// u32 isValid;
	u32 /*base, */remain;
	u32 ret, hProcess;

	if (pid == -1) {
		hProcess = getCurrentProcessHandle();
	}
	else {
		ret = svc_openProcess(&hProcess, pid);
		if (ret != 0) {
			nsDbgPrint("openProcess failed: %08x, pid: %08x\n", ret, pid);
			hProcess = 0;
			goto final;
		}
	}
	if (addr < 0x20000000) {
		ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr, size);
		if (ret != 0) {
			nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x\n", ret, 0);
			goto final;
		}
	}
	pac->dataLen = size;
	nsSendPacketHeader();
	remain = size;
	while (remain) {
		u32 tmpsize = 0x1000;
		if (tmpsize > remain) {
			tmpsize = remain;
		}
		if (pid == -1) {
			if (addr > 0x20000000) {
				kmemcpy(g_nsCtx->gBuff, (void*)addr, tmpsize);
			}
			else{
				memcpy(g_nsCtx->gBuff, (void*)addr, tmpsize);
			}

		}
		else {
			ret = copyRemoteMemory(0xffff8001, g_nsCtx->gBuff, hProcess, (void*)addr, tmpsize);
			if (ret != 0) {
				nsDbgPrint("copyRemoteMemory failed: %08x, addr: %08x\n", ret, addr);
			}
		}
		nsSendPacketData(g_nsCtx->gBuff, tmpsize);
		addr += tmpsize;
		remain -= tmpsize;
	}
	nsDbgPrint("finished");
	final:
	if (hProcess != 0) {
		if (pid != -1) {
			svc_closeHandle(hProcess);
		}

	}
}


u32 nsGetPCToAttachProcess(u32 hProcess) {
	u32 /*handle, */ret;
	// NS_PACKET* pac = &(g_nsCtx->packetBuf);
	// u32 pid = pac->args[0];
	u32 tids[100];
	u32 tidCount, i, j;
	u32 ctx[400];
	u32 pc[100], lr[100];
	u32 addr = 0;


	ret = svc_getThreadList(&tidCount, tids, 100, hProcess);
	if (ret != 0) {
		nsDbgPrint("getThreadList failed: %08x\n", ret);
		return 0;
	}
	for (i = 0; i < tidCount; i++) {
		u32 tid = tids[i];
		memset(ctx, 0x33, sizeof(ctx));
		rtGetThreadReg(hProcess, tid, ctx);
		pc[i] = ctx[15];
		lr[i] = ctx[14];
	}

	nsDbgPrint("recommend pc:\n");
	for (i = 0; i < tidCount; i++) {
		for (j = 0; j < tidCount; j++) {
			if ((i != j) && (pc[i] == pc[j])) break;
		}
		if (j >= tidCount) {
			nsDbgPrint("%08x\n", pc[i]);
			if (!addr) addr = pc[i];
		}
	}

	nsDbgPrint("recommend lr:\n");
	for (i = 0; i < tidCount; i++) {
		for (j = 0; j < tidCount; j++) {
			if ((i != j) && (lr[i] == lr[j])) break;
		}
		if (j >= tidCount) {
			nsDbgPrint("%08x\n", lr[i]);
			if (!addr) addr = lr[i];
		}
	}
	return addr;
}

void nsHandleListThread() {
	u32 /*handle, */ret;
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	Handle hProcess;
	u32 pid = pac->args[0];
	u32 tids[100];
	u32 tidCount, i/*, j*/;
	u32 ctx[400];
	// u32 hThread;
	// u32 pKThread;
	// u32 pContext;

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}
	ret = svc_getThreadList(&tidCount, tids, 100, hProcess);
	if (ret != 0) {
		nsDbgPrint("getThreadList failed: %08x\n", ret);
		goto final;
	}
	for (i = 0; i < tidCount; i++) {
		u32 tid = tids[i];
		nsDbgPrint("tid: 0x%08x\n", tid);
		memset(ctx, 0x33, sizeof(ctx));
		rtGetThreadReg(hProcess, tid, ctx);
		nsDbgPrint("pc: %08x, lr: %08x\n", ctx[15], ctx[14]);
		nsDbgPrint(
			"%08x ""%08x ""%08x ""%08x "
			"%08x ""%08x ""%08x ""%08x "
			"%08x ""%08x ""%08x ""%08x "
			"%08x ""%08x ""%08x ""%08x "
			"%08x ""%08x ""%08x ""%08x "
			"%08x ""%08x ""%08x ""%08x "
			"%08x ""%08x ""%08x ""%08x "
			"%08x ""%08x ""%08x ""%08x\n",
			ctx[0], ctx[1], ctx[2], ctx[3],
			ctx[4], ctx[5], ctx[6], ctx[7],
			ctx[8], ctx[9], ctx[10], ctx[11],
			ctx[12], ctx[13], ctx[14], ctx[15],
			ctx[16], ctx[17], ctx[18], ctx[19],
			ctx[20], ctx[21], ctx[22], ctx[23],
			ctx[24], ctx[25], ctx[26], ctx[27],
			ctx[28], ctx[29], ctx[30], ctx[31]);
		// svc_closeHandle(hThread);

	}
	nsGetPCToAttachProcess(hProcess);

	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
}



u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg, int cfgHasNtrConfig) {
	u32 size = 0;
	u32* buf = 0;
	u32 baseAddr = NS_CONFIGURE_ADDR;
	u32 stackSize = 0x4000;
	u32 totalSize;
	u32 ret/*, handle, outAddr*/;
	u32 tmp[20];
	u32 arm11StartAddress;

	arm11StartAddress = baseAddr + 0x1000 + stackSize;
	buf = (u32*)arm11BinStart;
	size = arm11BinSize;
	nsDbgPrint("buf: %08x, size: %08x\n", buf, size);


	if (!buf) {
		showDbg("arm11 not loaded", 0, 0);
		return -1;
	}

	totalSize = size + stackSize + 0x1000;
	// if (sysRegion) {
	// 	// allocate buffer to remote memory
	// 	ret = mapRemoteMemoryInSysRegion(hProcess, baseAddr, totalSize, 3);
	// }
	// else {
		// allocate buffer to remote memory
		ret = mapRemoteMemory(hProcess, baseAddr, totalSize);
	// }

	if (ret != 0) {
		showDbg("mapRemoteMemory failed: %08x", ret, 0);
	}
	// set rwx
	ret = protectRemoteMemory(hProcess, (void*)baseAddr, totalSize);
	if (ret != 0) {
		showDbg("protectRemoteMemory failed: %08x", ret, 0);
		goto final;
	}
	// load arm11.bin code at arm11StartAddress
	ret = copyRemoteMemory(hProcess, (void*)arm11StartAddress, arm11BinProcess, buf, size);
	if (ret != 0) {
		showDbg("copyRemoteMemory(1) failed: %08x", ret, 0);
		goto final;
	}

	if (remotePC == 0) {
		remotePC = nsGetPCToAttachProcess(hProcess);
	}
	nsDbgPrint("remotePC: %08x\n", remotePC, 0);
	if (remotePC == 0) {
		goto final;
	}
	ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, remotePC, 8);
	if (ret != 0) {
		showDbg("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x", ret, 0);
		goto final;
	}



	cfg->initMode = NS_INITMODE_FROMHOOK;

	// store original 8-byte code
	ret = copyRemoteMemory(0xffff8001, &(cfg->startupInfo[0]), hProcess, (void*)remotePC, 8);
	if (ret != 0) {
		showDbg("copyRemoteMemory(3) failed: %08x", ret, 0);
		goto final;
	}
	cfg->startupInfo[2] = remotePC;
	if (!cfgHasNtrConfig)
		memcpy(&(cfg->ntrConfig), ntrConfig, sizeof(NTR_CONFIG));
	// copy cfg structure to remote process
	ret = copyRemoteMemory(hProcess, (void*)baseAddr, 0xffff8001, cfg, sizeof(NS_CONFIG));
	if (ret != 0) {
		showDbg("copyRemoteMemory(2) failed: %08x", ret, 0);
		goto final;
	}

	// write hook instructions to remote process
	tmp[0] = 0xe51ff004;
	tmp[1] = arm11StartAddress;
	ret = copyRemoteMemory(hProcess, (void*)remotePC, 0xffff8001, &tmp, 8);
	if (ret != 0) {
		showDbg("copyRemoteMemory(4) failed: %08x", ret, 0);
		goto final;
	}

	final:
	return ret;
}

void nsHandleAttachProcess() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	Handle hProcess;
	u32 ret;
	u32 pid = pac->args[0];
	u32 remotePC = pac->args[1];
	NS_CONFIG	cfg;

	memset(&cfg, 0, sizeof(NS_CONFIG));
	if (pid == 2) {
		cfg.startupCommand = NS_STARTCMD_INJECTPM;
	}
	else {
		cfg.startupCommand = NS_STARTCMD_DEBUG;
	}
	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}
	if (nsAttachProcess(hProcess, remotePC, &cfg, 0) == 0)
		nsDbgPrint("will listen at port %d \n", pid + 5000);
	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
	return;
}

void nsPrintRegs(u32* regs) {
	nsDbgPrint("cpsr:%08x lr:%08x sp:%08x\n", regs[0], regs[14], (u32)(regs)+14 * 4);
	nsDbgPrint(
		"r0:%08x " "r1:%08x " "r2:%08x " "r3:%08x "
		"r4:%08x " "r5:%08x " "r6:%08x " "r7:%08x "
		"r8:%08x " "r9:%08x " "r10:%08x " "r11:%08x\n",
		regs[1], regs[2], regs[3], regs[4],
		regs[5], regs[6], regs[7], regs[8],
		regs[9], regs[10], regs[11], regs[12]);
}

void nsUpdateDebugStatus() {
	NS_BREAKPOINT_STATUS  bpStatus;
	u32 isActived = 0;

	rtAcquireLock(&(g_nsCtx->breakPointStatusLock));
	memcpy(&bpStatus, &(g_nsCtx->breakPointStatus), sizeof(NS_BREAKPOINT_STATUS));
	rtReleaseLock(&(g_nsCtx->breakPointStatusLock));

	if ((bpStatus.bpid != 0) && (bpStatus.resumeFlag != 1)) {
		isActived = 1;
	}

	if ((isActived) && (!g_nsCtx->isBreakPointHandled)) {
		nsDbgPrint("breakpoint %d hit\n", bpStatus.bpid);
		nsPrintRegs((u32*)bpStatus.regs);
	}
	g_nsCtx->isBreakPointHandled = isActived;
}

void nsHandlePacket() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	g_nsCtx->remainDataLen = pac->dataLen;
	if (pac->cmd == NS_CMD_SAYHELLO) {
		disp(100, 0x100ff00);
		nsDbgPrint("hello");
		return;
	}

	if (pac->cmd == NS_CMD_HEARTBEAT) {
		rtAcquireLock(&(g_nsConfig->debugBufferLock));
		nsDbgLn();
		pac->dataLen = g_nsConfig->debugPtr;
		nsSendPacketHeader();
		if (pac->dataLen > 0) {
			nsSendPacketData(g_nsConfig->debugBuf, pac->dataLen);
		}
		g_nsConfig->debugPtr = 0;
		rtReleaseLock(&(g_nsConfig->debugBufferLock));
		return;
	}

	if (pac->cmd == NS_CMD_SAVEFILE) {
		nsHandleSaveFile();
		return;
	}

	if (pac->cmd == NS_CMD_RELOAD) {
		nsHandleReload();
		return;
	}

	if (pac->cmd == NS_CMD_LSPROCESS) {
		nsHandleListProcess();
		return;
	}

	if (pac->cmd == NS_CMD_LSTHREAD) {
		nsHandleListThread();
		return;
	}

	if (pac->cmd == NS_CMD_ATTACHPROCESS) {
		nsHandleAttachProcess();
		return;
	}

	if (pac->cmd == NS_CMD_MEMLAYOUT) {
		nsHandleMemLayout();
		return;
	}

	if (pac->cmd == NS_CMD_READMEM) {
		nsHandleReadMem();
		return;
	}

	if (pac->cmd == NS_CMD_WRITEMEM) {
		nsHandleWriteMem();
		return;
	}

	if (pac->cmd == NS_CMD_BREAKPOINT) {
		nsHandleBreakPoint();
		return;
	}

	if (pac->cmd == NS_CMD_QUERYHANDLE) {
		nsHandleQueryHandle();
		return;
	}

	if (pac->cmd == NS_CMD_REMOTEPLAY) {
		if (g_nsCtx->listenPort == 8000) {
			nsHandleRemotePlay();
		}
		return;
	}
}


void nsMainLoop() {
	while (1) {
		s32 listen_sock, ret, tmp, sockfd;
		struct sockaddr_in addr;

		while (1) {
			checkExitFlag();
			listen_sock = socket(AF_INET, SOCK_STREAM, 0);
			if (listen_sock > 0) {
				break;
			}
			svc_sleepThread(1000000000);
		}
		g_nsCtx->hListenSocket = listen_sock;

		addr.sin_family = AF_INET;
		addr.sin_port = rtIntToPortNumber(g_nsCtx->listenPort);
		addr.sin_addr.s_addr = INADDR_ANY;

		ret = bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
		if (ret < 0) {
			showDbg("bind failed: %08x", ret, 0);
			goto end_listen;
		}

		if (g_nsCtx->listenPort == 8000) {
			tmp = fcntl(listen_sock, F_GETFL);
			fcntl(listen_sock, F_SETFL, tmp | O_NONBLOCK);
		}

		ret = listen(listen_sock, 1);
		if (ret < 0) {
			showDbg("listen failed: %08x", ret, 0);
			goto end_listen;
		}

		while (1) {
			checkExitFlag();
			sockfd = accept(listen_sock, NULL, NULL);
			g_nsCtx->hSocket = sockfd;
			if (sockfd < 0) {
				int serr = SOC_GetErrno();
				if (serr == -EAGAIN || serr == -EWOULDBLOCK) {
					svc_sleepThread(100000000);
					continue;
				}
				break;
			}

			tmp = fcntl(sockfd, F_GETFL, 0);
			fcntl(sockfd, F_SETFL, tmp & ~O_NONBLOCK);

			while (1) {
				ret = rtRecvSocket(sockfd, (u8*)&(g_nsCtx->packetBuf), sizeof(NS_PACKET));
				if (ret != sizeof(NS_PACKET)) {
					nsDbgPrint("rtRecvSocket failed: %08x", ret, 0);
					break;
				}
				NS_PACKET* pac = &(g_nsCtx->packetBuf);
				if (pac->magic != 0x12345678) {
					nsDbgPrint("broken protocol: %08x, %08x", pac->magic, pac->seq);
					break;
				}
				nsUpdateDebugStatus();
				nsHandlePacket();
				pac->magic = 0;
			}
			closesocket(sockfd);
		}

end_listen:
		closesocket(listen_sock);
	}
}

void nsThreadStart() {
	nsMainLoop();
	svc_exitThread();
}

void nsInitDebug(void) {
	// xfunc_out = (void*)nsDbgPutc;
	rtInitLock(&(g_nsConfig->debugBufferLock));
	g_nsConfig->debugBuf = (u8*)(NS_CONFIGURE_ADDR + 0x0900);
	g_nsConfig->debugBufSize = 0xf0;
	g_nsConfig->debugPtr = 0;
	g_nsConfig->debugReady = 1;
}

void nsInit(void) {
	u32 socuSharedBufferSize;
	u32 bufferSize;
	u32 ret, outAddr;
	u32* threadStack;
	u32 handle;
	socuSharedBufferSize = 0x10000;
	bufferSize = socuSharedBufferSize + rtAlignToPageSize(sizeof(NS_CONTEXT)) + STACK_SIZE;
	u32 base = 0x06f00000;



	//showDbg("nsInit", 0, 0);

	/*
	while(1) {
	ret = svc_controlMemory(&outAddr, base, 0, heapSize, 0x3, 3);
	base += 0x1000;
	if (ret == 0) {
	break;
	}
	}*/

	if (g_nsConfig->initMode == NS_INITMODE_FROMRELOAD) {
		outAddr = base;
	}
	else {
		// if (g_nsConfig->initMode == NS_INITMODE_FROMHOOK) {
		// 	// ret = controlMemoryInSysRegion(&outAddr, base, 0, bufferSize, NS_DEFAULT_MEM_REGION + 3, 3);
		// 	u32 mem_region;
		// 	ret = getMemRegion(&mem_region, CURRENT_PROCESS_HANDLE);
		// 	if (ret != 0) {
		// 		showDbg("getMemRegion failed: %08x", ret, 0);
		// 		return;
		// 	}
			ret = svc_controlMemory(&outAddr, base, 0, bufferSize, /* mem_region + */3, 3);
		// }
		// else {
		// 	ret = svc_controlMemory(&outAddr, base, 0, bufferSize, NS_DEFAULT_MEM_REGION + 3, 3);
		// }
		if (ret != 0) {
			showDbg("svc_controlMemory failed: %08x", ret, 0);
			return;
		}
	}
	ret = rtCheckRemoteMemoryRegionSafeForWrite(getCurrentProcessHandle(), base, bufferSize);
	if (ret != 0) {
		showDbg("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x", ret, 0);
	}

	if (!srvHandle) {
		initSrv();
	}
	if (g_nsConfig->hSOCU == 0) {
		ret = SOC_Initialize((u32*)outAddr, socuSharedBufferSize);
		if (ret != 0) {
			showDbg("SOC_Initialize failed: %08x", ret, 0);
			return;
		}
		g_nsConfig->hSOCU = SOCU_handle;
	}
	else {
		SOCU_handle = g_nsConfig->hSOCU;
	}


	g_nsCtx = (void*)(outAddr + socuSharedBufferSize);

	memset(g_nsCtx, 0, sizeof(NS_CONTEXT));

	g_nsCtx->listenPort = 8000;
	if (g_nsConfig->initMode == NS_INITMODE_FROMHOOK) {
		g_nsCtx->listenPort = 5000 + getCurrentProcessId();
	}

	rtInitLock(&(g_nsCtx->breakPointTriggerLock));
	rtInitLock(&(g_nsCtx->breakPointStatusLock));

	g_nsConfig->debugBuf = g_nsCtx->debugBuf;
	g_nsConfig->debugPtr = 0;
	g_nsConfig->debugBufSize = DEBUG_BUFFER_SIZE;

	if (g_nsConfig->initMode == NS_INITMODE_FROMHOOK) {
		// hook entry point when attaching process
		nsInitBreakPoint(1, 0x00100000, NS_BPTYPE_CODEONESHOT);
	}
	if ((g_nsConfig->initMode == NS_INITMODE_FROMHOOK) || (g_nsConfig->initMode == NS_INITMODE_FROMRELOAD)) {
		nsMainLoop();
		return;
	}

	threadStack = (u32*)(outAddr + socuSharedBufferSize + rtAlignToPageSize(sizeof(NS_CONTEXT)));

	u32 affinity = 0x10;
	ret = svc_createThread(&handle, (void*)nsThreadStart, 0, &threadStack[(STACK_SIZE / 4) - 10], affinity, 0xFFFFFFFE);
	if (ret != 0) {
		showDbg("svc_createThread failed: %08x", ret, 0);
		return;
	}

}