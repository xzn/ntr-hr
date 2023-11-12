#include "rp_color_aux.h"
#include "rp_color.h"

static JLONG *ctab;

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
	u8 bpp, u8 bpp_2, u8 spp_lq, u8 spp_2_lq, u8 bpp_lq, u8 bpp_2_lq, u8 bias, u8 bias_2, int yuv_option, int color_transform_hp, int lq
) {
	if (yuv_option == 2 || yuv_option == 3) {
		u8 spp = 8 - bpp;
		u8 spp_2 = 8 - bpp_2;
		if (spp) {
			r <<= spp;
			g <<= spp_2;
			b <<= spp;
		}
	}

	u16 y = 0;
	s16 u = 0;
	s16 v = 0;

	switch (yuv_option) {
		case 1:
			if (lq == 1 || lq == 2) {
				convert_yuv_hp_2(r >> spp_lq, g >> spp_2_lq, b >> spp_lq, y_out, u_out, v_out, bpp_lq, color_transform_hp);
			} else if (lq == 3) {
				convert_yuv_hp(r >> spp_lq, g >> spp_2_lq, b >> spp_lq, y_out, u_out, v_out, bpp_lq, color_transform_hp);
			} else if (bpp_2 > bpp) {
				convert_yuv_hp_2(r, g, b, y_out, u_out, v_out, bpp, color_transform_hp);
			} else {
				convert_yuv_hp(r, g, b, y_out, u_out, v_out, bpp, color_transform_hp);
			}
			break;

		case 2: {
			if (lq == 0) {
#if 0
				y = 77 * (u16)r + 150 * (u16)g + 29 * (u16)b;
				u = -43 * (s16)r + -84 * (s16)g + 127 * (s16)b;
				v = 127 * (s16)r + -106 * (s16)g + -21 * (s16)b;
#else
				*y_out = (JSAMPLE)((ctab[r + R_Y_OFF] + ctab[g + G_Y_OFF] +
				                    ctab[b + B_Y_OFF]) >> SCALEBITS);
				*u_out = (JSAMPLE)((ctab[r + R_CB_OFF] + ctab[g + G_CB_OFF] +
				                    ctab[b + B_CB_OFF]) >> SCALEBITS);
				*v_out = (JSAMPLE)((ctab[r + R_CR_OFF] + ctab[g + G_CR_OFF] +
				                    ctab[b + B_CR_OFF]) >> SCALEBITS);
				return;
#endif
			} else {
			}
			break;
		}

		case 3: {
			if (lq == 0) {
				y = 66 * (u16)r + 129 * (u16)g + 25 * (u16)b;
				u = -38 * (s16)r + -74 * (s16)g + 112 * (s16)b;
				v = 112 * (s16)r + -94 * (s16)g + -18 * (s16)b;
			} else {
			}
			break;
		}

		default:
			*y_out = g >> spp_2_lq;
			*u_out = r >> spp_lq;
			*v_out = b >> spp_lq;
			return;
	};

	if (yuv_option == 2 || yuv_option == 3) {
		*y_out = ((y + bias_2) >> bpp_2_lq) >> (8 - bpp_2_lq);
		*u_out = srshift((u + bias) >> bpp_lq, 8 - bpp_lq);
		*v_out = srshift((v + bias) >> bpp_lq, 8 - bpp_lq);
	}
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

static RP_ALWAYS_INLINE int convert_yuv_image_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	if (yuv_option == 2 || yuv_option == 3) {
		if (lq)
			return -1;
	}

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

	u8 bpp, bpp_2;

	switch (format) {
		case 0:
		case 1:
			bpp = bpp_2 = 8; break;

		case 2:
			bpp = 5; bpp_2 = 6; break;

		case 3:
			bpp = 5; bpp_2 = 5; break;

		case 4:
			bpp = 4; bpp_2 = 4; break;

		default:
			return -1;
	}

	u8 spp_lq = 0;
	u8 spp_2_lq = 0;

	u8 bpp_lq = 8;
	u8 bpp_2_lq = 8;

	// LQ1: RGB565
	// LQ2: RGB454
	// LQ3: RGB444
	if (lq) {
		if (lq == 1)
			bpp_lq = 5;
		else
			bpp_lq = 4;

		if (lq == 1 || lq == 2)
			bpp_2_lq = bpp_lq + 1;
		else
			bpp_2_lq = bpp_lq;

		spp_lq = bpp - bpp_lq;
		spp_2_lq = bpp_2 - bpp_2_lq;
	}

	u8 bias_col = (1 << (bpp_lq - 1)) - 1;
	u8 bias_xor = (1 << bpp_lq) - 1;
	u8 bias_2_col = (1 << (bpp_2_lq - 1)) - 1;
	u8 bias_2_xor = (1 << bpp_2_lq) - 1;

	convert_set_3_zero(&dp_y_out, &dp_u_out, &dp_v_out);
	int x, y;

	int hq = 0;
	if (lq == 1) {
		*y_bpp = 6;
		*u_bpp = *v_bpp = 5;
	} else if (lq == 2) {
		*y_bpp = 5;
		*u_bpp = *v_bpp = 4;
	} else if (lq == 3) {
		*y_bpp = *u_bpp = *v_bpp = 4;
	} else {
		hq = 1;
	}

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
				u8 bias = bias_col;
				u8 bias_2 = bias_2_col;
				for (y = 0; y < height; ++y) {
					convert_yuv(sp[2], sp[1], sp[0], dp_y_out++, dp_u_out++, dp_v_out++,
						bpp, bpp_2, spp_lq, spp_2_lq, bpp_lq, bpp_2_lq, bias, bias_2,
						yuv_option, color_transform_hp, lq
					);
					sp += bytes_per_pixel;
					bias ^= bias_xor;
					bias_2 ^= bias_2_xor;
				}
				sp += bytes_to_next_column;
				bias_col ^= bias_xor;
				bias_2_col ^= bias_2_xor;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out);
			}
			if (hq) {
				*y_bpp = *u_bpp = *v_bpp = 8;
			}
			break;
		}

		case 2: {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(&dp_y_out, &dp_u_out, &dp_v_out, height);
				u8 bias = bias_col;
				u8 bias_2 = bias_2_col;
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f, (pix >> 5) & 0x3f, pix & 0x1f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						bpp, bpp_2, spp_lq, spp_2_lq, bpp_lq, bpp_2_lq, bias, bias_2,
						yuv_option, color_transform_hp, lq
					);
					sp += bytes_per_pixel;
					bias ^= bias_xor;
					bias_2 ^= bias_2_xor;
				}
				sp += bytes_to_next_column;
				bias_col ^= bias_xor;
				bias_2_col ^= bias_2_xor;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out);
			}
			if (hq) {
				if (yuv_option == 2 || yuv_option == 3) {
					*y_bpp = *u_bpp = *v_bpp = 8;
				} else {
					*y_bpp = 6;
					*u_bpp = *v_bpp = 5;
				}
			}
			break;
		}

		// untested
		case 3:
		if (0) {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(&dp_y_out, &dp_u_out, &dp_v_out, height);
				u8 bias = bias_col;
				u8 bias_2 = bias_2_col;
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f, (pix >> 6) & 0x1f, (pix >> 1) & 0x1f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						bpp, bpp_2, spp_lq, spp_2_lq, bpp_lq, bpp_2_lq, bias, bias_2,
						yuv_option, color_transform_hp, 0
					);
					sp += bytes_per_pixel;
					bias ^= bias_xor;
					bias_2 ^= bias_2_xor;
				}
				sp += bytes_to_next_column;
				bias_col ^= bias_xor;
				bias_2_col ^= bias_2_xor;
				convert_set_3_last(&dp_y_out, &dp_u_out, &dp_v_out);
			}
			*y_bpp = *u_bpp = *v_bpp = 5;
			break;
		} FALLTHRU

		// untested
		case 4:
		if (0) {
			for (x = 0; x < width; x++) {
				if (x > 0)
					convert_set_3_prev_first(&dp_y_out, &dp_u_out, &dp_v_out, height);
				u8 bias = bias_col;
				u8 bias_2 = bias_2_col;
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 12) & 0x0f, (pix >> 8) & 0x0f, (pix >> 4) & 0x0f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						bpp, bpp_2, spp_lq, spp_2_lq, bpp_lq, bpp_2_lq, bias, bias_2,
                        yuv_option, color_transform_hp, 0
					);
					sp += bytes_per_pixel;
					bias ^= bias_xor;
					bias_2 ^= bias_2_xor;
				}
				sp += bytes_to_next_column;
				bias_col ^= bias_xor;
				bias_2_col ^= bias_2_xor;
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

static NO_INLINE int convert_yuv_image_ng(
	int format UNUSED, int width UNUSED, int height UNUSED, int pitch UNUSED,
	const u8 *restrict sp UNUSED, u8 *restrict dp_y_out UNUSED, u8 *restrict dp_u_out UNUSED, u8 *restrict dp_v_out UNUSED,
	u8 *y_bpp UNUSED, u8 *u_bpp UNUSED, u8 *v_bpp UNUSED, int yuv_option UNUSED, int color_transform_hp UNUSED, int lq UNUSED
) {
#if 0
	return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);
#else
	return -1;
#endif
}

static RP_ALWAYS_INLINE int convert_yuv_image_color_transform_hp_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (color_transform_hp) {
		case 3:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 3, lq);

#if RP_FULL_INLINE_CODE_OPT
		default:
			return -1;

		case 0:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 0, lq);

		case 1:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 1, lq);

		case 2:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 2, lq);
#else
		default:
			return convert_yuv_image_ng(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);
#endif
	}
}

static RP_ALWAYS_INLINE int convert_yuv_image_yuv_option_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (yuv_option) {
		case 0:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 0, color_transform_hp, lq);

		case 1:
			return convert_yuv_image_color_transform_hp_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 1, color_transform_hp, lq);

		case 2:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 2, color_transform_hp, lq);

#if RP_FULL_INLINE_CODE_OPT
		default:
			return -1;

		case 3:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 3, color_transform_hp, lq);
#else
		default:
			return convert_yuv_image_ng(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);
#endif
	}
}

static RP_ALWAYS_INLINE int convert_yuv_image_lq_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (lq) {
		case 0:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 0);

#if RP_FULL_INLINE_CODE_OPT
		default:
			return -1;

		case 1:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 1);

		case 2:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 2);

		case 3:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 3);
#else
		default:
			return convert_yuv_image_ng(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);
#endif
	}
}

static RP_ALWAYS_INLINE int convert_yuv_image_format_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (format) {
		case 1:
			return convert_yuv_image_lq_g(1, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);

		case 2:
			return convert_yuv_image_lq_g(2, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);

#if RP_FULL_INLINE_CODE_OPT
		default:
			return -1;

		case 0:
			return convert_yuv_image_lq_g(0, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);

		case 3:
			return convert_yuv_image_yuv_option_g(3, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 0);

		case 4:
			return convert_yuv_image_yuv_option_g(4, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 0);
#else
		default:
			return convert_yuv_image_ng(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);
#endif
	}
}

int convert_yuv_image(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	return convert_yuv_image_format_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);
}

static RP_ALWAYS_INLINE int downscale_image_g(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig, int unsigned_signed) {
	ASSUME_ALIGN_4(ds_dst);
	ASSUME_ALIGN_4(src);

	int pitch = PADDED_HEIGHT(hOrig);
	const u8 *src_end = src + PADDED_SIZE(wOrig, hOrig);

	src += LEFTMARGIN;
	const u8 *src_col0 = src;
	const u8 *src_col1 = src + pitch;
	u8 bias_col = 1;
	while (src_col0 < src_end) {
		if (src_col0 == src) {
			convert_set_zero(&ds_dst);
		} else {
			convert_set_prev_first(&ds_dst, DS_DIM(hOrig, 1));
		}
		const u8 *src_col0_end = src_col0 + hOrig;
		u8 bias = bias_col;
		while (src_col0 < src_col0_end) {
			if (unsigned_signed == 0) {
				u16 p = *src_col0++;
				p += *src_col0++;
				p += *src_col1++;
				p += *src_col1++;

				// *ds_dst++ = rshift_to_even(p, 2);
				p += bias;
				*ds_dst++ = p >> 2;
				bias ^= 3;
			} else {
				s16 p = (s8)*src_col0++;
				p += (s8)*src_col0++;
				p += (s8)*src_col1++;
				p += (s8)*src_col1++;

				// *ds_dst++ = srshift_to_even(p, 2);
				p += bias;
				*ds_dst++ = p >> 2;
				bias ^= 3;
			}
		}
		bias_col ^= 3;
		src_col0 += RIGHTMARGIN + pitch + LEFTMARGIN;
		src_col1 += RIGHTMARGIN + pitch + LEFTMARGIN;
		convert_set_last(&ds_dst);
	}

	return 0;
}

static RP_ALWAYS_INLINE int downscale_image_unsigned_signed_g(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig, int unsigned_signed) {
	switch (unsigned_signed) {
		default:
			return -1;

		case 0:
			return downscale_image_g(ds_dst, src, wOrig, hOrig, 0);

		case 1:
			return downscale_image_g(ds_dst, src, wOrig, hOrig, 1);
	}
}

int NO_INLINE downscale_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig, int unsigned_signed) {
	return downscale_image_unsigned_signed_g(ds_dst, src, wOrig, hOrig, unsigned_signed);
}

static RP_ALWAYS_INLINE int downscale_3_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig, int unsigned_signed) {
	ASSUME_ALIGN_4(ds_dst);
	ASSUME_ALIGN_4(src);

	if (hOrig / 3 * 3 != hOrig)
		return -1;

	int pitch = PADDED_HEIGHT(hOrig);
	const u8 *src_end = src + PADDED_SIZE(wOrig, hOrig);

	src += LEFTMARGIN;
	const u8 *src_col0 = src;
	const u8 *src_col1 = src_col0 + pitch;
	const u8 *src_col2 = src_col1 + pitch;
	while (src_col0 < src_end) {
		if (src_col0 == src) {
			convert_set_zero(&ds_dst);
		} else {
			convert_set_prev_first(&ds_dst, hOrig / 3);
		}
		const u8 *src_col0_end = src_col0 + hOrig;
		while (src_col0 < src_col0_end) {
			if (unsigned_signed == 0) {
				u16 p = *src_col0++;
				p += *src_col0++;
				p += *src_col0++;

				p += *src_col1++;
				p += *src_col1++;
				p += *src_col1++;

				p += *src_col2++;
				p += *src_col2++;
				p += *src_col2++;

				// *ds_dst++ = (u8)roundf((float)p * (1.0f / 9.0f));
				*ds_dst++ = (p + 4) / 9;
			} else {
				s16 p = (s8)*src_col0++;
				p += (s8)*src_col0++;
				p += (s8)*src_col0++;

				p += (s8)*src_col1++;
				p += (s8)*src_col1++;
				p += (s8)*src_col1++;

				p += (s8)*src_col2++;
				p += (s8)*src_col2++;
				p += (s8)*src_col2++;

				// *ds_dst++ = (s8)roundf((float)p * (1.0f / 9.0f));
				*ds_dst++ = (p + 4) / 9;
			}
		}
		src_col0 += RIGHTMARGIN + pitch * (3 - 1) + LEFTMARGIN;
		src_col1 = RP_MIN(src_col1 + RIGHTMARGIN + pitch * (3 - 1) + LEFTMARGIN, src_end - RIGHTMARGIN - pitch);
		src_col2 = RP_MIN(src_col2 + RIGHTMARGIN + pitch * (3 - 1) + LEFTMARGIN, src_end - RIGHTMARGIN - pitch);
		convert_set_last(&ds_dst);
	}

	return 0;
}

static RP_ALWAYS_INLINE int downscale_4_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig, int unsigned_signed) {
	ASSUME_ALIGN_4(ds_dst);
	ASSUME_ALIGN_4(src);

	int pitch = PADDED_HEIGHT(hOrig);
	const u8 *src_end = src + PADDED_SIZE(wOrig, hOrig);

	src += LEFTMARGIN;
	const u8 *src_col0 = src;
	const u8 *src_col1 = src_col0 + pitch;
	const u8 *src_col2 = src_col1 + pitch;
	const u8 *src_col3 = src_col2 + pitch;
	u8 bias_col = 7;
	while (src_col0 < src_end) {
		if (src_col0 == src) {
			convert_set_zero(&ds_dst);
		} else {
			convert_set_prev_first(&ds_dst, DSX_DIM(hOrig, 4));
		}
		const u8 *src_col0_end = src_col0 + hOrig;
		u8 bias = bias_col;
		while (src_col0 < src_col0_end) {
			if (unsigned_signed == 0) {
				u16 p = *src_col0++;
				p += *src_col0++;
				p += *src_col0++;
				p += *src_col0++;

				p += *src_col1++;
				p += *src_col1++;
				p += *src_col1++;
				p += *src_col1++;

				p += *src_col2++;
				p += *src_col2++;
				p += *src_col2++;
				p += *src_col2++;

				p += *src_col3++;
				p += *src_col3++;
				p += *src_col3++;
				p += *src_col3++;

				// *ds_dst++ = rshift_to_even(p, 4);
				p += bias;
				*ds_dst++ = p >> 4;
				bias ^= 15;
			} else {
				s16 p = (s8)*src_col0++;
				p += (s8)*src_col0++;
				p += (s8)*src_col0++;
				p += (s8)*src_col0++;

				p += (s8)*src_col1++;
				p += (s8)*src_col1++;
				p += (s8)*src_col1++;
				p += (s8)*src_col1++;

				p += (s8)*src_col2++;
				p += (s8)*src_col2++;
				p += (s8)*src_col2++;
				p += (s8)*src_col2++;

				p += (s8)*src_col3++;
				p += (s8)*src_col3++;
				p += (s8)*src_col3++;
				p += (s8)*src_col3++;

				// *ds_dst++ = srshift_to_even(p, 4);
				p += bias;
				*ds_dst++ = p >> 4;
				bias ^= 15;
			}
		}
		bias_col ^= 15;
		src_col0 += RIGHTMARGIN + pitch * (4 - 1) + LEFTMARGIN;
#if 1
		src_col1 += RIGHTMARGIN + pitch * (4 - 1) + LEFTMARGIN;
		src_col2 += RIGHTMARGIN + pitch * (4 - 1) + LEFTMARGIN;
		src_col3 += RIGHTMARGIN + pitch * (4 - 1) + LEFTMARGIN;
#else
		src_col1 = RP_MIN(src_col1 + RIGHTMARGIN + pitch * (4 - 1) + LEFTMARGIN, src_end - RIGHTMARGIN - pitch);
		src_col2 = RP_MIN(src_col2 + RIGHTMARGIN + pitch * (4 - 1) + LEFTMARGIN, src_end - RIGHTMARGIN - pitch);
		src_col3 = RP_MIN(src_col3 + RIGHTMARGIN + pitch * (4 - 1) + LEFTMARGIN, src_end - RIGHTMARGIN - pitch);
#endif
		convert_set_last(&ds_dst);
	}

	return 0;
}

int downscale_x_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig, int dsx, int unsigned_signed) {
	switch (dsx) {
		default:
			return -1;

		case 2:
			return downscale_image(ds_dst, src, wOrig, hOrig, unsigned_signed);

		case 3:
			if (unsigned_signed == 0) {
				return downscale_3_image(ds_dst, src, wOrig, hOrig, 0);
			} else {
				return downscale_3_image(ds_dst, src, wOrig, hOrig, 1);
			}

		case 4:
			if (unsigned_signed == 0) {
				return downscale_4_image(ds_dst, src, wOrig, hOrig, 0);
			} else {
				return downscale_4_image(ds_dst, src, wOrig, hOrig, 1);
			}
	}
}

void rgb_ycc_start(JLONG rgb_ycc_tab[TABLE_SIZE]) {
	ctab = rgb_ycc_tab;

	for (int i = 0; i <= MAXJSAMPLE; ++i) {
		rgb_ycc_tab[i + R_Y_OFF] = FIX(0.29900) * i;
		rgb_ycc_tab[i + G_Y_OFF] = FIX(0.58700) * i;
		rgb_ycc_tab[i + B_Y_OFF] = FIX(0.11400) * i   + ONE_HALF;
		rgb_ycc_tab[i + R_CB_OFF] = (-FIX(0.16874)) * i;
		rgb_ycc_tab[i + G_CB_OFF] = (-FIX(0.33126)) * i;
		/* We use a rounding fudge-factor of 0.5-epsilon for Cb and Cr.
		 * This ensures that the maximum output will round to _MAXJSAMPLE
		 * not _MAXJSAMPLE+1, and thus that we don't have to range-limit.
		 */
		rgb_ycc_tab[i + B_CB_OFF] = FIX(0.50000) * i  + CBCR_OFFSET + ONE_HALF - 1;
		/* B=>Cb and R=>Cr tables are the same
		 * rgb_ycc_tab[i + R_CR_OFF] = FIX(0.50000) * i  + CBCR_OFFSET + ONE_HALF - 1;
		 */
		rgb_ycc_tab[i + G_CR_OFF] = (-FIX(0.41869)) * i;
		rgb_ycc_tab[i + B_CR_OFF] = (-FIX(0.08131)) * i;
	}
}
