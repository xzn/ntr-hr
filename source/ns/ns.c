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

#define RP_MODE_TOP_BOT_10X 0
#define RP_MODE_TOP_BOT_5X 1
#define RP_MODE_TOP_BOT_1X 2
#define RP_MODE_BOT_TOP_5X 3
#define RP_MODE_BOT_TOP_10X 4
#define RP_MODE_TOP_ONLY 5
#define RP_MODE_BOT_ONLY 6
#define RP_MODE_3D 10

int rpAllocDebug = 0;

struct jpeg_compress_struct cinfo_top, cinfo_bot;
struct rp_alloc_stats_check {
	struct rp_alloc_stats qual, comp, scan;
} alloc_stats_top, alloc_stats_bot;
struct jpeg_error_mgr jerr;

u64 rpMinIntervalBetweenPacketsInTick = 0;

#define SYSTICK_PER_US (268);




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
void remotePlayMain2() {
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

int packetLen = 0;
int remotePlayInited = 0;
u8 remotePlayBuffer[2000] = { 0 };
u8* dataBuf = remotePlayBuffer + 0x2a + 8;
u8* imgBuffer = 0;
int topFormat = 0, bottomFormat = 0;
int frameSkipA = 1, frameSkipB = 1;
u32 requireUpdateBottom = 0;
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

int	initUDPPacket(int dataLen) {
	dataLen += 8;
	*(u16*)(remotePlayBuffer + 0x22 + 8) = htons(8000); // src port
	*(u16*)(remotePlayBuffer + 0x24 + 8) = htons(8001); // dest port
	*(u16*)(remotePlayBuffer + 0x26 + 8) = htons(dataLen);
	*(u16*)(remotePlayBuffer + 0x28 + 8) = 0; // no checksum
	dataLen += 20;

	*(u16*)(remotePlayBuffer + 0x10 + 8) = htons(dataLen);
	*(u16*)(remotePlayBuffer + 0x12 + 8) = 0xaf01; // packet id is a random value since we won't use the fragment
	*(u16*)(remotePlayBuffer + 0x14 + 8) = 0x0040; // no fragment
	*(u16*)(remotePlayBuffer + 0x16 + 8) = 0x1140; // ttl 64, udp
	*(u16*)(remotePlayBuffer + 0x18 + 8) = 0;
	*(u16*)(remotePlayBuffer + 0x18 + 8) = ip_checksum(remotePlayBuffer + 0xE + 8, 0x14);

	dataLen += 22;
	*(u16*)(remotePlayBuffer + 12) = htons(dataLen);

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

	u8* transformDst;

	u8 id;
	u8 isTop;
	u8 frameCount;

	int directCompress;
	j_compress_ptr cinfo;
	struct rp_alloc_stats_check *alloc_stats;
} BLIT_CONTEXT;


void remotePlayBlitInit(BLIT_CONTEXT* ctx, int width, int height, int format, int src_pitch, u8* src) {

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




vu64 rpLastSendTick = 0;

void rpSendBuffer(u8* buf, u32 size, u32 flag) {
	if (rpAllocDebug) {
		showDbg("sendbuf: %08x, %d", buf, size);
		return;
	}
	vu64 tickDiff;
	vu64 sleepValue;
	{
		tickDiff = svc_getSystemTick() - rpLastSendTick;
		if (tickDiff < rpMinIntervalBetweenPacketsInTick) {
			sleepValue = ((rpMinIntervalBetweenPacketsInTick - tickDiff) * 1000) / SYSTICK_PER_US;
			svc_sleepThread(sleepValue);
		}
		if (flag) {
			dataBuf[1] |= flag;
		}
		if (size == 0) {
			dataBuf[4] = 0;
			size = 1;
		}
		packetLen = initUDPPacket(size + 4);
		nwmSendPacket(remotePlayBuffer, packetLen);
		dataBuf[3] += 1;
		rpLastSendTick = svc_getSystemTick();
	}
}


int remotePlayBlitCompressed(BLIT_CONTEXT* ctx) {
	int blockSize = 16;
	int bpp = ctx->bpp;
	int width = ctx->width;
	int height = ctx->height;
	int pitch = ctx->src_pitch;

	u32 px;
	u16 tmp;
	u8* blitBuffer = ctx->src;
	u8* sp = ctx->src;
	u8* dp = ctx->transformDst;
	int x = 0, y = 0, i, j;
	u8* rowp = ctx->src;
	u8* blkp;
	u8* pixp;

	ctx->directCompress = 0;
	if ((bpp == 3) || (bpp == 4)){
		ctx->directCompress = 1;
		return 0;
		/*
		for (x = 0; x < width; x++) {
			for (y = 0; y < height; y++) {
				dp[0] = sp[2];
				dp[1] = sp[1];
				dp[2] = sp[0];
				dp += 3;
				sp += bpp;
			}
			sp += ctx->blankInColumn;
		}
		*/
	}
	else {
		/*
		svc_sleepThread(500000);
		for (x = 0; x < width; x++) {
			for (y = 0; y < height; y++) {
				u16 pix = *(u16*)sp;
				dp[0] = ((pix >> 11) & 0x1f) << 3;
				dp[1] = ((pix >> 5) & 0x3f) << 2;
				dp[2] = (pix & 0x1f) << 3;
				dp += 3;
				sp += bpp;
			}
			sp += ctx->blankInColumn;
		}
		*/
	}

	//ctx->compressedSize = fastlz_compress_level(2, ctx->transformDst, (ctx->width) * (ctx->height) * 2, ctx->compressDst);
	return 0;
}





int rpInitJpegCompress() {
	j_compress_ptr cinfos[] = {&cinfo_top, &cinfo_bot};

	for (int i = 0; i < sizeof(cinfos) / sizeof(*cinfos); ++i) {
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
		cinfo->client_data = dataBuf + 4;
		jpeg_create_compress(cinfo);
		jpeg_stdio_dest(cinfo, 0);

		cinfo->in_color_space = JCS_RGB;
		cinfo->defaults_skip_tables = TRUE;
		jpeg_set_defaults(cinfo);
		cinfo->dct_method = JDCT_IFAST;
		cinfo->skip_markers = TRUE;
		cinfo->skip_buffers = TRUE;
	}

	jpeg_std_huff_tables((j_common_ptr)&cinfo_top);
	for (int i = 0; i < NUM_HUFF_TBLS; ++i) {
		cinfo_bot.dc_huff_tbl_ptrs[i] = cinfo_top.dc_huff_tbl_ptrs[i];
		cinfo_bot.ac_huff_tbl_ptrs[i] = cinfo_top.ac_huff_tbl_ptrs[i];
	}

	return 0;
}

#define rp_pre_proc_buffer_count (3)
#define rp_thread_count (2)
#define rp_mcu_buffer_count (4)

JSAMPARRAY pre_proc_buffers[rp_pre_proc_buffer_count][MAX_COMPONENTS];
JSAMPARRAY color_buffers[rp_thread_count][MAX_COMPONENTS];
JBLOCKROW MCU_buffers[rp_mcu_buffer_count][C_MAX_BLOCKS_IN_MCU];

void rpJPEGCompress(j_compress_ptr cinfo, u8 *src, u32 pitch) {
	JDIMENSION in_rows_blk = DCTSIZE * cinfo->max_v_samp_factor;
	JDIMENSION in_rows_blk_half = in_rows_blk / 2;

	// JSAMPIMAGE output_buf = jpeg_get_process_buf(cinfo);
	// JSAMPIMAGE color_buf = jpeg_get_pre_process_buf(cinfo);
	JSAMPIMAGE output_buf = pre_proc_buffers[0];
	JSAMPIMAGE color_buf = color_buffers[0];

	JSAMPROW input_buf[in_rows_blk_half];

	int j = 0;
	for (j = 0; j < cinfo->image_height;) {
		for (int i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 0);

		for (int i = 0; i < in_rows_blk_half; ++i, ++j)
			input_buf[i] = src + j * pitch;
		jpeg_pre_process(cinfo, input_buf, color_buf, output_buf, 1);

		// JBLOCKROW *MCU_buffer = jpeg_get_compress_data_buf(cinfo);
		JBLOCKROW *MCU_buffer = MCU_buffers[0];

		for (int k = 0; k < cinfo->MCUs_per_row; ++k) {
			jpeg_compress_data(cinfo, output_buf, MCU_buffer, k);
			jpeg_encode_mcu_huff(cinfo, MCU_buffer);
		}
	}
}

void rpCompressAndSendPacket(BLIT_CONTEXT* ctx) {
	u8* srcBuff;
	u32 row_stride, i;
	u8* row_pointer[400];

	/* !directCompress */
	u8* sp = ctx->src;
	j_compress_ptr cinfo = ctx->cinfo;

	dataBuf[0] = ctx->id;
	dataBuf[1] = ctx->isTop;
	dataBuf[2] = 2;
	dataBuf[3] = 0;

	cinfo->image_width = ctx->height;      /* image width and height, in pixels */
	cinfo->image_height = ctx->width;
	cinfo->input_components = 3;
	cinfo->in_color_space = JCS_RGB565;

	row_stride = ctx->src_pitch;
	srcBuff = ctx->src;
	// row_stride = cinfo->image_width * 3; /* JSAMPLEs per row in image_buffer */
	// srcBuff = ctx->transformDst;
	if (ctx->directCompress) {
		// row_stride = ctx->src_pitch;
		// srcBuff = ctx->src;
		cinfo->input_components = ctx->bpp;
		if (ctx->bpp == 3) {
			cinfo->in_color_space = JCS_EXT_BGR;
		}
		else {
			cinfo->in_color_space = JCS_EXT_BGRX;
		}
	}

	if (cinfo->global_state == JPEG_CSTATE_START) {
		// memcpy(&ctx->alloc_stats->scan, &cinfo->alloc.stats, sizeof(struct rp_alloc_stats));
		jpeg_start_compress(cinfo, TRUE);
	} else {
		jpeg_suppress_tables(cinfo, FALSE);
		jpeg_init_destination(cinfo);
		jpeg_start_pass_prep(cinfo, 0);
		jpeg_start_pass_huff(cinfo);
		jpeg_start_pass_coef(cinfo, 0);
		jpeg_start_pass_main(cinfo, 0);
		cinfo->next_scanline = 0;
	}

	memcpy(&ctx->alloc_stats->scan, &cinfo->alloc.stats, sizeof(struct rp_alloc_stats));

	jpeg_write_file_header(cinfo);
	jpeg_write_frame_header(cinfo);
	jpeg_write_scan_header(cinfo);

	rpJPEGCompress(cinfo, srcBuff, row_stride);
	// if (ctx->directCompress) {
		// for (i = 0; i < cinfo->image_height; i++) {
		// 	row_pointer[i] = &(srcBuff[i * row_stride]);
		// }
		// jpeg_write_scanlines(cinfo, row_pointer, cinfo->image_height);
	// } else {
	// 	for (i = 0; i < cinfo->image_height;) {
	// 		for (int j = 0; j < 16; ++j, ++i) {
	// 			u8 *dp = row_pointer[j] = &(srcBuff[i * row_stride]);

	// 			for (int y = 0; y < ctx->height; y++) {
	// 				u16 pix = *(u16*)sp;
	// 				dp[0] = ((pix >> 11) & 0x1f) << 3;
	// 				dp[1] = ((pix >> 5) & 0x3f) << 2;
	// 				dp[2] = (pix & 0x1f) << 3;
	// 				dp += 3;
	// 				sp += ctx->bpp;
	// 			}
	// 			sp += ctx->blankInColumn;
	// 		}
	// 		jpeg_write_scanlines(cinfo, row_pointer, 16);
	// 	}
	// }

	// jpeg_finish_compress(cinfo);
	jpeg_finish_pass_huff(cinfo);
	jpeg_write_file_trailer(cinfo);
	jpeg_term_destination(cinfo);
	// cinfo->global_state = JPEG_CSTATE_START;

	memcpy(&cinfo->alloc.stats, &ctx->alloc_stats->scan, sizeof(struct rp_alloc_stats));
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


void remotePlayKernelCallback() {



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

void rpCaptureScreen(int isTop) {
	u8 dmaConfig[80] = { 0, 0, 4 };
	u32 bufSize = isTop? (tl_pitch * 400) : (bl_pitch * 320);
	u32 phys = isTop ? tl_current : bl_current;
	u32 dest = imgBuffer;
	Handle hProcess = rpHandleHome;

	int ret;

	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, dest, bufSize);
	svc_closeHandle(rpHDma[isTop]);
	rpHDma[isTop] = 0;

	if (isInVRAM(phys)) {
		rpCloseGameHandle();
		svc_startInterProcessDma(&rpHDma[isTop], CURRENT_PROCESS_HANDLE,
			dest, hProcess, 0x1F000000 + (phys - 0x18000000), bufSize, dmaConfig);
		return;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle();
		if (hProcess) {
			ret = svc_startInterProcessDma(&rpHDma[isTop], CURRENT_PROCESS_HANDLE,
				dest, hProcess, rpGameFCRAMBase + (phys - 0x20000000), bufSize, dmaConfig);

		}

		return;
	}
	svc_sleepThread(1000000000);


}



void remotePlaySendFrames() {
	BLIT_CONTEXT
		topContext = { .cinfo = &cinfo_top, .alloc_stats = &alloc_stats_top },
		botContext = { .cinfo = &cinfo_bot, .alloc_stats = &alloc_stats_bot };

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

	cinfo_bot.global_state = cinfo_top.global_state = JPEG_CSTATE_START;
	jpeg_set_quality(&cinfo_top, rpQuality, TRUE);
	for (int i = 0; i < NUM_QUANT_TBLS; ++i)
		cinfo_bot.quant_tbl_ptrs[i] = cinfo_top.quant_tbl_ptrs[i];

	BLIT_CONTEXT *ctxs[] = {&topContext, &botContext};

	for (int i = 0; i < sizeof(ctxs) / sizeof(*ctxs); ++i) {
		if (!ctxs[i]->alloc_stats->qual.offset) {
			memcpy(&ctxs[i]->alloc_stats->qual, &ctxs[i]->cinfo->alloc.stats, sizeof(struct rp_alloc_stats));
		} else {
			memcpy(&ctxs[i]->cinfo->alloc.stats, &ctxs[i]->alloc_stats->qual, sizeof(struct rp_alloc_stats));
		}
	}

	jpeg_jinit_forward_dct(&cinfo_top);
	cinfo_top.fdct_reuse = TRUE;

	cinfo_bot.fdct = cinfo_top.fdct;
	cinfo_bot.fdct_reuse = TRUE;

	jpeg_start_pass_fdctmgr(&cinfo_top);

	u32 isPriorityTop = 1;
	u32 priorityFactor = 0;
	u32 mode = (rpCurrentMode & 0xff00) >> 8;
	u32 factor = (rpCurrentMode & 0xff);
	if (mode == 0) {
		isPriorityTop = 0;
	}
	priorityFactor = factor;

	for (int i = 0; i < rp_pre_proc_buffer_count; ++i) {
		for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
			pre_proc_buffers[i][ci] = jpeg_alloc_sarray((j_common_ptr)&cinfo_top, JPOOL_IMAGE,
				240, (JDIMENSION)(MAX_SAMP_FACTOR * DCTSIZE));
		}
	}
	for (int i = 0; i < rp_thread_count; ++i) {
		for (int ci = 0; ci < MAX_COMPONENTS; ++ci) {
			color_buffers[i][ci] = jpeg_alloc_sarray((j_common_ptr)&cinfo_bot, JPOOL_IMAGE,
				240, (JDIMENSION)MAX_SAMP_FACTOR);
		}
	}
	for (int i = 0; i < rp_mcu_buffer_count; ++i) {
		JBLOCKROW buffer = (JBLOCKROW)jpeg_alloc_large((j_common_ptr)&cinfo_bot, JPOOL_IMAGE, C_MAX_BLOCKS_IN_MCU * sizeof(JBLOCK));
		for (int b = 0; b < C_MAX_BLOCKS_IN_MCU; b++) {
			MCU_buffers[i][b] = buffer + b;
		}
	}

	for (int i = 0; i < sizeof(ctxs) / sizeof(*ctxs); ++i)
		memcpy(&ctxs[i]->alloc_stats->comp, &ctxs[i]->cinfo->alloc.stats, sizeof(struct rp_alloc_stats));

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
	__atomic_store_n(&g_nsConfig->rpConfigLock, 0, __ATOMIC_RELEASE);

	u32 currentUpdating = isPriorityTop;
	u32 frameCount = 0;
	u8 cnt;

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
			rpCaptureScreen(1);
			currentTopId += 1;
			remotePlayBlitInit(&topContext, 400, 240, tl_format, tl_pitch, imgBuffer);
			topContext.transformDst = imgBuffer + 0x00150000;
			topContext.id = (u8)currentTopId;
			topContext.isTop = 1;
			remotePlayBlitCompressed(&topContext);
			rpCompressAndSendPacket(&topContext);
		}
		else {
			// send bottom
			rpCaptureScreen(0);
			currentBottomId += 1;
			remotePlayBlitInit(&botContext, 320, 240, bl_format, bl_pitch, imgBuffer);
			botContext.transformDst = imgBuffer + 0x00150000;
			botContext.id = (u8)currentBottomId;
			botContext.isTop = 0;
			remotePlayBlitCompressed(&botContext);
			rpCompressAndSendPacket(&botContext);
		}

		if (__atomic_load_n(&g_nsConfig->rpConfigLock, __ATOMIC_CONSUME)) {
			// svc_sleepThread(1000000000);
			break;
		}
	}
}

void remotePlayThreadStart() {
	u32 i, ret;
	u8* dataBuf = remotePlayBuffer + 0x2a + 8;
	u32 remainSize;

	imgBuffer = plgRequestMemorySpecifyRegion(0x00200000, 1);

	if (rpInitJpegCompress() != 0) {
		goto final;
	}

	nsDbgPrint("imgBuffer: %08x\n", imgBuffer);
	if (!imgBuffer) {
		goto final;
	}
	rpInitDmaHome();
	// kRemotePlayCallback();
	while (1) {
		remotePlaySendFrames();
	}
	final:
	svc_exitThread();
}

int nwmValParamCallback(u8* buf, int buflen) {
	//rtDisableHook(&nwmValParamHook);
	int i;
	u32* threadStack;
	int stackSize = 0x10000;
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
			rtDisableHook(&nwmValParamHook);
			memcpy(remotePlayBuffer, buf, 0x22 + 8);
			packetLen = initUDPPacket(PACKET_SIZE);
			threadStack = plgRequestMemory(stackSize);
			ret = svc_createThread(&hThread, (void*)remotePlayThreadStart, 0, &threadStack[(stackSize / 4) - 10], 0x10, 2);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay Thread Failed: %08x\n", ret);
			}
		}
	}
	return 0;
}

void remotePlayMain() {
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