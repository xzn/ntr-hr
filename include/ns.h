#ifndef NS_H
#define NS_H

#include "3ds/types.h"
#include "ntr_config.h"
#include "rp.h"
#include "rt.h"
#include "constants.h"

#include <stdarg.h>

void nsDbgPrint2(const char *title, const char *msg);
void nsDbgPrintVerboseVABuf(const char *file_name, int line_number, const char *func_name, const char* fmt, va_list arp);
void nsDbgPrintVerboseVA(const char *file_name, int line_number, const char *func_name, const char* fmt, va_list arp);
void nsDbgPrintRaw(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define nsDbgPrint(fmt, ...) nsDbgPrintVerbose(__FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
void nsDbgPrintVerbose(const char *file_name, int line_number, const char *func_name, const char* fmt, ...) __attribute__((format(printf, 4, 5)));

typedef enum {
	NS_INITMODE_FROMBOOT,
	NS_INITMODE_FROMHOOK = 2,
} NS_INITMODE;

typedef struct {
	NS_INITMODE initMode;
	u32 startupCommand_unused;
	u32 hSOCU_unused;

	u8 *debugBuf_unused;
	u8 *debugBufEnd_unused;
	u8 *debugPtr;
	u32 debugReady;

	RT_LOCK debugBufferLock;

	u32 startupInfo[32];
	u32 hasDirectScreenAccess_unused;
	u32 exitFlag_unused;

	u32 sharedFunc[100];

	/* Plugin's NS_CONFIG ends here */
	NTR_CONFIG ntrConfig;
	RP_CONFIG rpConfig;
} NS_CONFIG;

static NS_CONFIG *const nsConfig = (NS_CONFIG *)NS_CONFIG_ADDR;
static RP_CONFIG *const rpConfig = &nsConfig->rpConfig;

void nsThreadInit();
int nsStartup(void);
int nsCheckPCSafeToWrite(u32 hProcess, u32 remotePC);
u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg, int thumbR3);
void nsHandlePacket(void);
void nsHandleDbgPrintPacket(void);
void nsHandleMenuPacket(void);
int nsControlRecv(int);

#endif
