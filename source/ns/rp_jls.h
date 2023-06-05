#ifndef RP_JLS_H
#define RP_JLS_H

#include "rp_common.h"

enum {
	RP_ENCODE_PARAMS_BPP8,
	RP_ENCODE_PARAMS_BPP7,
	RP_ENCODE_PARAMS_BPP6,
	RP_ENCODE_PARAMS_BPP5,
	RP_ENCODE_PARAMS_BPP4,
	RP_ENCODE_PARAMS_COUNT
};

struct rp_jls_params_t {
	struct {
		uint16_t vLUT_bpp8[2 * (1 << 8)][3];
		uint16_t vLUT_bpp7[2 * (1 << 7)][3];
		uint16_t vLUT_bpp6[2 * (1 << 6)][3];
		uint16_t vLUT_bpp5[2 * (1 << 5)][3];
		uint16_t vLUT_bpp4[2 * (1 << 4)][3];
		int16_t classmap[9 * 9 * 9];
	} enc_luts;
	struct jls_enc_params enc_params[RP_ENCODE_PARAMS_COUNT];
};

struct rp_jls_ctx_t {
	struct jls_enc_ctx enc;
	struct bito_ctx bito;
#if RP_ENCODE_VERIFY
	struct {
		u8 encode[RP_JLS_ENCODE_IMAGE_BUFFER_SIZE] ALIGN_4;
		u8 decode[SCREEN_WIDTH_MAX * SCREEN_HEIGHT] ALIGN_4;
		u8 decode_padded[SCREEN_SIZE_MAX] ALIGN_4;
	} verify_buffer;
#endif
};

void jls_encoder_prepare_LUTs(struct rp_jls_params_t *params);
int ffmpeg_jls_decode(uint8_t *dst, int width, int height, int pitch, const uint8_t *src, int src_size, int bpp);
int rpJLSEncodeImage(struct rp_jls_params_t *params, struct rp_jls_ctx_t *jls_ctx, u8 *dst, int dst_size, const u8 *src, int w, int h, int bpp, int encoder_which, int encode_verify);

#endif
