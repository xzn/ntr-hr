#ifndef RP_COLOR_H
#define RP_COLOR_H

#include "rp_common.h"

int convert_yuv_image(
	int format, int width, int height, int pitch,
	const u8 *restrict sp, u8 *restrict dp_y_out, u8 *restrict dp_u_out, u8 *restrict dp_v_out,
	u8 *y_bpp, u8 *u_bpp, u8 *v_bpp, int yuv_option, int color_transform_hp
);

void downscale_image(u8 *restrict ds_dst, const u8 *restrict src, int wOrig, int hOrig);

#endif
