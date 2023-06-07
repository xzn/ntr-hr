#include "rp_net.h"
#include "rp_syn_chan.h"
#include "rp_dyn_prio.h"
#include "rp_syn.h"
#include "rp_conf.h"

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
		nsDbgPrint("rp_udp_output len exceeded PACKET_SIZE: %d\n", len);
		return 0;
	}

	memcpy(dataBuf, buf, len);
	int packetLen = rpInitUDPPacket(len, sendBuf);

	nwmSendPacket(sendBuf, packetLen);

	return len;
}

void rpNetworkInit(struct rp_net_ctx_t *ctx, u8 *nwm_send_buf, u8 *ctrl_recv_buf) {
	ctx->nwm_send_buf = nwm_send_buf;
	ctx->ctrl_recv_buf = ctrl_recv_buf;
	svc_createMutex(&ctx->kcp_mutex, 0);
	__atomic_store_n(&ctx->kcp_inited, 1, __ATOMIC_RELEASE);
}

static int rpKCPLock(struct rp_net_ctx_t *ctx) {
	return svc_waitSynchronization1(ctx->kcp_mutex, RP_SYN_WAIT_MAX);
}

static void rpKCPUnlock(struct rp_net_ctx_t *ctx) {
	svc_releaseMutex(ctx->kcp_mutex);
}

static int rpKCPReady(struct rp_net_ctx_t *ctx, ikcpcb *kcp, struct rp_conf_kcp_t *kcp_conf, void *user) {
	int res;
	if ((res = rpKCPLock(ctx)))
		return res;

	kcp = ikcp_create(kcp, kcp_conf->conv, user);
	if (!kcp) {
		nsDbgPrint("ikcp_create failed\n");
		rpKCPUnlock(ctx);
		return -1;
	} else {
		kcp->output = rp_udp_output;
		if ((res = ikcp_setmtu(kcp, KCP_PACKET_SIZE)) < 0) {
			nsDbgPrint("ikcp_setmtu failed: %d\n", res);
			rpKCPUnlock(ctx);
			return -1;
		}
		ikcp_nodelay(kcp, kcp_conf->nodelay, 10, kcp_conf->fastresend, kcp_conf->nocwnd);
		kcp->rx_minrto = kcp_conf->minrto;
		ikcp_wndsize(kcp, kcp_conf->snd_wnd_size, 0);
		kcp->stream = 1;
	}

	ctx->kcp = kcp;
	ctx->kcp_ready = 1;
	rpKCPUnlock(ctx);
	return 0;
}

int rpKCPClear(struct rp_net_ctx_t *ctx) {
	int res;
	if ((res = rpKCPLock(ctx)))
		return res;
	if (ctx->kcp_ready) {
		ikcp_release(ctx->kcp);
		ctx->kcp = 0;
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

	if ((ret = ikcp_input(ctx->kcp, (const char *)ctx->ctrl_recv_buf, bufSize)) < 0) {
		nsDbgPrint("ikcp_input failed: %d\n", ret);
	}

	ikcp_update(ctx->kcp, iclock());
	ret = ikcp_recv(ctx->kcp, (char *)ctx->ctrl_recv_buf, RP_CONTROL_RECV_BUFFER_SIZE);
	rpKCPUnlock(ctx);
	if (ret >= 0) {
		rpControlRecvHandle(ctx->ctrl_recv_buf, ret);
	}
}

struct rp_net_state_t {
	u64 last_tick, desired_last_tick, min_send_interval_ticks;
	volatile u8 *exit_thread;
};

static int rpKCPSend(struct rp_net_ctx_t *ctx, struct rp_net_state_t *state, const u8 *buf, int size) {
	u64 curr_tick, tick_diff;
	s64 desired_tick_diff;

	int ret;
	while (!*state->exit_thread) {
		if ((tick_diff = (curr_tick = svc_getSystemTick()) - state->last_tick) > KCP_TIMEOUT_TICKS) {
			nsDbgPrint("kcp timeout send data\n");
			return -1;
		}

		desired_tick_diff = (s64)curr_tick - (s64)state->desired_last_tick;
		if (desired_tick_diff < (s64)state->min_send_interval_ticks) {
			u64 duration = ((s64)state->min_send_interval_ticks - desired_tick_diff) * 1000 / SYSTICK_PER_US;
			svc_sleepThread(duration);
		} else {
			u64 min_tick = state->min_send_interval_ticks * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM;
			if (tick_diff < min_tick) {
				u64 duration = (min_tick - tick_diff) * 1000 / SYSTICK_PER_US;
				svc_sleepThread(duration);
			}
		}

		if ((ret = rpKCPLock(ctx))) {
			nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
			return -1;
		}
		int waitsnd = ikcp_waitsnd(ctx->kcp);
		if (waitsnd < (int)ctx->kcp->snd_wnd) {
			ret = ikcp_send(ctx->kcp, (const char *)buf, size);

			if (ret < 0) {
				nsDbgPrint("ikcp_send failed: %d\n", ret);
				rpKCPUnlock(ctx);
				return -1;
			}

			ikcp_update(ctx->kcp, iclock());
			rpKCPUnlock(ctx);

			state->desired_last_tick += state->min_send_interval_ticks;
			state->last_tick = curr_tick;

			u64 desired_last_tick_step = state->min_send_interval_ticks * ctx->kcp->snd_wnd *
				RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM;
			if ((s64)state->last_tick - (s64)state->desired_last_tick > (s64)desired_last_tick_step)
				state->desired_last_tick = state->last_tick - desired_last_tick_step;

			return 0;
		}
		ikcp_update(ctx->kcp, iclock());
		rpKCPUnlock(ctx);

		svc_sleepThread(RP_THREAD_KCP_LOOP_WAIT);
	}
	return 0;
}

int rpNetworkTransfer(
	struct rp_net_ctx_t *ctx, int thread_n UNUSED, ikcpcb *kcp, struct rp_conf_kcp_t *kcp_conf, volatile u8 *exit_thread,
	struct rp_syn_comp_t *network_queue, u64 min_send_interval_ticks
) {
	int ret;

	// kcp init
	if ((ret = rpKCPReady(ctx, kcp, kcp_conf, ctx)))
		return ret;

	ikcp_update(ctx->kcp, iclock());
	rpKCPUnlock(ctx);

	u64 curr_tick = svc_getSystemTick();
	struct rp_net_state_t state = {
		.last_tick = curr_tick,
		.desired_last_tick = curr_tick + min_send_interval_ticks,
		.exit_thread = exit_thread,
		.min_send_interval_ticks = min_send_interval_ticks
	};

	while (!*exit_thread) {
		if ((curr_tick = svc_getSystemTick()) - state.last_tick > KCP_TIMEOUT_TICKS) {
			nsDbgPrint("kcp timeout transfer acquire\n");
			break;
		}

		if ((ret = rpKCPLock(ctx))) {
			nsDbgPrint("kcp mutex lock timeout, %d\n", ret);
			break;
		}
		ikcp_update(ctx->kcp, iclock());
		rpKCPUnlock(ctx);

		struct rp_network_encode_t *network = rp_network_transfer_acquire(&network_queue->transfer, RP_THREAD_LOOP_FAST_WAIT);
		if (!network) {
			continue;
		}

		u32 size_remain = network->size;
		u8 *data = network->buffer;

		// kcp send data
		while (!*exit_thread && size_remain) {
			u32 data_size = RP_MIN(size_remain, RP_PACKET_SIZE);

			if ((ret = rpKCPSend(ctx, &state, data, data_size)))
				return ret;
			size_remain -= data_size;
			data += data_size;
		}

		rp_network_encode_release(&network_queue->encode, network);
	}
	*exit_thread = 1;

	// kcp deinit
	if ((ret = rpKCPClear(ctx)))
		return ret;
	return 0;
}
