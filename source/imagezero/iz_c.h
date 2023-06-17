#ifndef IZ_C_H
#define IZ_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t Cache_t;
typedef uint32_t Code_def_t;

struct rp_jls_send_ctx_t;
struct BitCoderPtrs {
    Code_def_t *p, *p_end;
    struct rp_jls_send_ctx_t *user;
};

void izInitDecodeTable();
void izInitEncodeTable();

int izDecodeImageRGB(uint8_t *dst, const uint8_t *src, int width, int height);
int izEncodeImageRGB(struct BitCoderPtrs *dst, const uint8_t *src, int width, int height, int pitch);

int izBitCoderFlush(struct BitCoderPtrs *ctx);

#ifdef __cplusplus
}
#endif

#endif
