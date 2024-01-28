#include "global.h"

#include <string.h>

u32 allowDirectScreenAccess;

#define BOTTOM_WIDTH 320
#define BOTTOM_HEIGHT 240
#define BOTTOM_UI_BPP 2
#define BOTTOM_UI_PITCH (BOTTOM_HEIGHT * BOTTOM_UI_BPP)
#define BOTTOM_UI_FORMAT 3
#define BOTTOM_UI_FRAME_SIZE (BOTTOM_WIDTH * BOTTOM_UI_PITCH)
#define BOTTOM_FRAME (bottomFBRender)

u32 bottomFB;
u32 bottomFBRender;
u32 bottomFBBackup;

int showMsgVerbose(const char *, const char *, int, const char *) {
	// TODO
	return 0;
}

int showMsgRaw(const char *) {
	// TODO
	return 0;
}

int initDirectScreenAccess(void) {
	bottomFBRender = plgRequestMemory(BOTTOM_UI_FRAME_SIZE);
	if (!bottomFBRender)
		return -1;
	bottomFBBackup = plgRequestMemory(BOTTOM_UI_FRAME_SIZE);
	if (!bottomFBBackup)
		return -1;

	bottomFB = 0x1F000000;
	allowDirectScreenAccess = 1;
	return 0;
}

static void write_color(u32 addr, u8 r, u8 g, u8 b){
  u16 color = (((u16)r >> 3) << 11) | (((u16)g >> 3) << 6) | (((u16)b >> 3) << 1) | 1;
  *(u16 *)addr = color;
}

static void paint_pixel(u32 x, u32 y, u8 r, u8 g, u8 b, u32 addr){
	if (x >= BOTTOM_WIDTH) {
		return;
	}
	if (y >= BOTTOM_HEIGHT) {
		return;
	}

	u32 coord = BOTTOM_UI_PITCH * x + BOTTOM_UI_PITCH - (y * BOTTOM_UI_BPP) - BOTTOM_UI_BPP;
	write_color(addr + coord, r, g, b);
}

static void paint_square(int x, int y, u8 r, u8 g, u8 b, int w, int h, u32 addr){
	int x1, y1;

	for (x1 = x; x1 < x + w; ++x1){
		for (y1 = y; y1 < y + h; ++y1){
			paint_pixel(x1, y1, r, g, b, addr);
		}
	}
}

static void blank(void) {
	paint_square(0, 0, 255, 255, 255, BOTTOM_WIDTH, BOTTOM_HEIGHT, BOTTOM_FRAME);
}

void updateScreen(void) {
	memcpy_ctr((void *)bottomFB, (void *)bottomFBRender, BOTTOM_UI_FRAME_SIZE);
	REG(GPU_FB_BOTTOM_ADDR_1) = bottomFB;
	REG(GPU_FB_BOTTOM_ADDR_2) = bottomFB;
	REG(GPU_FB_BOTTOM_FMT) = 0x00080300 | BOTTOM_UI_FORMAT;
	REG(GPU_FB_BOTTOM_SIZE) = 0x014000f0;
	REG(GPU_FB_BOTTOM_STRIDE) = BOTTOM_UI_PITCH;
}

static u32 videoRef;
static u32 gpuRegsBackup[5];
static Handle hGameProcess;

static void backupGpuRegs(void) {
	gpuRegsBackup[0] = REG(GPU_FB_BOTTOM_ADDR_1);
	gpuRegsBackup[1] = REG(GPU_FB_BOTTOM_ADDR_2);
	gpuRegsBackup[2] = REG(GPU_FB_BOTTOM_FMT);
	gpuRegsBackup[3] = REG(GPU_FB_BOTTOM_SIZE);
	gpuRegsBackup[4] = REG(GPU_FB_BOTTOM_STRIDE);
}

static void restoreGpuRegs(void) {
	REG(GPU_FB_BOTTOM_ADDR_1) = gpuRegsBackup[0];
	REG(GPU_FB_BOTTOM_ADDR_2) = gpuRegsBackup[1];
	REG(GPU_FB_BOTTOM_FMT) = gpuRegsBackup[2];
	REG(GPU_FB_BOTTOM_SIZE) = gpuRegsBackup[3];
	REG(GPU_FB_BOTTOM_STRIDE) = gpuRegsBackup[4];
}

static void lockGameProcess(void) {
	if (plgLoader->gamePluginPid) {
		s32 res = svcOpenProcess(&hGameProcess, plgLoader->gamePluginPid);
		if (res == 0) {
			res = svcControlProcess(hGameProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0);
			if (res != 0) {
				nsDbgPrint("Locking game process failed: %08"PRIx32"\n", res);
				svcCloseHandle(hGameProcess);
				hGameProcess = 0;
			}
		} else {
			nsDbgPrint("Open game process failed: %08"PRIx32"\n", res);
			hGameProcess = 0;
		}
	}
}

static void unlockGameProcess(void) {
	if (hGameProcess) {
		s32 res = svcControlProcess(hGameProcess, PROCESSOP_SCHEDULE_THREADS, 0, 0);
		if (res != 0) {
			nsDbgPrint("Unlocking game process failed: %08"PRIx32"\n", res);
		}
		svcCloseHandle(hGameProcess);
		hGameProcess = 0;
	}
}

static void backupVRAMBuffer(void) {
	memcpy_ctr((void *)bottomFBBackup, (void *)bottomFB, BOTTOM_UI_FRAME_SIZE);
}

static void restoreVRAMBuffer(void) {
	memcpy_ctr((void *)bottomFB, (void *)bottomFBBackup, BOTTOM_UI_FRAME_SIZE);
}

void acquireVideo(void) {
	if (AFAR(videoRef, 1) == 0) {
		svcKernelSetState(0x10000, 4, 0, 0);
		lockGameProcess();

		while((GPU_PSC0_CNT | GPU_PSC1_CNT | GPU_TRANSFER_CNT | GPU_CMDLIST_CNT) & 1) {}
		backupGpuRegs();
		backupVRAMBuffer();

		REG(LCD_TOP_FILLCOLOR) = 0;
		REG(LCD_BOT_FILLCOLOR) = 0;
		blank();
		updateScreen();
	}
}

void releaseVideo(void) {
	if (ASFR(videoRef, 1) == 0) {
		restoreVRAMBuffer();
		restoreGpuRegs();

		unlockGameProcess();
		svcKernelSetState(0x10000, 4, 0, 0);
	}
}

static int confirmKey(u32 keyCode, int times) {
	int i;
	for (i = 0; i < times; i++) {
		if (getKey() != keyCode) {
			return 0;
		}
	}
	return 1;
}

static s32 const refreshScreenCount = 0x10000;
static u32 waitKeyAndRefreshScreen(u32 need_key, int times, s32 *refreshScreen) {
	u32 key;
	s32 count = *refreshScreen;
	while (1) {
		key = need_key ? getKey() : 0;
		if (need_key ? key != 0 : key == getKey()) {
			if (confirmKey(key, times)) {
				break;
			}
			count -= times;
		}
		--count;
		if (count < 0) {
			count += refreshScreenCount;
			updateScreen();
		}
	}
	*refreshScreen = count;
	return key;
}

u32 waitKey(void) {
	u32 key;
	s32 refreshScreen = refreshScreenCount;
	waitKeyAndRefreshScreen(0, 0x1000, &refreshScreen);
	key = waitKeyAndRefreshScreen(1, 0x10000, &refreshScreen);
	waitKeyAndRefreshScreen(0, 0x1000, &refreshScreen);
	return key;
}

#include "font.h"
#define CHAR_ADVANCE (CHAR_WIDTH)
#define LINE_HEIGHT (CHAR_HEIGHT)

static void paint_letter(char letter, int x, int y, u8 r, u8 g, u8 b, int addr) {

	int i;
	int k;
	int c;
	unsigned char mask;
	unsigned char l;
	if ((letter < 32) || (letter > 127)) {
		letter = '?';
	}
	c = (letter - 32) * CHAR_HEIGHT;

	for (i = 0; i < CHAR_HEIGHT; ++i){
		mask = 0b10000000;
		l = font[i + c];
		for (k = 0; k < CHAR_WIDTH; ++k){
			if ((mask >> k) & l){
				paint_pixel(k + x, i + y, r, g, b, addr);
			} else {
				paint_pixel(k + x, i + y, 255, 255, 255, addr);
			}
		}
	}
}

static int drawString(char *str, int x, int y, u8 r, u8 g, u8 b, int newLine) {
	int len = strlen(str);
	int i, currentX = x, totalLen = 0;

	for (i = 0; i < len; ++i) {
		int strln = str[i] == '\n';
		if (strln || currentX + CHAR_ADVANCE > BOTTOM_WIDTH) {
			if (!newLine) {
				return totalLen;
			}
			y += LINE_HEIGHT;
			if (y + LINE_HEIGHT > BOTTOM_HEIGHT) {
				return totalLen;
			}
			currentX = x;
			if (strln) {
				continue;
			}
		}
		paint_letter(str[i], currentX, y, r, g, b, BOTTOM_FRAME);
		totalLen += CHAR_ADVANCE;
		currentX += CHAR_ADVANCE;
	}
	return totalLen;
}

int print(char *s, int x, int y, u8 r, u8 g, u8 b) {
	return drawString(s, x, y, r, g, b, 1);
}
