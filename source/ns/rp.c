#include "global.h"
#include "ctr/syn.h"
#include "ctr/res.h"

#include "umm_malloc.h"
#include "ikcp.h"
#include "libavcodec/jpegls.h"
#include "libavcodec/get_bits.h"
#include "libavfilter/motion_estimation.h"
#include "../jpeg_ls/global.h"
#include "../jpeg_ls/bitio.h"
#include "xxhash.h"

// #pragma GCC diagnostic warning "-Wall"
// #pragma GCC diagnostic warning "-Wextra"
// #pragma GCC diagnostic warning "-Wpedantic"

#define RP_ENCODE_VERIFY (0)
#define RP_ME_INTERPOLATE (0)
// extern IUINT32 IKCP_OVERHEAD;
#define IKCP_OVERHEAD (24)

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC (268123480)

#define RP_MAX(a,b) ((a) > (b) ? (a) : (b))
#define RP_MIN(a,b) ((a) > (b) ? (b) : (a))

typedef u32(*sendPacketTypedef) (u8*, u32);
static sendPacketTypedef nwmSendPacket = 0;
static RT_HOOK nwmValParamHook;

int rp_recv_sock = -1;

static u8 rpInited = 0;

#define KCP_PACKET_SIZE 1448
#define NWM_HEADER_SIZE (0x2a + 8)
#define NWM_PACKET_SIZE (KCP_PACKET_SIZE + NWM_HEADER_SIZE)

#define KCP_TIMEOUT_TICKS (2000 * SYSTICK_PER_MS)
#define RP_PACKET_SIZE (KCP_PACKET_SIZE - IKCP_OVERHEAD)
#define KCP_SND_WND_SIZE 96

#define RP_BANDWIDTH_CONTROL_RATIO_NUM 2
#define RP_BANDWIDTH_CONTROL_RATIO_DENUM 3

#define RP_DEST_PORT (8001)
#define RP_SCREEN_BUFFER_SIZE (400 * 240 * 4)
#define RP_UMM_HEAP_SIZE (256 * 1024)
#define RP_STACK_SIZE (0x8000)
#define RP_MISC_STACK_SIZE (0x1000)
#define RP_CONTROL_RECV_BUFFER_SIZE (2000)
#define RP_JLS_ENCODE_IMAGE_BUFFER_SIZE ((400 * 240) + (400 * 240) / 16)
#define RP_JLS_ENCODE_IMAGE_ME_BUFFER_SIZE (RP_JLS_ENCODE_IMAGE_BUFFER_SIZE / RP_ME_MIN_BLOCK_SIZE / RP_ME_MIN_BLOCK_SIZE)
#define RP_JLS_ENCODE_BUFFER_SIZE (RP_JLS_ENCODE_IMAGE_BUFFER_SIZE + RP_JLS_ENCODE_IMAGE_ME_BUFFER_SIZE)
#define RP_TOP_BOT_STR(top_bot) ((top_bot) == 0 ? "top" : "bot")
#define RP_ME_MIN_BLOCK_SIZE (4)
#define RP_ME_MIN_SEARCH_PARAM (4)

#define RP_ASSERT(c, ...) do { if (!(c)) { nsDbgPrint(__VA_ARGS__); } } while (0) \

// attribute aligned
#define ALIGN_4 __attribute__ ((aligned (4)))
// assume aligned
#define ASSUME_ALIGN_4(a) (a = __builtin_assume_aligned (a, 4))
#define UNUSED __attribute__((unused))
#define FALLTHRU __attribute__((fallthrough));
#define ALWAYS_INLINE __attribute__((always_inline)) inline

#define SCREEN_COUNT (2)
enum {
	RP_ENCODE_PARAMS_BPP8,
	RP_ENCODE_PARAMS_BPP5,
	RP_ENCODE_PARAMS_BPP6,
	RP_ENCODE_PARAMS_BPP4,
	RP_ENCODE_PARAMS_COUNT
};
#define RP_ENCODE_MULTITHREAD (1)
#define RP_ENCODE_THREAD_COUNT (1 + RP_ENCODE_MULTITHREAD)
// (+ 1) for screen/network transfer then (+ 1) again for start/finish at different time
#define RP_ENCODE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 2)
// (+ 1) for motion estimation reference
#define RP_IMAGE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 1)
static struct {
	ikcpcb *kcp;
	u8 kcp_restart;
	Handle kcp_mutex;
	u8 kcp_ready;

	u8 exit_thread;
	Handle second_thread;
	Handle screen_thread;
	Handle network_thread;

	u8 nwm_send_buffer[NWM_PACKET_SIZE] ALIGN_4;
	u8 kcp_send_buffer[KCP_PACKET_SIZE] ALIGN_4;
	u8 thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 second_thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 network_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 screen_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 control_recv_buffer[RP_CONTROL_RECV_BUFFER_SIZE] ALIGN_4;
	u8 umm_heap[RP_UMM_HEAP_SIZE] ALIGN_4;

	Handle screen_hdma[RP_ENCODE_BUFFER_COUNT];
	u8 screen_buffer[RP_ENCODE_BUFFER_COUNT][RP_SCREEN_BUFFER_SIZE] ALIGN_4;
	u8 screen_top_bot[RP_ENCODE_BUFFER_COUNT];
	struct jls_enc_params jls_enc_params[RP_ENCODE_PARAMS_COUNT];
	struct jls_enc_ctx jls_enc_ctx[RP_ENCODE_THREAD_COUNT];
	struct bito_ctx jls_bito_ctx[RP_ENCODE_THREAD_COUNT];
	struct {
		uint16_t vLUT_bpp8[2 * (1 << 8)][3];
		uint16_t vLUT_bpp5[2 * (1 << 5)][3];
		uint16_t vLUT_bpp6[2 * (1 << 6)][3];
		uint16_t vLUT_bpp4[2 * (1 << 4)][3];
		int16_t classmap[9 * 9 * 9];
	} jls_enc_luts;
	u8 jls_encode_buffer[RP_ENCODE_BUFFER_COUNT][RP_JLS_ENCODE_BUFFER_SIZE] ALIGN_4;
	struct {
		u8 top_bot;
		u8 frame_n;
		u8 bpp;
		u8 format;
		u32 size;
		u32 size_1;
		u8 p_frame;
	} jls_encode[RP_ENCODE_BUFFER_COUNT];
#if RP_ENCODE_VERIFY
	u8 jls_encode_verify_buffer[RP_ENCODE_THREAD_COUNT][RP_JLS_ENCODE_IMAGE_BUFFER_SIZE] ALIGN_4;
	u8 jls_decode_verify_buffer[RP_ENCODE_THREAD_COUNT][RP_JLS_ENCODE_IMAGE_BUFFER_SIZE] ALIGN_4;
	u8 jls_decode_verify_padded_buffer[RP_ENCODE_THREAD_COUNT][400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
#endif

	struct {
		s8 me_x_image[1];
		s8 me_y_image[1];
		u8 y_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 u_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 v_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_y_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_u_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_v_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_ds_y_image[100 * (60 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 y_bpp;
		u8 u_bpp;
		u8 v_bpp;
		u8 format;
		u8 me_bpp;
	} top_image[RP_IMAGE_BUFFER_COUNT];

	struct {
		s8 me_x_image[1];
		s8 me_y_image[1];
		u8 y_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 u_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 v_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_y_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_u_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_v_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_ds_y_image[80 * (60 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 y_bpp;
		u8 u_bpp;
		u8 v_bpp;
		u8 format;
		u8 me_bpp;
	} bot_image[RP_IMAGE_BUFFER_COUNT];

#define DIV_CEIL(n, d) (((n) + (d - 1)) / d)
#define ME_SIZE(w, h) DIV_CEIL(w, RP_ME_MIN_BLOCK_SIZE) * (DIV_CEIL(h, RP_ME_MIN_BLOCK_SIZE) + LEFTMARGIN + RIGHTMARGIN)
#define ME_TOP_SIZE ME_SIZE(400, 240)
#define ME_BOT_SIZE ME_SIZE(320, 240)

	struct {
		s8 me_x_image[ME_TOP_SIZE] ALIGN_4;
		s8 me_y_image[ME_TOP_SIZE] ALIGN_4;
		u8 y_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 u_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 v_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_u_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_v_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
	} top_image_me[RP_ENCODE_THREAD_COUNT];

	struct {
		s8 me_x_image[ME_BOT_SIZE] ALIGN_4;
		s8 me_y_image[ME_BOT_SIZE] ALIGN_4;
		u8 y_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 u_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 v_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_u_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_v_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
	} bot_image_me[RP_ENCODE_THREAD_COUNT];

	u8 image_buffer_n[SCREEN_COUNT];
	u8 image_frame_n[SCREEN_COUNT];
	u8 image_p_frame[SCREEN_COUNT];
	Handle image_mutex[SCREEN_COUNT];

	struct {
		struct {
			struct {
				u8 id;
				Handle sem, mutex;
				u8 pos_head, pos_tail;
				s8 pos[RP_ENCODE_BUFFER_COUNT];
			} transfer, encode;
		} screen, network;
	} syn;

	struct {
		u8 updated;

		u32 kcp_conv;

		u8 yuv_option;
		u8 color_transform_hp;
		u8 encoder_which;
		u8 downscale_uv;

		u8 me_method;
		u8 me_block_size;
		u8 me_block_size_log2;
		u8 me_search_param;
		u8 me_bpp;
		u8 me_bpp_half_range;
		u8 me_downscale;
		u8 me_interpolate;

		u8 target_frame_rate;
		u8 target_mbit_rate;
		u8 dynamic_priority;
		u8 top_priority;
		u8 bot_priority;
		u8 low_latency;
		u8 multicore_encode;
		u8 encode_buffer_count;

		u32 min_send_interval_ticks;
		u32 max_capture_interval_ticks;

		int arg0;
		int arg1;
		int arg2;
	} conf;

#define RP_DYN_PRIO_FRAME_COUNT 2
#define RP_IMAGE_CHANNEL_COUNT 3
	struct {
		struct {
			u16 frame_size_acc;
			u16 priority_size_acc;

			u8 frame_size[RP_DYN_PRIO_FRAME_COUNT];
			u8 priority_size[RP_DYN_PRIO_FRAME_COUNT];

			u8 frame_size_chn;
			u8 priority_size_chn;
			u8 chn_index;

			u8 frame_index;

			u32 tick[RP_DYN_PRIO_FRAME_COUNT];
			u8 frame_rate;
			u8 initializing;
			u8 priority;

			u16 frame_size_est;
			u16 priority_size_est;
		} top, bot;
		Handle mutex;
	} dyn_prio;

	Handle home_handle, game_handle;
	u32 game_fcram_base;

	u8 dma_config[24];
	struct {
		u32 format;
		u32 pitch;
		u32 fbaddr;
	} screen[2];
} *rp_ctx;

static u8 rp_atomic_fetch_addb_wrap(u8 *p, u8 a, u8 factor) {
	u8 v, v_new;
	do {
		v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
		v_new = (v + a) % factor;
	} while (!__atomic_compare_exchange_n(p, &v, v_new, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
	return v;
}

static void rp_syn_init1(typeof(rp_ctx->syn.screen.transfer) *syn1, int init, int id) {
	if (syn1->sem)
		svc_closeHandle(syn1->sem);
	svc_createSemaphore(&syn1->sem, init ? rp_ctx->conf.encode_buffer_count : 0, rp_ctx->conf.encode_buffer_count);
	if (syn1->mutex)
		svc_closeHandle(syn1->mutex);
	svc_createMutex(&syn1->mutex, 0);

	syn1->pos_head = syn1->pos_tail = 0;
	syn1->id = id;

	for (int i = 0; i < RP_ENCODE_BUFFER_COUNT; ++i) {
		syn1->pos[i] = init ? i : -1;
	}
}

static void rp_syn_init(typeof(rp_ctx->syn.screen) *syn, int transfer_encode, int id) {
	rp_syn_init1(&syn->transfer, transfer_encode == 0, id);
	rp_syn_init1(&syn->encode, transfer_encode == 1, id);
}

static s32 rp_syn_acq(typeof(rp_ctx->syn.screen.transfer) *syn1, s64 timeout) {
	Result res;
	if ((res = svc_waitSynchronization1(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) == RD_TIMEOUT)
			return -1;
		nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
			R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
	}
	u8 pos_tail = syn1->pos_tail;
	syn1->pos_tail = (pos_tail + 1) % rp_ctx->conf.encode_buffer_count;
	// nsDbgPrint("rp_syn_acq id %d at %d\n", syn1->id, pos_tail);
	s8 pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = -1;
	if (pos < 0) {
		nsDbgPrint("error rp_syn_acq id %d at pos %d\n", syn1->id, pos_tail);
		return 0;
	}
	return pos;
}

static void rp_syn_rel(typeof(rp_ctx->syn.screen.transfer) *syn1, s32 pos) {
	u8 pos_head = syn1->pos_head;
	syn1->pos_head = (pos_head + 1) % rp_ctx->conf.encode_buffer_count;
	syn1->pos[pos_head] = pos;
	// nsDbgPrint("rp_syn_rel id %d at %d: %d\n", syn1->id, pos_head, pos);
	s32 count;
	svc_releaseSemaphore(&count, syn1->sem, 1);
}

static s32 rp_syn_acq1(typeof(rp_ctx->syn.screen.transfer) *syn1, s64 timeout) {
	Result res;
	if ((res = svc_waitSynchronization1(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) == RD_TIMEOUT)
			return -1;
		nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
			R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
	}
	u8 pos_tail = rp_atomic_fetch_addb_wrap(&syn1->pos_tail, 1, rp_ctx->conf.encode_buffer_count);
	// nsDbgPrint("rp_syn_acq id %d at %d\n", syn1->id, pos_tail);
	s8 pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = -1;
	if (pos < 0) {
		nsDbgPrint("error rp_syn_acq id %d at pos %d\n", syn1->id, pos_tail);
		return 0;
	}
	return pos;
}

static void rp_syn_rel1(typeof(rp_ctx->syn.screen.transfer) *syn1, s32 pos) {
	svc_waitSynchronization1(syn1->mutex, U64_MAX);
	u8 pos_head = syn1->pos_head;
	syn1->pos_head = (pos_head + 1) % rp_ctx->conf.encode_buffer_count;
	syn1->pos[pos_head] = pos;
	svc_releaseMutex(syn1->mutex);
	// nsDbgPrint("rp_syn_rel1 id %d at %d: %d\n", syn1->id, pos_head, pos);
	s32 count;
	svc_releaseSemaphore(&count, syn1->sem, 1);
}

static void rp_screen_queue_init() {
	rp_syn_init(&rp_ctx->syn.screen, 0, 0);
}

static void rp_network_queue_init() {
	rp_syn_init(&rp_ctx->syn.network, 1, 1);
}

static s32 rp_screen_transfer_acquire(s64 timeout) {
	return rp_syn_acq(&rp_ctx->syn.screen.transfer, timeout);
}

static void rp_screen_encode_release(u8 pos) {
	rp_syn_rel(&rp_ctx->syn.screen.encode, pos);
}

static s32 rp_screen_encode_acquire(s64 timeout) {
	return rp_syn_acq1(&rp_ctx->syn.screen.encode, timeout);
}

static void rp_screen_transfer_release(u8 pos) {
	rp_syn_rel1(&rp_ctx->syn.screen.transfer, pos);
}

static s32 rp_network_transfer_acquire(s64 timeout) {
	return rp_syn_acq(&rp_ctx->syn.network.transfer, timeout);
}

static void rp_network_encode_release(u8 pos) {
	rp_syn_rel(&rp_ctx->syn.network.encode, pos);
}

static s32 rp_network_encode_acquire(s64 timeout) {
	if (rp_ctx->conf.multicore_encode) {
		return rp_syn_acq1(&rp_ctx->syn.network.encode, timeout);
	} else {
		return rp_syn_acq(&rp_ctx->syn.network.encode, timeout);
	}
}

static void rp_network_transfer_release(u8 pos) {
	if (rp_ctx->conf.multicore_encode)	{
		rp_syn_rel1(&rp_ctx->syn.network.transfer, pos);
	} else {
		rp_syn_rel(&rp_ctx->syn.network.transfer, pos);
	}
}

static uint16_t ip_checksum(void* vdata, size_t length) {
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

static int rpInitUDPPacket(int dataLen) {
	dataLen += 8;

	u8 *rpSendBuffer = rp_ctx->nwm_send_buffer;
	*(u16*)(rpSendBuffer + 0x22 + 8) = htons(RP_PORT); // src port
	*(u16*)(rpSendBuffer + 0x24 + 8) = htons(RP_DEST_PORT); // dest port
	*(u16*)(rpSendBuffer + 0x26 + 8) = htons(dataLen);
	*(u16*)(rpSendBuffer + 0x28 + 8) = 0; // no checksum
	dataLen += 20;

	*(u16*)(rpSendBuffer + 0x10 + 8) = htons(dataLen);
	*(u16*)(rpSendBuffer + 0x12 + 8) = 0xaf01; // packet id is a random value since we won't use the fragment
	*(u16*)(rpSendBuffer + 0x14 + 8) = 0x0040; // no fragment
	*(u16*)(rpSendBuffer + 0x16 + 8) = 0x1140; // ttl 64, udp
	*(u16*)(rpSendBuffer + 0x18 + 8) = 0;
	*(u16*)(rpSendBuffer + 0x18 + 8) = ip_checksum(rpSendBuffer + 0xE + 8, 0x14);

	dataLen += 22;
	*(u16*)(rpSendBuffer + 12) = htons(dataLen);

	return dataLen;
}

static IINT64 iclock64(void)
{
	u64 value = svc_getSystemTick();
	return value / SYSTICK_PER_MS;
}

static IUINT32 iclock()
{
	return (IUINT32)(iclock64() & 0xfffffffful);
}

static int rp_udp_output(const char *buf, int len, ikcpcb *kcp UNUSED, void *user UNUSED) {
	// nsDbgPrint("rp_udp_output %d\n", iclock());

	u8 *sendBuf = rp_ctx->nwm_send_buffer;
	u8 *dataBuf = sendBuf + NWM_HEADER_SIZE;

	if (len > KCP_PACKET_SIZE) {
		nsDbgPrint("rp_udp_output len exceeded PACKET_SIZE: %d\n", len);
		return 0;
	}

	memcpy(dataBuf, buf, len);
	int packetLen = rpInitUDPPacket(len);
	nwmSendPacket(sendBuf, packetLen);

	return len;
}

static void rpControlRecvHandle(u8* buf UNUSED, int buf_size UNUSED) {
	// __atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
	// __atomic_store_n(&rp_ctx->kcp_restart, 1, __ATOMIC_RELAXED);
}

void rpControlRecv(void) {
	u8 *rpRecvBuffer = rp_ctx->control_recv_buffer;
	int ret = recv(rp_recv_sock, rpRecvBuffer, RP_CONTROL_RECV_BUFFER_SIZE, 0);
	if (ret == 0) {
		nsDbgPrint("rpControlRecv nothing\n");
		return;
	} else if (ret < 0) {
		int err = SOC_GetErrno();
		nsDbgPrint("rpControlRecv failed: %d, errno = %d\n", ret, err);
		return;
	}

	if (!__atomic_load_n(&rp_ctx->kcp_ready, __ATOMIC_ACQUIRE)) {
		svc_sleepThread(10000000);
		return;
	}

	svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
	if (rp_ctx->kcp) {
		int bufSize = ret;
		if ((ret = ikcp_input(rp_ctx->kcp, (const char *)rpRecvBuffer, bufSize)) < 0) {
			nsDbgPrint("ikcp_input failed: %d\n", ret);
		}

		ikcp_update(rp_ctx->kcp, iclock());
		ret = ikcp_recv(rp_ctx->kcp, (char *)rpRecvBuffer, RP_CONTROL_RECV_BUFFER_SIZE);
		if (ret >= 0) {
			rpControlRecvHandle(rpRecvBuffer, ret);
		}
	}
	svc_releaseMutex(rp_ctx->kcp_mutex);
}

static void rpInitPriorityCtx(void) {
	if (rp_ctx->dyn_prio.mutex) {
		svc_closeHandle(rp_ctx->dyn_prio.mutex);
	}
	memset(&rp_ctx->dyn_prio, 0, sizeof(rp_ctx->dyn_prio));
	svc_createMutex(&rp_ctx->dyn_prio.mutex, 0);

	rp_ctx->dyn_prio.top.initializing =
		rp_ctx->dyn_prio.bot.initializing = RP_DYN_PRIO_FRAME_COUNT;

	rp_ctx->dyn_prio.top.priority = rp_ctx->conf.top_priority;
	rp_ctx->dyn_prio.bot.priority = rp_ctx->conf.bot_priority;
}

static int rpGetPriorityScreen(int *frame_rate) {
	if (rp_ctx->conf.top_priority == 0)
		return 0;
	if (rp_ctx->conf.bot_priority == 0)
		return 1;

	typeof(rp_ctx->dyn_prio) *ctx = &rp_ctx->dyn_prio;
	int top_bot;
	svc_waitSynchronization1(ctx->mutex, U64_MAX);

#define TOP 0, top, bot
#define BOT 1, bot, top

#define SET_WITH_SIZE(s, c0, c1, te, ta) do { \
	top_bot = s; \
	ctx->c1.te -=ctx->c0.te; \
	ctx->c0.te = ctx->c0.ta; \
} while (0)
#define SET_WITH_FRAME_SIZE(s) SET_WITH_SIZE(s, frame_size_est, frame_size_acc)
#define SET_WITH_PRIORITY_SIZE(s) SET_WITH_SIZE(s, priority_size_est, priority_size_acc)

#define SET_WITH_SIZE_0(s, c0, c1, te, ta) do { \
	top_bot = s; \
	ctx->c1.te = 0; \
	ctx->c0.te = ctx->c0.ta; \
} while (0)
#define SET_WITH_FRAME_SIZE_0(s) SET_WITH_SIZE_0(s, frame_size_est, frame_size_acc)

#define SET_SIZE(_, c0, c1, te, ta) do { \
	if (ctx->c0.te <= ctx->c1.te) { \
		ctx->c1.te -=ctx->c0.te; \
	} else { \
		ctx->c1.te = 0; \
	} \
	ctx->c0.te = ctx->c0.ta; \
} while (0)
#define SET_FRAME_SIZE(s) SET_SIZE(s, frame_size_est, frame_size_acc)
#define SET_PRIORITY_SIZE(s) SET_SIZE(s, priority_size_est, priority_size_acc)

	if (rp_ctx->conf.dynamic_priority &&
		ctx->top.frame_rate + ctx->bot.frame_rate >= rp_ctx->conf.target_frame_rate
	) {
		if (ctx->top.frame_size_est <= ctx->bot.frame_size_est) {
			if (ctx->top.priority_size_est <= ctx->bot.priority) {
				SET_WITH_FRAME_SIZE(TOP);
				SET_PRIORITY_SIZE(TOP);
			} else {
				SET_WITH_FRAME_SIZE_0(BOT);
				SET_PRIORITY_SIZE(BOT);
			}
		} else {
			if (ctx->bot.priority_size_est <= ctx->top.priority) {
				SET_WITH_FRAME_SIZE(BOT);
				SET_PRIORITY_SIZE(BOT);
			} else {
				SET_WITH_FRAME_SIZE_0(TOP);
				SET_PRIORITY_SIZE(TOP);
			}
		}
	} else {
		if (ctx->top.priority_size_est <= ctx->bot.priority_size_est) {
			SET_WITH_PRIORITY_SIZE(TOP);
			if (rp_ctx->conf.dynamic_priority)
				SET_FRAME_SIZE(TOP);
		} else {
			SET_WITH_PRIORITY_SIZE(BOT);
			if (rp_ctx->conf.dynamic_priority)
				SET_FRAME_SIZE(BOT);
		}
	}

#undef TOP
#undef BOT

#undef SET_WITH_FRAME_SIZE
#undef SET_WITH_PRIORITY_SIZE
#undef SET_WITH_SIZE

#undef SET_WITH_FRAME_SIZE_0
#undef SET_WITH_SIZE_0

#undef SET_FRAME_SIZE
#undef SET_PRIORITY_SIZE
#undef SET_SIZE

	if (frame_rate)
		*frame_rate = ctx->top.frame_rate + ctx->bot.frame_rate;
	svc_releaseMutex(ctx->mutex);
	return top_bot;
}

static void rpSetPriorityScreen(int top_bot, u32 size) {
	if (rp_ctx->conf.top_priority == 0 || rp_ctx->conf.bot_priority == 0)
		return;

	typeof(rp_ctx->dyn_prio) *ctx = &rp_ctx->dyn_prio;
	typeof(rp_ctx->dyn_prio.top) *sctx;
	if (top_bot == 0) {
		sctx = &rp_ctx->dyn_prio.top;
	} else {
		sctx = &rp_ctx->dyn_prio.bot;
	}

	svc_waitSynchronization1(ctx->mutex, U64_MAX);

#define SET_SIZE(t, ta, tc) do { \
	sctx->ta += sctx->tc; \
	sctx->ta -= sctx->t[sctx->frame_index]; \
	sctx->t[sctx->frame_index] = sctx->tc; \
	sctx->tc = 0; \
} while (0)

	u8 chn_index = ++sctx->chn_index;
	sctx->chn_index %= RP_IMAGE_CHANNEL_COUNT;
	if (rp_ctx->conf.dynamic_priority) {
		sctx->frame_size_chn += av_ceil_log2(size);
		if (chn_index == RP_IMAGE_CHANNEL_COUNT) {
			SET_SIZE(frame_size, frame_size_acc, frame_size_chn);

			u32 tick = svc_getSystemTick();
			u32 tick_delta = tick - sctx->tick[sctx->frame_index];
			sctx->tick[sctx->frame_index] = tick;

			if (sctx->initializing) {
				--sctx->initializing;
				sctx->frame_rate = 0;
			} else {
				sctx->frame_rate = (u64)SYSTICK_PER_SEC * RP_DYN_PRIO_FRAME_COUNT / tick_delta;
			}
		}
	}
	sctx->priority_size_chn += sctx->priority;
	if (chn_index == RP_IMAGE_CHANNEL_COUNT) {
		SET_SIZE(priority_size, priority_size_acc, priority_size_chn);

		++sctx->frame_index;
		sctx->frame_index %= RP_DYN_PRIO_FRAME_COUNT;
	}

#undef SET_SIZE

	svc_releaseMutex(ctx->mutex);
}
#undef RP_DYN_PRIO_FRAME_COUNT

static int rp_set_params() {
	u8 multicore_encode = rp_ctx->conf.multicore_encode;

	rp_ctx->conf.arg0 = g_nsConfig->startupInfo[8];
	rp_ctx->conf.arg1 = g_nsConfig->startupInfo[9];
	rp_ctx->conf.arg2 = g_nsConfig->startupInfo[10];

	rp_ctx->conf.updated = 0;

	rp_ctx->conf.kcp_conv = rp_ctx->conf.arg0;

	rp_ctx->conf.yuv_option = (rp_ctx->conf.arg1 & 0x3);
	rp_ctx->conf.color_transform_hp = (rp_ctx->conf.arg1 & 0xc) >> 2;
	rp_ctx->conf.downscale_uv = (rp_ctx->conf.arg1 & 0x10) >> 4;
	rp_ctx->conf.encoder_which = (rp_ctx->conf.arg1 & 0x20) >> 5;

	rp_ctx->conf.me_method = (rp_ctx->conf.arg1 & 0x1c0) >> 6;
	rp_ctx->conf.me_block_size = ((rp_ctx->conf.arg1 & 0x200) >> 9) == 0 ? RP_ME_MIN_BLOCK_SIZE : (RP_ME_MIN_BLOCK_SIZE << 1);
	rp_ctx->conf.me_block_size_log2 = av_ceil_log2(rp_ctx->conf.me_block_size);
	rp_ctx->conf.me_search_param = ((rp_ctx->conf.arg1 & 0x7c00) >> 10) + RP_ME_MIN_SEARCH_PARAM;
	rp_ctx->conf.me_bpp = RP_MAX(3, RP_MIN(6, av_ceil_log2(rp_ctx->conf.me_search_param * 2 + 1)));
	rp_ctx->conf.me_bpp_half_range = (1 << rp_ctx->conf.me_bpp) >> 1;
	rp_ctx->conf.me_downscale = ((rp_ctx->conf.arg1 & 0x8000) >> 15);
#if RP_ME_INTERPOLATE
	rp_ctx->conf.me_interpolate = ((rp_ctx->conf.arg1 & 0x10000) >> 16);
#else
	rp_ctx->conf.me_interpolate = 0;
#endif

	rp_ctx->conf.top_priority = (rp_ctx->conf.arg2 & 0xf);
	rp_ctx->conf.bot_priority = (rp_ctx->conf.arg2 & 0xf0) >> 4;
	rp_ctx->conf.low_latency = (rp_ctx->conf.arg2 & 0x2000) >> 13;
	if (RP_ENCODE_MULTITHREAD)
		rp_ctx->conf.multicore_encode = (rp_ctx->conf.arg2 & 0x4000) >> 14;
	rp_ctx->conf.dynamic_priority = (rp_ctx->conf.arg2 & 0x8000) >> 15;
	rp_ctx->conf.target_frame_rate = (rp_ctx->conf.arg2 & 0xff0000) >> 16;
	rp_ctx->conf.target_mbit_rate = (rp_ctx->conf.arg2 & 0x1f00) >> 8;

	rp_ctx->conf.encode_buffer_count = RP_ENCODE_BUFFER_COUNT - rp_ctx->conf.low_latency -
		(RP_ENCODE_MULTITHREAD && !rp_ctx->conf.multicore_encode);

	rp_ctx->conf.min_send_interval_ticks =
		(u64)SYSTICK_PER_SEC * NWM_PACKET_SIZE * 8 /
		((u16)rp_ctx->conf.target_mbit_rate + 1) / 1024 / 1024;

	rp_ctx->conf.max_capture_interval_ticks =
		(u64)SYSTICK_PER_SEC / ((u16)rp_ctx->conf.target_frame_rate + 1);

	int ret = 0;
	if (rp_ctx->conf.multicore_encode != multicore_encode)
		ret = 1;

	return ret;
}

static void rp_init_syn_params(void) {
}

static void rp_acquire_params(int thread_n) {
}

static void rp_release_params(int thread_n) {
}

static void rp_acquire_params1(int thread_n) {
}

static int rp_check_params(int thread_n) {
	if (__atomic_load_n(&g_nsConfig->remotePlayUpdate, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&g_nsConfig->remotePlayUpdate, 0, __ATOMIC_RELEASE);

		if (!__atomic_test_and_set(&rp_ctx->conf.updated, __ATOMIC_RELAXED)) {
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		}
	}

	return 0;
}
#undef RP_QUICK_RESTART

enum {
	RP_SEND_HEADER_TOP_BOT = (1 << 0),
	RP_SEND_HEADER_P_FRAME = (1 << 1),
};

struct rp_send_header {
	u32 size;
	u32 size_1;
	u8 frame_n;
	u8 bpp;
	u8 format;
	u8 flags;
};

static void rpNetworkTransfer(int thread_n) {
	int ret;

	// kcp init
	svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
	rp_ctx->kcp = ikcp_create(rp_ctx->conf.kcp_conv, 0);
	if (!rp_ctx->kcp) {
		nsDbgPrint("ikcp_create failed\n");

		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		svc_releaseMutex(rp_ctx->kcp_mutex);
		return;
	} else {
		rp_ctx->kcp->output = rp_udp_output;
		if ((ret = ikcp_setmtu(rp_ctx->kcp, KCP_PACKET_SIZE)) < 0) {
			nsDbgPrint("ikcp_setmtu failed: %d\n", ret);
		}
		ikcp_nodelay(rp_ctx->kcp, 2, 10, 2, 1);
		// rp_ctx->kcp->rx_minrto = 10;
		ikcp_wndsize(rp_ctx->kcp, KCP_SND_WND_SIZE, 0);
	}
	// svc_releaseMutex(rp_ctx->kcp_mutex);

	// svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
	// send empty header to mark beginning
	{
		struct rp_send_header empty_header = { 0 };

		ret = ikcp_send(rp_ctx->kcp, (const char *)&empty_header, sizeof(empty_header));

		if (ret < 0) {
			nsDbgPrint("ikcp_send failed: %d\n", ret);
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		}
	}

	ikcp_update(rp_ctx->kcp, iclock());
	svc_releaseMutex(rp_ctx->kcp_mutex);

	u64 last_tick = svc_getSystemTick(), curr_tick, desired_last_tick = last_tick;

	while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED) &&
		!__atomic_load_n(&rp_ctx->kcp_restart, __ATOMIC_RELAXED)
	) {
		ret = rp_check_params(thread_n);
		if (ret) {
			__atomic_store_n(&rp_ctx->kcp_restart, 1, __ATOMIC_RELAXED);
			break;
		}

		if ((curr_tick = svc_getSystemTick()) - last_tick > KCP_TIMEOUT_TICKS) {
			nsDbgPrint("kcp timeout transfer acquire\n");
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
			break;
		}

		svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
		ikcp_update(rp_ctx->kcp, iclock());
		svc_releaseMutex(rp_ctx->kcp_mutex);

		s32 pos = rp_network_transfer_acquire(10000000);
		if (pos < 0) {
			continue;
		}

		last_tick = curr_tick;

		int top_bot = rp_ctx->jls_encode[pos].top_bot;
		struct rp_send_header header = {
			.size = rp_ctx->jls_encode[pos].size,
			.size_1 = rp_ctx->jls_encode[pos].size_1,
			.frame_n = rp_ctx->jls_encode[pos].frame_n,
			.bpp = rp_ctx->jls_encode[pos].bpp,
			.format = rp_ctx->jls_encode[pos].format,
			.flags = (top_bot ? RP_SEND_HEADER_TOP_BOT : 0) |
				(rp_ctx->jls_encode[pos].p_frame ? RP_SEND_HEADER_P_FRAME : 0),
		};
		// nsDbgPrint("%s %d acquired network transfer: %d\n",
		// 	RP_TOP_BOT_STR(header.top_bot), header.frame_n, pos);
		u32 size_remain = header.size + header.size_1;
		u8 *data = rp_ctx->jls_encode_buffer[pos];
		rpSetPriorityScreen(top_bot, size_remain);

		while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED) &&
			!__atomic_load_n(&rp_ctx->kcp_restart, __ATOMIC_RELAXED)
		) {
			if ((curr_tick = svc_getSystemTick()) - last_tick > KCP_TIMEOUT_TICKS) {
				nsDbgPrint("kcp timeout send header\n");
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				break;
			}
			u32 data_size = RP_MIN(size_remain, RP_PACKET_SIZE - sizeof(header));

			// kcp send header data
			svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
			int waitsnd = ikcp_waitsnd(rp_ctx->kcp);
			if (waitsnd < KCP_SND_WND_SIZE) {
				u8 *kcp_send_buffer = rp_ctx->kcp_send_buffer;
				memcpy(kcp_send_buffer, &header, sizeof(header));
				memcpy(kcp_send_buffer + sizeof(header), data, data_size);

				ret = ikcp_send(rp_ctx->kcp, (const char *)kcp_send_buffer, data_size + sizeof(header));

				if (ret < 0) {
					nsDbgPrint("ikcp_send failed: %d\n", ret);

					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					svc_releaseMutex(rp_ctx->kcp_mutex);
					break;
				}

				size_remain -= data_size;
				data += data_size;

				ikcp_update(rp_ctx->kcp, iclock());
				svc_releaseMutex(rp_ctx->kcp_mutex);

				desired_last_tick += rp_ctx->conf.min_send_interval_ticks;
				last_tick = curr_tick;
				break;
			}
			ikcp_update(rp_ctx->kcp, iclock());
			svc_releaseMutex(rp_ctx->kcp_mutex);

			svc_sleepThread(1000000);
		}

		u64 tick_diff;
		s64 desired_tick_diff;

		while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED) &&
			!__atomic_load_n(&rp_ctx->kcp_restart, __ATOMIC_RELAXED) &&
			size_remain
		) {
			if ((tick_diff = (curr_tick = svc_getSystemTick()) - last_tick) > KCP_TIMEOUT_TICKS) {
				nsDbgPrint("kcp timeout send data\n");
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				break;
			}
			desired_tick_diff = (s64)(curr_tick & ((1ULL << 48) - 1)) - (desired_last_tick & ((1ULL << 48) - 1));
			if (desired_tick_diff < rp_ctx->conf.min_send_interval_ticks) {
				u64 duration = (rp_ctx->conf.min_send_interval_ticks - desired_tick_diff) * 1000 / SYSTICK_PER_US;
				// nsDbgPrint("desired sleep %dus\n", (u32)(duration / 1000));
				svc_sleepThread(duration);
			} else {
				u64 min_tick = rp_ctx->conf.min_send_interval_ticks * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM;
				if (tick_diff < min_tick) {
					u64 duration = (min_tick - tick_diff) * 1000 / SYSTICK_PER_US;
					// nsDbgPrint("sleep %dus\n", (u32)(duration / 1000));
					svc_sleepThread(duration);
				}
			}

			u32 data_size = RP_MIN(size_remain, RP_PACKET_SIZE);

			// kcp send data
			svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
			int waitsnd = ikcp_waitsnd(rp_ctx->kcp);
			if (waitsnd < KCP_SND_WND_SIZE) {
				// nsDbgPrint("ikcp_send %d\n", iclock());
				ret = ikcp_send(rp_ctx->kcp, (const char *)data, data_size);

				if (ret < 0) {
					nsDbgPrint("ikcp_send failed: %d\n", ret);

					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					svc_releaseMutex(rp_ctx->kcp_mutex);
					break;
				}

				size_remain -= data_size;
				data += data_size;

				ikcp_update(rp_ctx->kcp, iclock());
				svc_releaseMutex(rp_ctx->kcp_mutex);

				desired_last_tick += rp_ctx->conf.min_send_interval_ticks;
				last_tick = curr_tick;

				if (last_tick - desired_last_tick < (1ULL << 48) &&
					last_tick - desired_last_tick > rp_ctx->conf.min_send_interval_ticks * KCP_SND_WND_SIZE
				)
					desired_last_tick = last_tick - rp_ctx->conf.min_send_interval_ticks * KCP_SND_WND_SIZE;
				continue;
			}
			ikcp_update(rp_ctx->kcp, iclock());
			svc_releaseMutex(rp_ctx->kcp_mutex);

			svc_sleepThread(1000000);
		}

		rp_network_encode_release(pos);
	}

	// kcp deinit
	svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
	ikcp_release(rp_ctx->kcp);
	rp_ctx->kcp = 0;
	svc_releaseMutex(rp_ctx->kcp_mutex);
}

static void rpNetworkTransferThread(u32 arg UNUSED) {
	svc_sleepThread(500000000);
	int thread_n = -1;
	rp_acquire_params(thread_n);
	while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
		__atomic_store_n(&rp_ctx->kcp_restart, 0, __ATOMIC_RELAXED);
		rpNetworkTransfer(thread_n);
		svc_sleepThread(50000000);
	}
	rp_release_params(thread_n);
	svc_exitThread();
}

static void rpInitDmaHome(void) {
	// u32 rp_ctx->dma_config[20] = { 0 };
	svc_openProcess(&rp_ctx->home_handle, 0xf);
}

static void rpCloseGameHandle(void) {
	if (rp_ctx->game_handle) {
		svc_closeHandle(rp_ctx->game_handle);
		rp_ctx->game_handle = 0;
		rp_ctx->game_fcram_base = 0;
	}
}

static Handle rpGetGameHandle(void) {
	int i;
	Handle hProcess;
	if (rp_ctx->game_handle == 0) {
		for (i = 0x28; i < 0x38; i++) {
			int ret = svc_openProcess(&hProcess, i);
			if (ret == 0) {
				nsDbgPrint("game process: %x\n", i);
				rp_ctx->game_handle = hProcess;
				break;
			}
		}
		if (rp_ctx->game_handle == 0) {
			return 0;
		}
	}
	if (rp_ctx->game_fcram_base == 0) {
		if (svc_flushProcessDataCache(hProcess, 0x14000000, 0x1000) == 0) {
			rp_ctx->game_fcram_base = 0x14000000;
		}
		else if (svc_flushProcessDataCache(hProcess, 0x30000000, 0x1000) == 0) {
			rp_ctx->game_fcram_base = 0x30000000;
		}
		else {
			return 0;
		}
	}
	return rp_ctx->game_handle;
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

static int rpCaptureScreen(int screen_buffer_n, int top_bot) {
	u32 bufSize = rp_ctx->screen[top_bot].pitch * (top_bot == 0 ? 400 : 320);
	if (bufSize > RP_SCREEN_BUFFER_SIZE) {
		nsDbgPrint("rpCaptureScreen bufSize too large: %x > %x\n", bufSize, RP_SCREEN_BUFFER_SIZE);
		return -1;
	}

	u32 phys = rp_ctx->screen[top_bot].fbaddr;
	u8 *dest = rp_ctx->screen_buffer[screen_buffer_n];
	Handle hProcess = rp_ctx->home_handle;

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)dest, bufSize);
	Handle *hdma = &rp_ctx->screen_hdma[screen_buffer_n];
	if (*hdma)
		svc_closeHandle(*hdma);
	*hdma = 0;

	int ret;
	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		ret = svc_startInterProcessDma(hdma, CURRENT_PROCESS_HANDLE,
			dest, hProcess, (const void *)(0x1F000000 + (phys - 0x18000000)), bufSize, (u32 *)rp_ctx->dma_config);
		return ret;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			ret = svc_startInterProcessDma(hdma, CURRENT_PROCESS_HANDLE,
				dest, hProcess, (const void *)(rp_ctx->game_fcram_base + (phys - 0x20000000)), bufSize, (u32 *)rp_ctx->dma_config);
			return ret;
		}
		return 0;
	}
	svc_sleepThread(1000000000);

	return 0;
}

static void jls_encoder_prepare_LUTs(void) {
	prepare_classmap(rp_ctx->jls_enc_luts.classmap);
	struct jls_enc_params *p;

#define RP_JLS_INIT_LUT(bpp, bpp_index, bpp_lut_name) do { \
	p = &rp_ctx->jls_enc_params[bpp_index]; \
	jpeg_ls_init(p, bpp, (const uint16_t (*)[3])rp_ctx->jls_enc_luts.bpp_lut_name); \
	prepare_vLUT(rp_ctx->jls_enc_luts.bpp_lut_name, p->alpha, p->T1, p->T2, p->T3); } while (0) \

	RP_JLS_INIT_LUT(8, RP_ENCODE_PARAMS_BPP8, vLUT_bpp8);
	RP_JLS_INIT_LUT(5, RP_ENCODE_PARAMS_BPP5, vLUT_bpp5);
	RP_JLS_INIT_LUT(6, RP_ENCODE_PARAMS_BPP6, vLUT_bpp6);
	RP_JLS_INIT_LUT(4, RP_ENCODE_PARAMS_BPP4, vLUT_bpp4);

#undef RP_JLS_INIT_LUT
}

extern const uint8_t psl0[];
static u8 *jpeg_ls_encode_pad_source(int thread_n, const u8 *src_unpadded, int width, int height);

static int ffmpeg_jls_decode(int thread_n, uint8_t *dst, int width, int height, int pitch, const uint8_t *src, int src_size, int bpp) {
	JLSState state = { 0 };
	state.bpp = bpp;
	ff_jpegls_reset_coding_parameters(&state, 0);
	ff_jpegls_init_state(&state);

	int ret, t;

	GetBitContext s;
	ret = init_get_bits8(&s, src, src_size);
	if (ret < 0) {
		return ret;
	}

	const uint8_t *last;
	uint8_t *cur;
	last = psl0;
	cur = dst;

	int i;
	t = 0;
	for (i = 0; i < width; ++i) {
		ret = ls_decode_line(&state, &s, last, cur, t, height);
		if (ret < 0)
		{
			nsDbgPrint("Failed decode at col %d\n", i);
			return ret;
		}
		t = last[0];
		last = cur;
		cur += pitch;
	}

	return width * height;
}

static int rpJLSEncodeImage(int thread_n, u8 *dst, int dst_size, const u8 *src, int w, int h, int bpp) {
#if RP_ENCODE_VERIFY
	XXH32_hash_t src_hash = XXH32(src, w * (h + LEFTMARGIN + RIGHTMARGIN), 0);
	u8 *dst2 = rp_ctx->jls_encode_verify_buffer[thread_n];

	if (rp_ctx->conf.encoder_which == 1) {
		u8 *tmp = dst;
		dst = dst2;
		dst2 = tmp;
	}
#else
	u8 *dst2 = dst;
#endif

	struct jls_enc_params *params;
	switch (bpp) {
		case 8:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP8]; break;

		case 5:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP5]; break;

		case 6:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP6]; break;

		case 4:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP4]; break;

		default:
			nsDbgPrint("Unsupported bpp in rpJLSEncodeImage: %d\n", bpp);
			return -1;
	}

	int ret, ret2;
#if !RP_ENCODE_VERIFY
	if (rp_ctx->conf.encoder_which == 0) {
#endif
		JLSState state = { 0 };
		state.bpp = bpp;

		ff_jpegls_reset_coding_parameters(&state, 0);
		ff_jpegls_init_state(&state);

		PutBitContext s;
		init_put_bits(&s, dst, dst_size);

		const u8 *last = psl0 + LEFTMARGIN;
		const u8 *in = src + LEFTMARGIN;

		for (int i = 0; i < w; ++i) {
			ls_encode_line(
				&state, &s, last, in, h,
				params->vLUT,
				rp_ctx->jls_enc_luts.classmap
			);
			last = in;
			in += h + LEFTMARGIN + RIGHTMARGIN;
		}

		// put_bits(&s, 7, 0);
		// int size_in_bits = put_bits_count(&s);
		flush_put_bits(&s);
		ret = put_bytes_output(&s);
#if !RP_ENCODE_VERIFY
	} else {
#endif
		struct jls_enc_ctx *ctx = &rp_ctx->jls_enc_ctx[thread_n];
		struct bito_ctx *bctx = &rp_ctx->jls_bito_ctx[thread_n];
		ret2 = jpeg_ls_encode(
			params, ctx, bctx, (char *)dst2, dst_size, src,
			w, h, h + LEFTMARGIN + RIGHTMARGIN,
			rp_ctx->jls_enc_luts.classmap
		);
#if !RP_ENCODE_VERIFY
		ret = ret2;
	}
#endif

#if RP_ENCODE_VERIFY
	nsDbgPrint("rpJLSEncodeImage: w = %d, h = %d, bpp = %d\n", w, h, bpp);
	if (src_hash != XXH32(src, w * (h + LEFTMARGIN + RIGHTMARGIN), 0))
		nsDbgPrint("rpJLSEncodeImage src buffer corrupt during encode, race condition?\n");
	if (ret != ret2) {
		nsDbgPrint("Failed encode size verify: %d, %d\n", ret, ret2);
	} else if (memcmp(dst, dst2, ret) != 0) {
		nsDbgPrint("Failed encode content verify\n");
	} else {
		u8 *decoded = rp_ctx->jls_decode_verify_buffer[thread_n];

		int ret3 = ffmpeg_jls_decode(thread_n, decoded, w, h, h, dst2, ret2, bpp);
		if (ret3 != w * h) {
			nsDbgPrint("Failed decode size verify: %d (expected %d)\n", ret3, w * h);
		} else {
			for (int i = 0; i < w; ++i) {
				if (memcmp(decoded + i * h, src + LEFTMARGIN + i * (h + LEFTMARGIN + RIGHTMARGIN), h) != 0) {
					nsDbgPrint("Failed decode content verify at col %d\n", i);
					break;
				}
			}
			decoded = jpeg_ls_encode_pad_source(thread_n, decoded, w, h);
			if (memcmp(decoded, src, w * (h + LEFTMARGIN + RIGHTMARGIN)) != 0) {
				nsDbgPrint("Failed decode pad content verify\n");
			}
			for (int i = 0; i < w; ++i) {
				if (memcmp(
						decoded + LEFTMARGIN + i * (h + LEFTMARGIN + RIGHTMARGIN),
						src + LEFTMARGIN + i * (h + LEFTMARGIN + RIGHTMARGIN),
						h
					) != 0
				) {
					nsDbgPrint("Failed decode pad content verify at col %d\n", i);
					break;
				}
			}
		}
	}
#endif

	if (ret >= dst_size) {
		nsDbgPrint("Possible buffer overrun in rpJLSEncodeImage\n");
		return -1;
	}
#if RP_ENCODE_VERIFY
	return rp_ctx->conf.encoder_which == 0 ? ret : ret2;
#else
	return ret;
#endif
}

#define rshift_to_even(n, s) (((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))
#define srshift_to_even(t, n, s) ((t)((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))

static ALWAYS_INLINE
void convert_yuv_hp(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	int bpp
) {
	u8 half_range = 1 << (bpp - 1);
	switch (rp_ctx->conf.color_transform_hp) {
		case 1:
			*y_out = g;
			*u_out = r - g + half_range;
			*v_out = b - g + half_range;
			break;

		case 2:
			*y_out = g;
			*u_out = r - g + half_range;
			*v_out = b - (((u16)r + g) >> 1) - half_range;
			break;

		case 3: {
			u8 quarter_range = 1 << (bpp - 2);
			u8 u = r - g + half_range;
			u8 v = b - g + half_range;

			*y_out = g + (((u16)u + v) >> 2) - quarter_range;
			*u_out = u;
			*v_out = v;
			break;
		}

		default:
			*y_out = g;
			*u_out = r;
			*v_out = b;
			break;
	}
}

static ALWAYS_INLINE
void convert_yuv_hp_2(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	int bpp
) {
	u8 half_range = 1 << (bpp - 1);
	u8 half_g = g >> 1;
	switch (rp_ctx->conf.color_transform_hp) {
		case 1:
			*y_out = g;
			*u_out = r - half_g + half_range;
			*v_out = b - half_g + half_range;
			break;

		case 2:
			*y_out = g;
			*u_out = r - half_g + half_range;
			*v_out = b - (((u16)r + half_g) >> 1) - half_range;
			break;

		case 3: {
			u8 u = r - half_g + half_range;
			u8 v = b - half_g + half_range;

			*y_out = g + ((u + v) >> 1) - half_range;
			*u_out = u;
			*v_out = v;
			break;
		}

		default:
			*y_out = g;
			*u_out = r;
			*v_out = b;
			break;
	}
}

static ALWAYS_INLINE
void convert_yuv(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	u8 bpp, u8 bpp_2
) {
	switch (rp_ctx->conf.yuv_option) {
		case 1:
			if (bpp_2) {
				convert_yuv_hp_2(r, g, b, y_out, u_out, v_out, bpp);
			} else {
				convert_yuv_hp(r, g, b, y_out, u_out, v_out, bpp);
			}
			break;

#define RP_RGB_SHIFT \
	u8 spp = 8 - bpp; \
	u8 spp_2 = 8 - bpp - (bpp_2 ? 1 : 0); \
	if (spp) { \
		r <<= spp; \
		g <<= spp_2; \
		b <<= spp; \
	}

		case 2: {
			RP_RGB_SHIFT
			u16 y = 77 * (u16)r + 150 * (u16)g + 29 * (u16)b;
			s16 u = -43 * (s16)r + -84 * (s16)g + 127 * (s16)b;
			s16 v = 127 * (s16)r + -106 * (s16)g + -21 * (s16)b;
			*y_out = rshift_to_even(y, 8 + spp_2);
			*u_out = (u8)((u8)srshift_to_even(s16, u, 8) + 128) >> spp;
			*v_out = (u8)((u8)srshift_to_even(s16, v, 8) + 128) >> spp;
			break;
		}

		case 3: {
			RP_RGB_SHIFT
			u16 y = 66 * (u16)r + 129 * (u16)g + 25 * (u16)b;
			s16 u = -38 * (s16)r + -74 * (s16)g + 112 * (s16)b;
			s16 v = 112 * (s16)r + -94 * (s16)g + -18 * (s16)b;
			*y_out = (u8)((u8)rshift_to_even(y, 8) + 16) >> spp_2;
			*u_out = (u8)((u8)srshift_to_even(s16, u, 8) + 128) >> spp;
			*v_out = (u8)((u8)srshift_to_even(s16, v, 8) + 128) >> spp;
			break;
		}

#undef RP_RGB_SHIFT

		default:
			*y_out = g;
			*u_out = r;
			*v_out = b;
			break;
	};
}

static ALWAYS_INLINE
void convert_set_zero(u8 *restrict *restrict dp_y_out, int count) {
	for (int i = 0; i < count; ++i) {
		*(*dp_y_out)++ = 0;
	}
}

static ALWAYS_INLINE
void convert_set_3_zero(
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out,
	int count
) {
	convert_set_zero(dp_y_out, count);
	convert_set_zero(dp_u_out, count);
	convert_set_zero(dp_v_out, count);
}

static ALWAYS_INLINE
void convert_set_last(u8 *restrict *restrict dp_y_out, int count) {
	for (int i = 0; i < count; ++i) {
		**dp_y_out = *(*dp_y_out - 1);
		++*dp_y_out;
	}
}

static ALWAYS_INLINE
void convert_set_3_last(
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out,
	int count
) {
	convert_set_last(dp_y_out, count);
	convert_set_last(dp_u_out, count);
	convert_set_last(dp_v_out, count);
}

static ALWAYS_INLINE
void convert_set_prev_first(int prev_off, u8 *restrict *restrict dp_y_out, int count) {
	u8 prev_first = *(*dp_y_out - prev_off);
	for (int i = 0; i < count; ++i) {
		*(*dp_y_out)++ = prev_first;
	}
}

static ALWAYS_INLINE
void convert_set_3_prev_first(
	int prev_off,
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out,
	int count
) {
	convert_set_prev_first(prev_off, dp_y_out, count);
	convert_set_prev_first(prev_off, dp_u_out, count);
	convert_set_prev_first(prev_off, dp_v_out, count);
}

static int convert_yuv_image(
	int format, int width, int height, int bytes_per_pixel, int bytes_to_next_column,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp
) {
	ASSUME_ALIGN_4(sp);
	ASSUME_ALIGN_4(dp_y_out);
	ASSUME_ALIGN_4(dp_u_out);
	ASSUME_ALIGN_4(dp_v_out);

	convert_set_3_zero(&dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
	int prev_off = height + RIGHTMARGIN;
	int x, y;

	switch (format) {
		// untested
		case 0:
			if (0)
				++sp;
			else
				return -1;

			FALLTHRU
		case 1: {
			for (x = 0; x < width; ++x) {
				if (x > 0)
					convert_set_3_prev_first(prev_off, &dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
				for (y = 0; y < height; ++y) {
					convert_yuv(sp[2], sp[1], sp[0], dp_y_out++, dp_u_out++, dp_v_out++,
						8, 0
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out, RIGHTMARGIN);
			}
			*y_bpp = *u_bpp = *v_bpp = 8;
			break;
		}

		case 2: {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(prev_off, &dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f, (pix >> 5) & 0x3f, pix & 0x1f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						5, 1
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out, RIGHTMARGIN);
			}
			*y_bpp = 6;
			*u_bpp = 5;
			*v_bpp = 5;
			break;
		}

		// untested
		case 3:
		if (0) {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(prev_off, &dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f, (pix >> 6) & 0x1f, (pix >> 1) & 0x1f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						5, 0
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out, RIGHTMARGIN);
			}
			*y_bpp = *u_bpp = *v_bpp = 5;
			break;
		} FALLTHRU

		// untested
		case 4:
		if (0) {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(prev_off, &dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 12) & 0x0f, (pix >> 8) & 0x0f, (pix >> 4) & 0x0f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						4, 0
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out, RIGHTMARGIN);
			}
			*y_bpp = *u_bpp = *v_bpp = 4;
			break;
		} FALLTHRU

		default:
			return -1;
	}
	return 0;
}

#if RP_ENCODE_VERIFY
static u8 *jpeg_ls_encode_pad_source(int thread_n, const u8 *src, int width, int height) {
	u8 *dst = rp_ctx->jls_decode_verify_padded_buffer[thread_n];
	u8 *ret = dst;
	convert_set_zero(&dst, LEFTMARGIN);
	for (int y = 0; y < width; ++y) {
		if (y) {
			convert_set_prev_first(height + RIGHTMARGIN, &dst, LEFTMARGIN);
		}
		for (int x = 0; x < height; ++x) {
			*dst++ = *src++;
		}
		convert_set_last(&dst, RIGHTMARGIN);
	}
	if (dst - ret != width * (height + LEFTMARGIN + RIGHTMARGIN)) {
		nsDbgPrint("Failed pad source size: %d (expected %d)\n",
			dst - ret,
			width * (height + LEFTMARGIN + RIGHTMARGIN)
		);
	}
	if (dst - ret > sizeof(rp_ctx->jls_decode_verify_padded_buffer[thread_n])) {
		nsDbgPrint("Failed pad source buffer overflow: %d\n", dst - ret);
	}
	return ret;
}
#endif

static void downscale_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig) {
	ASSUME_ALIGN_4(ds_dst);
	ASSUME_ALIGN_4(src);

	src += LEFTMARGIN;
	int pitch = LEFTMARGIN + hOrig + RIGHTMARGIN;
	const u8 *src_end = src + wOrig * pitch;
	const u8 *src_col0 = src;
	const u8 *src_col1 = src + pitch;
	while (src_col0 < src_end) {
		if (src_col0 == src) {
			convert_set_zero(&ds_dst, LEFTMARGIN);
		} else {
			convert_set_prev_first(hOrig / 2 + RIGHTMARGIN, &ds_dst, LEFTMARGIN);
		}
		const u8 *src_col0_end = src_col0 + hOrig;
		while (src_col0 < src_col0_end) {
			u16 p = *src_col0++;
			p += *src_col0++;
			p += *src_col1++;
			p += *src_col1++;

			*ds_dst++ = rshift_to_even(p, 2);
		}
		src_col0 += RIGHTMARGIN + pitch + LEFTMARGIN;
		src_col1 += RIGHTMARGIN + pitch + LEFTMARGIN;
		convert_set_last(&ds_dst, RIGHTMARGIN);
	}
}

static void motion_estimate(s8 *me_x_image, s8 *me_y_image, const u8 *ref, const u8 *cur, int width, int height, int pitch) {
	// ffmpeg is row-major, 3DS is column-major; so switch x and y
	{
		s8 *temp = me_x_image;
		me_x_image = me_y_image;
		me_y_image = temp;
	}
	{
		int temp = width;
		width = height;
		height = temp;
	}

	u8 half_range = rp_ctx->conf.me_bpp_half_range;

	AVMotionEstContext me_ctx;
	AVMotionEstPredictor *preds = me_ctx.preds;

	u8 block_size = rp_ctx->conf.me_block_size;
	u8 block_size_log2 = rp_ctx->conf.me_block_size_log2;
	u8 block_size_mask = (1 << block_size_log2) - 1;

	u8 block_x_n = width >> block_size_log2;
	u8 block_y_n = height >> block_size_log2;
	u8 x_off = (width & block_size_mask) >> 1;
	u8 y_off = (height & block_size_mask) >> 1;

	ff_me_init_context(&me_ctx, block_size, rp_ctx->conf.me_search_param,
		width, height,
		0, width - block_size,
		0, height - block_size
	);
	me_ctx.linesize = pitch;
	me_ctx.data_ref = ref;
	me_ctx.data_cur = cur;

	convert_set_zero((u8 **)&me_x_image, LEFTMARGIN);
	convert_set_zero((u8 **)&me_y_image, LEFTMARGIN);

	for (int block_y = 0, y = y_off; block_y < block_y_n; ++block_y, y += block_size) {
		if (block_y > 0) {
			convert_set_prev_first(block_x_n + RIGHTMARGIN, (u8 **)&me_x_image, LEFTMARGIN);
			convert_set_prev_first(block_x_n + RIGHTMARGIN, (u8 **)&me_y_image, LEFTMARGIN);
		}

		for (int block_x = 0, x = x_off; block_x < block_x_n; ++block_x, x += block_size) {
			int mv[2] = {x, y};

			switch (rp_ctx->conf.me_method) {
				default:
					ff_me_search_esa(&me_ctx, x, y, mv);
					break;

				case 0:
					ff_me_search_tss(&me_ctx, x, y, mv);
					break;

				case 1:
					ff_me_search_tdls(&me_ctx, x, y, mv);
					break;

				case 2:
					ff_me_search_ntss(&me_ctx, x, y, mv);
					break;

				case 3:
					ff_me_search_fss(&me_ctx, x, y, mv);
					break;

				case 4:
					ff_me_search_ds(&me_ctx, x, y, mv);
					break;

				case 5:
					ff_me_search_hexbs(&me_ctx, x, y, mv);
					break;
			}

			*me_x_image++ = mv[0] - x + half_range;
			*me_y_image++ = mv[1] - y + half_range;
		}

		convert_set_last((u8 **)&me_x_image, RIGHTMARGIN);
		convert_set_last((u8 **)&me_y_image, RIGHTMARGIN);
	}
}

enum {
	CORNER_TOP_LEFT,
	CORNER_BOT_LEFT,
	CORNER_BOT_RIGHT,
	CORNER_TOP_RIGHT,
	CORNER_COUNT,
};

static void interpolate_me(const s8 *me_x_vec[CORNER_COUNT], const s8 *me_y_vec[CORNER_COUNT], int half_range, int scale_log2, int block_size, int block_size_log2, int i, int j, s8 *x, s8 *y) {
	int step_count = block_size;
	int step_total = block_size * 2;
	int step_base = 1;
	int step = 2;

	int x_left = i * step + step_base;
	int x_right = step_total - x_left;

	int y_top = j * step + step_base;
	int y_bot = step_total - y_top;

	int rshift_scale = (block_size_log2 + 1) * 2 + 2;

	int x_unscaled =
		((int)*me_x_vec[CORNER_TOP_LEFT] - half_range) * x_left * y_top +
		((int)*me_x_vec[CORNER_BOT_LEFT] - half_range) * x_left * y_bot +
		((int)*me_x_vec[CORNER_BOT_RIGHT] - half_range) * x_right * y_bot +
		((int)*me_x_vec[CORNER_TOP_RIGHT] - half_range) * x_right * y_top;
	*x = srshift_to_even(int, x_unscaled, rshift_scale) << scale_log2;

	int y_unscaled =
		((int)*me_y_vec[CORNER_TOP_LEFT] - half_range) * x_left * y_top +
		((int)*me_y_vec[CORNER_BOT_LEFT] - half_range) * x_left * y_bot +
		((int)*me_y_vec[CORNER_BOT_RIGHT] - half_range) * x_right * y_bot +
		((int)*me_y_vec[CORNER_TOP_RIGHT] - half_range) * x_right * y_top;
	*y = srshift_to_even(int, y_unscaled, rshift_scale) << scale_log2;
}

static void predict_image(u8 *dst, const u8 *ref, const u8 *cur, const s8 *me_x_image, const s8 *me_y_image, int width, int height, int scale_log2, int bpp) {
	u8 half_range = rp_ctx->conf.me_bpp_half_range;

	u8 block_size = rp_ctx->conf.me_block_size << scale_log2;
	u8 block_size_log2 = rp_ctx->conf.me_block_size_log2 + scale_log2;
	u8 block_size_mask = (1 << block_size_log2) - 1;

	u8 block_x_n = width >> block_size_log2;
	u8 block_y_n = height >> block_size_log2;
	u8 block_pitch = block_y_n + RIGHTMARGIN + LEFTMARGIN;
	u8 x_off = (width & block_size_mask) >> 1;
	u8 y_off = (height & block_size_mask) >> 1;

	convert_set_zero(&dst, LEFTMARGIN);
	ref += LEFTMARGIN;
	cur += LEFTMARGIN;

	const s8 *me_x_col = me_x_image + LEFTMARGIN;
	const s8 *me_y_col = me_y_image + LEFTMARGIN;

	const s8 *me_x_col_vec[CORNER_COUNT];
	const s8 *me_y_col_vec[CORNER_COUNT];
	for (int i = 0; i < CORNER_COUNT; ++i) {
		me_x_col_vec[i] = me_x_col;
		me_y_col_vec[i] = me_y_col;
	}

	for (int i = 0; i < width; ++i) {
		if (i > 0) {
			convert_set_prev_first(height + RIGHTMARGIN, &dst, LEFTMARGIN);
			ref += LEFTMARGIN;
			cur += LEFTMARGIN;
		}

#define DO_PREDICTION() do { \
	const u8 *ref_est = ref++ + (s16)x * (height + LEFTMARGIN + RIGHTMARGIN) + y; \
	*dst++ = (u8)((*cur++ << (8 - bpp)) - (*ref_est << (8 - bpp)) + 128) >> (8 - bpp); \
} while (0)

		if (rp_ctx->conf.me_interpolate) {
			x_off += block_size >> 1;
			y_off += block_size >> 1;

			int i_off = (i - x_off) & block_size_mask;
			if (i_off == 0) {
				if (i < width - x_off - 1) {
					me_x_col_vec[CORNER_BOT_LEFT] += block_pitch;
					me_x_col_vec[CORNER_BOT_RIGHT] += block_pitch;

					me_y_col_vec[CORNER_BOT_LEFT] += block_pitch;
					me_y_col_vec[CORNER_BOT_RIGHT] += block_pitch;
				}
				if (i > x_off) {
					me_x_col_vec[CORNER_TOP_LEFT] += block_pitch;
					me_x_col_vec[CORNER_TOP_RIGHT] += block_pitch;

					me_y_col_vec[CORNER_TOP_LEFT] += block_pitch;
					me_y_col_vec[CORNER_TOP_RIGHT] += block_pitch;
				}
			}

			const s8 *me_x_vec[CORNER_COUNT];
			const s8 *me_y_vec[CORNER_COUNT];
			memcpy(me_x_vec, me_x_col_vec, sizeof(me_x_vec));
			memcpy(me_y_vec, me_y_col_vec, sizeof(me_y_vec));
			for (int j = 0; j < height; ++j) {
				int j_off = (j - y_off) & block_size_mask;
				if (j_off == 0) {
					if (j < height - y_off - 1) {
						++me_x_vec[CORNER_BOT_LEFT];
						++me_x_vec[CORNER_BOT_RIGHT];

						++me_y_vec[CORNER_BOT_LEFT];
						++me_y_vec[CORNER_BOT_RIGHT];
					}
					if (j > y_off) {
						++me_x_vec[CORNER_TOP_LEFT];
						++me_x_vec[CORNER_TOP_RIGHT];

						++me_y_vec[CORNER_TOP_LEFT];
						++me_y_vec[CORNER_TOP_RIGHT];
					}
				}
				s8 x, y;
				interpolate_me(me_x_vec, me_y_vec, half_range, scale_log2, block_size, block_size_log2, i_off, j_off, &x, &y);

				// do prediction
				DO_PREDICTION();
			}
		} else {
			int i_off = (i - x_off) & block_size_mask;
			if (i > x_off && i_off == 0 && i < width - x_off - 1) {
				me_x_col += block_pitch;
				me_y_col += block_pitch;
			}

			const s8 *me_x = me_x_col;
			const s8 *me_y = me_y_col;
			s8 x = (*me_x - half_range) << scale_log2;
			s8 y = (*me_y - half_range) << scale_log2;
			for (int j = 0; j < height; ++j) {
				int j_off = (j - y_off) & block_size_mask;
				if (j > y_off && j_off == 0 && j < height - y_off - 1) {
					++me_x;
					++me_y;

					x = (*me_x - half_range) << scale_log2;
					y = (*me_y - half_range) << scale_log2;
				}

				// do prediction
				DO_PREDICTION();
			}
		}

		convert_set_last(&dst, RIGHTMARGIN);
		ref += RIGHTMARGIN;
		cur += RIGHTMARGIN;
	}
}

static int rpEncodeImage(int screen_buffer_n, int image_buffer_n, int thread_n, int top_bot, int *p_frame) {
	int width, height;
	if (top_bot == 0) {
		width = 400;
	} else {
		width = 320;
	}
	height = 240;

	int format = rp_ctx->screen[top_bot].format;
	format &= 0x0f;
	int bytes_per_pixel;
	if (format == 0) {
		bytes_per_pixel = 4;
	} else if (format == 1) {
		bytes_per_pixel = 3;
	} else {
		bytes_per_pixel = 2;
	}
	int bytes_per_column = bytes_per_pixel * height;
	int pitch = rp_ctx->screen[top_bot].pitch;
	int bytes_to_next_column = pitch - bytes_per_column;

	int image_buffer_n_prev = (image_buffer_n + RP_IMAGE_BUFFER_COUNT - 1) % RP_IMAGE_BUFFER_COUNT;

	u8 *y_image;
	u8 *u_image;
	u8 *v_image;
	u8 *ds_u_image;
	u8 *ds_v_image;
	u8 *y_bpp;
	u8 *u_bpp;
	u8 *v_bpp;
	u8 *ds_y_image;
	u8 *ds_ds_y_image;
	u8 *me_bpp;

	u8 *y_image_prev;
	u8 *u_image_prev;
	u8 *v_image_prev;
	u8 *ds_u_image_prev;
	u8 *ds_v_image_prev;
	u8 *y_bpp_prev;
	u8 *u_bpp_prev;
	u8 *v_bpp_prev;
	u8 *ds_y_image_prev;
	u8 *ds_ds_y_image_prev;

	u8 *y_image_me;
	u8 *u_image_me;
	u8 *v_image_me;
	u8 *ds_u_image_me;
	u8 *ds_v_image_me;
	s8 *me_x_image;
	s8 *me_y_image;

#define RP_IMAGE_SET(top_bot_image, top_bot_image_me) do { \
	y_image = rp_ctx->top_bot_image[image_buffer_n].y_image; \
	u_image = rp_ctx->top_bot_image[image_buffer_n].u_image; \
	v_image = rp_ctx->top_bot_image[image_buffer_n].v_image; \
	ds_y_image = rp_ctx->top_bot_image[image_buffer_n].ds_y_image; \
	ds_u_image = rp_ctx->top_bot_image[image_buffer_n].ds_u_image; \
	ds_v_image = rp_ctx->top_bot_image[image_buffer_n].ds_v_image; \
	ds_ds_y_image = rp_ctx->top_bot_image[image_buffer_n].ds_ds_y_image; \
	y_bpp = &rp_ctx->top_bot_image[image_buffer_n].y_bpp; \
	u_bpp = &rp_ctx->top_bot_image[image_buffer_n].u_bpp; \
	v_bpp = &rp_ctx->top_bot_image[image_buffer_n].v_bpp; \
	me_bpp = &rp_ctx->top_bot_image[image_buffer_n].me_bpp; \
 \
	y_image_prev = rp_ctx->top_bot_image[image_buffer_n_prev].y_image; \
	u_image_prev = rp_ctx->top_bot_image[image_buffer_n_prev].u_image; \
	v_image_prev = rp_ctx->top_bot_image[image_buffer_n_prev].v_image; \
	ds_y_image_prev = rp_ctx->top_bot_image[image_buffer_n_prev].ds_y_image; \
	ds_u_image_prev = rp_ctx->top_bot_image[image_buffer_n_prev].ds_u_image; \
	ds_v_image_prev = rp_ctx->top_bot_image[image_buffer_n_prev].ds_v_image; \
	ds_ds_y_image_prev = rp_ctx->top_bot_image[image_buffer_n_prev].ds_ds_y_image; \
	y_bpp_prev = &rp_ctx->top_bot_image[image_buffer_n_prev].y_bpp; \
	u_bpp_prev = &rp_ctx->top_bot_image[image_buffer_n_prev].u_bpp; \
	v_bpp_prev = &rp_ctx->top_bot_image[image_buffer_n_prev].v_bpp; \
 \
	y_image_me = rp_ctx->top_bot_image_me[thread_n].y_image; \
	u_image_me = rp_ctx->top_bot_image_me[thread_n].u_image; \
	v_image_me = rp_ctx->top_bot_image_me[thread_n].v_image; \
	ds_u_image_me = rp_ctx->top_bot_image_me[thread_n].ds_u_image; \
	ds_v_image_me = rp_ctx->top_bot_image_me[thread_n].ds_v_image; \
	me_x_image = rp_ctx->top_bot_image_me[thread_n].me_x_image; \
	me_y_image = rp_ctx->top_bot_image_me[thread_n].me_y_image; \
 \
	rp_ctx->top_bot_image[image_buffer_n].format = format; } while (0)

	if (top_bot == 0) {
		RP_IMAGE_SET(top_image, top_image_me);
	} else {
		RP_IMAGE_SET(bot_image, bot_image_me);
	}

#undef RP_IMAGE_SET

	*me_bpp = rp_ctx->conf.me_bpp;

	int ret = convert_yuv_image(
		format, width, height, bytes_per_pixel, bytes_to_next_column,
		rp_ctx->screen_buffer[screen_buffer_n],
		y_image, u_image, v_image,
		y_bpp, u_bpp, v_bpp
	);

	if (ret < 0)
		return ret;

	if (rp_ctx->conf.downscale_uv) {
		downscale_image(
			ds_u_image,
			u_image,
			width, height
		);

		downscale_image(
			ds_v_image,
			v_image,
			width, height
		);
	}

	int ds_width = width / 2;
	int ds_height = height / 2;

	if (*p_frame &&
		(*y_bpp != *y_bpp_prev || *u_bpp != *u_bpp_prev || *v_bpp != *v_bpp_prev)
	)
		*p_frame = 0;

	if (*p_frame) {
		downscale_image(
			ds_y_image,
			y_image,
			width, height
		);

		if (rp_ctx->conf.me_downscale) {
			downscale_image(
				ds_ds_y_image,
				ds_y_image,
				ds_width, ds_height
			);

			int ds_ds_width = ds_width / 2;
			int ds_ds_height = ds_height / 2;
			motion_estimate(me_x_image, me_y_image,
				ds_ds_y_image_prev + LEFTMARGIN, ds_ds_y_image + LEFTMARGIN,
				ds_ds_width, ds_ds_height, ds_ds_height + LEFTMARGIN + RIGHTMARGIN
			);
		} else {
			motion_estimate(me_x_image, me_y_image,
				ds_y_image_prev + LEFTMARGIN, ds_y_image + LEFTMARGIN,
				ds_width, ds_height, ds_height + LEFTMARGIN + RIGHTMARGIN
			);
		}

		int scale_log2_offset = rp_ctx->conf.me_downscale == 0 ? 0 : 1;
		int scale_log2 = 1 + scale_log2_offset;
		int ds_scale_log2 = 0 + scale_log2_offset;
		predict_image(y_image_me, y_image_prev, y_image, me_x_image, me_y_image,
			width, height, scale_log2, *y_bpp);

		if (rp_ctx->conf.downscale_uv) {
			predict_image(ds_u_image_me, ds_u_image_prev, ds_u_image, me_x_image, me_y_image,
				ds_width, ds_height, ds_scale_log2, *u_bpp);
			predict_image(ds_v_image_me, ds_v_image_prev, ds_v_image, me_x_image, me_y_image,
				ds_width, ds_height, ds_scale_log2, *v_bpp);
		} else {
			predict_image(u_image_me, u_image_prev, u_image, me_x_image, me_y_image,
				width, height, scale_log2, *u_bpp);
			predict_image(v_image_me, v_image_prev, v_image, me_x_image, me_y_image,
				width, height, scale_log2, *v_bpp);
		}
	}

	return 0;
}

void rpKernelCallback(int top_bot);
static void rpEncodeScreenAndSend(int thread_n) {
	svc_sleepThread(250000000);

	int ret;
	rp_acquire_params(thread_n);
	while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
		rp_check_params(thread_n);

		s32 pos;
		int top_bot;

		if (rp_ctx->conf.multicore_encode) {
			pos = rp_screen_encode_acquire(25000000);
			if (pos < 0) {
				continue;
			}

			top_bot = rp_ctx->screen_top_bot[pos];
			svc_waitSynchronization1(rp_ctx->image_mutex[top_bot], U64_MAX);
			// nsDbgPrint("%s acquired screen encode: %d\n", RP_TOP_BOT_STR(top_bot), pos);
		} else {
			pos = thread_n;
			top_bot = rpGetPriorityScreen(NULL);

			rpKernelCallback(top_bot);

			int capture_n = 0;
			do {
				ret = rpCaptureScreen(pos, top_bot);

				if (ret == 0)
					break;

				if (capture_n++ > 25) {
					nsDbgPrint("rpCaptureScreen failed\n");
					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					goto final;
				}

				svc_sleepThread(10000000);
			} while (1);
		}

		int screen_buffer_n = pos;
		u8 *image_buffer_n_prev = &rp_ctx->image_buffer_n[top_bot];
		int image_buffer_n = *image_buffer_n_prev;
		*image_buffer_n_prev = (*image_buffer_n_prev + 1) % RP_IMAGE_BUFFER_COUNT;

		u8 *p_frame_prev = &rp_ctx->image_p_frame[top_bot];
		int p_frame = *p_frame_prev;
		if (!*p_frame_prev)
			*p_frame_prev = 1;

		ret = rpEncodeImage(screen_buffer_n, image_buffer_n, thread_n, top_bot, &p_frame);
		if (ret < 0) {
			nsDbgPrint("rpEncodeImage failed\n");
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
			break;
		}

		int frame_n = rp_ctx->image_frame_n[top_bot]++;

		if (rp_ctx->conf.multicore_encode) {
			svc_releaseMutex(rp_ctx->image_mutex[top_bot]);
			rp_screen_transfer_release(pos);
		}

#define RP_ACCESS_TOP_BOT_IMAGE(s, n) \
	(s == 0 ? \
		rp_ctx->top_image[image_buffer_n].n : \
		rp_ctx->bot_image[image_buffer_n].n) \

#define RP_ACCESS_TOP_BOT_IMAGE_ME(s, n) \
	(s == 0 ? \
		rp_ctx->top_image_me[thread_n].n : \
		rp_ctx->bot_image_me[thread_n].n) \

#define RP_PROCESS_IMAGE_AND_SEND(n, wt, wb, h, b, a) while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) { \
	if (a < 1) \
		pos = rp_network_encode_acquire(25000000); \
	if (pos < 0) { \
		continue; \
	} \
	int encode_buffer_n = pos; \
	int bpp = RP_ACCESS_TOP_BOT_IMAGE(top_bot, b); \
	ret = (p_frame || a < 1) ? rpJLSEncodeImage(thread_n, \
		rp_ctx->jls_encode_buffer[encode_buffer_n] + (a < 1 ? 0 : ret), \
		(a < 1 ? RP_JLS_ENCODE_IMAGE_BUFFER_SIZE : RP_JLS_ENCODE_IMAGE_ME_BUFFER_SIZE), \
		p_frame ? \
			RP_ACCESS_TOP_BOT_IMAGE_ME(top_bot, n) : \
			RP_ACCESS_TOP_BOT_IMAGE(top_bot, n), \
		top_bot == 0 ? wt : wb, \
		h, \
		bpp \
	) : 0; \
	if (ret < 0) { \
		nsDbgPrint("rpJLSEncodeImage failed\n"); \
		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED); \
		break; \
	} \
	if (a < 1) { \
		rp_ctx->jls_encode[pos].top_bot = top_bot; \
		rp_ctx->jls_encode[pos].frame_n = frame_n; \
		rp_ctx->jls_encode[pos].size = ret; \
		rp_ctx->jls_encode[pos].bpp = bpp; \
		rp_ctx->jls_encode[pos].format = RP_ACCESS_TOP_BOT_IMAGE(top_bot, format); \
		rp_ctx->jls_encode[pos].p_frame = p_frame; \
		rp_ctx->jls_encode[pos].size_1 = 0; \
	} else { \
		rp_ctx->jls_encode[pos].size_1 = ret; \
	} \
	if (p_frame ? a != 0 : a < 1) \
		rp_network_transfer_release(pos); \
	// nsDbgPrint("%s %d released network transfer: %d\n", RP_TOP_BOT_STR(top_bot), frame_n, pos);

#define RP_PROCESS_IMAGE_AND_SEND_END break; }

		int scale_log2_offset = rp_ctx->conf.me_downscale == 0 ? 0 : 1;
		int scale_log2 = 1 + scale_log2_offset;
		u8 block_size_log2 = rp_ctx->conf.me_block_size_log2 + scale_log2;

		int top_width = 400;
		int bot_width = 320;
		int height = 240;

		int me_top_width = top_width >> block_size_log2;
		int me_bot_width = bot_width >> block_size_log2;
		int me_height = height >> block_size_log2;

		if (rp_ctx->conf.downscale_uv)
		{
			int ds_top_width = top_width / 2;
			int ds_bot_width = bot_width / 2;
			int ds_height = height / 2;

			// y_image
			RP_PROCESS_IMAGE_AND_SEND(y_image, top_width, bot_width, height, y_bpp, -1)
				// ds_u_image
				RP_PROCESS_IMAGE_AND_SEND(ds_u_image, ds_top_width, ds_bot_width, ds_height, u_bpp, 0)
					// me_x_image
					RP_PROCESS_IMAGE_AND_SEND(me_x_image, me_top_width, me_bot_width, me_height, me_bpp, 1)
						// ds_v_image
						RP_PROCESS_IMAGE_AND_SEND(ds_v_image, ds_top_width, ds_bot_width, ds_height, v_bpp, 0)
							// me_x_image
							RP_PROCESS_IMAGE_AND_SEND(me_y_image, me_top_width, me_bot_width, me_height, me_bpp, 1)

							RP_PROCESS_IMAGE_AND_SEND_END

						RP_PROCESS_IMAGE_AND_SEND_END

					RP_PROCESS_IMAGE_AND_SEND_END

				RP_PROCESS_IMAGE_AND_SEND_END

			RP_PROCESS_IMAGE_AND_SEND_END
		} else {
			// y_image
			RP_PROCESS_IMAGE_AND_SEND(y_image, top_width, bot_width, height, y_bpp, -1)
				// u_image
				RP_PROCESS_IMAGE_AND_SEND(u_image, top_width, bot_width, height, u_bpp, 0)
					// me_x_image
					RP_PROCESS_IMAGE_AND_SEND(me_x_image, me_top_width, me_bot_width, me_height, me_bpp, 1)
						// v_image
						RP_PROCESS_IMAGE_AND_SEND(v_image, top_width, bot_width, height, v_bpp, 0)

							// me_x_image
							RP_PROCESS_IMAGE_AND_SEND(me_y_image, me_top_width, me_bot_width, me_height, me_bpp, 1)

							RP_PROCESS_IMAGE_AND_SEND_END

						RP_PROCESS_IMAGE_AND_SEND_END

					RP_PROCESS_IMAGE_AND_SEND_END

				RP_PROCESS_IMAGE_AND_SEND_END

			RP_PROCESS_IMAGE_AND_SEND_END
		}

#undef RP_PROCESS_IMAGE_AND_SEND
#undef RP_PROCESS_IMAGE_AND_SEND_END
#undef RP_ACCESS_TOP_BOT_IMAGE_ME
#undef RP_ACCESS_TOP_BOT_IMAGE
	}
final:
	rp_release_params(thread_n);
}

static void rpSecondThreadStart(u32 arg UNUSED) {
	rpEncodeScreenAndSend(1);
	svc_exitThread();
}

static void rpScreenTransferThread(u32 arg UNUSED) {
	int ret;
	int thread_n = -2;

	u64 last_tick = svc_getSystemTick(), curr_tick;

	rp_acquire_params(thread_n);
	while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
		rp_check_params(thread_n);

		s32 pos = rp_screen_transfer_acquire(25000000);
		if (pos < 0) {
			continue;
		}

		while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
			u64 tick_diff = (curr_tick = svc_getSystemTick()) - last_tick;

			int frame_rate;
			int top_bot = rpGetPriorityScreen(&frame_rate);
			u64 desired_tick_diff = (u64)SYSTICK_PER_SEC * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM / frame_rate;
			desired_tick_diff = RP_MIN(desired_tick_diff, rp_ctx->conf.max_capture_interval_ticks);
			if (tick_diff < desired_tick_diff) {
				u64 duration = (desired_tick_diff - tick_diff) * 1000 / SYSTICK_PER_US;
				svc_sleepThread(duration);
			}

			rpKernelCallback(top_bot);

			ret = rpCaptureScreen(pos, top_bot);

			if (ret < 0) {
				nsDbgPrint("rpCaptureScreen failed\n");
				svc_sleepThread(1000000000);
				continue;
			}

			rp_ctx->screen_top_bot[pos] = top_bot;
			rp_screen_encode_release(pos);
			// nsDbgPrint("%s released screen encode: %d\n", RP_TOP_BOT_STR(top_bot), pos);

			last_tick = curr_tick;
			break;
		}
	}
	rp_release_params(thread_n);

	svc_exitThread();
}

static int rpSendFrames(void) {
	int ret;

	// __atomic_store_n(&rp_ctx->exit_thread, 0, __ATOMIC_RELAXED);
	if (rp_ctx->conf.multicore_encode) {
		rp_screen_queue_init();
		ret = svc_createThread(
			&rp_ctx->second_thread,
			rpSecondThreadStart,
			0,
			(u32 *)&rp_ctx->second_thread_stack[RP_STACK_SIZE - 40],
			0x10,
			3);
		if (ret != 0) {
			nsDbgPrint("Create rpSecondThreadStart Thread Failed: %08x\n", ret);
			return -1;
		}
		ret = svc_createThread(
			&rp_ctx->screen_thread,
			rpScreenTransferThread,
			0,
			(u32 *)&rp_ctx->screen_transfer_thread_stack[RP_MISC_STACK_SIZE - 40],
			0x8,
			2);
		if (ret != 0) {
			nsDbgPrint("Create rpScreenTransferThread Thread Failed: %08x\n", ret);

			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
			svc_waitSynchronization1(rp_ctx->second_thread, U64_MAX);
			svc_closeHandle(rp_ctx->second_thread);
			return -1;
		}
	}

	rpEncodeScreenAndSend(0);

	if (rp_ctx->conf.multicore_encode) {
		svc_waitSynchronization1(rp_ctx->second_thread, U64_MAX);
		svc_waitSynchronization1(rp_ctx->screen_thread, U64_MAX);
		svc_closeHandle(rp_ctx->second_thread);
		svc_closeHandle(rp_ctx->screen_thread);
	}

	return ret;
}

static void rpThreadStart(u32 arg UNUSED) {
	jls_encoder_prepare_LUTs();
	rp_init_syn_params();
	rpInitDmaHome();
	// kRemotePlayCallback();

	svc_createMutex(&rp_ctx->kcp_mutex, 0);
	__atomic_store_n(&rp_ctx->kcp_ready, 1, __ATOMIC_RELEASE);

	for (int i = 0; i < SCREEN_COUNT; ++i)
		svc_createMutex(&rp_ctx->image_mutex[i], 0);

	int ret = 0;
	while (ret >= 0) {
		rp_set_params();

		__atomic_store_n(&rp_ctx->exit_thread, 0, __ATOMIC_RELAXED);
		rp_network_queue_init();
		rpInitPriorityCtx();
		ret = svc_createThread(
			&rp_ctx->network_thread,
			rpNetworkTransferThread,
			0,
			(u32 *)&rp_ctx->network_transfer_thread_stack[RP_MISC_STACK_SIZE - 40],
			0x8,
			3);
		if (ret != 0) {
			nsDbgPrint("Create rpNetworkTransferThread Failed: %08x\n", ret);
			goto final;
		}

		for (int i = 0; i < SCREEN_COUNT; ++i)
			rp_ctx->image_p_frame[i] = 0;

		ret = rpSendFrames();

		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		svc_waitSynchronization1(rp_ctx->network_thread, U64_MAX);
		svc_closeHandle(rp_ctx->network_thread);

		svc_sleepThread(100000000);
	}

final:
	svc_closeHandle(rp_ctx->kcp_mutex);
	svc_exitThread();
}

static int nwmValParamCallback(u8* buf, int buflen UNUSED) {
	//rtDisableHook(&nwmValParamHook);

	/*
	if (buf[31] != 6) {
	nsDbgPrint("buflen: %d\n", buflen);
	for (i = 0; i < buflen; i++) {
	nsDbgPrint("%02x ", buf[i]);
	}
	}*/

	int ret;
	Handle hThread;
	// int stackSize = RP_STACK_SIZE;

	if (rpInited) {
		return 0;
	}
	if (buf[0x17 + 0x8] == 6) {
		if ((*(u16*)(&buf[0x22 + 0x8])) == 0x401f) {  // src port 8000
			rpInited = 1;
			rtDisableHook(&nwmValParamHook);

			int storage_size = rtAlignToPageSize(sizeof(*rp_ctx));
			rp_ctx = (typeof(rp_ctx))plgRequestMemory(storage_size);
			if (!rp_ctx) {
				nsDbgPrint("Request memory for RemotePlay failed\n");
				return 0;
			}
			memset(rp_ctx, 0, sizeof(*rp_ctx));
			nsDbgPrint("RemotePlay memory: 0x%08x (0x%x bytes)\n", rp_ctx, storage_size);

			memcpy(rp_ctx->nwm_send_buffer, buf, 0x22 + 8);

			umm_init_heap(rp_ctx->umm_heap, RP_UMM_HEAP_SIZE);
			ikcp_allocator(umm_malloc, umm_free);

			rp_ctx->dma_config[2] = 4;

			ret = svc_createThread(&hThread, rpThreadStart, 0, (u32 *)&rp_ctx->thread_stack[RP_STACK_SIZE - 40], 0x10, 2);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay thread failed: %08x\n", ret);
			}
		}
	}

	return 0;
}

void remotePlayMain(void) {
	nwmSendPacket = (sendPacketTypedef)g_nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, g_nsConfig->startupInfo[11], (u32)nwmValParamCallback);
	rtEnableHook(&nwmValParamHook);
}

void rpKernelCallback(int top_bot) {
	// u32 ret;
	// u32 fbP2VOffset = 0xc0000000;
	u32 current_fb;

	if (top_bot == 0) {
		rp_ctx->screen[0].format = REG(IoBasePdc + 0x470);
		rp_ctx->screen[0].pitch = REG(IoBasePdc + 0x490);

		current_fb = REG(IoBasePdc + 0x478);
		current_fb &= 1;

		rp_ctx->screen[0].fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x468) :
			REG(IoBasePdc + 0x46c);
	} else {
		rp_ctx->screen[1].format = REG(IoBasePdc + 0x570);
		rp_ctx->screen[1].pitch = REG(IoBasePdc + 0x590);

		current_fb = REG(IoBasePdc + 0x578);
		current_fb &= 1;

		rp_ctx->screen[1].fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x568) :
			REG(IoBasePdc + 0x56c);
	}
}
