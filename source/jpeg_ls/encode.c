#include "global.h"
#include "bitio.h"

#include <stdint.h>


void jpeg_ls_init(struct jls_enc_params *ctx, int bpp, const uint16_t (*vLUT)[3]) {
    ctx->T1 = 0;
    ctx->T2 = 0;
    ctx->T3 = 0;

#ifndef FIXALPHA
    ALPHA(ctx) = 1 << bpp;
    CEIL_HALF_ALPHA(ctx) = (ALPHA(ctx) + 1) / 2;
#ifdef POW2
	HIGHMASK(ctx) = -ALPHA(ctx);
#endif
#endif

    /* compute bits per sample for input symbols */
    ctx->bpp = bpp;

    /* compute bits per sample for unencoded prediction errors */
    ctx->qbpp = bpp;

    if ( bpp < 2 ) bpp = 2;

    /* limit for unary part of Golomb code */
    if ( bpp < 8 )
        ctx->limit = 2*(bpp + 8) - ctx->qbpp -1;
    else
        ctx->limit = 4*bpp - ctx->qbpp - 1;

    set_thresholds(ALPHA(ctx), &ctx->T1, &ctx->T2, &ctx->T3);

    ctx->vLUT = vLUT;
}

const uint8_t psl0[240 + LEFTMARGIN + RIGHTMARGIN] = { 0 };
int jpeg_ls_encode(const struct jls_enc_params *params, struct jls_enc_ctx *ctx, struct bito_ctx *bctx,
    char *dst, int dst_size, const pixel *src, int w, int h, int pitch, const int16_t classmap[]
) {
    bctx->buf = dst;
    bctx->buf_end = dst + dst_size;

    init_stats(ctx, params->alpha);

    init_process_run(ctx, MAXRUN);

    bitoinit(bctx);

    const pixel *psl = psl0 + LEFTMARGIN - 1;
    const pixel *sl = src + LEFTMARGIN - 1;

    for (int i = 0; i < w; ++i) {
        lossless_doscanline(params, ctx, bctx, psl, sl, h, classmap);
        psl = sl;
        sl += pitch;
    }

    bitoflush(bctx);

    return bctx->buf_end - bctx->buf;
}
