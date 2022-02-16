#include <stdint.h>

#define RLE_MIN_RUN 4
#define RLE_MAX_RUN (127 + RLE_MIN_RUN)
#define RLE_MIN_LIT 1
#define RLE_MAX_LIT (127 + RLE_MIN_LIT)

#define rle_max_compressed_size(src_size) (((src_size) + RLE_MAX_LIT - 1) / RLE_MAX_LIT + (src_size))

int rle_encode(uint8_t *dst, const uint8_t *src, int src_size);
int rle_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size);
