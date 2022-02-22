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

u32 heapStart, heapEnd;


void doSendDelay(u32 time) {
	vu32 i;
	for (i = 0; i < time; i++) {

	}
}

void tje_log(char* str) {
	nsDbgPrint("tje: %s\n", str);
}

#define RP_MODE_TOP_BOT_10X 0
#define RP_MODE_TOP_BOT_5X 1
#define RP_MODE_TOP_BOT_1X 2
#define RP_MODE_BOT_TOP_5X 3
#define RP_MODE_BOT_TOP_10X 4
#define RP_MODE_TOP_ONLY 5
#define RP_MODE_BOT_ONLY 6
#define RP_MODE_3D 10

u8* rpAllocBuff = 0;
// u32 rpAllocBuffOffset = 0;
// u32 rpAllocBuffRemainSize = 0;
RP_CONFIG rpConfig;
int rpAllocDebug = 0;
u64 rpMinIntervalBetweenPacketsInTick = 0;
int rpControlSocket = -1;

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC 268123480

/*
extern u8* dbgBuf;
void rpSendString(u32 size);
void rpDbg(const char* fmt, ...);
*/

/*
void*  rpMalloc( u32 size)

{
	void* ret = rpAllocBuff + rpAllocBuffOffset;
	u32 totalSize = size;
	if (totalSize % 32 != 0) {
		totalSize += 32 - (totalSize % 32);
	}
	if (rpAllocBuffRemainSize < totalSize) {
		xsprintf(dataBuf, "bad alloc,  size: %d\n", size);
		rpSendString(strlen(dataBuf));

		nsDbgPrint("bad alloc,  size: %d\n", size);
		if (rpAllocDebug) {
			showDbg("bad alloc,  size: %d\n", size, 0);
		}
		return 0;
	}
	rpAllocBuffOffset += totalSize;
	rpAllocBuffRemainSize -= totalSize;
	// memset(ret, 0, totalSize);
	nsDbgPrint("alloc size: %d, ptr: %08x\n", size, ret);
	if (rpAllocDebug) {
		showDbg("alloc size: %d, ptr: %08x\n", size, ret);
	}
	return ret;
}

void  rpFree(void* ptr)
{
	nsDbgPrint("free: %08x\n", ptr);
	return;
}
*/


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

extern u8 *image_buf;
void allocImageBuf();

/*
void remotePlayMain2(void) {
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

#define PACKET_SIZE 1448
#define NWM_HEADER_SIZE (0x2a + 8)
#define RP_PACKET_SIZE (PACKET_SIZE + NWM_HEADER_SIZE)
int remotePlayInited = 0;
u8 *rpSendBufs;
u8* rpThreadStack;
u8* rpDataThreadStack;
u8* umm_malloc_heap_addr;
u8* rpWorkBufferEnd;
LightSemaphore rpWorkDoneSem;
LightSemaphore rpWorkAvaiSem;
LightLock rpControlLock;
// u8 rpDbgBuffer[2000] = { 0 };
// u8* dbgBuf = rpDbgBuffer + 0x2a + 8;
u8* rpImgBuffer;
#define HR_WORK_BUFFER_COUNT 2
u8* rp_work_dst[HR_WORK_BUFFER_COUNT];
u8* rp_send_src[HR_WORK_BUFFER_COUNT];
// int topFormat = 0, bottomFormat = 0;
// int frameSkipA = 1, frameSkipB = 1;
// u32 requireUpdateBottom = 0;
u32 currentTopId = 0;
u32 currentBottomId = 0;

static u32 tl_fbaddr[2];
static u32 bl_fbaddr[2];
static u32 tl_format, bl_format;
static u32 tl_pitch, bl_pitch;
u32 tl_current, bl_current;


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
	int width, height, format, src_pitch;
	int x, y;
	u8* src;
	int src_size;
	int isKey, force_key;
	int outformat, bpp;
	u32 bytesInColumn ;
	u32 blankInColumn;

	u8* transformDst; // Y
	// u8* transformDst2; // UV
	// u8* transformDst3; // YUV mask for frame delta
	// u32 dst_size;
	// u32 dst2_size;
	// u32 dst3_size;
	// u8* compressDst;
	// u32 compressedSize;

	u8 id;
	u8 isTop;
	u8 frameCount;
	u32 lastSize;

	// int reset;
	// int directCompress;
} BLIT_CONTEXT;
#define RP_TRANSFORM_DST_OFFSET 0x001a0000

static inline void remotePlayBlitInit(BLIT_CONTEXT* ctx, int width, int height, int format, int src_pitch, u8* src, int src_size) {

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
	ctx->format = format;
	ctx->width = width;
	ctx->height = height;
	ctx->src_pitch = src_pitch;
	ctx->src = src;
	ctx->src_size = HR_MAX(src_size, 0);
	ctx->x = 0;
	ctx->y = 0;
	if (ctx->bpp == 2) {
		ctx->outformat = format;
	}
	else {
		ctx->outformat = GL_RGB565_LE;
	}
	// ctx->compressDst = 0;
	// ctx->compressedSize;
	ctx->frameCount = 0;
	ctx->lastSize = 0;
	ctx->isKey = !(rpConfig.flags & RP_USE_FRAME_DELTA) || ctx->force_key;
	ctx->force_key = 0;
}




static u64 rpLastSendTick = 0;
static u64 rpLastKcpSendTick = 0;
static int kcp_restart = 0;

/*
void rpSendBuffer(u8* buf, u32 size, u32 flag) {
	if (rpAllocDebug) {
		showDbg("sendbuf: %08x, %d", buf, size);
		return;
	}
	vu64 tickDiff;
	vu64 sleepValue;
	while (size) {
		tickDiff = svc_getSystemTick() - rpLastSendTick;
		if (tickDiff < rpMinIntervalBetweenPacketsInTick) {
			sleepValue = ((rpMinIntervalBetweenPacketsInTick - tickDiff) * 1000) / SYSTICK_PER_US;
			svc_sleepThread(sleepValue);
		}
		u32 sendSize = size;
		if (sendSize > (PACKET_SIZE - 4)) {
			sendSize = (PACKET_SIZE - 4);
		}
		size -= sendSize;
		if (size == 0) {
			dataBuf[1] |= flag;
		}
		memcpy(dataBuf + 4, buf, sendSize);
		packetLen = initUDPPacket(sendSize + 4, REMOTE_PLAY_PORT);
		nwmSendPacket(remotePlayBuffer, packetLen);
		buf += sendSize;
		dataBuf[3] += 1;
		rpLastSendTick = svc_getSystemTick();
	}
}
*/

/*
void rpSendString(u32 size) {
	int packetLen = initUDPPacket(rpDbgBuffer, size, RP_DBG_PORT);
	nwmSendPacket(rpDbgBuffer, packetLen);
}

void rpDbg(const char* fmt, ...)
{
	if (!(rpConfig.flags & RP_DEBUG))
		return;

	va_list arp;
	va_start(arp, fmt);
	xvsprintf(dbgBuf, fmt, arp);
	va_end(arp);

	rpSendString(strlen(dbgBuf));
}
*/

void rpDbg(const char* fmt, ...) {
	if (!(rpConfig.flags & RP_DEBUG))
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

static inline void convertYUV(u8 r, u8 g, u8 b, u8 *y_out, u8 *u_out, u8 *v_out) {
	u16 y = 77 * (u16)r + 150 * (u16)g + 29 * (u16)b;
	u16 u = -43 * (u16)r + -84 * (u16)g + 127 * (u16)b;
	u16 v = 127 * (u16)r + -106 * (u16)g + -21 * (u16)b;

	*y_out = (y + 128) >> 8;
	*u_out = ((u + 128) >> 8) + 128;
	*v_out = ((v + 128) >> 8) + 128;
}

static inline u8 accessImageNoCheck(const u8 *image, int x, int y, int w, int h) {
	return image[x * h + y];
}

static inline u8 accessImage(const u8 *image, int x, int y, int w, int h) {
	return accessImageNoCheck(image, HR_MAX(HR_MIN(x, w), 0), HR_MAX(HR_MIN(y, h), 0), w, h);
}

static inline u8 medianOf3_u8(u8 a, u8 b, u8 c) {
	u8 max = a > b ? a : b;
	max = max > c ? max : c;

	u8 min = a < b ? a : b;
	min = min < c ? min : c;

	return a + b + c - max - min;
}

static inline u8 predictPixel(const u8 *image, int x, int y, int w, int h) {
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

static inline void predictImage(u8 *dst, const u8 *src, int w, int h) {
	const u8* src_begin = src;
	for (int i = 0; i < w; ++i) {
		for (int j = 0; j < h; ++j) {
			*dst++ = *src++ - predictPixel(src_begin, i, j, w, h);
		}
	}
}

// x and y are % 2 == 0
static inline u16 accessImageDownsampleUnscaled(const u8 *image, int x, int y, int w, int h) {
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
static inline u8 accessImageDownsample(const u8 *image, int x, int y, int w, int h) {
	return (accessImageDownsampleUnscaled(image, x, y, w, h) + 2) / 4;
}

static inline void downsampleImage(u8 *ds_dst, const u8 *src, int wOrig, int hOrig) {
	int i = 0, j = 0;
	for (; i < wOrig; i += 2) {
		j = 0;
		for (; j < hOrig; j += 2) {
			*ds_dst++ = accessImageDownsample(src, i, j, wOrig, hOrig);
		}
	}
}

static inline u16 accessImageUpsampleUnscaled(const u8 *ds_image, int xOrig, int yOrig, int wOrig, int hOrig) {
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

static inline u16 accessImageUpsample(const u8 *ds_image, int xOrig, int yOrig, int wOrig, int hOrig) {
	return (accessImageUpsampleUnscaled(ds_image, xOrig, yOrig, wOrig, hOrig) + 8) / 16;
}

static inline void upsampleImage(u8 *dst, const u8 *ds_src, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; ++i) {
		j = 0;
		for (; j < h; ++j) {
			*dst++ = accessImageUpsample(ds_src, i, j, w, h);
		}
	}
}

static inline void differenceImage(u8 *dst, const u8 *src, const u8 *src_prev, int w, int h) {
	u8 *dst_end = dst + w * h;
	while (dst != dst_end) {
		*dst++ = *src++ - *src_prev++;
	}
}

static inline void differenceFromDownsampled(u8 *dst, const u8 *src, const u8 *ds_src_prev, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; ++i) {
		j = 0;
		for (; j < h; ++j) {
			*dst++ = *src++ - accessImageUpsample(ds_src_prev, i, j, w, h);
		}
	}
}

static inline void downsampledDifference(u8 *ds_pf, u8 *fd_ds_dst, const u8 *src, const u8 *src_prev, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; i += 2) {
		j = 0;
		for (; j < h; j += 2) {
			*fd_ds_dst++ = (*ds_pf++ = accessImageDownsample(src, i, j, w, h)) - accessImageDownsample(src_prev, i, j, w, h);
		}
	}
}

static inline void downsampledDifferenceFromDownsampled(u8 *ds_pf, u8 *fd_ds_dst, const u8 *src, const u8 *ds_src_prev, int w, int h) {
	int i = 0, j = 0;
	for (; i < w; i += 2) {
		j = 0;
		for (; j < h; j += 2) {
			*fd_ds_dst++ = (*ds_pf++ = accessImageDownsample(src, i, j, w, h)) - *ds_src_prev++;
		}
	}
}

#define BITS_PER_BYTE 8
#define ENCODE_SELECT_MASK_X_SCALE 1
#define ENCODE_SELECT_MASK_Y_SCALE 8
#define ENCODE_SELECT_MASK_FACTOR (BITS_PER_BYTE * ENCODE_SELECT_MASK_X_SCALE * ENCODE_SELECT_MASK_Y_SCALE)

static inline u8 abs_s8(s8 s) {
	return s > 0 ? s : -s;
}

static inline void selectImage(u8 *s_dst, u8 *m_dst, u8 *p_fd, const u8 *p, int w, int h) {
	u16 sum_p_fd, sum_p;
	u8 mask_bit, mask = 0;
	int x = 0, y, i, j, n;
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
			mask_bit = sum_p_fd < sum_p;

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
}

typedef struct _COMPRESS_CONTEXT {
	const u8* data;
	u32 data_size;
	const u8* data2;
	u32 data2_size;
	u32 max_compressed_size;
} COMPRESS_CONTEXT;

static struct {
	u32 targetBitsPerSec;
	u32 targetFrameRate;
	u32 qualityFactorNum;
	u32 qualityFactorDenum;
	u32 bitsPerFrame;
	u32 bitsPerY;
	u32 bitsPerUV;
} rpNetworkParams;

#define RP_DATA_TOP ((u32)1 << 0)
#define RP_DATA_Y ((u32)1 << 1)
#define RP_DATA_DS ((u32)1 << 2)
#define RP_DATA_FD ((u32)1 << 3)
#define RP_DATA_PFD ((u32)1 << 4)
#define RP_DATA_SPFD ((u32)1 << 5)
#define RP_DATA_RLE ((u32)1 << 6)

static struct RP_DATA_HEADER {
	u32 flags;
	u32 len;
	u32 id;
	u32 uncompressed_len;
} rpDataHeader;

u8 rpFrameId;
static inline void rpSendData(u8* data) {
	rp_send_src[rpFrameId] = data;
	rpFrameId = (rpFrameId + 1) % 2;
}

u8 rpControlPacketId;

#define RP_CONTROL_TOP_KEY (1 << 0)
#define RP_CONTROL_BOT_KEY (1 << 0)

u8 rp_control_top_force_key;
u8 rp_control_bot_force_key;

void rpControlRecvHandle(u8* buf, int buf_size) {
}

int rp_udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
	u8 *sendBuf = rpSendBufs;
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

static inline IINT64 iclock64(void)
{
  u64 value = svc_getSystemTick();
  return value / SYSTICK_PER_MS;
}

static inline IUINT32 iclock()
{
  return (IUINT32)(iclock64() & 0xfffffffful);
}

struct RP_PACKET_HEADER {
	u32 id;
	u32 len;
};

static u32 rpPacketId;
static u32 rpFrameHeaderId;

u8 rpThreadFrameId;
ikcpcb *rpKcp;
extern const IUINT32 IKCP_OVERHEAD;
#define HR_KCP_MAGIC 0x12345fff
#define KCP_SOCKET_TIMEOUT 10
#define KCP_TIMEOUT_TICKS (250 * SYSTICK_PER_MS)
#define KCP_PACKET_SIZE (PACKET_SIZE - IKCP_OVERHEAD)

static u8 rpSendDataPacketBuffer[PACKET_SIZE];
void rpSendDataThreadMain(void) {
	int ret;

	LightLock_Lock(&rpControlLock);
	rpKcp = ikcp_create(HR_KCP_MAGIC, 0);
	if (!rpKcp) {
		nsDbgPrint("ikcp_create failed\n");
		LightLock_Unlock(&rpControlLock);
		svc_exitThread();
		return;
	}
	rpPacketId = 0;
	rpFrameHeaderId = 0;
	rpKcp->output = rp_udp_output;
	if ((ret = ikcp_setmtu(rpKcp, PACKET_SIZE)) < 0) {
		nsDbgPrint("ikcp_setmtu failed: %d\n", ret);
	}
	ikcp_nodelay(rpKcp, 1, 10, 2, 1);
	ikcp_wndsize(rpKcp, 256, 128);
	LightLock_Unlock(&rpControlLock);

	while (1) {
		ret = LightSemaphore_TryAcquire(&rpWorkDoneSem, 1);
		if (ret != 0) {
			svc_sleepThread(KCP_SOCKET_TIMEOUT * 1000000);
			LightLock_Lock(&rpControlLock);
			ikcp_update(rpKcp, iclock());
			LightLock_Unlock(&rpControlLock);
			continue;
		}

		u8* data;
		data = rp_send_src[rpThreadFrameId];
		rpThreadFrameId = (rpThreadFrameId + 1) % 2;

		struct RP_DATA_HEADER header;
		memcpy(&header, data, sizeof(header));
		u32 size = header.len + sizeof(header);
		rpDbg("send %d %d %d %d\n", header.flags, header.len, header.id, header.uncompressed_len);
		u64 tickDiff, currentTick;
		u64 sleepValue;
		rpLastKcpSendTick = svc_getSystemTick();

		while (size) {
			if (kcp_restart) {
				break;
			}

			currentTick = svc_getSystemTick();
			tickDiff = currentTick - rpLastSendTick;

			if (currentTick - rpLastKcpSendTick > KCP_TIMEOUT_TICKS) {
				break;
			}

			if (tickDiff < rpMinIntervalBetweenPacketsInTick) {
				sleepValue = ((rpMinIntervalBetweenPacketsInTick - tickDiff) * 1000000000) / SYSTICK_PER_SEC;
				svc_sleepThread(sleepValue);
			}

			u32 sendSize = size;
			if (sendSize > KCP_PACKET_SIZE - sizeof(struct RP_PACKET_HEADER)) {
				sendSize = KCP_PACKET_SIZE - sizeof(struct RP_PACKET_HEADER);
			}

			LightLock_Lock(&rpControlLock);
			int waitsnd = ikcp_waitsnd(rpKcp);
			if (waitsnd > 256) {
				ret = 1;
			} else {
				struct RP_PACKET_HEADER packet_header;
				packet_header.len = sendSize;
				packet_header.id = ++rpPacketId;
				memcpy(rpSendDataPacketBuffer, &packet_header, sizeof(struct RP_PACKET_HEADER));
				memcpy(rpSendDataPacketBuffer + sizeof(struct RP_PACKET_HEADER), data, sendSize);
				ret = ikcp_send(rpKcp, rpSendDataPacketBuffer, sendSize + sizeof(struct RP_PACKET_HEADER));
				rpLastKcpSendTick = currentTick;
			}
			ikcp_update(rpKcp, iclock());
			LightLock_Unlock(&rpControlLock);

			if (ret < 0) {
				nsDbgPrint("ikcp_send failed: %d\n", ret);
			} else if (ret == 0) {
				size -= sendSize;
				data += sendSize;
			}

			rpLastSendTick = currentTick;
		}

		LightSemaphore_Release(&rpWorkAvaiSem, 1);

		if (kcp_restart) {
			while (LightSemaphore_TryAcquire(&rpWorkDoneSem, 1) == 0) {
				LightSemaphore_Release(&rpWorkAvaiSem, 1);
			}
			kcp_restart = 0;
			break;
		}

		if (size) {
			// rpDbg("ikcp_send timeout %d\n", (u32)rpLastKcpSendTick);
			rp_control_top_force_key = 1;
			rp_control_bot_force_key = 1;
			break;
		}
	}

	LightLock_Lock(&rpControlLock);
	ikcp_release(rpKcp);
	rpKcp = 0;
	LightLock_Unlock(&rpControlLock);
}

void rpSendDataThread(void) {
	while (1) {
		rpSendDataThreadMain();
	}

	svc_exitThread();
}

const int huffman_rle_dst_offset = 96000 + 256 + rle_max_compressed_size(96000 + 256) + sizeof(rpDataHeader); // for 400 * 240 of data
static inline int rpTestCompressAndSend(COMPRESS_CONTEXT* cctx, int skipTest) {
	skipTest = skipTest || !(rpConfig.flags & RP_DYNAMIC_ENCODE);

	LightSemaphore_Acquire(&rpWorkAvaiSem, 1);
	u8* dst = rp_work_dst[rpFrameId];
	u8* huffman_dst = dst + sizeof(rpDataHeader);
	uint32_t* counts = huffman_len_table(huffman_dst, cctx->data, cctx->data_size);
	int huffman_size = huffman_compressed_size(counts, huffman_dst) + 256;
	if (!skipTest && huffman_size > cctx->max_compressed_size) {
		rpDbg("Exceed bandwidth budget at %d (%d available)\n", huffman_size, cctx->max_compressed_size);
		s32 count;
		LightSemaphore_Release(&rpWorkAvaiSem, 1);
		return -1;
	}
	int dst_size = huffman_size;
	if (rpConfig.flags & RP_RLE_ENCODE) {
		dst_size += rle_max_compressed_size(huffman_size);
	}
	if (dst_size > huffman_rle_dst_offset) {
		nsDbgPrint("Not enough memory for compression: need %d (%d available)", dst_size, huffman_rle_dst_offset);
	}
	huffman_size = huffman_encode_with_len_table(counts, huffman_dst, cctx->data, cctx->data_size);
	u8* rle_dst = huffman_dst + huffman_size;
	int rle_size = INT_MAX;
	if (rpConfig.flags & RP_RLE_ENCODE) {
		rle_size = rle_encode(rle_dst, huffman_dst, huffman_size);
		// nsDbgPrint("Huffman %d RLE %d from %d", huffman_size, rle_size, cctx->data_size);
	} else {
		// nsDbgPrint("Huffman %d from %d", huffman_size, cctx->data_size);
	}

	u8* dh_dst;
	if (rle_size < huffman_size) {
		rpDataHeader.flags |= RP_DATA_RLE;
		rpDataHeader.len = rle_size;
		dh_dst = rle_dst - sizeof(rpDataHeader);
	} else {
		rpDataHeader.flags &= ~RP_DATA_RLE;
		rpDataHeader.len = huffman_size;
		dh_dst = huffman_dst - sizeof(rpDataHeader);
	}
	// rpDbg("frame send %d %d\n", rpDataHeader.flags, rpDataHeader.len  + sizeof(rpDataHeader));
	rpDataHeader.id = ++rpFrameHeaderId;
	rpDataHeader.uncompressed_len = cctx->data_size;
	memcpy(dh_dst, &rpDataHeader, sizeof(rpDataHeader));
	rpSendData(dh_dst);
	LightSemaphore_Release(&rpWorkDoneSem, 1);
	return 0;
}

#define RP_CONTROL_RECV_BUF_SIZE 2000
u8 *rpControlRecvBuf;
int rpControlRecvBuf_ready;
static inline void rpControlRecv(void) {
	if (!rpControlRecvBuf_ready) {
		svc_sleepThread(1000000000);
		return;
	}

	int ret = recv(rpControlSocket, rpControlRecvBuf, RP_CONTROL_RECV_BUF_SIZE, 0);
	if (ret == 0) {
		nsDbgPrint("rpControlRecv nothing\n");
		return;
	} else if (ret < 0) {
		int err = SOC_GetErrno();
		nsDbgPrint("rpControlRecv failed: %d\n", ret);
		return;
	}

	LightLock_Lock(&rpControlLock);
	if (rpKcp) {
		int bufSize = ret;
		if ((ret = ikcp_input(rpKcp, rpControlRecvBuf, bufSize)) < 0) {
			nsDbgPrint("ikcp_input failed: %d\n", ret);
		}
		// ikcp_update(rpKcp, iclock());
		ret = ikcp_recv(rpKcp, rpControlRecvBuf, RP_CONTROL_RECV_BUF_SIZE);
		if (ret >= 0) {
			rpControlRecvHandle(rpControlRecvBuf, ret);
		}
	}
	LightLock_Unlock(&rpControlLock);
}

static inline void rpControlPollAndRecv(void) {
	struct pollfd pollinfo;
	pollinfo.fd = rpControlSocket;
	pollinfo.events = POLLIN;
	pollinfo.revents = 0;
	int ret = poll2(&pollinfo, 1, 0);
	if (ret < 0) {
		nsDbgPrint("rpControlPollAndRecv failed: %d\n", ret);
		return;
	}
	if (pollinfo.revents & (POLLIN | POLLHUP)) {
		rpControlRecv();
	}
}

static inline int remotePlayBlitCompressAndSend(BLIT_CONTEXT* ctx) {
	// rpControlPollAndRecv();

	int blockSize = 16;
	int bpp = ctx->bpp;
	int width = ctx->width;
	int height = ctx->height;
	int pitch = ctx->src_pitch;
	int isKey = ctx->isKey;
	int isTop = ctx->isTop;
	int screenFrameId = ctx->id % 2;

	u32 px;
	u16 tmp;
	u8* blitBuffer = ctx->src;
	u8* sp = ctx->src;

	// This is a lot of variables.
	// Take care to not clobber anything.

	u8 *dp_begin = ctx->transformDst;

	 // need for next frame
	u8* dp_flags = dp_begin;
	// assume worst case width = 400, height = 240 for top screen
	u8 *dp_save_begin = dp_flags + 1; // dp_flags need to be saved as well, its size is calculated separately however
	u8* dp_y = dp_save_begin;
	u32 dp_y_size = width * height; // 96'000
	u8* dp_ds_u = dp_y + dp_y_size;
	u32 dp_ds_u_size = dp_y_size / 4; // 24'000
	u8* dp_ds_v = dp_ds_u + dp_ds_u_size;
	u32 dp_ds_v_size = dp_ds_u_size; // 24'000

	// dynamic encoding
	u8* dp_ds_y = dp_ds_v + dp_ds_v_size;
	u32 dp_ds_y_size = dp_ds_u_size;
	u8* dp_ds_ds_u = dp_ds_y + dp_ds_y_size;
	u32 dp_ds_ds_u_size = dp_ds_y_size / 4;
	u8* dp_ds_ds_v = dp_ds_ds_u + dp_ds_ds_u_size;
	u32 dp_ds_ds_v_size = dp_ds_ds_u_size;

	u8 *dp_save_end = dp_ds_ds_v + dp_ds_ds_v_size;
	u32 dp_save_size = dp_save_end - dp_begin;

	// output when isKey and not downsample
	u8* dp_p_y = dp_save_end;
	u32 dp_p_y_size = dp_y_size; // 96'000
	u8* dp_p_ds_u = dp_p_y + dp_p_y_size;
	u32 dp_p_ds_u_size = dp_ds_u_size; // 24'000
	u8* dp_p_ds_v = dp_p_ds_u + dp_p_ds_u_size;
	u32 dp_p_ds_v_size = dp_ds_v_size; // 24'000

	u8* dp_u = dp_p_ds_v + dp_p_ds_v_size;
	u32 dp_u_size = dp_y_size; // 96'000
	u8* dp_v = dp_u + dp_u_size;
	u32 dp_v_size = dp_y_size; // 96'000

	// output when not isKey
	// output when not RP_SELECT_PREDICTION and not RP_PREDICT_FRAME_DELTA
	u8* dp_fd_y = dp_u; // reuse from dp_u, after dp_ds_u is ready, make sure dp_fd_y_size <= dp_u_size
	u32 dp_fd_y_size = dp_y_size;
	u8* dp_fd_ds_u = dp_fd_y + dp_fd_y_size; // reuse from dp_v, after dp_ds_v is ready, need to be consecutive in layout with dp_fd_ds_v
	u32 dp_fd_ds_u_size = dp_ds_u_size;
	u8* dp_fd_ds_v = dp_fd_ds_u + dp_fd_ds_u_size; // make sure dp_fd_ds_u_size + dp_fd_ds_v_size <= dp_v_size
	u32 dp_fd_ds_v_size = dp_ds_v_size;

	// output when not RP_SELECT_PREDICTION and RP_PREDICT_FRAME_DELTA
	u8* dp_p_fd_y = dp_v + dp_v_size;
	u32 dp_p_fd_y_size = dp_fd_y_size; // 96'000
	u8* dp_p_fd_ds_u = dp_p_fd_y + dp_p_fd_y_size; // need to be consecutive in layout with dp_p_fd_ds_v
	u32 dp_p_fd_ds_u_size = dp_fd_ds_u_size; // 24'000
	u8* dp_p_fd_ds_v = dp_p_fd_ds_u + dp_p_fd_ds_u_size;
	u32 dp_p_fd_ds_v_size = dp_fd_ds_v_size; // 24'000

	// output when RP_SELECT_PREDICTION
	u8* dp_s_p_fd_y = dp_fd_y; // reuse from dp_fd_y, after dp_p_fd_y is ready
	u32 dp_s_p_fd_y_size = dp_fd_y_size;
	u8* dp_s_p_fd_ds_u = dp_fd_ds_u; // reuse from dp_fd_ds_u, after dp_p_fd_ds_u is ready
	u32 dp_s_p_fd_ds_u_size = dp_fd_ds_u_size;
	u8* dp_s_p_fd_ds_v = dp_fd_ds_v; // reuse from dp_fd_ds_v, after dp_p_fd_ds_v is ready
	u32 dp_s_p_fd_ds_v_size = dp_fd_ds_v_size;

	// output when RP_SELECT_PREDICTION
	u8* dp_m_p_fd_y = dp_p_fd_ds_v + dp_p_fd_ds_v_size; // need to be consecutive in layout with dp_m_p_fd_ds_u
	u32 dp_m_p_fd_y_size = (dp_p_fd_y_size + ENCODE_SELECT_MASK_FACTOR - 1) / ENCODE_SELECT_MASK_FACTOR;
	u8* dp_m_p_fd_ds_u = dp_m_p_fd_y + dp_m_p_fd_y_size; // need to be consecutive in layout with dp_m_p_fd_ds_v
	u32 dp_m_p_fd_ds_u_size = (dp_p_fd_ds_u_size + ENCODE_SELECT_MASK_FACTOR - 1) / ENCODE_SELECT_MASK_FACTOR;
	u8* dp_m_p_fd_ds_v = dp_m_p_fd_ds_u + dp_m_p_fd_ds_u_size;
	u32 dp_m_p_fd_ds_v_size = (dp_p_fd_ds_v_size + ENCODE_SELECT_MASK_FACTOR - 1) / ENCODE_SELECT_MASK_FACTOR;

	// output when isKey and downsample
	u8* dp_p_ds_y = dp_m_p_fd_ds_v + dp_m_p_fd_ds_v_size;
	u32 dp_p_ds_y_size = dp_ds_y_size;
	u8* dp_p_ds_ds_u = dp_p_ds_y + dp_p_ds_y_size;
	u32 dp_p_ds_ds_u_size = dp_ds_ds_u_size;
	u8* dp_p_ds_ds_v = dp_p_ds_ds_u + dp_p_ds_ds_u_size;
	u32 dp_p_ds_ds_v_size = dp_ds_ds_v_size;

	// dynamic encoding
	u8* dp_fd_ds_y = dp_fd_y; // reuse from dp_fd_y
	u32 dp_fd_ds_y_size = dp_ds_y_size;
	u8* dp_fd_ds_ds_u = dp_fd_ds_y + dp_fd_ds_y_size;
	u32 dp_fd_ds_ds_u_size = dp_ds_ds_u_size;
	u8* dp_fd_ds_ds_v = dp_fd_ds_ds_u + dp_fd_ds_ds_u_size;
	u32 dp_fd_ds_ds_v_size = dp_ds_ds_v_size;

	u8* dp_p_fd_ds_y = dp_fd_ds_ds_v + dp_fd_ds_ds_v_size; // reuse continued
	u32 dp_p_fd_ds_y_size = dp_fd_ds_y_size;
	u8* dp_p_fd_ds_ds_u = dp_p_fd_ds_y + dp_p_fd_ds_y_size;
	u32 dp_p_fd_ds_ds_u_size = dp_fd_ds_ds_u_size;
	u8* dp_p_fd_ds_ds_v = dp_p_fd_ds_ds_u + dp_p_fd_ds_ds_u_size;
	u32 dp_p_fd_ds_ds_v_size = dp_fd_ds_ds_v_size;

	u8* dp_s_p_fd_ds_y = dp_fd_ds_y; // reuse from dp_fd_ds_y, after dp_p_fd_ds_y is ready
	u32 dp_s_p_fd_ds_y_size = dp_fd_ds_y_size;
	u8* dp_s_p_fd_ds_ds_u = dp_fd_ds_ds_u; // reuse from dp_fd_ds_ds_u, after dp_p_fd_ds_ds_u is ready, need to be consecutive in layout with dp_s_p_fd_ds_ds_v
	u32 dp_s_p_fd_ds_ds_u_size = dp_fd_ds_ds_u_size;
	u8* dp_s_p_fd_ds_ds_v = dp_fd_ds_ds_v; // reuse from dp_fd_ds_ds_v, after dp_p_fd_ds_ds_v is ready
	u32 dp_s_p_fd_ds_ds_v_size = dp_fd_ds_ds_v_size;

	u8* dp_m_p_fd_ds_y = dp_p_fd_ds_ds_v + dp_p_fd_ds_ds_v_size; // need to be consecutive in layout with dp_m_p_fd_ds_ds_u
	u32 dp_m_p_fd_ds_y_size = (dp_p_fd_ds_y_size + ENCODE_SELECT_MASK_FACTOR - 1) / ENCODE_SELECT_MASK_FACTOR;
	u8* dp_m_p_fd_ds_ds_u = dp_m_p_fd_ds_y + dp_m_p_fd_ds_y_size; // need to be consecutive in layout with dp_m_p_fd_ds_ds_v
	u32 dp_m_p_fd_ds_ds_u_size = (dp_p_fd_ds_ds_u_size + ENCODE_SELECT_MASK_FACTOR - 1) / ENCODE_SELECT_MASK_FACTOR;
	u8* dp_m_p_fd_ds_ds_v = dp_m_p_fd_ds_ds_u + dp_m_p_fd_ds_ds_u_size;
	u32 dp_m_p_fd_ds_ds_v_size = (dp_p_fd_ds_ds_v_size + ENCODE_SELECT_MASK_FACTOR - 1) / ENCODE_SELECT_MASK_FACTOR;

	u8* dp_end = HR_MAX(dp_p_ds_ds_v + dp_p_ds_ds_v_size, dp_m_p_fd_ds_ds_v + dp_m_p_fd_ds_ds_v_size);
	if (dp_end > rpWorkBufferEnd) {
		nsDbgPrint("Allocated buffer too small: %d needed (%d available)\n", dp_end - dp_begin, rpWorkBufferEnd - dp_begin);
		return -1;
	}

	int frameOffset = dp_save_size * (400 + 320) / width; // offset for both screens
	int bottomScreenOffset = dp_save_size * 400 / width; // offset from top
	frameOffset += 2; // add offset for dp_flags, twice for both screens;
	bottomScreenOffset += 1; // add offset for dp_flags;

	u8* dp_flags_pf;
	u8* dp_y_pf;
	u8* dp_ds_u_pf;
	u8* dp_ds_v_pf;
	u8* dp_ds_y_pf;
	u8* dp_ds_ds_u_pf;
	u8* dp_ds_ds_v_pf;

	u8* dp_pf = dp_flags - frameOffset * 2;

	u8* dp_compression[HR_WORK_BUFFER_COUNT];
	dp_compression[0] = dp_pf - huffman_rle_dst_offset;
	for (int i = 1; i < HR_WORK_BUFFER_COUNT; ++i) {
		dp_compression[i] = dp_compression[i - 1] - huffman_rle_dst_offset;
	}
	if (dp_compression[HR_WORK_BUFFER_COUNT - 1] < ctx->src + ctx->src_size) {
		nsDbgPrint("Not enough memory: need additional %d\n", ctx->src + ctx->src_size - dp_compression[HR_WORK_BUFFER_COUNT - 1]);
		return -1;
	}

	if (rp_work_dst[0] == 0) {
		for (int i = 0; i < HR_WORK_BUFFER_COUNT; ++i) {
			rp_work_dst[i] = dp_compression[i];
		}
		nsDbgPrint("Memory available %d\n", rp_work_dst[HR_WORK_BUFFER_COUNT - 1] - (ctx->src + ctx->src_size));

		nsDbgPrint("Allocated buffer room %d\n", rpWorkBufferEnd - dp_end);
	}

	if (!isTop) { // apply bottomScreenOffset (which is offset from top)
		dp_flags -= bottomScreenOffset;
		dp_y -= bottomScreenOffset;
		dp_ds_u -= bottomScreenOffset;
		dp_ds_v -= bottomScreenOffset;
		dp_ds_y -= bottomScreenOffset;
		dp_ds_ds_u -= bottomScreenOffset;
		dp_ds_ds_v -= bottomScreenOffset;
	}
	// apply frameOffset
	if (screenFrameId) {
		dp_flags_pf = dp_flags;
		dp_y_pf = dp_y;
		dp_ds_u_pf = dp_ds_u;
		dp_ds_v_pf = dp_ds_v;
		dp_ds_y_pf = dp_ds_y;
		dp_ds_ds_u_pf = dp_ds_ds_u;
		dp_ds_ds_v_pf = dp_ds_ds_v;

		dp_flags -= frameOffset;
		dp_y -= frameOffset;
		dp_ds_u -= frameOffset;
		dp_ds_v -= frameOffset;
		dp_ds_y -= frameOffset;
		dp_ds_ds_u -= frameOffset;
		dp_ds_ds_v -= frameOffset;
	} else {
		dp_flags_pf = dp_flags - frameOffset;
		dp_y_pf = dp_y - frameOffset;
		dp_ds_u_pf = dp_ds_u - frameOffset;
		dp_ds_v_pf = dp_ds_v - frameOffset;
		dp_ds_y_pf = dp_ds_y - frameOffset;
		dp_ds_ds_u_pf = dp_ds_ds_u - frameOffset;
		dp_ds_ds_v_pf = dp_ds_ds_v - frameOffset;
	}

	int x = 0, y = 0, i, j;
	u8 r, g, b;

	u8* dp_y_in = dp_y;
	u8* dp_u_in = dp_u;
	u8* dp_v_in = dp_v;

	// ctx->directCompress = 0;
	if ((bpp == 3) || (bpp == 4)){
		/*
		ctx->directCompress = 1;
		return 0;
		*/
		for (x = 0; x < width; x++) {
			for (y = 0; y < height; y++) {
				r = sp[2];
				g = sp[1];
				b = sp[0];
				convertYUV(r, g, b, &r, &g, &b);
				*dp_y_in++ = r;
				*dp_u_in++ = g;
				*dp_v_in++ = b;
				sp += bpp;
			}
			sp += ctx->blankInColumn;
		}
	}
	else {
		svc_sleepThread(500000);
		for (x = 0; x < width; x++) {
			for (y = 0; y < height; y++) {
				u16 pix = *(u16*)sp;
				r = ((pix >> 11) & 0x1f) << 3;
				g = ((pix >> 5) & 0x3f) << 2;
				b = (pix & 0x1f) << 3;
				convertYUV(r, g, b, &r, &g, &b);
				*dp_y_in++ = r;
				*dp_u_in++ = g;
				*dp_v_in++ = b;
				sp += bpp;
			}
			sp += ctx->blankInColumn;
		}

	}

#define RP_DOWNSAMPLE_Y (1 << 0)
#define RP_DOWNSAMPLE2_UV (1 << 1)
	u8 flags = 0;
	u8 flags_pf = *dp_flags_pf;
	int ds_width = width / 2;
	int ds_height = height / 2;
	int ds_ds_width = ds_width / 2;
	int ds_ds_height = ds_height / 2;

	u8* dp_y_out;
	u32 dp_y_out_size;
	u8* dp_u_out;
	u32 dp_u_out_size;
	u8* dp_v_out;
	u32 dp_v_out_size;

	COMPRESS_CONTEXT cctx = { 0 };

#define downsampleImage1(c, w, h) downsampleImage(dp_ds_ ## c, dp_ ## c, w, h)
#define predictImage1(c, w, h) predictImage(dp_p_ ## c, dp_ ## c, w, h)
#define predictImage1out(o, c, w, h) do { \
	predictImage1(c, w, h); \
	dp_ ## o ## _out = dp_p_ ## c; \
	dp_ ## o ## _out ## _size = dp_p_ ## c ## _size; \
} while (0)

#define RP_HEADER_SET(f) do { \
	rpDataHeader.flags |= f; \
} while (0)

#define RP_HEADER_RESET(f) do { \
	rpDataHeader.flags &= ~f; \
} while (0)

#define cctx_data_y do { \
	cctx.max_compressed_size = rpNetworkParams.bitsPerY / 8; \
	cctx.data = dp_y_out; \
	cctx.data_size = dp_y_out_size; \
	RP_HEADER_SET(RP_DATA_Y); \
} while (0)

#define cctx_data_uv do { \
	cctx.max_compressed_size = rpNetworkParams.bitsPerUV / 8; \
	cctx.data = dp_u_out; \
	cctx.data_size = dp_u_out_size + dp_v_out_size; \
	RP_HEADER_RESET(RP_DATA_Y); \
} while (0)

	downsampleImage1(u, width, height);
	downsampleImage1(v, width, height);

	RP_HEADER_RESET((u32)-1);

	if (isTop) {
		RP_HEADER_SET(RP_DATA_TOP);
	} else {
		RP_HEADER_RESET(RP_DATA_TOP);
	}

	if (isKey) {
		RP_HEADER_RESET(RP_DATA_FD);
		// Y
		predictImage1out(y, y, width, height);
		cctx_data_y;
		RP_HEADER_RESET(RP_DATA_DS);

		if (rpTestCompressAndSend(&cctx, 0) < 0) {
			// Y downsampled
			flags |= RP_DOWNSAMPLE_Y;
			downsampleImage1(y, width, height);
			predictImage1out(y, ds_y, ds_width, ds_height);
			cctx_data_y;
			RP_HEADER_SET(RP_DATA_DS);
			if (rpTestCompressAndSend(&cctx, 1) < 0) {
				return -1;
			}
		}

		// UV
		predictImage1out(u, ds_u, ds_width, ds_height);
		predictImage1out(v, ds_v, ds_width, ds_height);
		cctx_data_uv;
		RP_HEADER_RESET(RP_DATA_DS);

		if (rpTestCompressAndSend(&cctx, 0) < 0) {
			// UV downsampled
			flags |= RP_DOWNSAMPLE2_UV;
			downsampleImage1(ds_u, ds_width, ds_height);
			predictImage1out(u, ds_ds_u, ds_ds_width, ds_ds_height);
			downsampleImage1(ds_v, ds_width, ds_height);
			predictImage1out(v, ds_ds_v, ds_ds_width, ds_ds_height);
			cctx_data_uv;
			RP_HEADER_SET(RP_DATA_DS);
			if (rpTestCompressAndSend(&cctx, 1) < 0) {
				return -1;
			}
		}

		*dp_flags = flags;
	} else {

#define predictImage2(c, w, h) do { \
	if (rpConfig.flags & RP_SELECT_PREDICTION) { \
		predictImage1(c, w, h); \
	} else { \
		dp_p_ ## c = dp_ ## c; \
	} \
} while (0)

#define selectImage2(s, c, w, h) selectImage(dp_s_p_fd_ ## c, dp_m_p_fd_ ## c, dp_ ## s ## c, dp_p_ ## c, w, h);
#define selectImage2out(o, s, c, w, h) do { \
	if (rpConfig.flags & RP_SELECT_PREDICTION) { \
		selectImage2(s, c, w, h); \
		dp_ ## o ## _out = dp_s_p_fd_ ## c; \
		dp_ ## o ## _out_size = dp_s_p_fd_ ## c ## _size; \
	} else { \
		dp_ ## o ## _out = dp_ ## s ## c; \
		dp_ ## o ## _out_size = dp_ ## s ## c ## _size; \
	} \
} while (0)

#define predictAndSelectImage2out(o, c, w, h) do { \
	if (rpConfig.flags & RP_PREDICT_FRAME_DELTA) { \
		predictImage1(fd_ ## c, w, h); \
		selectImage2out(o, p_fd_, c, w, h); \
	} else { \
		selectImage2out(o, fd_, c, w, h); \
	} \
} while (0)

#define cctx_data2(m) do { \
	if (rpConfig.flags & RP_SELECT_PREDICTION) { \
		cctx.data2 = dp_m_p_fd_ ## m; \
		cctx.data2_size = dp_m_p_fd_ ## m ## _size; \
	} else { \
		cctx.data2 = 0; \
		cctx.data2_size = 0; \
	} \
} while(0)

#define cctx_data2_size(m) do { \
	if (rpConfig.flags & RP_SELECT_PREDICTION) { \
		cctx.data2_size += dp_m_p_fd_ ## m ## _size; \
	} \
} while(0)

#define cctx_data2_y(y) do { \
	cctx_data_y; \
	cctx_data2(y); \
} while (0)

#define cctx_data2_uv(u, v) do { \
	cctx_data_uv; \
	cctx_data2(u); \
	cctx_data2_size(v); \
} while (0)

#define differenceImage1(c, w, h) differenceImage(dp_fd_ ## c, dp_ ## c, dp_ ## c ## _pf, w, h)
#define differenceFromDownsampled1(c, w, h) differenceFromDownsampled(dp_fd_ ## c, dp_ ## c, dp_ds_ ## c ## _pf, w, h)
#define downsampledDifference1(c, w, h) downsampledDifference(dp_ds_ ## c, dp_fd_ds_ ## c, dp_ ## c, dp_ ## c ## _pf, w, h)
#define downsampledDifferenceFromDownsampled1(c, w, h) downsampledDifferenceFromDownsampled(dp_ds_ ## c, dp_fd_ds_ ## c, dp_ ## c, dp_ds_ ## c ## _pf, w, h)

		RP_HEADER_SET(RP_DATA_FD);
		if (rpConfig.flags & RP_PREDICT_FRAME_DELTA) {
			RP_HEADER_SET(RP_DATA_PFD);
		} else {
			RP_HEADER_RESET(RP_DATA_PFD);
		}
		if (rpConfig.flags & RP_SELECT_PREDICTION) {
			RP_HEADER_SET(RP_DATA_SPFD);
		} else {
			RP_HEADER_RESET(RP_DATA_SPFD);
		}

		// Y
		if (flags_pf & RP_DOWNSAMPLE_Y) {
			differenceFromDownsampled1(y, width, height);
		} else {
			differenceImage1(y, width, height);
		}

		if (flags_pf & RP_DOWNSAMPLE2_UV) {
			differenceFromDownsampled1(ds_u, ds_width, ds_height);
			differenceFromDownsampled1(ds_v, ds_width, ds_height);
		} else {
			differenceImage1(ds_u, ds_width, ds_height);
			differenceImage1(ds_v, ds_width, ds_height);
		}

		predictImage2(y, width, height);
		predictAndSelectImage2out(y, y, width, height);
		cctx_data2_y(y);
		RP_HEADER_RESET(RP_DATA_DS);

		if (rpTestCompressAndSend(&cctx, 0) < 0) {
			// Y downsampled
			flags |= RP_DOWNSAMPLE_Y;

			if (flags_pf & RP_DOWNSAMPLE_Y) {
				downsampledDifferenceFromDownsampled1(y, width, height);
			} else {
				downsampledDifference1(y, width, height);
			}
			predictImage2(ds_y, ds_width, ds_height);
			predictAndSelectImage2out(y, ds_y, ds_width, ds_height);
			cctx_data2_y(ds_y);
			RP_HEADER_SET(RP_DATA_DS);

			if (rpTestCompressAndSend(&cctx, 1) < 0) {
				return -1;
			}
		}

		// UV
		predictImage2(ds_u, ds_width, ds_height);
		predictAndSelectImage2out(u, ds_u, ds_width, ds_height);
		predictImage2(ds_v, ds_width, ds_height);
		predictAndSelectImage2out(v, ds_v, ds_width, ds_height);
		cctx_data2_uv(ds_u, ds_v);
		RP_HEADER_RESET(RP_DATA_DS);

		if (rpTestCompressAndSend(&cctx, 0) < 0) {
			// UV downsampled
			flags |= RP_DOWNSAMPLE2_UV;

			if (flags_pf & RP_DOWNSAMPLE2_UV) {
				downsampledDifferenceFromDownsampled1(ds_u, ds_width, ds_height);
			} else {
				downsampledDifference1(ds_u, ds_width, ds_height);
			}
			predictImage2(ds_ds_u, ds_ds_width, ds_ds_height);
			predictAndSelectImage2out(u, ds_ds_u, ds_ds_width, ds_ds_height);

			if (flags_pf & RP_DOWNSAMPLE2_UV) {
				downsampledDifferenceFromDownsampled1(ds_v, ds_width, ds_height);
			} else {
				downsampledDifference1(ds_v, ds_width, ds_height);
			}
			predictImage2(ds_ds_v, ds_ds_width, ds_ds_height);
			predictAndSelectImage2out(v, ds_ds_v, ds_ds_width, ds_ds_height);

			cctx_data2_uv(ds_ds_u, ds_ds_v);
			RP_HEADER_SET(RP_DATA_DS);

			if (rpTestCompressAndSend(&cctx, 1) < 0) {
				return -1;
			}
		}

		*dp_flags = flags;
	}

	//ctx->compressedSize = fastlz_compress_level(2, ctx->transformDst, (ctx->width) * (ctx->height) * 2, ctx->compressDst);
	return 0;
}

#if 0
int remotePlayBlit(BLIT_CONTEXT* ctx) {
	int bpp = ctx->bpp;
	int width = ctx->width;
	int height = ctx->height;
	u8 *dp;
	u32 px;
	u16 tmp;

	/*
	if (blankInColumn == 0) {
	if (bpp == 2) {
	memcpy(dst, src, width * height * 2);
	return format;
	}
	}*/

	dp = dataBuf + 4;

	while (ctx->x < width) {
		if (bpp == 2) {
			while (ctx->y < height) {
				if (dp - dataBuf >= PACKET_SIZE) {
					return dp - dataBuf;
				}
				dp[0] = ctx->src[0];
				dp[1] = ctx->src[1];
				dp += 2;
				ctx->src+= bpp;
				ctx->y += 1;
			}
		}
		else {

			while (ctx->y < height) {
				if (dp - dataBuf >= PACKET_SIZE) {
					return dp - dataBuf;
				}
				*((u16*)(dp)) = ((u16)((ctx->src[2] >> 3) & 0x1f) << 11) |
					((u16)((ctx->src[1] >> 2) & 0x3f) << 5) |
					((u16)((ctx->src[0] >> 3) & 0x1f));
				dp += 2;
				ctx->src += bpp;
				ctx->y += 1;
			}
		}
		ctx->src += ctx->blankInColumn;
		ctx->y = 0;
		ctx->x += 1;
	}
	return dp - dataBuf;
}
#endif


void remotePlayKernelCallback(void) {



	u32 ret;
	u32 fbP2VOffset = 0xc0000000;
	u32 current_fb;

	tl_fbaddr[0] = REG(IoBasePdc + 0x468);
	tl_fbaddr[1] = REG(IoBasePdc + 0x46c);
	bl_fbaddr[0] = REG(IoBasePdc + 0x568);
	bl_fbaddr[1] = REG(IoBasePdc + 0x56c);
	tl_format = REG(IoBasePdc + 0x470);
	bl_format = REG(IoBasePdc + 0x570);
	tl_pitch = REG(IoBasePdc + 0x490);
	bl_pitch = REG(IoBasePdc + 0x590);


	current_fb = REG(IoBasePdc + 0x478);
	current_fb &= 1;
	tl_current = tl_fbaddr[current_fb];

	current_fb = REG(IoBasePdc + 0x578);
	current_fb &= 1;
	bl_current = bl_fbaddr[current_fb];

	/*
	memcpy(imgBuffer, (void*)tl_current, tl_pitch * 400);

	if (requireUpdateBottom) {
		memcpy(imgBuffer + 0x00050000, (void*)bl_current, bl_pitch * 320);
	}*/

	// TOP screen
	/*
	current_fb = REG(IoBasePdc + 0x478);
	current_fb &= 1;
	topFormat = remotePlayBlit(imgBuffer, 400, 240, (void*)tl_fbaddr[current_fb], tl_format, tl_pitch);
	*/
	/*
	// Bottom screen
	current_fb = REG(IoBasePdc + 0x578);
	current_fb &= 1;
	bottomFormat = remotePlayBlit(imgBuffer + 0x50000, 320, 240, (void*)bl_fbaddr[current_fb], bl_format, bl_pitch);
	*/
}


Handle rpHDma[2], rpHandleHome, rpHandleGame;
u32 rpGameFCRAMBase = 0;

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

static inline int isInVRAM(u32 phys) {
	if (phys >= 0x18000000) {
		if (phys < 0x18000000 + 0x00600000) {
			return 1;
		}
	}
	return 0;
}

static inline int isInFCRAM(u32 phys) {
	if (phys >= 0x20000000) {
		if (phys < 0x20000000 + 0x10000000) {
			return 1;
		}
	}
	return 0;
}

static inline int rpCaptureScreen(int isTop) {

	u8 dmaConfig[80] = { 0, 0, 4 };
	u32 bufSize = isTop? (tl_pitch * 400) : (bl_pitch * 320);
	u32 phys = isTop ? tl_current : bl_current;
	u32 dest = rpImgBuffer;
	Handle hProcess = rpHandleHome;

	int ret;

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, dest, bufSize);
	svc_closeHandle(rpHDma[isTop]);
	rpHDma[isTop] = 0;

	if (isInVRAM(phys)) {
		svc_startInterProcessDma(&rpHDma[isTop], CURRENT_PROCESS_HANDLE,
			dest, hProcess, 0x1F000000 + (phys - 0x18000000), bufSize, dmaConfig);
		return bufSize;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			ret = svc_startInterProcessDma(&rpHDma[isTop], CURRENT_PROCESS_HANDLE,
				dest, hProcess, rpGameFCRAMBase + (phys - 0x20000000), bufSize, dmaConfig);

		}
		return bufSize;
	}
	svc_sleepThread(1000000000);
	return -1;
}

u32 rpQualityFN;
u32 rpQualityFD;
void updateNetworkParams(void) {
	rpNetworkParams.targetBitsPerSec = rpConfig.qos;
	rpNetworkParams.targetFrameRate = 45;
	rpQualityFN = (rpConfig.quality & 0xff);
	rpQualityFD = (rpConfig.quality & 0xff00) >> 8;
	if (rpQualityFN == 0 || rpQualityFD == 0)
	{
		rpNetworkParams.qualityFactorNum = 2;
		rpNetworkParams.qualityFactorDenum = 1;
	} else if (((u64)rpQualityFN + rpQualityFD - 1) / rpQualityFD > 4) {
		rpNetworkParams.qualityFactorNum = 4;
		rpNetworkParams.qualityFactorDenum = 1;
	} else if (((u64)rpQualityFD + rpQualityFN - 1) / rpQualityFN > 4) {
		rpNetworkParams.qualityFactorNum = 1;
		rpNetworkParams.qualityFactorDenum = 4;
	} else {
		rpNetworkParams.qualityFactorNum = rpQualityFN;
		rpNetworkParams.qualityFactorDenum = rpQualityFD;
	}
	rpNetworkParams.bitsPerFrame =
		(u64)rpNetworkParams.targetBitsPerSec * rpNetworkParams.qualityFactorNum / rpNetworkParams.targetFrameRate / rpNetworkParams.qualityFactorDenum;
	rpMinIntervalBetweenPacketsInTick = (u64)SYSTICK_PER_SEC * PACKET_SIZE * 8 / rpConfig.qos;
	rpNetworkParams.bitsPerY = rpNetworkParams.bitsPerFrame * 2 / 3;
	rpNetworkParams.bitsPerUV = rpNetworkParams.bitsPerFrame / 3;
	rpDbg(
		"network params: target frame rate %d, bits per frame %d, bits per y %d, bits per uv %d\n",
		rpNetworkParams.targetFrameRate, rpNetworkParams.bitsPerFrame, rpNetworkParams.bitsPerY, rpNetworkParams.bitsPerUV);
}

void remotePlaySendFrames(void) {
	u32 isPriorityTop = 1;
	u32 priorityFactor = 0;
	u32 mode = (rpConfig.mode & 0xff00) >> 8;
	u32 factor = (rpConfig.mode & 0xff);
	if (mode == 0) {
		isPriorityTop = 0;
	}
	priorityFactor = factor;
	rpConfig.qos = HR_MIN(HR_MAX(rpConfig.qos, 1024 * 512 * 3), 1024 * 512 * 36);
	updateNetworkParams();

	nsDbgPrint(
		"remote play config: priority top %d, priority factor %d, "
		"target bitrate %d, quality factor %d/%d%s%s%s%s%s\n",
		isPriorityTop, priorityFactor,
		rpConfig.qos, rpQualityFN, rpQualityFD,
		(rpConfig.flags & RP_USE_FRAME_DELTA) ? ", use frame delta" : "",
		(rpConfig.flags & RP_PREDICT_FRAME_DELTA) ? ", predict frame delta" : "",
		(rpConfig.flags & RP_SELECT_PREDICTION) ? ", select prediction" : "",
		(rpConfig.flags & RP_DYNAMIC_ENCODE) ? ", use dynamic encode" : "",
		(rpConfig.flags & RP_RLE_ENCODE) ? ", use rle encode" : "");

	kcp_restart = 1;

	u32 currentUpdating = isPriorityTop;
	u32 frameCount = 0;
	u8 cnt;
	BLIT_CONTEXT topContext = { 0 }, botContext = { 0 };
	topContext.force_key = 1;
	botContext.force_key = 1;
	u64 currentTick = 0;
	int src_size;

	// u32 tl_pitch_max = 0;
	// u32 bl_pitch_max = 0;

	while (1) {
		currentUpdating = isPriorityTop;
		frameCount += 1;
		if (priorityFactor != 0) {
			if (frameCount % (priorityFactor + 1) == 0) {
				currentUpdating = !isPriorityTop;
			}
		}

		remotePlayKernelCallback();

		if (currentUpdating) {
			// send top
			src_size = rpCaptureScreen(1);
			currentTopId += 1;
			if (rp_control_top_force_key) {
				rp_control_top_force_key = 0;
				topContext.force_key = 1;
			}
			remotePlayBlitInit(&topContext, 400, 240, tl_format, tl_pitch, rpImgBuffer, src_size);
			// tl_pitch_max = tl_pitch_max > tl_pitch ? tl_pitch_max : tl_pitch;
			// topContext.compressDst = 0;
			topContext.transformDst = rpImgBuffer + RP_TRANSFORM_DST_OFFSET;
			// botContext.transformDst2 = 0;
			// topContext.reset = 1;
			topContext.id = (u8)currentTopId;
			topContext.isTop = 1;
			topContext.force_key = remotePlayBlitCompressAndSend(&topContext) < 0;
		}
		else {
			// send bottom
			src_size = rpCaptureScreen(0);
			currentBottomId += 1;
			if (rp_control_bot_force_key) {
				rp_control_bot_force_key = 0;
				botContext.force_key = 1;
			}
			remotePlayBlitInit(&botContext, 320, 240, bl_format, bl_pitch, rpImgBuffer, src_size);
			// bl_pitch_max = bl_pitch_max > bl_pitch ? bl_pitch_max : bl_pitch;
			// botContext.compressDst = 0;
			botContext.transformDst = rpImgBuffer + RP_TRANSFORM_DST_OFFSET;
			// botContext.transformDst2 = 0;
			// botContext.reset = 1;
			botContext.id = (u8)currentBottomId;
			botContext.isTop = 0;
			botContext.force_key = remotePlayBlitCompressAndSend(&botContext) < 0;
		}

/*
#define SEND_STAT_EVERY_X_FRAMES 64
		if (frameCount % SEND_STAT_EVERY_X_FRAMES == 0) {
			u64 nextTick = svc_getSystemTick();
			if (currentTick) {
				u32 ms = (nextTick - currentTick) / 1000 / SYSTICK_PER_US;
				nsDbgPrint("%d ms for %d frames\n", ms, SEND_STAT_EVERY_X_FRAMES);
				// tl_pitch_max = bl_pitch_max = 0;
			}
			currentTick = nextTick;
		}
*/

		if (g_nsConfig->rpConfig.control == 1) {
			rpConfig = g_nsConfig->rpConfig;
			g_nsConfig->rpConfig.control = 0;
			kcp_restart = 1;
			svc_sleepThread(1000000000);
			break;
		}
	}
}

#define RP_stackSize 0x10000
#define RP_dataStackSize 0x1000
void remotePlayThreadStart(void) {
	// if (rpAllocBuff) {
	// 	rpAllocBuffRemainSize = 0x00100000;
	// }
	// else {
	// 	goto final;
	// }
	// rpInitCompress();

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
	kRemotePlayCallback();
	int ret;

	// rpControlSocket = socket(AF_INET, SOCK_DGRAM, 0);
	// if (rpControlSocket < 0) {
	// 	showDbg("rpControlSocket open failed: %d\n", rpControlSocket, 0);
	// 	goto final;
	// }

	// struct sockaddr_in sai = {0};
	// sai.sin_family = AF_INET;
	// sai.sin_port = htons(8001);
	// sai.sin_addr.s_addr = htonl(INADDR_ANY);

	// ret = bind(rpControlSocket, (struct sockaddr *)&sai, sizeof(sai));
	// if (ret < 0) {
	// 	showDbg("rpControlSocket bind failed: %d\n", ret, 0);
	// 	goto final;
	// }

	Handle hThread;
	/*
	if (buf[31] != 6) {
	nsDbgPrint("buflen: %d\n", buflen);
	for (i = 0; i < buflen; i++) {
	nsDbgPrint("%02x ", buf[i]);
	}
	}*/
	LightSemaphore_Init(&rpWorkDoneSem, 0, 2);
	LightSemaphore_Init(&rpWorkAvaiSem, 2, 2);
	LightLock_Init(&rpControlLock);

	ret = svc_createThread(&hThread, (void*)rpSendDataThread, 0, &rpDataThreadStack[RP_dataStackSize - 40], 0x10, 3);
	if (ret != 0) {
		nsDbgPrint("Create rpSendDataThread Failed: %08x\n", ret);
		goto final;
	}

	while (1) {
		remotePlaySendFrames();
	}
	final:

	svc_exitThread();
}

int nwmValParamCallback(u8* buf, int buflen) {
	//rtDisableHook(&nwmValParamHook);
	int i;
	int ret;
	Handle hThread;
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

#define RP_IMG_BUFFER_SIZE 0x00300000
#define UMM_HEAP_SIZE (512 * 1024)
			rpImgBuffer = plgRequestMemory(RP_IMG_BUFFER_SIZE);
			if (!rpImgBuffer) {
				nsDbgPrint("imgBuffer alloc failed\n");
				return 0;
			}
			rpAllocBuff = rpImgBuffer + RP_IMG_BUFFER_SIZE - huffman_malloc_usage();
			rpSendBufs = rpAllocBuff - RP_PACKET_SIZE;
			memcpy(rpSendBufs, buf, NWM_HEADER_SIZE);

			rpThreadStack = (u32)(rpSendBufs - RP_stackSize) / 4 * 4;
			rpDataThreadStack = (u32)(rpThreadStack - RP_dataStackSize) / 4 * 4;
			umm_malloc_heap_addr = (u32)(rpDataThreadStack - UMM_HEAP_SIZE) / 4 * 4;
			umm_init_heap(umm_malloc_heap_addr, UMM_HEAP_SIZE);
			ikcp_allocator(umm_malloc, umm_free);
			rpControlRecvBuf = umm_malloc_heap_addr - RP_CONTROL_RECV_BUF_SIZE;
			rpControlRecvBuf_ready = 1;
			rpWorkBufferEnd = rpControlRecvBuf;

			ret = svc_createThread(&hThread, (void*)remotePlayThreadStart, 0, &rpThreadStack[RP_stackSize - 40], 0x10, 2);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Thread Failed: %08x\n", ret);
			}
		}
	}
	return 0;
}

void remotePlayMain(void) {
	nwmSendPacket = g_nsConfig->startupInfo[12];
	rpConfig = g_nsConfig->rpConfig;
	rtInitHookThumb(&nwmValParamHook, g_nsConfig->startupInfo[11], nwmValParamCallback);
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

static inline void nsRemotePlayControl(void) {
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
		&rpConfig,
		sizeof(rpConfig));
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

	rpConfig.mode = pac->args[0];
	u32 rp_magic = pac->args[1];
	rpConfig.qos = pac->args[2];
	rpConfig.flags = pac->args[3];
	rpConfig.flags |= RP_FLAG_DBG;
	rpConfig.quality = pac->args[4];
	rpConfig.control = 0;

	if ((rp_magic >= 10) && (rp_magic <= 100)) {
		nsDbgPrint("JPEG encoder not supported in this build\n");
		goto final;
	} else if (rp_magic != RP_MAGIC) {
		nsDbgPrint("Illegal params. Please update your NTR viewer client\n");
		goto final;
	}

	if (nsIsRemotePlayStarted) {
		nsDbgPrint("remote play already started, updating params\n");
		nsRemotePlayControl();
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
	cfg.rpConfig = rpConfig;
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
		svc_closeHandle(hThread);

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

	int rpProcess = g_nsCtx->listenPort == 5000 + 0x1a;
	// int rpProcess = 0;
	if (rpProcess) {
		rpControlSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (rpControlSocket < 0) {
			showDbg("rpControlSocket open failed: %d\n", rpControlSocket, 0);
			return;
		}

		struct sockaddr_in sai = {0};
		sai.sin_family = AF_INET;
		sai.sin_port = htons(REMOTE_PLAY_PORT);
		sai.sin_addr.s_addr = htonl(INADDR_ANY);

		ret = bind(rpControlSocket, (struct sockaddr *)&sai, sizeof(sai));
		if (ret < 0) {
			showDbg("rpControlSocket bind failed: %d\n", ret, 0);
			return;
		}
	}

#define RP_PROCESS_SELECT_BEGIN(s) \
	if (rpProcess) { \
		FD_ZERO(&rset); \
		FD_SET(s, &rset); \
		if (rpControlRecvBuf_ready) { \
			FD_SET(rpControlSocket, &rset); \
			maxfdp1 = HR_MAX(s, rpControlSocket) + 1; \
		} else { \
			maxfdp1 = s + 1; \
		} \
		nready = select2(maxfdp1, &rset, NULL, NULL, NULL); \
	} \
 \
	if (rpProcess && FD_ISSET(rpControlSocket, &rset)) { \
		rpControlRecv(); \
	} \
 \
	if (!rpProcess || FD_ISSET(s, &rset)) {
#define RP_PROCESS_SELECT_END \
	}

	while (1) {
		RP_PROCESS_SELECT_BEGIN(listen_sock)

			sockfd = accept(listen_sock, NULL, NULL);
			g_nsCtx->hSocket = sockfd;
			if (sockfd < 0) {
				if (!rpProcess) {
					svc_sleepThread(1000000000);
				}
				continue;
			}
			/*
			tmp = fcntl(sockfd, F_GETFL);
			fcntl(sockfd, F_SETFL, tmp | O_NONBLOCK);
			*/
			while (1) {
				RP_PROCESS_SELECT_BEGIN(sockfd)

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

				RP_PROCESS_SELECT_END
			}
			closesocket(sockfd);

		RP_PROCESS_SELECT_END
	}
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