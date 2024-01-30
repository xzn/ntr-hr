#include "global.h"

#include "3ds/services/soc.h"
#include "3ds/services/fs.h"

#include <memory.h>
#include <errno.h>

void rtInitLock(RT_LOCK *lock) {
	ACL(lock);
}

void rtAcquireLock(RT_LOCK *lock) {
	while (ATSC(lock)) {
		svcSleepThread(1000000);
	}
}

void rtReleaseLock(RT_LOCK *lock) {
	ACL(lock);
}

void rtGenerateJumpCode(u32 dst, u32* buf) {
	buf[0] = 0xe51ff004;
	buf[1] = dst;
}

void rtInitHook(RT_HOOK *hook, u32 funcAddr, u32 callbackAddr) {
	*hook = (RT_HOOK) { 0 };

	if (!funcAddr || !callbackAddr) {
		showDbg("Missing funcAddr or callbackAddr");
		return;
	}

	s32 ret = rtCheckMemory(funcAddr, 8, MEMPERM_READWRITE | MEMPERM_EXECUTE);
	if (ret != 0) {
		showDbg("rtCheckMemory for addr %08"PRIx32" failed: %08"PRIx32, funcAddr, ret);
		return;
	}

	hook->funcAddr = funcAddr;
	memcpy(hook->bakCode, (void *)funcAddr, 8);
	rtGenerateJumpCode(callbackAddr, hook->jmpCode);
	memcpy(hook->callCode, (void *)funcAddr, 8);
	rtGenerateJumpCode(funcAddr + 8, &hook->callCode[2]);
	rtFlushInstructionCache(hook->callCode, 16);
}

void rtEnableHook(RT_HOOK *hook) {
	if (!hook->funcAddr || hook->isEnabled) {
		return;
	}
	memcpy((void *)hook->funcAddr, hook->jmpCode, 8);
	rtFlushInstructionCache((void *)hook->funcAddr, 8);
	hook->isEnabled = 1;
}

void rtDisableHook(RT_HOOK *hook) {
	if (!hook->funcAddr || !hook->isEnabled) {
		return;
	}
	memcpy((void *)hook->funcAddr, hook->bakCode, 8);
	rtFlushInstructionCache((void *)hook->funcAddr, 8);
	hook->isEnabled = 0;
}

u32 rtAlignToPageSize(u32 size) {
	return ALIGN_TO_PAGE_SIZE(size);
}

u32 rtGetPageOfAddress(u32 addr) {
	return PAGE_OF_ADDR(addr);
}

u32 rtCheckRemoteMemory(Handle hProcess, u32 addr, u32 size, MemPerm perm) {
	MemInfo memInfo;
	PageInfo pageInfo;
	s32 ret = svcQueryMemory(&memInfo, &pageInfo, addr);
	if (ret != 0) {
		nsDbgPrint("svcQueryMemory failed for addr %08"PRIx32": %08"PRIx32"\n", addr, ret);
		return ret;
	}
	if (memInfo.perm == 0) {
		return -1;
	}
	if (memInfo.base_addr + memInfo.size < addr + size) {
		return -1;
	}

	if (perm & MEMPERM_WRITE)
		perm |= MEMPERM_READ;
	if ((memInfo.perm & perm) == perm) {
		return 0;
	}

	perm |= memInfo.perm;

	u32 startPage, endPage;

	startPage = rtGetPageOfAddress(addr);
	endPage = rtGetPageOfAddress(addr + size - 1);
	size = endPage - startPage + 0x1000;

	ret = protectRemoteMemory(hProcess, (void *)startPage, size, perm);
	return ret;
}

u32 rtCheckMemory(u32 addr, u32 size, MemPerm perm) {
	return rtCheckRemoteMemory(getCurrentProcessHandle(), addr, size, perm);
}

u32 rtGetThreadReg(Handle hProcess, u32 tid, u32 *ctx) {
	u32 hThread;
	u32 pKThread, pContext;
	u32 ret;

	ret = svcOpenThread(&hThread, hProcess, tid);
	if (ret != 0) {
		return ret;
	}
	pKThread = kGetKProcessByHandle(hThread);
	kmemcpy(ctx, (void *)pKThread, 160);
	pContext = ctx[0x8c / 4] - 0x10c;
	kmemcpy(ctx, (void *)pContext, 0x10c);
	svcCloseHandle(hThread);
	return 0;
}

u32 rtFlushInstructionCache(void *ptr, u32 size) {
	return svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)ptr, size);
}

int rtRecvSocket(u32 sockfd, u8 *buf, int size)
{
	int ret, pos = 0;
	int tmpsize = size;

	while(tmpsize)
	{
		if((ret = recv(sockfd, &buf[pos], tmpsize, 0)) <= 0)
		{
			if (ret < 0) {
				ret = errno;
				if (ret == -EWOULDBLOCK || ret == -EAGAIN) {
					svcSleepThread(50000000);
					continue;
				}
			}
			return ret;
		}
		pos += ret;
		tmpsize -= ret;
	}

	return size;
}

int rtSendSocket(u32 sockfd, u8 *buf, int size)
{
	int ret, pos = 0;
	int tmpsize = size;

	while(tmpsize)
	{
		if((ret = send(sockfd, &buf[pos], tmpsize, 0)) < 0)
		{
			ret = errno;
			if (ret == -EWOULDBLOCK || ret == -EAGAIN) {
				svcSleepThread(50000000);
				continue;
			}
			return ret;
		}
		pos += ret;
		tmpsize -= ret;
	}

	return size;
}

Handle rtOpenFile(char *fileName) {
	Handle file;
	s32 ret = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL), fsMakePath(PATH_ASCII, fileName), FS_OPEN_READ, 0);
	if (ret != 0) {
		nsDbgPrint("Failed to open file %s: %08"PRIx32"\n", fileName, ret);
		return 0;
	}
	return file;
}

Handle rtOpenFile16(u16 *fileName) {
	Handle file;
	s32 ret = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL), fsMakePath(PATH_UTF16, fileName), FS_OPEN_READ, 0);
	if (ret != 0) {
		nsDbgPrint("Failed to open file: %08"PRIx32"\n", ret);
		return 0;
	}
	return file;
}

u32 rtGetFileSize(Handle file) {
	u64 fileSize;
	s32 ret = FSFILE_GetSize(file, &fileSize);
	if (ret != 0) {
		nsDbgPrint("Failed to get file size: %08"PRIx32"\n", ret);
		return 0;
	}
	return (u32)fileSize;
}

u32 rtLoadFileToBuffer(Handle file, void *pBuf, u32 bufSize) {
	u32 bytesRead;
	s32 ret = FSFILE_Read(file, &bytesRead, 0, (void *)pBuf, bufSize);
	if (ret != 0) {
		nsDbgPrint("Failed to read file: %08"PRIx32"\n", ret);
		return 0;
	}
	return bytesRead;
}

void rtCloseFile(Handle file) {
	FSFILE_Close(file);
}
