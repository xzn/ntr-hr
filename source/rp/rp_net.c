#include "rp_net.h"
#include "rp_syn_chan.h"
#include "rp_dyn_prio.h"
#include "rp_syn.h"
#include "rp_conf.h"
#include "rp_jls.h"

static uint16_t ip_checksum(void* vdata, size_t length) {
	// Cast the data pointer to one that can be indexed.
	char* data = (char*)vdata;
	size_t i;
	// Initialise the accumulator.
	uint32_t acc = 0xffff;

	// Handle complete 16-bit blocks.
	for (i = 0; i + 1 < length; i += 2) {
		uint16_t word;
		memcpy(&word, data + i, 2);
		acc += ntohs(word);
		if (acc > 0xffff) {
			acc -= 0xffff;
		}
	}

	// Handle any partial block at the end of the data.
	if (length & 1) {
		uint16_t word = 0;
		memcpy(&word, data + length - 1, 1);
		acc += ntohs(word);
		if (acc > 0xffff) {
			acc -= 0xffff;
		}
	}

	// Return the checksum in network byte order.
	return htons(~acc);
}

static int rpInitUDPPacket(int dataLen, u8 *sendBuf) {
	dataLen += 8;

	*(u16*)(sendBuf + 0x22 + 8) = htons(RP_PORT); // src port
	*(u16*)(sendBuf + 0x24 + 8) = htons(RP_DEST_PORT); // dest port
	*(u16*)(sendBuf + 0x26 + 8) = htons(dataLen);
	*(u16*)(sendBuf + 0x28 + 8) = 0; // no checksum
	dataLen += 20;

	*(u16*)(sendBuf + 0x10 + 8) = htons(dataLen);
	*(u16*)(sendBuf + 0x12 + 8) = 0xaf01; // packet id is a random value since we won't use the fragment
	*(u16*)(sendBuf + 0x14 + 8) = 0x0040; // no fragment
	*(u16*)(sendBuf + 0x16 + 8) = 0x1140; // ttl 64, udp
	*(u16*)(sendBuf + 0x18 + 8) = 0;
	*(u16*)(sendBuf + 0x18 + 8) = ip_checksum(sendBuf + 0xE + 8, 0x14);

	dataLen += 22;
	*(u16*)(sendBuf + 12) = htons(dataLen);

	return dataLen;
}

static int rp_udp_output(const char *buf, int len, ikcpcb *kcp UNUSED, void *user) {
	struct rp_net_ctx_t *ctx = user;

	u8 *sendBuf = ctx->nwm_send_buf;
	u8 *dataBuf = sendBuf + NWM_HEADER_SIZE;

	if (len > KCP_PACKET_SIZE) {
		nsDbgPrint("rp_udp_output len exceeded KCP_PACKET_SIZE: %d\n", len);
		return 0;
	}

	memcpy(dataBuf, buf, len);
	int packetLen = rpInitUDPPacket(len, sendBuf);

	nwmSendPacket(sendBuf, packetLen);

	return len;
}

int rpNetworkInit(struct rp_net_ctx_t *ctx, u8 *nwm_send_buf, u8 *ctrl_recv_buf, struct rp_kcp_ctx_t *kcp_ctx) {
	int ret;
	ctx->nwm_send_buf = nwm_send_buf;
	ctx->ctrl_recv_buf = ctrl_recv_buf;
	ctx->kcp_ctx = kcp_ctx;
	if ((ret = svc_createMutex(&ctx->kcp_mutex, 0))) {
		nsDbgPrint("kcp_mutex create failed: %d\n", ret);
		return ret;
	}
	__atomic_store_n(&ctx->kcp_inited, 1, __ATOMIC_RELEASE);
	return 0;
}

static int rpKCPLock(struct rp_net_ctx_t *ctx) {
	return svc_waitSynchronization1(ctx->kcp_mutex, RP_SYN_WAIT_MAX);
}

static void rpKCPUnlock(struct rp_net_ctx_t *ctx) {
	svc_releaseMutex(ctx->kcp_mutex);
}

int rpKCPReady(struct rp_net_ctx_t *ctx, struct rp_conf_kcp_t *kcp_conf) {
	int res;
	if ((res = rpKCPLock(ctx)))
		return res;

	ikcpcb *kcp = ikcp_create(&ctx->kcp_ctx->kcp, kcp_conf->conv, ctx,
		KCP_PACKET_SIZE, (char *)ctx->kcp_ctx->buffer, sizeof(ctx->kcp_ctx->buffer),
		ctx->kcp_ctx->acklist, sizeof(ctx->kcp_ctx->acklist) / sizeof(IUINT32) / 2);
	if (!kcp) {
		nsDbgPrint("ikcp_create failed\n");
		rpKCPUnlock(ctx);
		return -1;
	} else {
		kcp->output = rp_udp_output;
		ikcp_nodelay(kcp, kcp_conf->nodelay, 10, kcp_conf->fastresend, kcp_conf->nocwnd);
		kcp->rx_minrto = kcp_conf->minrto;
		ikcp_wndsize(kcp, kcp_conf->snd_wnd_size, 0);
		// kcp->stream = 1;
	}

	ctx->kcp_ready = 1;
	rpKCPUnlock(ctx);
	return 0;
}

int rpKCPClear(struct rp_net_ctx_t *ctx) {
	int res;
	if ((res = rpKCPLock(ctx)))
		return res;
	if (ctx->kcp_ready) {
		ikcp_release(&ctx->kcp_ctx->kcp);
		ctx->kcp_ready = 0;
	}
	rpKCPUnlock(ctx);
	return 0;
}

static IINT64 iclock64(void)
{
	u64 value = svc_getSystemTick();
	return value / SYSTICK_PER_MS;
}

static IUINT32 iclock()
{
	return (IUINT32)(iclock64() & 0xfffffffful);
}

static void rpControlRecvHandle(u8* buf UNUSED, int buf_size UNUSED) {
}

void rpControlRecv(struct rp_net_ctx_t *ctx) {
	if (!ctx)
		return;

	int ret = recv(rp_recv_sock, ctx->ctrl_recv_buf, RP_CONTROL_RECV_BUFFER_SIZE, 0);
	if (ret == 0) {
		nsDbgPrint("rpControlRecv nothing\n");
		return;
	} else if (ret < 0) {
		int err = SOC_GetErrno();
		nsDbgPrint("rpControlRecv failed: %d, errno = %d\n", ret, err);
		return;
	}

	int bufSize = ret;
	if (!__atomic_load_n(&ctx->kcp_inited, __ATOMIC_ACQUIRE)) {
		svc_sleepThread(RP_THREAD_LOOP_FAST_WAIT);
		return;
	}

	if ((ret = rpKCPLock(ctx))) {
		nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
		return;
	}
	if (!ctx->kcp_ready) {
		rpKCPUnlock(ctx);
		svc_sleepThread(RP_THREAD_LOOP_FAST_WAIT);
		return;
	}

	if ((ret = ikcp_input(&ctx->kcp_ctx->kcp, (const char *)ctx->ctrl_recv_buf, bufSize)) < 0) {
		nsDbgPrint("ikcp_input failed: %d\n", ret);
	}

	ikcp_update(&ctx->kcp_ctx->kcp, iclock());
	ret = ikcp_recv(&ctx->kcp_ctx->kcp, (char *)ctx->ctrl_recv_buf, RP_CONTROL_RECV_BUFFER_SIZE);
	rpKCPUnlock(ctx);
	if (ret >= 0) {
		rpControlRecvHandle(ctx->ctrl_recv_buf, ret);
	}
}

int rpKCPSend(struct rp_net_state_t *state, const u8 *buf, int size) {
	int ret;

	if (state->sync && (ret = rp_lock_wait(state->mutex, RP_SYN_WAIT_MAX))) {
		nsDbgPrint("rpKCPSend mutex wait failed: %d", ret);
		return -1;
	}

	u64 duration = 0;
	u64 curr_tick = svc_getSystemTick(), tick_diff = curr_tick - state->last_tick;
	s64 desired_tick_diff = (s64)curr_tick - (s64)state->desired_last_tick;
	if (desired_tick_diff < (s64)state->min_send_interval_ticks) {
		duration = ((s64)state->min_send_interval_ticks - desired_tick_diff) * 1000 / SYSTICK_PER_US;
	} else {
		u64 min_tick = state->min_send_interval_ticks * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM;
		if (tick_diff < min_tick)
			duration = (min_tick - tick_diff) * 1000 / SYSTICK_PER_US;
	}

	state->desired_last_tick += state->min_send_interval_ticks;
	u64 last_tick = state->last_tick = curr_tick;

	struct rp_net_ctx_t *ctx = state->net_ctx;

	u64 desired_last_tick_step = SYSTICK_PER_SEC * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM;
	if ((s64)state->last_tick - (s64)state->desired_last_tick > (s64)desired_last_tick_step)
		state->desired_last_tick = state->last_tick - desired_last_tick_step;

	volatile u8 *exit_thread = state->exit_thread;

	if (state->sync)
		rp_lock_rel(state->mutex);

	if (duration)
		svc_sleepThread(duration);

	while (!*exit_thread) {
		if ((tick_diff = (curr_tick = svc_getSystemTick()) - last_tick) > KCP_TIMEOUT_TICKS) {
			nsDbgPrint("kcp timeout send data\n");
			return -1;
		}

		if ((ret = rpKCPLock(ctx))) {
			nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
			return -1;
		}
		int waitsnd = ikcp_waitsnd(&ctx->kcp_ctx->kcp);
		if (waitsnd < (int)ctx->kcp_ctx->kcp.snd_wnd) {
			ret = ikcp_send(&ctx->kcp_ctx->kcp, (const char *)buf, size);

			if (ret < 0) {
				nsDbgPrint("ikcp_send failed: %d\n", ret);
				rpKCPUnlock(ctx);
				return -1;
			}

			ikcp_update(&ctx->kcp_ctx->kcp, iclock());
			rpKCPUnlock(ctx);

			return 0;
		}
		ikcp_update(&ctx->kcp_ctx->kcp, iclock());
		rpKCPUnlock(ctx);

		svc_sleepThread(RP_THREAD_KCP_LOOP_WAIT);
	}
	return 0;
}

void rpNetworkStateInit(struct rp_net_state_t *state, struct rp_net_ctx_t *ctx, volatile u8 *exit_thread, u64 min_send_interval_ticks, u8 sync) {
	rp_lock_close(state->mutex);
	if (sync)
		(void)rp_lock_init(state->mutex);
	else
		state->mutex = 0;
	state->sync = sync;
	state->exit_thread = exit_thread;
	state->min_send_interval_ticks = min_send_interval_ticks;
	u64 curr_tick = svc_getSystemTick();
	state->last_tick = curr_tick;
	state->desired_last_tick = curr_tick + min_send_interval_ticks;
	state->net_ctx = ctx;
}

int rpNetworkTransfer(struct rp_net_state_t *state, int thread_n UNUSED, struct rp_conf_kcp_t *kcp_conf, struct rp_syn_comp_t *network_queue) {
	int ret;

	struct rp_net_ctx_t *ctx = state->net_ctx;

	// kcp init
	if ((ret = rpKCPReady(ctx, kcp_conf)))
		return ret;

	u64 curr_tick = svc_getSystemTick();

	volatile u8 *exit_thread = state->exit_thread;

	while (!*exit_thread) {
		if ((curr_tick = svc_getSystemTick()) - state->last_tick > KCP_TIMEOUT_TICKS) {
			nsDbgPrint("kcp timeout transfer acquire\n");
			break;
		}

		if ((ret = rpKCPLock(ctx))) {
			nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
			break;
		}
		ikcp_update(&ctx->kcp_ctx->kcp, iclock());
		rpKCPUnlock(ctx);

		struct rp_network_encode_t *network = rp_network_transfer_acquire(&network_queue->transfer, RP_THREAD_LOOP_FAST_WAIT);
		if (!network) {
			continue;
		}

		struct rp_send_data_header *header = (struct rp_send_data_header *)network->buffer;
		if (0 && header->type_data == RP_SEND_HEADER_TYPE_DATA) {
			nsDbgPrint(
				"s %d "
				"n %d "
				"p %d "
				"type %d "
				"comp %d "
				"size %d "
				"end %d\n",
				(s32)header->top_bot, (s32)header->frame_n, (s32)header->p_frame, (s32)header->plane_type, (s32)header->plane_comp, (s32)header->data_size, (s32)header->data_end);
		}
		// kcp send data
		if ((ret = rpKCPSend(state, network->buffer, network->size)))
			return ret;

		rp_network_encode_release(&network_queue->encode, network);
	}
	*exit_thread = 1;

	// kcp deinit
	if ((ret = rpKCPClear(ctx)))
		return ret;
	return 0;
}
