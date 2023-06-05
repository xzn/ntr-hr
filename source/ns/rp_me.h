#ifndef RP_ME_H
#define RP_ME_H

#include "rp_common.h"

void predict_image(u8 *dst, const u8 *ref, const u8 *cur, const s8 *me_x_image, const s8 *me_y_image, int width, int height, int scale_log2, int bpp,
	u8 block_size, u8 block_size_log2, int interpolate
);
void me_add_half_range(u8 *me, int width, int height, u8 scale_log2, u8 half_range, u8 block_size_log2);
void motion_estimate(s8 *me_x_image, s8 *me_y_image, const u8 *ref, const u8 *cur, int width, int height, int pitch,
	u8 block_size, u8 block_size_log2, u8 search_param, u8 me_method
);

#endif
