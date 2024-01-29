#ifndef NS_H
#define NS_H

#include "3ds/types.h"
#include "ntr_config.h"
#include "rp.h"
#include "rt.h"
#include "constants.h"

#include <stdarg.h>

void nsDbgPrintRaw(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define nsDbgPrint(fmt, ...) nsDbgPrintVerbose(__FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
void nsDbgPrintVerbose(const char *file_name, int line_number, const char *func_name, const char* fmt, ...) __attribute__((format(printf, 4, 5)));

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

static NS_CONFIG *const nsConfig = (NS_CONFIG *)NS_CONFIG_ADDR;

int nsStartup(void);
u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg);
void nsHandlePacket(void);
void nsHandleDbgPrintPacket(void);
void nsHandleMenuPacket(void);

#endif
