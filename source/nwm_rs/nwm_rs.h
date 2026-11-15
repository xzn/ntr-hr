#include <errno.h>
#include "global.h"

void __system_initSyscalls(void);
#include "3ds/services/gspgpu.h"
#include "3ds/result.h"
#ifdef NEW_3DS
#include "3ds/services/csnd.h"
#include "../nwm_misc/ikcp.h"
#include "../nwm_misc/fecal.h"
#include "../nwm_misc/gf256.h"
#include "../nwm_misc/rp_syn.h"
#include "../nwm_misc/rp_res.h"
#endif

#ifdef NEW_3DS
#define RP_RECV_BUF_N (2)
const static u32 csnd_cap_timer = CSND_CAP_TIMER;

struct rp_cb_locked {
	struct IKCPCB ikcp;
	char send_bufs[SEND_BUFS_COUNT][NWM_PACKET_SIZE] ALIGNED(sizeof(void *));
	char recv_buf[RP_RECV_BUF_N][RP_RECV_PACKET_SIZE] ALIGNED(sizeof(void *));
	mp_pool_t send_pool;
	char cur_send_bufs[SEND_CUR_BUFS_COUNT][NWM_PACKET_SIZE] ALIGNED(sizeof(void *));
	mp_pool_t cur_send_pool;
};

struct rp_cb {
	struct rp_cb_locked locked;
	struct rp_syn_comp_func_t nwm_syn;
	void *nwm_syn_data[SEND_BUFS_COUNT];
};
#endif
#ifdef OLD_3DS
#define RP_CB_HDR_SIZE ROUND_UP(NWM_HDR_SIZE + DATA_HDR_SIZE, sizeof(void *))
#define RP_CB_SIZE (RP_DATA_SIZE + RP_CB_HDR_SIZE)
struct rp_cb {
	char buf[RP_CB_SIZE];
};
#endif

#define NWM_PROPORTIONAL_MIN_INTERVAL 1
#define NWM_AGGRESSIVE_NEXT_TICK 1

#include <math.h>
