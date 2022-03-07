
struct I_C(RP_ENC_CTX_, I_N) {
	BLIT_CONTEXT *bctx;
	struct I_C(ENC_FD_CTX_, I_N) *fd, *fd_pf;
	struct I_C(ENC_CTX_, I_N) *enc;
};

static int I_C(downsampleHDiffPredSelCctxImageSend_Y_, I_N)(COMPRESS_CONTEXT *cctx, struct RP_DATA_HEADER *header, int top_bot,
	struct I_C(RP_ENC_CTX_, I_N)* ctx, int select_prediction
) {
	int ret;
	downsampleImageH(ctx->fd->y.ds2, ctx->fd->y.ds, I_WIDTH, I_HEIGHT / 2);
	diffPredSelCctxImage_Y(cctx, ctx->enc->im.ds2_y_fd, ctx->fd->y.ds2, ctx->fd_pf->y.ds2,
		ctx->enc->im.ds2_y_p, ctx->enc->im.ds2_y_s, ctx->enc->im.ds2_y_m, sizeof(ctx->enc->im.ds2_y_s), sizeof(ctx->enc->im.ds2_y_m),
		I_WIDTH / 2, I_HEIGHT / 2, select_prediction);
	cctx->max_compressed_size = UINT32_MAX;
	header->flags |= RP_DATA_DOWNSAMPLE2;
	if ((ret = rpTestCompressAndSend(top_bot, *header, cctx)) < 0) {
		return -1;
	}
	ctx->fd->flags |= RP_ENC_DS2_Y;
	return ret;
}

static int I_C(downsampleVDiffPredSelCctxImageSend_Y_, I_N)(COMPRESS_CONTEXT *cctx, struct RP_DATA_HEADER *header, int top_bot,
	struct I_C(RP_ENC_CTX_, I_N)* ctx, int select_prediction)
{
	int ret;
	downsampleImageV(ctx->fd->y.ds, ctx->fd->y.im, I_WIDTH, I_HEIGHT);
	diffPredSelCctxImage_Y(cctx, ctx->enc->im.ds_y_fd, ctx->fd->y.ds, ctx->fd_pf->y.ds,
		ctx->enc->im.ds_y_p, ctx->enc->im.ds_y_s, ctx->enc->im.ds_y_m,
		sizeof(ctx->enc->im.ds_y_s), sizeof(ctx->enc->im.ds_y_m),
		I_WIDTH, I_HEIGHT / 2, select_prediction);
	cctx->max_compressed_size = rp_ctx->network_params.bitsPerY / BITS_PER_BYTE;
	header->flags |= RP_DATA_DOWNSAMPLE;
	if ((ret = rpTestCompressAndSend(top_bot, *header, cctx)) < 0) {
		downsampleImageH(ctx->fd_pf->y.ds2, ctx->fd_pf->y.ds, I_WIDTH, I_HEIGHT / 2);
		if ((ret = I_C(downsampleHDiffPredSelCctxImageSend_Y_, I_N)(cctx, header, top_bot,
			ctx, select_prediction
		)) < 0) {
			return -1;
		}
	} else {
		ctx->fd->flags &= ~RP_ENC_DS2_Y;
		ctx->fd->flags |= RP_ENC_DS_Y;
	}
	return ret;
}

static int I_C(downsampleHDiffPredSelCctxImageSend_UV_, I_N)(COMPRESS_CONTEXT *cctx, struct RP_DATA_HEADER *header, int top_bot,
	struct I_C(RP_ENC_CTX_, I_N)* ctx, int select_prediction
) {
	int ret;
	downsampleImageH(ctx->fd->ds2_u.ds2, ctx->fd->ds2_u.ds, I_WIDTH / 2, I_HEIGHT / 4);
	downsampleImageH(ctx->fd->ds2_v.ds2, ctx->fd->ds2_v.ds, I_WIDTH / 2, I_HEIGHT / 4);
	diffPredSelCctxImage_UV(cctx, ctx->enc->im.ds2_ds2_u_fd, ctx->fd->ds2_u.ds2, ctx->fd_pf->ds2_u.ds2, ctx->enc->im.ds2_ds2_u_p,
		ctx->enc->im.ds2_ds2_u_s, ctx->enc->im.ds2_ds2_u_m, sizeof(ctx->enc->im.ds2_ds2_u_s), sizeof(ctx->enc->im.ds2_ds2_u_m),
		ctx->enc->im.ds2_ds2_v_fd, ctx->fd->ds2_v.ds2, ctx->fd_pf->ds2_v.ds2, ctx->enc->im.ds2_ds2_v_p,
		ctx->enc->im.ds2_ds2_v_s, ctx->enc->im.ds2_ds2_v_m, sizeof(ctx->enc->im.ds2_ds2_v_s), sizeof(ctx->enc->im.ds2_ds2_v_m),
		I_WIDTH / 4, I_HEIGHT / 4, select_prediction);
	cctx->max_compressed_size = UINT32_MAX;
	header->flags |= RP_DATA_DOWNSAMPLE2;
	if ((ret = rpTestCompressAndSend(top_bot, *header, cctx)) < 0) {
		return -1;
	}
	ctx->fd->flags |= RP_ENC_DS2_DS2_UV;
	return ret;
}

static int I_C(downsampleVDiffPredSelCctxImageSend_UV_, I_N)(COMPRESS_CONTEXT *cctx, struct RP_DATA_HEADER *header, int top_bot,
	struct I_C(RP_ENC_CTX_, I_N)* ctx, int select_prediction)
{
	int ret;
	downsampleImageV(ctx->fd->ds2_u.ds, ctx->fd->ds2_u.im, I_WIDTH / 2, I_HEIGHT / 2);
	downsampleImageV(ctx->fd->ds2_v.ds, ctx->fd->ds2_v.im, I_WIDTH / 2, I_HEIGHT / 2);
	diffPredSelCctxImage_UV(cctx, ctx->enc->im.ds_ds2_u_fd, ctx->fd->ds2_u.ds, ctx->fd_pf->ds2_u.ds, ctx->enc->im.ds_ds2_u_p,
		ctx->enc->im.ds_ds2_u_s, ctx->enc->im.ds_ds2_u_m, sizeof(ctx->enc->im.ds_ds2_u_s), sizeof(ctx->enc->im.ds_ds2_u_m),
		ctx->enc->im.ds_ds2_v_fd, ctx->fd->ds2_v.ds, ctx->fd_pf->ds2_v.ds, ctx->enc->im.ds_ds2_v_p,
		ctx->enc->im.ds_ds2_v_s, ctx->enc->im.ds_ds2_v_m, sizeof(ctx->enc->im.ds_ds2_v_s), sizeof(ctx->enc->im.ds_ds2_v_m),
		I_WIDTH / 2, I_HEIGHT / 4, select_prediction);
	cctx->max_compressed_size = rp_ctx->network_params.bitsPerUV / BITS_PER_BYTE;
	header->flags |= RP_DATA_DOWNSAMPLE;
	if ((ret = rpTestCompressAndSend(top_bot, *header, cctx)) < 0) {
		downsampleImageH(ctx->fd_pf->ds2_u.ds2, ctx->fd_pf->ds2_u.ds, I_WIDTH / 2, I_HEIGHT / 4);
		downsampleImageH(ctx->fd_pf->ds2_v.ds2, ctx->fd_pf->ds2_v.ds, I_WIDTH / 2, I_HEIGHT / 4);
		if ((ret = I_C(downsampleHDiffPredSelCctxImageSend_UV_, I_N)(cctx, header, top_bot,
			ctx, select_prediction
		)) < 0) {
			return -1;
		}
	} else {
		ctx->fd->flags &= ~RP_ENC_DS2_DS2_UV;
		ctx->fd->flags |= RP_ENC_DS_DS2_UV;
	}
	return ret;
}

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
	u8 *dp_y_in = ctx->fd->y.im;
	u8 *dp_u_in = ctx->enc->im.u;
	u8 *dp_v_in = ctx->enc->im.v;
	u8 dynamic_downsample = !!(rp_ctx->cfg.flags & RP_DYNAMIC_DOWNSAMPLE);

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

	downsampleImage(ctx->fd->ds2_u.im, ctx->enc->im.u, I_WIDTH, I_HEIGHT);
	downsampleImage(ctx->fd->ds2_v.im, ctx->enc->im.v, I_WIDTH, I_HEIGHT);

	if (is_key) {
		data_header.flags &= ~RP_DATA_FRAME_DELTA;

		predictImage(ctx->enc->im.y_p, ctx->fd->y.im, I_WIDTH, I_HEIGHT);
		data_header.flags &= ~RP_DATA_Y_UV;
		data_header.flags &= ~(RP_DATA_DOWNSAMPLE | RP_DATA_DOWNSAMPLE2);

		cctx.data = ctx->enc->im.y_p;
		cctx.data_size = sizeof(ctx->enc->im.y_p);
		cctx.max_compressed_size = dynamic_downsample ? rp_ctx->network_params.bitsPerY / BITS_PER_BYTE : UINT32_MAX;

		if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			downsampleImageV(ctx->fd->y.ds, ctx->fd->y.im, I_WIDTH, I_HEIGHT);
			predictImage(ctx->enc->im.ds_y_p, ctx->fd->y.ds, I_WIDTH, I_HEIGHT / 2);
			data_header.flags |= RP_DATA_DOWNSAMPLE;

			cctx.data = ctx->enc->im.ds_y_p;
			cctx.data_size = sizeof(ctx->enc->im.ds_y_p);

			if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
				downsampleImageH(ctx->fd->y.ds2, ctx->fd->y.ds, I_WIDTH, I_HEIGHT / 2);
				predictImage(ctx->enc->im.ds2_y_p, ctx->fd->y.ds2, I_WIDTH / 2, I_HEIGHT / 2);
				data_header.flags |= RP_DATA_DOWNSAMPLE2;

				cctx.data = ctx->enc->im.ds2_y_p;
				cctx.data_size = sizeof(ctx->enc->im.ds2_y_p);
				cctx.max_compressed_size = UINT32_MAX;

				if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
					return -1;
				}
				ctx->fd->flags |= RP_ENC_DS2_Y;
			} else {
				ctx->fd->flags |= RP_ENC_DS_Y;
			}
		}
		ctx->fd->flags |= RP_ENC_HAVE_Y;
		rets += ret;

		predictImage(ctx->enc->im.ds2_u_p, ctx->fd->ds2_u.im, I_WIDTH / 2, I_HEIGHT / 2);
		predictImage(ctx->enc->im.ds2_v_p, ctx->fd->ds2_v.im, I_WIDTH / 2, I_HEIGHT / 2);
		data_header.flags |= RP_DATA_Y_UV;
		data_header.flags &= ~(RP_DATA_DOWNSAMPLE | RP_DATA_DOWNSAMPLE2);

		cctx.data = ctx->enc->im.ds2_u_p;
		cctx.data_size = sizeof(ctx->enc->im.ds2_u_p) + sizeof(ctx->enc->im.ds2_v_p);
		cctx.max_compressed_size = dynamic_downsample ? rp_ctx->network_params.bitsPerUV / BITS_PER_BYTE : UINT32_MAX;

		if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			downsampleImageV(ctx->fd->ds2_u.ds, ctx->fd->ds2_u.im, I_WIDTH / 2, I_HEIGHT / 2);
			predictImage(ctx->enc->im.ds_ds2_u_p, ctx->fd->ds2_u.ds, I_WIDTH / 2, I_HEIGHT / 4);
			downsampleImageV(ctx->fd->ds2_v.ds, ctx->fd->ds2_v.im, I_WIDTH / 2, I_HEIGHT / 2);
			predictImage(ctx->enc->im.ds_ds2_v_p, ctx->fd->ds2_v.ds, I_WIDTH / 2, I_HEIGHT / 4);
			data_header.flags |= RP_DATA_DOWNSAMPLE;

			cctx.data = ctx->enc->im.ds_ds2_u_p;
			cctx.data_size = sizeof(ctx->enc->im.ds_ds2_u_p) + sizeof(ctx->enc->im.ds_ds2_v_p);

			if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
				downsampleImageH(ctx->fd->ds2_u.ds2, ctx->fd->ds2_u.ds, I_WIDTH / 2, I_HEIGHT / 4);
				predictImage(ctx->enc->im.ds2_ds2_u_p, ctx->fd->ds2_u.ds2, I_WIDTH / 4, I_HEIGHT / 4);
				downsampleImageH(ctx->fd->ds2_v.ds2, ctx->fd->ds2_v.ds, I_WIDTH / 2, I_HEIGHT / 4);
				predictImage(ctx->enc->im.ds2_ds2_v_p, ctx->fd->ds2_v.ds2, I_WIDTH / 4, I_HEIGHT / 4);
				data_header.flags |= RP_DATA_DOWNSAMPLE2;

				cctx.data = ctx->enc->im.ds2_ds2_u_p;
				cctx.data_size = sizeof(ctx->enc->im.ds2_ds2_u_p) + sizeof(ctx->enc->im.ds2_ds2_v_p);
				cctx.max_compressed_size = UINT32_MAX;

				if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
					return -1;
				}
				ctx->fd->flags |= RP_ENC_DS2_DS2_UV;
			} else {
				ctx->fd->flags |= RP_ENC_DS_DS2_UV;
			}
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

		data_header.flags &= ~RP_DATA_Y_UV;
		data_header.flags &= ~(RP_DATA_DOWNSAMPLE | RP_DATA_DOWNSAMPLE2);
		if (ctx->fd_pf->flags & RP_ENC_DS2_Y) {
			downsampleImageV(ctx->fd->y.ds, ctx->fd->y.im, I_WIDTH, I_HEIGHT);
			if ((ret = I_C(downsampleHDiffPredSelCctxImageSend_Y_, I_N)(&cctx, &data_header, top_bot,
				ctx, select_prediction
			)) < 0) {
				return -1;
			}
			rets += ret;

			upsampleFDCImageH(ctx->enc->rs.ds2_y_fd, ctx->enc->rs.ds2_y_c,
				ctx->enc->rs.ds2_y_c + sizeof(ctx->enc->rs.ds2_y_c),
				ctx->fd->y.ds, ctx->fd->y.ds2, I_WIDTH, I_HEIGHT / 2);
			cctx_data_sel(&cctx, ctx->enc->rs.ds2_y_fd, ctx->enc->rs.ds2_y_c,
				sizeof(ctx->enc->rs.ds2_y_fd), sizeof(ctx->enc->rs.ds2_y_c), 0, 1);
			cctx.max_compressed_size = rp_ctx->network_params.bitsPerY / BITS_PER_BYTE - rets;
			data_header.flags &= ~RP_DATA_DOWNSAMPLE2;
			data_header.flags |= RP_DATA_DOWNSAMPLE;
			if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			} else {
				ctx->fd->flags &= ~RP_ENC_DS2_Y;
				ctx->fd->flags |= RP_ENC_DS_Y;
				rets += ret;
			}
		} else if (ctx->fd_pf->flags & RP_ENC_DS_Y) {
			if ((ret = I_C(downsampleVDiffPredSelCctxImageSend_Y_, I_N)(&cctx, &data_header, top_bot,
				ctx, select_prediction
			)) < 0) {
				return -1;
			}
			rets += ret;

			if (ctx->fd->flags & RP_ENC_DS2_Y) {
			} else {
				upsampleFDCImageV(ctx->enc->rs.ds_y_fd, ctx->enc->rs.ds_y_c,
					ctx->enc->rs.ds_y_c + sizeof(ctx->enc->rs.ds_y_c),
					ctx->fd->y.im, ctx->fd->y.ds, I_WIDTH, I_HEIGHT);
				cctx_data_sel(&cctx, ctx->enc->rs.ds_y_fd, ctx->enc->rs.ds_y_c,
					sizeof(ctx->enc->rs.ds_y_fd), sizeof(ctx->enc->rs.ds_y_c), 0, 1);
				cctx.max_compressed_size -= rets;
				data_header.flags &= ~RP_DATA_DOWNSAMPLE;
				if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
				} else {
					ctx->fd->flags &= ~RP_ENC_DS_Y;
					rets += ret;
				}
			}
		} else {
			diffPredSelCctxImage_Y(&cctx, ctx->enc->im.y_fd, ctx->fd->y.im, ctx->fd_pf->y.im,
				ctx->enc->im.y_p, ctx->enc->im.y_s, ctx->enc->im.y_m,
				sizeof(ctx->enc->im.y_s), sizeof(ctx->enc->im.y_m),
				I_WIDTH, I_HEIGHT, select_prediction);
			cctx.max_compressed_size = dynamic_downsample ? rp_ctx->network_params.bitsPerY / BITS_PER_BYTE : UINT32_MAX;

			if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
				downsampleImageV(ctx->fd_pf->y.ds, ctx->fd_pf->y.im, I_WIDTH, I_HEIGHT);
				if ((ret = I_C(downsampleVDiffPredSelCctxImageSend_Y_, I_N)(&cctx, &data_header, top_bot,
					ctx, select_prediction
				)) < 0) {
					return -1;
				}
			}
			rets += ret;
		}
		ctx->fd->flags |= RP_ENC_HAVE_Y;

		data_header.flags |= RP_DATA_Y_UV;
		data_header.flags &= ~(RP_DATA_DOWNSAMPLE | RP_DATA_DOWNSAMPLE2);
		if (ctx->fd_pf->flags & RP_ENC_DS2_DS2_UV) {
			downsampleImageV(ctx->fd->ds2_u.ds, ctx->fd->ds2_u.im, I_WIDTH / 2, I_HEIGHT / 2);
			downsampleImageV(ctx->fd->ds2_v.ds, ctx->fd->ds2_v.im, I_WIDTH / 2, I_HEIGHT / 2);
			if ((ret = I_C(downsampleHDiffPredSelCctxImageSend_UV_, I_N)(&cctx, &data_header, top_bot,
				ctx, select_prediction
			)) < 0) {
				return -1;
			}
			rets += ret;

			upsampleFDCImageH(ctx->enc->rs.ds2_ds2_u_fd, ctx->enc->rs.ds2_ds2_u_c,
				ctx->enc->rs.ds2_ds2_u_c + sizeof(ctx->enc->rs.ds2_ds2_u_c),
				ctx->fd->ds2_u.ds, ctx->fd->ds2_u.ds2, I_WIDTH / 2, I_HEIGHT / 4);
			upsampleFDCImageH(ctx->enc->rs.ds2_ds2_v_fd, ctx->enc->rs.ds2_ds2_v_c,
				ctx->enc->rs.ds2_ds2_v_c + sizeof(ctx->enc->rs.ds2_ds2_v_c),
				ctx->fd->ds2_v.ds, ctx->fd->ds2_v.ds2, I_WIDTH / 2, I_HEIGHT / 4);
			cctx_data_sel(&cctx, ctx->enc->rs.ds2_ds2_u_fd, ctx->enc->rs.ds2_ds2_u_c,
				sizeof(ctx->enc->rs.ds2_ds2_u_fd) + sizeof(ctx->enc->rs.ds2_ds2_v_fd),
				sizeof(ctx->enc->rs.ds2_ds2_u_c) + sizeof(ctx->enc->rs.ds2_ds2_v_c), 0, 1);
			cctx.max_compressed_size = rp_ctx->network_params.bitsPerUV / BITS_PER_BYTE - rets;
			data_header.flags &= ~RP_DATA_DOWNSAMPLE2;
			data_header.flags |= RP_DATA_DOWNSAMPLE;
			if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
			} else {
				ctx->fd->flags &= ~RP_ENC_DS2_DS2_UV;
				ctx->fd->flags |= RP_ENC_DS_DS2_UV;
				rets += ret;
			}
		} else if (ctx->fd_pf->flags & RP_ENC_DS_DS2_UV) {
			if ((ret = I_C(downsampleVDiffPredSelCctxImageSend_UV_, I_N)(&cctx, &data_header, top_bot,
				ctx, select_prediction
			)) < 0) {
				return -1;
			}
			rets += ret;

			if (ctx->fd->flags & RP_ENC_DS2_DS2_UV) {
			} else {
				upsampleFDCImageV(ctx->enc->rs.ds_ds2_u_fd, ctx->enc->rs.ds_ds2_u_c,
					ctx->enc->rs.ds_ds2_u_c + sizeof(ctx->enc->rs.ds_ds2_u_c),
					ctx->fd->ds2_v.im, ctx->fd->ds2_v.ds, I_WIDTH / 2, I_HEIGHT / 2);
				upsampleFDCImageV(ctx->enc->rs.ds_ds2_v_fd, ctx->enc->rs.ds_ds2_v_c,
					ctx->enc->rs.ds_ds2_v_c + sizeof(ctx->enc->rs.ds_ds2_v_c),
					ctx->fd->ds2_v.im, ctx->fd->ds2_v.ds, I_WIDTH / 2, I_HEIGHT / 2);
				cctx_data_sel(&cctx, ctx->enc->rs.ds_ds2_u_fd, ctx->enc->rs.ds_ds2_u_c,
					sizeof(ctx->enc->rs.ds_ds2_u_fd) + sizeof(ctx->enc->rs.ds_ds2_v_fd),
					sizeof(ctx->enc->rs.ds_ds2_u_c) + sizeof(ctx->enc->rs.ds_ds2_v_c), 0, 1);
				cctx.max_compressed_size -= rets;
				data_header.flags &= ~RP_DATA_DOWNSAMPLE;
				if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
				} else {
					ctx->fd->flags &= ~RP_ENC_DS_DS2_UV;
					rets += ret;
				}
			}
		} else {
			diffPredSelCctxImage_UV(&cctx, ctx->enc->im.ds2_u_fd, ctx->fd->ds2_u.im, ctx->fd_pf->ds2_u.im, ctx->enc->im.ds2_u_p,
				ctx->enc->im.ds2_u_s, ctx->enc->im.ds2_u_m, sizeof(ctx->enc->im.ds2_u_s), sizeof(ctx->enc->im.ds2_u_m),
				ctx->enc->im.ds2_v_fd, ctx->fd->ds2_v.im, ctx->fd_pf->ds2_v.im, ctx->enc->im.ds2_v_p,
				ctx->enc->im.ds2_v_s, ctx->enc->im.ds2_v_m, sizeof(ctx->enc->im.ds2_v_s), sizeof(ctx->enc->im.ds2_v_m),
				I_WIDTH / 2, I_HEIGHT / 2, select_prediction);
			cctx.max_compressed_size = dynamic_downsample ? rp_ctx->network_params.bitsPerUV / BITS_PER_BYTE : UINT32_MAX;

			if ((ret = rpTestCompressAndSend(top_bot, data_header, &cctx)) < 0) {
				downsampleImageV(ctx->fd_pf->ds2_u.ds, ctx->fd_pf->ds2_u.im, I_WIDTH / 2, I_HEIGHT / 2);
				downsampleImageV(ctx->fd_pf->ds2_v.ds, ctx->fd_pf->ds2_v.im, I_WIDTH / 2, I_HEIGHT / 2);
				if ((ret = I_C(downsampleVDiffPredSelCctxImageSend_UV_, I_N)(&cctx, &data_header, top_bot,
					ctx, select_prediction
				)) < 0) {
					return -1;
				}
			}
			rets += ret;
		}
		ctx->fd->flags |= RP_ENC_HAVE_UV;
	}

	return rets;
}
