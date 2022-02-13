#include <huffmancodec.h>
#include "commondef.h"
#include "qsort.h"
#include "bitenc.h"

#include <string.h>

typedef struct HuffmanHeapElem
{
    uint32_t val;
    int name;
} HuffmanHeapElem;

typedef struct HuffmanEntry
{
    uint16_t sym;
    uint8_t len;
    uint32_t code;
} HuffmanEntry;

static inline void count_usage(const uint8_t *src, int src_size, uint32_t *counts)
{
    const uint8_t *src_end = src + src_size;
    while (src != src_end)
    {
        ++counts[*src++];
    }
}

static inline void heap_sift(HuffmanHeapElem *h, int root, int size)
{
    while (root * 2 + 1 < size)
    {
        int child = root * 2 + 1;
        if (child < size - 1 && h[child].val > h[child + 1].val)
            child++;
        if (h[root].val > h[child].val)
        {
            FFSWAP(HuffmanHeapElem, h[root], h[child]);
            root = child;
        }
        else
            break;
    }
}

static inline void ff_huff_gen_len_table(uint8_t *dst, const uint32_t *counts)
{
    // HuffmanHeapElem *h = HR_MALLOC(sizeof(*h) * 256);
    // int *up = HR_MALLOC(sizeof(*up) * 2 * 256);
    // uint8_t *len = HR_MALLOC(sizeof(*len) * 2 * 256);
    // uint16_t *map = HR_MALLOC(sizeof(*map) * 256);

    HuffmanHeapElem *h = rpAllocBuff;
    int *up = rpAllocBuff + 0x800;
    uint8_t *len = rpAllocBuff + 0x800 + 0x800;
    uint16_t *map = rpAllocBuff + 0x800 + 0x800 + 0x200;

    int offset, i, next;
    int size = 0;
    int ret = 0;

    for (i = 0; i < 256; ++i)
    {
        dst[i] = 255;
        if (counts[i])
            map[size++] = i;
    }

    for (offset = 1;; offset <<= 1)
    {
        for (i = 0; i < size; ++i)
        {
            h[i].name = i;
            h[i].val = (counts[map[i]] << 14) + offset;
        }
        for (i = size / 2 - 1; i >= 0; --i)
            heap_sift(h, i, size);

        for (next = size; next < size * 2 - 1; ++next)
        {
            // merge the two smallest entries, and put it back in the heap
            uint32_t min1v = h[0].val;
            up[h[0].name] = next;
            h[0].val = INT32_MAX;
            heap_sift(h, 0, size);
            up[h[0].name] = next;
            h[0].name = next;
            h[0].val += min1v;
            heap_sift(h, 0, size);
        }

        len[2 * size - 2] = 0;
        for (i = 2 * size - 3; i >= size; --i)
            len[i] = len[up[i]] + 1;
        for (i = 0; i < size; ++i)
        {
            dst[map[i]] = len[up[i]] + 1;
            if (dst[map[i]] >= 32)
                break;
        }
        if (i == size)
            break;
    }

    // HR_FREE(map);
    // HR_FREE(len);
    // HR_FREE(up);
    // HR_FREE(h);
}

/* Compare huffman tree nodes */
static inline int ut_huff_cmp_len(const void *a, const void *b)
{
    const HuffmanEntry *aa = a, *bb = b;
    return (aa->len - bb->len) * 256 + aa->sym - bb->sym;
}

/* Compare huffentry symbols */
static inline int huff_cmp_sym(const void *a, const void *b)
{
    const HuffmanEntry *aa = a, *bb = b;
    return aa->sym - bb->sym;
}

static inline void calculate_codes(HuffmanEntry *he)
{
    int last, i;
    uint32_t code;

    AV_QSORT(he, 256, HuffmanEntry, ut_huff_cmp_len);
    // qsort(he, 256, sizeof(HuffmanEntry), ut_huff_cmp_len);

    last = 255;
    while (he[last].len == 255 && last)
        last--;

    code = 0;
    for (i = last; i >= 0; --i)
    {
        he[i].code = code >> (32 - he[i].len);
        code += 0x80000000u >> (he[i].len - 1);
    }

    AV_QSORT(he, 256, HuffmanEntry, huff_cmp_sym);
    // qsort(he, 256, sizeof(HuffmanEntry), huff_cmp_sym);
}

static inline int write_huff_codes(uint8_t *dst, const uint8_t *src, int src_size, HuffmanEntry *he)
{
    PutBitContext pb;
    int count;
    const uint8_t *src_end = src + src_size;

    init_put_bits(&pb, dst);

    /* Write the codes */
    while (src != src_end)
    {
        put_bits(&pb, he[*src].len, he[*src].code);
        ++src;
    }

    /* Pad output to a 32-bit boundary */
    count = put_bits_count(&pb) & 0x1F;

    if (count)
        put_bits(&pb, 32 - count, 0);

    /* Flush the rest with zeroes */
    flush_put_bits(&pb);

    return put_bytes_output(&pb);
}

int huffman_encode(uint8_t *dst, const uint8_t *src, int src_size)
{
    // uint32_t *counts = HR_MALLOC(sizeof(*counts) * 256);
    // HuffmanEntry *he = HR_MALLOC(sizeof(*he) * 256);
    uint32_t *counts = rpAllocBuff + 0x800 + 0x800 + 0x200 + 0x200;
    HuffmanEntry *he = rpAllocBuff + 0x800 + 0x800 + 0x200 + 0x200 + 0x400;
    memset(counts, 0, sizeof(*counts) * 256);
    int i;

    count_usage(src, src_size, counts);

    ff_huff_gen_len_table(dst, counts);

    for (i = 0; i < 256; ++i)
    {
        he[i].len = *dst++;
        he[i].sym = i;
    }

    calculate_codes(he);

    int ret = write_huff_codes(dst, src, src_size, he);

    // HR_FREE(he);
    // HR_FREE(counts);

    return 256 + ret;
}
