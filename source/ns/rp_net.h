#ifndef RP_NET_H
#define RP_NET_H

#include "rp_syn.h"

struct rp_net_ctx_t {
    u8 *nwm_send_buf;
    u8 *ctrl_recv_buf;
    u8 kcp_inited;
    u8 kcp_ready;
    Handle kcp_mutex;
    ikcpcb *kcp;
};

struct rp_net_state_t {
    struct rp_net_ctx_t *net_ctx;
	u8 sync;
	rp_lock_t mutex;
    u64 last_tick, desired_last_tick, min_send_interval_ticks;
    volatile u8 *exit_thread;
};

struct rp_syn_comp_t;
struct rp_dyn_prio_t;
struct rp_conf_kcp_t;
void rpNetworkInit(struct rp_net_ctx_t *ctx, u8 *nwm_send_buf, u8 *ctrl_recv_buf);
int rpKCPClear(struct rp_net_ctx_t *ctx);
int rpKCPSend(struct rp_net_state_t *state, const u8 *buf, int size);
void rpNetworkStateInit(struct rp_net_state_t *state, struct rp_net_ctx_t *ctx, volatile u8 *exit_thread, u64 min_send_interval_ticks, u8 sync);
int rpNetworkTransfer(
	struct rp_net_state_t *state, int thread_n,
    ikcpcb *kcp, struct rp_conf_kcp_t *kcp_conf, struct rp_syn_comp_t *network_queue);

#endif
