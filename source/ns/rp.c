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
#define RP_ENCODE_MULTITHREAD (1)
// (0) svc (1) syn
#define RP_SYN_METHOD (1)

// extern IUINT32 IKCP_OVERHEAD;
#define IKCP_OVERHEAD (24)

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC (268123480)

#define RP_MAX(a,b) ((a) > (b) ? (a) : (b))
#define RP_MIN(a,b) ((a) > (b) ? (b) : (a))

#define RP_SVC_MS(ms) ((u64)ms * 1000 * 1000)

#define RP_SYN_WAIT_MAX RP_SVC_MS (2000)
#define RP_SYN_WAIT_IDLE RP_SVC_MS (1000)
#define RP_THREAD_LOOP_WAIT_COUNT (25)
#define RP_THREAD_LOOP_SLOW_WAIT RP_SVC_MS(50)
#define RP_THREAD_LOOP_MED_WAIT RP_SVC_MS(25)
#define RP_THREAD_LOOP_FAST_WAIT RP_SVC_MS(10)
#define RP_THREAD_LOOP_IDLE_WAIT RP_SVC_MS(100)
#define RP_THREAD_LOOP_ULTRA_FAST_WAIT RP_SVC_MS(1)
#define RP_THREAD_KCP_LOOP_WAIT RP_SVC_MS(1)
#define RP_THREAD_ENCODE_BEGIN_WAIT RP_SVC_MS(50)
#define RP_THREAD_NETWORK_BEGIN_WAIT RP_SVC_MS(100)

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
#define RP_JLS_ENCODE_IMAGE_ME_BUFFER_SIZE (RP_JLS_ENCODE_IMAGE_BUFFER_SIZE / RP_ME_MIN_BLOCK_SIZE / RP_ME_MIN_BLOCK_SIZE / 2 / 2)
#define RP_JLS_ENCODE_BUFFER_SIZE (RP_JLS_ENCODE_IMAGE_BUFFER_SIZE + RP_JLS_ENCODE_IMAGE_ME_BUFFER_SIZE)
#define RP_TOP_BOT_STR(top_bot) ((top_bot) == 0 ? "top" : "bot")
#define RP_ME_MIN_BLOCK_SIZE (4)
#define RP_ME_MIN_SEARCH_PARAM (8)

#define DIV_CEIL(n, d) (((n) + ((d) - 1)) / (d))
// (* 2) since motion estimation operates on images downscaled at least once
#define ME_SIZE(w, h) DIV_CEIL(w, RP_ME_MIN_BLOCK_SIZE * 2) * (DIV_CEIL(h, RP_ME_MIN_BLOCK_SIZE * 2) + LEFTMARGIN + RIGHTMARGIN)
#define ME_TOP_SIZE ME_SIZE(400, 240)
#define ME_BOT_SIZE ME_SIZE(320, 240)

#define RP_ASSERT(c, ...) do { if (!(c)) { nsDbgPrint(__VA_ARGS__); } } while (0) \

// attribute aligned
#define ALIGN_4 __attribute__ ((aligned (4)))
// assume aligned
#define ASSUME_ALIGN_4(a) (a = __builtin_assume_aligned (a, 4))
#define UNUSED __attribute__((unused))
#define FALLTHRU __attribute__((fallthrough));
#define ALWAYS_INLINE __attribute__((always_inline)) inline

#if (RP_SYN_METHOD == 0)
typedef Handle rp_lock_t;
typedef Handle rp_sem_t;

#define rp_lock_init(n) svc_createMutex(&n, 0)
#define rp_lock_wait(n, to) svc_waitSynchronization1(n, to)
#define rp_lock_rel(n) svc_releaseMutex(n)
#define rp_lock_close(n) do { if (n) svc_closeHandle(n); } while (0)

#define rp_sem_init(n, i, m) svc_createSemaphore(&n, i, m)
#define rp_sem_wait(n, to) svc_waitSynchronization1(n, to)
#define rp_sem_rel(n, c) do { s32 count; svc_releaseSemaphore(&count, n, c); } while (0)
#define rp_sem_close(n) do { if (n) svc_closeHandle(n); } while (0)
#else
typedef LightLock rp_lock_t;
typedef LightSemaphore rp_sem_t;

#define rp_lock_init(n) (LightLock_Init(&n), 0)
#define rp_lock_wait(n, to) LightLock_LockTimeout(&n, to)
#define rp_lock_rel(n) LightLock_Unlock(&n)
#define rp_lock_close(n) ((void)0)

#define rp_sem_init(n, i, m) (LightSemaphore_Init(&n, i, m), 0)
#define rp_sem_wait(n, to) LightSemaphore_AcquireTimeout(&n, 1, to)
#define rp_sem_rel(n, c) LightSemaphore_Release(&n, c)
#define rp_sem_close(n) ((void)0)
#endif

enum {
	SCREEN_TOP,
	SCREEN_BOT,
	SCREEN_COUNT,
};
enum {
	RP_ENCODE_PARAMS_BPP8,
	RP_ENCODE_PARAMS_BPP7,
	RP_ENCODE_PARAMS_BPP6,
	RP_ENCODE_PARAMS_BPP5,
	RP_ENCODE_PARAMS_BPP4,
	RP_ENCODE_PARAMS_COUNT
};
#define RP_ENCODE_THREAD_COUNT (1 + RP_ENCODE_MULTITHREAD)
// (+ 1) for screen/network transfer then (+ 1) again for start/finish at different time
#define RP_ENCODE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 2)
// (+ 1) for motion estimation reference
#define RP_IMAGE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 1)
static u8 kcp_recv_ready;
static struct rp_ctx_t {
	ikcpcb kcp;
	u8 kcp_restart;
	Handle kcp_mutex;
	u8 kcp_ready;

	u8 exit_thread;
	Handle second_thread;
	Handle screen_thread;
	Handle network_thread;

	Handle home_handle, game_handle;
	u32 game_fcram_base;
	u8 dma_config[24];

	u8 nwm_send_buffer[NWM_PACKET_SIZE] ALIGN_4;
	u8 kcp_send_buffer[KCP_PACKET_SIZE] ALIGN_4;
	u8 thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 second_thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 network_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 screen_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 control_recv_buffer[RP_CONTROL_RECV_BUFFER_SIZE] ALIGN_4;
	u8 umm_heap[RP_UMM_HEAP_SIZE] ALIGN_4;

	struct rp_screen_encode_t {
		u32 format;
		u32 pitch;
		u32 fbaddr;
		Handle hdma;
		u8 buffer[RP_SCREEN_BUFFER_SIZE] ALIGN_4;
		struct rp_image_ctx_t {
			u8 top_bot;
			u8 p_frame;
			u8 frame_n;
			struct rp_image_t *image, *image_prev;
		} c;
	} screen_encode[RP_ENCODE_BUFFER_COUNT];
	struct rp_network_encode_t {
		u8 buffer[RP_JLS_ENCODE_BUFFER_SIZE] ALIGN_4;
		u8 top_bot;
		u8 frame_n;
		u8 bpp;
		u8 format;
		u32 size;
		u32 size_1;
		u8 p_frame;
	} network_encode[RP_ENCODE_BUFFER_COUNT];
	struct {
		uint16_t vLUT_bpp8[2 * (1 << 8)][3];
		uint16_t vLUT_bpp7[2 * (1 << 7)][3];
		uint16_t vLUT_bpp6[2 * (1 << 6)][3];
		uint16_t vLUT_bpp5[2 * (1 << 5)][3];
		uint16_t vLUT_bpp4[2 * (1 << 4)][3];
		int16_t classmap[9 * 9 * 9];
	} jls_enc_luts;
	struct jls_enc_params jls_enc_params[RP_ENCODE_PARAMS_COUNT];
	struct rp_jls_ctx_t {
		struct jls_enc_ctx enc;
		struct bito_ctx bito;
#if RP_ENCODE_VERIFY
		struct {
			u8 encode[RP_JLS_ENCODE_IMAGE_BUFFER_SIZE] ALIGN_4;
			u8 decode[400 * 240] ALIGN_4;
			u8 decode_padded[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		} verify_buffer;
#endif
	} jls_ctx[RP_ENCODE_THREAD_COUNT];

	struct rp_image_t {
		struct rp_image_top_t {
			s8 me_x_image[1];
			s8 me_y_image[1];
			u8 y_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 u_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 v_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_y_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_u_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_v_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_ds_y_image[100 * (60 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		} top;
		struct rp_image_bot_t {
			s8 me_x_image[1];
			s8 me_y_image[1];
			u8 y_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 u_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 v_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_y_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_u_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_v_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_ds_y_image[80 * (60 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		} bot;
		struct rp_image_common_t {
			u8 y_bpp;
			u8 u_bpp;
			u8 v_bpp;
			u8 format;
			u8 me_bpp;
			rp_sem_t sem_write;
			// rp_lock_t sem_read;
			rp_sem_t sem_try;
			u8 sem_count;
		} s[SCREEN_COUNT];
	} image[RP_IMAGE_BUFFER_COUNT];

	struct rp_image_me_t {
		struct {
			s8 me_x_image[ME_TOP_SIZE] ALIGN_4;
			s8 me_y_image[ME_TOP_SIZE] ALIGN_4;
			u8 y_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 u_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 v_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_u_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_v_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		} top;
		struct {
			s8 me_x_image[ME_BOT_SIZE] ALIGN_4;
			s8 me_y_image[ME_BOT_SIZE] ALIGN_4;
			u8 y_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 u_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 v_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_u_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
			u8 ds_v_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		} bot;
		struct rp_image_hash_t {
			XXH32_hash_t y_image, u_image, v_image, ds_u_image, ds_v_image;
			XXH32_hash_t y_image_prev, u_image_prev, v_image_prev, ds_u_image_prev, ds_v_image_prev;
		} hash;
	} image_me[RP_ENCODE_THREAD_COUNT];

	struct rp_screen_image_t {
		u8 image_n;
		u8 frame_n;
		u8 p_frame;
	} screen_image[SCREEN_COUNT];

	struct rp_syn_t {
		struct rp_syn_comp_t {
			struct rp_syn_comp_func_t {
				u8 id;
				rp_sem_t sem;
				rp_lock_t mutex;
				u8 pos_head, pos_tail;
				s8 pos[RP_ENCODE_BUFFER_COUNT];
			} transfer, encode;
		} screen, network;
	} syn;

	struct rp_conf_t {
		u8 updated;

		u32 kcp_conv;

		u8 yuv_option;
		u8 color_transform_hp;
		u8 encoder_which;
		u8 downscale_uv;
		u8 encode_verify;

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
		u8 screen_priority[SCREEN_COUNT];
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
	struct rp_dyo_prio_t {
		struct rp_dyo_prio_screen_t {
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
		} s[SCREEN_COUNT];
		rp_lock_t mutex;
	} dyn_prio;
} *rp_ctx;

static int LightLock_LockTimeout(LightLock* lock, s64 timeout)
{
	s32 val;
	bool bAlreadyLocked;

	// Try to lock, or if that's not possible, increment the number of waiting threads
	do
	{
		// Read the current lock state
		val = __ldrex(lock);
		if (val == 0) val = 1; // 0 is an invalid state - treat it as 1 (unlocked)
		bAlreadyLocked = val < 0;

		// Calculate the desired next state of the lock
		if (!bAlreadyLocked)
			val = -val; // transition into locked state
		else
			--val; // increment the number of waiting threads (which has the sign reversed during locked state)
	} while (__strex(lock, val));

	// While the lock is held by a different thread:
	while (bAlreadyLocked)
	{
		// Wait for the lock holder thread to wake us up
		Result rc;
		rc = syncArbitrateAddressWithTimeout(lock, ARBITRATION_WAIT_IF_LESS_THAN_TIMEOUT, 0, timeout);
		// if (R_DESCRIPTION(rc) == RD_TIMEOUT)
		if (rc)
		{
			do
			{
				val = __ldrex(lock);
				bAlreadyLocked = val < 0;

				if (!bAlreadyLocked)
					--val;
				else
					++val;
			} while (__strex(lock, val));

			__dmb();
			return rc;
		}

		// Try to lock again
		do
		{
			// Read the current lock state
			val = __ldrex(lock);
			bAlreadyLocked = val < 0;

			// Calculate the desired next state of the lock
			if (!bAlreadyLocked)
				val = -(val-1); // decrement the number of waiting threads *and* transition into locked state
			else
			{
				// Since the lock is still held, we need to cancel the atomic update and wait again
				__clrex();
				break;
			}
		} while (__strex(lock, val));
	}

	__dmb();
	return 0;
}

static int LightSemaphore_AcquireTimeout(LightSemaphore* semaphore, s32 count, s64 timeout)
{
	s32 old_count;
	s16 num_threads_acq;

	do
	{
		for (;;)
		{
			old_count = __ldrex(&semaphore->current_count);
			if (old_count >= count)
				break;
			__clrex();

			do
				num_threads_acq = (s16)__ldrexh((u16 *)&semaphore->num_threads_acq);
			while (__strexh((u16 *)&semaphore->num_threads_acq, num_threads_acq + 1));

			Result rc;
			rc = syncArbitrateAddressWithTimeout(&semaphore->current_count, ARBITRATION_WAIT_IF_LESS_THAN_TIMEOUT, count, timeout);

			do
				num_threads_acq = (s16)__ldrexh((u16 *)&semaphore->num_threads_acq);
			while (__strexh((u16 *)&semaphore->num_threads_acq, num_threads_acq - 1));

			// if (R_DESCRIPTION(rc) == RD_TIMEOUT)
			if (rc)
			{
				__dmb();
				return rc;
			}
		}
	} while (__strex(&semaphore->current_count, old_count - count));

	__dmb();
	return 0;
}

static u8 rp_atomic_fetch_addb_wrap(u8 *p, u8 a, u8 factor) {
	u8 v, v_new;
	do {
		v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
		v_new = (v + a) % factor;
	} while (!__atomic_compare_exchange_n(p, &v, v_new, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
	return v;
}

static void rp_syn_init1(struct rp_syn_comp_func_t *syn1, int init, int id) {
	rp_sem_close(syn1->sem);
	rp_sem_init(syn1->sem, init ? rp_ctx->conf.encode_buffer_count : 0, rp_ctx->conf.encode_buffer_count);
	rp_lock_close(syn1->mutex);
	rp_lock_init(syn1->mutex);

	syn1->pos_head = syn1->pos_tail = 0;
	syn1->id = id;

	for (int i = 0; i < RP_ENCODE_BUFFER_COUNT; ++i) {
		syn1->pos[i] = init ? i : -1;
	}
}

static void rp_syn_init(struct rp_syn_comp_t *syn, int transfer_encode, int id) {
	rp_syn_init1(&syn->transfer, transfer_encode == 0, id);
	rp_syn_init1(&syn->encode, transfer_encode == 1, id);
}

static s32 rp_syn_acq(struct rp_syn_comp_func_t *syn1, s64 timeout) {
	Result res;
	if ((res = rp_sem_wait(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return -1;
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

static void rp_syn_rel(struct rp_syn_comp_func_t *syn1, s32 pos) {
	u8 pos_head = syn1->pos_head;
	syn1->pos_head = (pos_head + 1) % rp_ctx->conf.encode_buffer_count;
	syn1->pos[pos_head] = pos;
	// nsDbgPrint("rp_syn_rel id %d at %d: %d\n", syn1->id, pos_head, pos);
	// s32 count;
	rp_sem_rel(syn1->sem, 1);
}

static s32 rp_syn_acq1(struct rp_syn_comp_func_t *syn1, s64 timeout) {
	Result res;
	if ((res = rp_sem_wait(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return -1;
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

static int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, s32 pos) {
	int res;
	if ((res = rp_lock_wait(syn1->mutex, RP_SYN_WAIT_MAX))) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_rel1 wait mutex error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return -1;
	}

	u8 pos_head = syn1->pos_head;
	syn1->pos_head = (pos_head + 1) % rp_ctx->conf.encode_buffer_count;
	syn1->pos[pos_head] = pos;
	rp_lock_rel(syn1->mutex);
	// nsDbgPrint("rp_syn_rel1 id %d at %d: %d\n", syn1->id, pos_head, pos);
	// s32 count;
	rp_sem_rel(syn1->sem, 1);
	return 0;
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
	// return rp_syn_acq(&rp_ctx->syn.screen.encode, timeout);
	return rp_syn_acq1(&rp_ctx->syn.screen.encode, timeout);
}

static int rp_screen_transfer_release(u8 pos) {
	return rp_syn_rel1(&rp_ctx->syn.screen.transfer, pos);
}

static s32 rp_network_transfer_acquire(s64 timeout) {
	return rp_syn_acq(&rp_ctx->syn.network.transfer, timeout);
}

static void rp_network_encode_release(u8 pos) {
	rp_syn_rel(&rp_ctx->syn.network.encode, pos);
}

static s32 rp_network_encode_acquire(s64 timeout) {
	if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
		return rp_syn_acq1(&rp_ctx->syn.network.encode, timeout);
	} else {
		return rp_syn_acq(&rp_ctx->syn.network.encode, timeout);
	}
}

static int rp_network_transfer_release(u8 pos) {
	if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode)	{
		return rp_syn_rel1(&rp_ctx->syn.network.transfer, pos);
	} else {
		rp_syn_rel(&rp_ctx->syn.network.transfer, pos);
		return 0;
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

	int bufSize = ret;
	if (!__atomic_load_n(&kcp_recv_ready, __ATOMIC_ACQUIRE)) {
		svc_sleepThread(RP_THREAD_LOOP_FAST_WAIT);
		return;
	}

	if ((ret = svc_waitSynchronization1(rp_ctx->kcp_mutex, RP_SYN_WAIT_MAX))) {
		nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		return;
	}
	if (!__atomic_load_n(&rp_ctx->kcp_ready, __ATOMIC_ACQUIRE)) {
		svc_releaseMutex(rp_ctx->kcp_mutex);
		svc_sleepThread(RP_THREAD_LOOP_FAST_WAIT);
		return;
	}

	if ((ret = ikcp_input(&rp_ctx->kcp, (const char *)rpRecvBuffer, bufSize)) < 0) {
		nsDbgPrint("ikcp_input failed: %d\n", ret);
	}

	ikcp_update(&rp_ctx->kcp, iclock());
	ret = ikcp_recv(&rp_ctx->kcp, (char *)rpRecvBuffer, RP_CONTROL_RECV_BUFFER_SIZE);
	if (ret >= 0) {
		rpControlRecvHandle(rpRecvBuffer, ret);
	}
	svc_releaseMutex(rp_ctx->kcp_mutex);
}

static void rpInitPriorityCtx(void) {
	rp_lock_close(rp_ctx->dyn_prio.mutex);
	memset(&rp_ctx->dyn_prio, 0, sizeof(rp_ctx->dyn_prio));
	rp_lock_init(rp_ctx->dyn_prio.mutex);

	for (int i = 0; i < SCREEN_COUNT; ++i) {
		rp_ctx->dyn_prio.s[i].initializing = RP_DYN_PRIO_FRAME_COUNT;

		rp_ctx->dyn_prio.s[i].priority = rp_ctx->conf.screen_priority[i];
	}
}

static int rpGetPriorityScreen(int *frame_rate) {
	for (int i = 0; i < SCREEN_COUNT; ++i)
		if (rp_ctx->conf.screen_priority[i] == 0)
			return i;

	struct rp_dyo_prio_t *ctx = &rp_ctx->dyn_prio;
	int top_bot;
	if (rp_lock_wait(ctx->mutex, RP_SYN_WAIT_MAX) != 0)
		return 0;

#define SET_WITH_SIZE(si, c0, c1, te, ta) do { \
	top_bot = si; \
	ctx->s[c1].te -=ctx->s[c0].te; \
	ctx->s[c0].te = ctx->s[c0].ta; \
} while (0)
#define SET_WITH_FRAME_SIZE(si, c0, c1) SET_WITH_SIZE(si, c0, c1, frame_size_est, frame_size_acc)
#define SET_WITH_PRIORITY_SIZE(si, c0, c1) SET_WITH_SIZE(si, c0, c1, priority_size_est, priority_size_acc)

#define SET_WITH_SIZE_0(si, c0, c1, te, ta) do { \
	top_bot = si; \
	ctx->s[c1].te = 0; \
	ctx->s[c0].te = ctx->s[c0].ta; \
} while (0)
#define SET_WITH_FRAME_SIZE_0(si, c0, c1) SET_WITH_SIZE_0(si, c0, c1, frame_size_est, frame_size_acc)

#define SET_SIZE(_, c0, c1, te, ta) do { \
	if (ctx->s[c0].te <= ctx->s[c1].te) { \
		ctx->s[c1].te -=ctx->s[c0].te; \
	} else { \
		ctx->s[c1].te = 0; \
	} \
	ctx->s[c0].te = ctx->s[c0].ta; \
} while (0)
#define SET_FRAME_SIZE(si, c0, c1) SET_SIZE(si, c0, c1, frame_size_est, frame_size_acc)
#define SET_PRIORITY_SIZE(si, c0, c1) SET_SIZE(si, c0, c1, priority_size_est, priority_size_acc)

#define SET_CASE_FRAME_SIZE(s0, s1) do { \
	if (ctx->s[s0].priority_size_est <= ctx->s[s1].priority) { \
		SET_WITH_FRAME_SIZE(s0, s0, s1); \
		SET_PRIORITY_SIZE(s0, s0, s1); \
	} else { \
		SET_WITH_FRAME_SIZE_0(s1, s1, s0); \
		SET_PRIORITY_SIZE(s1, s1, s0); \
	} \
} while (0)

#define SET_CASE_PRIORITY_SIZE(s0, s1) do { \
	SET_WITH_PRIORITY_SIZE(s0, s0, s1); \
	if (rp_ctx->conf.dynamic_priority) \
		SET_FRAME_SIZE(s0, s0, s1); \
} while (0)

	if (rp_ctx->conf.dynamic_priority &&
		ctx->s[SCREEN_TOP].frame_rate + ctx->s[SCREEN_BOT].frame_rate >= rp_ctx->conf.target_frame_rate
	) {
		if (ctx->s[SCREEN_TOP].frame_size_est <= ctx->s[SCREEN_BOT].frame_size_est) {
			SET_CASE_FRAME_SIZE(SCREEN_TOP, SCREEN_BOT);
		} else {
			SET_CASE_FRAME_SIZE(SCREEN_BOT, SCREEN_TOP);
		}
	} else {
		if (ctx->s[SCREEN_TOP].priority_size_est <= ctx->s[SCREEN_BOT].priority_size_est) {
			SET_CASE_PRIORITY_SIZE(SCREEN_TOP, SCREEN_BOT);
		} else {
			SET_CASE_PRIORITY_SIZE(SCREEN_BOT, SCREEN_TOP);
		}
	}

#undef SET_CASE_PRIORITY_SIZE
#undef SET_CASE_FRAME_SIZE

#undef SET_WITH_FRAME_SIZE
#undef SET_WITH_PRIORITY_SIZE
#undef SET_WITH_SIZE

#undef SET_WITH_FRAME_SIZE_0
#undef SET_WITH_SIZE_0

#undef SET_FRAME_SIZE
#undef SET_PRIORITY_SIZE
#undef SET_SIZE

	if (frame_rate)
		*frame_rate = ctx->s[SCREEN_TOP].frame_rate + ctx->s[SCREEN_BOT].frame_rate;
	rp_lock_rel(ctx->mutex);
	return top_bot;
}

static void rpSetPriorityScreen(int top_bot, u32 size) {
	if (rp_ctx->conf.screen_priority[SCREEN_TOP] == 0 || rp_ctx->conf.screen_priority[SCREEN_BOT] == 0)
		return;

	struct rp_dyo_prio_t *ctx = &rp_ctx->dyn_prio;
	struct rp_dyo_prio_screen_t *sctx;
	sctx = &rp_ctx->dyn_prio.s[top_bot];

	if (rp_lock_wait(ctx->mutex, RP_SYN_WAIT_MAX))
		return;

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

	rp_lock_rel(ctx->mutex);
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
	rp_ctx->conf.me_block_size = RP_ME_MIN_BLOCK_SIZE << ((rp_ctx->conf.arg1 & 0x600) >> 9);
	rp_ctx->conf.me_block_size_log2 = av_ceil_log2(rp_ctx->conf.me_block_size);
	rp_ctx->conf.me_search_param = ((rp_ctx->conf.arg1 & 0xf800) >> 11) + RP_ME_MIN_SEARCH_PARAM;
	rp_ctx->conf.me_bpp = av_ceil_log2(rp_ctx->conf.me_search_param * 2 + 1);
	rp_ctx->conf.me_bpp_half_range = (1 << rp_ctx->conf.me_bpp) >> 1;
	rp_ctx->conf.me_downscale = ((rp_ctx->conf.arg1 & 0x10000) >> 16);
#if RP_ME_INTERPOLATE
	rp_ctx->conf.me_interpolate = ((rp_ctx->conf.arg1 & 0x20000) >> 17);
#else
	rp_ctx->conf.me_interpolate = 0;
#endif
	rp_ctx->conf.encode_verify = ((rp_ctx->conf.arg1 & 0x40000) >> 18);

	rp_ctx->conf.screen_priority[SCREEN_TOP] = (rp_ctx->conf.arg2 & 0xf);
	rp_ctx->conf.screen_priority[SCREEN_BOT] = (rp_ctx->conf.arg2 & 0xf0) >> 4;
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

static void rp_acquire_params(int thread_n UNUSED) {
}

static void rp_release_params(int thread_n UNUSED) {
}

static void UNUSED rp_acquire_params1(int thread_n UNUSED) {
}

static int rp_check_params(int thread_n UNUSED) {
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
	if ((ret = svc_waitSynchronization1(rp_ctx->kcp_mutex, RP_SYN_WAIT_MAX))) {
		nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		return;
	}
	ikcpcb *kcp = ikcp_create(&rp_ctx->kcp, rp_ctx->conf.kcp_conv, 0);
	if (!kcp) {
		nsDbgPrint("ikcp_create failed\n");

		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		svc_releaseMutex(rp_ctx->kcp_mutex);
		return;
	} else {
		rp_ctx->kcp.output = rp_udp_output;
		if ((ret = ikcp_setmtu(&rp_ctx->kcp, KCP_PACKET_SIZE)) < 0) {
			nsDbgPrint("ikcp_setmtu failed: %d\n", ret);
		}
		ikcp_nodelay(&rp_ctx->kcp, 2, 10, 2, 1);
		// rp_ctx->kcp->rx_minrto = 10;
		ikcp_wndsize(&rp_ctx->kcp, KCP_SND_WND_SIZE, 0);
	}
	__atomic_store_n(&rp_ctx->kcp_ready, 1, __ATOMIC_RELEASE);

	// send empty header to mark beginning
	{
		struct rp_send_header empty_header = { 0 };

		ret = ikcp_send(&rp_ctx->kcp, (const char *)&empty_header, sizeof(empty_header));

		if (ret < 0) {
			nsDbgPrint("ikcp_send failed: %d\n", ret);
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		}
	}

	ikcp_update(&rp_ctx->kcp, iclock());
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

		if ((ret = svc_waitSynchronization1(rp_ctx->kcp_mutex, RP_SYN_WAIT_MAX))) {
			nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
			break;
		}
		ikcp_update(&rp_ctx->kcp, iclock());
		svc_releaseMutex(rp_ctx->kcp_mutex);

		s32 pos = rp_network_transfer_acquire(RP_THREAD_LOOP_FAST_WAIT);
		if (pos < 0) {
			continue;
		}
		struct rp_network_encode_t *network_ctx = &rp_ctx->network_encode[pos];

		last_tick = curr_tick;

		int top_bot = network_ctx->top_bot;
		struct rp_send_header header = {
			.size = network_ctx->size,
			.size_1 = network_ctx->size_1,
			.frame_n = network_ctx->frame_n,
			.bpp = network_ctx->bpp,
			.format = network_ctx->format,
			.flags = (top_bot ? RP_SEND_HEADER_TOP_BOT : 0) |
				(network_ctx->p_frame ? RP_SEND_HEADER_P_FRAME : 0),
		};
		// nsDbgPrint("%s %d acquired network transfer: %d\n",
		// 	RP_TOP_BOT_STR(header.top_bot), header.frame_n, pos);
		u32 size_remain = header.size + header.size_1;
		u8 *data = network_ctx->buffer;
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
			if ((ret = svc_waitSynchronization1(rp_ctx->kcp_mutex, RP_SYN_WAIT_MAX))) {
				nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				break;
			}
			int waitsnd = ikcp_waitsnd(&rp_ctx->kcp);
			if (waitsnd < KCP_SND_WND_SIZE) {
				u8 *kcp_send_buffer = rp_ctx->kcp_send_buffer;
				memcpy(kcp_send_buffer, &header, sizeof(header));
				memcpy(kcp_send_buffer + sizeof(header), data, data_size);

				ret = ikcp_send(&rp_ctx->kcp, (const char *)kcp_send_buffer, data_size + sizeof(header));

				if (ret < 0) {
					nsDbgPrint("ikcp_send failed: %d\n", ret);

					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					svc_releaseMutex(rp_ctx->kcp_mutex);
					break;
				}

				size_remain -= data_size;
				data += data_size;

				ikcp_update(&rp_ctx->kcp, iclock());
				svc_releaseMutex(rp_ctx->kcp_mutex);

				desired_last_tick += rp_ctx->conf.min_send_interval_ticks;
				last_tick = curr_tick;
				break;
			}
			ikcp_update(&rp_ctx->kcp, iclock());
			svc_releaseMutex(rp_ctx->kcp_mutex);

			svc_sleepThread(RP_THREAD_KCP_LOOP_WAIT);
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
			if ((ret = svc_waitSynchronization1(rp_ctx->kcp_mutex, RP_SYN_WAIT_MAX))) {
				nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				break;
			}
			int waitsnd = ikcp_waitsnd(&rp_ctx->kcp);
			if (waitsnd < KCP_SND_WND_SIZE) {
				// nsDbgPrint("ikcp_send %d\n", iclock());
				ret = ikcp_send(&rp_ctx->kcp, (const char *)data, data_size);

				if (ret < 0) {
					nsDbgPrint("ikcp_send failed: %d\n", ret);

					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					svc_releaseMutex(rp_ctx->kcp_mutex);
					break;
				}

				size_remain -= data_size;
				data += data_size;

				ikcp_update(&rp_ctx->kcp, iclock());
				svc_releaseMutex(rp_ctx->kcp_mutex);

				desired_last_tick += rp_ctx->conf.min_send_interval_ticks;
				last_tick = curr_tick;

				if (last_tick - desired_last_tick < (1ULL << 48) &&
					last_tick - desired_last_tick > rp_ctx->conf.min_send_interval_ticks * KCP_SND_WND_SIZE
				)
					desired_last_tick = last_tick - rp_ctx->conf.min_send_interval_ticks * KCP_SND_WND_SIZE;
				continue;
			}
			ikcp_update(&rp_ctx->kcp, iclock());
			svc_releaseMutex(rp_ctx->kcp_mutex);

			svc_sleepThread(RP_THREAD_KCP_LOOP_WAIT);
		}

		rp_network_encode_release(pos);
	}

	// kcp deinit
	if ((ret = svc_waitSynchronization1(rp_ctx->kcp_mutex, RP_SYN_WAIT_MAX))) {
		nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		return;
	}
	__atomic_store_n(&rp_ctx->kcp_ready, 0, __ATOMIC_RELEASE);
	ikcp_release(&rp_ctx->kcp);
	// rp_ctx->kcp = 0;
	svc_releaseMutex(rp_ctx->kcp_mutex);
}

static void rpNetworkTransferThread(u32 arg UNUSED) {
	svc_sleepThread(RP_THREAD_NETWORK_BEGIN_WAIT);
	int thread_n = -1;
	rp_acquire_params(thread_n);
	while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
		__atomic_store_n(&rp_ctx->kcp_restart, 0, __ATOMIC_RELAXED);
		rpNetworkTransfer(thread_n);
		svc_sleepThread(RP_THREAD_LOOP_SLOW_WAIT);
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

static int rpCaptureScreen(struct rp_screen_encode_t *screen_ctx) {
	u32 bufSize = screen_ctx->pitch * (screen_ctx->c.top_bot == 0 ? 400 : 320);
	if (bufSize > RP_SCREEN_BUFFER_SIZE) {
		nsDbgPrint("rpCaptureScreen bufSize too large: %x > %x\n", bufSize, RP_SCREEN_BUFFER_SIZE);
		return -1;
	}

	u32 phys = screen_ctx->fbaddr;
	u8 *dest = screen_ctx->buffer;
	Handle hProcess = rp_ctx->home_handle;

	Handle *hdma = &screen_ctx->hdma;
	if (*hdma)
		svc_closeHandle(*hdma);
	*hdma = 0;

	int ret;
	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		ret = svc_startInterProcessDma(hdma, CURRENT_PROCESS_HANDLE,
			dest, hProcess, (const void *)(0x1F000000 + (phys - 0x18000000)), bufSize, (u32 *)rp_ctx->dma_config);
		if (ret != 0)
			return ret;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			ret = svc_startInterProcessDma(hdma, CURRENT_PROCESS_HANDLE,
				dest, hProcess, (const void *)(rp_ctx->game_fcram_base + (phys - 0x20000000)), bufSize, (u32 *)rp_ctx->dma_config);
			if (ret != 0)
				return ret;
		} else {
			return 0;
		}
	} else {
		svc_sleepThread(RP_THREAD_LOOP_IDLE_WAIT);
		return 0;
	}

	u32 state, i;
	for (i = 0; i < RP_THREAD_LOOP_WAIT_COUNT; i++ ) {
		state = 0;
		svc_getDmaState(&state, *hdma);
		if (state == 4)
			break;
		svc_sleepThread(RP_THREAD_LOOP_ULTRA_FAST_WAIT);
	}
	if (i >= RP_THREAD_LOOP_WAIT_COUNT) {
		nsDbgPrint("rpCaptureScreen time out %08x", state, 0);
	}
	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)dest, bufSize);
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
	RP_JLS_INIT_LUT(7, RP_ENCODE_PARAMS_BPP7, vLUT_bpp7);
	RP_JLS_INIT_LUT(6, RP_ENCODE_PARAMS_BPP6, vLUT_bpp6);
	RP_JLS_INIT_LUT(5, RP_ENCODE_PARAMS_BPP5, vLUT_bpp5);
	RP_JLS_INIT_LUT(4, RP_ENCODE_PARAMS_BPP4, vLUT_bpp4);

#undef RP_JLS_INIT_LUT
}

extern const uint8_t psl0[];
static void jpeg_ls_encode_pad_source(u8 *dst, int dst_size, const u8 *src_unpadded, int width, int height);
static int UNUSED ffmpeg_jls_decode(uint8_t *dst, int width, int height, int pitch, const uint8_t *src, int src_size, int bpp) {
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

static int rpJLSEncodeImage(struct rp_jls_ctx_t *jls_ctx, u8 *dst, int dst_size, const u8 *src, int w, int h, int bpp) {
	// nsDbgPrint("rpJLSEncodeImage: ctx = (%d), buffer = (%d), image = (%d), w = %d, h = %d, bpp = %d\n",
	// 	(s32)jls_ctx, (s32)dst, (s32)src, w, h, bpp
	// );

	XXH32_hash_t UNUSED src_hash = 0;
	u8 *dst2 = 0;
	if (RP_ENCODE_VERIFY && rp_ctx->conf.encode_verify) {
#if RP_ENCODE_VERIFY
		src_hash = XXH32(src, w * (h + LEFTMARGIN + RIGHTMARGIN), 0);
		dst2 = jls_ctx->verify_buffer.encode;

		if (rp_ctx->conf.encoder_which == 1) {
			u8 *tmp = dst;
			dst = dst2;
			dst2 = tmp;
		}
#endif
	} else {
		dst2 = dst;
	}

	struct jls_enc_params *params;
	switch (bpp) {
		case 8:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP8]; break;

		case 7:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP7]; break;

		case 6:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP6]; break;

		case 5:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP5]; break;

		case 4:
			params = &rp_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP4]; break;

		default:
			nsDbgPrint("Unsupported bpp in rpJLSEncodeImage: %d\n", bpp);
			return -1;
	}

	int ret = 0, ret2 = 0;
	if ((RP_ENCODE_VERIFY && rp_ctx->conf.encode_verify) || rp_ctx->conf.encoder_which == 0) {
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
	}
	if ((RP_ENCODE_VERIFY && rp_ctx->conf.encode_verify) || rp_ctx->conf.encoder_which == 1) {
		struct jls_enc_ctx *ctx = &jls_ctx->enc;
		struct bito_ctx *bctx = &jls_ctx->bito;
		ret2 = jpeg_ls_encode(
			params, ctx, bctx, (char *)dst2, dst_size, src,
			w, h, h + LEFTMARGIN + RIGHTMARGIN,
			rp_ctx->jls_enc_luts.classmap
		);
	}
	if (RP_ENCODE_VERIFY && rp_ctx->conf.encode_verify) {
#if RP_ENCODE_VERIFY
		nsDbgPrint("rpJLSEncodeImage: w = %d, h = %d, bpp = %d\n", w, h, bpp);
		if (src_hash != XXH32(src, w * (h + LEFTMARGIN + RIGHTMARGIN), 0))
			nsDbgPrint("rpJLSEncodeImage src buffer corrupt during encode, race condition?\n");
		if (ret != ret2) {
			nsDbgPrint("Failed encode size verify: %d, %d\n", ret, ret2);
		} else if (memcmp(dst, dst2, ret) != 0) {
			nsDbgPrint("Failed encode content verify\n");
		} else {
			u8 *decoded = jls_ctx->verify_buffer.decode;

			int ret3 = ffmpeg_jls_decode(decoded, w, h, h, dst2, ret2, bpp);
			if (ret3 != w * h) {
				nsDbgPrint("Failed decode size verify: %d (expected %d)\n", ret3, w * h);
			} else {
				for (int i = 0; i < w; ++i) {
					if (memcmp(decoded + i * h, src + LEFTMARGIN + i * (h + LEFTMARGIN + RIGHTMARGIN), h) != 0) {
						nsDbgPrint("Failed decode content verify at col %d\n", i);
						break;
					}
				}

				u8 *decode_padded = jls_ctx->verify_buffer.decode_padded;
				int decode_padded_size = sizeof(jls_ctx->verify_buffer.decode_padded);

				jpeg_ls_encode_pad_source(decode_padded, decode_padded_size, decoded, w, h);
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
	} else if (rp_ctx->conf.encoder_which == 1) {
		ret = ret2;
	}

	if (ret >= dst_size) {
		nsDbgPrint("Possible buffer overrun in rpJLSEncodeImage\n");
		return -1;
	}
	if (RP_ENCODE_VERIFY && rp_ctx->conf.encode_verify) {
		return rp_ctx->conf.encoder_which == 0 ? ret : ret2;
	} else {
		return ret;
	}
}

#define rshift_to_even(n, s) (((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))
#define srshift_to_even(t, n, s) ((t)((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))

static ALWAYS_INLINE
void convert_yuv_hp(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	int bpp
) {
	u8 half_range = 1 << (bpp - 1);
	u8 bpp_mask = (1 << bpp) - 1;
	switch (rp_ctx->conf.color_transform_hp) {
		case 1:
			*y_out = g;
			*u_out = (r - g + half_range) & bpp_mask;
			*v_out = (b - g + half_range) & bpp_mask;
			break;

		case 2:
			*y_out = g;
			*u_out = (r - g + half_range) & bpp_mask;
			*v_out = (b - (((s16)r + g) >> 1) - half_range) & bpp_mask;
			break;

		case 3: {
			u8 quarter_range = 1 << (bpp - 2);
			u8 u = (r - g + half_range) & bpp_mask;
			u8 v = (b - g + half_range) & bpp_mask;

			*y_out = ((s16)g + ((u + v) >> 2) - quarter_range) & bpp_mask;
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
	u8 bpp_mask = (1 << bpp) - 1;
	u8 bpp_2_mask = (1 << (bpp + 1)) - 1;
	switch (rp_ctx->conf.color_transform_hp) {
		case 1:
			*y_out = g;
			*u_out = (r - half_g + half_range) & bpp_mask;
			*v_out = (b - half_g + half_range) & bpp_mask;
			break;

		case 2:
			*y_out = g;
			*u_out = (r - half_g + half_range) & bpp_mask;
			*v_out = (b - (((s16)r + half_g) >> 1) - half_range) & bpp_mask;
			break;

		case 3: {
			u8 u = (r - half_g + half_range) & bpp_mask;
			u8 v = (b - half_g + half_range) & bpp_mask;

			*y_out = ((s16)g + ((u + v) >> 1) - half_range) & bpp_2_mask;
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
	u8 bpp_mask = (1 << bpp) - 1; \
	u8 UNUSED bpp_2_mask = (1 << (bpp + (bpp_2 ? 1 : 0))) - 1; \
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
			*u_out = (u8)((u8)srshift_to_even(s16, u, 8 + spp) + (128 >> spp)) & bpp_mask;
			*v_out = (u8)((u8)srshift_to_even(s16, v, 8 + spp) + (128 >> spp)) & bpp_mask;
			break;
		}

		case 3: {
			RP_RGB_SHIFT
			u16 y = 66 * (u16)r + 129 * (u16)g + 25 * (u16)b;
			s16 u = -38 * (s16)r + -74 * (s16)g + 112 * (s16)b;
			s16 v = 112 * (s16)r + -94 * (s16)g + -18 * (s16)b;
			*y_out = (u8)((u8)rshift_to_even(y, 8 + spp_2) + (16 >> spp_2)) & bpp_2_mask;
			*u_out = (u8)((u8)srshift_to_even(s16, u, 8 + spp) + (128 >> spp)) & bpp_mask;
			*v_out = (u8)((u8)srshift_to_even(s16, v, 8 + spp) + (128 >> spp)) & bpp_mask;
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

static void UNUSED jpeg_ls_encode_pad_source(u8 *dst, int dst_size, const u8 *src, int width, int height) {
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
	if (dst - ret > dst_size) {
		nsDbgPrint("Failed pad source buffer overflow: %d\n", dst - ret);
	}
}

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
	AVMotionEstPredictor *preds UNUSED = me_ctx.preds;

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

				case 1:
					ff_me_search_tss(&me_ctx, x, y, mv);
					break;

				case 2:
					ff_me_search_tdls(&me_ctx, x, y, mv);
					break;

				case 3:
					ff_me_search_ntss(&me_ctx, x, y, mv);
					break;

				case 4:
					ff_me_search_fss(&me_ctx, x, y, mv);
					break;

				case 5:
					ff_me_search_ds(&me_ctx, x, y, mv);
					break;

				case 6:
					ff_me_search_hexbs(&me_ctx, x, y, mv);
					break;

				case 7:
					// Diff Only
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

	u8 block_x_n UNUSED = width >> block_size_log2;
	u8 block_y_n = height >> block_size_log2;
	u8 block_pitch = block_y_n + RIGHTMARGIN + LEFTMARGIN;
	u8 x_off = (width & block_size_mask) >> 1;
	u8 y_off = (height & block_size_mask) >> 1;

	if (RP_ME_INTERPOLATE && rp_ctx->conf.me_interpolate) {
		x_off += block_size >> 1;
		y_off += block_size >> 1;
	}

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
	int c_x = av_clip(x, -i, width - i - 1); \
	int c_y = av_clip(y, -j, height - j - 1); \
	const u8 *ref_est = ref++ + c_x * (height + LEFTMARGIN + RIGHTMARGIN) + c_y; \
	*dst++ = (u8)((u8)(*cur++) - (u8)(*ref_est) + (128 >> (8 - bpp))) & ((1 << bpp) - 1); \
} while (0)

		if (RP_ME_INTERPOLATE && rp_ctx->conf.me_interpolate) {
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
			int x = ((s16)*me_x - (s16)half_range) << scale_log2;
			int y = ((s16)*me_y - (s16)half_range) << scale_log2;
			for (int j = 0; j < height; ++j) {
				int j_off = (j - y_off) & block_size_mask;
				if (j > y_off && j_off == 0 && j < height - y_off - 1) {
					++me_x;
					++me_y;

					x = (int)((s16)*me_x - (s16)half_range) << scale_log2;
					y = (int)((s16)*me_y - (s16)half_range) << scale_log2;
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

static int rpEncodeImage(struct rp_screen_encode_t *screen_ctx, struct rp_image_me_t *image_me_ctx) {
	int width, height;
	if (screen_ctx->c.top_bot == 0) {
		width = 400;
	} else {
		width = 320;
	}
	height = 240;

	int format = screen_ctx->format;
	int bytes_per_pixel;
	if (format == 0) {
		bytes_per_pixel = 4;
	} else if (format == 1) {
		bytes_per_pixel = 3;
	} else {
		bytes_per_pixel = 2;
	}
	int bytes_per_column = bytes_per_pixel * height;
	int pitch = screen_ctx->pitch;
	int bytes_to_next_column = pitch - bytes_per_column;

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

	const u8 *y_image_prev;
	const u8 *u_image_prev;
	const u8 *v_image_prev;
	const u8 *ds_u_image_prev;
	const u8 *ds_v_image_prev;
	const u8 *ds_y_image_prev;
	const u8 *ds_ds_y_image_prev;

	u8 *y_image_me;
	u8 *u_image_me;
	u8 *v_image_me;
	u8 *ds_u_image_me;
	u8 *ds_v_image_me;
	s8 *me_x_image;
	s8 *me_y_image;

#define RP_IMAGE_SET(si, sn) do { \
	struct rp_image_ctx_t *c = &screen_ctx->c; \
	y_image = c->image->sn.y_image; \
	u_image = c->image->sn.u_image; \
	v_image = c->image->sn.v_image; \
	ds_y_image = c->image->sn.ds_y_image; \
	ds_u_image = c->image->sn.ds_u_image; \
	ds_v_image = c->image->sn.ds_v_image; \
	ds_ds_y_image = c->image->sn.ds_ds_y_image; \
	y_bpp = &c->image->s[si].y_bpp; \
	u_bpp = &c->image->s[si].u_bpp; \
	v_bpp = &c->image->s[si].v_bpp; \
	me_bpp = &c->image->s[si].me_bpp; \
 \
	y_image_prev = c->image_prev->sn.y_image; \
	u_image_prev = c->image_prev->sn.u_image; \
	v_image_prev = c->image_prev->sn.v_image; \
	ds_y_image_prev = c->image_prev->sn.ds_y_image; \
	ds_u_image_prev = c->image_prev->sn.ds_u_image; \
	ds_v_image_prev = c->image_prev->sn.ds_v_image; \
	ds_ds_y_image_prev = c->image_prev->sn.ds_ds_y_image; \
 \
	y_image_me = image_me_ctx->sn.y_image; \
	u_image_me = image_me_ctx->sn.u_image; \
	v_image_me = image_me_ctx->sn.v_image; \
	ds_u_image_me = image_me_ctx->sn.ds_u_image; \
	ds_v_image_me = image_me_ctx->sn.ds_v_image; \
	me_x_image = image_me_ctx->sn.me_x_image; \
	me_y_image = image_me_ctx->sn.me_y_image; \
 \
	c->image->s[si].format = format; } while (0)

	if (screen_ctx->c.top_bot == 0) {
		RP_IMAGE_SET(SCREEN_TOP, top);
	} else {
		RP_IMAGE_SET(SCREEN_BOT, bot);
	}

#undef RP_IMAGE_SET

	*me_bpp = rp_ctx->conf.me_bpp;

	int ret = convert_yuv_image(
		format, width, height, bytes_per_pixel, bytes_to_next_column,
		screen_ctx->buffer,
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

	if (screen_ctx->c.p_frame) {
		int ds_width = width / 2;
		int ds_height = height / 2;

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

static int rpImageReadLock(struct rp_image_common_t *image_common) {
	s32 res;
	// if ((res = rp_lock_wait(image_common->sem_read, RP_SYN_WAIT_MAX))) {
	// 	return res;
	// }
	if (__atomic_fetch_add(&image_common->sem_count, 1, __ATOMIC_RELAXED) == 0) {
		if ((res = rp_sem_wait(image_common->sem_write, RP_SYN_WAIT_MAX))) {
			__atomic_sub_fetch(&image_common->sem_count, 1, __ATOMIC_RELAXED);
			// rp_lock_rel(image_common->sem_read);
			return res;
		}
	}
	// rp_lock_rel(image_common->sem_read);
	return 0;
}

static int rpImageReadUnlockCount(struct rp_image_common_t *image_common, int count) {
	// s32 res;
	// if ((res = rp_lock_wait(image_common->sem_read, RP_SYN_WAIT_MAX))) {
	// 	return res;
	// }
	if (__atomic_add_fetch(&image_common->sem_count, count, __ATOMIC_RELAXED) >= 4) { // (4) is lock/unlock for 2 readers
		__atomic_store_n(&image_common->sem_count, 0, __ATOMIC_RELAXED);
		rp_sem_rel(image_common->sem_write, 1);
		rp_sem_rel(image_common->sem_try, 1);
	}
	// rp_lock_rel(image_common->sem_read);
	return 0;
}

static int rpImageReadUnlock(struct rp_image_common_t *image_common) {
	return rpImageReadUnlockCount(image_common, 1);
}

void rpKernelCallback(struct rp_screen_encode_t *screen_ctx);
static void rpEncodeScreenAndSend(int thread_n) {
	svc_sleepThread(RP_THREAD_ENCODE_BEGIN_WAIT);

	int ret;
	rp_acquire_params(thread_n);

	u64 last_tick = svc_getSystemTick(), curr_tick;
	u64 sleep_duration = 0;
	int acquire_count = 0;
	struct rp_image_me_t *image_me_ctx = &rp_ctx->image_me[thread_n];
	while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
		rp_check_params(thread_n);

		s32 pos;

		struct rp_screen_encode_t *screen_ctx;
		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			pos = rp_screen_encode_acquire(RP_THREAD_LOOP_MED_WAIT);
			if (pos < 0) {
				if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) {
					nsDbgPrint("rp_screen_encode_acquire timeout\n");
					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					break;
				}
				continue;
			}
			acquire_count = 0;

			screen_ctx = &rp_ctx->screen_encode[pos];
			// nsDbgPrint("%s acquired screen encode: %d\n", RP_TOP_BOT_STR(top_bot), pos);
		} else {
			pos = thread_n;

			if (sleep_duration)
				svc_sleepThread(sleep_duration);

			u64 tick_diff = (curr_tick = svc_getSystemTick()) - last_tick;
			int frame_rate;
			int top_bot = rpGetPriorityScreen(&frame_rate);
			u64 desired_tick_diff = (u64)SYSTICK_PER_SEC * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM / frame_rate;
			desired_tick_diff = RP_MIN(desired_tick_diff, rp_ctx->conf.max_capture_interval_ticks);
			if (tick_diff < desired_tick_diff) {
				sleep_duration = (desired_tick_diff - tick_diff) * 1000 / SYSTICK_PER_US;
			} else {
				sleep_duration = 0;
			}

			screen_ctx = &rp_ctx->screen_encode[pos];
			screen_ctx->c.top_bot = top_bot;
			rpKernelCallback(screen_ctx);

			int capture_n = 0;
			do {
				ret = rpCaptureScreen(screen_ctx);

				if (ret == 0)
					break;

				if (++capture_n > RP_THREAD_LOOP_WAIT_COUNT) {
					nsDbgPrint("rpCaptureScreen failed\n");
					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					goto final;
				}

				svc_sleepThread(RP_THREAD_LOOP_FAST_WAIT);
			} while (1);

			struct rp_screen_image_t *screen_image_ctx = &rp_ctx->screen_image[top_bot];

			u8 *image_n_prev = &screen_image_ctx->image_n;
			u8 image_n = *image_n_prev;
			*image_n_prev = (*image_n_prev + 1) % RP_IMAGE_BUFFER_COUNT;

			u8 *p_frame_prev = &screen_image_ctx->p_frame;
			u8 p_frame = *p_frame_prev;
			if (!*p_frame_prev)
				*p_frame_prev = 1;

			u8 frame_n = screen_image_ctx->frame_n++;

			screen_ctx->c.p_frame = p_frame;
			screen_ctx->c.frame_n = frame_n;

			screen_ctx->c.image = &rp_ctx->image[image_n];
			image_n = (image_n + (RP_IMAGE_BUFFER_COUNT - 1)) % RP_IMAGE_BUFFER_COUNT;
			screen_ctx->c.image_prev = &rp_ctx->image[image_n];
		}

		struct rp_image_ctx_t image_ctx = screen_ctx->c;
		struct rp_image_common_t *image_common = &image_ctx.image->s[image_ctx.top_bot];
		struct rp_image_common_t *image_prev_common = &image_ctx.image_prev->s[image_ctx.top_bot];

		if (rp_ctx->conf.me_method == 0) {
			image_ctx.p_frame = 0;
		}

		if (image_ctx.p_frame &&
			screen_ctx->format != image_prev_common->format
		) {
			nsDbgPrint("Screen format change; key frame\n");
			image_ctx.p_frame = 0;
		}

		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			if (rp_ctx->conf.me_method != 0) {
				if (image_ctx.p_frame) {
					// s32 res;
					// if ((res = rp_sem_wait(image_prev_common->sem_try, 1, RP_SYN_WAIT_MAX))) {
					// 	__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					// 	nsDbgPrint("%d rpEncodeScreenAndSend sem try wait timeout/error (%d) at %d (%d)\n", thread_n, res, pos, (s32)image_ctx.image);
					// 	goto final;
					// }
					if ((ret = rpImageReadLock(image_prev_common))) {
						nsDbgPrint("%d rpEncodeScreenAndSend rpImageReadLock image_prev timeout/error\n", thread_n, ret);
						__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
						break;
					}
					// rp_sem_rel(image_prev_common->sem_try, 1);
				} else {
					rpImageReadUnlockCount(image_prev_common, 2); // (2) to skip both lock/unlock
				}
			}
		}

		screen_ctx->c.p_frame = image_ctx.p_frame;
		ret = rpEncodeImage(screen_ctx, image_me_ctx);
		if (ret < 0) {
			nsDbgPrint("rpEncodeImage failed\n");
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
			break;
		}

		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			// s32 count;
			if (rp_ctx->conf.me_method != 0) {
				if (image_ctx.p_frame) {
					if ((ret = rpImageReadUnlock(image_prev_common))) {
						nsDbgPrint("%d rpEncodeScreenAndSend rpImageReadUnlock image_prev timeout/error: %d\n", thread_n, ret);
						__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
						break;
					}
				}

				if (__atomic_fetch_add(&image_common->sem_count, 1, __ATOMIC_ACQ_REL) > 0)
					rp_sem_rel(image_common->sem_write, 1);
				// if ((ret = rpImageReadLock(image_common))) {
				// 	nsDbgPrint("%d rpEncodeScreenAndSend rpImageReadLock image timeout/error\n", thread_n, ret);
				// 	__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				// 	break;
				// }
			}
			// rp_sem_rel(image_common->sem_try, 1);
			// if (rpImageReadLock(image_common)) {
			// 	__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
			// 	nsDbgPrint("%d rpEncodeScreenAndSend rpImageReadLock image timeout/error\n", thread_n);
			// 	break;
			// }

			if (rp_screen_transfer_release(pos) < 0) {
				nsDbgPrint("%d rpEncodeScreenAndSend screen release syn failed\n", thread_n);
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				break;
			}
		}

#define RP_ACCESS_TOP_BOT_S(n) \
	(image_common->n) \

#define RP_ACCESS_TOP_BOT_IMAGE(s, n) \
	((s) == 0 ? \
		image_ctx.image->top.n : \
		image_ctx.image->bot.n) \

#define RP_ACCESS_TOP_BOT_IMAGE_ME(s, n) \
	((s) == 0 ? \
		image_me_ctx->top.n : \
		image_me_ctx->bot.n) \

#define RP_PROCESS_IMAGE_AND_SEND(n, wt, wb, h, b, a) while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) { \
	if (a < 1) { \
		pos = rp_network_encode_acquire(RP_THREAD_LOOP_MED_WAIT); \
		if (pos < 0) { \
			if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) { \
				nsDbgPrint("rp_network_encode_acquire timeout\n"); \
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED); \
				break; \
			} \
			continue; \
		} \
		acquire_count = 0; \
		network_ctx = &rp_ctx->network_encode[pos]; \
	} \
	int bpp = RP_ACCESS_TOP_BOT_S(b); \
	ret = (image_ctx.p_frame || a < 1) ? rpJLSEncodeImage(jls_ctx, \
		network_ctx->buffer + (a < 1 ? 0 : ret), \
		(a < 1 ? RP_JLS_ENCODE_IMAGE_BUFFER_SIZE : RP_JLS_ENCODE_IMAGE_ME_BUFFER_SIZE), \
		(const u8 *)(image_ctx.p_frame ? \
			RP_ACCESS_TOP_BOT_IMAGE_ME(image_ctx.top_bot, n) : \
			RP_ACCESS_TOP_BOT_IMAGE(image_ctx.top_bot, n)), \
		image_ctx.top_bot == 0 ? wt : wb, \
		h, \
		bpp \
	) : 0; \
	if (ret < 0) { \
		nsDbgPrint("rpJLSEncodeImage failed\n"); \
		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED); \
		break; \
	} \
	if (a < 1) { \
		network_ctx->top_bot = image_ctx.top_bot; \
		network_ctx->frame_n = image_ctx.frame_n; \
		network_ctx->size = ret; \
		network_ctx->bpp = bpp; \
		network_ctx->format = RP_ACCESS_TOP_BOT_S(format); \
		network_ctx->p_frame = image_ctx.p_frame; \
		network_ctx->size_1 = 0; \
	} else { \
		network_ctx->size_1 = ret; \
	} \
	if (image_ctx.p_frame ? a != 0 : a < 1) \
		if (rp_network_transfer_release(pos) < 0) { \
			nsDbgPrint("%d rpEncodeScreenAndSend network release syn failed\n", thread_n); \
			__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED); \
			break; \
		}

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

		struct rp_jls_ctx_t *jls_ctx = &rp_ctx->jls_ctx[thread_n];
		struct rp_network_encode_t *network_ctx;

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

#undef RP_PROCESS_IMAGE_HASH_CHECK
#undef RP_PROCESS_IMAGE_HASH_SET
#undef RP_PROCESS_IMAGE_AND_SEND
#undef RP_PROCESS_IMAGE_AND_SEND_END
#undef RP_ACCESS_TOP_BOT_IMAGE_ME
#undef RP_ACCESS_TOP_BOT_IMAGE
#undef RP_ACCESS_TOP_BOT_S

		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode && rp_ctx->conf.me_method != 0) {
			// rp_sem_rel(image_common->sem_write, 1);
			if ((ret = rpImageReadUnlock(image_common))) {
				nsDbgPrint("%d rpEncodeScreenAndSend rpImageReadUnlock image timeout/error: %d\n", thread_n, ret);
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				break;
			}
		}
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
	u64 sleep_duration = 0;

	int acquire_count = 0;
	rp_acquire_params(thread_n);
	while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
		rp_check_params(thread_n);

		s32 pos = rp_screen_transfer_acquire(RP_THREAD_LOOP_MED_WAIT);
		if (pos < 0) {
			if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) {
				nsDbgPrint("rp_screen_transfer_acquire timeout\n");
				__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
				break;
			}
			continue;
		}
		acquire_count = 0;
		struct rp_screen_encode_t *screen_ctx = &rp_ctx->screen_encode[pos];
		// nsDbgPrint("acquired screen transfer: %d\n", pos);

		int capture_count = 0;
		while (!__atomic_load_n(&rp_ctx->exit_thread, __ATOMIC_RELAXED)) {
			if (sleep_duration)
				svc_sleepThread(sleep_duration);

			u64 tick_diff = (curr_tick = svc_getSystemTick()) - last_tick;

			int frame_rate;
			int top_bot = rpGetPriorityScreen(&frame_rate);
			u64 desired_tick_diff = (u64)SYSTICK_PER_SEC * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM / frame_rate;
			desired_tick_diff = RP_MIN(desired_tick_diff, rp_ctx->conf.max_capture_interval_ticks);
			if (tick_diff < desired_tick_diff) {
				sleep_duration = (desired_tick_diff - tick_diff) * 1000 / SYSTICK_PER_US;
			} else {
				sleep_duration = 0;
			}

			screen_ctx->c.top_bot = top_bot;

			rpKernelCallback(screen_ctx);

			ret = rpCaptureScreen(screen_ctx);

			if (ret < 0) {
				svc_sleepThread(RP_THREAD_LOOP_IDLE_WAIT);
				if (++capture_count > RP_THREAD_LOOP_WAIT_COUNT) {
					nsDbgPrint("rpCaptureScreen failed\n");
					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					break;
				}
				continue;
			}

			struct rp_screen_image_t *screen_image_ctx = &rp_ctx->screen_image[top_bot];

			u8 *image_n_prev = &screen_image_ctx->image_n;
			u8 image_n = *image_n_prev;
			*image_n_prev = (*image_n_prev + 1) % RP_IMAGE_BUFFER_COUNT;

			u8 *p_frame_prev = &screen_image_ctx->p_frame;
			u8 p_frame = *p_frame_prev;
			if (!*p_frame_prev)
				*p_frame_prev = 1;

			u8 frame_n = screen_image_ctx->frame_n++;

			screen_ctx->c.p_frame = p_frame;
			screen_ctx->c.frame_n = frame_n;

			screen_ctx->c.image = &rp_ctx->image[image_n];
			image_n = (image_n + (RP_IMAGE_BUFFER_COUNT - 1)) % RP_IMAGE_BUFFER_COUNT;
			screen_ctx->c.image_prev = &rp_ctx->image[image_n];

			if (rp_ctx->conf.me_method != 0) {
				int res;
				struct rp_image_common_t *image_common = &screen_ctx->c.image->s[top_bot];
				if ((res = rp_sem_wait(image_common->sem_try, RP_SYN_WAIT_MAX))) {
					nsDbgPrint("rpScreenTransferThread sem try wait timeout/error (%d) at %d (%d)\n", res, pos, (s32)screen_ctx->c.image);
					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					goto final;
				}
				if ((res = rp_sem_wait(image_common->sem_write, RP_SYN_WAIT_MAX))) {
					nsDbgPrint("rpScreenTransferThread sem write wait timeout/error (%d) at %d (%d)\n", res, pos, (s32)screen_ctx->c.image);
					__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
					goto final;
				}
			}
			rp_screen_encode_release(pos);
			// nsDbgPrint("%s released screen encode: %d\n", RP_TOP_BOT_STR(top_bot), pos);

			last_tick = curr_tick;
			break;
		}
	}
final:
	rp_release_params(thread_n);
	svc_exitThread();
}

static void rp_svc_increase_limits(void) {
	Handle resLim;
	Result res;
	if ((res = svcGetResourceLimit(&resLim, CURRENT_PROCESS_HANDLE))) {
		nsDbgPrint("svcGetResourceLimit failed\n");
		return;
	}
	ResourceLimitType types[] = {RESLIMIT_MUTEX, RESLIMIT_SEMAPHORE};
	int count = sizeof(types) / sizeof(types[0]);
	const char *names[] = {"mutex", "sem"};
	s64 values[] = {64, 128};

	if ((res = svcSetResourceLimitValues(resLim, types, values, count))) {
		nsDbgPrint("svcSetResourceLimitValues failed\n");
		return;
	}

	// if ((res = svcSetProcessResourceLimits(CURRENT_PROCESS_HANDLE, resLim))) {
	// 	nsDbgPrint("svcSetProcessResourceLimits failed\n");
	// 	return;
	// }
}

static void rp_svc_print_limits(void) {
	Handle resLim;
	Result res;
	if ((res = svcGetResourceLimit(&resLim, CURRENT_PROCESS_HANDLE))) {
		nsDbgPrint("svcGetResourceLimit failed\n");
		return;
	}
	ResourceLimitType types[] = {RESLIMIT_MUTEX, RESLIMIT_SEMAPHORE};
	int count = sizeof(types) / sizeof(types[0]);
	const char *names[] = {"mutex", "sem"};
	s64 values[count];

	if ((res = svcGetResourceLimitCurrentValues(values, resLim, types, count))) {
		nsDbgPrint("svcGetResourceLimitCurrentValues failed\n");
		return;
	}

	for (int i = 0; i < count; ++i) {
		nsDbgPrint("%s res current %d\n", names[i], (s32)values[i]);
	}

	if ((res = svcGetResourceLimitLimitValues(values, resLim, types, count))) {
		nsDbgPrint("svcGetResourceLimitLimitValues failed\n");
		return;
	}

	for (int i = 0; i < count; ++i) {
		nsDbgPrint("%s res limit %d\n", names[i], (s32)values[i]);
	}
}

static int rpSendFrames(void) {
	int ret = 0;

	for (int i = 0; i < SCREEN_COUNT; ++i)
		rp_ctx->screen_image[i].frame_n = rp_ctx->screen_image[i].p_frame = 0;

	// __atomic_store_n(&rp_ctx->exit_thread, 0, __ATOMIC_RELAXED);
	if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
		rp_screen_queue_init();

#define RP_INIT_SEM(s, n, m) do { \
	rp_sem_close(s); \
	int res; \
	if ((res = rp_sem_init(s, n, m))) { \
		nsDbgPrint("rpSendFrames create sem failed: %d\n", res); \
		return -1; \
	} \
} while (0)

		for (int i = 0; i < RP_IMAGE_BUFFER_COUNT; ++i) {
			for (int j = 0; j < SCREEN_COUNT; ++j) {
				RP_INIT_SEM(rp_ctx->image[i].s[j].sem_write, 1, 1);
				// rp_lock_init(rp_ctx->image[i].s[j].sem_read);
				RP_INIT_SEM(rp_ctx->image[i].s[j].sem_try, 1, 1);
				rp_ctx->image[i].s[j].sem_count = 0;
			}
		}

#undef RP_INIT_SEM

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

	rp_svc_print_limits();

	rpEncodeScreenAndSend(0);

	if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
		svc_waitSynchronization1(rp_ctx->second_thread, U64_MAX);
		svc_waitSynchronization1(rp_ctx->screen_thread, U64_MAX);
		svc_closeHandle(rp_ctx->second_thread);
		svc_closeHandle(rp_ctx->screen_thread);
	}

	return ret;
}

Result __sync_init(void);
static void rpThreadStart(u32 arg UNUSED) {
	if (RP_SYN_METHOD == 0) {
		rp_svc_increase_limits();
	} else {
		__sync_init();
	}
	jls_encoder_prepare_LUTs();
	rp_init_syn_params();
	rpInitDmaHome();
	// kRemotePlayCallback();

	// s32 res;
	// if ((res = svc_createMutex(&rp_ctx->kcp_mutex, 0))) {
	// 	nsDbgPrint("rpThreadStart create mutex failed: %d\n", res);
	// 	goto final;
	// }
	svc_createMutex(&rp_ctx->kcp_mutex, 0);
	__atomic_store_n(&kcp_recv_ready, 1, __ATOMIC_RELEASE);

	int ret = 0;
	while (ret >= 0) {
		rp_set_params();

		if ((ret = svc_waitSynchronization1(rp_ctx->kcp_mutex, RP_SYN_WAIT_MAX))) {
			nsDbgPrint("rpThreadStart kcp mutex lock timeout, %d\n", ret);
			continue;
		}
		if (__atomic_load_n(&rp_ctx->kcp_ready, __ATOMIC_ACQUIRE)) {
			__atomic_store_n(&rp_ctx->kcp_ready, 0, __ATOMIC_RELEASE);
			ikcp_release(&rp_ctx->kcp);
		}
		svc_releaseMutex(rp_ctx->kcp_mutex);

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

		ret = rpSendFrames();

		__atomic_store_n(&rp_ctx->exit_thread, 1, __ATOMIC_RELAXED);
		svc_waitSynchronization1(rp_ctx->network_thread, U64_MAX);
		svc_closeHandle(rp_ctx->network_thread);

		svc_sleepThread(RP_SYN_WAIT_IDLE);

		nsDbgPrint("Restarting RemotePlay threads\n");
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

			int storage_size = rtAlignToPageSize(sizeof(struct rp_ctx_t));
			rp_ctx = (struct rp_ctx_t *)plgRequestMemory(storage_size);
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

void rpKernelCallback(struct rp_screen_encode_t *screen_ctx) {
	// u32 ret;
	// u32 fbP2VOffset = 0xc0000000;
	u32 current_fb;

	if (screen_ctx->c.top_bot == 0) {
		screen_ctx->format = REG(IoBasePdc + 0x470);
		screen_ctx->pitch = REG(IoBasePdc + 0x490);

		current_fb = REG(IoBasePdc + 0x478);
		current_fb &= 1;

		screen_ctx->fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x468) :
			REG(IoBasePdc + 0x46c);
	} else {
		screen_ctx->format = REG(IoBasePdc + 0x570);
		screen_ctx->pitch = REG(IoBasePdc + 0x590);

		current_fb = REG(IoBasePdc + 0x578);
		current_fb &= 1;

		screen_ctx->fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x568) :
			REG(IoBasePdc + 0x56c);
	}
	screen_ctx->format &= 0x0f;
}
