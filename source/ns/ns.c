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


u32 heapStart, heapEnd;


void doSendDelay(u32 time) {
	vu32 i;
	for (i = 0; i < time; i++) {

	}
}

void tje_log(char* str) {
	nsDbgPrint("tje: %s\n", str);
}

int rpAllocDebug = 0;

#define rp_work_count (2)
#define rp_cinfos_count (rp_work_count * 2)

j_compress_ptr cinfos[rp_cinfos_count];
struct jpeg_compress_struct cinfos_top[rp_work_count], cinfos_bot[rp_work_count];
struct rp_alloc_stats_check {
	struct rp_alloc_stats qual, comp, scan;
} alloc_stats_top[rp_work_count], alloc_stats_bot[rp_work_count];
struct jpeg_error_mgr jerr;

u32 rpMinIntervalBetweenPacketsInTick = 0;
static u32 rpThreadStackSize = 0x10000;

#define SYSTICK_PER_US (268)

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
		nsDbgPrint("bad alloc,  size: %d\n", size);
		if (rpAllocDebug) {
			showDbg("bad alloc,  size: %d\n", size, 0);
		}
		return 0;
	}
	cinfo->alloc.stats.offset += totalSize;
	cinfo->alloc.stats.remaining -= totalSize;
	// memset(ret, 0, totalSize);
	// nsDbgPrint("alloc size: %d, ptr: %08x\n", size, ret);
	// if (rpAllocDebug) {
	// 	showDbg("alloc size: %d, ptr: %08x\n", size, ret);
	// }
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

void nsDbgPrint(			/* Put a formatted string to the default device */
	const char*	fmt,	/* Pointer to the format string */
	...					/* Optional arguments */
	)
{
	va_list arp;



	va_start(arp, fmt);
	if (g_nsConfig) {
		if (g_nsConfig->debugReady) {
			rtAcquireLock(&(g_nsConfig->debugBufferLock));
			xvprintf(fmt, arp);
			rtReleaseLock(&(g_nsConfig->debugBufferLock));
		}
	}

	va_end(arp);
}

#define nsDbgPrint(n, ...) nsDbgPrint("(%d) " n, (u32)(svc_getSystemTick() / SYSTICK_PER_US / 1000), ## __VA_ARGS__)

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
	Handle mutex;
	Handle sem_end, sem_start, sem_work;
	Handle event, event_nwm;
	int sem_count;
	u8 sem_set;
} *rp_work_syn[rp_work_count];

RT_HOOK nwmValParamHook;

#define rp_nwm_send_buffer_count (4)
static int packetLen[rp_nwm_send_buffer_count];
static int rpInited = 0;
static u8 rpNwmBuffer[rp_nwm_send_buffer_count][2000];
static u8 *rpDataBuf[rp_nwm_send_buffer_count];

static u8* imgBuffer = 0;
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
	uint32_t acc = 0xffff;

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

	// Return the checksum in network byte order.
	return htons(~acc);
}

int	initUDPPacket(u8 *rpNwmBufferCur, int dataLen) {
	dataLen += 8;
	*(u16*)(rpNwmBufferCur + 0x22 + 8) = htons(8000); // src port
	*(u16*)(rpNwmBufferCur + 0x24 + 8) = htons(8001); // dest port
	*(u16*)(rpNwmBufferCur + 0x26 + 8) = htons(dataLen);
	*(u16*)(rpNwmBufferCur + 0x28 + 8) = 0; // no checksum
	dataLen += 20;

	*(u16*)(rpNwmBufferCur + 0x10 + 8) = htons(dataLen);
	*(u16*)(rpNwmBufferCur + 0x12 + 8) = 0xaf01; // packet id is a random value since we won't use the fragment
	*(u16*)(rpNwmBufferCur + 0x14 + 8) = 0x0040; // no fragment
	*(u16*)(rpNwmBufferCur + 0x16 + 8) = 0x1140; // ttl 64, udp
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


int getBppForFormat(int format) {
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
	u32 bytesInColumn ;
	u32 blankInColumn;

	// u8* transformDst;

	u8 id;
	u8 isTop;
	u8 frameCount;

	// int directCompress;
	j_compress_ptr cinfo;
	struct rp_alloc_stats_check *alloc_stats;
} BLIT_CONTEXT;


void rpCtxInit(BLIT_CONTEXT* ctx, int width, int height, int format, int src_pitch, u8* src) {

	format &= 0x0f;
	if (format == 0){
		ctx->bpp = 4;
	}
	else if (format == 1){
		ctx->bpp = 3;
	}
	else{
		ctx->bpp = 2;
	}
	ctx->bytesInColumn = ctx->bpp * height;
	ctx->blankInColumn = src_pitch - ctx->bytesInColumn;
	if (ctx->format != format) {
		if (ctx->cinfo->global_state != JPEG_CSTATE_START) {
			memcpy(&ctx->cinfo->alloc.stats, &ctx->alloc_stats->comp, sizeof(struct rp_alloc_stats));
			ctx->cinfo->global_state = JPEG_CSTATE_START;
		}
	}
	ctx->format = format;
	ctx->width = width;
	ctx->height = height;
	ctx->src_pitch = src_pitch;
	ctx->src = src;
	ctx->x = 0;
	ctx->y = 0;
	ctx->frameCount = 0;
}




static u32 rpLastSendTick = 0;

static int rpDataBufFilled, rpDataBufSendPos, rpDataBufPos;
static u8 rpDataBufHdr[4];

void rpSendNextBuffer(u32 nextTick) {
	nwmSendPacket(rpNwmBuffer[rpDataBufSendPos], packetLen[rpDataBufSendPos]);
	--rpDataBufFilled;
	rpDataBufSendPos = (rpDataBufSendPos + 1) % rp_nwm_send_buffer_count;
	rpLastSendTick = nextTick;
}

/* flag is only set when called from term_destination (jpeg_term_destination) */
void rpSendBuffer(j_compress_ptr cinfo, u8* buf, u32 size, u32 flag) {
	if (rpAllocDebug) {
		showDbg("sendbuf: %08x, %d", buf, size);
		return;
	}
	s32 tickDiff;
	u32 nextTick;
	u32 sleepValue;
	s32 res;
	// if (!flag && (res = svc_waitSynchronization1(syn->nwm_mutex, 1000000000))) {
	// 	nsDbgPrint("nwm_mutex wait failed: %d\n", res);
	// 	return;
	// }

	u8 *rpDataBufCur = rpDataBuf[rpDataBufPos];
	{
		if (flag) {
			rpDataBufHdr[1] |= flag;
		}
		if (size == 0) {
			rpDataBufCur[4] = 0;
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

	// if (!flag && (res = svc_releaseMutex(syn->nwm_mutex))) {
	// 	nsDbgPrint("nwm_mutex release failed: %d\n", res);
	// }
}

int rpInitJpegCompress() {
	for (int i = 0; i < rp_work_count; ++i) {
		cinfos[i] = &cinfos_top[i];
		cinfos[i + rp_work_count] = &cinfos_bot[i];
	}

	for (int i = 0; i < rp_cinfos_count; ++i) {
		j_compress_ptr cinfo = cinfos[i];

		cinfo->alloc.buf = plgRequestMemorySpecifyRegion(0x00100000, 1);
		if (cinfo->alloc.buf) {
			cinfo->alloc.stats.offset = 0;
			cinfo->alloc.stats.remaining = 0x00100000;
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

#define rp_jpeg_samp_factor (2)

#define rp_thread_count (3)
#define rp_nwm_thread_id (0)

#define rp_prep_buffer_count (rp_thread_count * 2)
#define rp_mcu_buffer_count (rp_thread_count * 3)
#define rp_rows_blk_halves_count (2)

typedef JSAMPARRAY pre_proc_buffer_t[MAX_COMPONENTS];
typedef JSAMPARRAY color_buffer_t[MAX_COMPONENTS];
typedef JBLOCKROW MCU_buffer_t[C_MAX_BLOCKS_IN_MCU];

pre_proc_buffer_t prep_buffers[rp_work_count][rp_prep_buffer_count];
color_buffer_t color_buffers[rp_work_count][rp_thread_count];
MCU_buffer_t MCU_buffers[rp_work_count][rp_mcu_buffer_count];

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

void rpCaptureNextScreen(int work_next);
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

void rpJPEGCompress0(j_compress_ptr cinfo, u8* src, u32 pitch, int work_next) {
	JDIMENSION in_rows_blk = DCTSIZE * cinfo->max_v_samp_factor;
	JDIMENSION in_rows_blk_half = in_rows_blk / 2;

	// JSAMPIMAGE output_buf = jpeg_get_process_buf(cinfo);
	// JSAMPIMAGE color_buf = jpeg_get_pre_process_buf(cinfo);
	JSAMPIMAGE output_buf = prep_buffers[work_next][0];
	JSAMPIMAGE color_buf = color_buffers[work_next][0];

	JSAMPROW input_buf[in_rows_blk_half];

	int j = 0;
	for (j = 0; j < cinfo->image_height;) {
		for (int i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 0);

		for (int i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 1);

		if (j == cinfo->image_height)
			rpCaptureNextScreen(work_next);

		// JBLOCKROW *MCU_buffer = jpeg_get_compress_data_buf(cinfo);
		JBLOCKROW *MCU_buffer = MCU_buffers[work_next][0];

		for (int k = 0; k < cinfo->MCUs_per_row; ++k) {
			jpeg_compress_data(cinfo, output_buf, MCU_buffer, k);
			jpeg_encode_mcu_huff(cinfo, MCU_buffer);
		}
	}
}

void rpReadyWork(BLIT_CONTEXT* ctx, int work_next) {
	// nsDbgPrint("rpReadyWork %d\n", work_next);
	u8* src;
	u32 pitch, i;
	j_compress_ptr cinfo = ctx->cinfo;
	pitch = ctx->src_pitch;
	src = ctx->src;

	if (ctx->format != 1 && ctx->format != 2) {
		svc_sleepThread(1000000000);
		return;
	}

	cinfo->image_width = ctx->height;      /* image width and height, in pixels */
	cinfo->image_height = ctx->width;
	cinfo->input_components = 3;
	cinfo->in_color_space = ctx->format == 1 ? JCS_EXT_BGR : JCS_RGB565;
	if (cinfo->global_state == JPEG_CSTATE_START) {
		jpeg_start_compress(cinfo, TRUE);
	} else {
		jpeg_suppress_tables(cinfo, FALSE);
		jpeg_start_pass_prep(cinfo, 0);
		jpeg_start_pass_huff(cinfo);
		jpeg_start_pass_coef(cinfo, 0);
		jpeg_start_pass_main(cinfo, 0);
		cinfo->next_scanline = 0;
	}

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
}

void rpSendFramesBody(int thread_id, BLIT_CONTEXT* ctx, int work_next) {
	// nsDbgPrint("(%d) rpSendFramesBody (%d)\n", thread_id, work_next);
	j_compress_ptr cinfo = ctx->cinfo;

	struct rp_work_t *work = rp_work[work_next];
	struct rp_work_syn_t *syn = rp_work_syn[work_next];

	if (thread_id == rp_nwm_thread_id) {
		rpDataBufHdr[0] = ctx->id;
		rpDataBufHdr[1] = ctx->isTop;
		rpDataBufHdr[2] = 2;
		rpDataBufHdr[3] = 0;
		u8 *rpDataBufCur = rpDataBuf[rpDataBufPos];

		cinfo->client_data = rpDataBufCur + 4;
		jpeg_init_destination(cinfo);

		jpeg_write_file_header(cinfo);
		jpeg_write_frame_header(cinfo);
		jpeg_write_scan_header(cinfo);
	}

	rpJPEGCompress(work, syn, thread_id);

	if (thread_id == rp_nwm_thread_id) {
		jpeg_finish_pass_huff(cinfo);
		jpeg_write_file_trailer(cinfo);
		jpeg_term_destination(cinfo);
	}
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

Handle rpHDma[2], rpHandleHome, rpHandleGame;
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
	int i;
	Handle hProcess;
	if (rpHandleGame == 0) {
		for (i = 0x28; i < 0x38; i++) {
			int ret = svc_openProcess(&hProcess, i);
			if (ret == 0) {
				nsDbgPrint("game process: %x\n", i);
				rpHandleGame = hProcess;
				break;
			}
		}
		if (rpHandleGame == 0) {
			return 0;
		}
	}
	if (rpGameFCRAMBase == 0) {
		if (svc_flushProcessDataCache(hProcess, 0x14000000, 0x1000) == 0) {
			rpGameFCRAMBase = 0x14000000;
		}
		else if (svc_flushProcessDataCache(hProcess, 0x30000000, 0x1000) == 0) {
			rpGameFCRAMBase = 0x30000000;
		}
		else {
			return 0;
		}
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

int rpCaptureScreen(int isTop) {
	u8 dmaConfig[80] = { 0, 0, 4 };
	u32 bufSize = isTop? (tl_pitch * 400) : (bl_pitch * 320);
	u32 phys = isTop ? tl_current : bl_current;
	u32 dest = imgBuffer;
	Handle hProcess = rpHandleHome;

	int ret;
	s32 res;

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, dest, bufSize);
	svc_closeHandle(rpHDma[isTop]);
	rpHDma[isTop] = 0;

	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		res = svc_startInterProcessDma(&rpHDma[isTop], CURRENT_PROCESS_HANDLE,
			dest, hProcess, 0x1F000000 + (phys - 0x18000000), bufSize, dmaConfig);
		if (res < 0) {
			nsDbgPrint("svc_startInterProcessDma home failed: %08x\n", res);
			goto final;
		}
		return 0;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			res = svc_startInterProcessDma(&rpHDma[isTop], CURRENT_PROCESS_HANDLE,
				dest, hProcess, rpGameFCRAMBase + (phys - 0x20000000), bufSize, dmaConfig);
			if (res < 0) {
				nsDbgPrint("svc_startInterProcessDma home failed: %08x\n", res);
				goto final;
			}
			return 0;
		}
	}
final:
	svc_sleepThread(1000000000);
	rpHDma[isTop] = 0;
	return -1;
}


static u32 currentUpdating;
static u32 frameCount;
static u32 isPriorityTop;
static u32 priorityFactor;
static int nextScreenCaptured[rp_work_count];

static BLIT_CONTEXT blit_context[rp_work_count];
static int rpConfigChanged;

void rpCaptureNextScreen(int work_next) {
	if (__atomic_load_n(&g_nsConfig->rpConfigLock, __ATOMIC_CONSUME)) {
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

	rpKernelCallback(currentUpdating);
	int captured = rpCaptureScreen(currentUpdating) == 0;
	if (captured) {
		struct rp_work_syn_t *syn = rp_work_syn[work_next];
		s32 res = svc_waitSynchronization1(syn->sem_end, 1000000000);
		if (res) {
			nsDbgPrint("svc_waitSynchronization1 sem_end (%d) failed: %d\n", work_next, res);
			return;
		}

		nextScreenCaptured[work_next] = captured;
		__atomic_clear(&syn->sem_set, __ATOMIC_RELEASE);

		s32 count;
		res = svc_releaseSemaphore(&count, syn->sem_start, rp_thread_count - 1);
		if (res) {
			nsDbgPrint("svc_releaseSemaphore sem_start failed: %d\n", res);
			return;
		}
	}
}

static int rp_work_next = 0;

void rpSendFramesStart(int thread_id, int work_next) {
	BLIT_CONTEXT *ctx = &blit_context[work_next];
	struct rp_work_syn_t *syn = rp_work_syn[work_next];

	if (!__atomic_test_and_set(&syn->sem_set, __ATOMIC_ACQUIRE)) {

		if (currentUpdating) {
			// send top
			// rpCaptureScreen(1);
			ctx->cinfo = &cinfos_top[work_next];
			ctx->alloc_stats = &alloc_stats_top[work_next];

			currentTopId += 1;
			rpCtxInit(ctx, 400, 240, tl_format, tl_pitch, imgBuffer);
			// topContext.transformDst = imgBuffer + 0x00150000;
			ctx->id = (u8)currentTopId;
			ctx->isTop = 1;
			// remotePlayBlitCompressed(&topContext);
		} else {
			// send bottom
			// rpCaptureScreen(0);
			ctx->cinfo = &cinfos_bot[work_next];
			ctx->alloc_stats = &alloc_stats_bot[work_next];

			currentBottomId += 1;
			rpCtxInit(ctx, 320, 240, bl_format, bl_pitch, imgBuffer);
			// botContext.transformDst = imgBuffer + 0x00150000;
			ctx->id = (u8)currentBottomId;
			ctx->isTop = 0;
			// remotePlayBlitCompressed(&botContext);
		}
		rpReadyWork(ctx, work_next);

		s32 count, res;
		res = svc_releaseSemaphore(&count, syn->sem_work, rp_thread_count - 1);
		if (res) {
			nsDbgPrint("(%d) svc_releaseSemaphore sem_work (%d) failed: %d\n", thread_id, work_next, res);
			goto final;
		}
	} else {
		s32 res = svc_waitSynchronization1(syn->sem_work, 1000000000);
		if (res) {
			nsDbgPrint("(%d) svc_waitSynchronization1 sem_work (%d) failed: %d\n", thread_id, work_next, res);
			goto final;
		}
	}

	rpSendFramesBody(thread_id, ctx, work_next);

final:
	// nsDbgPrint("(%d) __atomic_add_fetch sem_count (%d):\n", thread_id, work_next);
	if (__atomic_add_fetch(&syn->sem_count, 1, __ATOMIC_ACQUIRE) == rp_thread_count) {
		__atomic_store_n(&syn->sem_count, 0, __ATOMIC_RELEASE);
		s32 count;
		// nsDbgPrint("(%d) svc_releaseSemaphore sem_end (%d):\n", thread_id, work_next);
		s32 res = svc_releaseSemaphore(&count, syn->sem_end, 1);
		if (res) {
			nsDbgPrint("svc_releaseSemaphore sem_end failed: %d\n", res);
			return;
		}
	}

	return;
}

static void rpSendFrames() {

#define rpCurrentMode (g_nsConfig->rpConfig.currentMode)
#define rpQuality (g_nsConfig->rpConfig.quality)
#define rpQosValueInBytes (g_nsConfig->rpConfig.qosValueInBytes)

	// rpCurrentMode = g_nsConfig->startupInfo[8];
	// rpQuality = g_nsConfig->startupInfo[9];
	// rpQosValueInBytes = g_nsConfig->startupInfo[10];
	if (rpQosValueInBytes < 500 * 1024) {
		rpQosValueInBytes = 2 * 1024 * 1024;
	}
	rpMinIntervalBetweenPacketsInTick = (1000000 / (rpQosValueInBytes / PACKET_SIZE)) * SYSTICK_PER_US;

	for (int j = 0; j < rp_cinfos_count; ++j)
		cinfos[j]->global_state = JPEG_CSTATE_START;
	jpeg_set_quality(cinfos[0], rpQuality, TRUE);
	for (int j = 1; j < rp_cinfos_count; ++j)
		for (int i = 0; i < NUM_QUANT_TBLS; ++i)
			cinfos[j]->quant_tbl_ptrs[i] = cinfos[0]->quant_tbl_ptrs[i];

	for (int i = 0; i < rp_work_count; ++i) {
		if (!alloc_stats_top[i].qual.offset) {
			memcpy(&alloc_stats_top[i].qual, &cinfos_top[i].alloc.stats, sizeof(struct rp_alloc_stats));
		} else {
			memcpy(&cinfos_top[i].alloc.stats, &alloc_stats_top[i].qual, sizeof(struct rp_alloc_stats));
		}

		if (!alloc_stats_bot[i].qual.offset) {
			memcpy(&alloc_stats_bot[i].qual, &cinfos_bot[i].alloc.stats, sizeof(struct rp_alloc_stats));
		} else {
			memcpy(&cinfos_bot[i].alloc.stats, &alloc_stats_bot[i].qual, sizeof(struct rp_alloc_stats));
		}
	}

	jpeg_jinit_forward_dct(cinfos[0]);
	cinfos[0]->fdct_reuse = TRUE;

	for (int j = 1; j < rp_cinfos_count; ++j) {
		cinfos[j]->fdct = cinfos[0]->fdct;
		cinfos[j]->fdct_reuse = TRUE;
	}

	jpeg_start_pass_fdctmgr(cinfos[0]);

	isPriorityTop = 1;
	priorityFactor = 0;
	u32 mode = (rpCurrentMode & 0xff00) >> 8;
	u32 factor = (rpCurrentMode & 0xff);
	if (mode == 0) {
		isPriorityTop = 0;
	}
	priorityFactor = factor;

	for (int h = 0; h < rp_work_count; ++h) {
		for (int i = 0; i < rp_prep_buffer_count; ++i) {
			for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
				prep_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
					240, (JDIMENSION)(MAX_SAMP_FACTOR * DCTSIZE));
			}
		}
		for (int i = 0; i < rp_thread_count; ++i) {
			for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
				color_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
					240, (JDIMENSION)MAX_SAMP_FACTOR);
			}
		}
		for (int i = 0; i < rp_mcu_buffer_count; ++i) {
			JBLOCKROW buffer = (JBLOCKROW)jpeg_alloc_large((j_common_ptr)cinfos[h], JPOOL_IMAGE, C_MAX_BLOCKS_IN_MCU * sizeof(JBLOCK));
			for (int b = 0; b < C_MAX_BLOCKS_IN_MCU; b++) {
				MCU_buffers[h][i][b] = buffer + b;
			}
		}
	}

	for (int i = 0; i < rp_work_count; ++i) {
		memcpy(&alloc_stats_top[i].comp, &cinfos_top[i].alloc.stats, sizeof(struct rp_alloc_stats));

		memcpy(&alloc_stats_bot[i].comp, &cinfos_bot[i].alloc.stats, sizeof(struct rp_alloc_stats));
	}

	// for (int i = 0; i < sizeof(ctxs) / sizeof(*ctxs); ++i) {
	// 	ctxs[i]->cinfo->image_width = 240;
	// 	ctxs[i]->cinfo->image_height = i == 0 ? 400 : 320;
	// 	ctxs[i]->cinfo->input_components = 3;
	// 	ctxs[i]->cinfo->in_color_space = JCS_RGB;

	// 	jpeg_start_compress(ctxs[i]->cinfo, TRUE); /* alloc buffers */
	// 	ctxs[i]->cinfo->global_state == JPEG_CSTATE_START;
	// };

#undef rpCurrentMode
#undef rpQuality
#undef rpQosValueInBytes
	rpConfigChanged = 0;
	__atomic_store_n(&g_nsConfig->rpConfigLock, 0, __ATOMIC_RELEASE);

	currentUpdating = isPriorityTop;
	frameCount = 0;
	for (int i = 0; i < rp_work_count; ++i)
		nextScreenCaptured[i] = 0;

	while (1) {
		if (rpConfigChanged) {
			break;
		}

		if (!nextScreenCaptured[rp_work_next]) {
			rpCaptureNextScreen(rp_work_next);
			continue;
		}
		nextScreenCaptured[rp_work_next] = 0;

		rpSendFramesStart(0, rp_work_next);

		rp_work_next = (rp_work_next + 1) % rp_work_count;
	}
}

static void rpAuxThreadStart(u32 thread_id) {
	int work_next = 0;
	while (1) {
		int res;

		struct rp_work_syn_t *syn = rp_work_syn[work_next];

		res = svc_waitSynchronization1(syn->sem_start, 1000000000);
		if (res) {
			nsDbgPrint("(%d) svc_waitSynchronization1 sem_start (%d) failed: %d\n", thread_id, work_next, res);
			continue;
		}

		rpSendFramesStart(thread_id, work_next);
		work_next = (work_next + 1) % rp_work_count;
	}
	svc_exitThread();
}

static void rpThreadStart(void *arg) {
	u32 i, ret;
	u32 remainSize;

	for (i = 0; i < rp_nwm_send_buffer_count; ++i)
		rpDataBuf[i] = rpNwmBuffer[i] + 0x2a + 8;
	imgBuffer = plgRequestMemory(0x180000);

	if (rpInitJpegCompress() != 0) {
		nsDbgPrint("rpInitJpegCompress failed\n");
		goto final;
	}

	nsDbgPrint("imgBuffer: %08x\n", imgBuffer);
	if (!imgBuffer) {
		goto final;
	}

	for (i = 0; i < rp_work_count; ++i) {
		rp_work[i] = plgRequestMemory(0x1000);
		rp_work_syn[i] = plgRequestMemory(0x1000);
	}

	rpInitDmaHome();

	// kRemotePlayCallback();

	for (i = 0; i < rp_work_count; ++i) {
		ret = svc_createMutex(&rp_work_syn[i]->mutex, 0);
		if (ret != 0) {
			nsDbgPrint("svc_createMutex (%d) failed: %08x\n", i, ret);
			goto final;
		}

		ret = svc_createSemaphore(&rp_work_syn[i]->sem_end, 1, 1);
		if (ret != 0) {
			nsDbgPrint("svc_createSemaphore sem_end (%d) failed: %08x\n", i, ret);
			goto final;
		}
		ret = svc_createSemaphore(&rp_work_syn[i]->sem_start, 0, rp_thread_count - 1);
		if (ret != 0) {
			nsDbgPrint("svc_createSemaphore sem_start (%d) failed: %08x\n", i, ret);
			goto final;
		}
		ret = svc_createSemaphore(&rp_work_syn[i]->sem_work, 0, rp_thread_count - 1);
		if (ret != 0) {
			nsDbgPrint("svc_createSemaphore sem_work (%d) failed: %08x\n", i, ret);
			goto final;
		}
		ret = svc_createEvent(&rp_work_syn[i]->event, 1);
		if (ret != 0) {
			nsDbgPrint("svc_createEvent (%d) failed: %08x\n", i, ret);
			goto final;
		}
		ret = svc_createEvent(&rp_work_syn[i]->event_nwm, 1);
		if (ret != 0) {
			nsDbgPrint("svc_createEvent event_nwm (%d) failed: %08x\n", i, ret);
			goto final;
		}
		rp_work_syn[i]->sem_count = 0;
		rp_work_syn[i]->sem_set = 0;
	}

	if (rp_thread_count >= 2) {
		Handle hThreadAux;
		u32 *threadStack = plgRequestMemory(rpThreadStackSize);
		ret = svc_createThread(&hThreadAux, (void*)rpAuxThreadStart, 1, &threadStack[(rpThreadStackSize / 4) - 10], 0x10, 3);
		if (ret != 0) {
			nsDbgPrint("Create RemotePlay Aux Thread Failed: %08x\n", ret);
			goto final;
		}
	}
	if (rp_thread_count >= 3) {
		Handle hThreadAux;
		u32 *threadStack = plgRequestMemory(rpThreadStackSize);
		ret = svc_createThread(&hThreadAux, (void*)rpAuxThreadStart, 2, &threadStack[(rpThreadStackSize / 4) - 10], 0x3f, 1);
		if (ret != 0) {
			nsDbgPrint("Create RemotePlay Aux Thread Failed: %08x\n", ret);
			goto final;
		}
	}

	while (1) {
		rpSendFrames();
	}
	final:
	svc_exitThread();
}

int nwmValParamCallback(u8* buf, int buflen) {
	//rtDisableHook(&nwmValParamHook);
	int i;
	u32* threadStack;
	int ret;
	Handle hThread;
	/*
	if (buf[31] != 6) {
	nsDbgPrint("buflen: %d\n", buflen);
	for (i = 0; i < buflen; i++) {
	nsDbgPrint("%02x ", buf[i]);
	}
	}*/

	if (rpInited) {
		return 0;
	}
	if (buf[0x17 + 0x8] == 6) {
		if ((*(u16*)(&buf[0x22 + 0x8])) == 0x401f) {  // src port 8000
			rpInited = 1;
			rtDisableHook(&nwmValParamHook);

			for (i = 0; i < rp_nwm_send_buffer_count; ++i) {
				memcpy(rpNwmBuffer[i], buf, 0x22 + 8);
				packetLen[i] = initUDPPacket(rpNwmBuffer[i], PACKET_SIZE);
			}

			threadStack = plgRequestMemory(rpThreadStackSize);
			ret = svc_createThread(&hThread, (void*)rpThreadStart, 0, &threadStack[(rpThreadStackSize / 4) - 10], 0x10, 2);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Thread Failed: %08x\n", ret);
			}
		}
	}
	return 0;
}

void rpMain() {
	nwmSendPacket = g_nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, g_nsConfig->startupInfo[11], nwmValParamCallback);
	rtEnableHook(&nwmValParamHook);

}



int nsIsRemotePlayStarted = 0;

/*
void testJpeg() {
	int ret;
	cinfo.err = jpeg_std_error(&jerr);
	rpAllocBuff = plgRequestMemory(0x00100000);
	rpAllocBuffRemainSize = 0x00100000;
	rpAllocDebug = 1;

	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, 0);

	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 50, TRUE);

	cinfo.image_width = 240;
	cinfo.image_height = 400;
	cinfo.input_components = 3;
	showDbg("start compress", 0, 0);
	jpeg_start_compress(&cinfo, TRUE);

}



void tickTest() {
	svc_sleepThread(1000000000);
	u32 time1 = svc_getSystemTick();
	svc_sleepThread(1000000000);
	u32 time2 = svc_getSystemTick();
	nsDbgPrint("%08x, %08x\n", time1, time2);
}
*/

static inline void nsRemotePlayControl(u32 mode, u32 quality, u32 qos) {
	Handle hProcess;
	u32 pid = 0x1a;
	int ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		return;
	}

	u32 control, controlCount = 1000;
	do {
		ret = copyRemoteMemory(
			0xffff8001,
			&control,
			hProcess,
			(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfigLock),
			sizeof(control));
		if (ret != 0) {
			nsDbgPrint("copyRemoteMemory (0) failed: %08x\n", ret, 0);
			svc_closeHandle(hProcess);
			return;
		}
		if (control) {
			if (!--controlCount) {
				nsDbgPrint("rpConfigLock wait timed out\n", 0, 0);
				svc_closeHandle(hProcess);
				return;
			}
			svc_sleepThread(1000000);
		} else {
			break;
		}
	} while (1);

	RP_CONFIG rp = {
		.currentMode = mode,
		.quality = quality,
		.qosValueInBytes = qos,
	};

	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfig),
		0xffff8001,
		&rp,
		sizeof(rp));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory (1) failed: %08x\n", ret, 0);
		svc_closeHandle(hProcess);
		return;
	}

	control = 1;
	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfigLock),
		0xffff8001,
		&control,
		sizeof(control));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory (2) failed: %08x\n", ret, 0);
	}

	svc_closeHandle(hProcess);
}

void nsHandleRemotePlay(void) {

	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 mode = pac->args[0];
	u32 quality = pac->args[1];
	u32 qosValue = pac->args[2];

	if (!((quality >= 10) && (quality <= 100))) {
		nsDbgPrint("illegal quality\n");
		return;
	}

	if (nsIsRemotePlayStarted) {
		nsDbgPrint("remote play already started, updating params\n");
		nsRemotePlayControl(mode, quality, qosValue);
		return;
	}
	nsIsRemotePlayStarted = 1;

	Handle hProcess;
	u32 ret;
	u32 pid = 0x1a;
	u32 remotePC = 0x001231d0;
	NS_CONFIG	cfg;

	memset(&cfg, 0, sizeof(NS_CONFIG));
	cfg.startupCommand = NS_STARTCMD_DEBUG;
	// cfg.startupInfo[8] = mode;
	// cfg.startupInfo[9] = quality;
	// cfg.startupInfo[10] = qosValue;
	cfg.rpConfig.currentMode = mode;
	cfg.rpConfig.quality = quality;
	cfg.rpConfig.qosValueInBytes = qosValue;
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
		goto final;
	}
	setCpuClockLock(3);
	nsDbgPrint("cpu was locked on 804MHz, L2 Enabled\n");
	nsDbgPrint("starting remoteplay...\n");
	nsAttachProcess(hProcess, remotePC, &cfg, 1);

	final:
	if (hProcess != 0) {
		svc_closeHandle(hProcess);
	}
}

void nsHandleSaveFile() {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 remain = pac->dataLen;
	u8 buf[0x220];
	u32 ret, hFile;
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
		return;
	}
	ret = listen(listen_sock, 1);
	if (ret < 0) {
		showDbg("listen failed: %08x", ret, 0);
		return;
	}

	while (1) {
		checkExitFlag();
		sockfd = accept(listen_sock, NULL, NULL);
		g_nsCtx->hSocket = sockfd;
		if (sockfd < 0) {
			svc_sleepThread(1000000000);
			continue;
		}
		/*
		tmp = fcntl(sockfd, F_GETFL);
		fcntl(sockfd, F_SETFL, tmp | O_NONBLOCK);
		*/
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
}

void nsThreadStart() {
	nsMainLoop();
	svc_exitThread();
}

#define STACK_SIZE 0x4000

void nsInitDebug() {
	xfunc_out = (void*)nsDbgPutc;
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