#include "rp_color_aux.h"
#include "rp_color.h"

static ALWAYS_INLINE
void convert_yuv_hp(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	int bpp, int color_transform_hp
) {
	u8 half_range = 1 << (bpp - 1);
	u8 bpp_mask = (1 << bpp) - 1;
	switch (color_transform_hp) {
		case 1:
			*y_out = g;
			*u_out = (r - g + half_range) & bpp_mask;
			*v_out = (b - g + half_range) & bpp_mask;
			break;

		case 2:
			*y_out = g;
			*u_out = (r - g + half_range) & bpp_mask;
			*v_out = (b - (((s16)r + g) >> 1) - half_range) & bpp_mask;
			break;

		case 3: {
			u8 quarter_range = 1 << (bpp - 2);
			u8 u = (r - g + half_range) & bpp_mask;
			u8 v = (b - g + half_range) & bpp_mask;

			*y_out = ((s16)g + ((u + v) >> 2) - quarter_range) & bpp_mask;
			*u_out = u;
			*v_out = v;
			break;
		}

		default:
			*y_out = g;
			*u_out = r;
			*v_out = b;
			break;
	}
}

static ALWAYS_INLINE
void convert_yuv_hp_2(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	int bpp, int color_transform_hp
) {
	u8 half_range = 1 << (bpp - 1);
	u8 half_g = g >> 1;
	u8 bpp_mask = (1 << bpp) - 1;
	u8 bpp_2_mask = (1 << (bpp + 1)) - 1;
	switch (color_transform_hp) {
		case 1:
			*y_out = g;
			*u_out = (r - half_g + half_range) & bpp_mask;
			*v_out = (b - half_g + half_range) & bpp_mask;
			break;

		case 2:
			*y_out = g;
			*u_out = (r - half_g + half_range) & bpp_mask;
			*v_out = (b - (((s16)r + half_g) >> 1) - half_range) & bpp_mask;
			break;

		case 3: {
			u8 u = (r - half_g + half_range) & bpp_mask;
			u8 v = (b - half_g + half_range) & bpp_mask;

			*y_out = ((s16)g + ((u + v) >> 1) - half_range) & bpp_2_mask;
			*u_out = u;
			*v_out = v;
			break;
		}

		default:
			*y_out = g;
			*u_out = r;
			*v_out = b;
			break;
	}
}

static ALWAYS_INLINE
void convert_yuv(u8 r, u8 g, u8 b, u8 *restrict y_out, u8 *restrict u_out, u8 *restrict v_out,
	u8 bpp, u8 bpp_2, int yuv_option, int color_transform_hp, int lq
) {
	bpp_2 = bpp_2 ? 1 : 0;

	u8 spp_lq = 0;
	u8 spp_2_lq = 0;

	// LQ: RGB454
	if (lq) {
		spp_lq = bpp - 4;
		spp_2_lq = bpp - 4 - (bpp_2 ? 0 : 1);
	}

	switch (yuv_option) {
		case 1:
			if (bpp_2 || lq) {
				convert_yuv_hp_2(r >> spp_lq, g >> spp_2_lq, b >> spp_lq, y_out, u_out, v_out, lq ? 4 : bpp, color_transform_hp);
			} else {
				convert_yuv_hp(r, g, b, y_out, u_out, v_out, bpp, color_transform_hp);
			}
			break;

#define RP_RGB_SHIFT \
	u8 spp = 8 - bpp; \
	u8 spp_2 = 8 - bpp - bpp_2; \
	u8 bpp_mask = (1 << bpp) - 1; \
	u8 UNUSED bpp_2_mask = (1 << (bpp + bpp_2)) - 1; \
	if (spp) { \
		r <<= spp; \
		g <<= spp_2; \
		b <<= spp; \
	} \
	spp += spp_lq; \
	spp_2 += spp_2_lq;

		case 2: {
			RP_RGB_SHIFT
			u16 y = 77 * (u16)r + 150 * (u16)g + 29 * (u16)b;
			s16 u = -43 * (s16)r + -84 * (s16)g + 127 * (s16)b;
			s16 v = 127 * (s16)r + -106 * (s16)g + -21 * (s16)b;
			*y_out = rshift_to_even(y, 8 + spp_2);
			*u_out = (u8)((u8)srshift_to_even(s16, u, 8 + spp) + (128 >> spp)) & bpp_mask;
			*v_out = (u8)((u8)srshift_to_even(s16, v, 8 + spp) + (128 >> spp)) & bpp_mask;
			break;
		}

		case 3: {
			RP_RGB_SHIFT
			u16 y = 66 * (u16)r + 129 * (u16)g + 25 * (u16)b;
			s16 u = -38 * (s16)r + -74 * (s16)g + 112 * (s16)b;
			s16 v = 112 * (s16)r + -94 * (s16)g + -18 * (s16)b;
			*y_out = (u8)((u8)rshift_to_even(y, 8 + spp_2) + (16 >> spp_2));
			*u_out = (u8)((u8)srshift_to_even(s16, u, 8 + spp) + (128 >> spp)) & bpp_mask;
			*v_out = (u8)((u8)srshift_to_even(s16, v, 8 + spp) + (128 >> spp)) & bpp_mask;
			break;
		}

#undef RP_RGB_SHIFT

		default:
			*y_out = g >> spp_2_lq;
			*u_out = r >> spp_lq;
			*v_out = b >> spp_lq;
			break;
	};
}

int convert_rgb_image(int format, int width, int height, int pitch, const u8 *restrict sp, u8 *restrict dp_rgb_out, u8 *bpp) {
	int bytes_per_pixel;
	if (format == 0) {
		bytes_per_pixel = 4;
	} else if (format == 1) {
		bytes_per_pixel = 3;
	} else {
		bytes_per_pixel = 2;
	}
	int bytes_per_column = bytes_per_pixel * height;
	int bytes_to_next_column = pitch - bytes_per_column;

	ASSUME_ALIGN_4(sp);
	ASSUME_ALIGN_4(dp_rgb_out);

	int x, y;

	switch (format) {
		// untested
		case 0:
			if (0)
				++sp;
			else
				return -1;

			FALLTHRU
		case 1: {
			for (x = 0; x < width; ++x) {
				for (y = 0; y < height; ++y) {
					*dp_rgb_out++ = sp[2];
					*dp_rgb_out++ = sp[1];
					*dp_rgb_out++ = sp[0];
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			if (bpp)
				*bpp = 8;
			break;
		}

		case 2: {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					if (bpp) {
						*dp_rgb_out++ = ((pix >> 11) & 0x1f) << 1;
						*dp_rgb_out++ = (pix >> 5) & 0x3f;
						*dp_rgb_out++ = (pix & 0x1f) << 1;
					} else {
						*dp_rgb_out++ = ((pix >> 11) & 0x1f) << 3;
						*dp_rgb_out++ = ((pix >> 5) & 0x3f) << 2;
						*dp_rgb_out++ = (pix & 0x1f) << 3;
					}
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			if (bpp)
				*bpp = 6;
			break;
		}

		// untested
		case 3:
		if (0) {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					if (bpp) {
						*dp_rgb_out++ = (pix >> 11) & 0x1f;
						*dp_rgb_out++ = (pix >> 6) & 0x1f;
						*dp_rgb_out++ = (pix >> 1) & 0x1f;
					} else {
						*dp_rgb_out++ = ((pix >> 11) & 0x1f) << 3;
						*dp_rgb_out++ = ((pix >> 6) & 0x1f) << 3;
						*dp_rgb_out++ = ((pix >> 1) & 0x1f) << 3;
					}
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			if (bpp)
				*bpp = 5;
			break;
		} FALLTHRU

		// untested
		case 4:
		if (0) {
			for (x = 0; x < width; x++) {
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					if (bpp) {
						*dp_rgb_out++ = (pix >> 12) & 0x0f;
						*dp_rgb_out++ = (pix >> 8) & 0x0f;
						*dp_rgb_out++ = (pix >> 4) & 0x0f;
					} else {
						*dp_rgb_out++ = ((pix >> 12) & 0x0f) << 4;
						*dp_rgb_out++ = ((pix >> 8) & 0x0f) << 4;
						*dp_rgb_out++ = ((pix >> 4) & 0x0f) << 4;
					}
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
			}
			if (bpp)
				*bpp = 4;
			break;
		} FALLTHRU

		default:
			return -1;
	}
	return 0;
}

int convert_yuv_image(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	int bytes_per_pixel;
	if (format == 0) {
		bytes_per_pixel = 4;
	} else if (format == 1) {
		bytes_per_pixel = 3;
	} else {
		bytes_per_pixel = 2;
	}
	int bytes_per_column = bytes_per_pixel * height;
	int bytes_to_next_column = pitch - bytes_per_column;

	ASSUME_ALIGN_4(sp);
	ASSUME_ALIGN_4(dp_y_out);
	ASSUME_ALIGN_4(dp_u_out);
	ASSUME_ALIGN_4(dp_v_out);

	convert_set_3_zero(&dp_y_out, &dp_u_out, &dp_v_out);
	int x, y;

	switch (format) {
		// untested
		case 0:
			if (0)
				++sp;
			else
				return -1;

			FALLTHRU
		case 1: {
			for (x = 0; x < width; ++x) {
				if (x > 0)
					convert_set_3_prev_first(&dp_y_out, &dp_u_out, &dp_v_out, height);
				for (y = 0; y < height; ++y) {
					convert_yuv(sp[2], sp[1], sp[0], dp_y_out++, dp_u_out++, dp_v_out++,
						8, 0,
						yuv_option, color_transform_hp, lq
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out);
			}
			if (lq) {
				*y_bpp = 5;
				*u_bpp = *v_bpp = 4;
			} else {
				*y_bpp = *u_bpp = *v_bpp = 8;
			}
			break;
		}

		case 2: {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(&dp_y_out, &dp_u_out, &dp_v_out, height);
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f, (pix >> 5) & 0x3f, pix & 0x1f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						5, 1,
						yuv_option, color_transform_hp, lq
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out);
			}
			if (lq) {
				*y_bpp = 5;
				*u_bpp = *v_bpp = 4;
			} else {
				*y_bpp = 6;
				*u_bpp = *v_bpp =5;
			}
			break;
		}

		// untested
		case 3:
		if (0) {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(&dp_y_out, &dp_u_out, &dp_v_out, height);
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f, (pix >> 6) & 0x1f, (pix >> 1) & 0x1f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						5, 0,
						yuv_option, color_transform_hp, lq
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out);
			}
			if (lq) {
				*y_bpp = 5;
				*u_bpp = *v_bpp = 4;
			} else {
				*y_bpp = *u_bpp = *v_bpp = 5;
			}
			break;
		} FALLTHRU

		// untested
		case 4:
		if (0) {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(&dp_y_out, &dp_u_out, &dp_v_out, height);
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 12) & 0x0f, (pix >> 8) & 0x0f, (pix >> 4) & 0x0f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						4, 0,
                        yuv_option, color_transform_hp, 0
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out);
			}
			*y_bpp = *u_bpp = *v_bpp = 4;
			break;
		} FALLTHRU

		default:
			return -1;
	}
	return 0;
}

void downscale_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig) {
	ASSUME_ALIGN_4(ds_dst);
	ASSUME_ALIGN_4(src);

	int pitch = PADDED_HEIGHT(hOrig);
	const u8 *src_end = src + PADDED_SIZE(wOrig, hOrig);

	src += LEFTMARGIN;
	const u8 *src_col0 = src;
	const u8 *src_col1 = src + pitch;
	while (src_col0 < src_end) {
		if (src_col0 == src) {
			convert_set_zero(&ds_dst);
		} else {
			convert_set_prev_first(&ds_dst, DS_DIM(hOrig, 1));
		}
		const u8 *src_col0_end = src_col0 + hOrig;
		while (src_col0 < src_col0_end) {
			u16 p = *src_col0++;
			p += *src_col0++;
			p += *src_col1++;
			p += *src_col1++;

			*ds_dst++ = rshift_to_even(p, 2);
		}
		src_col0 += RIGHTMARGIN + pitch + LEFTMARGIN;
		src_col1 += RIGHTMARGIN + pitch + LEFTMARGIN;
		convert_set_last(&ds_dst);
	}
}
