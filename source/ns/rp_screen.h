#ifndef RP_SCREEN_H
#define RP_SCREEN_H

#include "rp_image.h"
#include "rp_dyn_prio.h"
#include "rp_syn.h"

struct rp_screen_encode_t;
void rpKernelCallback(struct rp_screen_encode_t *screen);

struct rp_screen_state_t {
	u8 sync;
	rp_lock_t mutex;
	u64 last_tick;
	u64 desired_last_tick;
	u32 min_capture_interval_ticks;
	struct rp_dyn_prio_t *dyn_prio;
};

struct rp_screen_image_t;
struct rp_image_t;
struct rp_dma_ctx_t;
void rpScreenEncodeInit(struct rp_screen_state_t *ctx, struct rp_dyn_prio_t *dyn_prio, u32 min_capture_interval_ticks, u8 sync);
int rpScreenEncodeSetup(
	struct rp_screen_encode_t *screen, struct rp_screen_state_t *ctx,
	struct rp_screen_image_t screen_images[SCREEN_MAX], struct rp_image_t images[SCREEN_MAX][RP_IMAGE_BUFFER_COUNT],
	struct rp_dma_ctx_t *dma, int me_enabled, int lock_write, int thread_n);

struct rp_conf_me_t;
struct rp_screen_ctx_t;
struct rp_image_data_t;
struct rp_const_image_t;
struct rp_const_image_data_t;
int rpEncodeImage(struct rp_screen_encode_t *screen, int yuv_option, int color_transform_hp);
int rpDownscaleMEImage(struct rp_screen_ctx_t *c, struct rp_image_data_t *im, struct rp_const_image_t *image_prev, struct rp_image_data_t *image_me, u8 downscale_uv, struct rp_conf_me_t *me, u8 multicore);

int rpEncodeImageRGB(struct rp_screen_encode_t *screen);

#endif
