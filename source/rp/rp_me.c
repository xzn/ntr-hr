#include "rp_me.h"
#include "rp_color_aux.h"

UNUSED static u32 sad_const(const u8 *src, int stride, int width, int height, u8 bpp) {
	u32 sad = 0;
	int x, y;

	for (x = 0; x < width; x++) {
		for (y = 0; y < height; y++)
			sad += FFABS(src[x] - (1 << (bpp - 1)));
		src += stride;
	}
	return sad;
}

void calc_mafd_image(u16 *mafd, u8 mafd_shift UNUSED, const u8 *cur UNUSED, int width, int height, int pitch UNUSED, u8 block_size, u8 block_size_log2, u8 bpp UNUSED) {
	u8 block_size_mask = (1 << block_size_log2) - 1;
	u8 block_x_n = width >> block_size_log2;
	u8 block_y_n = height >> block_size_log2;
	u8 x_off = (width & block_size_mask) >> 1;
	u8 y_off = (height & block_size_mask) >> 1;

	for (int block_x = 0, x = x_off; block_x < block_x_n; ++block_x, x += block_size) {
		for (int block_y = 0, y = y_off; block_y < block_y_n; ++block_y, y += block_size) {
#if 0
			u16 sad = sad_const(cur + x * pitch + y, pitch, block_size, block_size, bpp) >> mafd_shift;
			*mafd++ = sad;
#else
			*mafd++ = 0;
#endif
		}
	}
}

void motion_estimate(s8 *me_x_image, s8 *me_y_image, const u8 *ref, const u8 *cur,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, u8 bpp,
	u8 block_size, u8 block_size_log2, u8 search_param, u8 me_method, u8 half_range
) {
	// ffmpeg is row-major, 3DS is column-major; so switch x and y
	{
		s8 *temp = me_x_image;
		me_x_image = me_y_image;
		me_y_image = temp;
	}
	{
		int temp = width;
		width = height;
		height = temp;
	}

	AVMotionEstContext me_ctx;
	AVMotionEstPredictor *preds UNUSED = me_ctx.preds;

	u8 block_size_mask = (1 << block_size_log2) - 1;
	u8 block_x_n = width >> block_size_log2;
	u8 block_y_n = height >> block_size_log2;
	u8 x_off = (width & block_size_mask) >> 1;
	u8 y_off = (height & block_size_mask) >> 1;

	ff_me_init_context(&me_ctx, block_size, search_param,
		width, height,
		0, width - block_size,
		0, height - block_size
	);
	me_ctx.linesize = pitch;
	me_ctx.data_ref = ref;
	me_ctx.data_cur = cur;

	select_threshold = ((u32)select_threshold * (1 << bpp)) >> (RP_IMAGE_ME_SELECT_BITS + mafd_shift);

	for (int block_y = 0, y = y_off; block_y < block_y_n; ++block_y, y += block_size) {
		me_x_image += LEFTMARGIN;
		me_y_image += LEFTMARGIN;

		for (int block_x = 0, x = x_off; block_x < block_x_n; ++block_x, x += block_size) {
			if (select) {
				u32 sad;
				ff_scene_sad_c(ref + y * pitch + x, pitch, cur + y * pitch + x, pitch, block_size, block_size, &sad);
				u16 sad_prev = *mafd_prev++;
				*mafd++ = sad >>= mafd_shift;
				s32 diff = FFABS((s32)sad_prev - (s32)sad);
				if (RP_MIN(diff, (s32)sad) >= select_threshold) {
					*me_x_image++ = -(s8)half_range;
					*me_y_image++ = -(s8)half_range;
					continue;
				}
			}

			int mv[2] = {x, y};

			switch (me_method) {
				default:
#if 0
					ff_me_search_esa(&me_ctx, x, y, mv);
#endif
					break;

				case AV_ME_METHOD_TSS:
					ff_me_search_tss(&me_ctx, x, y, mv);
					break;

				case AV_ME_METHOD_TDLS:
					ff_me_search_tdls(&me_ctx, x, y, mv);
					break;

				case AV_ME_METHOD_NTSS:
					ff_me_search_ntss(&me_ctx, x, y, mv);
					break;

				case AV_ME_METHOD_FSS:
					ff_me_search_fss(&me_ctx, x, y, mv);
					break;

				case AV_ME_METHOD_DS:
					ff_me_search_ds(&me_ctx, x, y, mv);
					break;

				case AV_ME_METHOD_HEXBS:
					ff_me_search_hexbs(&me_ctx, x, y, mv);
					break;
			}

			*me_x_image++ = mv[0] - x;
			*me_y_image++ = mv[1] - y;
		}

		me_x_image += RIGHTMARGIN;
		me_y_image += RIGHTMARGIN;
	}
}

enum {
	CORNER_TOP_LEFT,
	CORNER_BOT_LEFT,
	CORNER_BOT_RIGHT,
	CORNER_TOP_RIGHT,
	CORNER_COUNT,
};

static void interpolate_me(const s8 *me_x_vec[CORNER_COUNT], const s8 *me_y_vec[CORNER_COUNT], int scale_log2, int block_size, int block_size_log2, int i, int j, s8 *x, s8 *y) {
	int step_total = block_size * 2;
	int step_base = 1;
	int step = 2;

	int x_left = i * step + step_base;
	int x_right = step_total - x_left;

	int y_top = j * step + step_base;
	int y_bot = step_total - y_top;

	int rshift_scale = (block_size_log2 + 1) * 2 + 2;

	int x_unscaled =
		((int)*me_x_vec[CORNER_TOP_LEFT]) * x_left * y_top +
		((int)*me_x_vec[CORNER_BOT_LEFT]) * x_left * y_bot +
		((int)*me_x_vec[CORNER_BOT_RIGHT]) * x_right * y_bot +
		((int)*me_x_vec[CORNER_TOP_RIGHT]) * x_right * y_top;
	*x = srshift_to_even(x_unscaled, rshift_scale) << scale_log2;

	int y_unscaled =
		((int)*me_y_vec[CORNER_TOP_LEFT]) * x_left * y_top +
		((int)*me_y_vec[CORNER_BOT_LEFT]) * x_left * y_bot +
		((int)*me_y_vec[CORNER_BOT_RIGHT]) * x_right * y_bot +
		((int)*me_y_vec[CORNER_TOP_RIGHT]) * x_right * y_top;
	*y = srshift_to_even(y_unscaled, rshift_scale) << scale_log2;
}

void me_add_half_range(u8 *me, int width, int height, u8 scale_log2, u8 half_range, u8 block_size_log2) {
	block_size_log2 += scale_log2;
	u8 block_x_n = width >> block_size_log2;
	u8 block_y_n = height >> block_size_log2;

	convert_set_zero(&me);
	for (int i = 0; i < block_x_n; ++i) {
		if (i)
			convert_set_prev_first(&me, block_y_n);
		for (int j = 0; j < block_y_n; ++j) {
			*me = *me + half_range;
			++me;
		}
		convert_set_last(&me);
	}
}

static int downshift_image_g(u8 *dst, u8 *cur, int width, int height, int pitch UNUSED, int bpp, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap) {
	convert_set_zero(&dst);
	cur += LEFTMARGIN;

	u8 max_lq = (1 << (bpp - spp_lq)) - 1;
	s8 half_max_lq = (1 << (bpp - spp_lq - 1)) - 1;
	u8 bias_col = (1 << (spp_lq - 1));
	u8 bias_xor = (1 << spp_lq) - 1;

	for (int i = 0; i < width; ++i) {
		if (i > 0) {
			convert_set_prev_first(&dst, height);
			cur += LEFTMARGIN;
		}

		u8 bias = bias_col;
		for (int j = 0; j < height; ++j) {
			if (unsigned_wrap) {
				*dst = *cur >> spp_lq;
				*cur = *dst << spp_lq;
			} else if (unsigned_signed == 0) {
				*dst = RP_MIN((*cur + bias) >> spp_lq, max_lq);
				*cur = *dst << spp_lq;
			} else {
				*dst = RP_MIN(((s8)*cur + bias) >> spp_lq, half_max_lq);
				*cur = (s8)*dst << spp_lq;
			}
			bias ^= bias_xor;
			++dst, ++cur;
		}

		bias_col ^= bias_xor;
		convert_set_last(&dst);
		cur += RIGHTMARGIN;
	}
	return 0;
}

static int downshift_image_unsigned_signed_g(u8 *dst, u8 *cur, int width, int height, int pitch UNUSED, int bpp, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap) {
	if (unsigned_wrap) {
		return downshift_image_g(dst, cur, width, height, pitch, bpp, spp_lq, 0, 1);
	} else if (unsigned_signed == 0) {
		return downshift_image_g(dst, cur, width, height, pitch, bpp, spp_lq, 0, 0);
	} else {
		return downshift_image_g(dst, cur, width, height, pitch, bpp, spp_lq, 1, 0);
	}
}

int downshift_image(u8 *dst, u8 *cur, int width, int height, int pitch UNUSED, int bpp, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap) {
	return downshift_image_unsigned_signed_g(dst, cur, width, height, pitch, bpp, spp_lq, unsigned_signed, unsigned_wrap);
}

static int diff_image_g(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, int dsx, u8 block_size, u8 block_size_log2
) {
	convert_set_zero(&dst);
	ref += LEFTMARGIN;
	cur += LEFTMARGIN;

	u8 max_lq = (1 << (bpp - spp_lq)) - 1;
	s8 half_max_lq = (1 << (bpp - spp_lq - 1)) - 1;

#define DO_DIFF_UNSIGNED() do { \
	*dst = (*cur - *ref) / (1 << spp_lq); \
	*cur = *ref + (s8)*dst * (1 << spp_lq); \
	++dst, ++cur, ++ref; \
} while (0)

#define DO_DIFF_SIGNED() do { \
	*dst = ((s8)*cur - (s8)*ref) / (1 << spp_lq); \
	*cur = (s8)*ref + (s8)*dst * (1 << spp_lq); \
	++dst, ++cur, ++ref; \
} while (0)

#if 0
#define DO_DIFF() do { \
  if (unsigned_signed == 0) { \
    DO_DIFF_UNSIGNED(); \
  } else { \
    DO_DIFF_SIGNED(); \
  } \
} while (0)
#else
#define DO_DIFF() do { \
	u8 cur_shifted; \
	if (unsigned_wrap) { \
		cur_shifted = *cur >> spp_lq; \
		*dst = cur_shifted - (*ref >> spp_lq); \
		*cur = *ref + (((s8)*dst << spp_lq)); \
	} else if (unsigned_signed == 0) { \
		cur_shifted = RP_MIN((*cur + bias) >> spp_lq, max_lq); \
		*dst = cur_shifted - (*ref >> spp_lq); \
		*cur = *ref + (((s8)*dst << spp_lq)); \
	} else { \
		cur_shifted = RP_MIN(((s8)*cur + bias) >> spp_lq, half_max_lq); \
		*dst = (s8)cur_shifted - (s8)((s8)*ref >> spp_lq); \
		*cur = (s8)*ref + (((s8)*dst << spp_lq)); \
	} \
	++dst, ++cur, ++ref; \
} while (0)
#endif

	u8 bias_col = (1 << (spp_lq - 1));
	u8 bias_xor = (1 << spp_lq) - 1;

	if (select) {
		block_size = 1 << block_size_log2;
		block_size <<= scale_log2;
		block_size /= dsx;
		// block_size_log2 += scale_log2;
		// u8 block_size_mask = (1 << block_size_log2) - 1;
		u8 block_x_n = width / block_size;
		u8 block_y_n = height / block_size;
		u8 block_pitch = PADDED_HEIGHT(block_y_n);
		u8 x_off = (width % block_size) >> 1;
		u8 y_off = (height % block_size) >> 1;

		select_threshold = ((u32)select_threshold * (1 << bpp)) >> (RP_IMAGE_ME_SELECT_BITS + mafd_shift);

		convert_set_zero((u8 **)&me_x_image);

		const s8 *me_x_col = me_x_image;

		if (mafd && mafd_prev) {
			for (int block_x = 0, x = x_off; block_x < block_x_n; ++block_x, x += block_size) {
				if (block_x > 0) {
					convert_set_prev_first((u8 **)&me_x_image, block_y_n);
				}

				for (int block_y = 0, y = y_off; block_y < block_y_n; ++block_y, y += block_size) {
					u32 sad;
					ff_scene_sad_c(ref + x * pitch + y, pitch, cur + x * pitch + y, pitch, block_size, block_size, &sad);
					u16 sad_prev = *mafd_prev++;
					sad *= dsx * dsx;
					*mafd++ = sad >>= (mafd_shift + scale_log2 * 2);
					s32 diff = FFABS((s32)sad_prev - (s32)sad);
					if (RP_MIN(diff, (s32)sad) >= select_threshold) {
						*me_x_image++ = 1;
					} else {
						*me_x_image++ = 0;
					}
				}

				convert_set_last((u8 **)&me_x_image);
			}
		}

		for (int i = 0; i < width; ++i) {
			if (i > 0) {
				convert_set_prev_first(&dst, height);
				ref += LEFTMARGIN;
				cur += LEFTMARGIN;
			}

			int i_off = (i - x_off) % block_size;
			if (i > x_off && i_off == 0 && i < width - x_off - 1) {
				me_x_col += block_pitch;
			}

			const s8 *me_x = me_x_col;

			int scene_change = 0;

			if (select)
				scene_change = *me_x == 1;

			u8 bias = bias_col;

			for (int j = 0; j < height; ++j) {
				int j_off = (j - y_off) % block_size;
				if (j > y_off && j_off == 0 && j < height - y_off - 1) {
					++me_x;

					if (select)
						scene_change = *me_x == 1;
				}

				if (scene_change) {
					if (unsigned_wrap) {
						*dst = *cur >> spp_lq;
						*cur = *dst << spp_lq;
					} else if (unsigned_signed == 0) {
						*dst = RP_MIN((*cur + bias) >> spp_lq, max_lq);
						*cur = *dst << spp_lq;
					} else {
						*dst = RP_MIN(((s8)*cur + bias) >> spp_lq, half_max_lq);
						*cur = (s8)*dst << spp_lq;
					}
					++dst, ++cur, ++ref;
				} else {
					// do diff
					DO_DIFF();
				}
				bias ^= bias_xor;
			}

			bias_col ^= bias_xor;
			convert_set_last(&dst);
			ref += RIGHTMARGIN;
			cur += RIGHTMARGIN;
		}
	} else {
		for (int i = 0; i < width; ++i) {
			if (i > 0) {
				convert_set_prev_first(&dst, height);
				ref += LEFTMARGIN;
				cur += LEFTMARGIN;
			}

			u8 bias = bias_col;
			for (int j = 0; j < height; ++j) {
				DO_DIFF();
				bias ^= bias_xor;
			}

			bias_col ^= bias_xor;
			convert_set_last(&dst);
			ref += RIGHTMARGIN;
			cur += RIGHTMARGIN;
		}
	}

	return 0;
}

static int diff_image_block_size_log2_g(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, int dsx, u8 block_size, u8 block_size_log2
) {
	switch (block_size_log2) {
		default:
			return -1;

		case RP_ME_MIN_BLOCK_SIZE_LOG2:
			return diff_image_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, RP_ME_MIN_BLOCK_SIZE_LOG2);

		case RP_ME_MIN_BLOCK_SIZE_LOG2 + 1:
			return diff_image_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, RP_ME_MIN_BLOCK_SIZE_LOG2 + 1);

		case RP_ME_MIN_BLOCK_SIZE_LOG2 + 2:
			return diff_image_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, RP_ME_MIN_BLOCK_SIZE_LOG2 + 2);

		case RP_ME_MIN_BLOCK_SIZE_LOG2 + 3:
			return diff_image_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, RP_ME_MIN_BLOCK_SIZE_LOG2 + 3);
	}
}

static int diff_image_scale_log2_g(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, int dsx, u8 block_size, u8 block_size_log2
) {
	switch (scale_log2) {
		default:
			return -1;

		case 0:
			return diff_image_block_size_log2_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, 0, dsx, block_size, block_size_log2);

		case 1:
			return diff_image_block_size_log2_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, 1, dsx, block_size, block_size_log2);

		case 2:
			return diff_image_block_size_log2_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, 2, dsx, block_size, block_size_log2);
	}
}

static int diff_image_dsx_g(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, int dsx, u8 block_size, u8 block_size_log2
) {
	switch (dsx) {
		default:
			return -1;

		case 1:
			return diff_image_scale_log2_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, 1, block_size, block_size_log2);

		case 2:
			return diff_image_scale_log2_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, 2, block_size, block_size_log2);

		case 3:
			return diff_image_scale_log2_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, 3, block_size, block_size_log2);

		case 4:
			return diff_image_scale_log2_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, 4, block_size, block_size_log2);
	}
}

static int diff_image_unsigned_signed_g(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, int dsx, u8 block_size, u8 block_size_log2
) {
	if (unsigned_wrap) {
		return diff_image_dsx_g(me_x_image, dst, ref, cur, spp_lq, 0, 1, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);
	} else if (unsigned_signed == 0) {
		return diff_image_dsx_g(me_x_image, dst, ref, cur, spp_lq, 0, 0, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);
	} else {
		return diff_image_dsx_g(me_x_image, dst, ref, cur, spp_lq, 1, 0, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);
	}
}

static int diff_image_spp_lq_g(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, int dsx, u8 block_size, u8 block_size_log2
) {
	switch (spp_lq) {
#if 1
		case 0:
			return diff_image_unsigned_signed_g(me_x_image, dst, ref, cur, 0, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);

		case 1:
			return diff_image_unsigned_signed_g(me_x_image, dst, ref, cur, 1, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);

		case 2:
			return diff_image_unsigned_signed_g(me_x_image, dst, ref, cur, 2, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);

		case 3:
			return diff_image_unsigned_signed_g(me_x_image, dst, ref, cur, 3, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);

		case 4:
			return diff_image_unsigned_signed_g(me_x_image, dst, ref, cur, 4, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);
#endif

		default:
			return -1;
			// return diff_image_unsigned_signed_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);
	}
}

int diff_image(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed, u8 unsigned_wrap,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, int dsx, u8 block_size, u8 block_size_log2
) {
	return diff_image_spp_lq_g(me_x_image, dst, ref, cur, spp_lq, unsigned_signed, unsigned_wrap, select, select_threshold, mafd, mafd_prev, mafd_shift, width, height, pitch, bpp, scale_log2, dsx, block_size, block_size_log2);
}

void predict_image(u8 *dst, const u8 *ref, const u8 *cur, const s8 *me_x_image, const s8 *me_y_image, int width, int height, int scale_log2, int bpp,
	u8 block_size, u8 block_size_log2, int interpolate, u8 select, u8 half_range
) {
	interpolate = RP_ME_INTERPOLATE && interpolate;

	block_size <<= scale_log2;
	block_size_log2 += scale_log2;

	u8 block_size_mask = (1 << block_size_log2) - 1;

	u8 block_x_n UNUSED = width >> block_size_log2;
	u8 block_y_n = height >> block_size_log2;
	u8 block_pitch = PADDED_HEIGHT(block_y_n);
	u8 x_off = (width & block_size_mask) >> 1;
	u8 y_off = (height & block_size_mask) >> 1;

	if (select)
		interpolate = 0;

	if (interpolate) {
		x_off += block_size >> 1;
		y_off += block_size >> 1;
	}

	convert_set_zero(&dst);
	ref += LEFTMARGIN;
	cur += LEFTMARGIN;

	const s8 *me_x_col = me_x_image + LEFTMARGIN;
	const s8 *me_y_col = me_y_image + LEFTMARGIN;

	const s8 *me_x_col_vec[CORNER_COUNT];
	const s8 *me_y_col_vec[CORNER_COUNT];
	for (int i = 0; i < CORNER_COUNT; ++i) {
		me_x_col_vec[i] = me_x_col;
		me_y_col_vec[i] = me_y_col;
	}

	for (int i = 0; i < width; ++i) {
		if (i > 0) {
			convert_set_prev_first(&dst, height);
			ref += LEFTMARGIN;
			cur += LEFTMARGIN;
		}

#define DO_PREDICTION() do { \
	int c_x = av_clip(x, -i, width - i - 1); \
	int c_y = av_clip(y, -j, height - j - 1); \
	const u8 *ref_est = ref++ + c_x * PADDED_HEIGHT(height) + c_y; \
	*dst++ = (u8)((u8)(*cur++) - (u8)(*ref_est) + (128 >> (8 - bpp))) & ((1 << bpp) - 1); \
} while (0)

		if (interpolate) {
			int i_off = (i - x_off) & block_size_mask;
			if (i_off == 0) {
				if (i < width - x_off - 1) {
					me_x_col_vec[CORNER_BOT_LEFT] += block_pitch;
					me_x_col_vec[CORNER_BOT_RIGHT] += block_pitch;

					me_y_col_vec[CORNER_BOT_LEFT] += block_pitch;
					me_y_col_vec[CORNER_BOT_RIGHT] += block_pitch;
				}
				if (i > x_off) {
					me_x_col_vec[CORNER_TOP_LEFT] += block_pitch;
					me_x_col_vec[CORNER_TOP_RIGHT] += block_pitch;

					me_y_col_vec[CORNER_TOP_LEFT] += block_pitch;
					me_y_col_vec[CORNER_TOP_RIGHT] += block_pitch;
				}
			}

			const s8 *me_x_vec[CORNER_COUNT];
			const s8 *me_y_vec[CORNER_COUNT];
			memcpy(me_x_vec, me_x_col_vec, sizeof(me_x_vec));
			memcpy(me_y_vec, me_y_col_vec, sizeof(me_y_vec));
			for (int j = 0; j < height; ++j) {
				int j_off = (j - y_off) & block_size_mask;
				if (j_off == 0) {
					if (j < height - y_off - 1) {
						++me_x_vec[CORNER_BOT_LEFT];
						++me_x_vec[CORNER_BOT_RIGHT];

						++me_y_vec[CORNER_BOT_LEFT];
						++me_y_vec[CORNER_BOT_RIGHT];
					}
					if (j > y_off) {
						++me_x_vec[CORNER_TOP_LEFT];
						++me_x_vec[CORNER_TOP_RIGHT];

						++me_y_vec[CORNER_TOP_LEFT];
						++me_y_vec[CORNER_TOP_RIGHT];
					}
				}
				s8 x, y;
				interpolate_me(me_x_vec, me_y_vec, scale_log2, block_size, block_size_log2, i_off, j_off, &x, &y);

				// do prediction
				DO_PREDICTION();
			}
		} else {
			int i_off = (i - x_off) & block_size_mask;
			if (i > x_off && i_off == 0 && i < width - x_off - 1) {
				me_x_col += block_pitch;
				me_y_col += block_pitch;
			}

			const s8 *me_x = me_x_col;
			const s8 *me_y = me_y_col;

			int scene_change = 0;
			int x = 0, y = 0;

			if (select)
				scene_change = *me_x == -(s8)half_range && *me_y == -(s8)half_range;

			if (!scene_change) {
				x = (int)*me_x << scale_log2;
				y = (int)*me_y << scale_log2;
			}

			for (int j = 0; j < height; ++j) {
				int j_off = (j - y_off) & block_size_mask;
				if (j > y_off && j_off == 0 && j < height - y_off - 1) {
					++me_x;
					++me_y;

					if (select)
						scene_change = *me_x == -(s8)half_range && *me_y == -(s8)half_range;

					if (!scene_change) {
						x = (int)*me_x << scale_log2;
						y = (int)*me_y << scale_log2;
					}
				}

				if (scene_change) {
					ref++;
					*dst++ = *cur++;
				} else {
					// do prediction
					DO_PREDICTION();
				}
			}
		}

		convert_set_last(&dst);
		ref += RIGHTMARGIN;
		cur += RIGHTMARGIN;
	}
}
