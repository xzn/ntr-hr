#include "global.h"

#define SCREEN_COUNT (2)
#define RP_WORK_COUNT (2)

extern struct rp_handles_t {
	struct rp_work_syn_t {
		Handle sem_end, sem_nwm, sem_send;
		u32 sem_count;
		u8 sem_set;
	} work[RP_WORK_COUNT];

	struct rp_thread_syn_t {
		Handle sem_start, sem_work;
	} thread[RP_CORE_COUNT_MAX];

	Handle nwmEvent;
	Handle portEvent[SCREEN_COUNT];
	Handle screenCapSem;
} *rp_syn;

extern Handle hThreadMain;
extern u32 rpPortGamePid;
extern int rpResetThreads;

#define RP_NWM_HDR_SIZE (0x2a + 8)
#define RP_DATA_HDR_SIZE (4)

extern u8 rpNwmHdr[RP_NWM_HDR_SIZE];

void rpThreadMain(void *);

extern int rpInited;
extern u32 rpSrcAddr;
