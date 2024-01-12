
#include "global.h"

#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR   3
#define O_CREAT  4



u8 *image_buf = NULL;





int sdf_open(char *filename, int mode)
{
	FS_archive sdmcArchive = { 0x9, (FS_path){ PATH_EMPTY, 1, "" }, 0, 0 };

	Handle hd;
	Result retv;
	FS_path filePath;

	filePath.type = PATH_CHAR;
	filePath.size = strlen(filename) + 1;
	filePath.data = (u8*)filename;

	retv = FSUSER_OpenFileDirectly(fsUserHandle, &hd, sdmcArchive, filePath, mode, 0);
	if (retv < 0){
		return retv;
	}

	return hd;
}

int sdf_read(int fd, int offset, void *buf, int size)
{
	int read_size;
	Result retv;

	retv = FSFILE_Read(fd, (u32*)&read_size, offset, buf, size);
	if (retv < 0)
		return retv;

	return read_size;
}

int sdf_write(int fd, int offset, void *buf, int size)
{
	int write_size;
	Result retv;

	retv = FSFILE_Write(fd, (u32*)&write_size, offset, buf, size, 0);
	if (retv < 0)
		return retv;

	return write_size;
}

int sdf_close(int fd)
{
	Result retv;

	retv = FSFILE_Close(fd);

	return retv;
}

/**************************************************************/


struct BitmapHeader
{
	char id[2];
	u32  filesize;
	u32  reserved;
	u32  offset;
	u32  headsize;
	u32  width;
	u32  height;
	u16  planes;
	u16  bpp;
	u32  comp;
	u32  bitmapsize;
	u32  hres;
	u32  vres;
	u32  colors;
	u32  impcolors;
} __attribute__((packed));


void put_le32(void *dst, u32 val)
{
	u8 *dp = (u8*)dst;

	dp[0] = val & 0xff;
	dp[1] = (val >> 8) & 0xff;
	dp[2] = (val >> 16) & 0xff;
	dp[3] = (val >> 24) & 0xff;
}

void put_le16(void *dst, u32 val)
{
	u8 *dp = (u8*)dst;

	dp[0] = val & 0xff;
	dp[1] = (val >> 8) & 0xff;
}


void bmp_write(u8 *bmp_buf, int width, int height, char *name)
{
	struct BitmapHeader *bmp;
	int fd, fsize, hsize;

	bmp = (struct BitmapHeader*)bmp_buf;
	hsize = sizeof(struct BitmapHeader);
	fsize = width*height * 3 + sizeof(struct BitmapHeader);

	memset(bmp, 0, hsize);

	bmp->id[0] = 'B';
	bmp->id[1] = 'M';
	put_le32(&bmp->filesize, fsize);
	put_le32(&bmp->offset, hsize);
	put_le32(&bmp->headsize, 0x28);
	put_le32(&bmp->width, width);
	put_le32(&bmp->height, height);
	put_le16(&bmp->planes, 1);
	put_le16(&bmp->bpp, 24);
	put_le32(&bmp->bitmapsize, width*height * 3);
	put_le32(&bmp->hres, 2834);
	put_le32(&bmp->vres, 2834);

	fd = sdf_open(name, O_RDWR | O_CREAT);
	if (fd > 0){
		sdf_write(fd, 0, bmp_buf, fsize);
		sdf_close(fd);
	}
}


void rev_image(u8 *dst, int width, int height, u8 *src, int src_pitch, int format)
{
	int x, y, bpp;
	u8 *dp, *sp;
	u32 px;

	if ((format & 0x0f) == 0){
		bpp = 4;
	}
	else if ((format & 0x0f) == 1){
		bpp = 3;
	}
	else{
		bpp = 2;
	}
	format &= 0x0f;

	for (y = 0; y < height; y++){
		dp = dst + y*width * 3;
		sp = src + y*bpp;
		for (x = 0; x < width; x++){
			if (format == 0){
				dp[0] = sp[0];
				dp[1] = sp[1];
				dp[2] = sp[2];
			}
			else if (format == 1){
				dp[0] = sp[0];
				dp[1] = sp[1];
				dp[2] = sp[2];
			}
			else if (format == 2){
				px = (sp[1] << 8) | sp[0];
				dp[0] = (px << 3) & 0xf8;
				dp[1] = (px >> 3) & 0xfc;
				dp[2] = (px >> 8) & 0xf8;
			}
			else if (format == 3){
				px = (sp[1] << 8) | sp[0];
				dp[0] = (px << 2) & 0xf8;
				dp[1] = (px >> 3) & 0xf8;
				dp[2] = (px >> 8) & 0xf8;
			}
			else if (format == 4){
				px = (sp[1] << 8) | sp[0];
				dp[0] = (px << 0) & 0xf0;
				dp[1] = (px >> 4) & 0xf0;
				dp[2] = (px >> 8) & 0xf0;
			}
			dp += 3;
			sp += src_pitch;
		}
	}
}


int bmp_index;

int get_file_index(void)
{
	int fd;
	char name[64];
	int id;

	id = 0;
	while (1){
		xsprintf(name, "/top_%04d.bmp", id);
		fd = sdf_open(name, O_RDWR);
		if (fd <= 0){
			break;
		}
		sdf_close(fd);
		id += 1;
	}

	return id;
}


void allocImageBuf() {
	if (image_buf) {
		return;
	}
	u32 out_addr = plgRequestMemory(0x00100000);
	nsDbgPrint("    out_addr: %08x", out_addr);
	image_buf = (u8*)out_addr;
}


void do_screen_shoot(void)
{
	u32 tl_fbaddr[2];
	u32 bl_fbaddr[2];
	u32 current_fb;
	u32 tl_format, bl_format;
	u32 tl_pitch, bl_pitch;
	u8 *rev_buf;
	u32 fbP2VOffset = 0xc0000000;

	u8 *workingBuffer = 0;

	char name[64];

	if (ntrConfig->memMode == NTR_MEMMODE_DEFAULT) {
		if (!image_buf) {
			allocImageBuf();
		}
		if (image_buf == NULL) {
			return;
		}
		workingBuffer = image_buf;
	}
	else {
		workingBuffer = (u8 *)plgRequestTempBuffer(0xA0000);
	}

	if (workingBuffer == 0) {
		return;
	}


	tl_fbaddr[0] = REG(IoBasePdc + 0x468) + fbP2VOffset;
	tl_fbaddr[1] = REG(IoBasePdc + 0x46c) + fbP2VOffset;
	bl_fbaddr[0] = REG(IoBasePdc + 0x568) + fbP2VOffset;
	bl_fbaddr[1] = REG(IoBasePdc + 0x56c) + fbP2VOffset;
	tl_format = REG(IoBasePdc + 0x470);
	bl_format = REG(IoBasePdc + 0x570);
	tl_pitch = REG(IoBasePdc + 0x490);
	bl_pitch = REG(IoBasePdc + 0x590);

	// TOP screen
	current_fb = REG(IoBasePdc + 0x478);
	current_fb &= 1;
	kmemcpy((void*)(workingBuffer + 0x50000), (void*)tl_fbaddr[current_fb], 240 * 400 * 3);

	rev_buf = workingBuffer + sizeof(struct BitmapHeader);
	rev_image(rev_buf, 400, 240, workingBuffer + 0x50000, tl_pitch, tl_format);
	xsprintf(name, "/top_%04d.bmp", bmp_index);
	bmp_write(workingBuffer, 400, 240, name);


	// Bottom screen
	current_fb = REG(IoBasePdc + 0x578);
	current_fb &= 1;
	kmemcpy((void*)(workingBuffer + 0x50000), (void*)bl_fbaddr[current_fb], 240 * 320 * 3);

	rev_buf = workingBuffer + sizeof(struct BitmapHeader);
	rev_image(rev_buf, 320, 240, workingBuffer + 0x50000, bl_pitch, bl_format);
	xsprintf(name, "/bot_%04d.bmp", bmp_index);
	bmp_write(workingBuffer, 320, 240, name);
	disp(100, 0x01ff00ff);

	bmp_index += 1;
}

u32 takeScreenShot() {
	vu32 i;

	for (i = 0; i < 0x1000000; i++) {
	}
	controlVideo(CONTROLVIDEO_RELEASEVIDEO, 0, 0, 0);
	svc_sleepThread(100000000);
	do_screen_shoot();
	controlVideo(CONTROLVIDEO_ACQUIREVIDEO, 0, 0, 0);
	return 1;
}

u32 getRegionSize(Handle hProcess, u32 addr) {
	u32 size = 0, ret;
	while (1) {
		ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr + size, 0x1000);
		if (ret != 0) {
			break;
		}
		size += 0x1000;
	}
	return size;
}

u32 fastCopyMemory(Handle hDst, u8 *ptrDst, Handle hSrc, u8 *ptrSrc, u32 size) {
	u32 ret = 0;
	ret = copyRemoteMemory(hDst, ptrDst, hSrc, ptrSrc, size);
	if (ret == 0) {
		return 0;
	}
	u32 off = 0;
	for (off = 0; off < size; off += 0x1000) {
		ret = copyRemoteMemory(hDst, ptrDst + off, hSrc, ptrSrc + off, 0x1000);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

#define VRAM_ADDR (0x1F000000)
#define VRAM_SIZE (0x00600000)
u32 loadSaveVRAM(int fd, int isLoad) {
	u32 off;
	int ret;
	svc_flushProcessDataCache(CURRENT_PROCESS_HANDLE, VRAM_ADDR, VRAM_SIZE);
	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, VRAM_ADDR, VRAM_SIZE);
	for (off = 0; off < 0x00600000; off += 0x00100000) {
		if (isLoad) {
			ret = sdf_read(fd, off, image_buf, 0x00100000);
			if (ret >= 0) {
				memcpy((u8 *)VRAM_ADDR + off, image_buf, 0x00100000);
			}
		}
		else {
			memcpy(image_buf, (u8 *)VRAM_ADDR + off, 0x00100000);
			ret = sdf_write(fd, off, image_buf, 0x00100000);
		}
		if (ret < 0) {
			return ret;
		}
	}
	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, VRAM_ADDR, VRAM_SIZE);
	svc_flushProcessDataCache(CURRENT_PROCESS_HANDLE, VRAM_ADDR, VRAM_SIZE);
	return 0;
}


u32 saveRegion(Handle hProcess, u32 addr, int fd, u32 offsetInFile, u32 size) {
	u32 off = 0;
	u32 currentRead;
	u32 ret;
	while (off < size) {
		currentRead = size - off;
		if (currentRead > 0x00100000) {
			currentRead = 0x00100000;
		}
		ret = fastCopyMemory(CURRENT_PROCESS_HANDLE, image_buf, hProcess, (u8 *)addr + off, currentRead);
		if (ret != 0) {
			return ret;
		}
		sdf_write(fd, off + offsetInFile, image_buf, currentRead);
		off += currentRead;
	}
	return 0;
}

u32 loadRegion(Handle hProcess, u32 addr, int fd, u32 offsetInFile, u32 size) {
	u32 off = 0;
	u32 currentRead, ret;

	while (off < size) {
		currentRead = size - off;
		if (currentRead > 0x00100000) {
			currentRead = 0x00100000;
		}
		sdf_read(fd, off + offsetInFile, image_buf, currentRead);
		ret = fastCopyMemory(hProcess, (u8 *)addr + off, CURRENT_PROCESS_HANDLE, image_buf, currentRead);
		if (ret != 0) {
			return ret;
		}
		off += currentRead;
	}
	return 0;
}

u32 saveAddrMap[] = {
	0x00100000, 0x08000000, 0x14000000, 0x30000000
};

extern PLGLOADER_INFO *g_plgInfo;;
u32 instantSave(int id, int isLoad) {
	delayUi();
	u32 gamePid = g_plgInfo->gamePluginPid;
	if (gamePid == 0) {
		showDbg("game not running", 0, 0);
		return 0;
	}
	Handle hProcess = 0, hGSPProcess = 0;
	u32 ret;
	int fd = 0;
	ret = svc_openProcess(&hProcess, gamePid);
	if (ret != 0) {
		showDbg("open Game Process failed: %08x", ret, 0);
		return 0;
	}
	ret = svc_openProcess(&hGSPProcess, 0x14);
	if (ret != 0) {
		showDbg("open GSP Process failed: %08x", ret, 0);
		goto final;
	}

	char buf[40];
	xsprintf(buf, "/save_%08x_%d.rts", g_plgInfo->tid[0], id);
	fd = sdf_open(buf, isLoad ? O_RDWR : (O_RDWR | O_CREAT));
	if (fd <= 0) {
		showDbg("open file failed: %08x", fd, 0);
		goto final;
	}

	u32 addrIdx, regionSize, offsetInFile = 0, addr;
	ret = loadSaveVRAM(fd, isLoad);
	if (ret != 0) {
		showDbg("VRAM failed: %08x", ret, 0);
		goto final;
	}
	offsetInFile += VRAM_SIZE;

	addr = 0x08000000;
	if (isLoad) {
		ret = loadRegion(hGSPProcess, addr, fd, offsetInFile, VRAM_SIZE);
	}
	else {
		ret = saveRegion(hGSPProcess, addr, fd, offsetInFile, VRAM_SIZE);
	}
	if (ret != 0) {
		showDbg("GSP VRAM failed: %08x", ret, 0);
		goto final;
	}
	offsetInFile += VRAM_SIZE;

	for (addrIdx = 0; addrIdx < 3; addrIdx++) {
		addr = saveAddrMap[addrIdx];
		regionSize = getRegionSize(hProcess, addr);
		if (regionSize > 0) {
			if (isLoad) {
				ret = loadRegion(hProcess, addr, fd, offsetInFile, regionSize);
			}
			else {
				ret = saveRegion(hProcess, addr, fd, offsetInFile, regionSize);
			}
			if (ret != 0) {
				showDbg("region %08x failed: %08x", addr, ret);
				goto final;
			}
		}
		offsetInFile += regionSize;
	}
	if (isLoad)
		showDbg("Load %d OK, GamePID: %08x.", id, gamePid);
	else
		showDbg("Save %d OK, GamePID: %08x.", id, gamePid);
	final:
	if (fd > 0) {
		sdf_close(fd);
	}
	svc_closeHandle(hProcess);
	svc_closeHandle(hGSPProcess);
	return 0;
}



u32 instantSaveMenu() {
	if (ntrConfig->memMode != NTR_MEMMODE_DEFAULT) {
		showMsg("RTS is not compatitable with extended memory mode.");
		return 1;
	}
	if (!image_buf) {
		allocImageBuf();
	}
	if (image_buf == NULL){
		showMsg("FATAL image buffer alloc failed.");
		return 1;
	}

	char* entries[7];
	char buf[5][20];

	int r, i;
	for (i = 0; i < 3; i++) {
		xsprintf(buf[i], plgTranslate("Save %d"), i);
		xsprintf(buf[i + 3], plgTranslate("Load %d"), i);
		entries[i] = buf[i];
		entries[i + 3] = buf[i + 3];
	}
	entries[6] = plgTranslate("Hint: Back to Home Menu before Load/Save.");
	while (1){
		r = showMenu(plgTranslate("Instant Save"), 7, entries);
		if (r == -1) {
			break;
		}
		if ((r >= 0) && (r <= 2)) {
			instantSave(r, 0);
			break;
		}
		if ((r >= 3) && (r <= 5)) {
			instantSave(r - 3, 1);
			break;
		}
	}

	return 1;
}



u32 cpuClockUi() {
	acquireVideo();
	char* entries[8];
	int r;
	entries[0] = plgTranslate("268MHz, L2 Disabled (Default)");
	entries[1] = plgTranslate("804MHz, L2 Disabled");
	entries[2] = plgTranslate("268MHz, L2 Enabled");
	entries[3] = plgTranslate("804MHz, L2 Enabled (Best)");
	while (1) {
		blank(0, 0, 320, 240);
		r = showMenu(plgTranslate("CPU Clock"), 4, entries);
		if (r == -1) {
			break;
		}
		if ((r >= 0) && (r <= 3)) {
			u32 ret = svc_kernelSetState(10, r, 0, 0);
			if (ret != 0) {
				showDbg("kernelSetState failed: %08x", ret, 0);
			}
			setCpuClockLock(r);
			break;
		}
	}
	releaseVideo();
	return 0;
}


void plgDoReboot() {
	Handle hNSS = 0;
	u32 ret;
	ret = srv_getServiceHandle(NULL, &hNSS, "ns:s");
	if (ret != 0) {
		showDbg("open ns:s failed: %08x", ret, 0);
		return;
	}
	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x00100180;
	cmdbuf[1] = 0x00000000;
	cmdbuf[2] = 0x00000000;
	cmdbuf[3] = 0x00000000;
	cmdbuf[4] = 0x00000000;
	cmdbuf[5] = 0x00000000;
	cmdbuf[6] = 0x00000000;
	svc_sendSyncRequest(hNSS);
	svc_closeHandle(hNSS);

}

void plgDoPowerOff() {
	Handle hNSS = 0;
	u32 ret;
	ret = srv_getServiceHandle(NULL, &hNSS, "ns:s");
	if (ret != 0) {
		showDbg("open ns:s failed: %08x", ret, 0);
		return;
	}
	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x000E0000;
	cmdbuf[1] = 0x00000000;
	cmdbuf[2] = 0x00000000;
	cmdbuf[3] = 0x00000000;
	cmdbuf[4] = 0x00000000;
	cmdbuf[5] = 0x00000000;
	cmdbuf[6] = 0x00000000;
	svc_sendSyncRequest(hNSS);
	svc_closeHandle(hNSS);

}


u32 powerMenu() {
	acquireVideo();
	char* entries[8];
	int r;
	// vu32 i;
	entries[0] = plgTranslate("Reboot");
	entries[1] = plgTranslate("Power Off");
	while (1) {
		r = showMenu(plgTranslate("Power"), 2, entries);
		if (r == -1) {
			break;
		}
		if (r == 0) {
			// for (i = 0; i < 0x05000000; i++) {
			// }
			svc_sleepThread(0x05000000);
			plgDoReboot();
			break;
		}
		if (r == 1) {
			// for (i = 0; i < 0x05000000; i++) {
			// }
			svc_sleepThread(0x05000000);
			plgDoPowerOff();
			break;
		}
	}
	releaseVideo();
	return 1;
}

int GSPPid = 0x14;
int isGSPPatchRequired = 0;
u32 gspPatchAddr = 0;
int bklightValue = 50;

int checkBacklightSupported() {

	
	Handle hGSPProcess = 0;
	u32 ret;
	u8 buf[16] = { 0 };
	static u8 desiredHeader[16] = { 0x30, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0x01, 0x5C, 0x80, 0xE2, 0x8C, 0x01, 0xD0, 0xE5 };

	if (!(ntrConfig->isNew3DS)) {
		return 1;
	}

	ret = svc_openProcess(&hGSPProcess, GSPPid);
	if (ret != 0) {
		return 0;
	}
	gspPatchAddr = 0x0010740C;
	copyRemoteMemory(getCurrentProcessHandle(), buf, hGSPProcess, (void *)gspPatchAddr, 16);

	if (memcmp(buf, desiredHeader, 16) == 0) {
		goto requirePatch;
	}

	// 11.3.0
	gspPatchAddr = 0x0010743C;
	copyRemoteMemory(getCurrentProcessHandle(), buf, hGSPProcess, (void *)gspPatchAddr, 16);

	if (memcmp(buf, desiredHeader, 16) == 0) {
		goto requirePatch;
	}

	svc_closeHandle(hGSPProcess);
	return 0;

requirePatch:
	svc_closeHandle(hGSPProcess);
	isGSPPatchRequired = 1;
	return 1;
}


void reliableWriteU8(vu8* addr, u8 data) {
	while (1) {
		*addr = data;
		mdelay(1);
		if ((*addr) == data) {
			return;
		}
	}
}

int patchGSP() {
	if (!isGSPPatchRequired) {
		return 0;
	}
	u32 ret;
	u32 buf[4] = { 0, 0 };
	ret = writeRemoteProcessMemory(GSPPid, gspPatchAddr + 0x68, 4, buf);
	ret = writeRemoteProcessMemory(GSPPid, gspPatchAddr + 0xC8, 4, buf);
	
	if (ret == 0) {
		isGSPPatchRequired = 0;
	}
	return ret;
}


void updateBklight() {
	u32 t;
	t = bklightValue * 2;
	if (bklightValue == 1) {
		t = 1;
	}
	if (bklightValue == 100) {
		t = 0xff;
	}

	// http://3dbrew.org/wiki/LCD_Registers

	*(vu32*)(IoBaseLcd + 0x240) = t;
	*(vu32*)(IoBaseLcd + 0xa40) = t;


}

void adjustBklight(int adj) {
	bklightValue += adj;
	if (bklightValue < 1) {
		bklightValue = 1;
	}
	if (bklightValue > 100) {
		bklightValue = 100;
	}
	updateBklight();
}

u32 backlightMenu() {  
	if (isGSPPatchRequired) {
		patchGSP();
	}
	char buf[50];
	while (1) {
		blank(0, 0, 320, 240);
		xsprintf(buf, "backlight: %d", bklightValue);
		print(buf, 10, 10, 255, 0, 0);
		print(plgTranslate("Press Up/Down/Left/Right to adjust."), 10, 220, 0, 0, 255);
		updateScreen();
		u32 key = waitKey();
		if (key == BUTTON_DU) {
			adjustBklight(1);
		}
		if (key == BUTTON_DD) {
			adjustBklight(-1);
		}
		if (key == BUTTON_DL) {
			adjustBklight(-10);
		}
		if (key == BUTTON_DR) {
			adjustBklight(10);
		}
		if (key == BUTTON_B) {
			break;
		}
	}
	return 1;
}

u32 nightShiftUi() {
	int configUpdated = 0;
	acquireVideo();
	char* entries[11];
	int r;
	entries[0] = plgTranslate("Disabled");
	entries[1] = plgTranslate("Reduce Blue Light Level 1");
	entries[2] = plgTranslate("Reduce Blue Light Level 2");
	entries[3] = plgTranslate("Reduce Blue Light Level 3");
	entries[4] = plgTranslate("Reduce Blue Light Level 4");
	entries[5] = plgTranslate("Reduce Blue Light Level 5");
	entries[6] = plgTranslate("Invert Colors");
	entries[7] = plgTranslate("Grayscale");
	entries[8] = plgTranslate("Hint: Must be enabled before starting game. Will set CPU to L2+804MHz on New3DS.");
	while (1) {
		blank(0, 0, 320, 240);
		r = showMenu(plgTranslate("Screen Filter"), 8, entries);
		if (r == -1) {
			break;

		}
		if ((r >= 0) && (r <= 7)) {
			g_plgInfo->nightShiftLevel = r;
			if (r > 0) {
				if (ntrConfig->isNew3DS) {
					setCpuClockLock(3);
				}
			}
			configUpdated = 1;
			break;
		}
	}
	releaseVideo();
	if (configUpdated) {
		plgTryUpdateConfig();
	}
	return 1;
}
void screenshotMain(void) {
	nsDbgPrint("initializing screenshot plugin\n");
	plgRegisterMenuEntry(1, plgTranslate("Take Screenshot"), takeScreenShot);
	plgRegisterMenuEntry(1, plgTranslate("Real-Time Save (Experimental)"), instantSaveMenu);

	nsDbgPrint("fsUserHandle: %08x\n", fsUserHandle);
	bmp_index = get_file_index();
	nsDbgPrint("bmp index is: %d", bmp_index);

	if (ntrConfig->isNew3DS) {
		plgRegisterMenuEntry(1, plgTranslate("CPU Clock (New3DS Only)"), cpuClockUi);
	}
	plgRegisterMenuEntry(1, plgTranslate("Power"), powerMenu);
	if (checkBacklightSupported()){
		plgRegisterMenuEntry(1, plgTranslate("Backlight"), backlightMenu);
	}

	plgRegisterMenuEntry(1, plgTranslate("Screen Filter"), nightShiftUi);
}





