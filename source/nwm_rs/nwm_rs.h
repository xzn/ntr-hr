#include <errno.h>
#include "global.h"

void __system_initSyscalls(void);
#include "3ds/services/gspgpu.h"
#include "3ds/result.h"
#include "../nwm_misc/ikcp.h"

struct rp_cb {
	struct IKCPCB ikcp;
	char send_bufs[SEND_BUFS_COUNT + WORK_COUNT][NWM_PACKET_SIZE];
	char recv_bufs[IKCP_WND_RCV_CONST][RP_RECV_PACKET_SIZE];
	mp_pool_t send_pool;
	mp_pool_t recv_pool;
};

#define RP_HDR_RELIABLE_STREAM_FLAG (1 << 14)
#define RP_HDR_KCP_CONV_MASK (3)
#define RP_HDR_KCP_CONV_SHIFT (10)
