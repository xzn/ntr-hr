#include "global.h"
#include <ctr/SOC.h>
#include <ctr/syn.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include "umm_malloc.h"
#include "ikcp.h"
#include "libavcodec/jpegls.h"

NS_CONTEXT* g_nsCtx = 0;
NS_CONFIG* g_nsConfig;

static RT_HOOK nwmValParamHook;
static int rp_recv_sock = -1;

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC (268123480)

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

int nsSendPacketHeader(void) {
	g_nsCtx->remainDataLen = g_nsCtx->packetBuf.dataLen;
	rtSendSocket(g_nsCtx->hSocket, (u8*)&(g_nsCtx->packetBuf), sizeof(NS_PACKET));
}

int nsSendPacketData(u8* buf, u32 size) {
	if (g_nsCtx->remainDataLen < size) {
		showDbg("send remain < size: %08x, %08x", g_nsCtx->remainDataLen, size);
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
	rtRecvSocket(g_nsCtx->hSocket, buf, size);
}

typedef u32(*sendPacketTypedef) (u8*, u32);
static sendPacketTypedef nwmSendPacket = 0;

#define KCP_PACKET_SIZE 1448
#define NWM_HEADER_SIZE (0x2a + 8)
#define NWM_PACKET_SIZE (KCP_PACKET_SIZE + NWM_HEADER_SIZE)

#define KCP_MAGIC 0x12345fff
#define KCP_SOCKET_TIMEOUT 10
#define KCP_TIMEOUT_TICKS (250 * SYSTICK_PER_MS)
#define RP_PACKET_SIZE (KCP_PACKET_SIZE - IKCP_OVERHEAD)
#define KCP_SND_WND_SIZE 40

#define RP_PORT (8001)
#define RP_DEST_PORT RP_PORT
#define RP_SCREEN_BUFFER_SIZE (400 * 240 * 4)
#define RP_UMM_HEAP_SIZE (256 * 1024)
#define RP_STACK_SIZE (0x10000)
#define RP_WORKER_STACK_SIZE (0x1000)
#define RP_CONTROL_RECV_BUFFER_SIZE (2000)
#define RP_JLS_ENCODE_BUFFER_SIZE (400 * 240)

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
	u8 network_transfer_thread_stack[RP_WORKER_STACK_SIZE] ALIGN_4;
	u8 screen_transfer_thread_stack[RP_WORKER_STACK_SIZE] ALIGN_4;
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

int rp_udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
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

static void rpControlRecv(void) {
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

void rpNetworkTransfer(void) {
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

void rpNetworkTransferThread(u32 arg) {
	while (!exit_rp_network_thread) {
		rpNetworkTransfer();
	}
	svc_exitThread();
}

static Handle rpHDma[2], rpHandleHome, rpHandleGame;
static u32 rpGameFCRAMBase = 0;

void rpInitDmaHome(void) {
	// u32 rp_dma_config[20] = { 0 };
	svc_openProcess(&rpHandleHome, 0xf);
}

void rpCloseGameHandle(void) {
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
		(u32 *)&rp_storage_ctx->screen_transfer_thread_stack[RP_WORKER_STACK_SIZE - 40],
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
			(u32 *)&rp_storage_ctx->network_transfer_thread_stack[RP_WORKER_STACK_SIZE - 40],
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

int nwmValParamCallback(u8* buf, int buflen) {
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

int nsHandleRemotePlay(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);

	Handle hProcess;
	u32 ret;
	u32 pid = 0x1a;
	u32 remotePC = 0x001231d0;
	NS_CONFIG	cfg;

	memset(&cfg, 0, sizeof(NS_CONFIG));
	cfg.startupCommand = NS_STARTCMD_DEBUG;
	cfg.startupInfo[8] = pac->args[0];
	cfg.startupInfo[9] = pac->args[1];
	cfg.startupInfo[10] = pac->args[2];

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
		copyRemoteMemory(CURRENT_PROCESS_HANDLE, buf, hProcess, (void *)remotePC, 16);
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

	return 0;
}

void nsHandleSaveFile(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 remain = pac->dataLen;
	u8 buf[0x220];
	u32 ret, hFile;
	u32 off = 0, tmp;

	nsRecvPacketData(buf, 0x200);
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
		nsRecvPacketData(g_nsCtx->gBuff, t);
		FSFILE_Write(hFile, &tmp, off, (u32*)g_nsCtx->gBuff, t, 0);

		remain -= t;
		off += t;
	}
	svc_closeHandle(hFile);
	nsDbgPrint("saved to %s successfully\n", buf);
}

int nsFindFreeBreakPoint(void) {
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

void nsHandleQueryHandle(void) {
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
	kmemcpy(&pHandleTable, (void *)(pKProcess + KProcessHandleDataOffset), 4);
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

void nsHandleBreakPoint(void) {
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

void nsHandleReload(void) {
	u32 ret, outAddr;
	u32 hFile, size;
	u64 size64;
	u8* fileName = "/arm11.bin";
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

void nsHandleListProcess(void) {
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

		ret = getProcessInfo(pids[i], pname, tid, &kpobj);
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

void nsHandleMemLayout(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	u32 pid = pac->args[0];
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

void nsHandleWriteMem(void) {
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

void nsHandleReadMem(void) {
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

void nsHandleListThread(void) {
	u32 handle, ret;
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	Handle hProcess;
	u32 pid = pac->args[0];
	u32 tids[100];
	u32 tidCount, i, j;
	u32 ctx[400];
	// u32 hThread;
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
		return;
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
		return -1;
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

void nsHandleAttachProcess(void) {
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

void nsUpdateDebugStatus(void) {
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

void nsHandlePacket(void) {
	NS_PACKET* pac = &(g_nsCtx->packetBuf);
	g_nsCtx->remainDataLen = pac->dataLen;
	if (pac->cmd == NS_CMD_SAYHELLO) {
		disp(100, 0x100ff00);
		nsDbgPrint("hello\n");
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

void nsMainLoop(void) {
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
		showDbg("bind failed: %08x\n", ret, 0);
		return;
	}
	ret = listen(listen_sock, 1);
	if (ret < 0) {
		showDbg("listen failed: %08x\n", ret, 0);
		return;
	}

	struct pollfd pi[2];
	fd_set rset;
	int maxfdp1;
	int nready;

	int rpProcess = getCurrentProcessId() == 0x1a;
	// int rpProcess = 0;
	if (rpProcess) {
		rp_recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (rp_recv_sock < 0) {
			showDbg("rp_recv_sock open failed: %d\n", rp_recv_sock, 0);
			disp(100, 0x100ffff);
			return;
		}

		struct sockaddr_in sai = {0};
		sai.sin_family = AF_INET;
		sai.sin_port = htons(RP_PORT);
		sai.sin_addr.s_addr = htonl(INADDR_ANY);

		int ret = bind(rp_recv_sock, (struct sockaddr *)&sai, sizeof(sai));
		if (ret < 0) {
			showDbg("rp_recv_sock bind failed: %d\n", ret, 0);
			disp(100, 0x1ffff00);
			return;
		}
	}

#define RP_PROCESS_SELECT2(s) \
	if (rpProcess) { \
		pi[0].fd = s; \
		pi[1].fd = rp_recv_sock; \
		pi[1].events = pi[0].events = POLLIN; \
		pi[1].revents = pi[0].revents = 0; \
		nready = poll2(pi, 2, -1); \
		if (nready <= 0) \
			continue; \
	} \
	if (rpProcess && pi[1].revents & (POLLIN|POLLHUP)) { \
		rpControlRecv(); \
	} \
	if (!rpProcess || pi[0].revents & (POLLIN|POLLHUP))

#define RP_PROCESS_SELECT(s) \
	if (rpProcess) { \
		FD_ZERO(&rset); \
		FD_SET(s, &rset); \
		FD_SET(rp_recv_sock, &rset); \
		maxfdp1 = (s > rp_recv_sock ? s : rp_recv_sock) + 1; \
		nready = select2(maxfdp1, &rset, NULL, NULL, NULL); \
		if (nready <= 0) \
			continue; \
	} \
 \
	if (rpProcess && FD_ISSET(rp_recv_sock, &rset)) { \
		rpControlRecv(); \
	} \
 \
	if (!rpProcess || FD_ISSET(s, &rset))

	while (1) {
		RP_PROCESS_SELECT(listen_sock) {
			sockfd = accept(listen_sock, NULL, NULL);
			g_nsCtx->hSocket = sockfd;
			if (sockfd < 0) {
				if (!rpProcess) {
					svc_sleepThread(100000000);
				}
				continue;
			}
			/*
			tmp = fcntl(sockfd, F_GETFL);
			fcntl(sockfd, F_SETFL, tmp | O_NONBLOCK);
			*/
			while (1) {
				RP_PROCESS_SELECT2(sockfd) {
					ret = rtRecvSocket(sockfd, (u8*)&(g_nsCtx->packetBuf), sizeof(NS_PACKET));
					if (ret != sizeof(NS_PACKET)) {
						nsDbgPrint("rtRecvSocket failed: %08x\n", ret, 0);
						break;
					}
					NS_PACKET* pac = &(g_nsCtx->packetBuf);
					if (pac->magic != 0x12345678) {
						nsDbgPrint("broken protocol: %08x, %08x\n", pac->magic, pac->seq);
						break;
					}
					nsUpdateDebugStatus();
					nsHandlePacket();
				}
			}
			closesocket(sockfd);
		}
	}
#undef RP_PROCESS_SELECT
#undef RP_PROCESS_SELECT2
}

void nsThreadStart(void) {
	nsMainLoop();
	svc_exitThread();
}

#define STACK_SIZE 0x4000

void nsInitDebug(void) {
	xfunc_out = (void*)nsDbgPutc;
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