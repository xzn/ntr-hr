#include "global.h"
#include "ctr/syn.h"
#include "ctr/res.h"

#include "umm_malloc.h"
#include "ikcp.h"
#include "libavcodec/jpegls.h"
#include "../jpeg_ls/global.h"
#include "../jpeg_ls/bitio.h"

// #pragma GCC diagnostic warning "-Wall"
// #pragma GCC diagnostic warning "-Wextra"
// #pragma GCC diagnostic warning "-Wpedantic"

extern IUINT32 IKCP_OVERHEAD;

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC (268123480)

#define RP_MAX(a,b) ((a) > (b) ? (a) : (b))
#define RP_MIN(a,b) ((a) > (b) ? (b) : (a))

typedef u32(*sendPacketTypedef) (u8*, u32);
static sendPacketTypedef nwmSendPacket = 0;
static RT_HOOK nwmValParamHook;

static struct {
	int yuv_option;
	int color_transform_hp;
	int encoder_which;
	int downscale_uv;

	int arg0;
	int arg1;
	int arg2;
} rp_config;

int rp_recv_sock = -1;

static u8 rpInited = 0;

static ikcpcb *rp_kcp;
static LightLock rp_kcp_mutex;
static u8 rp_kcp_ready = 0;

static u8 exit_rp_thread = 0;
static u8 exit_rp_network_thread = 0;
static u8 rp_reset_kcp = 0;
static Handle rp_second_thread;
static Handle rp_screen_thread;
static Handle rp_network_thread;

#define KCP_PACKET_SIZE 1448
#define NWM_HEADER_SIZE (0x2a + 8)
#define NWM_PACKET_SIZE (KCP_PACKET_SIZE + NWM_HEADER_SIZE)

#define KCP_MAGIC 0x12345fff
#define KCP_TIMEOUT_TICKS (250 * SYSTICK_PER_MS)
#define RP_PACKET_SIZE (KCP_PACKET_SIZE - IKCP_OVERHEAD)
#define KCP_SND_WND_SIZE 40

#define RP_DEST_PORT RP_PORT
#define RP_SCREEN_BUFFER_SIZE (400 * 240 * 4)
#define RP_UMM_HEAP_SIZE (128 * 1024)
#define RP_STACK_SIZE (0x8000)
#define RP_MISC_STACK_SIZE (0x1000)
#define RP_CONTROL_RECV_BUFFER_SIZE (2000)
#define RP_JLS_ENCODE_BUFFER_SIZE (400 * 240)

// attribute aligned
#define ALIGN_4 __attribute__ ((aligned (4)))
// assume aligned
#define ASSUME_ALIGN_4(a) (a = __builtin_assume_aligned (a, 4))
#define UNUSED __attribute__((unused))
#define FALLTHRU __attribute__((fallthrough));

enum {
	RP_ENCODE_PARAMS_BPP8,
	RP_ENCODE_PARAMS_BPP5,
	RP_ENCODE_PARAMS_BPP6,
	RP_ENCODE_PARAMS_COUNT
};
#define RP_ENCODE_THREAD_COUNT (2)
#define RP_ENCODE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 1)
#define RP_SCREEN_BUFFER_COUNT RP_ENCODE_BUFFER_COUNT
#define RP_IMAGE_BUFFER_COUNT RP_MAX(2, RP_ENCODE_THREAD_COUNT)
static struct {
	u8 nwm_send_buffer[NWM_PACKET_SIZE] ALIGN_4;
	u8 kcp_send_buffer[KCP_PACKET_SIZE] ALIGN_4;
	u8 thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 second_thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 network_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 screen_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 control_recv_buffer[RP_CONTROL_RECV_BUFFER_SIZE] ALIGN_4;
	u8 umm_heap[RP_UMM_HEAP_SIZE] ALIGN_4;

	u8 screen_buffer[RP_SCREEN_BUFFER_COUNT][RP_SCREEN_BUFFER_SIZE] ALIGN_4;
	u8 screen_top_bot[RP_SCREEN_BUFFER_COUNT] ALIGN_4;
	struct jls_enc_params jls_enc_params[RP_ENCODE_PARAMS_COUNT];
	struct jls_enc_ctx jls_enc_ctx[RP_ENCODE_THREAD_COUNT];
	struct bito_ctx jls_bito_ctx[RP_ENCODE_THREAD_COUNT];
	struct jls_enc_luts jls_enc_luts;
	u8 jls_encode_buffer[RP_ENCODE_BUFFER_COUNT][RP_JLS_ENCODE_BUFFER_SIZE] ALIGN_4;
	u8 jls_encode_top_bot[RP_ENCODE_BUFFER_COUNT] ALIGN_4;
	u8 jls_encode_frame_n[RP_ENCODE_BUFFER_COUNT] ALIGN_4;
	u32 jls_encode_size[RP_ENCODE_BUFFER_COUNT] ALIGN_4;

	struct {
		u8 y_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 u_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 v_image[400 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_u_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_v_image[200 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 y_bpp;
		u8 u_bpp;
		u8 v_bpp;
	} top_image[RP_IMAGE_BUFFER_COUNT] ALIGN_4;

	struct {
		u8 y_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 u_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 v_image[320 * (240 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_u_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 ds_v_image[160 * (120 + LEFTMARGIN + RIGHTMARGIN)] ALIGN_4;
		u8 y_bpp;
		u8 u_bpp;
		u8 v_bpp;
	} bot_image[RP_IMAGE_BUFFER_COUNT] ALIGN_4;
} *rp_storage_ctx;

static s32 rp_top_image_n;
static s32 rp_bot_image_n;
static u8 rp_top_image_send_n;
static u8 rp_bot_image_send_n;

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

	u8 *rpSendBuffer = rp_storage_ctx->nwm_send_buffer;
	*(u16*)(rpSendBuffer + 0x22 + 8) = htons(8000); // src port
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

static int rp_udp_output(const char *buf, int len, ikcpcb *kcp UNUSED, void *user UNUSED) {


	u8 *sendBuf = rp_storage_ctx->nwm_send_buffer;
	u8 *dataBuf = sendBuf + NWM_HEADER_SIZE;

	if (len > KCP_PACKET_SIZE) {
		nsDbgPrint("rp_udp_output len exceeded PACKET_SIZE: %d\n", len);
		return 0;
	}

	memcpy(dataBuf, buf, len);
	int packetLen = rpInitUDPPacket(KCP_PACKET_SIZE);
	nwmSendPacket(sendBuf, packetLen);

	return len;
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

static void rpControlRecvHandle(u8* buf UNUSED, int buf_size UNUSED) {
}

void rpControlRecv(void) {
	u8 *rpRecvBuffer = rp_storage_ctx->control_recv_buffer;
	int ret = recv(rp_recv_sock, rpRecvBuffer, RP_CONTROL_RECV_BUFFER_SIZE, 0);
	if (ret == 0) {
		nsDbgPrint("rpControlRecv nothing\n");
		return;
	} else if (ret < 0) {
		int err = SOC_GetErrno();
		nsDbgPrint("rpControlRecv failed: %d, errno = %d\n", ret, err);
		return;
	}

	if (!__atomic_load_n(&rp_kcp_ready, __ATOMIC_ACQUIRE)) {
		svc_sleepThread(10000000);
		return;
	}

	LightLock_Lock(&rp_kcp_mutex);
	if (rp_kcp) {
		int bufSize = ret;
		if ((ret = ikcp_input(rp_kcp, (const char *)rpRecvBuffer, bufSize)) < 0) {
			nsDbgPrint("ikcp_input failed: %d\n", ret);
		}

		ret = ikcp_recv(rp_kcp, (char *)rpRecvBuffer, RP_CONTROL_RECV_BUFFER_SIZE);
		if (ret >= 0) {
			rpControlRecvHandle(rpRecvBuffer, ret);
		}
	}
	LightLock_Unlock(&rp_kcp_mutex);
}

static u8 rp_atomic_incb(u8 *pos) {
	u8 val, val_new;
	do {
		val = __ldrexb(pos);
		val_new = val + 1;
	} while (__strexb(pos, val_new));
	return val;
}

static __attribute__((always_inline)) inline s32 rp_atomic_inc_mod(s32 *pos, int factor) {
	s32 val, val_new;
	do {
		val = __ldrex(pos);
		val_new = (val + 1) % factor;
	} while (__strex(pos, val_new));
	return val;
}

static s32 rp_mpsc_acquire(s32 *pos_arg, s32 *can_arg, int factor, s64 timeout) {
	s32 pos = *pos_arg;
	s32 *can = &can_arg[pos];
	while (1) {
		s32 val = *can;
		if (val >= 0) {
			*pos_arg = (pos + 1) % factor;
			*can = -1;
			__dmb();
			return val;
		} else {
			Result rc = syncArbitrateAddressWithTimeout(can, ARBITRATION_WAIT_IF_LESS_THAN, 0, timeout);
			if (R_DESCRIPTION(rc) == RD_TIMEOUT) {
				return -1;
			}
		}
	}
}

static void rp_mpsc_release(s32 *pos_arg, s32 *can_arg, int factor, int p_n, s32 val) {
	s32 pos = *pos_arg;
	*pos_arg = (pos + 1) % factor;
	s32 *can = &can_arg[pos];
	*can = val;
	__dmb();
	syncArbitrateAddress(can, ARBITRATION_SIGNAL, p_n);
}

static s32 rp_spmc_acquire(s32 *pos_arg, s32 *can_arg, LightLock *state_arg, int factor, s64 timeout) {
	while (1) {
		s32 pos = *pos_arg;
		s32 *can = &can_arg[pos];
		LightLock *state = &state_arg[pos];
		if (LightLock_TryLock(state)) {
			continue;
		}
		s32 val = *can;
		if (val >= 0) {
			rp_atomic_inc_mod(pos_arg, factor);
			*can = -1;
			LightLock_Unlock(state);
			return val;
		} else {
			LightLock_Unlock(state);
			Result rc = syncArbitrateAddressWithTimeout(can, ARBITRATION_WAIT_IF_LESS_THAN, 0, timeout);
			if (R_DESCRIPTION(rc) == RD_TIMEOUT) {
				return -1;
			}
		}
	}
}

static void rp_spmc_release(s32 *pos_arg, s32 *can_arg, int factor, s32 val) {
	s32 pos = rp_atomic_inc_mod(pos_arg, factor);
	s32 *can = &can_arg[pos];
	*can = val;
	__dmb();
	syncArbitrateAddress(can, ARBITRATION_SIGNAL, 1);
}

#define RP_DEFINE_QUEUE_FUNCTION( \
	mpsc_acq, mpsc_acq_pos, mpsc_rel, mpsc_rel_pos, spmc_acq, spmc_acq_pos, spmc_rel, spmc_rel_pos, \
	mpsc_can, spmc_can, spmc_state, buffer_count, thread_count, init, init_avail \
) \
static s32 mpsc_can[buffer_count]; \
static s32 spmc_can[buffer_count]; \
static s32 mpsc_acq_pos; \
static s32 mpsc_rel_pos; \
static s32 spmc_acq_pos; \
static s32 spmc_rel_pos; \
static LightLock spmc_state[buffer_count]; \
 \
static s32 mpsc_acq(s64 timeout) { \
	return rp_mpsc_acquire(&mpsc_acq_pos, mpsc_can, buffer_count, timeout); \
} \
 \
static void mpsc_rel(s32 val) { \
	rp_mpsc_release(&mpsc_rel_pos, spmc_can, buffer_count, thread_count, val); \
} \
 \
static s32 spmc_acq(s64 timeout) { \
	return rp_spmc_acquire(&spmc_acq_pos, spmc_can, spmc_state, buffer_count, timeout); \
} \
 \
static void spmc_rel(s32 val) { \
	rp_spmc_release(&spmc_rel_pos, mpsc_can, buffer_count, val); \
} \
 \
static void init(void) { \
	for (s32 i = 0; i < buffer_count; ++i) { \
		if (init_avail) { \
			mpsc_can[i] = i; spmc_can[i] = -1; \
		} else { \
			mpsc_can[i] = -1; spmc_can[i] = i; \
		} \
		LightLock_Init(&spmc_state[i]); \
	} \
	mpsc_acq_pos = mpsc_rel_pos = spmc_acq_pos = spmc_rel_pos = 0; \
}

RP_DEFINE_QUEUE_FUNCTION(
	rp_screen_acquire_transfer, rp_screen_transfer_acq_pos,
	rp_screen_release_process, rp_screen_process_rel_pos,
	rp_screen_acquire_process, rp_screen_process_acq_pos,
	rp_screen_release_transfer, rp_screen_transfer_rel_pos,
	rp_screen_can_transfer, rp_screen_can_process, rp_screen_process_acq_state,
	RP_SCREEN_BUFFER_COUNT, RP_ENCODE_THREAD_COUNT,
	rp_screen_queue_init, 0
)

RP_DEFINE_QUEUE_FUNCTION(
	rp_encode_acquire_send, rp_encode_send_acq_pos,
	rp_encode_release_process, rp_encode_process_rel_pos,
	rp_encode_acquire_process, rp_encode_process_acq_pos,
	rp_encode_release_send, rp_encode_send_rel_pos,
	rp_encode_can_send, rp_encode_can_process, rp_encode_process_acq_state,
	RP_ENCODE_BUFFER_COUNT, RP_ENCODE_THREAD_COUNT,
	rp_encode_queue_init, 1
)

#undef RP_DEFINE_QUEUE_FUNCTION

struct rp_send_header {
	u32 size;
	u8 frame_n;
	u8 top_bot;
};

static void rpNetworkTransfer(void) {
	int ret;

	// kcp init
	LightLock_Lock(&rp_kcp_mutex);
	rp_kcp = ikcp_create(KCP_MAGIC, 0);
	if (!rp_kcp) {
		nsDbgPrint("ikcp_create failed\n");
	} else {
		rp_kcp->output = rp_udp_output;
		if ((ret = ikcp_setmtu(rp_kcp, KCP_PACKET_SIZE)) < 0) {
			nsDbgPrint("ikcp_setmtu failed: %d\n", ret);
		}
		ikcp_nodelay(rp_kcp, 1, 10, 1, 1);
		rp_kcp->rx_minrto = 10;
		ikcp_wndsize(rp_kcp, KCP_SND_WND_SIZE, 0);
	}
	LightLock_Unlock(&rp_kcp_mutex);

	while (!__atomic_load_n(&exit_rp_network_thread, __ATOMIC_SEQ_CST) && !rp_reset_kcp) {
		LightLock_Lock(&rp_kcp_mutex);
		ikcp_update(rp_kcp, iclock());
		LightLock_Unlock(&rp_kcp_mutex);

		s32 pos = rp_encode_acquire_send(10000000);
		if (pos < 0) {
			continue;
		}

		struct rp_send_header header = {
			.size = rp_storage_ctx->jls_encode_size[pos],
			.frame_n = rp_storage_ctx->jls_encode_frame_n[pos],
			.top_bot = rp_storage_ctx->jls_encode_top_bot[pos]
		};

		u32 size_remain = header.size;
		u8 *data = rp_storage_ctx->jls_encode_buffer[pos];

		u64 last_tick = svc_getSystemTick();

		while (!__atomic_load_n(&exit_rp_network_thread, __ATOMIC_SEQ_CST)) {
			if (svc_getSystemTick() - last_tick > KCP_TIMEOUT_TICKS) {
				rp_reset_kcp = 1;
				break;
			}
			u32 data_size = RP_MIN(size_remain, RP_PACKET_SIZE - sizeof(header));

			// kcp send header data
			LightLock_Lock(&rp_kcp_mutex);
			int waitsnd = ikcp_waitsnd(rp_kcp);
			if (waitsnd < KCP_SND_WND_SIZE) {
				u8 *kcp_send_buffer = rp_storage_ctx->kcp_send_buffer;
				memcpy(kcp_send_buffer, &header, sizeof(header));
				memcpy(kcp_send_buffer + sizeof(header), data, data_size);

				ret = ikcp_send(rp_kcp, (const char *)kcp_send_buffer, data_size + sizeof(header));

				if (ret < 0) {
					nsDbgPrint("ikcp_send failed: %d\n", ret);

					__atomic_store_n(&exit_rp_thread, 1, __ATOMIC_SEQ_CST);
					LightLock_Unlock(&rp_kcp_mutex);
					break;
				}

				size_remain -= data_size;
				data += data_size;

				LightLock_Unlock(&rp_kcp_mutex);
				break;
			}
			LightLock_Unlock(&rp_kcp_mutex);
		}

		last_tick = svc_getSystemTick();

		while (!__atomic_load_n(&exit_rp_network_thread, __ATOMIC_SEQ_CST) &&
			!rp_reset_kcp &&
			size_remain &&
			svc_getSystemTick() - last_tick <= KCP_TIMEOUT_TICKS
		) {
			u32 data_size = RP_MIN(size_remain, RP_PACKET_SIZE);

			// kcp send data
			LightLock_Lock(&rp_kcp_mutex);
			int waitsnd = ikcp_waitsnd(rp_kcp);
			if (waitsnd < KCP_SND_WND_SIZE) {
				ret = ikcp_send(rp_kcp, (const char *)data, data_size);

				if (ret < 0) {
					nsDbgPrint("ikcp_send failed: %d\n", ret);

					__atomic_store_n(&exit_rp_thread, 1, __ATOMIC_SEQ_CST);
					LightLock_Unlock(&rp_kcp_mutex);
					break;
				}

				size_remain -= data_size;
				data += data_size;
			}
			LightLock_Unlock(&rp_kcp_mutex);
		}

		rp_encode_release_process(pos);
	}

	rp_reset_kcp = 0;

	// kcp deinit
	LightLock_Lock(&rp_kcp_mutex);
	ikcp_release(rp_kcp);
	rp_kcp = 0;
	LightLock_Unlock(&rp_kcp_mutex);
}

static void rpNetworkTransferThread(u32 arg UNUSED) {
	while (!__atomic_load_n(&exit_rp_network_thread, __ATOMIC_SEQ_CST)) {
		rpNetworkTransfer();
	}
	svc_exitThread();
}

static Handle rpHDma[2], rpHandleHome, rpHandleGame;
static u32 rpGameFCRAMBase = 0;

static void rpInitDmaHome(void) {
	// u32 rp_dma_config[20] = { 0 };
	svc_openProcess(&rpHandleHome, 0xf);
}

static void rpCloseGameHandle(void) {
	if (rpHandleGame) {
		svc_closeHandle(rpHandleGame);
		rpHandleGame = 0;
		rpGameFCRAMBase = 0;
	}
}

static Handle rpGetGameHandle(void) {
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

static u8 rp_dma_config[80] = { 0, 0, 4 };
static struct {
	u32 format;
	u32 pitch;
	u32 fbaddr;
} rp_screen_ctx[2];

static int rpCaptureScreen(int screen_buffer_n, int top_bot) {
	u32 bufSize = rp_screen_ctx[top_bot].pitch * (top_bot == 0 ? 400 : 320);
	if (bufSize > RP_SCREEN_BUFFER_SIZE) {
		nsDbgPrint("rpCaptureScreen bufSize too large: %x > %x\n", bufSize, RP_SCREEN_BUFFER_SIZE);
		return -1;
	}

	u32 phys = rp_screen_ctx[top_bot].fbaddr;
	u8 *dest = rp_storage_ctx->screen_buffer[screen_buffer_n];
	Handle hProcess = rpHandleHome;

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)dest, bufSize);
	svc_closeHandle(rpHDma[top_bot]);
	rpHDma[top_bot] = 0;

	int ret;
	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		ret = svc_startInterProcessDma(&rpHDma[top_bot], CURRENT_PROCESS_HANDLE,
			dest, hProcess, (const void *)(0x1F000000 + (phys - 0x18000000)), bufSize, (u32 *)rp_dma_config);
		return ret;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			ret = svc_startInterProcessDma(&rpHDma[top_bot], CURRENT_PROCESS_HANDLE,
				dest, hProcess, (const void *)(rpGameFCRAMBase + (phys - 0x20000000)), bufSize, (u32 *)rp_dma_config);
			return ret;
		}
		return 0;
	}
	svc_sleepThread(1000000000);

	return 0;
}

static void jls_encoder_prepare_LUTs(void) {
	prepare_classmap(rp_storage_ctx->jls_enc_luts.classmap);
	struct jls_enc_params *p;

#define RP_JLS_INIT_LUT(bpp, bpp_index, bpp_lut_name) do { \
	p = &rp_storage_ctx->jls_enc_params[bpp_index]; \
	jpeg_ls_init(p, bpp, (const word (*)[3])rp_storage_ctx->jls_enc_luts.bpp_lut_name); \
	prepare_vLUT(rp_storage_ctx->jls_enc_luts.bpp_lut_name, p->alpha, p->T1, p->T2, p->T3); } while (0) \

	RP_JLS_INIT_LUT(8, RP_ENCODE_PARAMS_BPP8, vLUT_bpp8);
	RP_JLS_INIT_LUT(5, RP_ENCODE_PARAMS_BPP5, vLUT_bpp5);
	RP_JLS_INIT_LUT(6, RP_ENCODE_PARAMS_BPP6, vLUT_bpp6);

#undef RP_JLS_INIT_LUT
}

extern const uint8_t psl0[];
static int rpJLSEncodeImage(int thread_n, int encode_buffer_n, const u8 *src, int w, int h, int bpp) {
	u8 *dst = rp_storage_ctx->jls_encode_buffer[encode_buffer_n];
	struct jls_enc_params *params;
	switch (bpp) {
		case 8:
			params = &rp_storage_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP8]; break;

		case 5:
			params = &rp_storage_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP5]; break;

		case 6:
			params = &rp_storage_ctx->jls_enc_params[RP_ENCODE_PARAMS_BPP6]; break;

		default:
			nsDbgPrint("Unsupported bpp in rpJLSEncodeImage: %d\n", bpp);
			return -1;
	}

	if (rp_config.encoder_which == 0) {
		JLSState state = { 0 };
		state.bpp = bpp;

		ff_jpegls_reset_coding_parameters(&state, 0);
		ff_jpegls_init_state(&state);

		PutBitContext s;
		init_put_bits(&s, dst, RP_JLS_ENCODE_BUFFER_SIZE);

		const u8 *last = psl0 + LEFTMARGIN;
		const u8 *in = src + LEFTMARGIN;

		for (int i = 0; i < w; ++i) {
			ls_encode_line(
				&state, &s, last, in, h,
				(const uint16_t (*)[3])params->vLUT,
				rp_storage_ctx->jls_enc_luts.classmap
			);
			last = in;
			in += h + LEFTMARGIN + RIGHTMARGIN;
		}

		put_bits(&s, 7, 0);
		// int size_in_bits = put_bits_count(&s);
		flush_put_bits(&s);
		int size = put_bytes_output(&s);

		return size;
	} else {
		struct jls_enc_ctx *ctx = &rp_storage_ctx->jls_enc_ctx[thread_n];
		struct bito_ctx *bctx = &rp_storage_ctx->jls_bito_ctx[thread_n];
		int ret = jpeg_ls_encode(
			params, ctx, bctx, (char *)dst, src, h, w, h + LEFTMARGIN + RIGHTMARGIN, bpp,
			rp_storage_ctx->jls_enc_luts.classmap
		);
		if (ret > RP_JLS_ENCODE_BUFFER_SIZE) {
			nsDbgPrint("Buffer overrun in rpJLSEncodeImage\n");
			return -1; // if we didn't crash, fail because buffer overflow
		}
	}
	return 0;
}

#define rshift_to_even(n, s) (((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))
#define srshift_to_even(n, s) ((s16)((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))

static __attribute__((always_inline)) inline
void convert_yuv_hp(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	int bpp
) {
	u8 half_range = 1 << (bpp - 1);
	switch (rp_config.color_transform_hp) {
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

			*y_out = g + ((u + v) >> 2) - quarter_range;
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

static __attribute__((always_inline)) inline
void convert_yuv_hp_2(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	int bpp
) {
	u8 half_range = 1 << (bpp - 1);
	u8 half_g = g >> 1;
	switch (rp_config.color_transform_hp) {
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

static __attribute__((always_inline)) inline
void convert_yuv(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	u8 bpp, u8 bpp_2
) {
	switch (rp_config.yuv_option) {
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
			*u_out = srshift_to_even(u, 8 + spp) + (128 >> spp);
			*v_out = srshift_to_even(v, 8 + spp) + (128 >> spp);
			break;
		}

		case 3: {
			RP_RGB_SHIFT
			u16 y = 66 * (u16)r + 129 * (u16)g + 25 * (u16)b;
			s16 u = -38 * (s16)r + -74 * (s16)g + 112 * (s16)b;
			s16 v = 112 * (s16)r + -94 * (s16)g + -18 * (s16)b;
			*y_out = rshift_to_even(y, 8 + spp_2) + (16 >> spp_2);
			*u_out = srshift_to_even(u, 8) + (128 >> spp);
			*v_out = srshift_to_even(v, 8) + (128 >> spp);
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

static __attribute__((always_inline)) inline
void convert_set_zero(u8 *restrict *restrict dp_y_out, int count) {
	for (int i = 0; i < count; ++i) {
		*(*dp_y_out)++ = 0;
		++*dp_y_out;
	}
}

static __attribute__((always_inline)) inline
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

static __attribute__((always_inline)) inline
void convert_set_last(u8 *restrict *restrict dp_y_out, int count) {
	for (int i = 0; i < count; ++i) {
		**dp_y_out = *(*dp_y_out - 1);
		++*dp_y_out;
	}
}

static __attribute__((always_inline)) inline
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

static int convert_yuv_image(
	int format, int width, int height, int bytes_per_pixel, int bytes_to_next_column,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp
) {
	ASSUME_ALIGN_4(sp);
	ASSUME_ALIGN_4(dp_y_out);
	ASSUME_ALIGN_4(dp_u_out);
	ASSUME_ALIGN_4(dp_v_out);

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
				convert_set_3_zero(&dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
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
				convert_set_3_zero(&dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
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
				convert_set_3_zero(&dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
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
				convert_set_3_zero(&dp_y_out, &dp_u_out, &dp_v_out, LEFTMARGIN);
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

static void downscale_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig) {
	ASSUME_ALIGN_4(ds_dst);
	ASSUME_ALIGN_4(src);

	src += LEFTMARGIN;
	int pitch = hOrig + LEFTMARGIN + RIGHTMARGIN;
	const u8 *src_end = src + wOrig * pitch;
	const u8 *src_col0 = src;
	const u8 *src_col1 = src + pitch;
	while (src_col0 < src_end) {
		convert_set_zero(&ds_dst, LEFTMARGIN);
		const u8 *src_col0_end = src_col0 + hOrig;
		while (src_col0 < src_col0_end) {
			u16 p = *src_col0++;
			p += *src_col0++;
			p += *src_col1++;
			p += *src_col1++;

			*ds_dst++ = rshift_to_even(p, 2);
		}
		src_col0 += LEFTMARGIN + RIGHTMARGIN + pitch;
		src_col1 += LEFTMARGIN + RIGHTMARGIN + pitch;
		convert_set_last(&ds_dst, RIGHTMARGIN);
	}
}

static int rpEncodeImage(int screen_buffer_n, int image_buffer_n, int top_bot) {
	int width, height;
	if (top_bot == 0) {
		width = 400;
	} else {
		width = 320;
	}
	height = 240;

	int format = rp_screen_ctx[top_bot].format;
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
	int pitch = rp_screen_ctx[top_bot].pitch;
	int bytes_to_next_column = pitch - bytes_per_column;

	u8 *y_image;
	u8 *u_image;
	u8 *v_image;
	u8 *ds_u_image;
	u8 *ds_v_image;
	u8 *y_bpp;
	u8 *u_bpp;
	u8 *v_bpp;

#define RP_IMAGE_SET(top_bot_image) do { \
	y_image = rp_storage_ctx->top_bot_image[image_buffer_n].y_image; \
	u_image = rp_storage_ctx->top_bot_image[image_buffer_n].u_image; \
	v_image = rp_storage_ctx->top_bot_image[image_buffer_n].v_image; \
	ds_u_image = rp_storage_ctx->top_bot_image[image_buffer_n].ds_u_image; \
	ds_v_image = rp_storage_ctx->top_bot_image[image_buffer_n].ds_v_image; \
	y_bpp = &rp_storage_ctx->top_bot_image[image_buffer_n].y_bpp; \
	u_bpp = &rp_storage_ctx->top_bot_image[image_buffer_n].u_bpp; \
	v_bpp = &rp_storage_ctx->top_bot_image[image_buffer_n].v_bpp; } while (0)

	if (top_bot == 0) {
		RP_IMAGE_SET(top_image);
	} else {
		RP_IMAGE_SET(bot_image);
	}

#undef RP_IMAGE_SET

	int ret = convert_yuv_image(
		format, width, height, bytes_per_pixel, bytes_to_next_column,
		rp_storage_ctx->screen_buffer[screen_buffer_n],
		y_image, u_image, v_image,
		y_bpp, u_bpp, v_bpp
	);

	if (ret < 0)
		return ret;

	if (rp_config.downscale_uv) {
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

	return 0;
}

static void rpEncodeScreenAndSend(int thread_n) {
	int ret;
	while (!__atomic_load_n(&exit_rp_thread, __ATOMIC_SEQ_CST)) {
		s32 pos = rp_screen_acquire_process(250000000);
		if (pos < 0) {
			continue;
		}

		int top_bot = rp_storage_ctx->screen_top_bot[pos];

		int screen_buffer_n = pos;
		int image_buffer_n = top_bot == 0 ?
			rp_atomic_inc_mod(&rp_top_image_n, RP_IMAGE_BUFFER_COUNT) :
			rp_atomic_inc_mod(&rp_bot_image_n, RP_IMAGE_BUFFER_COUNT);
		ret = rpEncodeImage(screen_buffer_n, image_buffer_n, top_bot);
		if (ret < 0) {
			nsDbgPrint("rpEncodeImage failed");
			__atomic_store_n(&exit_rp_thread, 1, __ATOMIC_SEQ_CST);
			break;
		}

		rp_screen_release_transfer(pos);

		int frame_n = top_bot == 0 ?
			rp_atomic_incb(&rp_top_image_send_n) :
			rp_atomic_incb(&rp_bot_image_send_n);

#define RP_PROCESS_IMAGE_AND_SEND(n, wt, wb, h, b) while (!__atomic_load_n(&exit_rp_thread, __ATOMIC_SEQ_CST)) { \
	pos = rp_encode_acquire_process(250000000); \
	if (pos < 0) { \
		continue; \
	} \
	int encode_buffer_n = pos; \
	ret = rpJLSEncodeImage(thread_n, encode_buffer_n, \
		top_bot == 0 ? \
			rp_storage_ctx->top_image[image_buffer_n].n : \
			rp_storage_ctx->bot_image[image_buffer_n].n, \
		top_bot == 0 ? wt : wb, \
		h, \
		top_bot == 0 ? \
			rp_storage_ctx->top_image[image_buffer_n].b : \
			rp_storage_ctx->bot_image[image_buffer_n].b \
	); \
	if (ret < 0) { \
		nsDbgPrint("rpJLSEncodeImage failed"); \
		__atomic_store_n(&exit_rp_thread, 1, __ATOMIC_SEQ_CST); \
		break; \
	} \
	rp_storage_ctx->jls_encode_top_bot[pos] = top_bot; \
	rp_storage_ctx->jls_encode_frame_n[pos] = frame_n; \
	rp_storage_ctx->jls_encode_size[pos] = ret; \
	rp_encode_release_send(pos);

#define RP_PROCESS_IMAGE_AND_SEND_END break; }

		if (rp_config.downscale_uv)
		{
			// y_image
			RP_PROCESS_IMAGE_AND_SEND(y_image, 400, 320, 240, y_bpp);
			// ds_u_image
				RP_PROCESS_IMAGE_AND_SEND(ds_u_image, 200, 160, 120, u_bpp);
				// ds_v_image
					RP_PROCESS_IMAGE_AND_SEND(ds_v_image, 200, 160, 120, v_bpp);

					RP_PROCESS_IMAGE_AND_SEND_END

				RP_PROCESS_IMAGE_AND_SEND_END

			RP_PROCESS_IMAGE_AND_SEND_END
		} else {
			// y_image
			RP_PROCESS_IMAGE_AND_SEND(y_image, 400, 320, 240, y_bpp);
			// u_image
				RP_PROCESS_IMAGE_AND_SEND(u_image, 400, 320, 240, u_bpp);
				// v_image
					RP_PROCESS_IMAGE_AND_SEND(v_image, 400, 320, 240, v_bpp);

					RP_PROCESS_IMAGE_AND_SEND_END

				RP_PROCESS_IMAGE_AND_SEND_END

			RP_PROCESS_IMAGE_AND_SEND_END
		}

#undef RP_PROCESS_IMAGE_AND_SEND
#undef RP_PROCESS_IMAGE_AND_SEND_END
	}
}

static void rpSecondThreadStart(u32 arg UNUSED) {
	rpEncodeScreenAndSend(1);
	svc_exitThread();
}

void rpKernelCallback(int top_bot);
static void rpScreenTransferThread(u32 arg UNUSED) {
	int top_bot = 0;
	int ret;

	while (!__atomic_load_n(&exit_rp_thread, __ATOMIC_SEQ_CST)) {
		s32 pos = rp_screen_acquire_transfer(250000000);
		if (pos < 0) {
			continue;
		}

		while (!__atomic_load_n(&exit_rp_thread, __ATOMIC_SEQ_CST)) {
			rpKernelCallback(top_bot);

			ret = rpCaptureScreen(pos, top_bot);

			if (ret < 0) {
				svc_sleepThread(1000000000);
				continue;
			}

			rp_storage_ctx->screen_top_bot[pos] = top_bot;
			rp_screen_release_process(pos);
			top_bot = !top_bot;
			break;
		}
	}

	svc_exitThread();
}

static int rpSendFrames(void) {
	int ret;

	__atomic_store_n(&exit_rp_thread, 0, __ATOMIC_SEQ_CST);
	rp_screen_queue_init();
	ret = svc_createThread(
		&rp_second_thread,
		rpSecondThreadStart,
		0,
		(u32 *)&rp_storage_ctx->second_thread_stack[RP_STACK_SIZE - 40],
		0x10,
		3);
	if (ret != 0) {
		nsDbgPrint("Create rpSecondThreadStart Thread Failed: %08x\n", ret);
		return -1;
	}
	ret = svc_createThread(
		&rp_screen_thread,
		rpScreenTransferThread,
		0,
		(u32 *)&rp_storage_ctx->screen_transfer_thread_stack[RP_MISC_STACK_SIZE - 40],
		0x8,
		2);
	if (ret != 0) {
		nsDbgPrint("Create rpScreenTransferThread Thread Failed: %08x\n", ret);

		__atomic_store_n(&exit_rp_thread, 1, __ATOMIC_SEQ_CST);
		svc_waitSynchronization1(rp_second_thread, U64_MAX);
		svc_closeHandle(rp_second_thread);
		return -1;
	}

	rpEncodeScreenAndSend(0);

	svc_waitSynchronization1(rp_second_thread, U64_MAX);
	svc_waitSynchronization1(rp_screen_thread, U64_MAX);
	svc_closeHandle(rp_second_thread);
	svc_closeHandle(rp_screen_thread);

	return ret;
}

static void rp_set_params() {
	rp_config.arg0 = g_nsConfig->startupInfo[8];
	rp_config.arg1 = g_nsConfig->startupInfo[9];
	rp_config.arg2 = g_nsConfig->startupInfo[10];

	rp_config.downscale_uv = rp_config.arg0 & 0x1;
	rp_config.encoder_which = rp_config.arg0 & 0x2 >> 1;
	rp_config.yuv_option = rp_config.arg1 & 0x3;
	rp_config.color_transform_hp = rp_config.arg1 & 0xc >> 2;
}

static void rpThreadStart(u32 arg UNUSED) {
	rp_set_params();
	jls_encoder_prepare_LUTs();
	rpInitDmaHome();
	// kRemotePlayCallback();

	LightLock_Init(&rp_kcp_mutex);
	__atomic_store_n(&rp_kcp_ready, 1, __ATOMIC_RELEASE);

	int ret = 0;
	while (ret >= 0) {
		__atomic_store_n(&exit_rp_network_thread, 0, __ATOMIC_SEQ_CST);
		rp_encode_queue_init();
		ret = svc_createThread(
			&rp_network_thread,
			rpNetworkTransferThread,
			0,
			(u32 *)&rp_storage_ctx->network_transfer_thread_stack[RP_MISC_STACK_SIZE - 40],
			0x8,
			3);
		if (ret != 0) {
			nsDbgPrint("Create rpNetworkTransferThread Failed: %08x\n", ret);
			goto final;
		}

		ret = rpSendFrames();

		__atomic_store_n(&exit_rp_network_thread, 1, __ATOMIC_SEQ_CST);
		svc_waitSynchronization1(rp_network_thread, U64_MAX);
		svc_closeHandle(rp_network_thread);

		svc_sleepThread(250000000);
	}

final:
	rpInited = 0;
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
			rp_storage_ctx = (typeof(rp_storage_ctx))plgRequestMemory(sizeof(*rp_storage_ctx));
			if (!rp_storage_ctx) {
				nsDbgPrint("Request memory for RemotePlay failed\n");
				return 0;
			}
			nsDbgPrint("RemotePlay memory: 0x%08x (0x%x bytes)\n", rp_storage_ctx, sizeof(*rp_storage_ctx));

			rpInited = 1;
			memcpy(rp_storage_ctx->nwm_send_buffer, buf, 0x22 + 8);

			umm_init_heap(rp_storage_ctx->umm_heap, RP_UMM_HEAP_SIZE);
			ikcp_allocator(umm_malloc, umm_free);

			ret = svc_createThread(&hThread, rpThreadStart, 0, (u32 *)&rp_storage_ctx->thread_stack[RP_STACK_SIZE - 40], 0x10, 2);
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
		rp_screen_ctx[0].format = REG(IoBasePdc + 0x470);
		rp_screen_ctx[0].pitch = REG(IoBasePdc + 0x490);

		current_fb = REG(IoBasePdc + 0x478);
		current_fb &= 1;

		rp_screen_ctx[0].fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x468) :
			REG(IoBasePdc + 0x46c);
	} else {
		rp_screen_ctx[1].format = REG(IoBasePdc + 0x570);
		rp_screen_ctx[1].pitch = REG(IoBasePdc + 0x590);

		current_fb = REG(IoBasePdc + 0x578);
		current_fb &= 1;

		rp_screen_ctx[1].fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x568) :
			REG(IoBasePdc + 0x56c);
	}
}
