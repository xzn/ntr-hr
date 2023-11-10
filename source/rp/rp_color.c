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
	bpp_2 = bpp_2 ? bpp + 1 : bpp;

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
				y = 77 * (u16)r + 150 * (u16)g + 29 * (u16)b;
				u = -43 * (s16)r + -84 * (s16)g + 127 * (s16)b;
				v = 127 * (s16)r + -106 * (s16)g + -21 * (s16)b;
			} else if (lq == 1) {
				y = 19 * (u16)r + 38 * (u16)g + 7 * (u16)b;
				u = -5 * (s16)r + -10 * (s16)g + 15 * (s16)b;
				v = 15 * (s16)r + -13 * (s16)g + -2 * (s16)b;
			} else {
				if (lq == 2) {
					y = 9 * (u16)r + 19 * (u16)g + 4 * (u16)b;
				} else {
					y = 5 * (u16)r + 9 * (u16)g + 2 * (u16)b;
				}
				u = -2 * (s16)r + -5 * (s16)g + 7 * (s16)b;
				v = 7 * (s16)r + -6 * (s16)g + -1 * (s16)b;
			}
			break;
		}

		case 3: {
			if (lq == 0) {
				y = 66 * (u16)r + 129 * (u16)g + 25 * (u16)b;
				u = -38 * (s16)r + -74 * (s16)g + 112 * (s16)b;
				v = 112 * (s16)r + -94 * (s16)g + -18 * (s16)b;
			} else if (lq == 1) {
				y = 17 * (u16)r + 32 * (u16)g + 6 * (u16)b;
				u = -5 * (s16)r + -9 * (s16)g + 14 * (s16)b;
				v = 14 * (s16)r + -12 * (s16)g + -2 * (s16)b;
			} else {
				if (lq == 2) {
					y = 8 * (u16)r + 16 * (u16)g + 3 * (u16)b;
				} else {
					y = 4 * (u16)r + 8 * (u16)g + 1 * (u16)b;
				}
				u = -2 * (s16)r + -5 * (s16)g + 7 * (s16)b;
				v = 7 * (s16)r + -6 * (s16)g + -1 * (s16)b;
			}
			break;
		}

		default:
			*y_out = g >> spp_2_lq;
			*u_out = r >> spp_lq;
			*v_out = b >> spp_lq;
			break;
	};

	if (yuv_option == 2 || yuv_option == 3) {
		*y_out = rshift_to_even(y, bpp_2_lq) >> (8 - bpp_2_lq);
		*u_out = srshift(srshift_to_even(u, bpp_lq), 8 - bpp_lq);
		*v_out = srshift(srshift_to_even(v, bpp_lq), 8 - bpp_lq);
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

static ALWAYS_INLINE int convert_yuv_image_g(
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
			if (hq) {
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
				for (y = 0; y < height; y++) {
					u16 pix = *(u16*)sp;
					convert_yuv(
						(pix >> 11) & 0x1f, (pix >> 6) & 0x1f, (pix >> 1) & 0x1f,
						dp_y_out++, dp_u_out++, dp_v_out++,
						5, 0,
						yuv_option, color_transform_hp, 0
					);
					sp += bytes_per_pixel;
				}
				sp += bytes_to_next_column;
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

static ALWAYS_INLINE int convert_yuv_image_color_transform_hp_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (color_transform_hp) {
		default:
		case 0:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 0, lq);

		case 1:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 1, lq);

		case 2:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 2, lq);

		case 3:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, 3, lq);
	}
}

static ALWAYS_INLINE int convert_yuv_image_yuv_option_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (yuv_option) {
		default:
		case 0:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 0, color_transform_hp, lq);

		case 1:
			return convert_yuv_image_color_transform_hp_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 1, color_transform_hp, lq);

		case 2:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 2, color_transform_hp, lq);

		case 3:
			return convert_yuv_image_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, 3, color_transform_hp, lq);
	}
}

static ALWAYS_INLINE int convert_yuv_image_lq_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (lq) {
		default:
			return -1;

		case 0:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 0);

		case 1:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 1);

		case 2:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 2);

		case 3:
			return convert_yuv_image_yuv_option_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 3);
	}
}

static ALWAYS_INLINE int convert_yuv_image_format_g(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	switch (format) {
		case 0:
			return convert_yuv_image_lq_g(0, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);

		case 1:
			return convert_yuv_image_lq_g(1, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);

		case 2:
			return convert_yuv_image_lq_g(2, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);

		case 3:
			return convert_yuv_image_yuv_option_g(3, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 0);

		case 4:
			return convert_yuv_image_yuv_option_g(4, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, 0);

		default:
			return -1;
	}
}

int convert_yuv_image(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp, int lq
) {
	return convert_yuv_image_format_g(format, width, height, pitch, sp, dp_y_out, dp_u_out, dp_v_out, y_bpp, u_bpp, v_bpp, yuv_option, color_transform_hp, lq);
}

void downscale_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig, int unsigned_signed) {
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
			if (unsigned_signed == 0) {
				u16 p = *src_col0++;
				p += *src_col0++;
				p += *src_col1++;
				p += *src_col1++;

				*ds_dst++ = rshift_to_even(p, 2);
			} else {
				s16 p = (s8)*src_col0++;
				p += (s8)*src_col0++;
				p += (s8)*src_col1++;
				p += (s8)*src_col1++;

				*ds_dst++ = srshift_to_even(p, 2);
			}
		}
		src_col0 += RIGHTMARGIN + pitch + LEFTMARGIN;
		src_col1 += RIGHTMARGIN + pitch + LEFTMARGIN;
		convert_set_last(&ds_dst);
	}
}
