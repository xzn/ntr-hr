#include "global.h"

void dumpKernel() {

}

Handle hCurrentProcess = 0;
u32 currentPid = 0;

u32 getCurrentProcessId() {
	svc_getProcessId(&currentPid, 0xffff8001);
	return currentPid;
}

u32 getCurrentProcessHandle() {
	u32 handle = 0;
	u32 ret;

	if (hCurrentProcess != 0) {
		return hCurrentProcess;
	}
	svc_getProcessId(&currentPid, 0xffff8001);
	ret = svc_openProcess(&handle, currentPid);
	if (ret != 0) {
		showDbg("openProcess failed, ret: %08x", ret, 0);
		return 0;
	}
	hCurrentProcess = handle;
	return hCurrentProcess;
}

u32 getCurrentProcessKProcess() {
	return kGetCurrentKProcess();
}

u32 mapRemoteMemory(Handle hProcess, u32 addr, u32 size) {
	u32 outAddr = 0;
	u32 ret;
	u32 newKP = kGetKProcessByHandle(hProcess);
	u32 oldKP = kGetCurrentKProcess();


	//u32 oldPid = kSwapProcessPid(newKP, 1);

	kSetCurrentKProcess(newKP);
	ret = svc_controlMemory(&outAddr, addr, addr, size, NS_DEFAULT_MEM_REGION + 3, 3);
	kSetCurrentKProcess(oldKP);
	//kSwapProcessPid(newKP, oldPid);
	if (ret != 0) {
		showDbg("svc_controlMemory failed: %08x", ret, 0);
		return ret;
	}
	if (outAddr != addr) {
		showDbg("outAddr: %08x, addr: %08x", outAddr, addr);
		return 0;
	}
	//showMsg("mapremote done");
	return 0;
}

u32 mapRemoteMemoryInSysRegion(Handle hProcess, u32 addr, u32 size) {
	u32 outAddr = 0;
	u32 ret;
	u32 newKP = kGetKProcessByHandle(hProcess);
	u32 oldKP = kGetCurrentKProcess();

	u32 oldPid = kSwapProcessPid(newKP, 1);

	kSetCurrentKProcess(newKP);
	ret = svc_controlMemory(&outAddr, addr, addr, size, NS_DEFAULT_MEM_REGION + 3, 3);
	kSetCurrentKProcess(oldKP);
	kSwapProcessPid(newKP, oldPid);
	if (ret != 0) {
		showDbg("svc_controlMemory failed: %08x", ret, 0);
		return ret;
	}
	if (outAddr != addr) {
		showDbg("outAddr: %08x, addr: %08x", outAddr, addr);
		return 0;
	}
	//showMsg("mapremote done");
	return 0;
}

u32 controlMemoryInSysRegion(u32* outAddr, u32 addr0, u32 addr1, u32 size, u32 op, u32 perm) {
	u32 currentKP = kGetCurrentKProcess();
	u32 oldPid = kSwapProcessPid(currentKP, 1);
	u32 ret = svc_controlMemory(outAddr, addr0, addr1, size, op, perm);
	kSwapProcessPid(currentKP, oldPid);
	return ret;
}

u32 protectRemoteMemory(Handle hProcess, void* addr, u32 size) {
	u32 outAddr = 0;

	return svc_controlProcessMemory(hProcess, addr, addr, size, 6, 7);
}

u32 protectMemory(void* addr, u32 size) {
	return protectRemoteMemory(getCurrentProcessHandle(), addr, size);
}

u32 copyRemoteMemory(Handle hDst, void* ptrDst, Handle hSrc, void* ptrSrc, u32 size) {
	u8 dmaConfig[80] = {0, 0, 4};
	u32 hdma = 0;
	u32 state, i, ret;

	ret = svc_flushProcessDataCache(hSrc, (u32)ptrSrc, size);
	if (ret != 0) {
		nsDbgPrint("svc_flushProcessDataCache(hSrc) failed.\n");
		return ret;
	}
	ret = svc_flushProcessDataCache(hDst, (u32)ptrDst, size);
	if (ret != 0) {
		nsDbgPrint("svc_flushProcessDataCache(hDst) failed.\n");
		return ret;
	}

	ret = svc_startInterProcessDma(&hdma, hDst, ptrDst, hSrc, ptrSrc, size, dmaConfig);
	if (ret != 0) {
		return ret;
	}
	for (i = 0; i < 10000; i++ ) {
		state = 0;
		ret = svc_getDmaState(&state, hdma);
		if (ntrConfig->InterProcessDmaFinishState == 0) {
			if (state == 4) {
				break;
			}
		}
		else {
			if (state == ntrConfig->InterProcessDmaFinishState) {
				break;
			}
		}

		svc_sleepThread(1000000);
	}

	if (i >= 10000) {
		showDbg("readRemoteMemory time out %08x", state, 0);
		svc_closeHandle(hdma);
		return 1;
	}

	svc_closeHandle(hdma);
	ret = svc_invalidateProcessDataCache(hDst, (u32)ptrDst, size);
	if (ret != 0) {
		return ret;
	}
	return 0;
}

u32 getProcessTIDByHandle(u32 hProcess, u32 tid[]) {
	u8 bufKProcess[0x100], bufKCodeSet[0x100];
	u32  pKCodeSet, pKProcess, ret;

	pKProcess = kGetKProcessByHandle(hProcess);
	kmemcpy(bufKProcess, (void*)pKProcess, 0x100);
	pKCodeSet = *(u32*)(&bufKProcess[KProcessCodesetOffset]);
	kmemcpy(bufKCodeSet, (void*)pKCodeSet, 0x100);
	u32* pTid = (u32*)(&bufKCodeSet[0x5c]);
	tid[0] = pTid[0];
	tid[1] = pTid[1];
}


u32 getProcessInfo(u32 pid, u8* pname, u32 pname_size, u32 tid[], u32* kpobj) {
	u8 bufKProcess[0x100], bufKCodeSet[0x100];
	u32 hProcess, pKCodeSet, pKProcess, ret;
	u8 buf[300];

	ret = 0;
	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		nsDbgPrint("openProcess failed: %08x\n", ret, 0);
		return ret;
	}

	pKProcess = kGetKProcessByHandle(hProcess);
	kmemcpy(bufKProcess, (void*)pKProcess, 0x100);
	pKCodeSet = *(u32*)(&bufKProcess[KProcessCodesetOffset]);
	kmemcpy(bufKCodeSet, (void*)pKCodeSet, 0x100);
	bufKCodeSet[0x5A] = 0;
	u8* pProcessName = &bufKCodeSet[0x50];
	u32* pTid = (u32*)(&bufKCodeSet[0x5c]);
	tid[0] = pTid[0];
	tid[1] = pTid[1];
	strncpy(pname, pProcessName, pname_size - 1);
	pname[pname_size - 1] = 0;
	*kpobj = pKProcess;

	final:
	if (hProcess) {
		svc_closeHandle(hProcess);
	}
	return ret;
}


void dumpRemoteProcess(u32 pid, u8* fileName, u32 startAddr) {
	u32 hProcess, hFile, ret, i, state, t;
	u8 buf[0x1020];
	u32 dmaConfig[20] = {0};
	u32 base = startAddr;
	u32 off = 0, addr;
	u32 kProcess = 0;


	FS_path testPath = (FS_path){PATH_CHAR, strlen(fileName) + 1, fileName};
	ret = FSUSER_OpenFileDirectly(fsUserHandle, &hFile, sdmcArchive, testPath, 7, 0);
	if (ret != 0) {
		showDbg("openFile failed: %08x", ret, 0);
		goto final;
	}
	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		showDbg("openProcess failed: %08x", ret, 0);
		goto final;
	}

	while(1) {
		addr = base + off;
		xsprintf(buf, "addr: %08x", base + off);
		showMsgNoPause(buf);
		memset(buf, 0, sizeof(buf));
		ret = protectRemoteMemory(hProcess, (void*)addr, 0x1000);
		if (ret != 0) {
			showDbg("dump finished at addr: %08x", addr, 0);
			goto final;
		}
		ret = copyRemoteMemory(0xffff8001, buf, hProcess, (void*)addr, 0x1000);
		if (ret != 0) {
			showDbg("readRemoteMemory failed: %08x", ret, 0);
		}
		FSFILE_Write(hFile, &t, off, (u32*)buf, 0x1000, 0);	
		off += 0x1000;
	}

	final:
	if (hProcess) {
		svc_closeHandle(hProcess);
	}
	if (hFile) {
		svc_closeHandle(hFile);
	}
}

void dumpRemoteProcess2(u32 pid, u8* fileName) {
	u32 hdebug = 0, hfile = 0;
	u32 ret;
	u32 hprocess = 0;
	u32 t, off, base = 0x00100000;
	u8 buf[0x1020];



	FS_path testPath = (FS_path){PATH_CHAR, strlen(fileName) + 1, fileName};
	ret = FSUSER_OpenFileDirectly(fsUserHandle, &hfile, sdmcArchive, testPath, 7, 0);
	if (ret != 0) {
		showDbg("openFile failed: %08x", ret, 0);
		goto final;
	}
	showDbg("hfile: %08x", hfile, 0);

	ret = svc_debugActiveProcess(&hdebug, pid);
	if (ret != 0) {
		showDbg("debugActiveProcess failed: %08x", ret, 0);
		goto final;
	}
	showDbg("hdebug: %08x", hdebug, 0);

	off = 0;
	while(1) {
		xsprintf(buf, "addr: %08x", base + off);
		print(buf, 10, 10, 255, 0, 0);
		updateScreen();
		ret = svc_readProcessMemory(buf, hdebug, off + base, 0x1000);
		if (ret != 0) {
			showDbg("readmemory addr = %08x, ret = %08x", base + off, ret);
			goto final;
		}
		FSFILE_Write(hfile, &t, off, (u32*)buf, 0x1000, 0);	
		off += 0x1000;
	}
	final:
	if (hdebug) {
		svc_closeHandle(hdebug);
	}
	if (hfile) {
		svc_closeHandle(hfile);
	}
}


void dumpCode(u32 base, u32 size, u8* fileName) {
	u32 off = 0;
	u8 tmpBuffer[0x1000];
	Handle handle = 0;
	u8 buf[200];
	u32 t = 0;
	vu32 i;

	showMsg("dumpcode");
	FS_path testPath = (FS_path){PATH_CHAR, strlen(fileName) + 1, fileName};
	showMsg("testpath");
	FSUSER_OpenFileDirectly(fsUserHandle, &handle, sdmcArchive, testPath, 7, 0);
	showMsg("openfile");
	if (handle == 0) {
		showMsg("open file failed");
		return;
	}

	while(off < size) {
		
		xsprintf(buf, "addr: %08x", base + off);
		showMsgNoPause(buf);
		for (i = 0; i < 1000000; i++) {
		}
		kmemcpy(tmpBuffer, (void*)(base + off), 0x1000);
		FSFILE_Write(handle, &t, off, tmpBuffer, 0x1000, 0);
		off += 0x1000;
	}
}

u32 writeRemoteProcessMemory(int pid, u32 addr, u32 size, u32* buf) {
	u32 hProcess = 0, ret;
	ret = svc_openProcess(&hProcess, pid);
	if (ret != 0) {
		return ret;
	}
	ret = rtCheckRemoteMemoryRegionSafeForWrite(hProcess, addr, size);
	if (ret != 0) {

		goto final;
	}
	ret = copyRemoteMemory(hProcess, (void*)addr, 0xffff8001, buf, size);
	if (ret != 0) {
		goto final;
	}
	final:
	if (hProcess) {
		svc_closeHandle(hProcess);
	}
	return ret;
}

void initSMPatch() {
	u32 hProcess = 0, ret;
	u32 buf[20];
	//write(0x101820,(0x01,0x00,0xA0,0xE3,0x1E,0xFF,0x2F,0xE1),pid=0x3)
	buf[0] = 0xe3a00001;
	buf[1] = 0xe12fff1e;
	ret = writeRemoteProcessMemory(3, 0x101820, 8, buf);
	if (ret != 0) {
		showDbg("patch sm failed: %08x", ret, 0);
	}
}

u32 showStartAddrMenu() {
	u8* entries[8];
	u32 r;
	entries[0] = "0x00100000";
	entries[1] = "0x08000000";
	entries[2] = "0x14000000";
	while (1){
		r = showMenu("set addr", 3, entries);
		if (r == 0) {
			return 0x00100000;
			break;
		}
		if (r == 1) {
			return 0x08000000;
			break;
		}
		if (r == 2) {
			return 0x14000000;
			break;
		}
	}
}

void processManager() {
	u32 pids[100];
	u32 ret, off, i, t;
	u32 pidCount = 0;
	u8 buf[800];
	u8* captions[100];
	u8 pidbuf[50];
	u8 pname[20];
	u32 tid[4];
	static dumpCnt = 0;
	u32 startAddr;

	ret = svc_getProcessList(&pidCount, pids, 100);
	if (ret != 0) {
		showDbg("getProcessList failed: %08x", ret, 0);
		return;
	}

	buf[0] = 0; off = 0;
	for (i = 0; i < pidCount; i++) {
		xsprintf(pidbuf, "%08x", pids[i]);
		captions[i] = &buf[off];
		strcpy(&buf[off], pidbuf);
		off += strlen(pidbuf) + 1;
	}

	while(1) {
		u32 r = showMenu("processList", pidCount, captions);
		if (r == -1) {
			return;
		}
		u8* actCaptions[] =  {"dump", "info"};
		u32 act = showMenu(captions[r], 2, actCaptions);
		if (act == 0) {
			xsprintf(pidbuf, "/dump_pid%x_%d.dmp", pids[r], dumpCnt);
			dumpCnt += 1;
			startAddr = showStartAddrMenu();
			dumpRemoteProcess(pids[r], pidbuf, startAddr);
		}
		if (act == 1) {
			ret = getProcessInfo(pids[r], pname, sizeof(pname), tid, &t);
			if (ret == 0) {
				showDbg("pname: %s", (u32)pname, 0);
				showDbg("tid: %08x%08x", tid[1], tid[0]);
			} else {
				showDbg("getProcessInfo error: %d", ret, 0);
			}
		}
	}

}