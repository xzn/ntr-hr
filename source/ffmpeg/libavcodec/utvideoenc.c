/*
 * Ut Video encoder
 * Copyright (c) 2012 Jan Ekström
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Ut Video encoder
 */
#include "libavutil/common.h"
#include "huffman.h"

/* Compare huffman tree nodes */
static int ut_huff_cmp_len(const void *a, const void *b)
{
    const HuffEntry *aa = a, *bb = b;
    return (aa->len - bb->len)*256 + aa->sym - bb->sym;
}

/* Compare huffentry symbols */
static int huff_cmp_sym(const void *a, const void *b)
{
    const HuffEntry *aa = a, *bb = b;
    return aa->sym - bb->sym;
}

/* Count the usage of values in a plane */
static void count_usage(const uint8_t *src, int src_size, uint32_t *counts)
{
    const uint8_t *src_end = src + src_size;
    while (src != src_end)
        ++counts[*src++];
}

/* Calculate the actual huffman codes from the code lengths */
static void calculate_codes(struct rp_huff_ctx *ctx, HuffEntry *he)
{
    int last, i;
    uint32_t code;

    qsort(he, 256, sizeof(*he), ut_huff_cmp_len);

    last = 255;
    while (he[last].len == 255 && last)
        last--;

    code = 0;
    for (i = last; i >= 0; i--) {
        he[i].code  = code >> (32 - he[i].len);
        code       += 0x80000000u >> (he[i].len - 1);
    }

    qsort(he, 256, sizeof(*he), huff_cmp_sym);
}

/* Write huffman bit codes to a memory block */
static int write_huff_codes(const uint8_t *src, PutBitContext *pb,
                            int src_size, HuffEntry *he)
{
    const uint8_t *src_end = src + src_size;
    int i, j;
    int count;
    int ret;

    /* Write the codes */
    while (src != src_end) {
        put_bits_checked(pb, he[src[i]].len, he[src[i]].code);
        ++src;
    }

    return 0;
}

int huff_len_table(struct rp_huff_ctx *ctx, PutBitContext *pb, const uint8_t *src, int src_size)
{
    if (pb->buf_ptr + 256 > pb->buf_end)
        return -1;

    uint32_t *counts = ctx->counts;

    memset(counts, 0, sizeof(*counts) * 256);

    count_usage(src, src_size, counts);

    ff_huff_gen_len_table(ctx, pb->buf_ptr, counts);

    return 0;
}

int huff_encode_with_len_table(struct rp_huff_ctx *ctx, PutBitContext *pb, const uint8_t *src, int src_size)
{
    HuffEntry *he = ctx->he;
    uint32_t *counts = ctx->counts;

    int i;
    for (i = 0; i < 256; ++i)
    {
        he[i].len = *pb->buf_ptr++;
        he[i].sym = i;
    }

    calculate_codes(ctx, he);

    return write_huff_codes(src, pb, src_size, he);
}

int huff_encode(struct rp_huff_ctx *ctx, PutBitContext *pb, const uint8_t *src, int src_size)
{
    if (huff_len_table(ctx, pb, src, src_size) < 0)
        return -1;
    return huff_encode_with_len_table(ctx, pb, src, src_size);
}
