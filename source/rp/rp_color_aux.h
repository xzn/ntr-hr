#ifndef RP_COLOR_AUX_H
#define RP_COLOR_AUX_H

#include "rp_common.h"

static ALWAYS_INLINE
void convert_set_zero_count(u8 *restrict *restrict dp_y_out, int count) {
	for (int i = 0; i < count; ++i) {
		*(*dp_y_out)++ = 0;
	}
}

static ALWAYS_INLINE
void convert_set_3_zero_count(
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out,
	int count
) {
	convert_set_zero_count(dp_y_out, count);
	convert_set_zero_count(dp_u_out, count);
	convert_set_zero_count(dp_v_out, count);
}

static ALWAYS_INLINE
void convert_set_last_count(u8 *restrict *restrict dp_y_out, int count) {
	for (int i = 0; i < count; ++i) {
		**dp_y_out = *(*dp_y_out - 1);
		++*dp_y_out;
	}
}

static ALWAYS_INLINE
void convert_set_3_last_count(
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out,
	int count
) {
	convert_set_last_count(dp_y_out, count);
	convert_set_last_count(dp_u_out, count);
	convert_set_last_count(dp_v_out, count);
}

static ALWAYS_INLINE
void convert_set_prev_first_count(int prev_off, u8 *restrict *restrict dp_y_out, int count) {
	u8 prev_first = *(*dp_y_out - prev_off);
	for (int i = 0; i < count; ++i) {
		*(*dp_y_out)++ = prev_first;
	}
}

static ALWAYS_INLINE
void convert_set_3_prev_first_count(
	int prev_off,
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out,
	int count
) {
	convert_set_prev_first_count(prev_off, dp_y_out, count);
	convert_set_prev_first_count(prev_off, dp_u_out, count);
	convert_set_prev_first_count(prev_off, dp_v_out, count);
}

static ALWAYS_INLINE
void convert_set_zero(u8 *restrict *restrict dp_y_out) {
    convert_set_zero_count(dp_y_out, LEFTMARGIN);
}

static ALWAYS_INLINE
void convert_set_3_zero(
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out
) {
    convert_set_3_zero_count(dp_y_out, dp_u_out, dp_v_out, LEFTMARGIN);
}

static ALWAYS_INLINE
void convert_set_last(u8 *restrict *restrict dp_y_out) {
    convert_set_last_count(dp_y_out, RIGHTMARGIN);
}

static ALWAYS_INLINE
void convert_set_3_last(
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out
) {
    convert_set_3_last_count(dp_y_out, dp_u_out, dp_v_out, RIGHTMARGIN);
}

static ALWAYS_INLINE
void convert_set_prev_first(u8 *restrict *restrict dp_y_out, int pitch) {
    convert_set_prev_first_count(pitch + RIGHTMARGIN, dp_y_out, LEFTMARGIN);
}

static ALWAYS_INLINE
void convert_set_3_prev_first(
	u8 *restrict *restrict dp_y_out,
	u8 *restrict *restrict dp_u_out,
	u8 *restrict *restrict dp_v_out,
	int pitch
) {
    convert_set_3_prev_first_count(pitch + RIGHTMARGIN, dp_y_out, dp_u_out, dp_v_out, LEFTMARGIN);
}

#endif
