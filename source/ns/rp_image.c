#include "rp_image.h"

void rp_init_image_buffers(struct rp_image_ctx_t *ctx) {
	for (int i = 0; i < RP_IMAGE_BUFFER_COUNT; ++i) {

#define SET_IMAGE_BUFFER(sv, sn, in) do { ctx->image[sv][i].d.in = ctx->image_buffer.sn[i].in; } while (0)

#define SET_IMAGE_BUFFER_SCREEN(sv, sn) do { \
	SET_IMAGE_BUFFER(sv, sn, y_image); \
	SET_IMAGE_BUFFER(sv, sn, u_image); \
	SET_IMAGE_BUFFER(sv, sn, v_image); \
	SET_IMAGE_BUFFER(sv, sn, ds_y_image_ds_uv); \
	SET_IMAGE_BUFFER(sv, sn, ds_y_image_full_uv); \
	SET_IMAGE_BUFFER(sv, sn, ds_u_image); \
	SET_IMAGE_BUFFER(sv, sn, ds_v_image); \
	SET_IMAGE_BUFFER(sv, sn, ds_ds_y_image_ds_uv); \
	SET_IMAGE_BUFFER(sv, sn, ds_ds_y_image_full_uv); \
	SET_IMAGE_BUFFER(sv, sn, mafd_image); \
	SET_IMAGE_BUFFER(sv, sn, mafd_ds_image_ds_uv); \
	SET_IMAGE_BUFFER(sv, sn, mafd_ds_image_full_uv); \
} while (0)

		SET_IMAGE_BUFFER_SCREEN(SCREEN_TOP, top);
		SET_IMAGE_BUFFER_SCREEN(SCREEN_BOT, bot);
	}

	for (int i = 0; i < RP_ENCODE_THREAD_COUNT; ++i) {
#define SET_IMAGE_ME_BUFFER(in) do { ctx->image_me[i].in = ctx->image_me_buffer[i].in; } while (0)

#define SET_IMAGE_ME_BUFFER_SCREEN() do { \
	SET_IMAGE_ME_BUFFER(me_x_image); \
	SET_IMAGE_ME_BUFFER(me_y_image); \
	SET_IMAGE_ME_BUFFER(y_image); \
	SET_IMAGE_ME_BUFFER(u_image); \
	SET_IMAGE_ME_BUFFER(v_image); \
	SET_IMAGE_ME_BUFFER(ds_u_image); \
	SET_IMAGE_ME_BUFFER(ds_v_image); \
} while (0)

		SET_IMAGE_ME_BUFFER_SCREEN();
	}
}

int rp_init_images(struct rp_image_ctx_t *ctx, int multicore) {
	for (int i = 0; i < SCREEN_MAX; ++i) {
		ctx->screen_image[i].frame_n = ctx->screen_image[i].p_frame = 0;
		ctx->screen_image[i].first_frame = 1;
	}

#define RP_INIT_SEM(s, n, m) do { \
	rp_sem_close(s); \
	int res; \
	if ((res = rp_sem_init(s, n, m))) { \
		nsDbgPrint("rpSendFrames create sem failed: %d\n", res); \
		return -1; \
	} \
} while (0)

	if (multicore) {
		for (int i = 0; i < SCREEN_MAX; ++i) {
			for (int j = 0; j < RP_IMAGE_BUFFER_COUNT; ++j) {
				RP_INIT_SEM(ctx->image[i][j].sem_write, 1, 1);
				RP_INIT_SEM(ctx->image[i][j].sem_read, 0, 1);
				ctx->image[i][j].sem_count = 0;
			}
		}
	}
	return 0;
}

int rpImageReadLock(struct rp_const_image_t *image) {
	s32 res;
	if ((res = rp_sem_wait(image->sem_read, RP_SYN_WAIT_MAX))) {
		nsDbgPrint("(%d) sem read wait failed\n", (s32)image);
		return res;
	}
	return 0;
}

void rpImageReadUnlockCount(struct rp_const_image_t *image, int count UNUSED) {
	if (__atomic_add_fetch(&image->sem_count, count, __ATOMIC_RELAXED) >= RP_IMAGE_READER_COUNT) {
		__atomic_store_n(&image->sem_count, 0, __ATOMIC_RELAXED);
		rp_sem_rel(image->sem_write, 1);
	}
}

void rpImageReadUnlock(struct rp_const_image_t *image) {
	rpImageReadUnlockCount(image, 1);
}

void rpImageReadSkip(struct rp_const_image_t *image) {
	rpImageReadUnlockCount(image, 1);
}

int rpImageWriteLock(struct rp_image_t *image) {
	int ret;
	if ((ret = rp_sem_wait(image->sem_write, RP_SYN_WAIT_MAX))) {
		return ret;
	}

	return 0;
}

void rpImageWriteUnlock(struct rp_const_image_t *image) {
	rp_sem_rel(image->sem_write, 1);
}

#if RP_SYN_EX
struct rp_const_image_t *rpImageWriteToRead(struct rp_image_t *image) {
	rp_sem_rel(image->sem_read, 1);
	return rp_const_image(image);
}

void rpImageReadUnlockFromWrite(struct rp_const_image_t *image) {
	rpImageReadUnlockCount(image, 1);
}
#else
struct rp_const_image_t *rpImageWriteToRead(struct rp_image_t *image UNUSED) {
	return rp_const_image(image);
}

void rpImageReadUnlockFromWrite(struct rp_const_image_t *image) {
	rp_sem_rel(image->sem_read, 1);
	rpImageReadUnlockCount(image, 1);
}
#endif
