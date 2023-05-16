#include "global.h"

#include <stdint.h>


static void jpeg_ls_init(struct jls_enc_ctx *ctx, int bpp) {
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

    prepareLUTs(ctx);
}

const uint8_t psl0[240 + LEFTMARGIN + RIGHTMARGIN];
int jpeg_ls_encode(struct jls_enc_ctx *ctx, struct bito_ctx *bctx, char *dst, const pixel *src, int w, int h, int pitch, int bpp) {
    jpeg_ls_init(ctx, bpp);

    ctx->out = dst;

    init_stats(ctx);

    init_process_run(MAXRUN);

    bitoinit(bctx);

    const pixel *psl = psl0 + LEFTMARGIN;
    const pixel *sl = src + LEFTMARGIN;

    for (int i = 0; i < w; ++i) {
        lossless_doscanline(ctx, bctx, psl, sl, h);
        psl = sl;
        sl += pitch;
    }

    bitoflush(bctx, ctx->out);

    return ctx->out - dst;
}
