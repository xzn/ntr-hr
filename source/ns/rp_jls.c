#include "rp_jls.h"
#include "rp_color_aux.h"
#include "rp_syn_chan.h"
#include "rp_syn.h"
#include "rp_net.h"

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
	if (init)
		memcpy(ctx->network->buffer, ctx->send_header, sizeof(struct rp_send_data_header));
	ctx->buffer_begin = ctx->network->buffer + sizeof(struct rp_send_data_header);
	ctx->buffer_end = ctx->network->buffer + sizeof(ctx->network->buffer);
	return 0;
}

static int rpJLSSendEnd(struct rp_jls_send_ctx_t *ctx, u8 fini) {
	if (*ctx->exit_thread)
		return -1;

	if (fini)
		memcpy(ctx->network->buffer, ctx->send_header, sizeof(struct rp_send_data_header));

	int ret;
	ctx->send_size_total += ctx->network->size = ctx->buffer_begin - ctx->network->buffer;
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

static int rpJLSSendEncodedCallback_0(struct PutBitContext *ctx) {
	struct rp_jls_send_ctx_t *sctx = (struct rp_jls_send_ctx_t *)ctx->user;
	sctx->buffer_begin = ctx->buf_ptr;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	ctx->buf_ptr = ctx->buf = sctx->buffer_begin;
	ctx->buf_end = sctx->buffer_end;
	return 0;
}

static int rpJLSSendEncodedCallback_1(struct bito_ctx *ctx) {
	struct rp_jls_send_ctx_t *sctx = (struct rp_jls_send_ctx_t *)ctx->user;
	sctx->buffer_begin = (u8 *)ctx->buf;
	int ret;
	if ((ret = rpJLSSendEncodedCallback(sctx)))
		return ret;
	ctx->buf = sctx->buffer_begin;
	ctx->buf_end = sctx->buffer_end;
	return 0;
}

extern const uint8_t psl0[];
int rpJLSEncodeImage(struct rp_jls_send_ctx_t *send_ctx,
	struct rp_jls_params_t *params, struct rp_jls_ctx_t *jls_ctx,
	const u8 *src, int w, int h, int bpp, u8 encoder_which
) {
	int ret;

	rpJLSSendClear(send_ctx);
	ret = rpJLSSendBegin(send_ctx, 1);
	if (ret < 0)
		return ret;

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

		case 1:
			enc_params = &params->enc_params[RP_ENCODE_PARAMS_BPP1]; break;

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
		init_put_bits(&s, send_ctx->buffer_begin, send_ctx->buffer_end);
		s.flush = rpJLSSendEncodedCallback_0;
		s.user = send_ctx;

		const u8 *last = psl0 + LEFTMARGIN;
		const u8 *in = src + LEFTMARGIN;

		for (int i = 0; i < w; ++i) {
			ret = ls_encode_line(
				&state, &s, last, in, h,
				enc_params->vLUT,
				params->enc_luts.classmap
			);
			if (ret)
				return ret;
			last = in;
			in += h + LEFTMARGIN + RIGHTMARGIN;
		}

		flush_put_bits(&s);
		send_ctx->buffer_begin = s.buf_ptr;
	} else {
		struct jls_enc_ctx *ctx = &jls_ctx->enc;
		struct bito_ctx *bctx = &jls_ctx->bito;
		bctx->flush = rpJLSSendEncodedCallback_1;
		bctx->user = send_ctx;
		ret = jpeg_ls_encode(
			enc_params, ctx, bctx,
			(char *)send_ctx->buffer_begin, (char *)send_ctx->buffer_end,
			src, w, h, h + LEFTMARGIN + RIGHTMARGIN,
			params->enc_luts.classmap
		);
		if (ret)
			return ret;
		send_ctx->buffer_begin = (u8 *)bctx->buf;
	}

	send_ctx->send_header->data_end = 1;
	send_ctx->send_header->data_size = send_ctx->buffer_begin - send_ctx->network->buffer - sizeof(struct rp_send_data_header);
	ret = rpJLSSendEnd(send_ctx, 1);
	if (ret)
		return ret;
	return send_ctx->send_size_total;
}
