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

	u8 screen_capture_buffer[RP_ENCODE_CAPTURE_BUFFER_COUNT][RP_SCREEN_BUFFER_SIZE] ALIGN_4;
	struct rp_screen_capture_syn_t {
		rp_sem_t sem;
		u8 count;
	} screen_capture_syn[RP_ENCODE_CAPTURE_BUFFER_COUNT];
	u8 screen_capture_n;

	struct rp_screen_encode_t *screen_left;

	struct rp_dyn_prio_t *dyn_prio;
};

struct rp_screen_image_t;
struct rp_image_t;
struct rp_dma_ctx_t;
int rpScreenEncodeInit(struct rp_screen_state_t *ctx, struct rp_dyn_prio_t *dyn_prio, u32 min_capture_interval_ticks, u8 sync);
int rpScreenEncodeSetup(
	struct rp_screen_encode_t *screen, struct rp_screen_state_t *ctx,
	struct rp_screen_image_t screen_images[SCREEN_COUNT],
	struct rp_image_t images_1[SCREEN_COUNT][RP_IMAGE_BUFFER_COUNT],
	struct rp_image_t images_2[SCREEN_COUNT][RP_IMAGE_BUFFER_COUNT][RP_SCREEN_SPLIT_COUNT],
	struct rp_dma_ctx_t *dma, int me_enabled, int thread_n, int split_image, int capture_n);

struct rp_conf_me_t;
struct rp_screen_ctx_t;
struct rp_image_data_t;
struct rp_const_image_t;
struct rp_const_image_data_t;
int rpEncodeImage(struct rp_screen_encode_t *screen, int yuv_option, int color_transform_hp, int lq);
int rpDownscaleMEImage(struct rp_screen_ctx_t *c, struct rp_image_data_t *im, struct rp_const_image_t *image_prev, struct rp_image_data_t *image_me, u8 downscale_uv, struct rp_conf_me_t *me, u8 multicore, u8 lq);

int rpEncodeImageRGB(struct rp_screen_encode_t *screen, struct rp_image_data_t *image_me, int force_bpp8);

#endif
