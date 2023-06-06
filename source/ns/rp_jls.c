#include "rp_jls.h"
#include "rp_color_aux.h"
#include "rp_syn_chan.h"
#include "rp_syn.h"

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

struct rp_jls_send_ctx_t {
	struct rp_syn_comp_t *network_queue;
	volatile u8 *exit_thread;
	u8 multicore;
	u8 thread_n;
	struct rp_network_encode_t *network;
	u8 *buffer_end;
	u32 send_size_total;
};

static int rpJLSSendBegin(struct rp_jls_send_ctx_t *ctx) {
	int acquire_count = 0;
	while (!*ctx->exit_thread) {
		ctx->network =
			rp_network_encode_acquire(&ctx->network_queue->encode, RP_THREAD_LOOP_MED_WAIT, ctx->multicore);
		if (!ctx->network) {
			if (++acquire_count > RP_THREAD_LOOP_WAIT_COUNT) {
				nsDbgPrint("rp_network_encode_acquire timeout\n");
				return -1;
			}
			continue;
		}
		return 0;
	}
	return -1;
}

static int rpJLSSendEnd(struct rp_jls_send_ctx_t *ctx) {
	ctx->send_size_total += ctx->network->size = ctx->buffer_end - ctx->network->buffer;
	if (rp_network_transfer_release(&ctx->network_queue->transfer, ctx->network, ctx->multicore) < 0) {
		nsDbgPrint("%d rpEncodeScreenAndSend network release syn failed\n", ctx->thread_n);
		return -1;
	}
	ctx->network = 0;
	ctx->buffer_end = 0;
	return 0;
}

static int rpJLSSendEncodedCallback(struct rp_jls_send_ctx_t *ctx) {
	int ret;
	if ((ret = rpJLSSendEnd(ctx)))
		return ret;
	if ((ret = rpJLSSendBegin(ctx)))
		return ret;
	return 0;
}

static void rpJLSSendEncodedCallback_0(struct PutBitContext *ctx) {
	struct rp_jls_send_ctx_t *sctx = (struct rp_jls_send_ctx_t *)ctx->user;
	sctx->buffer_end = ctx->buf_ptr;
	if (rpJLSSendEncodedCallback(sctx) == 0) {
		sctx->buffer_end = ctx->buf_ptr = ctx->buf = sctx->network->buffer;
		ctx->buf_end = sctx->network->buffer + sizeof(sctx->network->buffer);
	}
}

static void rpJLSSendEncodedCallback_1(struct bito_ctx *ctx) {
	struct rp_jls_send_ctx_t *sctx = (struct rp_jls_send_ctx_t *)ctx->user;
	sctx->buffer_end = (u8 *)ctx->buf;
	if (rpJLSSendEncodedCallback(sctx) == 0) {
		sctx->buffer_end = ctx->buf = sctx->network->buffer;
		ctx->buf_end = sctx->network->buffer + sizeof(sctx->network->buffer);
	}
}

int rpJLSEncodeImage(struct rp_send_data_header *send_header, struct rp_syn_comp_t *network_queue,
	int network_sync, volatile u8 *exit_thread,
	struct rp_jls_params_t *params, struct rp_jls_ctx_t *jls_ctx,
	const u8 *src, int w, int h, int bpp,
	u8 encoder_which, u8 encode_verify UNUSED, u8 thread_n
) {
	int ret;
	struct rp_jls_send_ctx_t send_ctx = {
		.network_queue = network_queue,
		.multicore = network_sync,
		.thread_n = thread_n,
		.exit_thread = exit_thread
	};
	ret = rpJLSSendBegin(&send_ctx);
	if (ret < 0)
		return ret;

	int send_header_size = sizeof(struct rp_send_data_header);
	memcpy(send_ctx.network->buffer, send_header, send_header_size);
	send_ctx.buffer_end = send_ctx.network->buffer + send_header_size;
	int buffer_size = sizeof(send_ctx.network->buffer) - send_header_size;

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

	if (encoder_which == 0) {
		JLSState state = { 0 };
		state.bpp = bpp;

		ff_jpegls_reset_coding_parameters(&state, 0);
		ff_jpegls_init_state(&state);

		PutBitContext s;
		init_put_bits(&s, send_ctx.buffer_end, buffer_size);
		s.flush = rpJLSSendEncodedCallback_0;
		s.user = &send_ctx;

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
		send_ctx.buffer_end = s.buf_ptr;
	} else {
		struct jls_enc_ctx *ctx = &jls_ctx->enc;
		struct bito_ctx *bctx = &jls_ctx->bito;
		bctx->flush = rpJLSSendEncodedCallback_1;
		bctx->user = &send_ctx;
		jpeg_ls_encode(
			enc_params, ctx, bctx, (char *)send_ctx.buffer_end, buffer_size, src,
			w, h, h + LEFTMARGIN + RIGHTMARGIN,
			params->enc_luts.classmap
		);
		send_ctx.buffer_end = (u8 *)bctx->buf;
	}

	ret = rpJLSSendEnd(&send_ctx);
	if (ret)
		return ret;
	return send_ctx.send_size_total;
}
