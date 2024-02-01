#include "global.h"

#include "3ds/os.h"
#include "3ds/allocator/mappable.h"
#include "3ds/services/gspgpu.h"
#include "3ds/ipc.h"

#define FILE void
#include "jpeglib.h"
/* CSTATE_START from jpegint.h */
#define JPEG_CSTATE_START 100

#include <memory.h>
#include <arpa/inet.h>
#include <math.h>

#define SCALEBITS 16
#define ONE_HALF ((u32)1 << (SCALEBITS - 1))
#define FIX(x) ((u32)((x) * (1L << SCALEBITS) + 0.5))

static Handle hThreadMain;
static int rpResetThreads;

static u32 rpMinIntervalBetweenPacketsInTick;
static u32 rpMinIntervalBetweenPacketsInNS;

#define SCREEN_COUNT (2)
#define rp_thread_count (3)
#define rp_work_count (2)
#define rp_cinfos_count (rp_work_count * rp_thread_count * SCREEN_COUNT)

static j_compress_ptr cinfos[rp_cinfos_count];
static u32 cinfo_alloc_sizes[rp_cinfos_count];
static struct jpeg_compress_struct cinfos_top[rp_work_count][rp_thread_count], cinfos_bot[rp_work_count][rp_thread_count];
static struct rp_alloc_stats_check {
	struct rp_alloc_stats qual, comp;
} alloc_stats_top[rp_work_count][rp_thread_count], alloc_stats_bot[rp_work_count][rp_thread_count];
static struct jpeg_error_mgr jerr;
static int jpeg_rows[rp_work_count];
static int jpeg_rows_last[rp_work_count];
static int jpeg_adjusted_rows[rp_work_count];
static int jpeg_adjusted_rows_last[rp_work_count];
static int jpeg_progress[rp_work_count][rp_thread_count];

typedef JSAMPARRAY pre_proc_buffer_t[MAX_COMPONENTS];
typedef JSAMPARRAY color_buffer_t[MAX_COMPONENTS];
typedef JBLOCKROW MCU_buffer_t[C_MAX_BLOCKS_IN_MCU];

static pre_proc_buffer_t prep_buffers[rp_work_count][rp_thread_count];
static color_buffer_t color_buffers[rp_work_count][rp_thread_count];
static MCU_buffer_t MCU_buffers[rp_work_count][rp_thread_count];

typedef struct {
	int width, height, format, src_pitch;
	u8* src;
	int bpp;

	u8 id;
	u8 isTop;

	j_compress_ptr cinfos[rp_thread_count];
	struct rp_alloc_stats_check *cinfos_alloc_stats[rp_thread_count];

	int irow_start[rp_thread_count];
	int irow_count[rp_thread_count];
	u8 capture_next;
} BLIT_CONTEXT;
static BLIT_CONTEXT blit_context[rp_work_count];

static u32 rpLastSendTick;

#define rp_nwm_hdr_size (0x2a + 8)
#define rp_data_hdr_size (4)
static u8 rpNwmHdr[rp_nwm_hdr_size];
static u8 *rpDataBuf[rp_work_count][rp_thread_count];
static u8 *rpPacketBufLast[rp_work_count][rp_thread_count];

static u32 rp_nwm_work_next, rp_nwm_thread_next;
static int rp_nwm_syn_next[rp_work_count];
static u8 rpDataBufHdr[rp_work_count][rp_data_hdr_size];

static u32 rp_work_next;
static u32 rp_screen_work_next;
static int rp_skip_frame[rp_work_count];

static struct rpDataBufInfo_t {
	u8 *sendPos, *pos;
	u32 flag;
} rpDataBufInfo[rp_work_count][rp_thread_count];

#define rp_img_buffer_size (0x60000)
#define rp_nwm_buffer_size (0x28000)
#define rp_screen_work_count (2)
static u8* imgBuffer[SCREEN_COUNT][rp_screen_work_count];
static u32 imgBuffer_work_next[SCREEN_COUNT];
static u32 currentTopId = 0, currentBottomId = 0;
static u32 tl_fbaddr[2];
static u32 bl_fbaddr[2];
static u32 tl_format, bl_format;
static u32 tl_pitch, bl_pitch;
static u32 tl_current, bl_current;

static int getBppForFormat(int format) {
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

int rpCtxInit(BLIT_CONTEXT *ctx, int width, int height, int format, u8 *src) {
	int ret = 0;
	ctx->bpp = getBppForFormat(format);
	format &= 0x0f;
	if (ctx->format != format) {
		ret = 1;
		for (u32 j = 0; j < rpConfig->coreCount; ++j) {
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
	return ret;
}


#define PACKET_SIZE (1448)
#define rp_packet_data_size (PACKET_SIZE - rp_data_hdr_size)

static struct rp_handles_t {
	struct rp_work_syn_t {
		Handle sem_end, sem_nwm, sem_send;
		u32 sem_count;
		u8 sem_set;
	} work[rp_work_count];

	struct rp_thread_syn_t {
		Handle sem_start, sem_work;
	} thread[rp_thread_count];

	Handle nwmEvent;
	Handle portEvent[SCREEN_COUNT];
	Handle screenCapSem;
} *rp_syn;

static uint16_t ip_checksum(void *vdata, size_t length) {
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

	// Handle complete 16-bit blocks.
	for (i = 0; i < length; ++i) {
		acc += ntohs(sdata[i]);
		// if (acc > 0xffff) {
		// 	acc -= 0xffff;
		// }
	}
	acc = (acc & 0xffff) + (acc >> 16);
	acc += (acc >> 16);

	// Return the checksum in network byte order.
	return htons(~acc);
}

static int initUDPPacket(u8 *rpNwmBufferCur, int dataLen) {
	dataLen += 8;
	*(u16*)(rpNwmBufferCur + 0x22 + 8) = htons(8000); // src port
	*(u16*)(rpNwmBufferCur + 0x24 + 8) = htons(rpConfig->dstPort); // dest port
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

static int rpDataBufFilled(struct rpDataBufInfo_t *info, u8 **pos, u32 *flag) {
	*flag = ALC(&info->flag);
	*pos = ALR(&info->pos);
	return info->sendPos < *pos || *flag;
}

static int rpSendNextBuffer(u32 nextTick, u8 *data_buf_pos, u32 data_buf_flag) {
	u32 work_next = rp_nwm_work_next;
	u32 thread_id = rp_nwm_thread_next;

	u8 *rp_nwm_buf, *rp_nwm_packet_buf, *rp_data_buf;
	u32 packet_len, size;

	u8 rp_nwm_buf_tmp[2000];
	struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][thread_id];

	rp_data_buf = info->sendPos;
	rp_nwm_packet_buf = rp_data_buf - rp_data_hdr_size;
	rp_nwm_buf = rp_nwm_packet_buf - rp_nwm_hdr_size;

	size = data_buf_pos - info->sendPos;
	size = MIN(size, rp_packet_data_size);

	int thread_emptied = info->sendPos + size == data_buf_pos;
	int thread_done = thread_emptied && data_buf_flag;

	if (size < rp_packet_data_size && !thread_done)
		return -1;

	u32 thread_next = (thread_id + 1) % rpConfig->coreCount;

	if (size < rp_packet_data_size && thread_id != rpConfig->coreCount - 1) {
		u32 total_size = 0, remaining_size = rp_packet_data_size;
		u32 sizes[rpConfig->coreCount];

		rp_nwm_buf = rp_nwm_buf_tmp;
		rp_nwm_packet_buf = rp_nwm_buf + rp_nwm_hdr_size;
		rp_data_buf = rp_nwm_packet_buf + rp_data_hdr_size;

		u8 *data_buf_pos_next;
		u32 data_buf_flag_next;

		memcpy(rp_data_buf, info->sendPos, size);
		total_size += size;
		remaining_size -= size;

		sizes[thread_id] = size;
		info->sendPos += size;

		while (1) {
			struct rpDataBufInfo_t *info_next = &rpDataBufInfo[work_next][thread_next];

			if (!rpDataBufFilled(info_next, &data_buf_pos_next, &data_buf_flag_next)) {
				// Rewind sizes
				for (u32 j = thread_id; j < thread_next; ++j) {
					struct rpDataBufInfo_t *info_prev = &rpDataBufInfo[work_next][j];
					info_prev->sendPos -= sizes[j];
				}
				return -1;
			}

			u32 next_size = data_buf_pos_next - info_next->sendPos;
			next_size = MIN(next_size, remaining_size);

			int thread_next_emptied = info_next->sendPos + next_size == data_buf_pos_next;
			/* thread_next_done should be equal to thread_next_emptied;
			   test the condition just because */
			int thread_next_done = thread_next_emptied && data_buf_flag_next;

			memcpy(rp_data_buf + total_size, info_next->sendPos, next_size);
			total_size += next_size;
			remaining_size -= next_size;

			sizes[thread_next] = next_size;
			info_next->sendPos += next_size;

			if (thread_next_done) {
				thread_next = (thread_next + 1) % rpConfig->coreCount;
				if (thread_next == 0) {
					break;
				}
			}
			if (remaining_size == 0)
				break;
		}

		size = total_size;
		thread_done = 1;
	}

	memcpy(rp_nwm_buf, rpNwmHdr, rp_nwm_hdr_size);
	packet_len = initUDPPacket(rp_nwm_buf, size + rp_data_hdr_size);
	memcpy(rp_nwm_packet_buf, rpDataBufHdr[work_next], rp_data_hdr_size);
	if (thread_done && thread_next == 0) {
		rp_nwm_packet_buf[1] |= data_buf_flag;
	}
	++rpDataBufHdr[work_next][3];

	nwmSendPacket(rp_nwm_buf, packet_len);
	rpLastSendTick = nextTick;

	info->sendPos += size;

	if (thread_done) {
		for (u32 j = thread_id; j != thread_next; j = (j + 1) % rpConfig->coreCount)
			ASR(&rpDataBufInfo[work_next][j].flag, 0);
		rp_nwm_thread_next = thread_next;

		if (rp_nwm_thread_next == 0) {
			s32 count, res;
			res = svcReleaseSemaphore(&count, rp_syn->work[work_next].sem_nwm, 1);
			if (res) {
			}

			rp_nwm_syn_next[work_next] = 1;
			work_next = (work_next + 1) % rp_work_count;
			rp_nwm_work_next = work_next;
		}
	}
	return 0;
}

static int rpTrySendNextBufferMaybe(int work_flush, int may_skip) {
	u32 work_next = rp_nwm_work_next;
	u32 thread_id = rp_nwm_thread_next;

	while (1) {
		struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][thread_id];

		if (rp_nwm_syn_next[work_next]) {
			s32 res = svcWaitSynchronization(rp_syn->work[work_next].sem_send, work_flush ? 100000000 : 0);
			if (res) {
				return -1;
			}
			rp_nwm_syn_next[work_next] = 0;
		}

		u8 *data_buf_pos;
		u32 data_buf_flag;
		int ret = 0;

		if (!rpDataBufFilled(info, &data_buf_pos, &data_buf_flag))
			return may_skip ? 0 : -1;

		u32 nextTick = svcGetSystemTick();
		s32 tickDiff = (s32)nextTick - (s32)rpLastSendTick;
		if (tickDiff < (s32)rpMinIntervalBetweenPacketsInTick) {
			if (work_flush) {
				u32 sleepValue = ((u64)((s32)rpMinIntervalBetweenPacketsInTick - tickDiff) * 1000000000) / SYSCLOCK_ARM11;
				svcSleepThread(sleepValue);
				ret = rpSendNextBuffer(svcGetSystemTick(), data_buf_pos, data_buf_flag);
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

void rpSendBuffer(j_compress_ptr cinfo, u8 *, u32 size, u32 flag) {
	u32 work_next = cinfo->user_work_next;
	u32 thread_id = cinfo->user_thread_id;

	struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][thread_id];

	u8 *data_buf_pos_next = info->pos + size;
	if (data_buf_pos_next > rpPacketBufLast[work_next][thread_id]) {
		nsDbgPrint("rpSendBuffer overrun\n");
		data_buf_pos_next = rpPacketBufLast[work_next][thread_id];
	}
	cinfo->client_data = data_buf_pos_next;

	ASR(&info->pos, data_buf_pos_next);
	if (flag) {
		ASL(&info->flag, flag);
	}

	s32 ret = svcSignalEvent(rp_syn->nwmEvent);
	if (ret != 0) {
		nsDbgPrint("nwmEvent signal error: %08"PRIx32"\n", ret);
	}
}

void *rpMalloc(j_common_ptr cinfo, u32 size) {
	void* ret = cinfo->alloc.buf + cinfo->alloc.stats.offset;
	u32 totalSize = size;
	/* min align is 4
	   32 for cache line size */
	if (totalSize % 32 != 0) {
		totalSize += 32 - (totalSize % 32);
	}
	if (cinfo->alloc.stats.remaining < totalSize) {
		u32 alloc_size = cinfo->alloc.stats.offset + cinfo->alloc.stats.remaining;
		nsDbgPrint("bad alloc, size: %"PRIx32"/%"PRIx32"\n", totalSize, alloc_size);
		return 0;
	}
	cinfo->alloc.stats.offset += totalSize;
	cinfo->alloc.stats.remaining -= totalSize;
	return ret;
}

void rpFree(j_common_ptr, void *) {}

static int rpReadyNwm(u32 thread_id, u32 work_next, int id, int isTop) {
	s32 res;
	while (1) {
		res = svcWaitSynchronization(rp_syn->work[work_next].sem_nwm, 100000000);
		if (ALR(&rpResetThreads)) {
			return -1;
		}
		if (res != 0) {
			if (res != RES_TIMEOUT) {
				nsDbgPrint("sem_nwm wait error: %08"PRIx32"\n", res);
				svcSleepThread(1000000000);
			}
			continue;
		}
		break;
	}

	for (u32 j = 0; j < rpConfig->coreCount; ++j) {
		struct rpDataBufInfo_t *info = &rpDataBufInfo[work_next][j];
		info->sendPos = info->pos = rpDataBuf[work_next][j] + rp_data_hdr_size;
		info->flag = 0;
	}

	s32 count;
	res = svcReleaseSemaphore(&count, rp_syn->work[work_next].sem_send, 1);
	if (res) {
		nsDbgPrint("(%"PRIx32") Release semaphore sem_send (%"PRIx32") failed: %"PRIx32"\n", thread_id, work_next, res);
	}

	rpDataBufHdr[work_next][0] = id;
	rpDataBufHdr[work_next][1] = isTop;
	rpDataBufHdr[work_next][2] = 2;
	rpDataBufHdr[work_next][3] = 0;

	return 0;
}

static void rpThreadMainSendFrames(void);
static int rpInitJpegCompress() {
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

static void rpShowNextFrameBothScreen(void) {
	/* Show at least one frame*/
	svcSignalEvent(rp_syn->portEvent[0]);
	svcSignalEvent(rp_syn->portEvent[1]);
	/* Force memcmp (frame-skip) to fail */
	for (int i = 0; i < rp_screen_work_count; ++i) {
		++imgBuffer[0][i][0];
		++imgBuffer[1][i][0];
	}
}

#define rp_jpeg_samp_factor (2)
static void rpReadyWork(BLIT_CONTEXT *ctx, u32 work_next) {
	u32 work_prev = work_next == 0 ? rp_work_count - 1 : work_next - 1;
	int progress[rpConfig->coreCount];
	for (u32 j = 0; j < rpConfig->coreCount; ++j) {
		ASR(&jpeg_progress[work_next][j], 0);
		progress[j] = ALR(&jpeg_progress[work_prev][j]);
	}

	int mcu_size = DCTSIZE * rp_jpeg_samp_factor;
	int mcus_per_row = ctx->height / mcu_size;
	int mcu_rows = ctx->width / mcu_size;
	int mcu_rows_per_thread = (mcu_rows + (rpConfig->coreCount - 1)) / rpConfig->coreCount;
	jpeg_rows[work_next] = mcu_rows_per_thread;
	jpeg_rows_last[work_next] = mcu_rows - jpeg_rows[work_next] * (rpConfig->coreCount - 1);

	/* I cannot explain how this code works and I think it's trash.
	   Its purpose is to dynamically adjust the work load of the last encoding thread.
	   It sort of works but I can't explain it so it really need some fixing. */
	if (rpConfig->coreCount > 1) {
		if (jpeg_rows[work_prev]) {
			int rows = jpeg_rows[work_next];
			int rows_last = jpeg_rows_last[work_next];
			int progress_last = progress[rpConfig->coreCount - 1];
			if (progress_last < jpeg_adjusted_rows_last[work_prev]) {
				rows_last = (rows_last * (1 << 16) *
					progress_last / jpeg_rows_last[work_prev] + (1 << 15)) >> 16;
				if (rows_last > jpeg_rows_last[work_next])
					rows_last = jpeg_rows_last[work_next];
				if (rows_last == 0)
					rows_last = 1;
				rows = (mcu_rows - rows_last) / (rpConfig->coreCount - 1);
			} else {
				int progress_rest = 0;
				for (u32 j = 0; j < rpConfig->coreCount - 1; ++j) {
					progress_rest += progress[j];
				}
				rows = (rows * (1 << 16) *
					progress_rest / jpeg_rows[work_prev] / (rpConfig->coreCount - 1) + (1 << 15)) >> 16;
				if (rows < jpeg_rows[work_next])
					rows = jpeg_rows[work_next];
				int rows_max = (mcu_rows - 1) / (rpConfig->coreCount - 1);
				if (rows > rows_max)
					rows = rows_max;
			}
			jpeg_adjusted_rows[work_next] = rows;
			jpeg_adjusted_rows_last[work_next] = mcu_rows - rows * (rpConfig->coreCount - 1);
		} else {
			jpeg_adjusted_rows[work_next] = jpeg_rows[work_next];
			jpeg_adjusted_rows_last[work_next] = jpeg_rows_last[work_next];
		}
	} else {
		jpeg_adjusted_rows[work_next] = jpeg_rows[work_next];
		jpeg_adjusted_rows_last[work_next] = jpeg_rows_last[work_next];
	}

	j_compress_ptr cinfo;
	for (u32 j = 0; j < rpConfig->coreCount; ++j) {
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
		ctx->irow_count[j] = j == rpConfig->coreCount - 1 ? jpeg_adjusted_rows_last[work_next] : cinfo->restart_in_rows;
	}
	ctx->capture_next = 0;
}

static Handle rpHDma[rp_work_count], rpHandleHome, rpHandleGame, rpGamePid;
static u32 rpGameFCRAMBase = 0;

static u32 frameCount[SCREEN_COUNT];
static int screenBusyWait[rp_work_count];
static u32 rpCurrentUpdating;
static u32 rpIsPriorityTop;
static u32 rpPriorityFactor;
static u32 rpPriorityFactorLogScaled;
static int nextScreenCaptured[rp_work_count];
static int nextScreenSynced[rp_work_count];
static u32 frameQueued[SCREEN_COUNT];
static u32 rpPortGamePid;

#define LOG(x) FIX(log(x) / log(2))
static u32 const log_scaled_tab[] = {
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

static void rpTryCaptureNextScreen(u32 thread_id, u8 *capture_next, u32 work_next) {
	if (!ATSR(capture_next)) {
		work_next = (work_next + 1) % rp_work_count;
		ASR(&rp_screen_work_next, work_next);
		s32 count;
		s32 res = svcReleaseSemaphore(&count, rp_syn->screenCapSem, 1);
		if (res != 0) {
			nsDbgPrint("(%"PRIx32") Release semaphore screenCapSem (%"PRIx32") failed: %"PRIx32"\n", thread_id, work_next, res);
		}
	}
}

static void rpJPEGCompress0(j_compress_ptr cinfo,
	u8 *src, u32 pitch,
	int irow_start, int irow_count,
	u32 work_next, u32 thread_id, u8 *capture_next
) {
	JDIMENSION in_rows_blk = DCTSIZE * cinfo->max_v_samp_factor;
	JDIMENSION in_rows_blk_half = in_rows_blk / 2;

	JSAMPIMAGE output_buf = prep_buffers[work_next][thread_id];
	JSAMPIMAGE color_buf = color_buffers[work_next][thread_id];

	JSAMPROW input_buf[in_rows_blk_half];

	u32 j_max = in_rows_blk * (irow_start + irow_count);
	j_max = MIN(j_max, cinfo->image_height);
	u32 j_max_half = in_rows_blk * (irow_start + irow_count / 2);
	j_max_half = MIN(j_max_half, cinfo->image_height);

	u32 j_start = in_rows_blk * irow_start;
	if (j_max_half == j_start)
		rpTryCaptureNextScreen(thread_id, capture_next, work_next);

	for (u32 j = j_start, progress = 0; j < j_max;) {
		for (u32 i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 0);

		for (u32 i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 1);

		if (j_max_half == j)
			rpTryCaptureNextScreen(thread_id, capture_next, work_next);

		JBLOCKROW *MCU_buffer = MCU_buffers[work_next][thread_id];
		for (u32 k = 0; k < cinfo->MCUs_per_row; ++k) {
			jpeg_compress_data(cinfo, output_buf, MCU_buffer, k);
			jpeg_encode_mcu_huff(cinfo, MCU_buffer);
		}

		ASR(&jpeg_progress[work_next][thread_id], ++progress);
	}
}

static void rpSendFramesMain(u32 thread_id, BLIT_CONTEXT *ctx, u32 work_next) {
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

	if (thread_id != rpConfig->coreCount - 1) {
		jpeg_emit_marker(cinfo, JPEG_RST0 + thread_id);
	} else {
		jpeg_write_file_trailer(cinfo);
	}
	jpeg_term_destination(cinfo);
}

static int rpSendFrames(u32 thread_id, u32 work_next) {
	BLIT_CONTEXT *ctx = &blit_context[work_next];
	struct rp_work_syn_t *syn = &rp_syn->work[work_next];

	u8 skip_frame = 0;

	if (!ATSR(&syn->sem_set)) {
		int format_changed = 0;
		ctx->isTop = rpCurrentUpdating;
		if (ctx->isTop) {
			for (u32 j = 0; j < rpConfig->coreCount; ++j) {
				ctx->cinfos[j] = &cinfos_top[work_next][j];
				ctx->cinfos_alloc_stats[j] = &alloc_stats_top[work_next][j];
			}

			format_changed = rpCtxInit(ctx, 400, 240, tl_format, imgBuffer[1][imgBuffer_work_next[1]]);
			ctx->id = (u8)currentTopId;
		} else {
			for (u32 j = 0; j < rpConfig->coreCount; ++j) {
				ctx->cinfos[j] = &cinfos_bot[work_next][j];
				ctx->cinfos_alloc_stats[j] = &alloc_stats_bot[work_next][j];
			}

			format_changed = rpCtxInit(ctx, 320, 240, bl_format, imgBuffer[0][imgBuffer_work_next[0]]);
			ctx->id = (u8)currentBottomId;
		}

		s32 res;
		res = svcWaitSynchronization(rpHDma[work_next], 1000000000);

		int imgBuffer_work_prev = imgBuffer_work_next[ctx->isTop];
		if (imgBuffer_work_prev == 0)
			imgBuffer_work_prev = rp_screen_work_count - 1;
		else
			--imgBuffer_work_prev;

		skip_frame = !format_changed && memcmp(ctx->src, imgBuffer[ctx->isTop][imgBuffer_work_prev], ctx->width * ctx->src_pitch) == 0;

		s32 count;
		if (!skip_frame) {
			res = rpReadyNwm(thread_id, work_next, ctx->id, ctx->isTop);
			if (res != 0) {
				skip_frame = 1;
			} else {
				imgBuffer_work_next[ctx->isTop] = (imgBuffer_work_next[ctx->isTop] + 1) % rp_screen_work_count;
				ctx->isTop ? ++currentTopId : ++currentBottomId;
				rpReadyWork(ctx, work_next);
			}
		} else {
			res = svcReleaseSemaphore(&count, syn->sem_end, 1);
			if (res) {
			}
			res = svcReleaseSemaphore(&count, rp_syn->screenCapSem, 1);
			if (res != 0) {
				nsDbgPrint("(%"PRIx32") Release semaphore screenCapSem (%"PRIx32") failed: %"PRIx32"\n", thread_id, work_next, res);
			}
		}

		ASR(&rp_skip_frame[work_next], skip_frame);
		for (u32 j = 0; j < rpConfig->coreCount; ++j) {
			if (j != thread_id) {
				res = svcReleaseSemaphore(&count, rp_syn->thread[j].sem_work, 1);
				if (res) {
					nsDbgPrint("(%"PRIx32") Release semaphore sem_work[%"PRIx32"] (%"PRIx32") failed: %"PRIx32"\n", thread_id, j, work_next, res);
				}
			}
		}
	} else {
		while (1) {
			s32 res = svcWaitSynchronization(rp_syn->thread[thread_id].sem_work, 100000000);
			if (res) {
				continue;
			}
			break;
		}
		skip_frame = ALR(&rp_skip_frame[work_next]);
	}

	if (!skip_frame)
		rpSendFramesMain(thread_id, ctx, work_next);

	if (AAFR(&syn->sem_count, 1) == rpConfig->coreCount) {
		ASR(&syn->sem_count, 0);
		ACR(&syn->sem_set);

		if (!skip_frame) {
			s32 count;
			s32 res = svcReleaseSemaphore(&count, syn->sem_end, 1);
			if (res) {
			}
		}
	}

	return skip_frame;
}

static void rpThreadMainSendFrames(void) {
	while (!ALR(&rpResetThreads)) {
		struct rp_thread_syn_t *syn = &rp_syn->thread[0];
		s32 res = svcWaitSynchronization(syn->sem_start, 100000000);
		if (res) {
			continue;
		}

		int ret = rpSendFrames(0, rp_work_next);

		if (ret == 0)
			rp_work_next = (rp_work_next + 1) % rp_work_count;
	}
}

static void rpThreadAux(u32 thread_id) {
	u32 work_next = 0;
	while (!ALR(&rpResetThreads)) {
		int res;

		struct rp_thread_syn_t *syn = &rp_syn->thread[thread_id];

		res = svcWaitSynchronization(syn->sem_start, 100000000);
		if (res) {
			continue;
		}

		res = rpSendFrames(thread_id, work_next);

		if (res == 0)
			work_next = (work_next + 1) % rp_work_count;
	}
	svcExitThread();
}

static void rpNwmThread(u32) {
	while (!ALR(&rpResetThreads)) {
		while (rpTrySendNextBuffer(1) == 0) svcSleepThread(rpMinIntervalBetweenPacketsInNS);
		if (ALR(&rpResetThreads))
			break;
		s32 ret = svcWaitSynchronization(rp_syn->nwmEvent, 100000000);
		if (ret != 0) {
			if (ret != RES_TIMEOUT) {
				nsDbgPrint("nwmEvent wait error: %08"PRIx32"\n", ret);
				svcSleepThread(1000000000);
			}
		}
	}

	svcExitThread();
}

static int isInVRAM(u32 phys) {
	if (phys >= 0x18000000) {
		if (phys < 0x18000000 + 0x00600000) {
			return 1;
		}
	}
	return 0;
}

static int isInFCRAM(u32 phys) {
	if (phys >= 0x20000000) {
		if (phys < 0x20000000 + 0x10000000) {
			return 1;
		}
	}
	return 0;
}

static void rpCloseGameHandle(void) {
	if (rpHandleGame) {
		svcCloseHandle(rpHandleGame);
		rpHandleGame = 0;
		rpGameFCRAMBase = 0;
		rpGamePid = 0;

		rpShowNextFrameBothScreen();
	}
}

static Handle rpGetGameHandle(void) {
	int i, res;
	Handle hProcess;
	u32 pids[LOCAL_PID_BUF_COUNT];
	s32 pidCount;
	u32 tid[2];

	u32 gamePid = ALR(&rpConfig->gamePid);
	if (gamePid != rpGamePid) {
		rpCloseGameHandle();
		rpGamePid = gamePid;
	}

	if (rpHandleGame == 0) {
		if (gamePid != 0) {
			res = svcOpenProcess(&hProcess, gamePid);
			if (res == 0) {
				rpHandleGame = hProcess;
			}
		}
		if (rpHandleGame == 0) {
			res = svcGetProcessList(&pidCount, pids, LOCAL_PID_BUF_COUNT);
			if (res == 0) {
				for (i = 0; i < pidCount; ++i) {
					if (pids[i] < 0x28)
						continue;

					res = svcOpenProcess(&hProcess, pids[i]);
					if (res == 0) {
						res = getProcessTIDByHandle(hProcess, tid);
						if (res == 0) {
							if ((tid[1] & 0xFFFF) == 0) {
								rpHandleGame = hProcess;
								gamePid = pids[i];
								break;
							}
						}
						svcCloseHandle(hProcess);
					}
				}
			}
		}
		if (rpHandleGame == 0) {
			return 0;
		}
	}
	if (rpGameFCRAMBase == 0) {
		if (svcFlushProcessDataCache(rpHandleGame, 0x14000000, 0x1000) == 0) {
			rpGameFCRAMBase = 0x14000000;
		}
		else if (svcFlushProcessDataCache(rpHandleGame, 0x30000000, 0x1000) == 0) {
			rpGameFCRAMBase = 0x30000000;
		}
		else {
			rpCloseGameHandle();
			return 0;
		}

		nsDbgPrint("FCRAM game process: pid 0x%04"PRIx32", base 0x%08"PRIx32"\n", gamePid, rpGameFCRAMBase);
	}
	return rpHandleGame;
}

static int rpCaptureScreen(u32 work_next, int isTop) {
	u32 phys = isTop ? tl_current : bl_current;
	void *dest = imgBuffer[isTop][imgBuffer_work_next[isTop]];
	Handle hProcess = rpHandleHome;

	u32 format = (isTop ? tl_format : bl_format) & 0x0f;

	if (format >= 3)
		goto final;

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

	/* Is there a point on using the largest burst size possible? */
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
		nsDbgPrint("bufSize exceeds imgBuffer: %"PRIx32" (%"PRIx32")\n", bufSize, (u32)rp_img_buffer_size);
		goto final;
	}

	svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, (u32)dest, bufSize);
	if (rpHDma[work_next]) {
		svcCloseHandle(rpHDma[work_next]);
		rpHDma[work_next] = 0;
	}

	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		res = svcStartInterProcessDma(&rpHDma[work_next], CUR_PROCESS_HANDLE,
			(u32)dest, hProcess, 0x1F000000 + (phys - 0x18000000), bufSize, &dmaConfig);
		if (res < 0) {
			goto final;
		}
		return 0;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			res = svcStartInterProcessDma(&rpHDma[work_next], CUR_PROCESS_HANDLE,
				(u32)dest, hProcess, rpGameFCRAMBase + (phys - 0x20000000), bufSize, &dmaConfig);
			if (res < 0) {
				rpCloseGameHandle();

				svcSleepThread(50000000);
				rpHDma[work_next] = 0;
				return -1;
			}
			return 0;
		}
		svcSleepThread(50000000);
		return -1;
	}

final:
	u32 pid = 0;
	svcGetProcessId(&pid, hProcess);
	nsDbgPrint("Capture screen failed: phys %08"PRIx32", hProc %08"PRIx32", pid %04"PRIx32"\n", phys, hProcess, pid);
	svcSleepThread(1000000000);
	rpHDma[work_next] = 0;
	return -1;
}

static void rpKernelCallback(int isTop) {
	u32 current_fb;
	if (isTop) {
		tl_fbaddr[0] = REG(GPU_FB_TOP_LEFT_ADDR_1);
		tl_fbaddr[1] = REG(GPU_FB_TOP_LEFT_ADDR_2);
		tl_format = REG(GPU_FB_TOP_FMT);
		tl_pitch = REG(GPU_FB_TOP_STRIDE);
		current_fb = REG(GPU_FB_TOP_SEL);
		current_fb &= 1;
		tl_current = tl_fbaddr[current_fb];

		int full_width = !(tl_format & (7 << 4));
		/* for full-width top screen (800x240), output every other column */
		if (full_width)
			tl_pitch *= 2;
	} else {
		bl_fbaddr[0] = REG(GPU_FB_BOTTOM_ADDR_1);
		bl_fbaddr[1] = REG(GPU_FB_BOTTOM_ADDR_2);
		bl_format = REG(GPU_FB_BOTTOM_FMT);
		bl_pitch = REG(GPU_FB_BOTTOM_STRIDE);
		current_fb = REG(GPU_FB_BOTTOM_SEL);
		current_fb &= 1;
		bl_current = bl_fbaddr[current_fb];
	}
}

static void rpDoWaitForVBlank(int isTop) {
	gspWaitForEvent(isTop ? GSPGPU_EVENT_VBlank0 : GSPGPU_EVENT_VBlank1, 0);
}

static u32 rpGetPrioScaled(u32 isTop) {
	return isTop == rpIsPriorityTop ? 1 << SCALEBITS : rpPriorityFactorLogScaled;
}

static void rpCaptureNextScreen(u32 work_next, int wait_sync) {
	struct rp_work_syn_t *syn = &rp_syn->work[work_next];
	s32 res;

	if (!nextScreenSynced[work_next]) {
		res = svcWaitSynchronization(syn->sem_end, wait_sync ? 100000000 : 0);
		if (res) {
			if (wait_sync && res != RES_TIMEOUT) {
				svcSleepThread(1000000000);
			}
			return;
		}
		nextScreenSynced[work_next] = 1;
	}

	rpCurrentUpdating = rpIsPriorityTop;
	ASR(&screenBusyWait[work_next], 0);

	while (!ALR(&rpResetThreads)) {
		if (!ALR(&rpPortGamePid)) {
			if (rpPriorityFactor != 0) {
				if (frameCount[rpCurrentUpdating] >= rpPriorityFactor) {
					if (frameCount[!rpCurrentUpdating] >= 1) {
						frameCount[rpCurrentUpdating] -= rpPriorityFactor;
						frameCount[!rpCurrentUpdating] -= 1;
						rpCurrentUpdating = !rpCurrentUpdating;
					}
				}
			}
			ASR(&screenBusyWait[work_next], 1);
			break;
		}

		s32 isTop = rpCurrentUpdating;

		if (rpPriorityFactor == 0) {
			if ((res = svcWaitSynchronization(rp_syn->portEvent[isTop], 100000000)) == 0) {
				break;
			}
			continue;
		}

		u32 prio[SCREEN_COUNT];
		prio[isTop] = rpGetPrioScaled(isTop);
		prio[!isTop] = rpGetPrioScaled(!isTop);

		u32 factor[2];
		factor[isTop] = (u64)(1 << SCALEBITS) * (s64)frameQueued[isTop] / (s64)prio[isTop];
		factor[!isTop] = (u64)(1 << SCALEBITS) * (s64)frameQueued[!isTop] / (s64)prio[!isTop];

		if (factor[isTop] < rpPriorityFactorLogScaled && factor[!isTop] < rpPriorityFactorLogScaled) {
			frameQueued[0] += rpPriorityFactorLogScaled;
			frameQueued[1] += rpPriorityFactorLogScaled;
			continue;
		}

		isTop = factor[isTop] >= factor[!isTop] ? isTop : !isTop;

		if (frameQueued[isTop] >= prio[isTop]) {
			if ((res = svcWaitSynchronization(rp_syn->portEvent[isTop], 0)) == 0) {
				rpCurrentUpdating = isTop;
				frameQueued[isTop] -= prio[isTop];
				break;
			}
		}

		if (frameQueued[!isTop] >= prio[!isTop]) {
			if ((res = svcWaitSynchronization(rp_syn->portEvent[!isTop], 0)) == 0) {
				rpCurrentUpdating = !isTop;
				frameQueued[!isTop] -= prio[!isTop];
				break;
			}
		}

		res = svcWaitSynchronizationN(&isTop, rp_syn->portEvent, 2, 0, 100000000);
		if (res != 0) {
			if (res != RES_TIMEOUT) {
				nsDbgPrint("wait rp_syn->portEvent all error: %08"PRIx32"\n", res);
				svcSleepThread(1000000000);
				break;
			}
			continue;
		}

		if (frameQueued[isTop] >= prio[isTop]) {
			rpCurrentUpdating = isTop;
			frameQueued[isTop] -= prio[isTop];
		} else {
			rpCurrentUpdating = isTop;
			frameQueued[isTop] = 0;
		}
		break;
	}
	if (ALR(&rpResetThreads))
		return;

	if (screenBusyWait[work_next])
		rpDoWaitForVBlank(rpCurrentUpdating);
	rpKernelCallback(rpCurrentUpdating);
	int captured = rpCaptureScreen(work_next, rpCurrentUpdating) == 0;
	if (captured) {
		if (screenBusyWait[work_next])
			frameCount[rpCurrentUpdating] += 1;
		nextScreenCaptured[work_next] = captured;
		nextScreenSynced[work_next] = 0;

		for (u32 j = 0; j < rpConfig->coreCount; ++j) {
			s32 count;
			res = svcReleaseSemaphore(&count, rp_syn->thread[j].sem_start, 1);
			if (res) {
				nsDbgPrint("Release semaphore sem_start failed: %"PRIx32"\n", res);
			}
		}
	}
}

static void rpScreenCaptureThread(u32) {
	while (!ALR(&rpResetThreads)) {
		s32 ret = svcWaitSynchronization(rp_syn->screenCapSem, 100000000);
		if (ret != 0) {
			if (ret != RES_TIMEOUT) {
				nsDbgPrint("screenCapEvent wait error: %08"PRIx32"\n", ret);
				svcSleepThread(1000000000);
			}
			continue;
		}
		u32 work_next = ALR(&rp_screen_work_next);
		while (!nextScreenCaptured[work_next] && !ALR(&rpResetThreads))
			rpCaptureNextScreen(work_next, 1);
		nextScreenCaptured[work_next] = 0;
	}

	svcExitThread();
}

#define rpPortSessionsMax 4
static void rpPortThread(u32) {
	s32 ret;
	Handle hServer = 0, hClient = 0;
	ret = svcCreatePort(&hServer, &hClient, SVC_PORT_NWM, rpPortSessionsMax);
	if (ret != 0) {
		nsDbgPrint("Create port failed: %08"PRIx32"\n", ret);
		svcExitThread();
	}

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
			if (ret == (s32)RES_HANDLE_CLOSED) {
				nsDbgPrint("Port handle closed: %08"PRIx32" (%"PRIx32")\n", hHandles[handleIndex], hHandlesMap[handleIndex]);
				handleReply = 0;
				cmdbuf[0] = 0xFFFF0000;
				svcCloseHandle(hHandles[handleIndex]);
				hSessions[hHandlesMap[handleIndex]] = 0;
				continue;
			}

			handleReply = 0;
			cmdbuf[0] = 0xFFFF0000;
			nsDbgPrint("Port reply and receive error: %08"PRIx32"\n", ret);
			continue;
		}

		if (hHandlesMap[handleIndex] == rpPortSessionsMax) {
			handleReply = 0;
			cmdbuf[0] = 0xFFFF0000;
			Handle hSession;
			ret = svcAcceptSession(&hSession, hServer);
			if (ret != 0) {
				nsDbgPrint("hServer accept error: %08"PRIx32"\n", ret);
				continue;
			}

			for (i = 0; i < rpPortSessionsMax; ++i) {
				if (hSessions[i] == 0) {
					hSessions[i] = hSession;
					break;
				}
			}
			if (i >= rpPortSessionsMax) {
				nsDbgPrint("Port session max exceeded\n");
				svcCloseHandle(hSession);
			}
			continue;
		}

		handleReply = hHandles[handleIndex];
		u32 cmd_id = cmdbuf[0] >> 16;
		u32 norm_param_count = (cmdbuf[0] >> 6) & 0x3F;
		u32 gamePid = norm_param_count >= 1 ? cmdbuf[1] : 0;
		u32 isTop = cmd_id - 1;
		if (isTop > 1) {
			ASR(&rpPortGamePid, 0);
		} else {
			if (ALR(&rpPortGamePid) != gamePid)
				ASR(&rpPortGamePid, gamePid);

			ret = svcSignalEvent(rp_syn->portEvent[isTop]);
			if (ret != 0) {
				nsDbgPrint("Signal port event failed: %08"PRIx32"\n", ret);
			}
		}

		cmdbuf[0] = IPC_MakeHeader(cmd_id, 0, 0);
	}

	if (hServer)
		svcCloseHandle(hServer);
	if (hClient)
		svcCloseHandle(hClient);

	svcExitThread();
}

Result __sync_init(void);
void __system_initSyscalls(void);
static void rpThreadMain(void *) {
	s32 res;
	res = __sync_init();
	if (res != 0) {
		nsDbgPrint("sync init failed: %08"PRIx32"\n", res);
		goto final;
	}
	__system_initSyscalls();
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	res = gspInit(1);
	if (res != 0) {
		nsDbgPrint("gsp init failed: %08"PRIx32"\n", res);
		goto final;
	}
	nsDbgPrint("gsp initted\n");

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

	for (j = 0; j < SCREEN_COUNT; ++j) {
		for (i = 0; i < rp_screen_work_count; ++i) {
			imgBuffer[j][i] = (u8 *)plgRequestMemory(rp_img_buffer_size);
			if (!imgBuffer[j][i]) {
				goto final;
			}
		}
	}

	for (i = 0; i < rp_cinfos_count; ++i) {
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
	if (!rp_syn) {
		goto final;
	}
	ret = svcCreateEvent(&rp_syn->portEvent[0], RESET_ONESHOT);
	if (ret != 0) {
		nsDbgPrint("create event portEvent[0] failed: %08"PRIx32"\n", ret);
		goto final;
	}
	ret = svcCreateEvent(&rp_syn->portEvent[1], RESET_ONESHOT);
	if (ret != 0) {
		nsDbgPrint("create event portEvent[1] failed: %08"PRIx32"\n", ret);
		goto final;
	}
	ret = svcCreateEvent(&rp_syn->nwmEvent, RESET_ONESHOT);
	if (ret != 0) {
		nsDbgPrint("create event nwmEvent failed: %08"PRIx32"\n", ret);
		goto final;
	}

	u32 *threadSvcStack = (u32 *)plgRequestMemory(STACK_SIZE);
	Handle hSvcThread;
	ret = svcCreateThread(&hSvcThread, (void*)rpPortThread, 0, &threadSvcStack[(STACK_SIZE / 4) - 10], 0x10, 1);
	if (ret != 0) {
		nsDbgPrint("Create remote play service thread failed: %08"PRIx32"\n", ret);
	}

	ret = svcOpenProcess(&rpHandleHome, ntrConfig->HomeMenuPid);
	if (ret != 0) {
		nsDbgPrint("open home menu process failed: %08"PRIx32"\n", ret);
		goto final;
	}

	u32 *threadAux1Stack = (u32 *)plgRequestMemory(RP_THREAD_STACK_SIZE);
	u32 *threadAux2Stack = (u32 *)plgRequestMemory(RP_THREAD_STACK_SIZE);
	u32 *threadNwmStack = (u32 *)plgRequestMemory(STACK_SIZE);
	u32 *threadScreenCapStack = (u32 *)plgRequestMemory(STACK_SIZE);

	while (1) {
		ASR(&rpResetThreads, 0);

		// TODO
		// Could really use some OOP stuff

		if (rpConfig->coreCount < 1)
			rpConfig->coreCount = 1;
		else if (rpConfig->coreCount > rp_thread_count)
			rpConfig->coreCount = rp_thread_count;

		{
			u32 isTop = 1;
			rpPriorityFactor = 0;
			u32 mode = (rpConfig->mode & 0xff00) >> 8;
			u32 factor = (rpConfig->mode & 0xff);
			if (mode == 0) {
				isTop = 0;
			}
			rpIsPriorityTop = isTop;
			rpPriorityFactor = factor;
			rpPriorityFactorLogScaled = log_scaled_tab[factor];

			rpShowNextFrameBothScreen();
		}

		if (rpConfig->dstPort == 0)
			rpConfig->dstPort = RP_DST_PORT_DEFAULT;

		rpMinIntervalBetweenPacketsInTick = (u64)SYSCLOCK_ARM11 * PACKET_SIZE / rpConfig->qos;
		rpMinIntervalBetweenPacketsInNS = (u64)rpMinIntervalBetweenPacketsInTick * 1000000000 / SYSCLOCK_ARM11;

		{
			for (j = 0; j < rp_cinfos_count; ++j)
				cinfos[j]->global_state = JPEG_CSTATE_START;
			jpeg_set_quality(cinfos[0], rpConfig->quality, TRUE);
			for (j = 1; j < rp_cinfos_count; ++j)
				for (i = 0; i < NUM_QUANT_TBLS; ++i)
					cinfos[j]->quant_tbl_ptrs[i] = cinfos[0]->quant_tbl_ptrs[i];

			for (i = 0; i < rp_work_count; ++i) {
				for (j = 0; j < rp_thread_count; ++j) {
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

			for (j = 1; j < rp_cinfos_count; ++j) {
				cinfos[j]->fdct = cinfos[0]->fdct;
				cinfos[j]->fdct_reuse = TRUE;
			}

			jpeg_start_pass_fdctmgr(cinfos[0]);

			for (int h = 0; h < rp_work_count; ++h) {
				for (i = 0; i < (sizeof(*prep_buffers) / sizeof(**prep_buffers)); ++i) {
					for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
						prep_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
							240, (JDIMENSION)(MAX_SAMP_FACTOR * DCTSIZE));
					}
				}
				for (i = 0; i < (sizeof(*color_buffers) / sizeof(**color_buffers)); ++i) {
					for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
						color_buffers[h][i][ci] = jpeg_alloc_sarray((j_common_ptr)cinfos[h], JPOOL_IMAGE,
							240, (JDIMENSION)MAX_SAMP_FACTOR);
					}
				}
				for (i = 0; i < (sizeof(*MCU_buffers) / sizeof(**MCU_buffers)); ++i) {
					JBLOCKROW buffer = (JBLOCKROW)jpeg_alloc_large((j_common_ptr)cinfos[h], JPOOL_IMAGE, C_MAX_BLOCKS_IN_MCU * sizeof(JBLOCK));
					for (int b = 0; b < C_MAX_BLOCKS_IN_MCU; b++) {
						MCU_buffers[h][i][b] = buffer + b;
					}
				}
			}

			for (int i = 0; i < rp_work_count; ++i) {
				for (j = 0; j < rpConfig->coreCount; ++j) {
					memcpy(&alloc_stats_top[i][j].comp, &cinfos_top[i][j].alloc.stats, sizeof(struct rp_alloc_stats));
					memcpy(&alloc_stats_bot[i][j].comp, &cinfos_bot[i][j].alloc.stats, sizeof(struct rp_alloc_stats));
				}
			}
		}

		ret = svcSetThreadPriority(hThreadMain, rpConfig->threadPriority);
		if (ret != 0) {
			nsDbgPrint("set main encoding thread priority failed: %08"PRIx32"\n", res);
		}

		rpCurrentUpdating = rpIsPriorityTop;
		frameCount[0] = frameCount[1] = 1;
		frameQueued[0] = frameQueued[1] = rpPriorityFactorLogScaled;
		for (int i = 0; i < rp_work_count; ++i) {
			nextScreenCaptured[i] = 0;
		}
		rpLastSendTick = svcGetSystemTick();

		for (i = 0; i < rp_work_count; ++i) {
			for (j = 0; j < rpConfig->coreCount; ++j) {
				struct rpDataBufInfo_t *info = &rpDataBufInfo[i][j];
				info->sendPos = info->pos = rpDataBuf[i][j] + rp_data_hdr_size;
				info->flag = 0;

				jpeg_progress[i][j] = 0;
			}
			jpeg_rows[i] = 0;
			jpeg_rows_last[i] = 0;
			jpeg_adjusted_rows[i] = 0;
			jpeg_adjusted_rows_last[i] = 0;

			rp_nwm_syn_next[i] = 1;
		}
		rp_nwm_work_next = rp_nwm_thread_next = 0;
		rp_work_next = 0;
		rp_screen_work_next = 0;

		ret = svcCreateSemaphore(&rp_syn->screenCapSem, 1, 1);
		if (ret != 0) {
			nsDbgPrint("Create semaphore screenCapSem failed: %08"PRIx32"\n", ret);
			goto final;
		}

		for (i = 0; i < rp_work_count; ++i) {
			ret = svcCreateSemaphore(&rp_syn->work[i].sem_end, 1, 1);
			if (ret != 0) {
				nsDbgPrint("Create semaphore sem_end (%"PRId32") failed: %08"PRIx32"\n", i, ret);
				goto final;
			}
			ret = svcCreateSemaphore(&rp_syn->work[i].sem_nwm, 1, 1);
			if (ret != 0) {
				nsDbgPrint("Create semaphore sem_nwm (%"PRId32") failed: %08"PRIx32"\n", i, ret);
				goto final;
			}
			ret = svcCreateSemaphore(&rp_syn->work[i].sem_send, 0, 1);
			if (ret != 0) {
				nsDbgPrint("Create semaphore sem_send (%"PRId32") failed: %08"PRIx32"\n", i, ret);
				goto final;
			}

			rp_syn->work[i].sem_count = 0;
			rp_syn->work[i].sem_set = 0;
		}
		for (j = 0; j < rpConfig->coreCount; ++j) {
			ret = svcCreateSemaphore(&rp_syn->thread[j].sem_start, 0, rp_work_count);
			if (ret != 0) {
				nsDbgPrint("Create semaphore sem_start (%"PRId32") failed: %08"PRIx32"\n", i, ret);
				goto final;
			}
			ret = svcCreateSemaphore(&rp_syn->thread[j].sem_work, 0, rp_work_count);
			if (ret != 0) {
				nsDbgPrint("Create semaphore sem_work (%"PRId32") failed: %08"PRIx32"\n", i, ret);
				goto final;
			}
		}

		Handle hThreadAux1;
		if (rpConfig->coreCount >= 2) {
			ret = svcCreateThread(&hThreadAux1, (void*)rpThreadAux, 1, &threadAux1Stack[(RP_THREAD_STACK_SIZE / 4) - 10], rpConfig->threadPriority, 3);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Aux Thread Failed: %08"PRIx32"\n", ret);
				goto final;
			}
		}

		Handle hThreadAux2;
		if (rpConfig->coreCount >= 3) {
			ret = svcCreateThread(&hThreadAux2, (void*)rpThreadAux, 2, &threadAux2Stack[(RP_THREAD_STACK_SIZE / 4) - 10], 0x3f, 1);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Aux Thread Failed: %08"PRIx32"\n", ret);
				goto final;
			}
		}

		Handle hThreadNwm;
		ret = svcCreateThread(&hThreadNwm, (void*)rpNwmThread, 0, &threadNwmStack[(STACK_SIZE / 4) - 10], 0x8, 1);
		if (ret != 0) {
			nsDbgPrint("Create remote play network thread Failed: %08"PRIx32"\n", ret);
			goto final;
		}

		Handle hThreadScreenCap;
		ret = svcCreateThread(&hThreadScreenCap, (void*)rpScreenCaptureThread, 0, &threadScreenCapStack[(STACK_SIZE / 4) - 10], 0x8, 1);
		if (ret != 0) {
			nsDbgPrint("Create remote play screen capture thread Failed: %08"PRIx32"\n", ret);
			goto final;
		}

		rpThreadMainSendFrames();

		if (rpConfig->coreCount >= 3) {
			svcWaitSynchronization(hThreadAux2, -1);
			svcCloseHandle(hThreadAux2);
		}
		if (rpConfig->coreCount >= 2) {
			svcWaitSynchronization(hThreadAux1, -1);
			svcCloseHandle(hThreadAux1);
		}

		svcWaitSynchronization(hThreadNwm, -1);
		svcCloseHandle(hThreadNwm);

		svcWaitSynchronization(hThreadScreenCap, -1);
		svcCloseHandle(hThreadScreenCap);

		for (i = 0; i < rp_work_count; ++i) {
			svcCloseHandle(rp_syn->work[i].sem_end);
			svcCloseHandle(rp_syn->work[i].sem_nwm);
			svcCloseHandle(rp_syn->work[i].sem_send);
		}
		for (j = 0; j < rpConfig->coreCount; ++j) {
			svcCloseHandle(rp_syn->thread[j].sem_start);
			svcCloseHandle(rp_syn->thread[j].sem_work);
		}

		svcCloseHandle(rp_syn->screenCapSem);
	}

final:
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
						daddr4[0], daddr4[1], daddr4[2], daddr4[3]
					);

					needUpdate = 1;
				}
			}
			if (rpSrcAddr != saddr) {
				rpSrcAddr = saddr;

				u8 *saddr4 = (u8 *)&saddr;
				nsDbgPrint("Remote play updated src IP: %d.%d.%d.%d\n",
					saddr4[0], saddr4[1], saddr4[2], saddr4[3]
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
			saddr4[0], saddr4[1], saddr4[2], saddr4[3],
			daddr4[0], daddr4[1], daddr4[2], daddr4[3]
		);

		memcpy(rpNwmHdr, buf, 0x22 + 8);
		printNwMHdr();
		updateDstAddr(daddr);
		rpSrcAddr = saddr;

		u32 *threadStack = (u32 *)plgRequestMemory(RP_THREAD_STACK_SIZE);
		s32 ret = svcCreateThread(&hThreadMain, rpThreadMain, 0, &threadStack[(RP_THREAD_STACK_SIZE / 4) - 10], RP_THREAD_PRIO_DEFAULT, 2);
		if (ret != 0) {
			nsDbgPrint("Create remote play thread failed: %08"PRIx32"\n", ret);
		}
	}
}
