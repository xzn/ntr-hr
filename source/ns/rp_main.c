#include "rp_ctx.h"
#include "rp_net.h"
#include "rp_conf.h"
#include "rp_syn_chan.h"
#include "rp_dyn_prio.h"
#include "rp_dma.h"
#include "rp_res.h"
#include "rp_main.h"
#include "rp_screen.h"

static int rpScreenEncodeSetupMain(struct rp_screen_encode_t *screen, struct rp_screen_encode_ctx_t *ctx, struct rp_ctx_t *rp_ctx) {
	return rpScreenEncodeSetup(screen, ctx, rp_ctx->image_ctx.screen_image,
		rp_ctx->image_ctx.image, &rp_ctx->dma_ctx, rp_ctx->conf.me.method == 0
	);
}

static void rpScreenTransferThread(u32 arg) {
	struct rp_ctx_t *rp_ctx = (struct rp_ctx_t *)arg;

	int UNUSED ret;
	int UNUSED thread_n = RP_SCREEN_TRANSFER_THREAD_ID;

	struct rp_screen_encode_ctx_t screen_encode_ctx;
	rpScreenEncodeInit(&screen_encode_ctx, &rp_ctx->dyn_prio, rp_ctx->conf.max_capture_interval_ticks);

	int acquire_count = 0;
	while (!rp_ctx->exit_thread) {
		rp_check_params(&rp_ctx->conf, &rp_ctx->exit_thread);

		struct rp_screen_encode_t *screen = rp_screen_transfer_acquire(&rp_ctx->syn.screen.transfer, RP_THREAD_LOOP_MED_WAIT);
		if (!screen) {
			if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) {
				nsDbgPrint("rp_screen_transfer_acquire timeout\n");
				break;
			}
			continue;
		}
		acquire_count = 0;

		if (rpScreenEncodeSetupMain(screen, &screen_encode_ctx, rp_ctx)) {
			break;
		}

		// lock write
		struct rp_image_t *image = screen->image;
		if ((ret = rpImageWriteLock(image)))
			nsDbgPrint("rpScreenTransferThread sem write wait timeout/error (%d) at (%d)\n", ret, (s32)screen);

		rp_screen_encode_release(&rp_ctx->syn.screen.encode, screen);
	}
	rp_ctx->exit_thread = 1;
	svc_exitThread();
}

static void rpNetworkTransferThread(u32 arg) {
	struct rp_ctx_t *rp_ctx = (struct rp_ctx_t *)arg;

	svc_sleepThread(RP_THREAD_NETWORK_BEGIN_WAIT);
	const int thread_n = RP_NETWORK_TRANSFER_THREAD_ID;

	while (!rp_ctx->exit_thread) {
		rp_check_params(&rp_ctx->conf, &rp_ctx->exit_thread);
		rpNetworkTransfer(
			&rp_ctx->net_ctx, thread_n, &rp_ctx->kcp, rp_ctx->conf.kcp_conv, &rp_ctx->exit_thread,
			&rp_ctx->syn.network, rp_ctx->kcp_send_buffer, rp_ctx->conf.min_send_interval_ticks, &rp_ctx->dyn_prio);
		svc_sleepThread(RP_THREAD_LOOP_SLOW_WAIT);
	}
	svc_exitThread();
}

static int rpJLSEncodeScreenAndSend(struct rp_screen_ctx_t *c, struct rp_const_image_data_t *im, struct rp_jls_params_t *jls_param, struct rp_jls_ctx_t *jls_ctx,
	struct rp_syn_comp_t *network_queue, u8 downscale_uv, u8 encoder_which, u8 encode_verify, struct rp_conf_me_t *me, volatile u8 *exit_thread, u8 multicore, u8 thread_n
) {
#define RP_PROCESS_DS_IMAGE_AND_SEND(n, ds_n, w, h, ds_w, ds_h, b, a) \
do { while (!*exit_thread) { \
	int begin_p = a & (RP_PROCESS_ALWAYS | RP_PROCESS_BEGIN_P); \
	int end_p = a & (RP_PROCESS_ALWAYS | RP_PROCESS_END_P); \
	if (begin_p) { \
		network = rp_network_encode_acquire(&network_queue->encode, RP_THREAD_LOOP_MED_WAIT, multicore); \
		if (!network) { \
			if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) { \
				nsDbgPrint("rp_network_encode_acquire timeout\n"); \
				return -1; \
			} \
			continue; \
		} \
		acquire_count = 0; \
	} \
	int bpp = im->b; \
	ret = (c->p_frame || begin_p) ? rpJLSEncodeImage(jls_param, jls_ctx, \
		network->buffer + (begin_p ? 0 : ret), \
		(begin_p ? RP_JLS_ENCODE_IMAGE_BUFFER_SIZE : RP_JLS_ENCODE_IMAGE_ME_BUFFER_SIZE), \
		(const u8 *)(downscale_uv ? im->ds_n : im->n), \
		downscale_uv ? ds_w : w, \
		downscale_uv ? ds_h : h, \
		bpp, \
		encoder_which, \
		encode_verify \
	) : 0; \
	if (ret < 0) { \
		nsDbgPrint("rpJLSEncodeImage failed\n"); \
		return -1; \
	} \
	if (begin_p) { \
		network->top_bot = c->top_bot; \
		network->frame_n = c->frame_n; \
		network->size = ret; \
		network->bpp = bpp; \
		network->format = c->format; \
		network->p_frame = c->p_frame; \
		network->size_1 = 0; \
	} else { \
		network->size_1 = ret; \
	} \
	if (c->p_frame ? end_p : begin_p) { \
		if (rp_network_transfer_release(&network_queue->transfer, network, multicore) < 0) { \
			nsDbgPrint("%d rpEncodeScreenAndSend network release syn failed\n", thread_n); \
			return -1; \
		} \
	} \
	break; \
} } while (0)
#define RP_PROCESS_IMAGE_AND_SEND(n, w, h, b, a) RP_PROCESS_DS_IMAGE_AND_SEND(n, n, w, h, w, h, b, a)

	int scale_log2_offset = me->downscale == 0 ? 0 : 1;
	int scale_log2 = 1 + scale_log2_offset;
	u8 block_size_log2 = me->block_size_log2 + scale_log2;

	int width = SCREEN_WIDTH(c->top_bot);
	int height = SCREEN_HEIGHT;

	int me_width = width >> block_size_log2;
	int me_height = height >> block_size_log2;

	struct rp_network_encode_t *network = 0;

	int ds_width = DS_DIM(width, 1);
	int ds_height = DS_DIM(height, 1);

	enum {
		RP_PROCESS_ALWAYS = BIT(0),
		RP_PROCESS_BEGIN_P = BIT(1),
		RP_PROCESS_END_P = BIT(2),
	};

	int acquire_count = 0, ret = 0;

	RP_PROCESS_IMAGE_AND_SEND(y_image, width, height, y_bpp, RP_PROCESS_ALWAYS);
	RP_PROCESS_DS_IMAGE_AND_SEND(u_image, ds_u_image, width, height, ds_width, ds_height, u_bpp, RP_PROCESS_BEGIN_P);
	RP_PROCESS_IMAGE_AND_SEND(me_x_image, me_width, me_height, me_bpp, RP_PROCESS_END_P);
	RP_PROCESS_DS_IMAGE_AND_SEND(v_image, ds_v_image, width, height, ds_width, ds_height, v_bpp, RP_PROCESS_BEGIN_P);
	RP_PROCESS_IMAGE_AND_SEND(me_y_image, me_width, me_height, me_bpp, RP_PROCESS_END_P);

#undef RP_PROCESS_DS_IMAGE_AND_SEND
#undef RP_PROCESS_IMAGE_AND_SEND

	return 0;
}

static void rpEncodeScreenAndSend(struct rp_ctx_t *rp_ctx, int thread_n) {
	svc_sleepThread(RP_THREAD_ENCODE_BEGIN_WAIT);

	int ret;
	struct rp_screen_encode_ctx_t screen_encode_ctx;
	rpScreenEncodeInit(&screen_encode_ctx, &rp_ctx->dyn_prio, rp_ctx->conf.max_capture_interval_ticks);
	struct rp_image_data_t *image_me = &rp_ctx->image_ctx.image_me[thread_n];

	int acquire_count = 0;
	while (!rp_ctx->exit_thread) {
		rp_check_params(&rp_ctx->conf, &rp_ctx->exit_thread);
		struct rp_screen_encode_t *screen;
		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			screen = rp_screen_encode_acquire(&rp_ctx->syn.screen.encode, RP_THREAD_LOOP_MED_WAIT);
			if (!screen) {
				if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) {
					nsDbgPrint("rp_screen_encode_acquire timeout\n");
					break;
				}
				continue;
			}
			acquire_count = 0;
		} else {
			screen = &rp_ctx->screen_encode[thread_n];
			if (rpScreenEncodeSetupMain(screen, &screen_encode_ctx, rp_ctx)) {
				break;
			}
		}

		ret = rpEncodeImage(screen, rp_ctx->conf.yuv_option, rp_ctx->conf.color_transform_hp);
		if (ret < 0) {
			nsDbgPrint("rpEncodeImage failed\n");
			break;
		}

		struct rp_image_t *image_curr = screen->image;
		struct rp_const_image_t *image_prev = screen->image_prev;
		struct rp_screen_ctx_t c = screen->c;
		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			if (rp_screen_transfer_release(&rp_ctx->syn.screen.transfer, screen) < 0) {
				nsDbgPrint("rpEncodeScreenAndSend screen release syn failed\n");
				break;
			}
		}
		screen = 0;

		ret = rpDownscaleMEImage(&c, &image_curr->d, image_prev, image_me, rp_ctx->conf.downscale_uv, &rp_ctx->conf.me, RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode);
		if (ret < 0) {
			nsDbgPrint("rpDownscaleMEImage failed\n");
			break;
		}
		image_prev = 0;
		struct rp_const_image_t *image = 0;

		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			// allow read
			if (rp_ctx->conf.me.method != 0) {
				image = rpImageWriteToRead(image_curr);
				image_curr = 0;
			}
#if !RP_SYN_EX
			if ((ret = rp_lock_wait(rp_ctx->network_mutex, RP_SYN_WAIT_MAX))) {
				nsDbgPrint("%d network_mutex wait timeout/error: %d\n", thread_n, ret); \
				break;
			}
#endif
		}

		struct rp_jls_ctx_t *jls_ctx = &rp_ctx->jls_ctx[thread_n];
		struct rp_const_image_data_t *im = c.p_frame ? rp_const_image_data(image_me) : rp_ctx->conf.me.method != 0 ? &image->d : &rp_const_image(image_curr)->d;

		ret = rpJLSEncodeScreenAndSend(&c, im, &rp_ctx->jls_param, jls_ctx, &rp_ctx->syn.network, rp_ctx->conf.downscale_uv, rp_ctx->conf.encoder_which, rp_ctx->conf.encode_verify,
			&rp_ctx->conf.me, &rp_ctx->exit_thread, (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode && RP_SYN_EX), thread_n);
		if (ret)
			break;

		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			if (rp_ctx->conf.me.method != 0) {
				// done read
				rpImageReadUnlockFromWrite(image);
			} else {
				// release write
				rpImageWriteUnlock(image_curr);
			}
#if !RP_SYN_EX
			rp_lock_rel(rp_ctx->network_mutex);
#endif
		}
	};
	rp_ctx->exit_thread = 1;
}

static void rpSecondThreadStart(u32 arg UNUSED) {
	struct rp_ctx_t *rp_ctx = (struct rp_ctx_t *)arg;

	rpEncodeScreenAndSend(rp_ctx, RP_SECOND_ENCODE_THREAD_ID);
	svc_exitThread();
}

static int rpSendFrames(struct rp_ctx_t *rp_ctx) {
	int ret = 0;
	int thread_n = RP_MAIN_ENCODE_THREAD_ID;

	if ((ret = rp_init_images(&rp_ctx->image_ctx, RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode)))
		return ret;

	if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
		if ((ret = rp_screen_queue_init(&rp_ctx->syn.screen, rp_ctx->screen_encode, rp_ctx->conf.encode_buffer_count))) {
			nsDbgPrint("rp_screen_queue_init failed %d\n", ret);
			return ret;
		}

#if !RP_SYN_EX
		rp_lock_close(rp_ctx->network_mutex);
		(void)rp_lock_init(rp_ctx->network_mutex);
#endif

		ret = svc_createThread(
			&rp_ctx->second_thread,
			rpSecondThreadStart,
			(u32)rp_ctx,
			(u32 *)&rp_ctx->second_thread_stack[RP_STACK_SIZE - 40],
			0x10,
			3);
		if (ret != 0) {
			nsDbgPrint("Create rpSecondThreadStart Thread Failed: %08x\n", ret);
			return -1;
		}
		ret = svc_createThread(
			&rp_ctx->screen_thread,
			rpScreenTransferThread,
			(u32)rp_ctx,
			(u32 *)&rp_ctx->screen_transfer_thread_stack[RP_MISC_STACK_SIZE - 40],
			0x8,
			2);
		if (ret != 0) {
			nsDbgPrint("Create rpScreenTransferThread Thread Failed: %08x\n", ret);

			rp_ctx->exit_thread = 1;
			svc_waitSynchronization1(rp_ctx->second_thread, U64_MAX);
			svc_closeHandle(rp_ctx->second_thread);
			return -1;
		}
	}

	rp_svc_print_limits();

	rpEncodeScreenAndSend(rp_ctx, thread_n);

	if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
		svc_waitSynchronization1(rp_ctx->second_thread, U64_MAX);
		svc_waitSynchronization1(rp_ctx->screen_thread, U64_MAX);
		svc_closeHandle(rp_ctx->second_thread);
		svc_closeHandle(rp_ctx->screen_thread);
	}

	return ret;
}

int rp_recv_sock = -1;
struct rp_net_ctx_t *rp_net_ctx;

Result __sync_init(void);
void rpThreadStart(u32 arg) {
	struct rp_ctx_t *rp_ctx = (struct rp_ctx_t *)arg;

	if (RP_SYN_METHOD == 0) {
		rp_svc_increase_limits();
	} else {
		__sync_init();
	}
	rp_init_image_buffers(&rp_ctx->image_ctx);
	jls_encoder_prepare_LUTs(&rp_ctx->jls_param);
	rpInitDmaHome(&rp_ctx->dma_ctx, rp_ctx->dma_config);

	rpNetworkInit(&rp_ctx->net_ctx, rp_ctx->nwm_send_buffer, rp_ctx->control_recv_buffer);
	rp_net_ctx = &rp_ctx->net_ctx;

	int ret = 0;
	while (ret >= 0) {
		rp_set_params(&rp_ctx->conf);

		if ((ret = rpKCPClear(&rp_ctx->net_ctx))) {
			nsDbgPrint("rpKCPClear timeout/error %d\n", ret);
			break;
		}

		rp_ctx->exit_thread = 0;
		if ((ret = rp_network_queue_init(&rp_ctx->syn.network, rp_ctx->network_encode, rp_ctx->conf.encode_buffer_count))) {
			nsDbgPrint("rp_network_queue_init failed %d\n", ret);
			break;
		}
		if ((ret = rpInitPriorityCtx(&rp_ctx->dyn_prio, rp_ctx->conf.screen_priority, rp_ctx->conf.dynamic_priority, rp_ctx->conf.target_frame_rate))) {
			nsDbgPrint("rpInitPriorityCtx failed %d\n", ret);
			break;
		}

		ret = svc_createThread(
			&rp_ctx->network_thread,
			rpNetworkTransferThread,
			(u32)rp_ctx,
			(u32 *)&rp_ctx->network_transfer_thread_stack[RP_MISC_STACK_SIZE - 40],
			0x8,
			3);
		if (ret != 0) {
			nsDbgPrint("Create rpNetworkTransferThread Failed: %08x\n", ret);
			break;
		}

		ret = rpSendFrames(rp_ctx);

		rp_ctx->exit_thread = 1;
		svc_waitSynchronization1(rp_ctx->network_thread, U64_MAX);
		svc_closeHandle(rp_ctx->network_thread);

		svc_sleepThread(RP_SYN_WAIT_IDLE);

		nsDbgPrint("Restarting RemotePlay threads\n");
	}
	svc_exitThread();
}
