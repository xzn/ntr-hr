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
#include "fastlz.h"
#include "gen.h"

#ifdef HAS_HUFFMAN_RLE
#include "huffmancodec.h"
#include "rlecodec.h"
#endif

#include "umm_malloc.h"
#include "ikcp.h"

#define HR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define HR_MIN(a, b) ((a) < (b) ? (a) : (b))

NS_CONTEXT* g_nsCtx = 0;
NS_CONFIG* g_nsConfig;

void doSendDelay(u32 time) {
	vu32 i;
	for (i = 0; i < time; i++) {

	}
}

void tje_log(char* str) {
	nsDbgPrint("tje: %s\n", str);
}

#define I_STRINGIFY2(a) #a
// stringify
#define I_S(a) I_STRINGIFY2(a)
#define I_CONCAT2(a, b) a ## b
// concat
#define I_C(a, b) I_CONCAT2(a, b)

// attribute aligned
#define I_A __attribute__ ((aligned (4)))
// assume aligned
#define I_AA(a) (a = __builtin_assume_aligned (a, 4))

#define BITS_PER_BYTE 8
#define ENCODE_SELECT_MASK_X_SCALE 1
#define ENCODE_SELECT_MASK_Y_SCALE 8
#define ENCODE_SELECT_MASK_STRIDE(h) \
    (((h) + (ENCODE_SELECT_MASK_Y_SCALE * BITS_PER_BYTE) - 1) / (ENCODE_SELECT_MASK_Y_SCALE * BITS_PER_BYTE))
#define ENCODE_SELECT_MASK_SIZE(w, h) \
    (ENCODE_SELECT_MASK_STRIDE(h) * (((w) + ENCODE_SELECT_MASK_X_SCALE - 1) / ENCODE_SELECT_MASK_X_SCALE))

#define ENCODE_UPSAMPLE_CARRY_SIZE(w, h) (((h) + BITS_PER_BYTE - 1) / BITS_PER_BYTE * (w))

RT_HOOK nwmValParamHook;

#define PACKET_SIZE 1448
#define NWM_HEADER_SIZE (0x2a + 8)
#define RP_PACKET_SIZE (PACKET_SIZE + NWM_HEADER_SIZE)
int remotePlayInited = 0;
#define RP_IMG_BUFFER_SIZE 0x00800000
#define RP_IMG_BUF_TOP_SIZE (400 * 240 * 4 * 2)
#define RP_IMG_BUF_BOT_SIZE (320 * 240 * 4)
#define RP_stackSize 0x10000
#define RP_dataStackSize 0x1000
#define RP_CONTROL_RECV_BUF_SIZE 2000
#define UMM_HEAP_SIZE (512 * 1024)
#define FRAME_RATE_AVERAGE_COUNT 90

#define RP_DATA_TOP_BOT ((u32)1 << 0)
#define RP_DATA_Y_UV ((u32)1 << 1)
#define RP_DATA_FRAME_DELTA ((u32)1 << 2)
#define RP_DATA_PREVIOUS_FRAME_DELTA ((u32)1 << 3)
#define RP_DATA_SELECT_FRAME_DELTA ((u32)1 << 4)
#define RP_DATA_HUFFMAN ((u32)1 << 5)
#define RP_DATA_RLE ((u32)1 << 6)
#define RP_DATA_YUV_LQ ((u32)1 << 7)
#define RP_DATA_INTERLACE ((u32)1 << 8)
#define RP_DATA_INTERLACE_EVEN_ODD ((u32)1 << 9)
#define RP_DATA_DOWNSAMPLE ((u32)1 << 10)
#define RP_DATA_DOWNSAMPLE2 ((u32)1 << 11)

struct RP_DATA_HEADER {
	u32 flags;
	u32 len;
	u32 id;
	u32 uncompressed_len;
};

#define RP_DATA2_HUFFMAN ((u32)1 << 0)
#define RP_DATA2_RLE ((u32)1 << 1)

struct RP_DATA2_HEADER {
	u32 flags;
	u32 len;
	u32 id;
	u32 uncompressed_len;
};

#define RP_TOP_DATA2_MAX_SIZE_1 (ENCODE_SELECT_MASK_SIZE(400, 240))
#define HR_DST_SIZE_TOP (96000 + rle_max_compressed_size(96000) + sizeof(struct RP_DATA_HEADER) + \
	RP_TOP_DATA2_MAX_SIZE_1 + rle_max_compressed_size(RP_TOP_DATA2_MAX_SIZE_1) + sizeof(struct RP_DATA2_HEADER))

#define RP_BOT_DATA2_MAX_SIZE_1 (ENCODE_SELECT_MASK_SIZE(320, 240))
#define HR_DST_SIZE_BOT (76800 + rle_max_compressed_size(76800) + sizeof(struct RP_DATA_HEADER) + \
	RP_BOT_DATA2_MAX_SIZE_1 + rle_max_compressed_size(RP_BOT_DATA2_MAX_SIZE_1) + sizeof(struct RP_DATA2_HEADER))

#define I_N top
#define I_WIDTH 400
#define I_HEIGHT 240
#include "enc_ctx.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

#define I_N bot
#define I_WIDTH 320
#define I_HEIGHT 240
#include "enc_ctx.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

#define I_N il_top
#define I_WIDTH 400
#define I_HEIGHT 120
#include "enc_ctx.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

#define I_N il_bot
#define I_WIDTH 320
#define I_HEIGHT 120
#include "enc_ctx.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

struct ENC_FD_CTX {
	struct ENC_FD_CTX_top fd_top[3];
	struct ENC_FD_CTX_bot fd_bot[3];
};

struct ENC_FD_il_CTX {
	struct ENC_FD_CTX_il_top fd_top[3];
	struct ENC_FD_CTX_il_bot fd_bot[3];
};

struct ENC_CTX {
	struct ENC_top top;
	struct ENC_bot bot;
};

struct ENC_il_CTX {
	struct ENC_il_top top;
	struct ENC_il_bot bot;
};

struct RP_NETWORK_PARAMS {
	u32 targetBitsPerSec;
	u32 targetFrameRate;
	u32 bitsPerFrame;
	u32 bitsPerY;
	u32 bitsPerUV;
};

struct RP_DYNAMIC_PRIO {
	u32 frame_n;
	float top_screen_time;
	float bot_screen_time;
	u8 previous_screen;
	int frame_n_since_previous_screen;
	int frame_rate;
	u64 tick_at_frame[FRAME_RATE_AVERAGE_COUNT];
	u8 tick_at_frame_i;
};

static struct RP_CTX {
	union {
		struct ENC_FD_CTX c;
		struct ENC_FD_il_CTX il_c[2];
	} fd_c;
	union {
		struct ENC_CTX c;
		struct ENC_il_CTX il_c;
	} c;

	struct huffman_alloc_s hr_alloc[2];

	u8 nwm_send_buf[RP_PACKET_SIZE] I_A;
	u8 kcp_send_buf[PACKET_SIZE] I_A;
	u8 enc_thread_stack[2][RP_stackSize] I_A;
	u8 send_thread_stack[RP_dataStackSize] I_A;
	u8 tr2_thread_stack[RP_dataStackSize] I_A;
	u8 umm_malloc_heap_addr[UMM_HEAP_SIZE] I_A;

	struct RP_DYNAMIC_PRIO dyn_prio;

	u8 recv_buf[RP_CONTROL_RECV_BUF_SIZE] I_A;

	u8 img_buf_top[RP_IMG_BUF_TOP_SIZE] I_A;
	u8 img_buf_bot[RP_IMG_BUF_BOT_SIZE] I_A;

	u8 hr_dst_top[3][HR_DST_SIZE_TOP] I_A;
	u8 hr_dst_bot[3][HR_DST_SIZE_BOT] I_A;

	RP_CONFIG cfg;

	s8 multicore_encode, triple_buffer_encode, priority_screen, dynamic_priority, priority_factor, interlace;
	u64 min_send_interval_in_ticks, last_send_tick, last_kcp_send_tick;
	u8 hr_frame_id[2];
	u8 nwm_frame_id[2];
	u8 *hr_dst[2][3];
	u8 *nwm_src[2][3];
	u32 nwm_len[2][3];

	u8 force_key[2];

	Handle enc_thread[2], send_thread, tr2_thread;
	Handle kcp_mutex;
	ikcpcb *kcp;

	u32 data_header_id[2][2];

	u8 nwm_thread_exit, enc_thread_exit, kcp_restart;

	Handle enc_done_sem[2];
	Handle enc_avai_sem[2];
	Handle tr2_done_sem;
	Handle tr2_avai_sem;
	int tr2_src_size;
	s32 frame_done_n[2];

	u32 fb_addr[2][2];
	u32 fb_format[2];
	u32 fb_pitch[2];
	u32 fb_current[2];

	struct RP_NETWORK_PARAMS network_params;
} *rp_ctx;
static int rp_recv_sock = -1;
static u8 rp_control_ready = 0;

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC 268123480

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

int	initUDPPacket(u8* rpBuf, int dataLen, int port) {
	dataLen += 8;
	*(u16*)(rpBuf + 0x22 + 8) = htons(8000); // src port
	*(u16*)(rpBuf + 0x24 + 8) = htons(port); // dest port
	*(u16*)(rpBuf + 0x26 + 8) = htons(dataLen);
	*(u16*)(rpBuf + 0x28 + 8) = 0; // no checksum
	dataLen += 20;

	*(u16*)(rpBuf + 0x10 + 8) = htons(dataLen);
	*(u16*)(rpBuf + 0x12 + 8) = 0xaf01; // packet id is a random value since we won't use the fragment
	*(u16*)(rpBuf + 0x14 + 8) = 0x0040; // no fragment
	*(u16*)(rpBuf + 0x16 + 8) = 0x1140; // ttl 64, udp
	*(u16*)(rpBuf + 0x18 + 8) = 0;
	*(u16*)(rpBuf + 0x18 + 8) = ip_checksum(rpBuf + 0xE + 8, 0x14);

	dataLen += 22;
	*(u16*)(rpBuf + 12) = htons(dataLen);

	return dataLen;
}


#define GL_RGBA8_OES (0)
#define GL_RGB8_OES (1)
#define GL_RGB565_OES (2)
#define GL_RGB5_A1_OES (3)
#define GL_RGBA4_OES (4)
#define GL_RGB565_LE (5)

#define REMOTE_PLAY_PORT (8001)
#define RP_DBG_PORT (8002)


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
	int format, src_pitch;

	u8* src;
	int src_size;

	int is_key, force_key, nextKey;

	int bpp;
	u32 bytesInColumn ;
	u32 blankInColumn;

	u8 id;
	u8 top_bot;
} BLIT_CONTEXT;
static void remotePlayBlitInit(BLIT_CONTEXT* ctx, int width, int height, int format, int src_pitch, u8* src, int src_size) {

	format &= 0x0f;
	if (format == 0){
		ctx->bpp = 4;
	} else if (format == 1){
		ctx->bpp = 3;
	} else {
		ctx->bpp = 2;
	}
	ctx->bytesInColumn = ctx->bpp * height;
	ctx->blankInColumn = src_pitch - ctx->bytesInColumn;
	ctx->format = format;

	ctx->src_pitch = src_pitch;
	ctx->src = src;
	ctx->src_size = HR_MAX(src_size, 0);

	ctx->is_key = !(rp_ctx->cfg.flags & RP_FRAME_DELTA) || ctx->force_key;
	ctx->force_key = 0;

	++ctx->id;
	if (rp_ctx->interlace) {
		if (ctx->is_key) {
			ctx->nextKey = 1;
		} else if (ctx->nextKey) {
			ctx->is_key = 1;
			ctx->nextKey = 0;
		}
	}
}

void rpDbg(const char* fmt, ...) {
	if (!(rp_ctx->cfg.flags & RP_DEBUG))
		return;

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

// #define rshift_to_even(n, s) ({ typeof(n) n_ = n >> (s - 1); u8 b_ = n_ & 1; n_ >>= 1; u8 c_ = n_ & 1; n_ + (b_ & c_); })
#define rshift_to_even(n, s) ((n + (s > 1 ? (1 << (s - 1)) : 0)) >> s)
// #define rshift_to_even(n, s) ((n + (1 << (s - 1))) >> s)

static void convertYUV(u8 r, u8 g, u8 b, u8 *y_out, u8 *u_out, u8 *v_out) {
	u16 y = 77 * (u16)r + 150 * (u16)g + 29 * (u16)b;
	u16 u = -43 * (u16)r + -84 * (u16)g + 127 * (u16)b;
	u16 v = 127 * (u16)r + -106 * (u16)g + -21 * (u16)b;

	if (rp_ctx->cfg.flags & RP_YUV_LQ) {
		*y_out = rshift_to_even(y, 10);
		*u_out = (rshift_to_even(u, 11) + 16) % 32;
		*v_out = (rshift_to_even(v, 11) + 16) % 32;
	} else {
		*y_out = rshift_to_even(y, 8);
		*u_out = rshift_to_even(u, 8) + 128;
		*v_out = rshift_to_even(v, 8) + 128;
	}
}

static u8 accessImageNoCheck(const u8 *image, int x, int y, int w, int h) {
	return image[x * h + y];
}

static u8 accessImage(const u8 *image, int x, int y, int w, int h) {
	return accessImageNoCheck(image, HR_MAX(HR_MIN(x, w - 1), 0), HR_MAX(HR_MIN(y, h - 1), 0), w, h);
}

static u8 medianOf3_u8(u8 a, u8 b, u8 c) {
	u8 max = a > b ? a : b;
	max = max > c ? max : c;

	u8 min = a < b ? a : b;
	min = min < c ? min : c;

	return a + b + c - max - min;
}

static u8 predictPixel(const u8 *image, int x, int y, int w, int h) {
	if (x == 0 && y == 0) {
		return 0;
	}

	if (x == 0) {
		return accessImageNoCheck(image, x, y - 1, w, h);
	}

	if (y == 0) {
		return accessImageNoCheck(image, x - 1, y, w, h);
	}

	u8 t = accessImageNoCheck(image, x, y - 1, w, h);
	u8 l = accessImageNoCheck(image, x - 1, y, w, h);
	u8 tl = accessImageNoCheck(image, x - 1, y - 1, w, h);

	return medianOf3_u8(t, l, t + l - tl);
}

static void predictImage(u8 *dst, const u8 *src, int w, int h) {
	const u8* src_begin = src;
	for (int i = 0; i < w; ++i) {
		for (int j = 0; j < h; ++j) {
			*dst++ = *src++ - predictPixel(src_begin, i, j, w, h);
		}
	}
}

// x and y are % 2 == 0
static u16 accessImageDownsampleUnscaled(const u8 *image, int x, int y, int w, int h) {
	int x0 = x; // / 2 * 2;
	int x1 = x0 + 1;
	int y0 = y; // / 2 * 2;
	int y1 = y0 + 1;

	// x1 = x1 >= w ? w - 1 : x1;
	// y1 = y1 >= h ? h - 1 : y1;

	u8 a = accessImageNoCheck(image, x0, y0, w, h);
	u8 b = accessImageNoCheck(image, x1, y0, w, h);
	u8 c = accessImageNoCheck(image, x0, y1, w, h);
	u8 d = accessImageNoCheck(image, x1, y1, w, h);

	return (u16)a + b + c + d;
}

// x and y are % 2 == 0, see accessImageDownsampleUnscaled
static u8 accessImageDownsample(const u8 *image, int x, int y, int w, int h) {
	u16 p = accessImageDownsampleUnscaled(image, x, y, w, h);
	return rshift_to_even(p, 2);
}

// x % 2 == 0
static u8 accessImageDownsampleH(const u8 *image, int x, int y, int w, int h) {
	int x0 = x; // / 2 * 2;
	int x1 = x0 + 1;

	// x1 = x1 >= w ? w - 1 : x1;
	// y1 = y1 >= h ? h - 1 : y1;

	u8 a = accessImageNoCheck(image, x0, y, w, h);
	u8 b = accessImageNoCheck(image, x1, y, w, h);

	u16 c = a + b;
	return rshift_to_even(c, 1);
}

// y % 2 == 0
static u8 accessImageDownsampleV(const u8 *image, int x, int y, int w, int h) {
	int y0 = y; // / 2 * 2;
	int y1 = y0 + 1;

	// x1 = x1 >= w ? w - 1 : x1;
	// y1 = y1 >= h ? h - 1 : y1;

	u8 a = accessImageNoCheck(image, x, y0, w, h);
	u8 b = accessImageNoCheck(image, x, y1, w, h);

	u16 c = a + b;
	return rshift_to_even(c, 1);
}

static void downsampleImageH(u8 *ds_dst, const u8 *src, int wOrig, int hOrig) {
	int i = 0, j = 0;
	for (; i < wOrig; i += 2) {
		j = 0;
		for (; j < hOrig; ++j) {
			*ds_dst++ = accessImageDownsampleH(src, i, j, wOrig, hOrig);
		}
	}
}

static void downsampleImageV(u8 *ds_dst, const u8 *src, int wOrig, int hOrig) {
	int i = 0, j = 0;
	for (; i < wOrig; ++i) {
		j = 0;
		for (; j < hOrig; j += 2) {
			*ds_dst++ = accessImageDownsampleV(src, i, j, wOrig, hOrig);
		}
	}
}

static void downsampleImage(u8 *ds_dst, const u8 *src, int wOrig, int hOrig) {
	int i = 0, j = 0;
	for (; i < wOrig; i += 2) {
		j = 0;
		for (; j < hOrig; j += 2) {
			*ds_dst++ = accessImageDownsample(src, i, j, wOrig, hOrig);
		}
	}
}

static void upsampleFDCImageH(u8 *ds_dst, u8 *ds_c_dst, u8 *ds_c_dst_end, const u8 *src, const u8 *ds_src, int w, int h) {
	int i = 0, ds_w = w / 2, ds_x = 0, i_1 = i + 1, mask = 0;
	int j, n;
	memset(ds_c_dst, 0, ds_c_dst_end - ds_c_dst);
	for (; i < w; i += 2, i_1 += 2, ++ds_x) {
		n = j = 0;
		for (; j < h; ++j) {
			u8 d = accessImageNoCheck(ds_src, ds_x, j, ds_w, h);
			u8 a = accessImageNoCheck(src, i, j, w, h);
			u8 b = accessImageNoCheck(src, i_1, j, w, h);
			*ds_dst++ = a - d;
			u8 c = (a + b) & 1;
			c <<= n++;
			mask |= c;
			n %= BITS_PER_BYTE;
			if (n == 0) {
				*ds_c_dst++ = mask;
				mask = 0;
			}
		}

		if (n != 0) {
			*ds_c_dst++ = mask;
			mask = 0;
		}
	}
	if (ds_c_dst > ds_c_dst_end) {
		nsDbgPrint("upsampleFDCImageH size error %d\n", ds_c_dst - ds_c_dst_end);
	}
}

static void upsampleFDCImageV(u8 *ds_dst, u8 *ds_c_dst, u8 *ds_c_dst_end, const u8 *src, const u8 *ds_src, int w, int h) {
	int i = 0, ds_h = h / 2, mask = 0;
	int j, ds_y, j_1, n;
	memset(ds_c_dst, 0, ds_c_dst_end - ds_c_dst);
	for (; i < w; ++i) {
		n = j = ds_y = 0;
		j_1 = j + 1;
		for (; j < h; j += 2, j_1 += 2, ++ds_y) {
			u8 d = accessImageNoCheck(ds_src, i, ds_y, w, ds_h);
			u8 a = accessImageNoCheck(src, i, j, w, h);
			u8 b = accessImageNoCheck(src, i, j_1, w, h);
			*ds_dst++ = a - d;
			u8 c = (a + b) & 1;
			c <<= n++;
			mask |= c;
			n %= BITS_PER_BYTE;
			if (n == 0) {
				*ds_c_dst++ = mask;
				mask = 0;
			}
		}

		if (n != 0) {
			*ds_c_dst++ = mask;
			mask = 0;
		}
	}
	if (ds_c_dst > ds_c_dst_end) {
		nsDbgPrint("upsampleFDCImageH size error %d\n", ds_c_dst - ds_c_dst_end);
	}
}

static u16 accessImageUpsampleUnscaled(const u8 *ds_image, int xOrig, int yOrig, int wOrig, int hOrig) {
	int ds_w = wOrig / 2;
	int ds_h = hOrig / 2;

	int ds_x0 = xOrig / 2;
	int ds_x1 = ds_x0;
	int ds_y0 = yOrig / 2;
	int ds_y1 = ds_y0;

	if (xOrig > ds_x0 * 2) { // xOrig is odd -> ds_x0 * 2 + 1 = xOrig = ds_x1 * 2 - 1
		++ds_x1;
	} else { // xOrig is even -> ds_x0 * 2 + 2 = xOrig = ds_x1 * 2
		--ds_x0;
	}

	if (yOrig > ds_y0 * 2) {
		++ds_y1;
	} else {
		--ds_y0;
	}

    u16 a = accessImage(ds_image, ds_x0, ds_y0, ds_w, ds_h);
    u16 b = accessImage(ds_image, ds_x1, ds_y0, ds_w, ds_h);
    u16 c = accessImage(ds_image, ds_x0, ds_y1, ds_w, ds_h);
    u16 d = accessImage(ds_image, ds_x1, ds_y1, ds_w, ds_h);

    if (xOrig < ds_x1 * 2) {
        a = (a * 3 + b);
        c = (c * 3 + d);
    } else {
        a = (a + b * 3);
        c = (c + d * 3);
    }

    if (yOrig < ds_y1 * 2) {
        a = (a * 3 + c);
    } else {
        a = (a + c * 3);
    }

	return a;
}

static u8 accessImageUpsample(const u8 *ds_image, int xOrig, int yOrig, int wOrig, int hOrig) {
	u16 p = accessImageUpsampleUnscaled(ds_image, xOrig, yOrig, wOrig, hOrig);
	return rshift_to_even(p, 4);
}

static void upsampleImage(u8 *dst, const u8 *ds_src, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; ++i) {
		j = 0;
		for (; j < h; ++j) {
			*dst++ = accessImageUpsample(ds_src, i, j, w, h);
		}
	}
}

static void differenceImage(u8 *dst, const u8 *src, const u8 *src_prev, int w, int h) {
	u8 *dst_end = dst + w * h;
	while (dst != dst_end) {
		*dst++ = *src++ - *src_prev++;
	}
}

static void differenceFromDownsampled(u8 *dst, const u8 *src, const u8 *ds_src_prev, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; ++i) {
		j = 0;
		for (; j < h; ++j) {
			*dst++ = *src++ - accessImageUpsample(ds_src_prev, i, j, w, h);
		}
	}
}

static void downsampledDifference(u8 *ds_pf, u8 *fd_ds_dst, const u8 *src, const u8 *src_prev, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; i += 2) {
		j = 0;
		for (; j < h; j += 2) {
			*fd_ds_dst++ = (*ds_pf++ = accessImageDownsample(src, i, j, w, h)) - accessImageDownsample(src_prev, i, j, w, h);
		}
	}
}

static void downsampledDifferenceFromDownsampled(u8 *ds_pf, u8 *fd_ds_dst, const u8 *src, const u8 *ds_src_prev, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; i += 2) {
		j = 0;
		for (; j < h; j += 2) {
			*fd_ds_dst++ = (*ds_pf++ = accessImageDownsample(src, i, j, w, h)) - *ds_src_prev++;
		}
	}
}

static u8 abs_s8(s8 s) {
	return s > 0 ? s : -s;
}

static void selectImage(u8 *s_dst, u8 *m_dst, u8 *m_dst_end, u8 *p_fd, const u8 *p, int w, int h) {
	u16 sum_p_fd, sum_p;
	u8 mask_bit, mask = 0;
	int x = 0, y, i, j, n;
	memset(m_dst, 0, m_dst_end - m_dst);
	while (1) {
		n = y = 0;
		while (1) {
			sum_p_fd = 0;
			sum_p = 0;
			for (i = x; i < HR_MIN(x + ENCODE_SELECT_MASK_X_SCALE, w); ++i) {
				for (j = y; j < HR_MIN(y + ENCODE_SELECT_MASK_Y_SCALE, h); ++j) {
					sum_p_fd += abs_s8(accessImageNoCheck(p_fd, i, j, w, h));
					sum_p += abs_s8(accessImageNoCheck(p, i, j, w, h));
				}
			}
			mask_bit = sum_p_fd <= sum_p;

			if (mask_bit) {
				for (i = x; i < HR_MIN(x + ENCODE_SELECT_MASK_X_SCALE, w); ++i)
					for (j = y; j < HR_MIN(y + ENCODE_SELECT_MASK_Y_SCALE, h); ++j)
						s_dst[i * h + j] = accessImageNoCheck(p_fd, i, j, w, h);
			} else {
				for (i = x; i < HR_MIN(x + ENCODE_SELECT_MASK_X_SCALE, w); ++i)
					for (j = y; j < HR_MIN(y + ENCODE_SELECT_MASK_Y_SCALE, h); ++j)
						s_dst[i * h + j] = accessImageNoCheck(p, i, j, w, h);
			}

			mask_bit <<= n++;
			mask |= mask_bit;
			n %= BITS_PER_BYTE;
			if (n == 0) {
				*m_dst++ = mask;
				mask = 0;
			}

			y += ENCODE_SELECT_MASK_Y_SCALE;
			if (y >= h) break;
		}

		if (n != 0) {
			*m_dst++ = mask;
			mask = 0;
		}

		x += ENCODE_SELECT_MASK_X_SCALE;
		if (x >= w) break;
	}
	if (m_dst > m_dst_end) {
		nsDbgPrint("selectImage size error %d\n", m_dst - m_dst_end);
	}
}

typedef struct _COMPRESS_CONTEXT {
	const u8* data;
	u32 data_size;
	const u8* data2;
	u32 data2_size;
	u32 max_compressed_size;
} COMPRESS_CONTEXT;

static void rpSendData(int top_bot, u8* data, u32 size) {
	rp_ctx->nwm_src[top_bot][rp_ctx->hr_frame_id[top_bot]] = data;
	rp_ctx->nwm_len[top_bot][rp_ctx->hr_frame_id[top_bot]] = size;
	rp_ctx->hr_frame_id[top_bot] = (rp_ctx->hr_frame_id[top_bot] + 1) % 2;
}

#define RP_CONTROL_TOP_KEY (1 << 0)
#define RP_CONTROL_BOT_KEY (1 << 1)

void rpControlRecvHandle(u8* buf, int buf_size) {
	if (*buf & RP_CONTROL_TOP_KEY) {
		// rpDbg("force top key frame requested\n");
		rp_ctx->force_key[0] = 1;
		// svc_flushProcessDataCache(rp_ctx->enc_thread[0], (u32)&rp_ctx->force_key[0], sizeof(rp_ctx->force_key[0]));
		// svc_flushProcessDataCache(rp_ctx->enc_thread[1], (u32)&rp_ctx->force_key[0], sizeof(rp_ctx->force_key[0]));
		__dmb();
	}
	if (*buf & RP_CONTROL_BOT_KEY) {
		// rpDbg("force bot key frame requested\n");
		rp_ctx->force_key[1] = 1;
		// svc_flushProcessDataCache(rp_ctx->enc_thread[0], (u32)&rp_ctx->force_key[1], sizeof(rp_ctx->force_key[1]));
		// svc_flushProcessDataCache(rp_ctx->enc_thread[1], (u32)&rp_ctx->force_key[1], sizeof(rp_ctx->force_key[1]));
		__dmb();
	}
}

int rp_udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
	u8 *sendBuf = rp_ctx->nwm_send_buf;
	u8 *dataBuf = sendBuf + NWM_HEADER_SIZE;

	if (len > PACKET_SIZE) {
		nsDbgPrint("rp_udp_output len exceeded PACKET_SIZE: %d\n", len);
		return 0;
	}

	memcpy(dataBuf, buf, len);
	int packetLen = initUDPPacket(sendBuf, len, REMOTE_PLAY_PORT);
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

struct RP_PACKET_HEADER {
	u32 id;
	u32 len;
};

extern const IUINT32 IKCP_OVERHEAD;
#define HR_KCP_MAGIC 0x12345fff
#define KCP_SOCKET_TIMEOUT 10
#define KCP_TIMEOUT_TICKS (250 * SYSTICK_PER_MS)
#define KCP_PACKET_SIZE (PACKET_SIZE - IKCP_OVERHEAD)
#define KCP_SND_WND_SIZE 40

static int rpDynPrioGetScreen(void) {
	int current_screen = rp_ctx->priority_screen;
	struct RP_DYNAMIC_PRIO *dyn_prio = &rp_ctx->dyn_prio;
	++dyn_prio->frame_n;

	if (rp_ctx->dynamic_priority) {
		if (dyn_prio->frame_rate > rp_ctx->network_params.targetFrameRate) {
			current_screen = dyn_prio->top_screen_time > dyn_prio->bot_screen_time;
		}
		if (current_screen == dyn_prio->previous_screen) {
			++dyn_prio->frame_n_since_previous_screen;
		} else {
			dyn_prio->previous_screen = current_screen;
			dyn_prio->frame_n_since_previous_screen = 1;
			// rpDbg("screen change %d %d %d\n", previous_screen, (int)(1.0f / top_screen_time), (int)(1.0f / bot_screen_time));
		}
		if (dyn_prio->frame_n_since_previous_screen > rp_ctx->priority_factor) {
			dyn_prio->previous_screen = current_screen = !current_screen;
			dyn_prio->frame_n_since_previous_screen = 1;
		}
		if (current_screen == 0) {
			dyn_prio->bot_screen_time = HR_MAX(dyn_prio->bot_screen_time - dyn_prio->top_screen_time, 0);
			dyn_prio->top_screen_time = 0;
		} else {
			dyn_prio->top_screen_time = HR_MAX(dyn_prio->top_screen_time - dyn_prio->bot_screen_time, 0);
			dyn_prio->bot_screen_time = 0;
		}

		u64 tick_at_current = svc_getSystemTick();
		u64 tick_diff = HR_MIN(HR_MAX(tick_at_current - dyn_prio->tick_at_frame[dyn_prio->tick_at_frame_i], SYSTICK_PER_SEC / rp_ctx->network_params.targetFrameRate), SYSTICK_PER_SEC);
		dyn_prio->tick_at_frame[dyn_prio->tick_at_frame_i] = tick_at_current;
		dyn_prio->tick_at_frame_i = (dyn_prio->tick_at_frame_i + 1) % FRAME_RATE_AVERAGE_COUNT;
		dyn_prio->frame_rate = (u64)SYSTICK_PER_SEC * FRAME_RATE_AVERAGE_COUNT / tick_diff;
		if (dyn_prio->tick_at_frame_i == 0) {
			// rpDbg("priority screen fps %d\n", (u32)frame_rate);
		}
	} else if (rp_ctx->priority_factor != 0) {
		if (dyn_prio->frame_n % (rp_ctx->priority_factor + 1) == 0) {
			current_screen = !rp_ctx->priority_screen;
		}
	}

	return current_screen;
}

static void rpSendDataDiscard(void) {
	s32 count;
	while (svc_waitSynchronization1(rp_ctx->enc_done_sem[0], 0) == 0) {
		rp_ctx->nwm_frame_id[0] = (rp_ctx->nwm_frame_id[0] + 1) % 2;
		svc_releaseSemaphore(&count, rp_ctx->enc_avai_sem[0], 1);
	}

	if (rp_ctx->multicore_encode) {
		while (svc_waitSynchronization1(rp_ctx->enc_done_sem[1], 0) == 0) {
			rp_ctx->nwm_frame_id[1] = (rp_ctx->nwm_frame_id[1] + 1) % 2;
			svc_releaseSemaphore(&count, rp_ctx->enc_avai_sem[1], 1);
		}
	}
}

void rpSendDataThreadMain(void) {
	int ret;

	svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
	rp_ctx->kcp = ikcp_create(HR_KCP_MAGIC, 0);
	if (!rp_ctx->kcp) {
		nsDbgPrint("ikcp_create failed\n");
		svc_releaseMutex(rp_ctx->kcp_mutex);
		rp_ctx->nwm_thread_exit = 1;
		return;
	}
	rp_ctx->kcp->output = rp_udp_output;
	if ((ret = ikcp_setmtu(rp_ctx->kcp, PACKET_SIZE)) < 0) {
		nsDbgPrint("ikcp_setmtu failed: %d\n", ret);
	}
	ikcp_nodelay(rp_ctx->kcp, 1, 10, 1, 1);
	// rp_ctx->kcp->rx_minrto = 10;
	ikcp_wndsize(rp_ctx->kcp, KCP_SND_WND_SIZE, 0);
	svc_releaseMutex(rp_ctx->kcp_mutex);
	rp_ctx->data_header_id[0][0] = 0;
	rp_ctx->data_header_id[0][1] = 0;
	rp_ctx->data_header_id[1][0] = 0;
	rp_ctx->data_header_id[1][1] = 0;

	int current_screen = rp_ctx->multicore_encode ? current_screen = rpDynPrioGetScreen() : 0;
	memset(&rp_ctx->dyn_prio, 0, sizeof(rp_ctx->dyn_prio));

	while (!rp_ctx->nwm_thread_exit) {
		u32 packet_header_id = 0;

		if (rp_ctx->multicore_encode) {
			s32 frame_done_n, *p_frame_done_n = &rp_ctx->frame_done_n[current_screen];
			if (*p_frame_done_n) {
				do {
					frame_done_n = __ldrex(p_frame_done_n);
				} while (__strex(p_frame_done_n, frame_done_n - 1));
				current_screen = rpDynPrioGetScreen();
			}
		} else {
			current_screen = 0;
		}

		Handle workDoneSem = rp_ctx->enc_done_sem[current_screen], workAvaiSem = rp_ctx->enc_avai_sem[current_screen];
		u8 **send_src = rp_ctx->nwm_src[current_screen];
		u32 *send_len = rp_ctx->nwm_len[current_screen];
		u8 *frameId = &rp_ctx->nwm_frame_id[current_screen];

		while (1) {
			ret = svc_waitSynchronization1(workDoneSem, KCP_SOCKET_TIMEOUT * 1000000);
			if (ret != 0) {
				svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
				ikcp_update(rp_ctx->kcp, iclock());
				svc_releaseMutex(rp_ctx->kcp_mutex);

				if (rp_ctx->kcp_restart || rp_ctx->nwm_thread_exit) {
					break;
				}

				continue;
			}
			break;
		}

		if (rp_ctx->kcp_restart || rp_ctx->nwm_thread_exit) {
			s32 count;

			if (ret == 0) {
				*frameId = (*frameId + 1) % 2;
				svc_releaseSemaphore(&count, workAvaiSem, 1);
			}

			rpSendDataDiscard();
			rp_ctx->kcp_restart = 0;
			break;
		}

		u8* data;
		u32 size;
		data = send_src[*frameId];
		size = send_len[*frameId];
		*frameId = (*frameId + 1) % 2;

		struct RP_DATA_HEADER header = { 0 };
		struct RP_DATA2_HEADER header2 = { 0 };
		memcpy(&header, data, sizeof(header));
		u8* data2 = data + header.len + sizeof(header);
		// rpDbg("send %d %d %d %d\n", header.flags, header.len + sizeof(header), header.id, header.uncompressed_len);
		ret = header.len;

		if (header.flags & RP_DATA_SELECT_FRAME_DELTA) {
			memcpy(&header2, data2, sizeof(header2));
			// rpDbg("send2 %d %d %d %d\n", header2.flags, header2.len + sizeof(header2), header2.id, header2.uncompressed_len);
			ret += header2.len;
		}

		if (rp_ctx->multicore_encode) {
			if (current_screen == 0) {
				rp_ctx->dyn_prio.top_screen_time += 1.0f / ret;
			} else {
				rp_ctx->dyn_prio.bot_screen_time += 1.0f / ret;
			}
		}

		u64 tickDiff, currentTick;
		u64 sleepValue;
		rp_ctx->last_kcp_send_tick = svc_getSystemTick();

		while (size) {
			if (rp_ctx->kcp_restart || rp_ctx->nwm_thread_exit) {
				break;
			}

			currentTick = svc_getSystemTick();
			tickDiff = currentTick - rp_ctx->last_send_tick;

			if (currentTick - rp_ctx->last_kcp_send_tick > KCP_TIMEOUT_TICKS) {
				break;
			}

			if (tickDiff < rp_ctx->min_send_interval_in_ticks) {
				sleepValue = ((rp_ctx->min_send_interval_in_ticks - tickDiff) * 1000000000) / SYSTICK_PER_SEC;
				svc_sleepThread(sleepValue);
			}

			u32 sendSize = size;
			if (sendSize > KCP_PACKET_SIZE - sizeof(struct RP_PACKET_HEADER)) {
				sendSize = KCP_PACKET_SIZE - sizeof(struct RP_PACKET_HEADER);
			}
			if (data < data2 && data + sendSize > data2) {
				sendSize = data2 - data;
			}

			svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
			int waitsnd = ikcp_waitsnd(rp_ctx->kcp);
			if (waitsnd < KCP_SND_WND_SIZE) {
				struct RP_PACKET_HEADER packet_header;
				packet_header.len = sendSize;
				packet_header.id = packet_header_id++;
				memcpy(rp_ctx->kcp_send_buf, &packet_header, sizeof(struct RP_PACKET_HEADER));
				memcpy(rp_ctx->kcp_send_buf + sizeof(struct RP_PACKET_HEADER), data, sendSize);
				ret = ikcp_send(rp_ctx->kcp, rp_ctx->kcp_send_buf, sendSize + sizeof(struct RP_PACKET_HEADER));

				if (ret < 0) {
					nsDbgPrint("ikcp_send failed: %d\n", ret);
					break;
				}
				size -= sendSize;
				data += sendSize;

				rp_ctx->last_kcp_send_tick = currentTick;
			}
			ikcp_update(rp_ctx->kcp, iclock());
			svc_releaseMutex(rp_ctx->kcp_mutex);

			rp_ctx->last_send_tick = currentTick;
		}

		if (header.flags & RP_DATA_SELECT_FRAME_DELTA) {
			memcpy(&header2, data2, sizeof(header2));
			// rpDbg("send2 post %d %d %d %d\n", header2.flags, header2.len + sizeof(header2), header2.id, header2.uncompressed_len);
		}

		s32 count;
		svc_releaseSemaphore(&count, workAvaiSem, 1);

		if (rp_ctx->kcp_restart || rp_ctx->nwm_thread_exit) {
			rpSendDataDiscard();
			rp_ctx->kcp_restart = 0;
			break;
		}

		if (size) {
			// rpDbg("ikcp_send timeout %d\n", (u32)rp_ctx->last_kcp_send_tick);
			rp_ctx->force_key[0] = 1;
			rp_ctx->force_key[1] = 1;
			break;
		}
	}

	svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
	ikcp_release(rp_ctx->kcp);
	rp_ctx->kcp = 0;
	svc_releaseMutex(rp_ctx->kcp_mutex);

	if (!rp_ctx->nwm_thread_exit) {
		svc_sleepThread(100000000);
	}
}

void rpSendDataThread(u32 arg) {
	while (!rp_ctx->nwm_thread_exit) {
		rpSendDataThreadMain();
	}

	// rpDbg("rpSendDataThread exit\n");
	svc_exitThread();
}

static int rpTestCompressAndSend(int top_bot, struct RP_DATA_HEADER data_header, COMPRESS_CONTEXT* cctx) {
	Handle workAvaiSem, workDoneSem;
	struct huffman_alloc_s *alloc;
	top_bot = rp_ctx->multicore_encode ? top_bot : 0;
	workAvaiSem = rp_ctx->enc_avai_sem[top_bot];
	workDoneSem = rp_ctx->enc_done_sem[top_bot];
	alloc = &rp_ctx->hr_alloc[top_bot];

	svc_waitSynchronization1(workAvaiSem, U64_MAX);
	u8* dst;
	u32 dst_offset;
	if (top_bot == 0) {
		dst_offset = HR_DST_SIZE_TOP;
		dst = rp_ctx->hr_dst_top[rp_ctx->hr_frame_id[top_bot]];
	} else {
		dst_offset = HR_DST_SIZE_BOT;
		dst = rp_ctx->hr_dst_bot[rp_ctx->hr_frame_id[top_bot]];
	}
	u8* huffman_dst = dst + sizeof(struct RP_DATA_HEADER);
	uint32_t* counts = huffman_len_table(alloc, huffman_dst, cctx->data, cctx->data_size);
	int huffman_size = huffman_compressed_size(counts, huffman_dst) + 256;
	int dst_size = huffman_size;
	int huffman = 1;
	if (huffman_size > cctx->data_size) {
		memcpy(huffman_dst, cctx->data, cctx->data_size);
		dst_size = huffman_size = cctx->data_size;
		huffman = 0;
		data_header.flags &= ~RP_DATA_HUFFMAN;
	}
	if (huffman_size > cctx->max_compressed_size) {
		rpDbg("Exceed bandwidth budget at %d (%d available)\n", huffman_size, cctx->max_compressed_size);
		s32 count;
		svc_releaseSemaphore(&count, workAvaiSem, 1);
		return -1;
	}
	if (rp_ctx->cfg.flags & RP_RLE_ENCODE) {
		dst_size += rle_max_compressed_size(huffman_size);
	}
	dst_size += sizeof(struct RP_DATA_HEADER);
	if (cctx->data2 && cctx->data2_size) {
		dst_size += cctx->data2_size + rle_max_compressed_size(cctx->data2_size) + sizeof(struct RP_DATA2_HEADER);
	}
	if (dst_size > dst_offset) {
		nsDbgPrint("Not enough memory for compression: need %d (%d available)\n", dst_size, dst_offset);
		s32 count;
		svc_releaseSemaphore(&count, workAvaiSem, 1);
		return -1;
	}
	if (huffman) {
		huffman_size = huffman_encode_with_len_table(alloc, counts, huffman_dst, cctx->data, cctx->data_size);
		data_header.flags |= RP_DATA_HUFFMAN;
	}
	u8* rle_dst = huffman_dst + huffman_size;
	int rle_size = INT_MAX;
	if (rp_ctx->cfg.flags & RP_RLE_ENCODE) {
		rle_size = rle_encode(rle_dst, huffman_dst, huffman_size);
		// nsDbgPrint("Huffman %d RLE %d from %d", huffman_size, rle_size, cctx->data_size);
	} else {
		// nsDbgPrint("Huffman %d from %d", huffman_size, cctx->data_size);
	}

	u8 *dh_dst, *dh_dst_end;
	if (rle_size < huffman_size) {
		data_header.flags |= RP_DATA_RLE;
		data_header.len = rle_size;
		dh_dst = rle_dst - sizeof(data_header);
	} else {
		data_header.flags &= ~RP_DATA_RLE;
		data_header.len = huffman_size;
		dh_dst = huffman_dst - sizeof(data_header);
	}
	dh_dst_end = dh_dst + sizeof(data_header) + data_header.len;
	if (cctx->data2 && cctx->data2_size) {
		struct RP_DATA2_HEADER data2_header = {0};
		data2_header.id = rp_ctx->data_header_id[top_bot][1]++;
		data2_header.uncompressed_len = cctx->data2_size;
		u8* dst2 = dh_dst_end + sizeof(data2_header);
		huffman_dst = dst2;
		huffman_size = huffman_encode(alloc, huffman_dst, cctx->data2, cctx->data2_size);
		if (huffman_size < cctx->data2_size) {
			rle_dst = huffman_dst + huffman_size;
			rle_size = rle_encode(rle_dst, huffman_dst, huffman_size);
			if (rle_size < huffman_size) {
				memcpy(huffman_dst, rle_dst, rle_size);
				data2_header.flags |= RP_DATA2_RLE;
				data2_header.len = rle_size;
			} else {
				data2_header.len = huffman_size;
			}
			data2_header.flags |= RP_DATA2_HUFFMAN;
		} else {
			rle_dst = dst2;
			rle_size = rle_encode(rle_dst, cctx->data2, cctx->data2_size);
			if (rle_size < cctx->data2_size) {
				data2_header.flags |= RP_DATA2_RLE;
				data2_header.len = rle_size;
			} else {
				memcpy(dst2, cctx->data2, cctx->data2_size);
				data2_header.len = cctx->data2_size;
			}
		}
		memcpy(dh_dst_end, &data2_header, sizeof(data2_header));
		dh_dst_end += data2_header.len + sizeof(data2_header);
	}
	// rpDbg("frame send %d %d\n", data_header.flags, data_header.len  + sizeof(data_header));
	data_header.id = rp_ctx->data_header_id[top_bot][0]++;
	data_header.uncompressed_len = cctx->data_size;
	memcpy(dh_dst, &data_header, sizeof(data_header));
	dst_size = dh_dst_end - dh_dst;
	rpSendData(top_bot, dh_dst, dst_size);
	s32 count;
	svc_releaseSemaphore(&count, workDoneSem, 1);
	return dst_size;
}

static void rpControlRecv(void) {
	if (!rp_control_ready) {
		svc_sleepThread(100000000);
		return;
	}

	int ret = recv(rp_recv_sock, rp_ctx->recv_buf, RP_CONTROL_RECV_BUF_SIZE, 0);
	if (ret == 0) {
		nsDbgPrint("rpControlRecv nothing\n");
		return;
	} else if (ret < 0) {
		int err = SOC_GetErrno();
		nsDbgPrint("rpControlRecv failed: %d\n", ret);
		return;
	}

	svc_waitSynchronization1(rp_ctx->kcp_mutex, U64_MAX);
	if (rp_ctx->kcp) {
		int bufSize = ret;
		if ((ret = ikcp_input(rp_ctx->kcp, rp_ctx->recv_buf, bufSize)) < 0) {
			nsDbgPrint("ikcp_input failed: %d\n", ret);
		}
		// ikcp_update(rp_ctx->kcp, iclock());
		ret = ikcp_recv(rp_ctx->kcp, rp_ctx->recv_buf, RP_CONTROL_RECV_BUF_SIZE);
		if (ret >= 0) {
			rpControlRecvHandle(rp_ctx->recv_buf, ret);
		}
	}
	svc_releaseMutex(rp_ctx->kcp_mutex);
}

static int convertYUVImage(
	int format, int width, int height, int bpp, int bic,
	const u8* sp, u8 *dp_y_out, u8 *dp_u_out, u8 *dp_v_out
) {
	int x, y;
	u8 r, g, b, y_out, u_out, v_out;

	switch (format) {
		// untested
		case 0:
			++sp;

			// fallthru
		case 1: {
			for (x = 0; x < width; ++x) {
				for (y = 0; y < height; ++y) {
					r = sp[2];
					g = sp[1];
					b = sp[0];
					convertYUV(r, g, b, &y_out, &u_out, &v_out);
					*dp_y_out++ = y_out;
					*dp_u_out++ = u_out;
					*dp_v_out++ = v_out;
					sp += bpp;
				}
				sp += bic;
			}
			break;
		}

		case 2: {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					r = ((pix >> 11) & 0x1f) << 3;
					g = ((pix >> 5) & 0x3f) << 2;
					b = (pix & 0x1f) << 3;
					convertYUV(r, g, b, &y_out, &u_out, &v_out);
					*dp_y_out++ = y_out;
					*dp_u_out++ = u_out;
					*dp_v_out++ = v_out;
					sp += bpp;
				}
				sp += bic;
			}
			break;
		}

		// untested
		case 3: {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					r = ((pix >> 11) & 0x1f) << 3;
					g = ((pix >> 6) & 0x1f) << 3;
					b = ((pix >> 1) & 0x1f) << 3;
					convertYUV(r, g, b, &y_out, &u_out, &v_out);
					*dp_y_out++ = y_out;
					*dp_u_out++ = u_out;
					*dp_v_out++ = v_out;
					sp += bpp;
				}
				sp += bic;
			}
			break;
		}

		// untested
		case 4: {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					r = ((pix >> 12) & 0x0f) << 4;
					g = ((pix >> 8) & 0x0f) << 4;
					b = ((pix >> 4) & 0x0f) << 4;
					convertYUV(r, g, b, &y_out, &u_out, &v_out);
					*dp_y_out++ = y_out;
					*dp_u_out++ = u_out;
					*dp_v_out++ = v_out;
					sp += bpp;
				}
				sp += bic;
			}
			break;
		}

		default:
			return -1;
	}
	return 0;
}

static void diffPredSelImage(u8 *dp_fd_im, u8 *dp_im, u8 *dp_im_pf, u8 *dp_p_im, u8 *dp_s_im, u8 *dp_m_im, u8 *dp_m_im_end, int width, int height, u8 sel) {
	differenceImage(dp_fd_im, dp_im, dp_im_pf, width, height);
	if (sel) {
		predictImage(dp_p_im, dp_im, width, height);
		selectImage(dp_s_im, dp_m_im, dp_m_im_end, dp_fd_im, dp_p_im, width, height);
	}
}

static void cctx_data_sel(COMPRESS_CONTEXT *cctx, u8 *dp_s_im, u8 *dp_m_im, int dp_s_im_size, int dp_m_im_size, u8 *dp_fd_im, int sel) {
	if (sel) {
		cctx->data = dp_s_im;
		cctx->data_size = dp_s_im_size;
		cctx->data2 = dp_m_im;
		cctx->data2_size = dp_m_im_size;
	} else {
		cctx->data = dp_fd_im;
		cctx->data_size = dp_s_im_size;
		cctx->data2 = 0;
		cctx->data2_size = 0;
	}
}

static void diffPredSelCctxImage_Y(COMPRESS_CONTEXT *cctx, u8 *dp_fd_im, u8 *dp_im, u8 *dp_im_pf, u8 *dp_p_im,
	u8 *dp_s_im, u8 *dp_m_im, int dp_s_im_size, int dp_m_im_size, int width, int height, u8 sel
) {
	diffPredSelImage(dp_fd_im, dp_im, dp_im_pf, dp_p_im, dp_s_im, dp_m_im, dp_m_im + dp_m_im_size, width, height, sel);
	cctx_data_sel(cctx, dp_s_im, dp_m_im, dp_s_im_size, dp_m_im_size, dp_fd_im, sel);
}

static int downsampleHDiffPredSelCctxImageSend_Y(COMPRESS_CONTEXT *cctx, struct RP_DATA_HEADER *header, int top_bot,
	u8 *dp_ds_fd_im, u8 *dp_ds_im, u8 *dp_im, u8 *dp_ds_im_pf, u8 *dp_ds_p_im,
	u8 *dp_ds_s_im, u8 *dp_ds_m_im, int dp_ds_s_im_size, int dp_ds_m_im_size, int width, int height, u8 sel
) {
	downsampleImageH(dp_ds_im, dp_im, width, height);
	diffPredSelCctxImage_Y(cctx, dp_ds_fd_im, dp_ds_im, dp_ds_im_pf,
		dp_ds_p_im, dp_ds_s_im, dp_ds_m_im, dp_ds_s_im_size, dp_ds_m_im_size,
		width / 2, height, sel);
	cctx->max_compressed_size = UINT32_MAX;
	header->flags |= RP_DATA_DOWNSAMPLE2;
	int ret;
	if ((ret = rpTestCompressAndSend(top_bot, *header, cctx)) < 0) {
		return -1;
	}
	return ret;
}

#define RP_ENC_HAVE_Y (1 << 0)
#define RP_ENC_HAVE_UV (1 << 1)
#define RP_ENC_DS_Y (1 << 2)
#define RP_ENC_DS2_Y (1 << 3)
#define RP_ENC_DS_DS2_UV (1 << 4)
#define RP_ENC_DS2_DS2_UV (1 << 5)

#define I_N top
#define I_WIDTH 400
#define I_HEIGHT 240
#include "enc_main.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

#define I_N bot
#define I_WIDTH 320
#define I_HEIGHT 240
#include "enc_main.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

#define I_N il_top
#define I_WIDTH 400
#define I_HEIGHT 120
#include "enc_main.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

#define I_N il_bot
#define I_WIDTH 320
#define I_HEIGHT 120
#include "enc_main.h"
#undef I_N
#undef I_WIDTH
#undef I_HEIGHT

static int remotePlayBlitCompressAndSend(BLIT_CONTEXT* ctx) {
	u8 top_bot = ctx->top_bot;
	s8 interlace = rp_ctx->interlace;
	u8 id = ctx->id;
	u8 frame_id = interlace == 0 ? id % 2 : (id / 2) % 2;
	u8 even_odd = interlace == 0 ? 0 : id % 2;

	struct RP_DATA_HEADER data_header = {0};
	if (top_bot == 0) {
		data_header.flags &= ~RP_DATA_TOP_BOT;
	} else {
		data_header.flags |= RP_DATA_TOP_BOT;
	}

	if (rp_ctx->cfg.flags & RP_YUV_LQ) {
		data_header.flags |= RP_DATA_YUV_LQ;
	} else {
		data_header.flags &= ~RP_DATA_YUV_LQ;
	}

	if (rp_ctx->interlace) {
		data_header.flags |= RP_DATA_INTERLACE;
		if (even_odd) {
			data_header.flags &= ~RP_DATA_INTERLACE_EVEN_ODD;
		} else {
			data_header.flags |= RP_DATA_INTERLACE_EVEN_ODD;
		}
	} else {
		data_header.flags &= ~RP_DATA_INTERLACE;
		data_header.flags &= ~RP_DATA_INTERLACE_EVEN_ODD;
	}

	if (interlace == 0) {
		if (top_bot == 0) {
			struct RP_ENC_CTX_top enc_ctx = {0};
			enc_ctx.bctx = ctx;
			enc_ctx.enc = &rp_ctx->c.c.top;
			enc_ctx.fd = &rp_ctx->fd_c.c.fd_top[frame_id];
			enc_ctx.fd_pf = &rp_ctx->fd_c.c.fd_top[!frame_id];
			return rp_enc_main_top(&enc_ctx, data_header);
		} else {
			struct RP_ENC_CTX_bot enc_ctx = {0};
			enc_ctx.bctx = ctx;
			enc_ctx.enc = &rp_ctx->c.c.bot;
			enc_ctx.fd = &rp_ctx->fd_c.c.fd_bot[frame_id];
			enc_ctx.fd_pf = &rp_ctx->fd_c.c.fd_bot[!frame_id];
			return rp_enc_main_bot(&enc_ctx, data_header);
		}
	} else {
		if (top_bot == 0) {
			struct RP_ENC_CTX_il_top enc_ctx = {0};
			enc_ctx.bctx = ctx;
			enc_ctx.enc = &rp_ctx->c.il_c.top;
			enc_ctx.fd = &rp_ctx->fd_c.il_c[even_odd].fd_top[frame_id];
			enc_ctx.fd_pf = &rp_ctx->fd_c.il_c[even_odd].fd_top[!frame_id];
			return rp_enc_main_il_top(&enc_ctx, data_header);
		} else {
			struct RP_ENC_CTX_il_bot enc_ctx = {0};
			enc_ctx.bctx = ctx;
			enc_ctx.enc = &rp_ctx->c.il_c.bot;
			enc_ctx.fd = &rp_ctx->fd_c.il_c[even_odd].fd_bot[frame_id];
			enc_ctx.fd_pf = &rp_ctx->fd_c.il_c[even_odd].fd_bot[!frame_id];
			return rp_enc_main_il_bot(&enc_ctx, data_header);
		}
	}

	return -1;
}


void remotePlayKernelCallback(int top_bot) {
	u32 ret;
	u32 fbP2VOffset = 0xc0000000;
	u32 current_fb;

	if (top_bot == 0) {
		rp_ctx->fb_addr[top_bot][0] = REG(IoBasePdc + 0x468);
		rp_ctx->fb_addr[top_bot][1] = REG(IoBasePdc + 0x46c);
		rp_ctx->fb_format[top_bot] = REG(IoBasePdc + 0x470);
		rp_ctx->fb_pitch[top_bot] = REG(IoBasePdc + 0x490);
		current_fb = REG(IoBasePdc + 0x478);
	} else {
		rp_ctx->fb_addr[top_bot][0] = REG(IoBasePdc + 0x568);
		rp_ctx->fb_addr[top_bot][1] = REG(IoBasePdc + 0x56c);
		rp_ctx->fb_format[top_bot] = REG(IoBasePdc + 0x570);
		rp_ctx->fb_pitch[top_bot] = REG(IoBasePdc + 0x590);
		current_fb = REG(IoBasePdc + 0x578);
	}
	current_fb &= 1;
	rp_ctx->fb_current[top_bot] = rp_ctx->fb_addr[top_bot][current_fb];
}


static Handle rpHDma[2], rpHandleHome, rpHandleGame;
static u32 rpGameFCRAMBase = 0;

void rpInitDmaHome(void) {
	u32 dmaConfig[20] = { 0 };
	svc_openProcess(&rpHandleHome, 0xf);

}

Handle rpGetGameHandle(void) {
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

static int rpCaptureScreen(int top_bot) {

	u8 dmaConfig[80] = { 0, 0, 4 };
	u32 bufSize = rp_ctx->fb_pitch[top_bot] * (top_bot == 0 ? 400 : 320);
	u32 BUF_SIZE_MAX = top_bot == 0 ? RP_IMG_BUF_TOP_SIZE : RP_IMG_BUF_BOT_SIZE;

	if (bufSize > BUF_SIZE_MAX) {
		nsDbgPrint("size exceed screen capture buffer %d\n", bufSize);
		svc_sleepThread(1000000000);
		return -1;
	}

	u32 phys = rp_ctx->fb_current[top_bot];
	u32 dest = (u32)(top_bot == 0 ? rp_ctx->img_buf_top : rp_ctx->img_buf_bot);
	Handle hProcess = rpHandleHome;

	int ret;

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)dest, bufSize);
	svc_closeHandle(rpHDma[top_bot]);
	rpHDma[top_bot] = 0;

	if (isInVRAM(phys)) {
		svc_startInterProcessDma(&rpHDma[top_bot], CURRENT_PROCESS_HANDLE,
			(void *)dest, hProcess, (void *)(0x1F000000 + (phys - 0x18000000)), bufSize, (u32 *)dmaConfig);
		return bufSize;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			ret = svc_startInterProcessDma(&rpHDma[top_bot], CURRENT_PROCESS_HANDLE,
				(void *)dest, hProcess, (void *)(rpGameFCRAMBase + (phys - 0x20000000)), bufSize, (u32 *)dmaConfig);

		}
		return bufSize;
	}
	svc_sleepThread(1000000000);
	return -1;
}

#define KCP_BANDWIDTH_FACTOR 2
void updateNetworkParams(void) {
	rp_ctx->network_params.targetBitsPerSec = rp_ctx->cfg.qos;
	rp_ctx->network_params.targetFrameRate = HR_MAX(HR_MIN(rp_ctx->cfg.quality, 120), 15);

	rp_ctx->network_params.bitsPerFrame =
		(u64)rp_ctx->network_params.targetBitsPerSec / rp_ctx->network_params.targetFrameRate;
	rp_ctx->min_send_interval_in_ticks = (u64)SYSTICK_PER_SEC * PACKET_SIZE * 8 * KCP_BANDWIDTH_FACTOR / rp_ctx->cfg.qos;
	rp_ctx->network_params.bitsPerY = rp_ctx->network_params.bitsPerFrame * 2 / 3;
	rp_ctx->network_params.bitsPerUV = rp_ctx->network_params.bitsPerFrame / 3;
	rpDbg(
		"network params: target bitrate %d, target frame rate %d, bits per frame %d, bits per y %d, bits per uv %d\n",
		rp_ctx->network_params.targetBitsPerSec, rp_ctx->network_params.targetFrameRate,
		rp_ctx->network_params.bitsPerFrame, rp_ctx->network_params.bitsPerY, rp_ctx->network_params.bitsPerUV);
}

static int rpBlitEncodeAndSend(BLIT_CONTEXT *ctx, int top_bot, int src_size) {
	int ret;

	if (src_size < 0) {
		return -1;
	}

	ctx->top_bot = top_bot;

	if (rp_ctx->force_key[top_bot]) {
		rp_ctx->force_key[top_bot] = 0;
		ctx->force_key = 1;
	}

	int width, height;
	if (top_bot == 0) {
		width = 400;
		height = 240;
	}
	else {
		width = 320;
		height = 240;
	}
	remotePlayBlitInit(ctx, width, height,
		rp_ctx->fb_format[top_bot], rp_ctx->fb_pitch[top_bot],
		top_bot == 0 ? rp_ctx->img_buf_top : rp_ctx->img_buf_bot, src_size);
	ret = remotePlayBlitCompressAndSend(ctx);
	if (ret < 0) {
		ctx->force_key = 1;
	}

	return ret;
}

// static int rp_ctx->enc_thread[1]SrcSize;
static void remotePlayThread2Start(u32 arg) {
	BLIT_CONTEXT blit_ctx[2] = { 0 };
	blit_ctx[0].force_key = 1;
	blit_ctx[1].force_key = 1;
	// only update non priority screen on core 3
	u32 current_screen = !rp_ctx->priority_screen;
	int ret;

	while (!rp_ctx->enc_thread_exit) {
		ret = svc_waitSynchronization1(rp_ctx->tr2_avai_sem, 100000000);
		if (ret != 0) {
			continue;
		}

		// remotePlayKernelCallback(current_screen);
		// kRemotePlayCallback(current_screen);
		int src_size = rpCaptureScreen(current_screen);
		rpBlitEncodeAndSend(&blit_ctx[current_screen], current_screen, src_size);

		s32 count;
		svc_releaseSemaphore(&count, rp_ctx->tr2_done_sem, 1);
	}

	svc_exitThread();
}

static void remotePlayThread2Transfer(u32 arg) {
	u32 current_screen = !rp_ctx->priority_screen;
	int ret;

	while (!rp_ctx->enc_thread_exit) {
		ret = svc_waitSynchronization1(rp_ctx->tr2_done_sem, 100000000);
		if (ret != 0) {
			continue;
		}

		remotePlayKernelCallback(current_screen);
		// kRemotePlayCallback(current_screen);
		// rp_ctx->tr2_src_size = rpCaptureScreen(current_screen);

		s32 count;
		svc_releaseSemaphore(&count, rp_ctx->tr2_avai_sem, 1);
	}
	svc_exitThread();
}

void remotePlaySendFrames(void) {
	rp_ctx->priority_screen = 0;
	u32 mode = (rp_ctx->cfg.mode & 0xff00) >> 8;
	u32 factor = (rp_ctx->cfg.mode & 0xff);
	if (mode == 1) {
		rp_ctx->priority_screen = 1;
	}
	rp_ctx->priority_factor = HR_MAX(HR_MIN(factor, 15), 0);
	rp_ctx->dynamic_priority = !!(rp_ctx->cfg.flags & RP_DYNAMIC_PRIORITY);
	if (!rp_ctx->priority_factor) {
		rp_ctx->dynamic_priority = 0;
	}
	rp_ctx->cfg.qos = HR_MIN(HR_MAX(rp_ctx->cfg.qos, 1024 * 512 * 3), 1024 * 512 * 36);
	rp_ctx->interlace = !!(rp_ctx->cfg.flags & RP_INTERLACE);
	updateNetworkParams();

	rpDbg(
		"remote play config: priority top %d, priority factor %d"
		"%s%s%s%s%s%s%s%s%s%s\n",
		rp_ctx->priority_screen, rp_ctx->priority_factor,
		(rp_ctx->cfg.flags & RP_FRAME_DELTA) ? ", use frame delta" : "",
		(rp_ctx->cfg.flags & RP_TRIPLE_BUFFER_ENCODE) ? ", triple buffer encode" : "",
		(rp_ctx->cfg.flags & RP_SELECT_PREDICTION) ? ", select prediction" : "",
		(rp_ctx->cfg.flags & RP_DYNAMIC_DOWNSAMPLE) ? ", use dynamic encode" : "",
		(rp_ctx->cfg.flags & RP_RLE_ENCODE) ? ", use rle encode" : "",
		(rp_ctx->cfg.flags & RP_YUV_LQ) ? ", low quality image" : "",
		(rp_ctx->cfg.flags & RP_DYNAMIC_PRIORITY) ? ", dynamic priority" : "",
		(rp_ctx->cfg.flags & RP_INTERLACE) ? ", interlaced video" : "",
		(rp_ctx->cfg.flags & RP_MULTICORE_NETWORK) ? ", multicore network" : "",
		(rp_ctx->cfg.flags & RP_MULTICORE_ENCODE) ? ", multicore encode" : "");

	rp_ctx->kcp_restart = 1;
	// svc_flushProcessDataCache(rp_ctx->send_thread, (u32)&rp_ctx->kcp_restart, sizeof(rp_ctx->kcp_restart));
	__dmb();

	u32 current_screen = rp_ctx->priority_screen;
	memset(&rp_ctx->dyn_prio, 0, sizeof(rp_ctx->dyn_prio));
	u8 cnt;
	BLIT_CONTEXT blit_ctx[2] = { 0 };
	blit_ctx[0].force_key = 1;
	blit_ctx[1].force_key = 1;
	u64 currentTick = 0;
	int src_size;
	int ret;

	// u32 rp_ctx->fb_pitch[0]_max = 0;
	// u32 rp_ctx->fb_pitch[1]_max = 0;

	if (rp_ctx->multicore_encode) {
		rp_ctx->enc_thread_exit = 0;
		ret = svc_createThread(&rp_ctx->enc_thread[1], remotePlayThread2Start, 0, (u32 *)&rp_ctx->enc_thread_stack[1][RP_stackSize - 40], 0x10, 3);
		if (ret != 0) {
			nsDbgPrint("Create remotePlayThread2Start Thread Failed: %08x\n", ret);
			return;
		}

		ret = svc_createThread(&rp_ctx->tr2_thread, remotePlayThread2Transfer, 0, (u32 *)&rp_ctx->tr2_thread_stack[RP_dataStackSize - 40], 0x8, 2);
		if (ret != 0) {
			nsDbgPrint("Create remotePlayThread2Transfer Thread Failed: %08x\n", ret);

			rp_ctx->enc_thread_exit = 1;
			// svc_flushProcessDataCache(rp_ctx->enc_thread[1], (u32)&rp_ctx->enc_thread_exit, sizeof(rp_ctx->enc_thread_exit));
			__dmb();
			svc_waitSynchronization1(rp_ctx->enc_thread[1], U64_MAX);
			return;
		}
	}

	while (1) {
		if (!rp_ctx->multicore_encode) {
			current_screen = rpDynPrioGetScreen();
		}

		remotePlayKernelCallback(current_screen);
		// kRemotePlayCallback(current_screen);
		src_size = rpCaptureScreen(current_screen);
		ret = rpBlitEncodeAndSend(&blit_ctx[current_screen], current_screen, src_size);

		if (ret > 0) {
			if (current_screen == 0) {
				rp_ctx->dyn_prio.top_screen_time += 1.0f / ret;
			} else {
				rp_ctx->dyn_prio.bot_screen_time += 1.0f / ret;
			}
		}

/*
#define SEND_STAT_EVERY_X_FRAMES 64
		if (frameCount % SEND_STAT_EVERY_X_FRAMES == 0) {
			u64 nextTick = svc_getSystemTick();
			if (currentTick) {
				u32 ms = (nextTick - currentTick) / 1000 / SYSTICK_PER_US;
				nsDbgPrint("%d ms for %d frames\n", ms, SEND_STAT_EVERY_X_FRAMES);
				// rp_ctx->fb_pitch[0]_max = rp_ctx->fb_pitch[1]_max = 0;
			}
			currentTick = nextTick;
		}
*/

		if (g_nsConfig->rpConfig.control == 1) {
			g_nsConfig->rpConfig.control = 0;
			rp_ctx->kcp_restart = 1;
			// svc_flushProcessDataCache(rp_ctx->send_thread, (u32)&rp_ctx->kcp_restart, sizeof(rp_ctx->kcp_restart));
			__dmb();
			break;
		}
	}

	if (rp_ctx->multicore_encode) {
		rp_ctx->enc_thread_exit = 1;
		// svc_flushProcessDataCache(rp_ctx->enc_thread[1], (u32)&rp_ctx->enc_thread_exit, sizeof(rp_ctx->enc_thread_exit));
		// svc_flushProcessDataCache(rp_ctx->tr2_thread, (u32)&rp_ctx->enc_thread_exit, sizeof(rp_ctx->enc_thread_exit));
		__dmb();
		svc_waitSynchronization1(rp_ctx->enc_thread[1], U64_MAX);
		svc_waitSynchronization1(rp_ctx->tr2_thread, U64_MAX);
		svc_closeHandle(rp_ctx->enc_thread[1]);
		svc_closeHandle(rp_ctx->tr2_thread);
	}

	// rpDbg("remotePlaySendFrames exit\n");
}

void remotePlayThreadStart(u32 arg) {
	// nsDbgPrint("remotePlayBuffer: { %x, %x, %x, %x, %x, %x, %x, %x, %x, %x, %x, %x }\n",
	// 	*(u32 *)remotePlayBuffer,
	// 	*(u32 *)(remotePlayBuffer + 0x04),
	// 	*(u32 *)(remotePlayBuffer + 0x08),
	// 	*(u32 *)(remotePlayBuffer + 0x0c),
	// 	*(u32 *)(remotePlayBuffer + 0x10),
	// 	*(u32 *)(remotePlayBuffer + 0x14),
	// 	*(u32 *)(remotePlayBuffer + 0x18),
	// 	*(u32 *)(remotePlayBuffer + 0x1c),
	// 	*(u32 *)(remotePlayBuffer + 0x20),
	// 	*(u32 *)(remotePlayBuffer + 0x24),
	// 	*(u32 *)(remotePlayBuffer + 0x28),
	// 	*(u32 *)(remotePlayBuffer + 0x2c));

	// nsDbgPrint("imgBuffer: %08x\n", imgBuffer);
	rpInitDmaHome();
	// kRemotePlayCallback();
	int ret;

	svc_createSemaphore(&rp_ctx->enc_done_sem[0], 0, 2);
	svc_createSemaphore(&rp_ctx->enc_avai_sem[0], 2, 2);

	svc_createSemaphore(&rp_ctx->enc_done_sem[1], 0, 2);
	svc_createSemaphore(&rp_ctx->enc_avai_sem[1], 2, 2);

	svc_createSemaphore(&rp_ctx->tr2_avai_sem, 0, 1);
	svc_createSemaphore(&rp_ctx->tr2_done_sem, 1, 1);

	svc_createMutex(&rp_ctx->kcp_mutex, 0);

	rp_control_ready = 1;

	while (1) {
		rp_ctx->cfg = g_nsConfig->rpConfig;

		rp_ctx->nwm_thread_exit = 0;
		rp_ctx->multicore_encode = !!(rp_ctx->cfg.flags & RP_MULTICORE_ENCODE);
		rp_ctx->triple_buffer_encode = !!(rp_ctx->cfg.flags & RP_TRIPLE_BUFFER_ENCODE);
		ret = svc_createThread(&rp_ctx->send_thread, rpSendDataThread, 0, (u32 *)&rp_ctx->send_thread_stack[RP_dataStackSize - 40], 0x8, rp_ctx->cfg.flags & RP_MULTICORE_NETWORK ? 3 : 2);
		if (ret != 0) {
			nsDbgPrint("Create rpSendDataThread Failed: %08x\n", ret);
			goto final;
		}
		remotePlaySendFrames();
		rp_ctx->nwm_thread_exit = 1;
		// svc_flushProcessDataCache(rp_ctx->send_thread, (u32)&rp_ctx->nwm_thread_exit, sizeof(rp_ctx->nwm_thread_exit));
		__dmb();
		svc_waitSynchronization1(rp_ctx->send_thread, U64_MAX);
		svc_closeHandle(rp_ctx->send_thread);

		svc_sleepThread(250000000);
	}
	final:

	svc_exitThread();
}

int nwmValParamCallback(u8* buf, int buflen) {
	//rtDisableHook(&nwmValParamHook);
	int i;
	int ret;
	/*
	if (buf[31] != 6) {
	nsDbgPrint("buflen: %d\n", buflen);
	for (i = 0; i < buflen; i++) {
	nsDbgPrint("%02x ", buf[i]);
	}
	}*/

	if (remotePlayInited) {
		return 0;
	}
	if (buf[0x17 + 0x8] == 6) {
		if ((*(u16*)(&buf[0x22 + 0x8])) == 0x401f) {  // src port 8000
			remotePlayInited = 1;

			if (sizeof(*rp_ctx) > (RP_IMG_BUFFER_SIZE)) {
				nsDbgPrint("imgBuffer too small\n");
				return 0;
			}
			rp_ctx = (void *)plgRequestMemory(RP_IMG_BUFFER_SIZE);
			if (!rp_ctx) {
				nsDbgPrint("imgBuffer alloc failed\n");
				return 0;
			}
			nsDbgPrint("imgBuffer %x\n", rp_ctx);
			memset(rp_ctx, 0, RP_IMG_BUFFER_SIZE);
			memcpy(rp_ctx->nwm_send_buf, buf, NWM_HEADER_SIZE);
			umm_init_heap(rp_ctx->umm_malloc_heap_addr, UMM_HEAP_SIZE);
			ikcp_allocator(umm_malloc, umm_free);

			ret = svc_createThread(&rp_ctx->enc_thread[0], remotePlayThreadStart, 0, (u32 *)&rp_ctx->enc_thread_stack[0][RP_stackSize - 40], 0x10, 2);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Thread Failed: %08x\n", ret);
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



int nsIsRemotePlayStarted = 0;

/*
void tickTest(void) {
	svc_sleepThread(1000000000);
	u32 time1 = svc_getSystemTick();
	svc_sleepThread(1000000000);
	u32 time2 = svc_getSystemTick();
	nsDbgPrint("%08x, %08x\n", time1, time2);
}
*/

static void nsRemotePlayControl(RP_CONFIG *rp_cfg) {
	Handle hProcess;
	u32 pid = 0x1a;
	u32 control;

	int ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		hProcess = 0;
		return;
	}

	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfig),
		0xffff8001,
		rp_cfg,
		sizeof(*rp_cfg));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory failed (1) : %08x\n", ret, 0);
		svc_closeHandle(hProcess);
		return;
	}

	control = 1;
	ret = copyRemoteMemory(
		hProcess,
		(u8 *)NS_CONFIGURE_ADDR + offsetof(NS_CONFIG, rpConfig) + offsetof(RP_CONFIG, control),
		0xffff8001,
		&control,
		sizeof(control));
	if (ret != 0) {
		nsDbgPrint("copyRemoteMemory failed (2) : %08x\n", ret, 0);
	}

	svc_closeHandle(hProcess);
}

int nsHandleRemotePlay(void) {
#ifndef HAS_HUFFMAN_RLE
	nsDbgPrint("Remote play not enabled in this build\n");
	return;
#endif

	NS_PACKET* pac = &(g_nsCtx->packetBuf);

	RP_CONFIG rp_cfg = { 0 };
	rp_cfg.mode = pac->args[0];
	u32 rp_magic = pac->args[1];
	rp_cfg.qos = pac->args[2];
	rp_cfg.flags = pac->args[3];
	rp_cfg.flags |= RP_FLAG_DBG;
	rp_cfg.quality = pac->args[4];
	rp_cfg.control = 0;

	if ((rp_magic >= 10) && (rp_magic <= 100)) {
		nsDbgPrint("JPEG encoder not supported in this build\n");
		goto final;
	} else if (rp_magic != RP_MAGIC) {
		nsDbgPrint("Illegal params. Please update your NTR viewer client\n");
		goto final;
	}

	if (nsIsRemotePlayStarted) {
		nsDbgPrint("remote play already started, updating params\n");
		nsRemotePlayControl(&rp_cfg);
		goto final;
	}
	nsIsRemotePlayStarted = 1;

	Handle hProcess;
	u32 ret;
	u32 pid = 0x1a;
	u32 remotePC = 0x001231d0;
	NS_CONFIG	cfg;

	memset(&cfg, 0, sizeof(NS_CONFIG));
	cfg.startupCommand = NS_STARTCMD_DEBUG;
	cfg.rpConfig = rp_cfg;
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
		sai.sin_port = htons(REMOTE_PLAY_PORT);
		sai.sin_addr.s_addr = htonl(INADDR_ANY);

		int ret = bind(rp_recv_sock, (struct sockaddr *)&sai, sizeof(sai));
		if (ret < 0) {
			showDbg("rp_recv_sock bind failed: %d\n", ret, 0);
			disp(100, 0x1ffff00);
			return;
		}
	}

#define RP_PROCESS_SELECT(s) \
	if (rpProcess) { \
		FD_ZERO(&rset); \
		FD_SET(s, &rset); \
		FD_SET(rp_recv_sock, &rset); \
		maxfdp1 = HR_MAX(s, rp_recv_sock) + 1; \
		nready = select2(maxfdp1, &rset, NULL, NULL, NULL); \
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
				RP_PROCESS_SELECT(sockfd) {
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