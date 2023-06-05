#ifndef RP_IMAGE_H
#define RP_IMAGE_H

#include "rp_syn.h"

#define RP_IMAGE_DATA_T_DEFINE(n) \
	struct n { \
		CONST_OPT s8 *me_x_image; \
		CONST_OPT s8 *me_y_image;\
		CONST_OPT u8 *y_image;\
		CONST_OPT u8 *u_image;\
		CONST_OPT u8 *v_image;\
		CONST_OPT u8 *ds_y_image; \
		CONST_OPT u8 *ds_u_image; \
		CONST_OPT u8 *ds_v_image; \
		CONST_OPT u8 *ds_ds_y_image; \
		u8 y_bpp; \
		u8 u_bpp; \
		u8 v_bpp; \
		u8 me_bpp; \
	}

#define CONST_OPT
	RP_IMAGE_DATA_T_DEFINE(rp_image_data_t);
#undef CONST_OPT

#define CONST_OPT const
	RP_IMAGE_DATA_T_DEFINE(rp_const_image_data_t);
#undef CONST_OPT

// one for current encode thread; one for next frame reading motion est ref data
#define RP_IMAGE_READER_COUNT (2)

#if RP_SYN_EX
#define RP_IMAGE_T_DEFINE(n, dn) \
	struct n { \
		struct dn d; \
		rp_sem_t sem_write; \
		rp_sem_t sem_read; \
		u8 sem_count; \
	}
#else
#define RP_IMAGE_T_DEFINE(n, dn) \
	struct n { \
		struct dn d; \
		u8 format; \
	}
#endif

	RP_IMAGE_T_DEFINE(rp_image_t, rp_image_data_t);
	RP_IMAGE_T_DEFINE(rp_const_image_t, rp_const_image_data_t);

static ALWAYS_INLINE struct rp_const_image_t *rp_const_image(struct rp_image_t *image) { return (struct rp_const_image_t *)image; }
static ALWAYS_INLINE struct rp_const_image_data_t *rp_const_image_data(struct rp_image_data_t *im) { return (struct rp_const_image_data_t *)im; }

struct rp_image_ctx_t {
	struct rp_image_t image[SCREEN_MAX][RP_IMAGE_BUFFER_COUNT];

#define RP_IMAGE_BUFFER_DEFINE(sv) \
	struct { \
		u8 y_image[SCREEN_PADDED_SIZE(sv)] ALIGN_4; \
		u8 u_image[SCREEN_PADDED_SIZE(sv)] ALIGN_4; \
		u8 v_image[SCREEN_PADDED_SIZE(sv)] ALIGN_4; \
		u8 ds_y_image[SCREEN_PADDED_DS_SIZE(sv, 1)] ALIGN_4; \
		u8 ds_u_image[SCREEN_PADDED_DS_SIZE(sv, 1)] ALIGN_4; \
		u8 ds_v_image[SCREEN_PADDED_DS_SIZE(sv, 1)] ALIGN_4; \
		u8 ds_ds_y_image[SCREEN_PADDED_DS_SIZE(sv, 2)] ALIGN_4; \
	} \

	struct {
		RP_IMAGE_BUFFER_DEFINE(SCREEN_TOP) top[RP_IMAGE_BUFFER_COUNT];
		RP_IMAGE_BUFFER_DEFINE(SCREEN_BOT) bot[RP_IMAGE_BUFFER_COUNT];
	} image_buffer;

	struct rp_image_data_t image_me[RP_ENCODE_THREAD_COUNT];

	struct {
		s8 me_x_image[ME_SIZE_MAX] ALIGN_4;
		s8 me_y_image[ME_SIZE_MAX] ALIGN_4;
		u8 y_image[SCREEN_SIZE_MAX] ALIGN_4;
		u8 u_image[SCREEN_SIZE_MAX] ALIGN_4;
		u8 v_image[SCREEN_SIZE_MAX] ALIGN_4;
		u8 ds_u_image[SCREEN_DS_SIZE_MAX(1)] ALIGN_4;
		u8 ds_v_image[SCREEN_DS_SIZE_MAX(1)] ALIGN_4;
	} image_me_buffer[RP_ENCODE_THREAD_COUNT];

	struct rp_screen_image_t {
		u8 image_n;
		u8 frame_n;
		u8 p_frame;
		u8 format;
	} screen_image[SCREEN_MAX];
};

void rp_init_image_buffers(struct rp_image_ctx_t *ctx);
int rp_init_images(struct rp_image_ctx_t *ctx, int multicore);

#if RP_SYN_EX
int rpImageReadLock(struct rp_const_image_t *image);
void rpImageReadUnlockCount(struct rp_const_image_t *image, int count);
void rpImageReadUnlock(struct rp_const_image_t *image);
void rpImageReadSkip(struct rp_const_image_t *image);
int rpImageWriteLock(struct rp_const_image_t *image);
void rpImageWriteUnlock(struct rp_const_image_t *image);
void rpImageWriteToRead(struct rp_const_image_t *image);
#endif

#endif
