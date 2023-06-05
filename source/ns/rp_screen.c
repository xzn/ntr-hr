#include "rp_screen.h"
#include "rp_syn_chan.h"
#include "rp_dma.h"
#include "rp_image.h"
#include "rp_conf.h"
#include "rp_color.h"
#include "rp_color_aux.h"
#include "rp_me.h"

int rpCaptureScreen(struct rp_screen_encode_t *screen, struct rp_dma_ctx_t *dma) {
	u32 bufSize = screen->pitch * SCREEN_WIDTH(screen->c.top_bot);
	if (bufSize > RP_SCREEN_BUFFER_SIZE) {
		nsDbgPrint("rpCaptureScreen bufSize too large: %x > %x\n", bufSize, RP_SCREEN_BUFFER_SIZE);
		return -1;
	}

	u32 phys = screen->fbaddr;
	u8 *dest = screen->buffer;
	Handle hProcess = dma->home_handle;

	Handle *hdma = &screen->hdma;
	if (*hdma)
		svc_closeHandle(*hdma);
	*hdma = 0;

	int ret;
	if (isInVRAM(phys)) {
		rpCloseGameHandle(dma);
		ret = svc_startInterProcessDma(hdma, CURRENT_PROCESS_HANDLE,
			dest, hProcess, (const void *)(0x1F000000 + (phys - 0x18000000)), bufSize, (u32 *)dma->dma_config);
		if (ret != 0)
			return ret;
	}
	else if (isInFCRAM(phys)) {
		hProcess = rpGetGameHandle(dma);
		if (hProcess) {
			ret = svc_startInterProcessDma(hdma, CURRENT_PROCESS_HANDLE,
				dest, hProcess, (const void *)(dma->game_fcram_base + (phys - 0x20000000)), bufSize, (u32 *)dma->dma_config);
			if (ret != 0)
				return ret;
		} else {
			return 0;
		}
	} else {
		svc_sleepThread(RP_THREAD_LOOP_IDLE_WAIT);
		return 0;
	}

	u32 state, i;
	for (i = 0; i < RP_THREAD_LOOP_WAIT_COUNT; i++ ) {
		state = 0;
		svc_getDmaState(&state, *hdma);
		if (state == 4)
			break;
		svc_sleepThread(RP_THREAD_LOOP_ULTRA_FAST_WAIT);
	}
	if (i >= RP_THREAD_LOOP_WAIT_COUNT) {
		nsDbgPrint("rpCaptureScreen time out %08x", state, 0);
	}
	svc_invalidateProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)dest, bufSize);
	return 0;
}

void rpKernelCallback(struct rp_screen_encode_t *screen) {
	u32 current_fb;

	if (screen->c.top_bot == 0) {
		screen->c.format = REG(IoBasePdc + 0x470);
		screen->pitch = REG(IoBasePdc + 0x490);

		current_fb = REG(IoBasePdc + 0x478);
		current_fb &= 1;

		screen->fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x468) :
			REG(IoBasePdc + 0x46c);
	} else {
		screen->c.format = REG(IoBasePdc + 0x570);
		screen->pitch = REG(IoBasePdc + 0x590);

		current_fb = REG(IoBasePdc + 0x578);
		current_fb &= 1;

		screen->fbaddr = current_fb == 0 ?
			REG(IoBasePdc + 0x568) :
			REG(IoBasePdc + 0x56c);
	}
	screen->c.format &= 0x0f;
}

void rpScreenEncodeInit(struct rp_screen_encode_ctx_t *ctx, struct rp_dyn_prio_t *dyn_prio, u32 max_capture_interval_ticks) {
	ctx->sleep_duration = 0;
	ctx->last_tick = svc_getSystemTick();
	ctx->dyn_prio = dyn_prio;
    ctx->max_capture_interval_ticks = max_capture_interval_ticks;
}

static int rpScreenEncodeGetScreenLimitFrameRate(struct rp_screen_encode_ctx_t *ctx) {
	if (ctx->sleep_duration)
		svc_sleepThread(ctx->sleep_duration);

	// limit frame rate
	u64 curr_tick, tick_diff = (curr_tick = svc_getSystemTick()) - ctx->last_tick;
	int frame_rate = 1;
	int top_bot = rpGetPriorityScreen(ctx->dyn_prio, &frame_rate);
	u64 desired_tick_diff = (u64)SYSTICK_PER_SEC * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM / frame_rate;
	desired_tick_diff = RP_MIN(desired_tick_diff, ctx->max_capture_interval_ticks);
	if (tick_diff < desired_tick_diff) {
		ctx->sleep_duration = (desired_tick_diff - tick_diff) * 1000 / SYSTICK_PER_US;
	} else {
		ctx->sleep_duration = 0;
	}
	ctx->last_tick = curr_tick;

	return top_bot;
}

static int rpScreenEncodeCaptureScreen(struct rp_screen_encode_t *screen, struct rp_dma_ctx_t *dma) {
	int ret;
	int capture_n = 0;
	do {
		rpKernelCallback(screen);
		ret = rpCaptureScreen(screen, dma);

		if (ret == 0)
			break;

		if (++capture_n > RP_THREAD_LOOP_WAIT_COUNT) {
			nsDbgPrint("rpCaptureScreen failed\n");
			return -1;
		}

		svc_sleepThread(RP_THREAD_LOOP_FAST_WAIT);
	} while (1);
	return 0;
}

static void rpScreenEncodeReadyImage(
    struct rp_screen_encode_t *screen, struct rp_screen_image_t screen_images[SCREEN_MAX],
    struct rp_image_t images[SCREEN_MAX][RP_IMAGE_BUFFER_COUNT],
    int no_p_frame
) {
	int top_bot = screen->c.top_bot;
	struct rp_screen_image_t *screen_image = &screen_images[top_bot];

	u8 image_n = screen_image->image_n;
	screen_image->image_n = (image_n + 1) % RP_IMAGE_BUFFER_COUNT;

	u8 frame_n = screen_image->frame_n++;
	u8 first_frame = screen_image->first_frame;

	u8 p_frame = screen_image->p_frame;
	if (no_p_frame) {
		p_frame = screen_image->p_frame = 0;
	} else if (!p_frame) {
		screen_image->p_frame = 1;
	} else if (first_frame) {
		nsDbgPrint("initialization error, p_frame should be 0 when first_frame is 1 (this should be harmless...)\n");
		p_frame = 0;
	} else if (screen->c.format != screen_image->format) {
		nsDbgPrint("format change, key frame\n");
		p_frame = 0;
	}

	screen_image->format = screen->c.format;

	screen->c.first_frame = first_frame;
	screen->c.p_frame = p_frame;
	screen->c.frame_n = frame_n;
	screen->c.image = &images[top_bot][image_n];
	image_n = (image_n + (RP_IMAGE_BUFFER_COUNT - 1)) % RP_IMAGE_BUFFER_COUNT;
	screen->c.image_prev = first_frame ? 0 : rp_const_image(&images[top_bot][image_n]);

	screen_image->first_frame = 0;
}

int rpScreenEncodeSetup(struct rp_screen_encode_t *screen, struct rp_screen_encode_ctx_t *ctx,
    struct rp_screen_image_t screen_images[SCREEN_MAX],
    struct rp_image_t images[SCREEN_MAX][RP_IMAGE_BUFFER_COUNT],
    struct rp_dma_ctx_t *dma, int no_p_frame
) {
	int ret;

	screen->c.top_bot = rpScreenEncodeGetScreenLimitFrameRate(ctx);

	if ((ret = rpScreenEncodeCaptureScreen(screen, dma)) != 0)
		return ret;

	rpScreenEncodeReadyImage(screen, screen_images, images, no_p_frame);
	return 0;
}

int rpEncodeImage(struct rp_screen_encode_t *screen, int yuv_option, int color_transform_hp) {
	struct rp_screen_ctx_t c = screen->c;
	int top_bot = c.top_bot;

	int width, height;
	width = SCREEN_WIDTH(top_bot);
	height = SCREEN_HEIGHT;;

	struct rp_image_t *image = c.image;
	struct rp_image_data_t *im = &image->d;

	return convert_yuv_image(
		screen->c.format, width, height, screen->pitch,
		screen->buffer,
		im->y_image, im->u_image, im->v_image,
		&im->y_bpp, &im->u_bpp, &im->v_bpp,
		yuv_option, color_transform_hp
	);
}

int rpDownscaleMEImage(struct rp_screen_ctx_t *c, struct rp_image_data_t *image_me, u8 downscale_uv, struct rp_conf_me_t *me, u8 multicore UNUSED) {
	int UNUSED ret;

	image_me->me_bpp = me->bpp;

	int top_bot = c->top_bot;
	int UNUSED frame_n = c->frame_n;
	int p_frame = c->p_frame;

	int width, height;
	width = SCREEN_WIDTH(top_bot);
	height = SCREEN_HEIGHT;;

	struct rp_image_t *image = c->image;
	struct rp_image_data_t *im = &image->d;

	struct rp_const_image_t *image_prev = c->image_prev;
	struct rp_const_image_data_t *im_prev = c->first_frame ? 0 : &image_prev->d;

	int ds_width = DS_DIM(width, 1);
	int ds_height = DS_DIM(height, 1);

	if (downscale_uv) {
		downscale_image(
			im->ds_u_image,
			im->u_image,
			width, height
		);

		downscale_image(
			im->ds_v_image,
			im->v_image,
			width, height
		);
	}

	if (p_frame) {
		downscale_image(
			im->ds_y_image,
			im->y_image,
			width, height
		);

		if (me->downscale) {
			downscale_image(
				im->ds_ds_y_image,
				im->ds_y_image,
				ds_width, ds_height
			);
		}

#if RP_SYN_EX
		if (multicore) {
			// lock read
			if ((ret = rpImageReadLock(image_prev))) {
				nsDbgPrint("rpEncodeImage rpImageReadLock image_prev timeout/error\n", ret);
				return -1;
			}
		}
#endif

#define MOTION_EST(n, w, h) do { \
	motion_estimate(image_me->me_x_image, image_me->me_y_image, \
		im_prev->n + LEFTMARGIN, im->n + LEFTMARGIN, \
		w, h, h + LEFTMARGIN + RIGHTMARGIN, \
		me->block_size, me->block_size_log2, \
		me->search_param, me->method \
	); \
} while (0)

		if (me->downscale) {
			int ds_ds_width = DS_DIM(width, 2);
			int ds_ds_height = DS_DIM(height, 2);

			MOTION_EST(ds_ds_y_image, ds_ds_width, ds_ds_height);
		} else {
			MOTION_EST(ds_y_image, ds_width, ds_height);
		}

		int scale_log2_offset = me->downscale == 0 ? 0 : 1;
		int scale_log2 = 1 + scale_log2_offset;
		int ds_scale_log2 = 0 + scale_log2_offset;

#define PREDICT_IM(n, w, h, s) do { \
	predict_image(image_me->n, im_prev->n, im->n, \
		image_me->me_x_image, image_me->me_y_image, \
		w, h, s, im->y_bpp, \
		me->block_size, me->block_size_log2, \
		RP_ME_INTERPOLATE && me->interpolate); \
} while (0)

		PREDICT_IM(y_image, width, height, scale_log2);

		if (downscale_uv) {
			PREDICT_IM(ds_u_image, ds_width, ds_height, ds_scale_log2);
			PREDICT_IM(ds_v_image, ds_width, ds_height, ds_scale_log2);
		} else {
			PREDICT_IM(u_image, width, height, scale_log2);
			PREDICT_IM(v_image, width, height, scale_log2);
		}

		image_me->y_bpp = im->y_bpp;
		image_me->u_bpp = im->u_bpp;
		image_me->v_bpp = im->v_bpp;

		me_add_half_range((u8 *)image_me->me_x_image, width, height, scale_log2,
			me->bpp_half_range, me->block_size_log2);
		me_add_half_range((u8 *)image_me->me_y_image, width, height, scale_log2,
			me->bpp_half_range, me->block_size_log2);

#if RP_SYN_EX
		if (multicore) {
			// done read
			rpImageReadUnlock(image_prev);
		}
#endif
	} else {
#if RP_SYN_EX
		if (multicore && !c->first_frame) {
			// done read by skipping
			rpImageReadSkip(image_prev);
		}
#endif
	}
	return 0;
}
