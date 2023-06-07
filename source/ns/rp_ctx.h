#ifndef RP_CTX_H
#define RP_CTX_H

#include "rp_net.h"
#include "rp_image.h"
#include "rp_jls.h"
#include "rp_dma.h"
#include "rp_conf.h"
#include "rp_syn_chan.h"
#include "rp_dyn_prio.h"
#include "rp_screen.h"

struct rp_ctx_t {
	ikcpcb kcp;

	u8 exit_thread;
	Handle second_thread;
	Handle screen_thread;
	Handle network_thread;

#if RP_SYN_NET == 1
	rp_lock_t network_mutex;
#elif RP_SYN_NET == 2
	rp_sem_t network_sem[RP_ENCODE_THREAD_COUNT];
#endif

	struct rp_dma_ctx_t dma_ctx;
	u8 dma_config[24];

	u8 nwm_send_buffer[NWM_PACKET_SIZE] ALIGN_4;
	u8 thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 second_thread_stack[RP_STACK_SIZE] ALIGN_4;
	u8 network_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 screen_transfer_thread_stack[RP_MISC_STACK_SIZE] ALIGN_4;
	u8 control_recv_buffer[RP_CONTROL_RECV_BUFFER_SIZE] ALIGN_4;
	u8 umm_heap[RP_UMM_HEAP_SIZE] ALIGN_4;

	struct rp_screen_encode_t screen_encode[RP_ENCODE_BUFFER_COUNT];
	struct rp_network_encode_t network_encode[RP_ENCODE_BUFFER_COUNT];

	struct rp_jls_params_t jls_param;
	struct rp_jls_ctx_t jls_ctx[RP_ENCODE_THREAD_COUNT];

	struct rp_image_ctx_t image_ctx;

	struct {
		struct rp_syn_comp_t screen, network;
	} syn;

	struct rp_conf_t conf;

	struct rp_dyn_prio_t dyn_prio;

	struct rp_net_ctx_t net_ctx;

	struct rp_screen_encode_ctx_t screen_ctx;
};

#endif
