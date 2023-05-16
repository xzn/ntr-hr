#include <stdint.h>

typedef struct HuffmanHeapElem
{
    uint64_t val;
    int name;
} HuffmanHeapElem;

typedef struct HuffmanEntry
{
    uint16_t sym;
    uint8_t len;
    uint32_t code;
} HuffmanEntry;

struct huffman_alloc_s
{
    HuffmanHeapElem h[256];
    int up[512];
    uint8_t len[512];
    uint16_t map[256];
    
    void *stack[64][2];

    uint32_t counts[256];
    HuffmanEntry he[256];
};

int huffman_malloc_usage();
uint32_t *huffman_len_table(struct huffman_alloc_s *alloc, uint8_t *dst, const uint8_t *src, int src_size); // return usage table, write len table to dst (256 entries)
int huffman_encode_with_len_table(struct huffman_alloc_s *alloc, const uint32_t *counts, uint8_t *dst, const uint8_t *src, int src_size);
int huffman_encode(struct huffman_alloc_s *alloc, uint8_t *dst, const uint8_t *src, int src_size);
int huffman_compressed_size(const uint32_t *counts, const uint8_t *lens); // does not include lens
int huffman_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size);
