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

NS_CONTEXT* g_nsCtx = 0;
NS_CONFIG* g_nsConfig;

#define rp_thread_count (3)
#define rp_nwm_thread_id (0)

#define rp_work_count (2)
/* 2 for number of screens (top/bot) */
#define rp_cinfos_count (rp_work_count * rp_thread_count * 2)

j_compress_ptr cinfos[rp_cinfos_count];
static cinfo_alloc_sizes[rp_cinfos_count];
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

u32 rpMinIntervalBetweenPacketsInTick = 0;
static u32 rpThreadStackSize = 0x10000;
static u8 rpResetThreads = 0;

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

void  rpFree(j_common_ptr cinfo, void* ptr)
{
	nsDbgPrint("free: %08x\n", ptr);
	return;
}


void nsDbgPutc(char ch) {

	if (g_nsConfig->debugPtr >= g_nsConfig->debugBufSize) {
		return;
	}
	(g_nsConfig->debugBuf)[g_nsConfig->debugPtr] = ch;
	g_nsConfig->debugPtr++;
}

void nsDbgPrintShared(const char* fmt, ...) {
	va_list arp;

	va_start(arp, fmt);
	if (g_nsConfig) {
		if (g_nsConfig->debugReady) {
			rtAcquireLock(&(g_nsConfig->debugBufferLock));
			xfvprintf(nsDbgPutc, fmt, arp);
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
			rtReleaseLock(&(g_nsConfig->debugBufferLock));
		}
	}

	va_end(arp);
}

int nsSendPacketHeader() {

	g_nsCtx->remainDataLen = g_nsCtx->packetBuf.dataLen;
	rtSendSocket(g_nsCtx->hSocket, (u8*)&(g_nsCtx->packetBuf), sizeof(NS_PACKET));
}

int nsSendPacketData(u8* buf, u32 size) {
	if (g_nsCtx->remainDataLen < size) {
		if (buf != g_nsConfig->debugBuf) /* avoid dead-lock */
			showDbg("send remain < size: %08x, %08x", g_nsCtx->remainDataLen, size);
		else
			showMsg("send remain < size");
		return -1;
	}
	g_nsCtx->remainDataLen -= size;
	rtSendSocket(g_nsCtx->hSocket, buf, size);
}

int nsRecvPacketData(u8* buf, u32 size) {
	if (g_nsCtx->remainDataLen < size) {
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

struct rp_work_syn_t {
	Handle sem_end, sem_start, sem_work, sem_nwm;
	int sem_count;
	u8 sem_set;
} *rp_work_syn[rp_work_count];

RT_HOOK nwmValParamHook;

#define rp_nwm_hdr_size (0x2a + 8)
#define rp_data_hdr_size (4)

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
	u16 *sdata = data;

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
	u8 frameCount;

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
		for (int j = 0; j < rpConfig.coreCount; ++j) {
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
	ctx->frameCount = 0;
	return ret;
}

static u32 rpLastSendTick = 0;

static u8 rp_nwm_work_skip[rp_work_count];
static int rp_nwm_work_next, rp_nwm_thread_next;
static u8 rpDataBufHdr[rp_work_count][rp_data_hdr_size];

static struct rpDataBufInfo_t {
	u8 *sendPos, *pos;
	// int filled;
	int flag;
} rpDataBufInfo[rp_work_count][rp_thread_count];

int rpDataBufFilled(struct rpDataBufInfo_t *info, u8 **pos, int *flag) {
	*pos = __atomic_load_n(&info->pos, __ATOMIC_RELAXED);
	*flag = __atomic_load_n(&info->flag, __ATOMIC_RELAXED);
	return info->sendPos < *pos || *flag;
}

void rpReadyNwm(int thread_id, int work_next, int id, int isTop) {
	while (1) {
		s32 res = svc_waitSynchronization1(rp_work_syn[work_next]->sem_nwm, 1000000000);
		if (res) {
			nsDbgPrint("(%d) svc_waitSynchronization1 sem_nwm (%d) failed: %d\n", thread_id, work_next, res);
			checkExitFlag();
			continue;
		}
		break;
	}

	for (int j = 0; j < rpConfig.coreCount; ++j) {
		struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][j];
		info->sendPos = info->pos = rpDataBuf[work_next][j] + rp_data_hdr_size;
		// info->filled = 0;
		info->flag = 0;
	}

	rpDataBufHdr[work_next][0] = id;
	rpDataBufHdr[work_next][1] = isTop;
	rpDataBufHdr[work_next][2] = 2;
	rpDataBufHdr[work_next][3] = 0;
}

void rpSendNextBuffer(u32 nextTick, u8 *data_buf_pos, int data_buf_flag) {
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
		return;

	if (size < rp_packet_data_size && thread_id != rpConfig.coreCount - 1) {
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
				return;
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
			res = svc_releaseSemaphore(&count, rp_work_syn[work_next]->sem_nwm, 1);
			if (res) {
				nsDbgPrint("svc_releaseSemaphore sem_nwm (%d) failed: %d\n", work_next, res);
			}

			work_next = (work_next + 1) % rp_work_count;
			rp_nwm_work_next = work_next;
		}

		return;
	}

	memcpy(rp_nwm_buf, rpNwmHdr, rp_nwm_hdr_size);
	packet_len = initUDPPacket(rp_nwm_buf, size + rp_data_hdr_size);
	memcpy(rp_nwm_packet_buf, rpDataBufHdr[work_next], rp_data_hdr_size);
	if (thread_done && thread_id == rpConfig.coreCount - 1) {
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
			res = svc_releaseSemaphore(&count, rp_work_syn[work_next]->sem_nwm, 1);
			if (res) {
				nsDbgPrint("svc_releaseSemaphore sem_nwm (%d) failed: %d\n", work_next, res);
			}

			work_next = (work_next + 1) % rp_work_count;
			rp_nwm_work_next = work_next;
		}
	}
}

int rpTrySendNextBuffer(int work_flush) {
	int work_next = rp_nwm_work_next;
	int thread_id = rp_nwm_thread_next;

#if 1
	if (rp_nwm_work_skip[work_next]) {
		rp_nwm_work_skip[work_next] = 0;

		rp_nwm_thread_next = 0;

		s32 count, res;
		res = svc_releaseSemaphore(&count, rp_work_syn[work_next]->sem_nwm, 1);
		if (res) {
			nsDbgPrint("svc_releaseSemaphore sem_nwm (%d) failed: %d\n", work_next, res);
		}

		work_next = (work_next + 1) % rp_work_count;
		rp_nwm_work_next = work_next;

		return 0;
	}
#endif

flush:
	struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][thread_id];

	u8 *data_buf_pos;
	int data_buf_flag;

	// if (!__atomic_load_n(&info->filled, __ATOMIC_CONSUME))
	// 	return;
	if (!rpDataBufFilled(info, &data_buf_pos, &data_buf_flag))
		return -1;

	u32 nextTick = svc_getSystemTick();
	s32 tickDiff = (s32)nextTick - (s32)rpLastSendTick;
	if (tickDiff < (s32)rpMinIntervalBetweenPacketsInTick) {
		if (work_flush) {
			u32 sleepValue = (((s32)rpMinIntervalBetweenPacketsInTick - tickDiff) * 1000) / SYSTICK_PER_US;
			svc_sleepThread(sleepValue);
			rpSendNextBuffer(svc_getSystemTick(), data_buf_pos, data_buf_flag);
		}
	} else {
		rpSendNextBuffer(nextTick, data_buf_pos, data_buf_flag);
	}
	if (work_flush) {
		if (work_next != rp_nwm_work_next)
			return 0;

		if (thread_id != rp_nwm_thread_next)
			thread_id = rp_nwm_thread_next;

		goto flush;
	}
	return 0;
}

void rpSendBuffer(j_compress_ptr cinfo, u8* buf, u32 size, u32 flag) {
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
		__atomic_store_n(&info->flag, flag, __ATOMIC_RELAXED);
	}

	// __atomic_store_n(&info->filled, 1, __ATOMIC_RELEASE);
}

#if 0
void rpSendNextBuffer(u32 nextTick) {
	nwmSendPacket(rpNwmBuffer[rpDataBufSendPos], packetLen[rpDataBufSendPos]);
	--rpDataBufFilled;
	rpDataBufSendPos = (rpDataBufSendPos + 1) % rp_nwm_send_buffer_count;
	rpLastSendTick = nextTick;
}

/* flag is only set when called from term_destination (jpeg_term_destination) */
void rpSendBuffer(j_compress_ptr cinfo, u8* buf, u32 size, u32 flag) {
	s32 tickDiff;
	u32 nextTick;
	u32 sleepValue;
	s32 res;

	u8 *rpDataBufCur = rpDataBuf[rpDataBufPos];
	{
		if (flag) {
			rpDataBufHdr[1] |= flag;
		}
		if (size == 0) {
			rpDataBufCur[rp_data_hdr_size] = 0;
			size = 1;
		}
		packetLen[rpDataBufPos] = initUDPPacket(rpNwmBuffer[rpDataBufPos], size + 4);
		*(u32 *)rpDataBufCur = *(u32 *)rpDataBufHdr;
		++rpDataBufHdr[3];

		++rpDataBufFilled;
		nextTick = svc_getSystemTick();
		tickDiff = (s32)nextTick - (s32)rpLastSendTick;
		if (tickDiff < (s32)rpMinIntervalBetweenPacketsInTick) {
			if (rpDataBufFilled == rp_nwm_send_buffer_count) {
				sleepValue = (((s32)rpMinIntervalBetweenPacketsInTick - tickDiff) * 1000) / SYSTICK_PER_US;
				svc_sleepThread(sleepValue);
				rpSendNextBuffer(svc_getSystemTick());
			}
		} else {
			rpSendNextBuffer(nextTick);
		}

		rpDataBufPos = (rpDataBufPos + 1) % rp_nwm_send_buffer_count;
		rpDataBufCur = rpDataBuf[rpDataBufPos];

		cinfo->client_data = rpDataBufCur + 4;
	}
}
#endif

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

		cinfo->alloc.buf = plgRequestMemorySpecifyRegion(cinfo_alloc_sizes[i], 1);
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
static u32 frameCount;
static u32 isPriorityTop;
static u32 priorityFactor;
static int nextScreenCaptured[rp_work_count];
static int nextScreenSynced[rp_work_count] = { 0 };

static BLIT_CONTEXT blit_context[rp_work_count];
static int rpConfigChanged;
static int rpDstAddrChanged;

void rpCaptureNextScreen(int work_next, int wait_sync);

#if 0
struct rp_work_t {
	int work_next;
	j_compress_ptr cinfo;
	u8 *src;
	u32 pitch;

	JDIMENSION mcu_row;
	JDIMENSION in_rows_blk;
	JDIMENSION in_rows_blk_half;
	JDIMENSION in_rows_blk_half_n;
	JDIMENSION mcu_n;
	int prep_reading_done_state;

	// enum rp_nwm_state_t {
	// 	rp_nwm_ready,
	// 	rp_nwm_sending,
	// } nwm;
	// int nwm_done_total;

	enum rp_mcu_state_t {
		rp_mcu_empty,
		rp_mcu_writing,
		rp_mcu_full,
		rp_mcu_reading,
	} mcu[rp_mcu_buffer_count];
	int mcu_write, mcu_read;
	int mcu_done_total;

	enum rp_prep_state_t {
		rp_prep_empty,
		rp_prep_writing = rp_prep_empty + rp_rows_blk_halves_count,
		rp_prep_reading,
	} prep[rp_prep_buffer_count];
	u8 prep_mcu[rp_prep_buffer_count][240 / rp_jpeg_samp_factor / DCTSIZE];
	int prep_write, prep_read, prep_mcu_empty[rp_prep_buffer_count];

	int in_prep[rp_prep_buffer_count];
	int in_read, in_done;
} *rp_work[rp_work_count];

struct rp_task_t {
	enum rp_task_which_t {
		rp_task_which_nwm,
		rp_task_which_mcu,
		rp_task_which_prep,
	} which;
	union {
		struct rp_task_nwm_t {
			int mcu;
		} nwm;
		struct rp_task_mcu_t {
			int mcu;
			int prep;
			int prep_mcu;
		} mcu;
		struct rp_task_prep_t {
			int prep;
			int in;
		} prep;
	};
};

#define RP_ERR_SYNC (-1)
#define RP_ERR_DONE (-2)
#define RP_ERR_ARG (-3)
#define RP_ERR_AGAIN (-4)

int rpJpegTryAcquireTask(struct rp_work_t *work, struct rp_work_syn_t *syn, struct rp_task_t *task, int thread_id) {
	int ret = 0, ret_nwm = 0;

	// nwm
	// if (work->nwm == rp_nwm_ready) {
	if (thread_id == rp_nwm_thread_id) {
		if (work->mcu[work->mcu_read] == rp_mcu_full) {
			task->which = rp_task_which_nwm;
			// mcu read index
			task->nwm.mcu = work->mcu_read;

			// nwm sending
			// work->nwm = rp_nwm_sending;
			// mcu reading
			work->mcu[work->mcu_read] = rp_mcu_reading;
			// mcu read index inc
			work->mcu_read = (work->mcu_read + 1) % rp_mcu_buffer_count;

			goto final;
		}
	}

	ret_nwm = work->mcu[work->mcu_read] != rp_mcu_full;

	// mcu
	if (work->mcu[work->mcu_write] == rp_mcu_empty) {
		if (
			work->prep[work->prep_read] >= rp_prep_reading &&
			work->prep[work->prep_read] < work->prep_reading_done_state
		) {
			int prep_mcu = work->prep[work->prep_read] - rp_prep_reading;

			task->which = rp_task_which_mcu;
			// mcu write index
			task->mcu.mcu = work->mcu_write;
			// prep read index
			task->mcu.prep = work->prep_read;
			// prep read sub index
			task->mcu.prep_mcu = prep_mcu;

			// mcu writing
			work->mcu[work->mcu_write] = rp_mcu_writing;
			// mcu write index inc
			work->mcu_write = (work->mcu_write + 1) % rp_mcu_buffer_count;
			// prep read sub index inc
			++work->prep[work->prep_read];
			// prep read sub index check
			if (work->prep[work->prep_read] == work->prep_reading_done_state) {
				// prep read index inc
				work->prep_read = (work->prep_read + 1) % rp_prep_buffer_count;
			}

			goto final;
		}
	}

	// prep
	if (work->prep[work->prep_write] < rp_prep_writing) {
		if (work->in_read < work->in_rows_blk_half_n) {
			task->which = rp_task_which_prep;
			// prep write index
			task->prep.prep = work->prep_write;
			// in read index
			task->prep.in = work->in_read;

			// prep write sub index inc
			++work->prep[work->prep_write];
			// prep write sub index check
			if (work->prep[work->prep_write] == rp_prep_writing) {
				// prep write index inc
				work->prep_write = (work->prep_write + 1) % rp_prep_buffer_count;
			}
			// in read index inc
			++work->in_read;

			goto final;
		}
	}

	if (work->mcu_done_total < work->mcu_n)
		ret = RP_ERR_AGAIN;
	else
		ret = RP_ERR_DONE;

final:

	if (thread_id == rp_nwm_thread_id) {
		if (ret == 0) {
			s32 res = svc_signalEvent(syn->event);
			if (res) {
				nsDbgPrint("svc_signalEvent failed: %d\n", res);
				ret = RP_ERR_SYNC;
			}
		} else if (ret == RP_ERR_AGAIN) {
			s32 res = svc_clearEvent(syn->event_nwm);
			if (res) {
				nsDbgPrint("svc_clearEvent event_nwm failed: %d\n", res);
				ret = RP_ERR_SYNC;
			}
		}
	} else {
		if (ret == 0) {
			s32 res = svc_signalEvent(syn->event);
			if (res) {
				nsDbgPrint("svc_signalEvent failed: %d\n", res);
				ret = RP_ERR_SYNC;
			}
		} else if (ret == RP_ERR_AGAIN) {
			s32 res = svc_clearEvent(syn->event);
			if (res) {
				nsDbgPrint("svc_clearEvent failed: %d\n", res);
				ret = RP_ERR_SYNC;
			}
		}

		if (ret_nwm == 0) {
			s32 res = svc_signalEvent(syn->event_nwm);
			if (res) {
				nsDbgPrint("svc_signalEvent event_nwm failed: %d\n", res);
				ret = RP_ERR_SYNC;
			}
		}
	}

	return ret;
}

int rpJpegAcquireTask(struct rp_work_t *work, struct rp_work_syn_t *syn, struct rp_task_t *task, int thread_id) {
	int ret = 0, res;

	if ((res = svc_waitSynchronization1(syn->mutex, 1000000000))) {
		nsDbgPrint("rpJpegAcquireTask mutex wait failed: %d\n", res);
		return RP_ERR_SYNC;
	}

	ret = rpJpegTryAcquireTask(work, syn, task, thread_id);

	if (res = svc_releaseMutex(syn->mutex)) {
		nsDbgPrint("rpJpegAcquireTask mutex release failed: %d\n", res);
		return RP_ERR_SYNC;
	}
	return ret;
}

int rpJpegReleaseTask(struct rp_work_t *work, struct rp_work_syn_t *syn, struct rp_task_t *task, int thread_id) {
	int ret = 0, res;

	if ((res = svc_waitSynchronization1(syn->mutex, 1000000000))) {
		nsDbgPrint("rpJpegReleaseTask mutex wait failed: %d\n", res);
		return RP_ERR_SYNC;
	}

	switch (task->which) {
		case rp_task_which_nwm: {
			struct rp_task_nwm_t *nwm = &task->nwm;

			// nwm ready
			// work->nwm = rp_nwm_ready;
			// mcu read done
			work->mcu[nwm->mcu] = rp_mcu_empty;

			// nwm done cond tracking
			// ++work->nwm_done_total;
		} break;

		case rp_task_which_mcu: {
			struct rp_task_mcu_t *mcu = &task->mcu;

			// mcu read ready
			work->mcu[mcu->mcu] = rp_mcu_full;
			int prep_mcu = mcu->prep_mcu;
			// prep read sub done
			work->prep_mcu[mcu->prep][prep_mcu] = 1;
			// prep read sub done base check
			if (work->prep_mcu_empty[mcu->prep] == prep_mcu) {
				// prep read sub done next check
				do {
					++prep_mcu;
					if (prep_mcu == work->mcu_row) {
						// prep read done
						work->prep[mcu->prep] = rp_prep_empty;
						// in prep ready
						work->in_prep[mcu->prep] = 0;

						break;
					}
				} while (work->prep_mcu[mcu->prep][prep_mcu]);
				// prep read sub done inc
				work->prep_mcu_empty[mcu->prep] = prep_mcu;
			}

			// mcu done cond tracking
			++work->mcu_done_total;
		} break;

		case rp_task_which_prep: {
			struct rp_task_prep_t *prep = &task->prep;

			// in prep inc
			++work->in_prep[prep->prep];
			// in prep check
			if (work->in_prep[prep->prep] == rp_rows_blk_halves_count) {
				// prep read ready
				work->prep[prep->prep] = rp_prep_reading;
				// prep read sub ready
				memset(work->prep_mcu[prep->prep], 0, sizeof(work->prep_mcu[prep->prep]));
				// prep read sub base ready
				work->prep_mcu_empty[prep->prep] = 0;
			}
			// signal for capturing next frame
			++work->in_done;
		} break;

		default:
			ret = RP_ERR_ARG;
			break;
	}

	if (ret == 0) {
		ret = rpJpegTryAcquireTask(work, syn, task, thread_id);
	}

final:
	/* only thread on core 2 access graphics registers */
	int capture_next = thread_id == 0 && work->in_done == work->in_rows_blk_half_n;
	if (capture_next)
		++work->in_done; /* capture only once */

	if (res = svc_releaseMutex(syn->mutex)) {
		nsDbgPrint("rpJpegReleaseTask mutex release failed: %d\n", res);
		ret = RP_ERR_SYNC;
	}

	/* early capture next frame to avoid left side of screen glitching due to running ahead of dma
	   (at the cost of slightly increased latency) */
	if (capture_next) {
		// nsDbgPrint("capture_next ");
		rpCaptureNextScreen((work->work_next + 1) % rp_work_count);
	}

	return ret;
}

int rpJpegRunTask(struct rp_work_t *work, struct rp_task_t *task, int thread_id) {
	int ret = 0;

	switch (task->which) {
		case rp_task_which_nwm: {
			struct rp_task_nwm_t *nwm = &task->nwm;

			jpeg_encode_mcu_huff(work->cinfo, MCU_buffers[work->work_next][nwm->mcu]);
			// nsDbgPrint("%d nwm: mcu %d\n", thread_id, nwm->mcu);
		} break;

		case rp_task_which_mcu: {
			struct rp_task_mcu_t *mcu = &task->mcu;

			jpeg_compress_data(work->cinfo, prep_buffers[work->work_next][mcu->prep], MCU_buffers[work->work_next][mcu->mcu], mcu->prep_mcu);
			// nsDbgPrint("%d mcu: mcu %d, prep %d, prep_mcu %d\n", thread_id, mcu->mcu, mcu->prep, mcu->prep_mcu);
		} break;

		case rp_task_which_prep: {
			struct rp_task_prep_t *prep = &task->prep;

			JSAMPROW input_buf[work->in_rows_blk_half];
			for (int i = 0, j = work->in_rows_blk_half * prep->in; i < work->in_rows_blk_half; ++i, ++j)
				input_buf[i] = work->src + j * work->pitch;
			jpeg_pre_process(work->cinfo, input_buf, color_buffers[work->work_next][thread_id], prep_buffers[work->work_next][prep->prep], prep->in % 2);
			// nsDbgPrint("%d prep: prep %d, in %d\n", thread_id, prep->prep, prep->in);
		} break;

		default:
			ret = RP_ERR_ARG;
			break;
	}

final:
	return ret;
}

void rpJPEGCompress(struct rp_work_t *work, struct rp_work_syn_t *syn, int thread_id) {
	while (1) {
		struct rp_task_t task;
		int task_was_nwm = 0;
		int ret;
		ret = rpJpegAcquireTask(work, syn, &task, thread_id);
again:
		if (ret) {
			if (ret == RP_ERR_AGAIN) {
				// nsDbgPrint("(%d) svc_waitSynchronization1 event (%d):\n", thread_id, work->work_next);
				if (thread_id == rp_nwm_thread_id) {
					// s32 out;
					// Handle handles[] = {syn->event, syn->event_nwm};
					// s32 res = svc_waitSynchronizationN(&out, handles, sizeof(handles) / sizeof(*handles), 0, 1000000000);
					// if (res) {
					// 	nsDbgPrint("(%d) svc_waitSynchronizationN event, event_nwm (%d) failed: %d (%d)\n", thread_id, work->work_next, res, work->nwm_done_total);
					// 	break;
					// }
					s32 res = svc_waitSynchronization1(syn->event_nwm, 1000000000);
					if (res) {
						nsDbgPrint("(%d) svc_waitSynchronization1 event_nwm (%d) failed: %d\n", thread_id, work->work_next, res);
						break;
					}
				} else {
					s32 res = svc_waitSynchronization1(syn->event, 1000000000);
					if (res) {
						nsDbgPrint("(%d) svc_waitSynchronization1 event (%d) failed: %d\n", thread_id, work->work_next, res);
						break;
					}
				}
				continue;
			}
			if (ret != RP_ERR_DONE)
				nsDbgPrint("rpJpegAcquireTask failed\n");
			break;
		}
		task_was_nwm = task.which == rp_task_which_nwm;
next:
		if (thread_id == rp_nwm_thread_id && !task_was_nwm && rpDataBufFilled) {
			u32 nextTick = svc_getSystemTick();
			s32 tickDiff = (s32)nextTick - (s32)rpLastSendTick;
			if (tickDiff >= (s32)rpMinIntervalBetweenPacketsInTick)
				rpSendNextBuffer(nextTick);
		}

		if (rpJpegRunTask(work, &task, thread_id)) {
			nsDbgPrint("rpJpegRunTask failed\n");
			break;
		}
		task_was_nwm = task.which == rp_task_which_nwm;
		if ((ret = rpJpegReleaseTask(work, syn, &task, thread_id))) {
			if (ret == RP_ERR_AGAIN)
				goto again;
			if (ret != RP_ERR_DONE)
				nsDbgPrint("rpJpegReleaseTask failed\n");
			break;
		}
		goto next;
		// svc_sleepThread(10000000);
	}
}
#endif

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
	j_max = j_max < cinfo->image_height ? j_max : cinfo->image_height;
	int j_max_half = in_rows_blk * (irow_start + irow_count / 2);
	j_max_half = j_max_half < cinfo->image_height ? j_max_half : cinfo->image_height;

	int j_start = in_rows_blk * irow_start;
	if (j_max_half == j_start)
		__atomic_store_n(capture_next, 1, __ATOMIC_RELAXED);
	rpTryCaptureNextScreen(&need_capture_next, capture_next, work_next);

	for (int j = j_start, progress = 0; j < j_max;) {
		for (int i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 0);

		for (int i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 1);

		if (j_max_half == j)
			__atomic_store_n(capture_next, 1, __ATOMIC_RELAXED);
		rpTryCaptureNextScreen(&need_capture_next, capture_next, work_next);

		JBLOCKROW *MCU_buffer = MCU_buffers[work_next][thread_id];
		for (int k = 0; k < cinfo->MCUs_per_row; ++k) {
			jpeg_compress_data(cinfo, output_buf, MCU_buffer, k);
			jpeg_encode_mcu_huff(cinfo, MCU_buffer);

			if (thread_id == rp_nwm_thread_id) {
				rpTrySendNextBuffer(0);
			}
		}

		__atomic_store_n(&jpeg_progress[work_next][thread_id], ++progress, __ATOMIC_RELAXED);
	}
}

void rpReadyWork(BLIT_CONTEXT* ctx, int work_next) {
	// nsDbgPrint("rpReadyWork %d\n", work_next);
	u32 i;
	j_compress_ptr cinfo;

	if (ctx->format >= 3) {
		svc_sleepThread(1000000000);
		return;
	}

	int work_prev = work_next == 0 ? rp_work_count - 1 : work_next - 1;
	int progress[rpConfig.coreCount];
	for (int j = 0; j < rpConfig.coreCount; ++j) {
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
				for (int j = 0; j < rpConfig.coreCount - 1; ++j) {
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

	for (int j = 0; j < rpConfig.coreCount; ++j) {
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
		ctx->irow_count[j] = j == rpConfig.coreCount - 1 ? jpeg_adjusted_rows_last[work_next] : cinfo->restart_in_rows;
	}
	ctx->capture_next = 0;

#if 0
	struct rp_work_t *work = rp_work[work_next];
	// struct rp_work_syn_t *syn = rp_work_syn[work_next];

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

	if (thread_id != rpConfig.coreCount - 1) {
		jpeg_emit_marker(cinfo, JPEG_RST0 + thread_id);
	} else {
		jpeg_write_file_trailer(cinfo);
	}
	jpeg_term_destination(cinfo);
}

void rpKernelCallback(int isTop) {
	u32 ret;
	u32 fbP2VOffset = 0xc0000000;
	u32 current_fb;

	if (isTop) {
		tl_fbaddr[0] = REG(IoBasePdc + 0x468);
		tl_fbaddr[1] = REG(IoBasePdc + 0x46c);
		tl_format = REG(IoBasePdc + 0x470);
		tl_pitch = REG(IoBasePdc + 0x490);
		current_fb = REG(IoBasePdc + 0x478);
		current_fb &= 1;
		tl_current = tl_fbaddr[current_fb];

		int full_width = !(tl_format & (7 << 4));
		/* for full-width top screen (800x240), output every other column */
		if (full_width)
			tl_pitch *= 2;
	} else {
		bl_fbaddr[0] = REG(IoBasePdc + 0x568);
		bl_fbaddr[1] = REG(IoBasePdc + 0x56c);
		bl_format = REG(IoBasePdc + 0x570);
		bl_pitch = REG(IoBasePdc + 0x590);
		current_fb = REG(IoBasePdc + 0x578);
		current_fb &= 1;
		bl_current = bl_fbaddr[current_fb];
	}
}

Handle rpHDma[rp_work_count], rpHandleHome, rpHandleGame;
u32 rpGameFCRAMBase = 0;

void rpInitDmaHome() {
	u32 dmaConfig[20] = { 0 };
	svc_openProcess(&rpHandleHome, 0xf);

}

void rpCloseGameHandle(void) {
	if (rpHandleGame) {
		svc_closeHandle(rpHandleGame);
		rpHandleGame = 0;
		rpGameFCRAMBase = 0;
	}
}

Handle rpGetGameHandle() {
	int i, res;
	Handle hProcess;
	u32 pids[100];
	u32 pidCount;

	int lock;
	if ((lock = __atomic_load_n(&g_nsConfig->rpGameLock, __ATOMIC_RELAXED))) {
		__atomic_store_n(&g_nsConfig->rpGameLock, 0, __ATOMIC_RELAXED);
		rpCloseGameHandle();
		if (lock < 0) {
			svc_sleepThread(1000000000);
		}
	}

	if (rpHandleGame == 0) {
#if 0
		for (i = 0x28; i < 0x38; i++) {
			int ret = svc_openProcess(&hProcess, i);
			if (ret == 0) {
				nsDbgPrint("game process: %x\n", i);
				rpHandleGame = hProcess;
				break;
			}
		}
#else
		res = svc_getProcessList(&pidCount, pids, 100);
		if (res == 0) {
			for (i = 0; i < pidCount; ++i) {
				if (pids[i] < 0x28)
					continue;

				res = svc_openProcess(&hProcess, pids[i]);
				if (res == 0) {
					rpHandleGame = hProcess;
				}
				break;
			}
		}
#endif
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
			svc_closeHandle(rpHandleGame);
			rpHandleGame = 0;
			return 0;
		}

		nsDbgPrint("game process: pid 0x%04x, fcram 0x%08x\n", pids[i], rpGameFCRAMBase);
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
	u32 dest = imgBuffer[isTop][imgBuffer_work_next[isTop]];
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

	int ret;
	s32 res;

	if (bufSize > rp_img_buffer_size) {
		nsDbgPrint("bufSize exceeds imgBuffer: %d (%d)\n", bufSize, rp_img_buffer_size);
		goto final;
	}

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, dest, bufSize);
	if (rpHDma[work_next]) {
		svc_closeHandle(rpHDma[work_next]);
		rpHDma[work_next] = 0;
	}

	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		res = svc_startInterProcessDma(&rpHDma[work_next], CURRENT_PROCESS_HANDLE,
			dest, hProcess, 0x1F000000 + (phys - 0x18000000), bufSize, &dmaConfig);
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
				dest, hProcess, rpGameFCRAMBase + (phys - 0x20000000), bufSize, &dmaConfig);
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

void rpCaptureNextScreen(int work_next, int wait_sync) {
	if (__atomic_load_n(&g_nsConfig->rpConfigLock, __ATOMIC_RELAXED)) {
		rpConfigChanged = 1;
		return;
	}

	currentUpdating = isPriorityTop;
	frameCount += 1;
	if (priorityFactor != 0) {
		if (frameCount % (priorityFactor + 1) == 0) {
			currentUpdating = !isPriorityTop;
		}
	}

	struct rp_work_syn_t *syn = rp_work_syn[work_next];
	s32 res;
	if (!nextScreenSynced[work_next]) {
		res = svc_waitSynchronization1(syn->sem_end, wait_sync ? 1000000000 : 0);
		if (res) {
			if (wait_sync || res < 0)
				nsDbgPrint("svc_waitSynchronization1 sem_end (%d) failed: %d\n", work_next, res);
			return;
		}

		nextScreenSynced[work_next] = 1;
	}

	rpKernelCallback(currentUpdating);
	int captured = rpCaptureScreen(work_next, currentUpdating) == 0;
	if (captured) {
		nextScreenCaptured[work_next] = captured;
		nextScreenSynced[work_next] = 0;
		__atomic_clear(&syn->sem_set, __ATOMIC_RELAXED);

		s32 count;
		res = svc_releaseSemaphore(&count, syn->sem_start, rpConfig.coreCount - 1);
		if (res) {
			nsDbgPrint("svc_releaseSemaphore sem_start failed: %d\n", res);
			return;
		}
	}
}

static int rp_work_next = 0;
static u8 rp_skip_frame[rp_work_count] = { 0 };

int rpSendFramesStart(int thread_id, int work_next) {
	BLIT_CONTEXT *ctx = &blit_context[work_next];
	struct rp_work_syn_t *syn = rp_work_syn[work_next];

	if (thread_id == rp_nwm_thread_id && rp_nwm_work_next == work_next) {
		rpTrySendNextBuffer(1);
	}

	u8 skip_frame = 0;
	if (!__atomic_test_and_set(&syn->sem_set, __ATOMIC_RELAXED)) {
		int format_changed = 0;
		if (currentUpdating) {
			// send top
			for (int j = 0; j < rpConfig.coreCount; ++j) {
				ctx->cinfos[j] = &cinfos_top[work_next][j];
				ctx->cinfos_alloc_stats[j] = &alloc_stats_top[work_next][j];
			}

			format_changed = rpCtxInit(ctx, 400, 240, tl_format, imgBuffer[1][imgBuffer_work_next[1]]);
			ctx->id = (u8)currentTopId;
			ctx->isTop = 1;
		} else {
			// send bottom
			for (int j = 0; j < rpConfig.coreCount; ++j) {
				ctx->cinfos[j] = &cinfos_bot[work_next][j];
				ctx->cinfos_alloc_stats[j] = &alloc_stats_bot[work_next][j];
			}

			format_changed = rpCtxInit(ctx, 320, 240, bl_format, imgBuffer[0][imgBuffer_work_next[0]]);
			ctx->id = (u8)currentBottomId;
			ctx->isTop = 0;
		}
		rpReadyWork(ctx, work_next);
		rpReadyNwm(thread_id, work_next, ctx->id, ctx->isTop);

		s32 res = svc_waitSynchronization1(rpHDma[work_next], 1000000000);
		if (res) {
			nsDbgPrint("(%d) svc_waitSynchronization1 rpHDma (%d) failed: %d\n", thread_id, work_next, res);
		}

		int imgBuffer_work_prev = imgBuffer_work_next[ctx->isTop];
		if (imgBuffer_work_prev == 0)
			imgBuffer_work_prev = rp_screen_work_count - 1;
		else
			--imgBuffer_work_prev;

		skip_frame = !format_changed && memcmp(ctx->src, imgBuffer[ctx->isTop][imgBuffer_work_prev], ctx->width * ctx->src_pitch) == 0;
		__atomic_store_n(&rp_skip_frame[work_next], skip_frame, __ATOMIC_RELAXED);
		if (!skip_frame) {
			imgBuffer_work_next[ctx->isTop] = (imgBuffer_work_next[ctx->isTop] + 1) % rp_screen_work_count;
			currentUpdating ? ++currentTopId : ++currentBottomId;
		}

		s32 count;
		res = svc_releaseSemaphore(&count, syn->sem_work, rpConfig.coreCount - 1);
		if (res) {
			nsDbgPrint("(%d) svc_releaseSemaphore sem_work (%d) failed: %d\n", thread_id, work_next, res);
			goto final;
		}
	} else {
		while (1) {
			s32 res = svc_waitSynchronization1(syn->sem_work, 1000000000);
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
	else if (thread_id == rp_nwm_thread_id) {
		rp_nwm_work_skip[work_next] = 1;
		// while (rp_nwm_work_next != work_next) {
		// 	if (rpTrySendNextBuffer(1)) {
		// 		u32 sleepValue = rpMinIntervalBetweenPacketsInTick * 1000 / SYSTICK_PER_US;
		// 		svc_sleepThread(sleepValue);
		// 	}
		// }
		// s32 count, res;
		// res = svc_releaseSemaphore(&count, rp_work_syn[work_next]->sem_nwm, 1);
		// if (res) {
		// 	nsDbgPrint("svc_releaseSemaphore sem_nwm (%d) failed: %d\n", work_next, res);
		// }
	}

final:
	if (__atomic_add_fetch(&syn->sem_count, 1, __ATOMIC_RELAXED) == rpConfig.coreCount) {
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

	{
		isPriorityTop = 1;
		priorityFactor = 0;
		u32 mode = (g_nsConfig->rpConfig.currentMode & 0xff00) >> 8;
		u32 factor = (g_nsConfig->rpConfig.currentMode & 0xff);
		if (mode == 0) {
			isPriorityTop = 0;
		}
		priorityFactor = factor;
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
			for (int j = 0; j < rpConfig.coreCount; ++j) {
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
			for (int i = 0; i < sizeof(*prep_buffers) / sizeof(**prep_buffers); ++i) {
				for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
					prep_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
						240, (JDIMENSION)(MAX_SAMP_FACTOR * DCTSIZE));
				}
			}
			for (int i = 0; i < sizeof(*color_buffers) / sizeof(**color_buffers); ++i) {
				for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
					color_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
						240, (JDIMENSION)MAX_SAMP_FACTOR);
				}
			}
			for (int i = 0; i < sizeof(*MCU_buffers) / sizeof(**MCU_buffers); ++i) {
				JBLOCKROW buffer = (JBLOCKROW)jpeg_alloc_large((j_common_ptr)cinfos[h], JPOOL_IMAGE, C_MAX_BLOCKS_IN_MCU * sizeof(JBLOCK));
				for (int b = 0; b < C_MAX_BLOCKS_IN_MCU; b++) {
					MCU_buffers[h][i][b] = buffer + b;
				}
			}
		}

		for (int i = 0; i < rp_work_count; ++i) {
			for (int j = 0; j < rpConfig.coreCount; ++j) {
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

	rpConfigChanged = 0;
	__atomic_store_n(&g_nsConfig->rpConfigLock, 0, __ATOMIC_RELAXED);

	currentUpdating = isPriorityTop;
	frameCount = 0;
	for (int i = 0; i < rp_work_count; ++i) {
		nextScreenCaptured[i] = 0;
	}

	rpLastSendTick = svc_getSystemTick();

	while (1) {
		checkExitFlag();

		if (g_nsConfig->rpConfig.dstAddr == 0) {
			g_nsConfig->rpConfig.dstAddr = rpConfig.dstAddr;
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

		int ret = rpSendFramesStart(0, rp_work_next);

		// if (ret == 0)
			rp_work_next = (rp_work_next + 1) % rp_work_count;
	}

	for (int i = 0; i < rp_work_count; ++i) {
		s32 res;
		while (1) {
			res = svc_waitSynchronization1(rp_work_syn[i]->sem_end, 1000000000);
			if (res) {
				nsDbgPrint("svc_waitSynchronization1 sem_end (%d) join failed: %d\n", i, res);
				checkExitFlag();
				continue;
			}
			break;
		}

		s32 count;
		res = svc_releaseSemaphore(&count, rp_work_syn[i]->sem_end, 1);
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

		struct rp_work_syn_t *syn = rp_work_syn[work_next];

		res = svc_waitSynchronization1(syn->sem_start, 1000000000);
		if (res) {
			nsDbgPrint("(%d) svc_waitSynchronization1 sem_start (%d) failed: %d\n", thread_id, work_next, res);
			continue;
		}

		res = rpSendFramesStart(thread_id, work_next);
		// if (res == 0)
			work_next = (work_next + 1) % rp_work_count;
	}
	svc_exitThread();
}

static void rpThreadStart(void *arg) {
	u32 i, j, ret;
	u32 remainSize;

	for (i = 0; i < rp_work_count; ++i) {
		for (j = 0; j < rp_thread_count; ++j) {
			u8 *nwm_buf = plgRequestMemory(rp_nwm_buffer_size);
			if (!nwm_buf) {
				goto final;
			}
			rpDataBuf[i][j] = nwm_buf + rp_nwm_hdr_size;
			rpPacketBufLast[i][j] = nwm_buf + rp_nwm_buffer_size - rp_packet_data_size;
		}
	}

	for (j = 0; j < 2; ++j) {
		for (i = 0; i < rp_screen_work_count; ++i) {
			imgBuffer[j][i] = plgRequestMemory(rp_img_buffer_size);
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

	for (i = 0; i < rp_work_count; ++i) {
		// rp_work[i] = plgRequestMemory(0x1000);
		rp_work_syn[i] = plgRequestMemory(0x1000);
	}

	rpInitDmaHome();

	// kRemotePlayCallback();

	while (1) {
		if (g_nsConfig->rpConfig.coreCount < 1)
			g_nsConfig->rpConfig.coreCount = 1;
		else if (g_nsConfig->rpConfig.coreCount > rp_thread_count)
			g_nsConfig->rpConfig.coreCount = rp_thread_count;
		if (rpConfig.coreCount != g_nsConfig->rpConfig.coreCount)
			rpConfig.coreCount = g_nsConfig->rpConfig.coreCount;
		rpResetThreads = 0;

		for (int i = 0; i < rp_work_count; ++i) {
			rp_nwm_work_skip[i] = 0;

			for (int j = 0; j < rpConfig.coreCount; ++j) {
				struct rpDataBufInfo_t *info = &rpDataBufInfo[i][j];
				info->sendPos = info->pos = rpDataBuf[i][j] + rp_data_hdr_size;
				// info->filled = 0;
				info->flag = 0;

				jpeg_progress[i][j] = 0;
			}
		}
		rp_nwm_work_next = rp_nwm_thread_next = 0;
		rp_work_next = 0;

		for (i = 0; i < rp_work_count; ++i) {
			ret = svc_createSemaphore(&rp_work_syn[i]->sem_end, 1, 1);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_end (%d) failed: %08x\n", i, ret);
				goto final;
			}
			ret = svc_createSemaphore(&rp_work_syn[i]->sem_start, 0, rpConfig.coreCount - 1);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_start (%d) failed: %08x\n", i, ret);
				goto final;
			}
			ret = svc_createSemaphore(&rp_work_syn[i]->sem_work, 0, rpConfig.coreCount - 1);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_work (%d) failed: %08x\n", i, ret);
				goto final;
			}
			ret = svc_createSemaphore(&rp_work_syn[i]->sem_nwm, 1, 1);
			if (ret != 0) {
				nsDbgPrint("svc_createSemaphore sem_nwm (%d) failed: %08x\n", i, ret);
				goto final;
			}

			rp_work_syn[i]->sem_count = 0;
			rp_work_syn[i]->sem_set = 0;
		}

		Handle hThreadAux1;
		if (rpConfig.coreCount >= 2) {
			u32 *threadStack = plgRequestMemory(rpThreadStackSize);
			ret = svc_createThread(&hThreadAux1, (void*)rpAuxThreadStart, 1, &threadStack[(rpThreadStackSize / 4) - 10], 0x10, 3);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Aux Thread Failed: %08x\n", ret);
				goto final;
			}
		}

		Handle hThreadAux2;
		if (rpConfig.coreCount >= 3) {
			u32 *threadStack = plgRequestMemory(rpThreadStackSize);
			ret = svc_createThread(&hThreadAux2, (void*)rpAuxThreadStart, 2, &threadStack[(rpThreadStackSize / 4) - 10], 0x3f, 1);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Aux Thread Failed: %08x\n", ret);
				goto final;
			}
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
			svc_waitSynchronization1(hThreadAux1, S64_MAX);
			svc_closeHandle(hThreadAux1);
		}

		for (i = 0; i < rp_work_count; ++i) {
			svc_closeHandle(rp_work_syn[i]->sem_end);
			svc_closeHandle(rp_work_syn[i]->sem_start);
			svc_closeHandle(rp_work_syn[i]->sem_work);
			svc_closeHandle(rp_work_syn[i]->sem_nwm);
		}
	}
final:
	svc_exitThread();
}

// static u8 rp_hdr_tmp[22];

void printNwMHdr(void) {
	u8 *buf = rpNwmHdr;
	nsDbgPrint("nwm hdr: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x .. .. %02x %02x %02x %02x %02x %02x %02x %02x\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
		buf[14], buf[15], buf[16], buf[17], buf[18], buf[19], buf[20], buf[21]
	);
}

static u32 current_nwm_src_addr;
int nwmValParamCallback(u8* buf, int buflen) {
	int i;
	u32* threadStack;
	int ret;
	Handle hThread;
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
				g_nsConfig->rpConfig.dstAddr = daddr;
				rpDstAddrChanged = 0;
				if (daddr != rpConfig.dstAddr) {
					rpConfig.dstAddr = daddr;

					u8 *daddr4 = &daddr;
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

		u8 *saddr4 = &saddr;
		u8 *daddr4 = &daddr;
		nsDbgPrint("remote play src IP: %d.%d.%d.%d, dst IP: %d.%d.%d.%d\n",
			(int)saddr4[0], (int)saddr4[1], (int)saddr4[2], (int)saddr4[3],
			(int)daddr4[0], (int)daddr4[1], (int)daddr4[2], (int)daddr4[3]
		);

		memcpy(rpNwmHdr, buf, 0x22 + 8);
		// *(u16*)(rpNwmHdr + 12) = 0;
		// memcpy(rp_hdr_tmp, rpNwmHdr, 22);
		printNwMHdr();
		initUDPPacket(rpNwmHdr, PACKET_SIZE);

		g_nsConfig->rpConfig.dstAddr = rpConfig.dstAddr = daddr;
		rpDstAddrChanged = 0;
		threadStack = plgRequestMemory(rpThreadStackSize);
		ret = svc_createThread(&hThread, (void*)rpThreadStart, 0, &threadStack[(rpThreadStackSize / 4) - 10], 0x10, 2);
		if (ret != 0) {
			nsDbgPrint("Create RemotePlay Thread Failed: %08x\n", ret);
		}
	}
	return 0;
}

void rpMain() {
	nwmSendPacket = g_nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, g_nsConfig->startupInfo[11], nwmValParamCallback);
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

void rpResetGameHandle(int status) {
	if (!__atomic_load_n(&nsIsRemotePlayStarted, __ATOMIC_RELAXED))
		return;

	Handle hProcess;
	u32 pid = 0x1a;
	int ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		return;
	}

	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpGameLock),
		0xffff8001,
		&status,
		sizeof(status));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory rpGameLock failed: %08x\n", ret, 0);
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

	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		goto final;
	}


	u8 desiredHeader[16] = { 0x04, 0x00, 0x2D, 0xE5, 0x4F, 0x00, 0x00, 0xEF, 0x00, 0x20, 0x9D, 0xE5, 0x00, 0x10, 0x82, 0xE5 };
	u8 buf[16] = { 0 };
	if (!(ntrConfig->isNew3DS)) {
		nsDbgPrint("remoteplay is available on new3ds only\n");
		ret = -1;
		goto final;
	}

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
		copyRemoteMemory(CURRENT_PROCESS_HANDLE, buf, hProcess, remotePC, 16);
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
	nsAttachProcess(hProcess, remotePC, &cfg, 1);
	ret = 0;

	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
	return ret;
}

void nsHandleRemotePlay(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	RP_CONFIG config = {};
	config.currentMode = pac->args[0];
	config.quality = pac->args[1];
	config.qosValueInBytes = pac->args[2];
	nsInitRemotePlay(&config, 0);
}

static void tryInitRemotePlay(u32 dstAddr) {
	struct sockaddr_in addr;
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

	if (bind(fd, &caddr, sizeof(struct sockaddr_in)) < 0) {
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
		if (sendto(fd, data, sizeof(data), 0, &saddr, sizeof(struct sockaddr_in)) < 0) {
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
		if (sendto(fd, data, sizeof(data), 0, &saddr, sizeof(struct sockaddr_in)) < 0) {
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

	if ((ret = copyRemoteMemory(CURRENT_PROCESS_HANDLE, &buf, hProcess, addr, sizeof(buf)))) {
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
	if ((ret = copyRemoteMemory(hProcess, addr, CURRENT_PROCESS_HANDLE, &buf, sizeof(buf)))) {
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

void ipAddrMenu(u32 *addr) {
	int posDigit = 0;
	int posOctet = 0;
	u32 localaddr = *addr;
	u32 key = 0;
	while (1) {
		blank(0, 0, 320, 240);

		u8 ipText[50];
		u8 *addr4 = &localaddr;

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

int remotePlayMenu(void) {
	u32 daddrCurrent = 0;
	do {
		Handle hProcess;
		u32 pid = 0x1a;
		int ret = svc_openProcess(&hProcess, pid);
		if (ret != 0) {
			nsDbgPrint("openProcess failed: %08x\n", ret, 0);
			break;
		}

		ret = copyRemoteMemory(
			0xffff8001,
			&daddrCurrent,
			hProcess,
			(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfig) + offsetof(RP_CONFIG, dstAddr),
			sizeof(daddrCurrent));
		if (ret != 0) {
			nsDbgPrint("copyRemoteMemory (1) failed: %08x\n", ret, 0);
		}

		svc_closeHandle(hProcess);
	} while (0);

	rpConfig.dstAddr = daddrCurrent;
	u32 select = 0;
	RP_CONFIG config = rpConfig;
	u32 localaddr = gethostid();
	u8 *dstAddr4 = &config.dstAddr;

	/* default values */
	if (config.quality == 0) {
		config.currentMode = 0x0103;
		config.quality = 75;
		config.qosValueInBytes = 2 * 1024 * 1024;
		config.dstPort = RP_DST_PORT_DEFAULT;
		config.coreCount = rp_thread_count;
	}
	if (config.dstAddr == 0) {
		config.dstAddr = localaddr;
		dstAddr4[3] = 1;
	}
	rpConfig = config;

	u8 title[50], titleNotStarted[50];
	u8 *localaddr4 = &localaddr;
	xsprintf(title, "Remote Play: %d.%d.%d.%d", (int)localaddr4[0], (int)localaddr4[1], (int)localaddr4[2], (int)localaddr4[3]);
	xsprintf(titleNotStarted, "Remote Play (Standby): %d.%d.%d.%d", (int)localaddr4[0], (int)localaddr4[1], (int)localaddr4[2], (int)localaddr4[3]);

	while (1) {
		u8 rpStarted = __atomic_load_n(&nsIsRemotePlayStarted, __ATOMIC_RELAXED);
		u8 *titleCurrent = title;
		if (!rpStarted) {
			titleCurrent = titleNotStarted;

			// u8 dstPortCaption[50];
			// xsprintf(dstPortCaption, "Port: %d", (int)config.dstPort);

			// u8 *captions[] = {
			// 	dstPortCaption,
			// 	"Apply"
			// };

			// u32 entryCount = sizeof(captions) / sizeof(*captions);

			// u32 key;
			// select = showMenuEx2(titleCurrent, entryCount, captions, NULL, select, &key);

			// if (select == 0) { /* dst port */
			// 	int dstPort = config.dstPort;
			// 	if (key == BUTTON_X)
			// 		dstPort = rpConfig.dstPort;
			// 	else
			// 		menu_adjust_value_with_key(&dstPort, key, 10, 100);

			// 	if (dstPort < 1024)
			// 		dstPort = 1024;
			// 	else if (dstPort > 65535)
			// 		dstPort = 65535;

			// 	if (dstPort != config.dstPort) {
			// 		config.dstPort = dstPort;
			// 	}
			// }

			// else if (select == 1 && key == BUTTON_A) { /* apply */
			// 	rpConfig.dstPort = config.dstPort;
			// 	return 0;
			// }

			// continue;
		}

		u8 coreCountCaption[50];
		xsprintf(coreCountCaption, "Number of Encoding Cores: %d", config.coreCount);

		u8 priorityScreenCaption[50];
		xsprintf(priorityScreenCaption, "Priority Screen: %s", (config.currentMode & 0xff00) == 0 ? "Bottom" : "Top");

		u8 priorityFactorCaption[50];
		xsprintf(priorityFactorCaption, "Priority Factor: %d", (int)(config.currentMode & 0xff));

		u8 qualityCaption[50];
		xsprintf(qualityCaption, "Quality: %d", (int)config.quality);

		u8 qosCaption[50];
		u32 qosMB = config.qosValueInBytes / 1024 / 1024;
		u32 qosKB = config.qosValueInBytes / 1024 % 1024 * 125 / 128;
		xsprintf(qosCaption, "QoS: %d.%d MBps", (int)qosMB, (int)qosKB);

		u8 dstAddrCaption[50];
		xsprintf(dstAddrCaption, "Viewer IP: %d.%d.%d.%d", (int)dstAddr4[0], (int)dstAddr4[1], (int)dstAddr4[2], (int)dstAddr4[3]);

		u8 dstPortCaption[50];
		xsprintf(dstPortCaption, "Port: %d", (int)config.dstPort);

		u8 *captions[] = {
			coreCountCaption,
			priorityScreenCaption,
			priorityFactorCaption,
			qualityCaption,
			qosCaption,
			dstAddrCaption,
			dstPortCaption,
			"Apply",
			"NFC Patch"
		};
		u32 entryCount = sizeof(captions) / sizeof(*captions);

		u32 key;
		select = showMenuEx2(titleCurrent, entryCount, captions, NULL, select, &key);

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

			if (coreCount != config.coreCount) {
				config.coreCount = coreCount;
			}
		}

		else if (select == 1) { /* screen priority */
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

		else if (select == 2) { /* priority factor */
			int factor = config.currentMode & 0xff;
			if (key == BUTTON_X)
				factor = rpConfig.currentMode & 0xff;
			else
				menu_adjust_value_with_key(&factor, key, 5, 10);

			if (factor < 0)
				factor = 0;
			else if (factor > 0xff)
				factor = 0xff;

			if (factor != (config.currentMode & 0xff)) {
				u32 mode = config.currentMode & 0xff00;
				config.currentMode = mode | factor;
			}
		}

		else if (select == 3) { /* quality */
			int quality = config.quality;
			if (key == BUTTON_X)
				quality = rpConfig.quality;
			else
				menu_adjust_value_with_key(&quality, key, 5, 10);

			if (quality < 10)
				quality = 10;
			else if (quality > 100)
				quality = 100;

			if (quality != config.quality) {
				config.quality = quality;
			}
		}

		else if (select == 4) { /* qos */
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

			if (qos != config.qosValueInBytes) {
				config.qosValueInBytes = qos;
			}
		}

		else if (select == 5) { /* dst addr */
			int dstAddr = config.dstAddr;
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

		else if (select == 6) { /* dst port */
			int dstPort = config.dstPort;
			if (key == BUTTON_X)
				dstPort = rpConfig.dstPort;
			else
				menu_adjust_value_with_key(&dstPort, key, 10, 100);

			if (dstPort < 1024)
				dstPort = 1024;
			else if (dstPort > 65535)
				dstPort = 65535;

			if (dstPort != config.dstPort) {
				config.dstPort = dstPort;
			}
		}

		else if (select == 7 && key == BUTTON_A) { /* apply */
			int updateDstAddr = !rpStarted || rpConfig.dstAddr != config.dstAddr || daddrCurrent == 0;
			u32 daddrUpdated = config.dstAddr;
			nsInitRemotePlay(&config, updateDstAddr);

			if (updateDstAddr) {
				tryInitRemotePlay(daddrUpdated);
			}

			return 1;
		}

		else if (select == 8 && key == BUTTON_A) { /* nfc patch */
			rpDoNFCPatch();
		}
	}
}

void nsHandleSaveFile() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 remain = pac->dataLen;
	u8 buf[0x220];
	int ret;
	Handle hFile;
	u32 off = 0, tmp;

	if ((ret = nsRecvPacketData(buf, 0x200)) < 0) {
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
		FSFILE_Write(hFile, &tmp, off, (u32*)g_nsCtx->gBuff, t, 0);

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

void nsBreakPointCallback(u32 regs, u32 bpid, u32 regs2) {
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
	kmemcpy(&pHandleTable, pKProcess + KProcessHandleDataOffset, 4);
	//showDbg("pHandleTable: %08x", pHandleTable, 0);
	kmemcpy(buf, pHandleTable, sizeof(buf));
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
	u8* fileName = "/arm11.bin";
	u32 tmp;

	typedef (*funcType)();
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
	ret = svc_controlMemory((u32*)&outAddr, 0, 0, size, 0x10003, 3);
	if (ret != 0) {
		showDbg("svc_controlMemory failed: %08x", ret, 0);
		return;
	}

	ret = FSFILE_Read(hFile, &tmp, 0, (u32*)outAddr, size);
	if (ret != 0) {
		showDbg("FSFILE_Read failed: %08x", ret, 0);
		return;
	}
	ret = protectMemory((u32*)outAddr, size);
	if (ret != 0) {
		showDbg("protectMemory failed: %08x", ret, 0);
		return;
	}
	((funcType)outAddr)();
	svc_exitThread();
}

void nsHandleListProcess() {
	u32 pids[100];
	u8 pname[20];
	u32 tid[4];
	u32 pidCount;
	u32 i, ret;
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
	u32 isLastValid = 0, lastAddr = 0;
	u32 isValid;
	u32 base = 0x00100000;
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
	u32 pid = pac->args[0];
	u32 addr = pac->args[1];
	u32 size = pac->args[2];
	u32 isLastValid = 0, lastAddr = 0;
	u32 isValid;
	u32 base, remain;
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
	u32 pid = pac->args[0];
	u32 addr = pac->args[1];
	u32 size = pac->args[2];
	u32 isLastValid = 0, lastAddr = 0;
	u32 isValid;
	u32 base, remain;
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
	u32 handle, ret;
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 pid = pac->args[0];
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
	u32 handle, ret;
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	Handle hProcess;
	u32 pid = pac->args[0];
	u32 tids[100];
	u32 tidCount, i, j;
	u32 ctx[400];
	u32 hThread;
	u32 pKThread;
	u32 pContext;

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
		for (j = 0; j < 32; j++) {

			nsDbgPrint("%08x ", ctx[j]);
		}
		nsDbgPrint("\n");
		// svc_closeHandle(hThread);

	}
	nsGetPCToAttachProcess(hProcess);

	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
}



u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg, int sysRegion) {
	u32 size = 0;
	u32* buf = 0;
	u32 baseAddr = NS_CONFIGURE_ADDR;
	u32 stackSize = 0x4000;
	u32 totalSize;
	u32 handle, ret, outAddr;
	u32 tmp[20];
	u32 arm11StartAddress;

	arm11StartAddress = baseAddr + 0x1000 + stackSize;
	buf = (u32*)arm11BinStart;
	size = arm11BinSize;
	nsDbgPrint("buf: %08x, size: %08x\n", buf, size);


	if (!buf) {
		nsDbgPrint("arm11 not loaded\n");
		return;
	}

	totalSize = size + stackSize + 0x1000;
	if (sysRegion) {
		// allocate buffer to remote memory
		ret = mapRemoteMemoryInSysRegion(hProcess, baseAddr, totalSize);
	}
	else {
		// allocate buffer to remote memory
		ret = mapRemoteMemory(hProcess, baseAddr, totalSize);
	}

	if (ret != 0) {
		nsDbgPrint("mapRemoteMemory failed: %08x\n", ret, 0);
	}
	// set rwx
	ret = protectRemoteMemory(hProcess, (void*)baseAddr, totalSize);
	if (ret != 0) {
		nsDbgPrint("protectRemoteMemory failed: %08x\n", ret, 0);
		goto final;
	}
	// load arm11.bin code at arm11StartAddress
	ret = copyRemoteMemory(hProcess, (void*)arm11StartAddress, arm11BinProcess, buf, size);
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(1) failed: %08x\n", ret, 0);
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
		nsDbgPrint("rtCheckRemoteMemoryRegionSafeForWrite failed: %08x\n", ret, 0);
		goto final;
	}



	cfg->initMode = NS_INITMODE_FROMHOOK;

	// store original 8-byte code
	ret = copyRemoteMemory(0xffff8001, &(cfg->startupInfo[0]), hProcess, (void*)remotePC, 8);
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(3) failed: %08x\n", ret, 0);
		goto final;
	}
	cfg->startupInfo[2] = remotePC;
	memcpy(&(cfg->ntrConfig), ntrConfig, sizeof(NTR_CONFIG));
	// copy cfg structure to remote process
	ret = copyRemoteMemory(hProcess, (void*)baseAddr, 0xffff8001, cfg, sizeof(NS_CONFIG));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(2) failed: %08x\n", ret, 0);
		goto final;
	}

	// write hook instructions to remote process
	tmp[0] = 0xe51ff004;
	tmp[1] = arm11StartAddress;
	ret = copyRemoteMemory(hProcess, (void*)remotePC, 0xffff8001, &tmp, 8);
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory(4) failed: %08x\n", ret, 0);
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
	nsAttachProcess(hProcess, remotePC, &cfg, 0);
	nsDbgPrint("will listen at port %d \n", pid + 5000);
	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
	return;
}

void nsPrintRegs(u32* regs) {
	u32 i;

	nsDbgPrint("cpsr:%08x ", regs[0]);
	nsDbgPrint("lr:%08x sp:%08x\n", regs[14], (u32)(regs)+14 * 4);
	for (i = 0; i <= 12; i++) {
		nsDbgPrint("r%d:%08x ", i, regs[1 + i]);
	}
	nsDbgPrint("\n");
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

#define STACK_SIZE 0x4000

void nsInitDebug() {
	// xfunc_out = (void*)nsDbgPutc;
	rtInitLock(&(g_nsConfig->debugBufferLock));
	g_nsConfig->debugBuf = (u8*)(NS_CONFIGURE_ADDR + 0x0900);
	g_nsConfig->debugBufSize = 0xf0;
	g_nsConfig->debugPtr = 0;
	g_nsConfig->debugReady = 1;
}

void nsInit(u32 initType) {
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
		if (g_nsConfig->initMode == NS_INITMODE_FROMHOOK) {
			ret = controlMemoryInSysRegion(&outAddr, base, 0, bufferSize, NS_DEFAULT_MEM_REGION + 3, 3);
		}
		else {
			ret = svc_controlMemory(&outAddr, base, 0, bufferSize, NS_DEFAULT_MEM_REGION + 3, 3);
		}
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