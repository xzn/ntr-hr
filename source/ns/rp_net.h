#ifndef RP_NET_H
#define RP_NET_H

#include "rp_common.h"

struct rp_net_ctx_t {
    u8 *nwm_send_buf;
    u8 *ctrl_recv_buf;
    u8 kcp_inited;
    u8 kcp_ready;
    Handle kcp_mutex;
    ikcpcb *kcp;
};

enum {
	RP_SEND_HEADER_TOP_BOT = BIT(0),
	RP_SEND_HEADER_P_FRAME = BIT(1),
};

struct rp_send_header {
	u32 size;
	u32 size_1;
	u8 frame_n;
	u8 bpp;
	u8 format;
	u8 flags;
};

struct rp_syn_comp_t;
struct rp_dyn_prio_t;
void rpNetworkInit(struct rp_net_ctx_t *ctx, u8 *nwm_send_buf, u8 *ctrl_recv_buf);
int rpKCPClear(struct rp_net_ctx_t *ctx);
int rpNetworkTransfer(
	struct rp_net_ctx_t *ctx, int thread_n, ikcpcb *kcp, int conv, volatile u8 *exit_thread,
	struct rp_syn_comp_t *network_queue, u8 *kcp_send_buf, u64 min_send_interval_ticks, struct rp_dyn_prio_t* dyn_prio);

#endif
