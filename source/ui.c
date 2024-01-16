#include "global.h"

u32 videoRef = 0;
u32 savedVideoState[10];
u32 allowDirectScreenAccess = 0;

u32 bottomFrameBuffer = 0x1F000000;
u32 bottomRenderingFrameBuffer = 0x1F000000;
u32 bottomAllocFrameBuffer = 0;
u32 bottomFrameBufferPitch = BOTTOM_UI_PITCH;
u32 hGSPProcess = 0;

u32 bottomFrameIsVid = 0;
u32 bottomFrameSavedVid = 0;

int builtinDrawString(char* str, int x, int y, char r, char g, char b, int newLine) {
	int len = strlen(str);
	int i, chWidth, currentX = x, totalLen = 0;

	for (i = 0; i < len; i++) {
		chWidth = 8;
		int strln = str[i] == '\n';
		if (strln || currentX + chWidth > BOTTOM_WIDTH) {
			if (!newLine) {
				return totalLen;
			}
			y += 12;
			if (y + 12 > BOTTOM_HEIGHT) {
				return totalLen;
			}
			currentX = x;
			if (strln) {
				continue;
			}
		}
		paint_letter(str[i], currentX, y, r, g, b, BOTTOM_FRAME1);
		totalLen += chWidth;
		currentX += chWidth;
	}
	return totalLen;
}

extern drawStringTypeDef plgDrawStringCallback;

int drawString(char* str, int x, int y, u8 r, u8 g, u8 b, int newLine) {
	if (plgDrawStringCallback) {
		return plgDrawStringCallback(str, x, y, r, g, b, newLine);
	}
	return builtinDrawString(str, x, y, r, g, b, newLine);
}

void print(char* s, int x, int y, u8 r, u8 g, u8 b) {
	drawString(s, x, y, r, g, b, 1);
}


u32 getPhysAddr(u32 vaddr) {
	if(vaddr >= 0x14000000 && vaddr<0x1c000000)return vaddr + 0x0c000000;//LINEAR memory
	if(vaddr >= 0x30000000 && vaddr<0x40000000)return vaddr - 0x10000000;//Only available under system-version v8.0 for certain processes, see here: http://3dbrew.org/wiki/SVC#enum_MemoryOperation
	if(vaddr >= 0x1F000000 && vaddr<0x1F600000)return vaddr - 0x07000000;//VRAM
	return vaddr;
}

u32 initDirectScreenAccess(void) {
	u32 ret;
	ret = protectMemory((void *)0x1F000000, 0x600000);
	if (ret != 0) {
		return ret;
	}
	bottomAllocFrameBuffer = plgRequestMemory(BOTTOM_FRAME_VID_SIZE);
	if (bottomAllocFrameBuffer == 0) {
		return -1;
	}
	allowDirectScreenAccess = 1;

	return 0;

}

u32 controlVideo(u32 cmd, u32 arg1, u32 /*arg2*/, u32 /*rg3*/) {
	bottomFrameIsVid = 1;

	if (cmd == CONTROLVIDEO_ACQUIREVIDEO) {
		acquireVideo();
		return 0;
	}
	if (cmd == CONTROLVIDEO_RELEASEVIDEO) {
		releaseVideo();
		return 0;
	}
	if (cmd == CONTROLVIDEO_GETFRAMEBUFFER) {
		return bottomRenderingFrameBuffer;
	}
	if (cmd == CONTROLVIDEO_SETFRAMEBUFFER) {
		bottomRenderingFrameBuffer = arg1;
		return 0;
	}
	if (cmd == CONTROLVIDEO_UPDATESCREEN) {
		updateScreen();
		return 0;
	}
	return 0;
}


void debounceKey(void) {
	vu32 t;
	for (t = 0; t < 0x00100000; t++) {
	}
	// svc_sleepThread(0x00100000);
}

void delayUi(void) {
	vu32 t;
	for (t = 0; t < 0x03000000; t++) {
	}
	// svc_sleepThread(0x03000000);
}

void mdelay(u32 m) {
	vu32 t;
	vu32 i;
	for (i = 0; i < m; i++) {
		for (t = 0; t < 0x00100000; t++) {
		}
	}
	// svc_sleepThread(0x00100000 * m);
}

void memcpy_ctr(void* dst, void* src, size_t size);
void updateScreen(void) {
	// if (bottomFrameBuffer != BOTTOM_FRAME1)
	if (bottomFrameIsVid)
		memcpy_ctr((void *)(getPhysAddr(bottomFrameBuffer) | 0x80000000), (void *)BOTTOM_FRAME1, BOTTOM_FRAME_VID_SIZE);
	else
		for (int j = 0; j < BOTTOM_WIDTH; ++j)
			memcpy_ctr(
				(u8 *)(getPhysAddr(bottomFrameBuffer) | 0x80000000) + j * bottomFrameBufferPitch,
				(u8 *)BOTTOM_FRAME1 + j * bottomFrameBufferPitch,
				BOTTOM_UI_PITCH
			);
	*(vu32*)(IoBasePdc + 0x568) = getPhysAddr(bottomFrameBuffer);
	*(vu32*)(IoBasePdc + 0x56C) = getPhysAddr(bottomFrameBuffer);
	*(vu32*)(IoBasePdc + 0x570) = 0x00080300 | (bottomFrameIsVid ? BOTTOM_VID_FORMAT : BOTTOM_UI_FORMAT);
	*(vu32*)(IoBasePdc + 0x55c) = 0x014000f0;
	*(vu32*)(IoBasePdc + 0x590) = bottomFrameIsVid ? BOTTOM_VID_PITCH : bottomFrameBufferPitch;
}

s32 showMenu(char* title, u32 entryCount, char* captions[]) {
	return showMenuEx(title, entryCount, captions, 0, 0);
}

s32 showMenuEx(char* title, u32 entryCount, char* captions[], char* descriptions[],  u32 selectOn) {
	return showMenuEx2(title, entryCount, captions, descriptions, selectOn, NULL);
}

s32 showMenuEx2(char* title, u32 entryCount, char* captions[], char* descriptions[],  u32 selectOn, u32 *keyPressed) {
	u32 maxCaptions = 10;
	u32 i;
	int select = 0;
	char buf[200];
	u32 pos;
	u32 x = 10, key = 0;
	u32 drawStart, drawEnd;

	if (selectOn < entryCount) {
		select = selectOn;
	}

	while(1) {
		blank(0, 0, 320, 240);
		pos = 10;
		print(title, x, pos, 255, 0, 0);
		print("http://44670.org/ntr", 10, 220, 0, 0, 255);
		pos += 20;
		drawStart = (select / maxCaptions) * maxCaptions;
		drawEnd = drawStart + maxCaptions;
		if (drawEnd > entryCount) {
			drawEnd = entryCount;
		}
		for (i = drawStart; i < drawEnd; i++) {
			strcpy(buf, ((int)i == select) ? " * " : "   ");
			strcat(buf, captions[i]);
			print(buf, x, pos, 0, 0, 0);
			pos += 13;
		}
		if (descriptions) {
			if (descriptions[select]) {
				print(descriptions[select], x, pos, 0, 0, 255);
			}
		}
		updateScreen();
		while((key = waitKey()) == 0);
		if (key == BUTTON_DD) {
			select += 1;
			if (select >= (int)entryCount) {
				select = 0;
			}
		}
		if (key == BUTTON_DU) {
			select -= 1;
			if (select < 0) {
				select = entryCount - 1;
			}
		}
		if (keyPressed) {
			*keyPressed = key;
			return select;
		}
		if (key == BUTTON_A) {
			return select;
		}
		if (key == BUTTON_B) {
			return -1;
		}
	}
}

int showMsgNoPause(char* msg) {
	if (ShowDbgFunc) {
		typedef void(*funcType)(char*);
		((funcType)(ShowDbgFunc))(msg);
		return 0;
	}
	return 0;
}

int showMsgExtra(char* msg, const char *file_name, int line_number, const char *func_name) {
	if (ShowDbgFunc) {
		typedef void(*funcType)(char*);
		((funcType)(ShowDbgFunc))(msg);
		svc_sleepThread(1000000000);
		return 0;
	}

	if (!allowDirectScreenAccess) {
		return 0;
	}

	u64 ticks = svc_getSystemTick();
	u64 mono_us = ticks / SYSTICK_PER_US;
	u32 pid = getCurrentProcessId();
	char extra[0x100];
	xsprintf(extra, "[%d.%d][%x]%s:%d:%s", (u32)(mono_us / 1000000), (u32)(mono_us % 1000000), pid, file_name, line_number, func_name);

	acquireVideo();

	while(1) {
		blank(0, 0, 320, 240);
		print(extra, 10, 10, 255, 0, 255);
		print(msg, 10, 44, 255, 0, 0);
		print(plgTranslate("Press [B] to close."), 10, 220, 0, 0, 255);
		updateScreen();
		u32 key = waitKey();
		if (key == BUTTON_B) {
			break;
		}
	}
	releaseVideo();
	return 0;
}

int showMsgDirect(char* msg) {
	if (ShowDbgFunc) {
		typedef void(*funcType)(char*);
		((funcType)(ShowDbgFunc))(msg);
		svc_sleepThread(1000000000);
		return 0;
	}
	if (!allowDirectScreenAccess) {
		return 0;
	}
	acquireVideo();

	while(1) {
		blank(0, 0, 320, 240);
		print(msg, 10, 10, 255, 0, 0);
		print(plgTranslate("Press [B] to close."), 10, 220, 0, 0, 255);
		updateScreen();
		u32 key = waitKey();
		if (key == BUTTON_B) {
			break;
		}
	}
	releaseVideo();
	return 0;
}

void showDbgShared(char* fmt, u32 v1, u32 v2) {
	char buf[400];

	nsDbgPrintShared(fmt, v1, v2);
	xsprintf(buf, fmt, v1, v2);
	showMsgDirect(buf);
}

extern PLGLOADER_INFO *g_plgInfo;
u32 decideBottomFrameBufferAddr() {
	bottomFrameBufferPitch = BOTTOM_UI_PITCH;
	if (g_plgInfo) {
		u32 tidLow = g_plgInfo->tid[0];
		if ((tidLow == 0x00125500) || (tidLow == 0x000D6E00) || (tidLow == 0x00125600)) {
			// The Legend Of Zelda Majoras Mask 3D
			return 0x1F500000;
		}
		if ((tidLow == 0x000EE000) || (tidLow == 0x000EDF00) || (tidLow == 0x000B8B00)) {
			// Super Smash Bros
			return 0x1F500000;
		}
		if ((tidLow == 0x0014F200) || (tidLow == 0x0014F100) || (tidLow == 0x0014F000)) {
			// Animal Crossing : Happy Home Designer
			return 0x1F500000;
		}
		if ((tidLow == 0x00086200) || (tidLow == 0x00086300) || (tidLow == 0x00086400)) {
			// Animal Crossing
			return 0x1F500000;
		}
		if ((tidLow == 0x00132600) || (tidLow == 0x00132800)) {
			// Mario RPG Papaer MIX
			return 0x1F500000;
		}
	}
	u32 bl_fbaddr[2];
	bl_fbaddr[0] = REG(IoBasePdc + 0x568);
	bl_fbaddr[1] = REG(IoBasePdc + 0x56c);
	if (
		isInVRAM(bl_fbaddr[0]) ||
		isInVRAM(bl_fbaddr[1])
	) {
		bottomFrameBufferPitch = REG(IoBasePdc + 0x590);
		return MIN(MIN(bl_fbaddr[0], bl_fbaddr[1]), 0x18600000 - BOTTOM_FRAME_VID_SIZE) + 0x07000000;
	}
	u32 tl_fbaddr[2];
	tl_fbaddr[0] = REG(IoBasePdc + 0x468);
	tl_fbaddr[1] = REG(IoBasePdc + 0x46c);
	if (
		isInVRAM(tl_fbaddr[0]) ||
		isInVRAM(tl_fbaddr[1])
	)
		return 0x1F500000;
	return 0x1F000000;
}

void acquireVideo(void) {
	if (videoRef == 0) {
		bottomFrameBuffer = decideBottomFrameBufferAddr();
		bottomRenderingFrameBuffer = bottomFrameBuffer;
		*(vu32*)(IoBaseLcd + 0x204) = 0;
		*(vu32*)(IoBaseLcd + 0xA04) = 0;
		if (!bottomFrameIsVid && bottomAllocFrameBuffer)
			for (int j = 0; j < BOTTOM_WIDTH; ++j)
				memcpy_ctr(
					(u8 *)bottomAllocFrameBuffer + j * BOTTOM_UI_PITCH,
					(u8 *)bottomFrameBuffer + j * bottomFrameBufferPitch,
					BOTTOM_UI_PITCH
				);
		else if (bottomFrameIsVid && bottomAllocFrameBuffer) {
			bottomFrameSavedVid = 1;
			memcpy_ctr(
				(void *)bottomAllocFrameBuffer,
				(void *)bottomFrameBuffer,
				BOTTOM_FRAME_VID_SIZE
			);
		}
		savedVideoState[0] = *(vu32*)(IoBasePdc + 0x568);
		savedVideoState[1] = *(vu32*)(IoBasePdc + 0x56C);
		savedVideoState[2] = *(vu32*)(IoBasePdc + 0x570);
		savedVideoState[3] = *(vu32*)(IoBasePdc + 0x55c);
		savedVideoState[4] = *(vu32*)(IoBasePdc + 0x590);
		blank(0, 0, 320, 240);
		updateScreen();
	}
	videoRef ++;
}

void releaseVideo(void) {
	videoRef --;
	if (videoRef == 0) {
		*(vu32*)(IoBasePdc + 0x568) = savedVideoState[0];
		*(vu32*)(IoBasePdc + 0x56C) = savedVideoState[1];
		*(vu32*)(IoBasePdc + 0x570) = savedVideoState[2];
		*(vu32*)(IoBasePdc + 0x55c) = savedVideoState[3];
		*(vu32*)(IoBasePdc + 0x590) = savedVideoState[4];
		if (!bottomFrameIsVid && bottomAllocFrameBuffer)
			for (int j = 0; j < BOTTOM_WIDTH; ++j)
				memcpy_ctr(
					(u8 *)bottomFrameBuffer + j * bottomFrameBufferPitch,
					(u8 *)bottomAllocFrameBuffer + j * BOTTOM_UI_PITCH,
					BOTTOM_UI_PITCH
				);
		else if (bottomFrameSavedVid) {
			bottomFrameSavedVid = 0;
			memcpy_ctr(
				(void *)bottomFrameBuffer,
				(void *)bottomAllocFrameBuffer,
				BOTTOM_FRAME_VID_SIZE
			);
		}
	}
}

u32 getKey(void) {
	return (*(vu32*)(IoBasePad) ^ 0xFFF) & 0xFFF;
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


void blinkColor(u32 c){
	*(vu32*)(IoBaseLcd + 0x204) = c;
	// for (t = 0; t < 100000; t++) {
	// }
	svc_sleepThread(100000);
	*(vu32*)(IoBaseLcd + 0x204) = 0;
	// for (t = 0; t < 100000; t++) {
	// }
	svc_sleepThread(100000);
}
