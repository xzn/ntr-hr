#include <errno.h>
#include "global.h"

void __system_initSyscalls(void);
#include "3ds/services/gspgpu.h"
#include "3ds/result.h"
#include "../nwm_misc/ikcp.h"
#include "../nwm_misc/fecal.h"
#include "../nwm_misc/gf256.h"
#include "../nwm_misc/rp_syn.h"
#include "../nwm_misc/rp_res.h"

struct rp_cb {
	struct IKCPCB ikcp;
	char send_bufs[SEND_BUFS_MP_COUNT][NWM_PACKET_SIZE] ALIGNED(sizeof(void *));
	char recv_bufs[IKCP_WND_RCV_CONST][RP_RECV_PACKET_SIZE] ALIGNED(sizeof(void *));
	mp_pool_t send_pool;
	mp_pool_t recv_pool;
	struct rp_syn_comp_func_t nwm_syn;
	void *nwm_syn_data[SEND_BUFS_MP_COUNT];
};
