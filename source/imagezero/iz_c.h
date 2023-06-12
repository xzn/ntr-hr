#ifndef IZ_C_H
#define IZ_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t Cache_t;
typedef uint32_t Code_def_t;

struct BitCoderPtrs {
    Code_def_t *p, *p_end;
    void *user;
    int (*flush)(struct BitCoderPtrs *);
};

void izInitDecodeTable();
void izInitEncodeTable();

int izDecodeImageRGB(uint8_t *dst, const uint8_t *src, int width, int height);
int izEncodeImageRGB(struct BitCoderPtrs *dst, const uint8_t *src, int width, int height, int pitch);

#ifdef __cplusplus
}
#endif

#endif
