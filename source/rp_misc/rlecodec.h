#ifndef RLECODEC_H
#define RLECODEC_H

#include <stdint.h>

#define RLE_MIN_RUN 4
#define RLE_MAX_RUN (127 + RLE_MIN_RUN)
#define RLE_MIN_LIT 1
#define RLE_MAX_LIT (127 + RLE_MIN_LIT)

#define RLE_MAX_COMPRESSED_SIZE(src_size) (((src_size) + RLE_MAX_LIT - 1) / RLE_MAX_LIT + (src_size))

struct rp_rle_encode_ctx_t {
    const uint8_t *dst_begin;
    int curr_val, curr_len, next_len, next_good;
    uint8_t *next_dst, *dst;
};

void rle_encode_init(struct rp_rle_encode_ctx_t *ctx, uint8_t *dst);
int rle_encode_end(struct rp_rle_encode_ctx_t *ctx);
void rle_encode_next(struct rp_rle_encode_ctx_t *ctx, const int next_val);

#endif
