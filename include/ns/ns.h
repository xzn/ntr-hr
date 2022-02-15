

extern u32 nsDefaultMemRegion;

#define NS_DEFAULT_MEM_REGION (nsDefaultMemRegion)

#define DEBUG_BUFFER_SIZE 0x2000
#define GLOBAL_BUFFER_SIZE 0x4000
#define MAX_BREAKPOINT 64

typedef struct _RT_LOCK {
	vu32 value;
} RT_LOCK;

#define NS_TYPE_NORMAL	0
#define NS_TYPE_BIGDATA	1

#define NS_CMD_HEARTBEAT	0
#define NS_CMD_SAVEFILE		1
#define NS_CMD_RECVFILE		2
#define NS_CMD_SAYHELLO		3
#define NS_CMD_RELOAD		4
#define NS_CMD_LSPROCESS		5
#define NS_CMD_ATTACHPROCESS		6
#define NS_CMD_LSTHREAD		7
#define NS_CMD_MEMLAYOUT	8
#define NS_CMD_READMEM		9
#define NS_CMD_WRITEMEM		10
#define NS_CMD_BREAKPOINT	11
#define NS_CMD_QUERYHANDLE	12
#define NS_CMD_REMOTEPLAY   901

#define NS_CONFIGURE_ADDR	0x06000000

typedef struct _RT_HOOK {
	u32 model;
	u32 isEnabled;
	u32 funcAddr;
	u32 bakCode[16];
	u32 jmpCode[16];
	u32 callCode[16];
} RT_HOOK;

typedef struct _NS_BREAKPOINT {
	u32 type;
	u32 flag;
	u32 addr;
	RT_HOOK hook;
	u32 stubCode[32];
	u32 isEnabled;
} NS_BREAKPOINT;

#define NS_BPTYPE_UNUSED	0
#define NS_BPTYPE_CODE		1
#define NS_BPTYPE_CODEONESHOT	2

#if 0
#define RP_SELECT_PREDICTION (1 << 0)
#define RP_USE_FRAME_DELTA (1 << 1)
#define RP_PREDICT_FRAME_DELTA (1 << 2)
#define RP_DYNAMIC_ENCODE (1 << 3)
#define RP_FLAG_ALL (1 << 31)
#else
#define RP_SELECT_PREDICTION 0
#define RP_USE_FRAME_DELTA 1
#define RP_PREDICT_FRAME_DELTA 0
#define RP_DYNAMIC_ENCODE 0
#define RP_FLAG_ALL 1
#endif

#define RP_EXTENDED_MODE (1 << 31)
#define RP_MAGIC 0xfff54321

typedef struct _RP_CONFIG {
	u32 flags; // flags
	u32 mode;
	u32 quality;
	u32 qos;
	volatile u32 control; // control
} RP_CONFIG;

typedef struct _NS_CONFIG {
	u32 initMode;
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
	NTR_CONFIG ntrConfig;
	RP_CONFIG rpConfig;
} NS_CONFIG;

#define NS_INITMODE_FROMBOOT	0
#define NS_INITMODE_FROMRELOAD	1
#define NS_INITMODE_FROMHOOK	2

#define NS_STARTCMD_INJECTPM	1000
#define NS_STARTCMD_INJECTGAME	1001
#define NS_STARTCMD_DEBUG		1002



typedef struct _NS_PACKET {
	u32 magic;
	u32 seq;
	u32 type;
	u32 cmd;
	u32 args[16];
	
	u32 dataLen;
} NS_PACKET;

typedef struct _NS_DEBUG_CONTEXT {
	vu32 state;
	u32 reg[64];
} NS_DEBUG_CONTEXT;

typedef struct _NS_BREAKPOINT_STATUS {
	vu32 bpid;
	vu32* regs;
	vu32 resumeFlag;
} NS_BREAKPOINT_STATUS;

typedef struct _NS_CONTEXT {
	u32 listenPort;
	u32 hListenSocket;
	u32 hSocket;
	u32 hCurrentProcess;
	

	u8 debugBuf[DEBUG_BUFFER_SIZE + 20];
		
	NS_PACKET packetBuf;
	s32 remainDataLen;
	u8 gBuff[GLOBAL_BUFFER_SIZE + 20];
	
	RT_LOCK	breakPointTriggerLock;
	NS_BREAKPOINT breakPoint[MAX_BREAKPOINT];

	RT_LOCK breakPointStatusLock;
	NS_BREAKPOINT_STATUS breakPointStatus;
	u32 isBreakPointHandled;

} NS_CONTEXT;

extern NS_CONTEXT *g_nsCtx;
extern NS_CONFIG *g_nsConfig;



void rtInitLock(RT_LOCK *lock) ;
void rtAcquireLock(RT_LOCK *lock) ;
void rtReleaseLock(RT_LOCK *lock) ;
u32 rtAlignToPageSize(u32 size);
u32 rtGetPageOfAddress(u32 addr) ;
u32 rtCheckRemoteMemoryRegionSafeForWrite(Handle hProcess, u32 addr, u32 size) ;
u32 rtSafeCopyMemory(u32 dst, u32 src, u32 size) ;
int rtRecvSocket(u32 sockfd, u8 *buf, int size);
int rtSendSocket(u32 sockfd, u8 *buf, int size);
u16 rtIntToPortNumber(u16 x) ;
u32 rtGetFileSize(u8* fileName);
u32 rtLoadFileToBuffer(u8* fileName, u32* pBuf, u32 bufSize) ;
u32 rtGetThreadReg(Handle hProcess, u32 tid, u32* ctx);
u32 rtFlushInstructionCache(void* ptr, u32 size);
void rtInitHook(RT_HOOK* hook, u32 funcAddr, u32 callbackAddr);
void rtEnableHook(RT_HOOK* hook);
void rtDisableHook(RT_HOOK* hook);
void rtInitHookThumb(RT_HOOK* hook, u32 funcAddr, u32 callbackAddr);

void rpSendBuffer(u8* buf, u32 size, u32 flag);


u32 nsAttachProcess(Handle hProcess, u32 remotePC, NS_CONFIG *cfg, int sysRegion);
u32 rtGenerateJumpCode(u32 dst, u32* buf);



