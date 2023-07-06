#include "rlecodec.h"

void rle_encode_init(struct rp_rle_encode_ctx_t *ctx, uint8_t *dst) {
    ctx->dst = dst;
    ctx->dst_begin = ctx->dst;

    ctx->curr_val = -1;
    ctx->curr_len = 0;
    ctx->next_len = 0;
    ctx->next_good = 1;

    ctx->next_dst = ctx->dst++;
}

int rle_encode_end(struct rp_rle_encode_ctx_t *ctx) {
    rle_encode_next(ctx, -2);

    if (ctx->next_len) {
        *ctx->next_dst = ctx->next_len - RLE_MIN_LIT - 128;
        ctx->next_len = 0;
        ctx->next_dst = ctx->dst++;
    }

    return --ctx->dst - ctx->dst_begin;
}

static void rle_encode_stop_run(struct rp_rle_encode_ctx_t *ctx) {
    if (ctx->next_len > 0) {
        *ctx->next_dst = ctx->next_len - RLE_MIN_LIT - 128;
        ctx->next_len = 0;
    } else {
        --ctx->dst;
    }
    *ctx->dst++ = ctx->curr_len - RLE_MIN_RUN;
    *ctx->dst++ = ctx->curr_val;
    ctx->next_dst = ctx->dst++;
}

void rle_encode_next(struct rp_rle_encode_ctx_t *ctx, const int next_val) {
    // start run
    if (next_val == ctx->curr_val) {
        ++ctx->curr_len;
        if (ctx->curr_len == RLE_MAX_RUN) {
            // restart run
            rle_encode_stop_run(ctx);
            ctx->curr_val = -1;
            ctx->curr_len = 0;
        }
    } else {
        if (ctx->curr_len >= RLE_MIN_RUN) {
            // stop run
            rle_encode_stop_run(ctx);
        } else {
            // no run
            while (ctx->curr_len) {
                *ctx->dst++ = ctx->curr_val;
                --ctx->curr_len;

                ++ctx->next_len;
                if (ctx->next_len == RLE_MAX_LIT) {
                    *ctx->next_dst = ctx->next_len - RLE_MIN_LIT - 128;
                    ctx->next_len = 0;
                    ctx->next_dst = ctx->dst++;
                }
            }
        }

        ctx->curr_val = next_val;
        ctx->curr_len = 1;
    }
}

#undef RLE_ENCODE_STOP_RUN
