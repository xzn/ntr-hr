#include <stdint.h>

int rle_encode(uint8_t *dst, const uint8_t *src, int src_size);
int rle_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size);
