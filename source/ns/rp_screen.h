#ifndef RP_SCREEN_H
#define RP_SCREEN_H

#include "rp_image.h"
#include "rp_dyn_prio.h"

struct rp_screen_encode_t;
struct rp_dma_ctx_t;
int rpCaptureScreen(struct rp_screen_encode_t *screen, struct rp_dma_ctx_t *dma);
void rpKernelCallback(struct rp_screen_encode_t *screen);

struct rp_screen_encode_ctx_t {
	int sleep_duration;
	u64 last_tick;
    u32 max_capture_interval_ticks;
    struct rp_dyn_prio_t *dyn_prio;
};

struct rp_screen_image_t;
struct rp_image_t;
void rpScreenEncodeInit(struct rp_screen_encode_ctx_t *ctx, struct rp_dyn_prio_t *dyn_prio, u32 max_capture_interval_ticks);
int rpScreenEncodeSetup(
    struct rp_screen_encode_t *screen, struct rp_screen_encode_ctx_t *ctx,
    struct rp_screen_image_t screen_images[SCREEN_MAX], struct rp_image_t images[SCREEN_MAX][RP_IMAGE_BUFFER_COUNT],
    struct rp_dma_ctx_t *dma, int no_p_frame);

struct rp_conf_me_t;
struct rp_screen_ctx_t;
struct rp_image_data_t;
int rpEncodeImage(struct rp_screen_encode_t *screen, int yuv_option, int color_transform_hp);
int rpDownscaleMEImage(struct rp_screen_ctx_t *c, struct rp_image_data_t *image_me, u8 downscale_uv, struct rp_conf_me_t *me, u8 multicore);

#endif
