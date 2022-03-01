#include <huffmancodec.h>
#include "commondef.h"
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

#define rpAllocBuff_h (sizeof(HuffmanHeapElem) * 256)
#define rpAllocBuff_up (rpAllocBuff_h + sizeof(int) * 2 * 256)
#define rpAllocBuff_len (rpAllocBuff_up + sizeof(uint8_t) * 2 * 256)
#define rpAllocBuff_map (rpAllocBuff_len + sizeof(uint16_t) * 256)

static inline void ff_huff_gen_len_table(uint8_t *alloc, uint8_t *dst, const uint32_t *counts)
{
    // HuffmanHeapElem *h = HR_MALLOC(sizeof(*h) * 256);
    // int *up = HR_MALLOC(sizeof(*up) * 2 * 256);
    // uint8_t *len = HR_MALLOC(sizeof(*len) * 2 * 256);
    // uint16_t *map = HR_MALLOC(sizeof(*map) * 256);

    // HuffmanHeapElem *h = alloc;
    // int *up = alloc + 0x800;
    // uint8_t *len = alloc + 0x800 + 0x800;
    // uint16_t *map = alloc + 0x800 + 0x800 + 0x200;

    HuffmanHeapElem *h = alloc;
    int *up = alloc + rpAllocBuff_h;
    uint8_t *len = alloc + rpAllocBuff_up;
    uint16_t *map = alloc + rpAllocBuff_len;

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

#define rpAllocBuff_counts (rpAllocBuff_map + sizeof(uint32_t) * 256)
#define rpAllocBuff_he (rpAllocBuff_counts + sizeof(HuffmanEntry) * 256)

#include "qsort.h"

static inline void calculate_codes(uint8_t *alloc, HuffmanEntry *he)
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
    // count = put_bits_count(&pb) & 0x1F;

    // if (count)
    //     put_bits(&pb, 32 - count, 0);

    /* Flush the rest with zeroes */
    flush_put_bits(&pb);

    return put_bytes_output(&pb);
}

uint32_t *huffman_len_table(uint8_t *alloc, uint8_t *dst, const uint8_t *src, int src_size)
{
    uint32_t *counts = alloc + rpAllocBuff_map;

    memset(counts, 0, sizeof(*counts) * 256);

    count_usage(src, src_size, counts);

    ff_huff_gen_len_table(alloc, dst, counts);

    return counts;
}

int huffman_encode_with_len_table(uint8_t *alloc, const uint32_t *counts, uint8_t *dst, const uint8_t *src, int src_size)
{
    HuffmanEntry *he = alloc + rpAllocBuff_counts;

    int i;
    for (i = 0; i < 256; ++i)
    {
        he[i].len = *dst++;
        he[i].sym = i;
    }

    calculate_codes(alloc, he);

    int ret = write_huff_codes(dst, src, src_size, he);

    return 256 + ret;
}

int huffman_encode(uint8_t *alloc, uint8_t *dst, const uint8_t *src, int src_size)
{
    // uint32_t *counts = HR_MALLOC(sizeof(*counts) * 256);
    // HuffmanEntry *he = HR_MALLOC(sizeof(*he) * 256);

    // uint32_t *counts = alloc + 0x800 + 0x800 + 0x200 + 0x200;
    // HuffmanEntry *he = alloc + 0x800 + 0x800 + 0x200 + 0x200 + 0x400;

    return huffman_encode_with_len_table(alloc, huffman_len_table(alloc, dst, src, src_size), dst, src, src_size);

    // HR_FREE(he);
    // HR_FREE(counts);
}

int huffman_compressed_size(const uint32_t *counts, const uint8_t *lens)
{
    int i = 0, n = 0;
    for (; i < 256; ++i) {
        n += counts[i] * lens[i];
    }
    // return (n + 31) / 32;
    return (n + 7) / 8;
}

int huffman_malloc_usage()
{
    // should be 0x2200
    return rpAllocBuff_stack;
}
