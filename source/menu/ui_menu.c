#include "global.h"

#include "3ds/services/hid.h"

#include <string.h>

u32 hasDirectScreenAccess;

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

int initDirectScreenAccess(void) {
	bottomFBRender = plgRequestMemory(BOTTOM_UI_FRAME_SIZE);
	if (!bottomFBRender)
		return -1;
	bottomFBBackup = plgRequestMemory(BOTTOM_UI_FRAME_SIZE);
	if (!bottomFBBackup)
		return -1;

	bottomFB = 0x1848F000 | 0x80000000; // From Luma3DS
	ASL(&hasDirectScreenAccess, 1);
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

static void blank(void) {
	memset((void *)BOTTOM_FRAME, 255, BOTTOM_UI_FRAME_SIZE);
}

void updateScreen(void) {
	memcpy_ctr((void *)bottomFB, (void *)BOTTOM_FRAME, BOTTOM_UI_FRAME_SIZE);
	REG(GPU_FB_BOTTOM_ADDR_1) = bottomFB & ~0x80000000;
	REG(GPU_FB_BOTTOM_ADDR_2) = bottomFB & ~0x80000000;
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
	if (AFAR(&videoRef, 1) == 0) {
		svcKernelSetState(0x10000, 4 | 2, 0, 0);
		lockGameProcess();

		while ((GPU_PSC0_CNT | GPU_PSC1_CNT | GPU_TRANSFER_CNT | GPU_CMDLIST_CNT) & 1) {}

		backupGpuRegs();
		backupVRAMBuffer();

		REG(LCD_TOP_FILLCOLOR) = 0;
		REG(LCD_BOT_FILLCOLOR) = 0;
		blank();
		updateScreen();
	}
}

void releaseVideo(void) {
	if (ASFR(&videoRef, 1) == 0) {
		restoreVRAMBuffer();
		restoreGpuRegs();

		unlockGameProcess();
		svcKernelSetState(0x10000, 4 | 2, 0, 0);
	}
}

// From libctru
static u32 const kDelay = 16, kInterval = 4;
static u32 kOld, kHeld, kDown, kUp, kRepeat, kCount = kDelay;

static void uiScanInput(void) {
	kOld = kHeld;
	kHeld = getKeys();

	kDown = (~kOld) & kHeld;
	kUp = kOld & (~kHeld);

	if (kDelay)
	{
		if (kHeld != kOld)
		{
			kCount = kDelay;
			kRepeat = kDown;
		}

		if (--kCount == 0)
		{
			kCount = kInterval;
			kRepeat = kHeld;
		}
	}
}

static u32 uiKeysDown(void)
{
	return kDown;
}

static u32 uiKeysDownRepeat(void)
{
	u32 ret = kRepeat;
	kRepeat = 0;
	return ret;
}

void waitKeysDelay3(void);
void waitKeysDelay(void);
u32 waitKeys(void) {
	u32 keys;
	do {
		if (ntrConfig->isNew3DS && (REG(PDN_LGR_SOCMODE) & 5) == 5) {
			waitKeysDelay3();
		} else {
			waitKeysDelay();
		}

		uiScanInput();
		keys = uiKeysDown() | uiKeysDownRepeat();
	} while (keys == 0);
	return keys;
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

static int drawString(const char *str, int x, int y, u8 r, u8 g, u8 b, int newLine) {
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

static int print(const char *s, int x, int y, u8 r, u8 g, u8 b) {
	return drawString(s, x, y, r, g, b, 1);
}

const char *plgTranslate(const char *msg) {
	return msg;
}

static void showMsgCommon(const char *msg, const char *title) {
	acquireVideo();
	while(1) {
		blank();
		if (title) {
			print(title, 10, 10, 255, 0, 255);
			print(msg, 10, 44, 255, 0, 0);
		} else {
			print(msg, 10, 10, 255, 0, 0);
		}
		print(plgTranslate("Press [B] to close."), 10, 220, 0, 0, 255);
		updateScreen();
		u32 keys = waitKeys();
		if (keys & KEY_B) {
			break;
		}
	}
	releaseVideo();
}

int showMsgVA(const char *file_name, int line_number, const char *func_name, const char* fmt, va_list va) {
	if (!canUseUI()) {
		disp(100, DBG_CL_MSG);
		svcSleepThread(1000000000);
		return 0;
	}

	char title[LOCAL_TITLE_BUF_SIZE];

	if (file_name && func_name) {
		u64 ticks = svcGetSystemTick();
		u64 mono_us = ticks * 1000000000ULL / SYSCLOCK_ARM11;
		u32 pid = getCurrentProcessId();
		xsnprintf(title, LOCAL_TITLE_BUF_SIZE, DBG_VERBOSE_TITLE, (u32)(mono_us / 1000000), (u32)(mono_us % 1000000), pid, file_name, line_number, func_name);
	} else {
		*title = 0;
	}

	char msg[LOCAL_MSG_BUF_SIZE];
	xvsnprintf(msg, LOCAL_MSG_BUF_SIZE, fmt, va);

	showMsgCommon(msg, *title ? title : NULL);

	return 0;
}

s32 showMenu(const char *title, u32 entriesCount, const char *captions[]) {
	return showMenuEx(title, entriesCount, captions, 0, 0);
}

s32 showMenuEx(const char *title, u32 entriesCount, const char *captions[], const char *descriptions[], u32 selectOn) {
	return showMenuEx2(title, entriesCount, captions, descriptions, selectOn, NULL);
}

#define MENU_ITEMS_MAX (10)
#define MENU_ITEM_HEIGHT (CHAR_HEIGHT + 1)
s32 showMenuEx2(const char *title, u32 entriesCount, const char *captions[], const char *descriptions[], u32 selectOn, u32 *keysPressed) {
	u32 i;
	int select = 0;
	char buf[LOCAL_TITLE_BUF_SIZE];
	u32 pos;
	u32 x = 10, keys = 0;
	u32 drawStart, drawEnd;

	if (selectOn < entriesCount) {
		select = selectOn;
	}

	while(1) {
		blank();
		pos = 10;
		print(title, x, pos, 255, 0, 0);
		print("http://44670.org/ntr", 10, 220, 0, 0, 255);
		pos += 20;
		drawStart = (select / MENU_ITEMS_MAX) * MENU_ITEMS_MAX;
		drawEnd = drawStart + MENU_ITEMS_MAX;
		if (drawEnd > entriesCount) {
			drawEnd = entriesCount;
		}
		for (i = drawStart; i < drawEnd; i++) {
			strnjoin(buf, LOCAL_TITLE_BUF_SIZE, i == (u32)select ? " * " : "   ", captions[i]);
			print(buf, x, pos, 0, 0, 0);
			pos += MENU_ITEM_HEIGHT;
		}
		if (descriptions) {
			if (descriptions[select]) {
				print(descriptions[select], x, pos, 0, 0, 255);
			}
		}
		updateScreen();
		keys = waitKeys();
		if (keys & KEY_DOWN) {
			select += 1;
			if (select >= (int)entriesCount) {
				select = 0;
			}
		}
		if (keys & KEY_UP) {
			select -= 1;
			if (select < 0) {
				select = entriesCount - 1;
			}
		}
		if (keysPressed) {
			*keysPressed = keys;
			return select;
		}
		if (keys & KEY_A) {
			return select;
		}
		if (keys & KEY_B) {
			return -1;
		}
	}
}

bool hidShouldUseIrrst(void)
{
	return 0;
}
