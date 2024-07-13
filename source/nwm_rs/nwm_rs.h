#include <errno.h>
#include "global.h"

void __system_initSyscalls(void);
#include "3ds/services/gspgpu.h"
#include "3ds/result.h"
#include "../nwm_misc/ikcp.h"

union nwm_cb {
	struct IKCPCB ikcp;
	char enet_umm_heap[200 * 1024];
};

#define NWM_RECV_SIZE (1500)
#define NWM_PACKET_SIZE (PACKET_SIZE + NWM_HDR_SIZE)
struct rp_cb {
	union nwm_cb cb;
	char nwm_buf[NWM_PACKET_SIZE];
	char recv_buf[NWM_RECV_SIZE];
};

#define RP_HDR_RELIABLE_STREAM_FLAG (1 << 14)
