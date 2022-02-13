#include <stdint.h>

int huffman_encode(uint8_t *dst, const uint8_t *src, int src_size);
int huffman_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size);
