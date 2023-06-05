#include "rp_jls.h"
#include "rp_color_aux.h"

void jls_encoder_prepare_LUTs(struct rp_jls_params_t *ctx) {
	prepare_classmap(ctx->enc_luts.classmap);
	struct jls_enc_params *p;

#define RP_JLS_INIT_LUT(bpp, bpp_index, bpp_lut_name) do { \
	p = &ctx->enc_params[bpp_index]; \
	jpeg_ls_init(p, bpp, (const uint16_t (*)[3])ctx->enc_luts.bpp_lut_name); \
	prepare_vLUT(ctx->enc_luts.bpp_lut_name, p->alpha, p->T1, p->T2, p->T3); } while (0) \

	RP_JLS_INIT_LUT(8, RP_ENCODE_PARAMS_BPP8, vLUT_bpp8);
	RP_JLS_INIT_LUT(7, RP_ENCODE_PARAMS_BPP7, vLUT_bpp7);
	RP_JLS_INIT_LUT(6, RP_ENCODE_PARAMS_BPP6, vLUT_bpp6);
	RP_JLS_INIT_LUT(5, RP_ENCODE_PARAMS_BPP5, vLUT_bpp5);
	RP_JLS_INIT_LUT(4, RP_ENCODE_PARAMS_BPP4, vLUT_bpp4);

#undef RP_JLS_INIT_LUT
}

void jpeg_ls_encode_pad_source(u8 *dst, int dst_size, const u8 *src, int width, int height) {
	u8 *ret = dst;
	convert_set_zero(&dst);
	for (int y = 0; y < width; ++y) {
		if (y) {
			convert_set_prev_first(&dst, height);
		}
		for (int x = 0; x < height; ++x) {
			*dst++ = *src++;
		}
		convert_set_last(&dst);
	}
	if (dst - ret != PADDED_SIZE(width, height)) {
		nsDbgPrint("Failed pad source size: %d (expected %d)\n",
			dst - ret,
			PADDED_SIZE(width, height)
		);
	}
	if (dst - ret > dst_size) {
		nsDbgPrint("Failed pad source buffer overflow: %d\n", dst - ret);
	}
}

extern const uint8_t psl0[];
int ffmpeg_jls_decode(uint8_t *dst, int width, int height, int pitch, const uint8_t *src, int src_size, int bpp) {
	JLSState state = { 0 };
	state.bpp = bpp;
	ff_jpegls_reset_coding_parameters(&state, 0);
	ff_jpegls_init_state(&state);

	int ret, t;

	GetBitContext s;
	ret = init_get_bits8(&s, src, src_size);
	if (ret < 0) {
		return ret;
	}

	const uint8_t *last;
	uint8_t *cur;
	last = psl0;
	cur = dst;

	int i;
	t = 0;
	for (i = 0; i < width; ++i) {
		ret = ls_decode_line(&state, &s, last, cur, t, height);
		if (ret < 0)
		{
			nsDbgPrint("Failed decode at col %d\n", i);
			return ret;
		}
		t = last[0];
		last = cur;
		cur += pitch;
	}

	return width * height;
}

int rpJLSEncodeImage(struct rp_jls_params_t *params, struct rp_jls_ctx_t *jls_ctx, u8 *dst, int dst_size, const u8 *src, int w, int h, int bpp, int encoder_which, int encode_verify) {
	XXH32_hash_t UNUSED src_hash = 0;
	u8 *dst2 = 0;
	if (RP_ENCODE_VERIFY && encode_verify) {
#if RP_ENCODE_VERIFY
		src_hash = XXH32(src, w * (h + LEFTMARGIN + RIGHTMARGIN), 0);
		dst2 = jls_ctx->verify_buffer.encode;

		if (encoder_which == 1) {
			u8 *tmp = dst;
			dst = dst2;
			dst2 = tmp;
		}
#endif
	} else {
		dst2 = dst;
	}

	struct jls_enc_params *enc_params;
	switch (bpp) {
		case 8:
			enc_params = &params->enc_params[RP_ENCODE_PARAMS_BPP8]; break;

		case 7:
			enc_params = &params->enc_params[RP_ENCODE_PARAMS_BPP7]; break;

		case 6:
			enc_params = &params->enc_params[RP_ENCODE_PARAMS_BPP6]; break;

		case 5:
			enc_params = &params->enc_params[RP_ENCODE_PARAMS_BPP5]; break;

		case 4:
			enc_params = &params->enc_params[RP_ENCODE_PARAMS_BPP4]; break;

		default:
			nsDbgPrint("Unsupported bpp in rpJLSEncodeImage: %d\n", bpp);
			return -1;
	}

	int ret = 0, ret2 = 0;
	if ((RP_ENCODE_VERIFY && encode_verify) || encoder_which == 0) {
		JLSState state = { 0 };
		state.bpp = bpp;

		ff_jpegls_reset_coding_parameters(&state, 0);
		ff_jpegls_init_state(&state);

		PutBitContext s;
		init_put_bits(&s, dst, dst_size);

		const u8 *last = psl0 + LEFTMARGIN;
		const u8 *in = src + LEFTMARGIN;

		for (int i = 0; i < w; ++i) {
			ls_encode_line(
				&state, &s, last, in, h,
				enc_params->vLUT,
				params->enc_luts.classmap
			);
			last = in;
			in += h + LEFTMARGIN + RIGHTMARGIN;
		}

		flush_put_bits(&s);
		ret = put_bytes_output(&s);
	}
	if ((RP_ENCODE_VERIFY && encode_verify) || encoder_which == 1) {
		struct jls_enc_ctx *ctx = &jls_ctx->enc;
		struct bito_ctx *bctx = &jls_ctx->bito;
		ret2 = jpeg_ls_encode(
			enc_params, ctx, bctx, (char *)dst2, dst_size, src,
			w, h, h + LEFTMARGIN + RIGHTMARGIN,
			params->enc_luts.classmap
		);
	}
	if (RP_ENCODE_VERIFY && encode_verify) {
#if RP_ENCODE_VERIFY
		nsDbgPrint("rpJLSEncodeImage: w = %d, h = %d, bpp = %d\n", w, h, bpp);
		if (src_hash != XXH32(src, w * (h + LEFTMARGIN + RIGHTMARGIN), 0))
			nsDbgPrint("rpJLSEncodeImage src buffer corrupt during encode, race condition?\n");
		if (ret != ret2) {
			nsDbgPrint("Failed encode size verify: %d, %d\n", ret, ret2);
		} else if (memcmp(dst, dst2, ret) != 0) {
			nsDbgPrint("Failed encode content verify\n");
		} else {
			u8 *decoded = jls_ctx->verify_buffer.decode;

			int ret3 = ffmpeg_jls_decode(decoded, w, h, h, dst2, ret2, bpp);
			if (ret3 != w * h) {
				nsDbgPrint("Failed decode size verify: %d (expected %d)\n", ret3, w * h);
			} else {
				for (int i = 0; i < w; ++i) {
					if (memcmp(decoded + i * h, src + LEFTMARGIN + i * (h + LEFTMARGIN + RIGHTMARGIN), h) != 0) {
						nsDbgPrint("Failed decode content verify at col %d\n", i);
						break;
					}
				}

				u8 *decode_padded = jls_ctx->verify_buffer.decode_padded;
				int decode_padded_size = sizeof(jls_ctx->verify_buffer.decode_padded);

				jpeg_ls_encode_pad_source(decode_padded, decode_padded_size, decoded, w, h);
				if (memcmp(decoded, src, w * (h + LEFTMARGIN + RIGHTMARGIN)) != 0) {
					nsDbgPrint("Failed decode pad content verify\n");
				}
				for (int i = 0; i < w; ++i) {
					if (memcmp(
							decoded + LEFTMARGIN + i * (h + LEFTMARGIN + RIGHTMARGIN),
							src + LEFTMARGIN + i * (h + LEFTMARGIN + RIGHTMARGIN),
							h
						) != 0
					) {
						nsDbgPrint("Failed decode pad content verify at col %d\n", i);
						break;
					}
				}
			}
		}
#endif
	} else if (encoder_which == 1) {
		ret = ret2;
	}

	if (ret >= dst_size) {
		nsDbgPrint("Possible buffer overrun in rpJLSEncodeImage\n");
		return -1;
	}
	if (RP_ENCODE_VERIFY && encode_verify) {
		return encoder_which == 0 ? ret : ret2;
	} else {
		return ret;
	}
}
