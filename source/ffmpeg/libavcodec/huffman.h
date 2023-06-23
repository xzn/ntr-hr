/*
 * Copyright (C) 2007  Aurelien Jacobs <aurel@gnuage.org>
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
 * huffman tree builder and VLC generator
 */

#ifndef AVCODEC_HUFFMAN_H
#define AVCODEC_HUFFMAN_H

#include <stdint.h>

#include "put_bits.h"
#include "vlc.h"

typedef struct HuffNode {
    int16_t  sym;
    int16_t  n0;
    uint32_t count;
} HuffNode;

typedef struct HuffHeapElem {
    uint64_t val;
    int name;
} HuffHeapElem;

typedef struct HuffEntry {
    uint16_t sym;
    uint8_t  len;
    uint32_t code;
} HuffEntry;

struct rp_huff_ctx
{
    HuffHeapElem h[256];
    int up[512];
    uint8_t len[512];
    uint16_t map[256];

    // void *stack[64][2];

    uint32_t counts[256];
    HuffEntry he[256];
};

#define FF_HUFFMAN_FLAG_HNODE_FIRST 0x01
#define FF_HUFFMAN_FLAG_ZERO_COUNT  0x02
#define FF_HUFFMAN_BITS 10

typedef int (*HuffCmp)(const void *va, const void *vb);
int ff_huff_gen_len_table(struct rp_huff_ctx *ctx, uint8_t *dst, const uint32_t *stats);

int huff_len_table(struct rp_huff_ctx *ctx, PutBitContext *pb, const uint8_t *src, int src_size); // return usage table, write len table to dst (256 entries)
int huff_encode_with_len_table(struct rp_huff_ctx *ctx, PutBitContext *pb, const uint8_t *src, int src_size);
int huff_encode(struct rp_huff_ctx *ctx, PutBitContext *pb, const uint8_t *src, int src_size);

#endif /* AVCODEC_HUFFMAN_H */
