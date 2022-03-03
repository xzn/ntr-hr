
struct I_C(RP_ENC_CTX_, I_N) {
	BLIT_CONTEXT *bctx;
	struct I_C(ENC_FD_CTX_, I_N) *fd, *fd_pf;
	struct I_C(ENC_, I_N) *enc;
};

static int I_C(rp_enc_main_, I_N)(struct I_C(RP_ENC_CTX_, I_N)* ctx, struct RP_DATA_HEADER data_header) {
	COMPRESS_CONTEXT cctx = { 0 };
	int ret;
	int rets = 0;

	int bpp = ctx->bctx->bpp;
	int bic = ctx->bctx->blankInColumn;
	int pitch = ctx->bctx->src_pitch;
	int format = ctx->bctx->format;
	int is_key = ctx->bctx->is_key;
	int top_bot = ctx->bctx->top_bot;
	u8 *sp = ctx->bctx->src;
	u8 *dp_y_in = ctx->fd->dp_y;
	u8 *dp_u_in = ctx->enc->im.dp_u;
	u8 *dp_v_in = ctx->enc->im.dp_v;

	int even_odd = 0;
	if (rp_ctx->interlace) {
		even_odd = ctx->bctx->id % 2;
		if (even_odd) {
			sp += bpp;
		}
		bpp *= 2;
	}

	if (convertYUVImage(format, I_WIDTH, I_HEIGHT, bpp, bic, sp, dp_y_in, dp_u_in, dp_v_in) < 0) {
		return -1;
	}

	if ((ctx->fd_pf->flags & (RP_ENC_HAVE_Y | RP_ENC_HAVE_UV)) != (RP_ENC_HAVE_Y | RP_ENC_HAVE_UV)) {
		is_key = 1;
	}
	ctx->fd->flags = 0;

	downsampleImage(ctx->fd->dp_ds2_u, ctx->enc->im.dp_u, I_WIDTH, I_HEIGHT);
	downsampleImage(ctx->fd->dp_ds2_v, ctx->enc->im.dp_v, I_WIDTH, I_HEIGHT);

	if (is_key) {
		data_header.flags &= ~RP_DATA_FRAME_DELTA;

		predictImage(ctx->enc->im.dp_p_y, ctx->fd->dp_y, I_WIDTH, I_HEIGHT);
		data_header.flags &= ~RP_DATA_Y_UV;

		cctx.data = ctx->enc->im.dp_p_y;
		cctx.data_size = sizeof(ctx->enc->im.dp_p_y);
		cctx.max_compressed_size = UINT32_MAX;

		if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			return -1;
		}
		ctx->fd->flags |= RP_ENC_HAVE_Y;
		rets += ret;

		predictImage(ctx->enc->im.dp_ds2_p_u, ctx->fd->dp_ds2_u, I_WIDTH / 2, I_HEIGHT / 2);
		predictImage(ctx->enc->im.dp_ds2_p_v, ctx->fd->dp_ds2_v, I_WIDTH / 2, I_HEIGHT / 2);
		data_header.flags |= RP_DATA_Y_UV;

		cctx.data = ctx->enc->im.dp_ds2_p_u;
		cctx.data_size = sizeof(ctx->enc->im.dp_ds2_p_u) + sizeof(ctx->enc->im.dp_ds2_p_v);
		cctx.max_compressed_size = UINT32_MAX;

		if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			return -1;
		}
		ctx->fd->flags |= RP_ENC_HAVE_UV;
		rets += ret;
	} else {
		data_header.flags |= RP_DATA_FRAME_DELTA;

		u8 select_prediction = !!(rp_ctx->cfg.flags & RP_SELECT_PREDICTION);
		if (select_prediction) {
			data_header.flags |= RP_DATA_SELECT_FRAME_DELTA;
		} else {
			data_header.flags &= ~RP_DATA_SELECT_FRAME_DELTA;
		}

		differenceImage(ctx->enc->im.dp_fd_y, ctx->fd->dp_y, ctx->fd_pf->dp_y, I_WIDTH, I_HEIGHT);
		data_header.flags &= ~RP_DATA_Y_UV;
		if (select_prediction) {
			predictImage(ctx->enc->im.dp_p_y, ctx->fd->dp_y, I_WIDTH, I_HEIGHT);
			selectImage(ctx->enc->im.dp_s_y, ctx->enc->im.dp_m_y, ctx->enc->im.dp_m_y + sizeof(ctx->enc->im.dp_m_y),
				ctx->enc->im.dp_fd_y, ctx->enc->im.dp_p_y, I_WIDTH, I_HEIGHT);
			cctx.data = ctx->enc->im.dp_s_y;
			cctx.data_size = sizeof(ctx->enc->im.dp_s_y);
			cctx.data2 = ctx->enc->im.dp_m_y;
			cctx.data2_size = sizeof(ctx->enc->im.dp_m_y);
		} else {
			cctx.data = ctx->enc->im.dp_fd_y;
			cctx.data_size = sizeof(ctx->enc->im.dp_fd_y);
		}
		cctx.max_compressed_size = UINT32_MAX;

		if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			return -1;
		}
		ctx->fd->flags |= RP_ENC_HAVE_Y;
		rets += ret;

		differenceImage(ctx->enc->im.dp_ds2_fd_u, ctx->fd->dp_ds2_u, ctx->fd_pf->dp_ds2_u, I_WIDTH / 2, I_HEIGHT / 2);
		differenceImage(ctx->enc->im.dp_ds2_fd_v, ctx->fd->dp_ds2_v, ctx->fd_pf->dp_ds2_v, I_WIDTH / 2, I_HEIGHT / 2);
		data_header.flags |= RP_DATA_Y_UV;
		if (select_prediction) {
			predictImage(ctx->enc->im.dp_ds2_p_u, ctx->fd->dp_ds2_u, I_WIDTH / 2, I_HEIGHT / 2);
			predictImage(ctx->enc->im.dp_ds2_p_v, ctx->fd->dp_ds2_v, I_WIDTH / 2, I_HEIGHT / 2);
			selectImage(ctx->enc->im.dp_ds2_s_u, ctx->enc->im.dp_ds2_m_u, ctx->enc->im.dp_ds2_m_u + sizeof(ctx->enc->im.dp_ds2_m_u),
				ctx->enc->im.dp_ds2_fd_u, ctx->enc->im.dp_ds2_p_u, I_WIDTH / 2, I_HEIGHT / 2);
			selectImage(ctx->enc->im.dp_ds2_s_v, ctx->enc->im.dp_ds2_m_v, ctx->enc->im.dp_ds2_m_v + sizeof(ctx->enc->im.dp_ds2_m_v),
				ctx->enc->im.dp_ds2_fd_v, ctx->enc->im.dp_ds2_p_v, I_WIDTH / 2, I_HEIGHT / 2);
			cctx.data = ctx->enc->im.dp_ds2_s_u;
			cctx.data_size = sizeof(ctx->enc->im.dp_ds2_s_u) + sizeof(ctx->enc->im.dp_ds2_s_v);
			cctx.data2 = ctx->enc->im.dp_ds2_m_u;
			cctx.data2_size = sizeof(ctx->enc->im.dp_ds2_m_u) + sizeof(ctx->enc->im.dp_ds2_m_v);
		} else {
			cctx.data = ctx->enc->im.dp_ds2_fd_u;
			cctx.data_size = sizeof(ctx->enc->im.dp_ds2_fd_u) + sizeof(ctx->enc->im.dp_ds2_fd_v);
		}
		cctx.max_compressed_size = UINT32_MAX;

		if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			return -1;
		}
		ctx->fd->flags |= RP_ENC_HAVE_UV;
		rets += ret;
	}

	return rets;
}
