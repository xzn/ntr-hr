#include <stdint.h>

int huffman_malloc_usage();
uint32_t *huffman_len_table(uint8_t *dst, const uint8_t *src, int src_size); // return usage table, write len table to dst (256 entries)
int huffman_encode_with_len_table(const uint32_t *counts, uint8_t *dst, const uint8_t *src, int src_size);
int huffman_encode(uint8_t *dst, const uint8_t *src, int src_size);
int huffman_compressed_size(const uint32_t *counts, const uint8_t *lens); // does not include lens
int huffman_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size);
