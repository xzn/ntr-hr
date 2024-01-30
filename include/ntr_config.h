#ifndef NTR_CONFIG_H
#define NTR_CONFIG_H

#include "3ds/types.h"

typedef struct {
	u32 noPlugins;
	u32 CTRPFCompat;
	u32 remotePlayBoost;
	u32 noLoaderMem;
	u32 memSizeTotal;
	u32 delayInit;
} PLGLOADER_EX_INFO;

typedef struct {
	u32 nsUseDbg;
	PLGLOADER_EX_INFO plg;
} NTR_EX_CONFIG;

typedef struct {
	u32 bootNTRVersion;
	u32 isNew3DS;
	u32 firmVersion;

	u32 IoBasePad;
	u32 IoBaseLcd;
	u32 IoBasePdc;
	u32 PMSvcRunAddr;
	u32 PMPid;
	u32 HomeMenuPid;

	u32 HomeMenuVersion;
	u32 HomeMenuInjectAddr; // FlushDataCache Function
	u32 HomeFSReadAddr;
	u32 HomeFSUHandleAddr;
	u32 HomeCardUpdateInitAddr;
	u32 HomeAptStartAppletAddr;

	u32 KProcessHandleDataOffset;
	u32 KProcessPIDOffset;
	u32 KProcessCodesetOffset;
	u32 ControlMemoryPatchAddr1;
	u32 ControlMemoryPatchAddr2;
	u32 KernelFreeSpaceAddr_Optional;
	u32 KMMUHaxAddr;
	u32 KMMUHaxSize;
	u32 InterProcessDmaFinishState;
	u32 fsUserHandle;
	u32 arm11BinStart;
	u32 arm11BinSize;
	u32 showDbgFunc;

	u32 memMode;
	char ntrFilePath[0x100];

	/* BootNTR's NTR_CONFIG ends here */
	NTR_EX_CONFIG ex;
} NTR_CONFIG;

#endif
