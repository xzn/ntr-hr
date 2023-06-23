/*
 * Copyright (c) 2006 Konstantin Shishkov
 * Copyright (c) 2007 Loren Merritt
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

#include <stdint.h>

#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/qsort.h"

#include "huffman.h"
#include "vlc.h"

/* symbol for Huffman tree node */
#define HNODE -1

static void heap_sift(HuffHeapElem *h, int root, int size)
{
    while (root * 2 + 1 < size) {
        int child = root * 2 + 1;
        if (child < size - 1 && h[child].val > h[child+1].val)
            child++;
        if (h[root].val > h[child].val) {
            FFSWAP(HuffHeapElem, h[root], h[child]);
            root = child;
        } else
            break;
    }
}

int ff_huff_gen_len_table(struct rp_huff_ctx *ctx, uint8_t *dst, const uint32_t *stats)
{
    const uint32_t stats_size = 256;
    HuffHeapElem *h  = ctx->h;
    int *up      = ctx->up;
    uint8_t *len = ctx->len;
    uint16_t *map= ctx->map;
    int offset, i, next;
    int size = 0;
    int ret = 0;

    for (i = 0; i<stats_size; i++) {
        dst[i] = 255;
        if (stats[i])
            map[size++] = i;
    }

    for (offset = 1; ; offset <<= 1) {
        for (i=0; i < size; i++) {
            h[i].name = i;
            h[i].val = (stats[map[i]] << 14) + offset;
        }
        for (i = size / 2 - 1; i >= 0; i--)
            heap_sift(h, i, size);

        for (next = size; next < size * 2 - 1; next++) {
            // merge the two smallest entries, and put it back in the heap
            uint64_t min1v = h[0].val;
            up[h[0].name] = next;
            h[0].val = INT64_MAX;
            heap_sift(h, 0, size);
            up[h[0].name] = next;
            h[0].name = next;
            h[0].val += min1v;
            heap_sift(h, 0, size);
        }

        len[2 * size - 2] = 0;
        for (i = 2 * size - 3; i >= size; i--)
            len[i] = len[up[i]] + 1;
        for (i = 0; i < size; i++) {
            dst[map[i]] = len[up[i]] + 1;
            if (dst[map[i]] >= 32) break;
        }
        if (i==size) break;
    }

    return ret;
}
