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

struct rp_syn_comp_t;
struct rp_dyn_prio_t;
struct rp_conf_kcp_t;
void rpNetworkInit(struct rp_net_ctx_t *ctx, u8 *nwm_send_buf, u8 *ctrl_recv_buf);
int rpKCPClear(struct rp_net_ctx_t *ctx);
int rpNetworkTransfer(
	struct rp_net_ctx_t *ctx, int thread_n, ikcpcb *kcp, struct rp_conf_kcp_t *kcp_conf, volatile u8 *exit_thread,
	struct rp_syn_comp_t *network_queue, u64 min_send_interval_ticks);

#endif
