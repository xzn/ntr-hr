#include "rp_ctx.h"
#include "rp_net.h"
#include "rp_conf.h"
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
			&rp_ctx->net_ctx, thread_n, &rp_ctx->kcp, &rp_ctx->conf.kcp, &rp_ctx->exit_thread,
			&rp_ctx->syn.network, rp_ctx->conf.min_send_interval_ticks);
		svc_sleepThread(RP_THREAD_LOOP_SLOW_WAIT);
	}
	svc_exitThread();
}

struct rp_encode_and_send_screen_ctx_t {
	struct rp_send_data_header *send_header;
	struct rp_jls_params_t *jls_param;
	struct rp_jls_ctx_t *jls_ctx;
	struct rp_syn_comp_t *network_queue;
	u8 downscale_uv;
	u8 encoder_which;
	u8 encode_verify;
	volatile u8 *exit_thread;
	u8 multicore;
	u8 thread_n;
};

static int rpJLSEncodeDownscaledPlaneAndSend(struct rp_encode_and_send_screen_ctx_t *ctx, const u8 *n, const u8 *ds_n,
	int w, int h, int ds_w, int ds_h, int bpp
) {
	ctx->send_header->bpp = bpp;
	return rpJLSEncodeImage(ctx->send_header, ctx->network_queue, ctx->multicore, ctx->exit_thread,
		ctx->jls_param, ctx->jls_ctx,
		(const u8 *)(ctx->downscale_uv ? ds_n : n),
		ctx->downscale_uv ? ds_w : w,
		ctx->downscale_uv ? ds_h : h,
		bpp,
		ctx->encoder_which,
		ctx->encode_verify,
		ctx->thread_n
	);
}

static int rpJLSEncodePlaneAndSend(struct rp_encode_and_send_screen_ctx_t *ctx, const u8 *n,
	int w, int h, int bpp) {
	return rpJLSEncodeDownscaledPlaneAndSend(ctx, n, n, w, h, w, h, bpp);
}

static void rpUpdateSendHeader(struct rp_send_data_header *send_header, u8 plane_type, u8 plane_comp) {
	send_header->data_end = 0;
	send_header->plane_type = plane_type;
	send_header->plane_comp = plane_comp;
}

static int rpJLSEncodeScreenAndSend(struct rp_encode_and_send_screen_ctx_t *ctx,
	struct rp_const_image_data_t *im, struct rp_screen_ctx_t *c, struct rp_conf_me_t *me
) {
	int scale_log2_offset = me->downscale == 0 ? 0 : 1;
	int scale_log2 = 1 + scale_log2_offset;
	u8 block_size_log2 = me->block_size_log2 + scale_log2;

	int width = SCREEN_WIDTH(c->top_bot);
	int height = SCREEN_HEIGHT;

	int me_width = width >> block_size_log2;
	int me_height = height >> block_size_log2;

	int ds_width = DS_DIM(width, 1);
	int ds_height = DS_DIM(height, 1);

	int ret, size = 0;

	rpUpdateSendHeader(ctx->send_header, RP_PLANE_TYPE_ME, RP_PLANE_COMP_ME_X);
	ret = rpJLSEncodePlaneAndSend(ctx, (const u8 *)im->me_x_image, me_width, me_height, im->me_bpp);
	if (ret < 0) { return ret; } size += ret;

	rpUpdateSendHeader(ctx->send_header, RP_PLANE_TYPE_ME, RP_PLANE_COMP_ME_Y);
	ret = rpJLSEncodePlaneAndSend(ctx, (const u8 *)im->me_y_image, me_width, me_height, im->me_bpp);
	if (ret < 0) { return ret; } size += ret;

	rpUpdateSendHeader(ctx->send_header, RP_PLANE_TYPE_COLOR, RP_PLANE_COMP_Y);
	ret = rpJLSEncodePlaneAndSend(ctx, im->y_image, width, height, im->y_bpp);
	if (ret < 0) { return ret; } size += ret;

	rpUpdateSendHeader(ctx->send_header, RP_PLANE_TYPE_COLOR, RP_PLANE_COMP_U);
	ret = rpJLSEncodeDownscaledPlaneAndSend(ctx, im->u_image, im->ds_u_image, width, height, ds_width, ds_height, im->u_bpp);
	if (ret < 0) { return ret; } size += ret;

	rpUpdateSendHeader(ctx->send_header, RP_PLANE_TYPE_COLOR, RP_PLANE_COMP_V);
	ret = rpJLSEncodeDownscaledPlaneAndSend(ctx, im->v_image, im->ds_v_image, width, height, ds_width, ds_height, im->v_bpp);
	if (ret < 0) { return ret; } size += ret;

	return size;
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
			if (rp_screen_transfer_release(&rp_ctx->syn.screen.transfer, screen) != 0) {
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
#if RP_SYN_NET == 1
			if ((ret = rp_lock_wait(rp_ctx->network_mutex, RP_SYN_WAIT_MAX))) {
				nsDbgPrint("%d network_mutex wait timeout/error: %d\n", thread_n, ret); \
				break;
			}
#elif RP_SYN_NET == 2
			if ((ret = rp_sem_wait(rp_ctx->network_sem[thread_n], RP_SYN_WAIT_MAX))) {
				nsDbgPrint("%d network_sem wait timeout/error: %d\n", thread_n, ret); \
				break;
			}
#endif
		}

		struct rp_jls_ctx_t *jls_ctx = &rp_ctx->jls_ctx[thread_n];
		struct rp_const_image_data_t *im = c.p_frame ? rp_const_image_data(image_me) : rp_ctx->conf.me.method != 0 ? &image->d : &rp_const_image(image_curr)->d;

		struct rp_send_data_header send_header = {
			.type_data = RP_SEND_HEADER_TYPE_DATA,
			.top_bot = c.top_bot,
			.frame_n = c.frame_n,
			.p_frame = c.p_frame,
		};
		struct rp_encode_and_send_screen_ctx_t encode_send_ctx = {
			.send_header = &send_header,
			.jls_param = &rp_ctx->jls_param,
			.jls_ctx = jls_ctx,
			.network_queue = &rp_ctx->syn.network,
			.downscale_uv = rp_ctx->conf.downscale_uv,
			.encoder_which = rp_ctx->conf.encoder_which,
			.encode_verify = rp_ctx->conf.encode_verify,
			.exit_thread = &rp_ctx->exit_thread,
			.multicore = (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode && !RP_SYN_NET),
			.thread_n = thread_n
		};
		ret = rpJLSEncodeScreenAndSend(&encode_send_ctx, im, &c, &rp_ctx->conf.me);
		if (ret)
			break;
		rpSetPriorityScreen(&rp_ctx->dyn_prio, c.top_bot, ret); \

		if (RP_ENCODE_MULTITHREAD && rp_ctx->conf.multicore_encode) {
			if (rp_ctx->conf.me.method != 0) {
				// done read
				rpImageReadUnlockFromWrite(image);
			} else {
				// release write
				rpImageWriteUnlock(image_curr);
			}
#if RP_SYN_NET == 1
			rp_lock_rel(rp_ctx->network_mutex);
#elif RP_SYN_NET == 2
			rp_sem_rel(rp_ctx->network_sem[(thread_n + 1) % RP_ENCODE_THREAD_COUNT], 1);
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

#if RP_SYN_NET == 1
		rp_lock_close(rp_ctx->network_mutex);
		(void)rp_lock_init(rp_ctx->network_mutex);
#elif RP_SYN_NET == 2
		for (int i = 0; i < RP_ENCODE_THREAD_COUNT; ++i) {
			rp_sem_close(rp_ctx->network_sem[i]);
			(void)rp_sem_init(rp_ctx->network_sem[i], i == 0 ? 1 : 0, 1);
		}
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
		if ((ret = rpInitPriorityCtx(&rp_ctx->dyn_prio, rp_ctx->conf.screen_priority, rp_ctx->conf.dynamic_priority, rp_ctx->conf.min_dp_frame_rate))) {
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
