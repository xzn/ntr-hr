#include "global.h"

u32 videoRef = 0;
u32 savedVideoState[10];
u32 allowDirectScreenAccess = 0;

u32 bottomFrameBuffer = 0x1F000000;
u32 bottomRenderingFrameBuffer = 0x1F000000;
u32 bottomAllocFrameBuffer = 0;
u32 hGSPProcess = 0;




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
	bottomAllocFrameBuffer = plgRequestMemory(BOTTOM_FRAME_SIZE);
	if (bottomAllocFrameBuffer == 0) {
		return -1;
	}
	allowDirectScreenAccess = 1;
	bottomRenderingFrameBuffer = bottomAllocFrameBuffer;

	return 0;

}

u32 controlVideo(u32 cmd, u32 arg1, u32 /*arg2*/, u32 /*rg3*/) {
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
		memcpy_ctr((void *)((getPhysAddr(bottomFrameBuffer) | 0x80000000)), (void *)BOTTOM_FRAME1, BOTTOM_FRAME_SIZE);
	*(vu32*)(IoBasePdc + 0x568) = getPhysAddr(bottomFrameBuffer);
	*(vu32*)(IoBasePdc + 0x56C) = getPhysAddr(bottomFrameBuffer);
	*(vu32*)(IoBasePdc + 0x570) = 0x00080301;
	*(vu32*)(IoBasePdc + 0x55c) = 0x014000f0;
	*(vu32*)(IoBasePdc + 0x590) = 0x000002d0;
	// if (bottomFrameBuffer == BOTTOM_FRAME1)
	// 	svc_flushProcessDataCache(0xffff8001, BOTTOM_FRAME1, BOTTOM_FRAME_SIZE);
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
	return 0x1F000000;
}

void acquireVideo(void) {
	if (videoRef == 0) {
		bottomFrameBuffer = decideBottomFrameBufferAddr();
		if (bottomAllocFrameBuffer != 0)
			bottomRenderingFrameBuffer = bottomAllocFrameBuffer;
		else
			bottomRenderingFrameBuffer = bottomFrameBuffer;
		*(vu32*)(IoBaseLcd + 0x204) = 0;
		*(vu32*)(IoBaseLcd + 0xA04) = 0;
		savedVideoState[0] = *(vu32*)(IoBasePdc + 0x568);
		savedVideoState[1] = *(vu32*)(IoBasePdc + 0x56C);
		savedVideoState[2] = *(vu32*)(IoBasePdc + 0x570);
		savedVideoState[3] = *(vu32*)(IoBasePdc + 0x55c);
		savedVideoState[4] = *(vu32*)(IoBasePdc + 0x590);
		*(vu32*)(IoBasePdc + 0x568) = getPhysAddr(bottomFrameBuffer);
		*(vu32*)(IoBasePdc + 0x56C) = getPhysAddr(bottomFrameBuffer);
		*(vu32*)(IoBasePdc + 0x570) = 0x00080301;
		*(vu32*)(IoBasePdc + 0x55c) = 0x014000f0;
		*(vu32*)(IoBasePdc + 0x590) = 0x000002d0;

		blank(0, 0, 320, 240);
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
	}
}

u32 getKey(void) {
	return (*(vu32*)(IoBasePad) ^ 0xFFF) & 0xFFF;
}


int confirmKey(u32 keyCode, int time) {
	int i;
	for (i = 0; i < time; i++) {
		if (getKey() != keyCode) {
			return 0;
		}
	}
	return 1;
}

u32 waitKey(void) {
	u32 key = 0;
	while (1) {
		if (getKey() == 0) {
			if (confirmKey(0, 0x1000)) {
				break;
			}
		}
	}
	while(1) {
		key = getKey();
		if (key != 0) {
			if (confirmKey(key, 0x10000)) {
				break;
			}
		}
	}
	while (1) {
		if (getKey() == 0) {
			if (confirmKey(0, 0x1000)) {
				break;
			}
		}
	}

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
