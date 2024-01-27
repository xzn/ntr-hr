#ifndef NS_H
#define NS_H

#include "3ds/types.h"
#include "ntr_config.h"
#include "rp.h"
#include "rt.h"
#include "constants.h"

void nsDbgPrintRaw(const char* fmt, ...);
#define nsDbgPrint(fmt, ...) do { \
	u64 nsDbgPrint_ticks__ = svcGetSystemTick(); \
	u64 nsDbgPrint_mono_us__ = nsDbgPrint_ticks__ * 1000000000ULL / SYSCLOCK_ARM11; \
	u32 nsDbgPrint_pid__ = getCurrentProcessId(); \
	nsDbgPrintRaw("[%d.%06d][%x]%s:%d:%s " fmt, (u32)(nsDbgPrint_mono_us__ / 1000000), (u32)(nsDbgPrint_mono_us__ % 1000000), nsDbgPrint_pid__, __FILE__, __LINE__, __func__, ## __VA_ARGS__); \
} while (0)

typedef enum {
	NS_INITMODE_FROMBOOT,
	NS_INITMODE_FROMHOOK = 2,
} NS_INITMODE;

typedef struct {
	NS_INITMODE initMode;
	u32 startupCommand;
	u32 hSOCU;

	u8* debugBuf;
	u32 debugBufSize;
	u32 debugPtr;
	u32 debugReady;

	RT_LOCK debugBufferLock;

	u32 startupInfo[32];
	u32 allowDirectScreenAccess;
	u32 exitFlag;

	u32 sharedFunc[100];

	/* Plugin's NS_CONFIG ends here */
	NTR_CONFIG ntrConfig;
	RP_CONFIG rpConfig;
} NS_CONFIG;

extern NS_CONFIG* nsConfig;

int nsStartup(void);
u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg);
void nsHandlePacket(void);
void nsHandleDbgPrintPacket(void);
void nsHandleMenuPacket(void);

#endif
