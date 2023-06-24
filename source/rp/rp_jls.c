#include "rp_jls.h"
#include "rp_color_aux.h"
#include "rp_syn_chan.h"
#include "rp_syn.h"
#include "rp_net.h"

#include "../zstd/compress/zstd_compress_internal.h"

void jls_encoder_prepare_LUTs(struct rp_jls_params_t *ctx) {
	memset(ctx, 0, sizeof(*ctx));

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
	RP_JLS_INIT_LUT(1, RP_ENCODE_PARAMS_BPP1, vLUT_bpp1);

#undef RP_JLS_INIT_LUT
}

static void rpJLSSendClear(struct rp_jls_send_ctx_t *ctx) {
	ctx->buffer_begin = ctx->buffer_end = (u8 *)(ctx->send_size_total = 0);
}

static int rpJLSSendBegin(struct rp_jls_send_ctx_t *ctx, u8 init) {
	if (*ctx->exit_thread)
		return -1;

	if (ctx->multicore_network) {
		int acquire_count = 0;
		while (1) {
			if (*ctx->exit_thread)
				return -1;

			ctx->network =
				rp_network_encode_acquire(&ctx->network_queue->encode, RP_THREAD_LOOP_MED_WAIT, ctx->network_sync);
			if (!ctx->network) {
				if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) {
					nsDbgPrint("rp_network_encode_acquire timeout\n");
					*ctx->exit_thread = 1;
					ctx->send_size_total = -1;
					return -1;
				}
				continue;
			}
			break;
		}
	}
	if (!ctx->multicore_network && init)
		memcpy(ctx->network->buffer, ctx->send_header, sizeof(struct rp_send_data_header));
	ctx->buffer_begin = ctx->network->buffer + sizeof(struct rp_send_data_header);
	ctx->buffer_end = ctx->network->buffer + sizeof(ctx->network->buffer);
	return 0;
}

static int rpJLSSendEnd(struct rp_jls_send_ctx_t *ctx, u8 fini) {
	if (*ctx->exit_thread)
		return -1;

	if (ctx->multicore_network || fini)
		memcpy(ctx->network->buffer, ctx->send_header, sizeof(struct rp_send_data_header));

	int ret;
	ctx->send_size_total += ctx->network->size = ctx->buffer_begin - ctx->network->buffer;
	struct rp_send_data_header *send_header = (struct rp_send_data_header *)ctx->network->buffer;
	send_header->data_size = ctx->network->size - sizeof(struct rp_send_data_header);

	if (ctx->multicore_network) {
		if ((ret = rp_network_transfer_release(&ctx->network_queue->transfer, ctx->network, ctx->network_sync))) {
			nsDbgPrint("%d rp_network_transfer_release syn failed\n", ctx->thread_n);
			*ctx->exit_thread = 1;
			ctx->send_size_total = -1;
			return ret;
		}
		ctx->network = 0;
	} else {
		if ((ret = rpKCPSend(ctx->net_state, ctx->network->buffer, ctx->network->size))) {
			nsDbgPrint("%d rpKCPSend failed\n", ctx->thread_n);
			*ctx->exit_thread = 1;
			ctx->send_size_total = -1;
			return ret;
		}
	}
	ctx->buffer_begin = 0;
	ctx->buffer_end = 0;
	return 0;
}

static int rpJLSSendEncodedCallback(struct rp_jls_send_ctx_t *ctx) {
	int ret;
	if ((ret = rpJLSSendEnd(ctx, 0)))
		return ret;
	if ((ret = rpJLSSendBegin(ctx, 0)))
		return ret;
	return 0;
}

static int rpJLSSendEncodedCallback_FFmpeg(struct PutBitContext *ctx) {
	struct rp_jls_send_ctx_t *sctx = ctx->user;
	sctx->buffer_begin = ctx->buf_ptr;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	ctx->buf_ptr = ctx->buf = sctx->buffer_begin;
	ctx->buf_end = sctx->buffer_end;
	return 0;
}

static int rpJLSSendEncodedCallback_JLS(struct bito_ctx *ctx) {
	struct rp_jls_send_ctx_t *sctx = ctx->user;
	sctx->buffer_begin = (u8 *)ctx->buf;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	ctx->buf = sctx->buffer_begin;
	ctx->buf_end = sctx->buffer_end;
	return 0;
}

static int rpJLSSendEncodedCallback_IZ(struct BitCoderPtrs *ctx) {
	struct rp_jls_send_ctx_t *sctx = ctx->user;
	sctx->buffer_begin = (u8 *)ctx->p;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	ctx->p = (Code_def_t *)sctx->buffer_begin;
	ctx->p_end = (Code_def_t *)sctx->buffer_end;
	return 0;
}

static int rpJLSSendEncodedCallback_JT(j_common_ptr ctx) {
	struct rp_jpeg_client_data_t *cctx = (struct rp_jpeg_client_data_t *)ctx->client_data;
	struct rp_jls_send_ctx_t *sctx = cctx->user;
	sctx->buffer_begin = cctx->dst;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	cctx->dst = sctx->buffer_begin;
	cctx->dst_end = sctx->buffer_end;
	return 0;
}

static int rpJLSSendEncodedCallback_ZSTD(struct rp_jls_send_ctx_t *sctx, ZSTD_outBuffer *cctx) {
	sctx->buffer_begin = cctx->dst + cctx->pos;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	cctx->dst = sctx->buffer_begin;
	cctx->size = sctx->buffer_end - sctx->buffer_begin;
	cctx->pos = 0;
	return 0;
}

struct rp_jls_lz4_ctx_t {
	u8 *dst, *dst_end;
};

static int rpJLSSendEncodedCallback_LZ4(struct rp_jls_send_ctx_t *sctx, struct rp_jls_lz4_ctx_t *cctx) {
	sctx->buffer_begin = cctx->dst;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	cctx->dst = sctx->buffer_begin;
	cctx->dst_end = sctx->buffer_end;
	return 0;
}

static uint8_t jls_pred_med(uint8_t Rb, uint8_t Ra, uint8_t Rc) {
	uint8_t minx;
	uint8_t maxx;

	if (Rb > Ra) {
		minx = Ra;
		maxx = Rb;
	} else {
		maxx = Ra;
		minx = Rb;
	}
	if (Rc >= maxx)
		return minx;
	else if (Rc <= minx)
		return maxx;
	else
		return Ra + Rb - Rc;
}

static size_t zstd_compress_stream(struct rp_jls_send_ctx_t *sctx,
	ZSTD_CStream *cstream, ZSTD_outBuffer *cur_output, ZSTD_inBuffer *cur_input, ZSTD_EndDirective endOp
) {
	size_t ret;
	while (1) {
		ret = ZSTD_compressStream2(cstream, cur_output, cur_input, endOp);
		if (cur_output->pos == cur_output->size) {
			if ((ret = rpJLSSendEncodedCallback_ZSTD(sctx, cur_output)))
				return -1;
			continue;
		}
		if (ZSTD_isError(ret)) {
			nsDbgPrint("ZSTD_compressStream2 error: %d\n", ret);
			return ret;
		}
		return 0;
	}
}

static int lz4_write_bytes(struct rp_jls_send_ctx_t *sctx, struct rp_jls_lz4_ctx_t *cctx, const u8 *bytes, int size) {
	int remaining_size = size;
	int ret;
	while (remaining_size) {
		size = RP_MIN(remaining_size, (int)(cctx->dst_end - cctx->dst));
		if (size == 0) {
			if ((ret = rpJLSSendEncodedCallback_LZ4(sctx, cctx)))
				return -1;
			continue;
		}
		memcpy(cctx->dst, bytes, size);
		cctx->dst += size;
		bytes += size;
		remaining_size -= size;
	}
	return 0;
}

extern const uint8_t psl0[];

static int rpJLSEncodeImage(struct rp_jls_send_ctx_t *send_ctx,
	struct rp_jls_params_t *params, struct rp_jls_ctx_t *jls_ctx,
	const u8 *src, const u8 *src_2, int w, int h, int bpp, int bpp_2, u8 encoder_which
) {
	int ret;

	rpJLSSendClear(send_ctx);
	ret = rpJLSSendBegin(send_ctx, 1);
	if (ret < 0)
		return ret;

	struct jls_enc_params *enc_params = 0;
	if (encoder_which < RP_ENCODER_JLS_USE_LUT_COUNT) {
		if (src_2 && bpp != bpp_2)
			return -1;

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

			case 1:
				enc_params = &params->enc_params[RP_ENCODE_PARAMS_BPP1]; break;

			default:
				nsDbgPrint("Unsupported bpp in rpJLSEncodeImage: %d\n", bpp);
				return -1;
		}
	}

	if (RP_ENCODER_FFMPEG_JLS_ENABLE && encoder_which == RP_ENCODER_FFMPEG_JLS) {
		JLSState state = { 0 };
		state.bpp = bpp;

		ff_jpegls_reset_coding_parameters(&state, 0);
		ff_jpegls_init_state(&state);

		PutBitContext s;
		init_put_bits(&s, send_ctx->buffer_begin, send_ctx->buffer_end);
		s.user = send_ctx;

		const u8 *last = psl0 + LEFTMARGIN;
		const u8 *in = src + LEFTMARGIN;

		for (int i = 0; i < w; ++i) {
			ret = ls_encode_line(
				&state, &s, last, in, h,
				enc_params->vLUT,
				params->enc_luts.classmap
			);
			if (ret) {
				nsDbgPrint("ls_encode_line failed: %d\n", ret);
				return -1;
			}
			last = in;
			in += h + LEFTMARGIN + RIGHTMARGIN;
		}

		if ((ret = flush_put_bits(&s))) {
			nsDbgPrint("flush_put_bits failed: %d\n", ret);
			return -1;
		}
		send_ctx->buffer_begin = s.buf_ptr;
	} else if (RP_ENCODER_HP_JLS_ENABLE && encoder_which == RP_ENCODER_HP_JLS) {
		struct jls_enc_ctx *ctx = &jls_ctx->enc;
		struct bito_ctx *bctx = &jls_ctx->bito;
		bctx->user = send_ctx;
		ret = jpeg_ls_encode(
			enc_params, ctx, bctx,
			(char *)send_ctx->buffer_begin, (char *)send_ctx->buffer_end,
			src, w, h, h + LEFTMARGIN + RIGHTMARGIN,
			params->enc_luts.classmap
		);
		if (ret) {
			nsDbgPrint("jpeg_ls_encode failed: %d\n", ret);
			return -1;
		}
		send_ctx->buffer_begin = (u8 *)bctx->buf;
	} else if (RP_ENCODER_LZ4_ENABLE && encoder_which == RP_ENCODER_LZ4_JLS) {
		struct rp_jls_lz4_ctx_t cctx = {
			.dst = send_ctx->buffer_begin,
			.dst_end = send_ctx->buffer_end,
		};
		uint8_t pred_buf_index = 0;

		const u8 *last = psl0 + LEFTMARGIN;
		const u8 *in = src + LEFTMARGIN;

		int ret;

		for (int j = 0; j < w; ++j) {
			u8 *pred_buf = send_ctx->lz4_med_pred_line[pred_buf_index];

			for (int i = 0; i < h; ++i) {
				u8 Ra = in[i - 1], Rb = last[i], Rc = last[i - 1];
				pred_buf[i] = in[i] - jls_pred_med(Rb, Ra, Rc);
			}

			u8 comp_buf[LZ4_COMPRESSBOUND(h)];

			const u8 comp_size = LZ4_compress_fast_continue(
				send_ctx->lz4_med_ws, (const char *)pred_buf, (char *)comp_buf, h, sizeof(comp_buf), 0);

			if (comp_size <= 0) {
				nsDbgPrint("LZ4_compress_fast_continue failed: %d\n", comp_size);
				return -1;
			}

			if ((ret = lz4_write_bytes(send_ctx, &cctx, &comp_size, sizeof(comp_size))))
				return ret;
			if ((ret = lz4_write_bytes(send_ctx, &cctx, comp_buf, comp_size)))
				return ret;

			last = in;
			in += h + LEFTMARGIN + RIGHTMARGIN;

			pred_buf_index = (pred_buf_index + 1) % 2;
		}

		send_ctx->buffer_begin = cctx.dst;
		LZ4_resetStream_fast(send_ctx->lz4_med_ws);

	} else if (RP_ENCODER_ZSTD_ENABLE && encoder_which == RP_ENCODER_ZSTD_JLS) {
		ZSTD_CStream *cstream = (ZSTD_CStream *)send_ctx->zstd_med_ws;
		u8 *pred_buf = send_ctx->zstd_med_pred_line;

		const u8 *last = psl0 + LEFTMARGIN;
		const u8 *in = src + LEFTMARGIN;

		size_t ret;

		ZSTD_outBuffer cur_output = {
			.dst = send_ctx->buffer_begin,
			.size = send_ctx->buffer_end - send_ctx->buffer_begin,
		};

		ZSTD_inBuffer cur_input = {
			.src = pred_buf,
			.size = h,
		};
		for (int j = 0; j < w; ++j) {
			for (int i = 0; i < h; ++i) {
				u8 Ra = in[i - 1], Rb = last[i], Rc = last[i - 1];
				pred_buf[i] = in[i] - jls_pred_med(Rb, Ra, Rc);
			}
			cur_input.pos = 0;
			ret = zstd_compress_stream(send_ctx, cstream, &cur_output, &cur_input, ZSTD_e_continue);
			if (ZSTD_isError(ret)) {
				nsDbgPrint("zstd_compress_stream failed\n");
				return ret;
			}

			last = in;
			in += h + LEFTMARGIN + RIGHTMARGIN;
		}

		cur_input.src = (void *)(cur_input.size = cur_input.pos = 0);
		ret = zstd_compress_stream(send_ctx, cstream, &cur_output, &cur_input, ZSTD_e_end);
        if (ZSTD_isError(ret)) {
            nsDbgPrint("zstd_compress_stream end failed\n");
            return ret;
        }

		send_ctx->buffer_begin = cur_output.dst + cur_output.pos;

		if (ZSTD_isError(ret = ZSTD_CCtx_reset(cstream, ZSTD_reset_session_only))) {
			nsDbgPrint("ZSTD_CCtx_reset failed: %d\n", ret);
			return ret;
		}
	} else if (RP_ENCODER_HUFF_ENABLE && encoder_which == RP_ENCODER_HUFF_JLS) {
		struct rp_rle_encode_ctx_t rle_ctx;
		rle_encode_init(&rle_ctx, send_ctx->huff_med_pred_image);

		const u8 *in_2[] = {src, src_2};

		for (int k = 0; k < (int)(sizeof(in_2) / sizeof(*in_2)) && in_2[k]; ++k) {
			const u8 *last = psl0 + LEFTMARGIN;
			const u8 *in = in_2[k] + LEFTMARGIN;

			for (int j = 0; j < w; ++j) {
				for (int i = 0; i < h; ++i) {
					u8 Ra = in[i - 1], Rb = last[i], Rc = last[i - 1];
					rle_encode_next(&rle_ctx, (u8)(in[i] - jls_pred_med(Rb, Ra, Rc)));
				}
				last = in;
				in += h + LEFTMARGIN + RIGHTMARGIN;
			}
		}

		int rle_size = rle_encode_end(&rle_ctx);
		if (rle_size > RP_HUFF_WS_SIZE) {
			nsDbgPrint("rle_encode_end overflow!\n");
			return -1;
		}

		*(u32 *)send_ctx->buffer_begin = rle_size;
		PutBitContext s;
		init_put_bits(&s, send_ctx->buffer_begin + sizeof(u32), send_ctx->buffer_end);
		s.user = send_ctx;

		int ret = huff_encode(send_ctx->huff_med_ws, &s, send_ctx->huff_med_pred_image, rle_size);
		if (ret) {
			nsDbgPrint("huff_encode failed: %d\n", ret);
			return -1;
		}
		if ((ret = flush_put_bits(&s))) {
			nsDbgPrint("huff flush_put_bits failed: %d\n", ret);
			return -1;
		}
		send_ctx->buffer_begin = s.buf_ptr;
	} else if (RP_ENCODER_IMAGEZERO_ENABLE && encoder_which == RP_ENCODER_IMAGE_ZERO) {
		struct BitCoderPtrs ptrs = {
			.p = (Code_def_t *)send_ctx->buffer_begin,
			.p_end = (Code_def_t *)send_ctx->buffer_end,
			.user = send_ctx,
		};
		ret = izEncodeImageRGB(&ptrs, src, h, w, h * 3);
		if (ret) {
			nsDbgPrint("izEncodeImageRGB failed: %d\n", ret);
			return -1;
		}
		send_ctx->buffer_begin = (u8 *)ptrs.p;
	} else if (RP_ENCODER_JPEG_TURBO_ENABLE && encoder_which == RP_ENCODER_JPEG_TURBO) {
		j_compress_ptr cinfo = send_ctx->jcinfo;
		struct rp_jpeg_client_data_t *cctx = (struct rp_jpeg_client_data_t *)cinfo->client_data;

		cctx->dst = send_ctx->buffer_begin;
		cctx->dst_end = send_ctx->buffer_end;
		cctx->user = send_ctx;

		cinfo->image_width = h;
		cinfo->image_height = w;
		int pitch = cinfo->image_width * cinfo->input_components;

		jpeg_start_compress(cinfo, 1);
		const u8 *rows[SCREEN_WIDTH_MAX];
		for (int i = 0; i < (int)cinfo->image_height; ++i)
			rows[i] = src + i * pitch;
		jpeg_write_scanlines(cinfo, (u8 **)rows, cinfo->image_height);
		jpeg_finish_compress(cinfo);
		if (0)
			nsDbgPrint("jpeg turbo memory used: %d/%d\n",
				cctx->alloc - cctx->alloc_begin,
				cctx->alloc_end - cctx->alloc_begin);
		cctx->alloc = cctx->alloc_begin;

		send_ctx->buffer_begin = cctx->dst;
	} else {
		nsDbgPrint("Unknown encoder: %d\n", encoder_which);
		return -1;
	}

	send_ctx->send_header->data_end = 1;
	ret = rpJLSSendEnd(send_ctx, 1);
	if (ret)
		return -1;
	return send_ctx->send_size_total;
}

int rpJLSEncodeImage_2(struct rp_jls_send_ctx_t *send_ctx,
	struct rp_jls_params_t *params, struct rp_jls_ctx_t *jls_ctx,
	const u8 *src, const u8 *src_2, int w, int h, int bpp, int bpp_2, u8 encoder_which
) {
	if (encoder_which == RP_ENCODER_HUFF_JLS || !src_2) {
		return rpJLSEncodeImage(send_ctx, params, jls_ctx, src, src_2, w, h, bpp, bpp_2, RP_ENCODER_HUFF_JLS);
	} else {
		if (send_ctx->send_header->plane_type != RP_PLANE_TYPE_COLOR || send_ctx->send_header->plane_comp != RP_PLANE_COMP_UV)
			return -1;

		int ret, size;

		send_ctx->send_header->plane_comp = RP_PLANE_COMP_U;
		ret = rpJLSEncodeImage(send_ctx, params, jls_ctx, src, 0, w, h, bpp, 0, encoder_which);
		if (ret < 0) { return ret; } size += ret;

		send_ctx->send_header->plane_comp = RP_PLANE_COMP_V;
		ret = rpJLSEncodeImage(send_ctx, params, jls_ctx, src_2, 0, w, h, bpp_2, 0, encoder_which);
		if (ret < 0) { return ret; } size += ret;

		return size;
	}
}

int ffmpeg_jls_flush(struct PutBitContext *ctx) {
	return rpJLSSendEncodedCallback_FFmpeg(ctx);
}

int jls_bito_flush(struct bito_ctx *ctx) {
	return rpJLSSendEncodedCallback_JLS(ctx);
}

int izBitCoderFlush(struct BitCoderPtrs *ctx) {
	return rpJLSSendEncodedCallback_IZ(ctx);
}

int zstd_med_init_ws(u8 *ws, int ws_size, int comp_level) {
	ZSTD_parameters params = ZSTD_getParams(comp_level, SCREEN_HEIGHT, 0);
	ZSTD_CCtx_params cctx = { 0 };

	size_t ret;
	if ((ret = ZSTD_CCtxParams_init_advanced(&cctx, params))) {
		nsDbgPrint("ZSTD_CCtxParams_init_advanced failed: %d\n", ret);
		return ret;
	}
	ret = ZSTD_estimateCStreamSize_usingCCtxParams(&cctx);
	if (ZSTD_isError(ret)) {
		nsDbgPrint("ZSTD_estimateCStreamSize_usingCCtxParams failed: %d\n", ret);
		return ret;
	}
	if ((int)ret > ws_size) {
		nsDbgPrint("zstd_med_init_ws provided buffer too small: %d (%d)\n", ws_size, ret);
		return -1;
	}
	ZSTD_CStream *cstream = ZSTD_initStaticCStream(ws, ws_size);
	if (!cstream) {
		nsDbgPrint("ZSTD_initStaticCStream failed\n");
		return -1;
	}

	if ((ret = ZSTD_CCtx_setParams(cstream, params))) {
		nsDbgPrint("ZSTD_CCtx_setParams failed: %d\n", ret);
		return ret;
	}

	return 0;
}

void jpeg_turbo_init_ctx(struct jpeg_compress_struct cinfo[RP_ENCODE_THREAD_COUNT],
	struct rp_jpeg_client_data_t cinfo_user[RP_ENCODE_THREAD_COUNT],
	struct jpeg_error_mgr *jerr, volatile u8 *exit_thread, u8 *alloc, u32 size
) {
	struct jpeg_error_mgr *err = jpeg_std_error(jerr);
	for (int i = 0; i < RP_ENCODE_THREAD_COUNT; ++i) {
		cinfo_user[i].alloc = alloc + size * i;
		cinfo_user[i].alloc_end = cinfo_user[i].alloc + size;
		cinfo_user[i].exit_thread = exit_thread;

		cinfo[i].err = err;
		cinfo[i].client_data = &cinfo_user[i];
		jpeg_create_compress(&cinfo[i]);
		jpeg_stdio_dest(&cinfo[i], 0);

		cinfo[i].in_color_space = JCS_RGB;
		jpeg_set_defaults(&cinfo[i]);
		cinfo[i].dct_method = JDCT_FASTEST;
		cinfo[i].input_components = 3;

		cinfo_user[i].alloc_begin = cinfo_user[i].alloc;
	}
}

int jpeg_turbo_write(j_common_ptr cinfo, const u8 *buf, u32 size) {
	struct rp_jpeg_client_data_t *cctx = (struct rp_jpeg_client_data_t *)cinfo->client_data;
	while (size) {
		int write_size = RP_MIN((int)size, (int)(cctx->dst_end - cctx->dst));
		if (!write_size) {
			int ret;
			if ((ret = rpJLSSendEncodedCallback_JT(cinfo)))
				return ret;
			continue;
		}
		memcpy(cctx->dst, buf, write_size);
		cctx->dst += write_size;
		buf += write_size;
		size -= write_size;
	}
	return 0;
}

void *jpeg_turbo_malloc(j_common_ptr cinfo, size_t size) {
	struct rp_jpeg_client_data_t *cctx = (struct rp_jpeg_client_data_t *)cinfo->client_data;
	size = (size + 8 - 1) / 8 * 8;
	u8 *ret = cctx->alloc;
	u8 *alloc_next = ret + size;
	if (alloc_next <= cctx->alloc_end) {
		cctx->alloc = alloc_next;
		return ret;
	}
	showDbg((u8 *)"jpeg_turbo_malloc out of memory\n", 0, 0);
	return 0;
}

void jpeg_turbo_free(j_common_ptr cinfo UNUSED, void *ptr UNUSED) {
}
