#ifndef RP_ME_H
#define RP_ME_H

#include "rp_common.h"

void diff_image(s8 *me_x_image, u8 *dst, const u8 *ref, u8 *cur, u8 spp_lq, u8 unsigned_signed,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, int bpp, int scale_log2, u8 block_size, u8 block_size_log2);
void predict_image(u8 *dst, const u8 *ref, const u8 *cur, const s8 *me_x_image, const s8 *me_y_image,
	int width, int height, int scale_log2, int bpp,
	u8 block_size, u8 block_size_log2, int interpolate, u8 select, u8 half_range
);
void me_add_half_range(u8 *me, int width, int height, u8 scale_log2, u8 half_range, u8 block_size_log2);
void motion_estimate(s8 *me_x_image, s8 *me_y_image, const u8 *ref, const u8 *cur,
	u8 select, u16 select_threshold, u16 *mafd, const u16 *mafd_prev, u8 mafd_shift,
	int width, int height, int pitch, u8 bpp,
	u8 block_size, u8 block_size_log2, u8 search_param, u8 me_method, u8 half_range
);
void calc_mafd_image(u16 *mafd, u8 mafd_shift, const u8 *cur, int width, int height, int pitch,
	u8 block_size, u8 block_size_log2, u8 bpp);

#endif
