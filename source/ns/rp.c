#include "global.h"

#include "umm_malloc.h"
#include "ikcp.h"
#include "libavcodec/jpegls.h"

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC (268123480)

typedef u32(*sendPacketTypedef) (u8*, u32);
static sendPacketTypedef nwmSendPacket = 0;
static RT_HOOK nwmValParamHook;

int rp_recv_sock = -1;

static u8 rpInited = 0;

static ikcpcb *rp_kcp;
static Handle rp_kcp_mutex;
static u8 rp_control_ready = 0;

static u8 exit_rp_thread = 0;
static u8 exit_rp_network_thread = 0;
static u8 reset_kcp = 0;
static Handle rp_second_thread;
static Handle rp_screen_thread;
static Handle rp_network_thread;

#define KCP_PACKET_SIZE 1448
#define NWM_HEADER_SIZE (0x2a + 8)
#define NWM_PACKET_SIZE (KCP_PACKET_SIZE + NWM_HEADER_SIZE)

#define KCP_MAGIC 0x12345fff
#define KCP_SOCKET_TIMEOUT 10
#define KCP_TIMEOUT_TICKS (250 * SYSTICK_PER_MS)
#define RP_PACKET_SIZE (KCP_PACKET_SIZE - IKCP_OVERHEAD)
#define KCP_SND_WND_SIZE 40

#define RP_DEST_PORT RP_PORT
#define RP_SCREEN_BUFFER_SIZE (400 * 240 * 4)
#define RP_UMM_HEAP_SIZE (256 * 1024)
#define RP_STACK_SIZE (0x10000)
#define RP_MISC_STACK_SIZE (0x1000)
#define RP_CONTROL_RECV_BUFFER_SIZE (2000)
#define RP_JLS_ENCODE_BUFFER_SIZE (400 * 240)

// attribute aligned
#define ALIGN_4 __attribute__ ((aligned (4)))
// assume aligned
#define ASSUME_ALIGN_4(a) (a = __builtin_assume_aligned (a, 4))

#define RP_ENCODE_THREAD_COUNT (2)
#define RP_ENCODE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 1)
#define RP_SCREEN_BUFFER_COUNT RP_ENCODE_BUFFER_COUNT
#define RP_IMAGE_BUFFER_COUNT (2)
static struct {
	u8 nwm_send_buffer[NWM_PACKET_SIZE] ALIGN_4;
	u8 kcp_send_buffer[KCP_PACKET_SIZE] ALIGN_4;
	u8 thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 second_thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 network_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 screen_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 control_recv_buffer[RP_CONTROL_RECV_BUFFER_SIZE] ALIGN_4;
	u8 umm_heap[RP_UMM_HEAP_SIZE] ALIGN_4;

	u8 screen_buffer[RP_SCREEN_BUFFER_COUNT][RP_SCREEN_BUFFER_SIZE];
	u8 jls_last_col_buffer[RP_ENCODE_THREAD_COUNT][240] ALIGN_4;
	u8 jls_encode_buffer[RP_ENCODE_BUFFER_COUNT][RP_JLS_ENCODE_BUFFER_SIZE] ALIGN_4;

	struct {
		u8 y_image[400 * 240] ALIGN_4;
		u8 u_image[400 * 240] ALIGN_4;
		u8 v_image[400 * 240] ALIGN_4;
		u8 ds_u_image[200 * 120] ALIGN_4;
		u8 ds_v_image[200 * 120] ALIGN_4;
	} top_image[RP_IMAGE_BUFFER_COUNT];

	struct {
		u8 y_image[320 * 240] ALIGN_4;
		u8 u_image[320 * 240] ALIGN_4;
		u8 v_image[320 * 240] ALIGN_4;
		u8 ds_u_image[160 * 120] ALIGN_4;
		u8 ds_v_image[160 * 120] ALIGN_4;
	} bot_image[RP_IMAGE_BUFFER_COUNT];
} *rp_storage_ctx;

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

static int rp_udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
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

static void rpControlRecvHandle(u8* buf, int buf_size) {
}

void rpControlRecv(void) {
	if (!rp_control_ready) {
		svc_sleepThread(100000000);
		return;
	}

	u8 *rpRecvBuffer = rp_storage_ctx->control_recv_buffer;
	int ret = recv(rp_recv_sock, rpRecvBuffer, RP_CONTROL_RECV_BUFFER_SIZE, 0);
	if (ret == 0) {
		nsDbgPrint("rpControlRecv nothing\n");
		return;
	} else if (ret < 0) {
		int err = SOC_GetErrno();
		nsDbgPrint("rpControlRecv failed: %d\n", ret);
		return;
	}

	svc_waitSynchronization1(rp_kcp_mutex, U64_MAX);
	if (rp_kcp) {
		int bufSize = ret;
		if ((ret = ikcp_input(rp_kcp, rpRecvBuffer, bufSize)) < 0) {
			nsDbgPrint("ikcp_input failed: %d\n", ret);
		}

		ret = ikcp_recv(rp_kcp, rpRecvBuffer, RP_CONTROL_RECV_BUFFER_SIZE);
		if (ret >= 0) {
			rpControlRecvHandle(rpRecvBuffer, ret);
		}
	}
	svc_releaseMutex(rp_kcp_mutex);
}

static void rpNetworkTransfer(void) {
	int ret;

	// kcp init
	svc_waitSynchronization1(rp_kcp_mutex, U64_MAX);
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
	svc_releaseMutex(rp_kcp_mutex);

	while (!exit_rp_network_thread && !reset_kcp) {
		// kcp send
		svc_waitSynchronization1(rp_kcp_mutex, U64_MAX);
		int waitsnd = ikcp_waitsnd(rp_kcp);
		if (waitsnd < KCP_SND_WND_SIZE) {
			u8 *kcp_send_buf = rp_storage_ctx->kcp_send_buffer;
			kcp_send_buf[0] = 0;
			ret = ikcp_send(rp_kcp, kcp_send_buf, 1);

			if (ret < 0) {
				nsDbgPrint("ikcp_send failed: %d\n", ret);
				break;
			}
		}
		ikcp_update(rp_kcp, iclock());
		svc_releaseMutex(rp_kcp_mutex);

		svc_sleepThread(100000000);
	}

	// kcp deinit
	svc_waitSynchronization1(rp_kcp_mutex, U64_MAX);
	ikcp_release(rp_kcp);
	rp_kcp = 0;
	svc_releaseMutex(rp_kcp_mutex);
}

static void rpNetworkTransferThread(u32 arg) {
	while (!exit_rp_network_thread) {
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
static int rp_next_screen_transfer;

static int rpCaptureScreen(int screen_buffer_n, int top_bot) {
	u32 bufSize = rp_screen_ctx[top_bot].pitch * (top_bot == 0 ? 400 : 320);
	if (bufSize > RP_SCREEN_BUFFER_SIZE) {
		nsDbgPrint("rpCaptureScreen bufSize too large: %x > %x\n", bufSize, RP_SCREEN_BUFFER_SIZE);
		return -1;
	}

	u32 phys = rp_screen_ctx[top_bot].fbaddr;
	u8 *dest = rp_storage_ctx->screen_buffer[screen_buffer_n];
	Handle hProcess = rpHandleHome;

	int ret;

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)dest, bufSize);
	svc_closeHandle(rpHDma[top_bot]);
	rpHDma[top_bot] = 0;

	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		svc_startInterProcessDma(&rpHDma[top_bot], CURRENT_PROCESS_HANDLE,
			dest, hProcess, (const void *)(0x1F000000 + (phys - 0x18000000)), bufSize, (u32 *)rp_dma_config);
		return 0;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			ret = svc_startInterProcessDma(&rpHDma[top_bot], CURRENT_PROCESS_HANDLE,
				dest, hProcess, (const void *)(rpGameFCRAMBase + (phys - 0x20000000)), bufSize, (u32 *)rp_dma_config);

		}
		return 0;
	}
	svc_sleepThread(1000000000);

	return 01;
}

static int rpJLSEncodeImage(int thread_n, int encode_buffer_n, const u8 *src, int w, int h) {
	JLSState state = { 0 };
	state.bpp = 8;

	ff_jpegls_reset_coding_parameters(&state, 0);
	ff_jpegls_init_state(&state);

	PutBitContext s;
    init_put_bits(&s, rp_storage_ctx->jls_encode_buffer[encode_buffer_n], RP_JLS_ENCODE_BUFFER_SIZE);

	u8 *last = rp_storage_ctx->jls_last_col_buffer[thread_n];
	memset(last, 0, h);

	const u8 *in = src;
	int t = 0;

	for (int i = 0; i < w; ++i) {
        int last0 = last[0];
        ls_encode_line(&state, &s, last, in, t, h, 1, 0, 8);
        t = last0;
        in += h;
    }

	put_bits(&s, 7, 0);
    // int size_in_bits = put_bits_count(&s);
    flush_put_bits(&s);
    int size = put_bytes_output(&s);

	return size;
}

#define rshift_to_even(n, s) ((n + (s > 1 ? (1 << (s - 1)) : 0)) >> s)
#define srshift_to_even(n, s) ((s16)(n + (s > 1 ? (1 << (s - 1)) : 0)) >> s)

static void downscale_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig) {
	const u8 *src_end = src + wOrig * hOrig;
	const u8 *src_col0 = src;
	const u8 *src_col1 = src + hOrig;
	while (src_col0 < src_end) {
		const u8 *src_col_end = src_col1;
		while (src_col0 < src_col_end) {
			u16 p = *src_col0++;
			p += *src_col0++;
			p += *src_col1++;
			p += *src_col1++;

			*ds_dst++ = rshift_to_even(p, 2);
		}
		src_col0 += hOrig;
		src_col1 += hOrig;
	}
}

static __attribute__((always_inline)) inline
void convert_yuv(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out) {
	u16 y = 77 * (u16)r + 150 * (u16)g + 29 * (u16)b;
	s16 u = -43 * (s16)r + -84 * (s16)g + 127 * (s16)b;
	s16 v = 127 * (s16)r + -106 * (s16)g + -21 * (s16)b;
	*y_out = rshift_to_even(y, 8);
	*u_out = srshift_to_even(u, 8) + 128;
	*v_out = srshift_to_even(v, 8) + 128;
}

static int convert_yuv_image(
	int format, int width, int height, int bytes_per_pixel, int bytes_to_next_column,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out
) {
	int x, y;
	u8 r, g, b, y_out, u_out, v_out;

	switch (format) {
		// untested
		case 0:
			if (0)
				++sp;
			else
				return -1;

			// fallthru
		case 1: {
			for (x = 0; x < width; ++x) {
				for (y = 0; y < height; ++y) {
					convert_yuv(
						sp[2],
						sp[1],
						sp[0],
						dp_y_out++,
						dp_u_out++,
						dp_v_out++);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			break;
		}

		case 2: {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f,
						(pix >> 5) & 0x3f,
						pix & 0x1f,
						dp_y_out++,
						dp_u_out++,
						dp_v_out++);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			break;
		}

		// untested
		case 3:
		if (0) {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					r = (pix >> 11) & 0x1f;
					g = (pix >> 6) & 0x1f;
					b = (pix >> 1) & 0x1f;
					convert_yuv(r, g, b, &y_out, &u_out, &v_out);
					*dp_y_out++ = y_out;
					*dp_u_out++ = u_out;
					*dp_v_out++ = v_out;
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			break;
		}

		// untested
		case 4:
		if (0) {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					r = (pix >> 12) & 0x0f;
					g = (pix >> 8) & 0x0f;
					b = (pix >> 4) & 0x0f;
					convert_yuv(r, g, b, &y_out, &u_out, &v_out);
					*dp_y_out++ = y_out;
					*dp_u_out++ = u_out;
					*dp_v_out++ = v_out;
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			break;
		}

		default:
			return -1;
	}
	return 0;
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
	if (top_bot == 0) {
		y_image = rp_storage_ctx->top_image[image_buffer_n].y_image;
		u_image = rp_storage_ctx->top_image[image_buffer_n].u_image;
		v_image = rp_storage_ctx->top_image[image_buffer_n].v_image;
		ds_u_image = rp_storage_ctx->top_image[image_buffer_n].ds_u_image;
		ds_v_image = rp_storage_ctx->top_image[image_buffer_n].ds_v_image;
	} else {
		y_image = rp_storage_ctx->bot_image[image_buffer_n].y_image;
		u_image = rp_storage_ctx->bot_image[image_buffer_n].u_image;
		v_image = rp_storage_ctx->bot_image[image_buffer_n].v_image;
		ds_u_image = rp_storage_ctx->bot_image[image_buffer_n].ds_u_image;
		ds_v_image = rp_storage_ctx->bot_image[image_buffer_n].ds_v_image;
	}

	int ret = convert_yuv_image(
		format, width, height, bytes_per_pixel, bytes_to_next_column,
		rp_storage_ctx->screen_buffer[screen_buffer_n],
		y_image,
		u_image,
		v_image
	);

	if (ret < 0)
		return ret;

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

	return 0;
}

static void rpSecondThreadStart(u32 arg) {
	while (!exit_rp_thread) {
		svc_sleepThread(1000000000);
	}

	svc_exitThread();
}

static void rpScreenTransferThread(u32 arg) {
	u32 current_screen = 0;
	int ret;

	while (!exit_rp_thread) {
		svc_sleepThread(1000000000);
	}

	svc_exitThread();
}

void rpKernelCallback(int top_bot);
static int rpSendFrames(void) {
	int top_bot = 0, ret;
	int thread_n = 0;

	exit_rp_thread = 0;
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

		exit_rp_thread = 1;
		svc_waitSynchronization1(rp_second_thread, U64_MAX);
		svc_closeHandle(rp_second_thread);
		return -1;
	}

	rp_next_screen_transfer = 0;
	while (1) {
		rpKernelCallback(top_bot);

		ret = rpCaptureScreen(0, top_bot);
		if (ret < 0)
			break;

		int image_buffer_n = 0, screen_buffer_n = 0, encode_buffer_n = 0;

		ret = rpEncodeImage(screen_buffer_n, image_buffer_n, top_bot);
		if (ret < 0)
			break;

		ret = rpJLSEncodeImage(thread_n, encode_buffer_n,
			top_bot == 0 ?
				rp_storage_ctx->top_image[image_buffer_n].y_image :
				rp_storage_ctx->bot_image[image_buffer_n].y_image,
			top_bot == 0 ? 400 : 320,
			240
		);
		if (ret < 0)
			break;

		top_bot = !top_bot;

		svc_sleepThread(1000000000);
	}

	exit_rp_thread = 1;
	svc_waitSynchronization1(rp_second_thread, U64_MAX);
	svc_waitSynchronization1(rp_screen_thread, U64_MAX);
	svc_closeHandle(rp_second_thread);
	svc_closeHandle(rp_screen_thread);

	return ret;
}

static void rpThreadStart(void) {
	rpInitDmaHome();
	// kRemotePlayCallback();

	svc_createMutex(&rp_kcp_mutex, 0);
	rp_control_ready = 1;

	int ret = 0;
	while (ret >= 0) {
		exit_rp_network_thread = 0;
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

		exit_rp_network_thread = 1;
		svc_waitSynchronization1(rp_network_thread, U64_MAX);
		svc_closeHandle(rp_network_thread);

		svc_sleepThread(250000000);
	}

final:
	rpInited = 0;
	svc_exitThread();
}

static int nwmValParamCallback(u8* buf, int buflen) {
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
				nsDbgPrint("Request memory for RemotePlay failed: %08x\n", ret);
				return 0;
			}
			nsDbgPrint("RemotePlay memory: 0x%08x (0x%x bytes)\n", rp_storage_ctx, sizeof(*rp_storage_ctx));

			rpInited = 1;
			memcpy(rp_storage_ctx->nwm_send_buffer, buf, 0x22 + 8);

			umm_init_heap(rp_storage_ctx->umm_heap, RP_UMM_HEAP_SIZE);
			ikcp_allocator(umm_malloc, umm_free);

			ret = svc_createThread(&hThread, (void*)rpThreadStart, 0, (u32 *)&rp_storage_ctx->thread_stack[RP_STACK_SIZE - 40], 0x10, 2);
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
