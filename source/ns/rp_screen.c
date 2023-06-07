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

	Handle hdma;

	int ret;
	if (isInVRAM(phys)) {
		rpCloseGameHandle(dma);
		ret = svc_startInterProcessDma(&hdma, CURRENT_PROCESS_HANDLE,
			dest, hProcess, (const void *)(0x1F000000 + (phys - 0x18000000)), bufSize, (u32 *)dma->dma_config);
		if (ret != 0)
			return ret;
	}
	else if (isInFCRAM(phys) && (hProcess = rpGetGameHandle(dma))) {
		ret = svc_startInterProcessDma(&hdma, CURRENT_PROCESS_HANDLE,
			dest, hProcess, (const void *)(dma->game_fcram_base + (phys - 0x20000000)), bufSize, (u32 *)dma->dma_config);
		if (ret != 0)
			return ret;
	} else {
		nsDbgPrint("No output avaiilable for capture\n");
		svc_sleepThread(RP_THREAD_LOOP_IDLE_WAIT);
		return 0;
	}

	u32 state, i;
	for (i = 0; i < RP_THREAD_LOOP_WAIT_COUNT; i++ ) {
		state = 0;
		svc_getDmaState(&state, hdma);
		if (state == 4)
			break;
		svc_sleepThread(RP_THREAD_LOOP_ULTRA_FAST_WAIT);
	}
	if (i >= RP_THREAD_LOOP_WAIT_COUNT) {
		nsDbgPrint("rpCaptureScreen time out %08x", state, 0);
	}
	svc_closeHandle(hdma);
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

void rpScreenEncodeInit(struct rp_screen_encode_ctx_t *ctx, struct rp_dyn_prio_t *dyn_prio, u32 min_capture_interval_ticks, u8 sync) {
	rp_lock_close(ctx->mutex);
	if (sync)
		(void)rp_lock_init(ctx->mutex);
	else
		ctx->mutex = 0;
	ctx->sync = sync;
	u64 curr_tick = svc_getSystemTick();
	ctx->last_tick = curr_tick;
	ctx->desired_last_tick = curr_tick + min_capture_interval_ticks;
	ctx->dyn_prio = dyn_prio;
	ctx->min_capture_interval_ticks = min_capture_interval_ticks;
}

static int rpScreenEncodeGetScreenLimitFrameRate(struct rp_screen_encode_ctx_t *ctx) {
	u64 curr_tick, tick_diff = (curr_tick = svc_getSystemTick()) - ctx->last_tick;
	s64 desired_tick_diff = (s64)curr_tick - (s64)ctx->desired_last_tick;

	if (desired_tick_diff < (s64)ctx->min_capture_interval_ticks) {
		u64 duration = ((s64)ctx->min_capture_interval_ticks - desired_tick_diff) * 1000 / SYSTICK_PER_US;
		svc_sleepThread(duration);
	} else {
		u64 min_tick = ctx->min_capture_interval_ticks * RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM;
		if (tick_diff < min_tick) {
			u64 duration = (min_tick - tick_diff) * 1000 / SYSTICK_PER_US;
			svc_sleepThread(duration);
		}
	}

	ctx->desired_last_tick += ctx->min_capture_interval_ticks;
	ctx->last_tick = curr_tick;

	int frame_rate = 1;
	int top_bot = rpGetPriorityScreen(ctx->dyn_prio, &frame_rate);

	u64 desired_last_tick_step = ctx->min_capture_interval_ticks * frame_rate *
		RP_BANDWIDTH_CONTROL_RATIO_NUM / RP_BANDWIDTH_CONTROL_RATIO_DENUM;
	if ((s64)ctx->last_tick - (s64)ctx->desired_last_tick > (s64)desired_last_tick_step)
		ctx->desired_last_tick = ctx->last_tick - desired_last_tick_step;

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

	u8 frame_n = screen_image->frame_n;
	screen_image->frame_n = (frame_n + 1) % RP_IMAGE_FRAME_N_RANGE;

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
	screen->image = &images[top_bot][image_n];
	image_n = (image_n + (RP_IMAGE_BUFFER_COUNT - 1)) % RP_IMAGE_BUFFER_COUNT;
	screen->image_prev = first_frame ? 0 : rp_const_image(&images[top_bot][image_n]);

	screen_image->first_frame = 0;
}

int rpScreenEncodeSetup(struct rp_screen_encode_t *screen, struct rp_screen_encode_ctx_t *ctx,
    struct rp_screen_image_t screen_images[SCREEN_MAX],
    struct rp_image_t images[SCREEN_MAX][RP_IMAGE_BUFFER_COUNT],
    struct rp_dma_ctx_t *dma, int no_p_frame
) {
	int ret;

	if (ctx->sync && (ret = rp_lock_wait(ctx->mutex, RP_SYN_WAIT_MAX)))
		return ret;
	screen->c.top_bot = rpScreenEncodeGetScreenLimitFrameRate(ctx);
	if (ctx->sync)
		rp_lock_rel(ctx->mutex);

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
	height = SCREEN_HEIGHT;

	struct rp_image_t *image = screen->image;
	struct rp_image_data_t *im = &image->d;

	return convert_yuv_image(
		screen->c.format, width, height, screen->pitch,
		screen->buffer,
		im->y_image, im->u_image, im->v_image,
		&im->y_bpp, &im->u_bpp, &im->v_bpp,
		yuv_option, color_transform_hp
	);
}

int rpDownscaleMEImage(struct rp_screen_ctx_t *c, struct rp_image_data_t *im, struct rp_const_image_t *image_prev, struct rp_image_data_t *image_me, u8 downscale_uv, struct rp_conf_me_t *me, u8 multicore UNUSED) {
	int UNUSED ret;

	image_me->me_bpp = me->bpp;

	int top_bot = c->top_bot;
	int UNUSED frame_n = c->frame_n;
	int p_frame = c->p_frame;

	int width, height;
	width = SCREEN_WIDTH(top_bot);
	height = SCREEN_HEIGHT;;

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

		im->ds_y_image = im->ds_y_image_ds_uv;
		im->ds_ds_y_image = im->ds_ds_y_image_ds_uv;
		im->mafd_ds_image = im->mafd_ds_image_ds_uv;
	} else {
		im->ds_y_image = im->ds_y_image_full_uv;
		im->ds_ds_y_image = im->ds_ds_y_image_full_uv;
		im->mafd_ds_image = im->mafd_ds_image_full_uv;
	}

	int ds_ds_width = DS_DIM(width, 2);
	int ds_ds_height = DS_DIM(height, 2);

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

		if (multicore && me->enabled != 0) {
			// lock read
			if ((ret = rpImageReadLock(image_prev))) {
				nsDbgPrint("rpEncodeImage rpImageReadLock image_prev timeout/error\n", ret);
				return -1;
			}
		}

		if (me->enabled == 1) {

#define MOTION_EST(n, m, w, h, b) do { \
	motion_estimate(image_me->me_x_image, image_me->me_y_image, \
		im_prev->n + LEFTMARGIN, im->n + LEFTMARGIN, \
		me->select, me->select_threshold, im->m, im_prev->m, me->mafd_shift, \
		w, h, h + LEFTMARGIN + RIGHTMARGIN, im->b, \
		me->block_size, me->block_size_log2, \
		me->search_param, me->method, me->bpp_half_range \
	); \
} while (0)

			if (me->downscale) {
				MOTION_EST(ds_ds_y_image, mafd_ds_image, ds_ds_width, ds_ds_height, y_bpp);
			} else {
				MOTION_EST(ds_y_image, mafd_image, ds_width, ds_height, y_bpp);
			}

			int scale_log2_offset = me->downscale == 0 ? 0 : 1;
			int scale_log2 = 1 + scale_log2_offset;
			int ds_scale_log2 = 0 + scale_log2_offset;

#define PREDICT_IM(n, w, h, s, b) do { \
	predict_image(image_me->n, im_prev->n, im->n, \
		image_me->me_x_image, image_me->me_y_image, \
		w, h, s, im->b, \
		me->block_size, me->block_size_log2, \
		RP_ME_INTERPOLATE && me->interpolate, \
		me->select, me->bpp_half_range); \
} while (0)

			PREDICT_IM(y_image, width, height, scale_log2, y_bpp);

			if (downscale_uv) {
				PREDICT_IM(ds_u_image, ds_width, ds_height, ds_scale_log2, u_bpp);
				PREDICT_IM(ds_v_image, ds_width, ds_height, ds_scale_log2, v_bpp);
			} else {
				PREDICT_IM(u_image, width, height, scale_log2, u_bpp);
				PREDICT_IM(v_image, width, height, scale_log2, v_bpp);
			}

			me_add_half_range((u8 *)image_me->me_x_image, width, height, scale_log2,
				me->bpp_half_range, me->block_size_log2);
			me_add_half_range((u8 *)image_me->me_y_image, width, height, scale_log2,
				me->bpp_half_range, me->block_size_log2);
		} else {

#define DIFF_IM(n, w, h, b) do { \
	diff_image(image_me->n, im_prev->n, im->n, w, h, im->b); \
} while (0)

			DIFF_IM(y_image, width, height, y_bpp);

			if (downscale_uv) {
				DIFF_IM(ds_u_image, ds_width, ds_height, u_bpp);
				DIFF_IM(ds_v_image, ds_width, ds_height, v_bpp);
			} else {
				DIFF_IM(u_image, width, height, u_bpp);
				DIFF_IM(v_image, width, height, v_bpp);
			}

		}

		image_me->y_bpp = im->y_bpp;
		image_me->u_bpp = im->u_bpp;
		image_me->v_bpp = im->v_bpp;

		if (multicore && me->enabled != 0) {
			// done read
			rpImageReadUnlock(image_prev);
		}
	} else {
		if (multicore && me->enabled != 0) {
			if (!c->first_frame) {
				// done read by skipping
				rpImageReadSkip(image_prev);
			}

#define MAFD_IMAGE(m, n, w, h, b) do { \
	mafd_image(im->m, me->mafd_shift, im->n + LEFTMARGIN, w, h, h + LEFTMARGIN + RIGHTMARGIN, me->block_size, me->block_size_log2, im->b); \
} while (0)

			if (me->select) {
				if (me->downscale) {
					MAFD_IMAGE(mafd_ds_image, ds_ds_y_image, ds_ds_width, ds_ds_height, y_bpp);
				} else {
					MAFD_IMAGE(mafd_image, ds_y_image, ds_width, ds_height, y_bpp);
				}
			}
		}
	}
	im->ds_y_image = 0;
	im->ds_ds_y_image = 0;
	im->mafd_ds_image = 0;

	return 0;
}
