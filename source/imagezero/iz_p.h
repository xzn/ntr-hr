#ifndef IZ_IZ_P_H
#define IZ_IZ_P_H 1

#include "intmacros.h"
#include "bitcoder.h"
#include "pixel.h"
#include "image.h"

namespace IZ {

#define CONTEXT_COUNT 9
#define CONTEXT_BITS 4
#define CONTEXT_SIZE_REGULAR 1
#define CONTEXT_SIZE_RLE 3

#define MAX_CODE_LENGTH 6
#define MAX_CODE_VALUE (1 << MAX_CODE_LENGTH)

extern unsigned int staticdBits[1 << (2 * CONTEXT_BITS)];
extern const unsigned int staticdCount[1 << (2 * CONTEXT_BITS)];
extern char decodeTable[1 << CONTEXT_BITS][MAX_CODE_VALUE];

} // namespace IZ

#endif
