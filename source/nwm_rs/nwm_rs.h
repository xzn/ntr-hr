#include <errno.h>
#include "global.h"

void __system_initSyscalls(void);
#include "3ds/services/gspgpu.h"
#include "3ds/result.h"
#include "../nwm_misc/ikcp.h"

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ROUND_UP(n, d) (DIV_ROUND_UP(n, d) * (d))
const unsigned NWM_PACKET_SIZE = ROUND_UP(PACKET_SIZE + NWM_HDR_SIZE, sizeof(void *));
const unsigned RP_RECV_CMD_SIZE_MAX = 0;
const unsigned RP_RECV_PACKET_SIZE = IKCP_OVERHEAD_CONST + RP_RECV_CMD_SIZE_MAX;

const u32 WORK_COUNT = 2;
#define COMPRESSED_SIZE_MAX (0x30000)
const unsigned SEND_BUFS_COUNT = DIV_ROUND_UP(COMPRESSED_SIZE_MAX, PACKET_SIZE - IKCP_OVERHEAD_CONST - DATA_HDR_SIZE) * WORK_COUNT;
const unsigned SEND_BUFS_SIZE = SEND_BUFS_COUNT * NWM_PACKET_SIZE;

struct rp_cb {
	struct IKCPCB ikcp;
	char send_bufs[SEND_BUFS_COUNT][NWM_PACKET_SIZE];
	char recv_bufs[IKCP_WND_RCV_CONST][RP_RECV_PACKET_SIZE];
	mp_pool_t send_pool;
	mp_pool_t recv_pool;
};

#define RP_HDR_RELIABLE_STREAM_FLAG (1 << 14)
#define RP_HDR_KCP_CONV_MASK (3)
#define RP_HDR_KCP_CONV_SHIFT (10)
