#include <cstdlib>
// #include <cstdio>

#include "iz_p.h"

#define DECODE_TABLE 0

namespace IZ {

const unsigned int staticdCount[1 << (2 * CONTEXT_BITS)] =
{
#if MAX_CODE_LENGTH >= 6
//  0, 1, 2, 3, 4, 5, 6, 7, 8
    1, 3, 2, 5, 5, 6, 6, 6, 6,   0, 0, 0, 0, 0, 0, 0,
    3, 2, 2, 2, 4, 6, 6, 6, 6,   0, 0, 0, 0, 0, 0, 0,
    4, 2, 2, 2, 3, 6, 6, 6, 6,   0, 0, 0, 0, 0, 0, 0,
    6, 4, 2, 2, 2, 3, 6, 6, 6,   0, 0, 0, 0, 0, 0, 0,
    6, 6, 3, 2, 2, 2, 4, 6, 6,   0, 0, 0, 0, 0, 0, 0,
    6, 6, 4, 2, 2, 2, 3, 6, 6,   0, 0, 0, 0, 0, 0, 0,
    6, 6, 6, 4, 2, 2, 2, 3, 6,   0, 0, 0, 0, 0, 0, 0,
    6, 6, 6, 6, 3, 2, 2, 2, 4,   0, 0, 0, 0, 0, 0, 0,
    6, 6, 5, 5, 5, 3, 2, 2, 2,   0, 0, 0, 0, 0, 0, 0,
#else
    // TODO also need static tables for
    // maximum code lengths of 5, 7, and 8
    1, 4, 4, 4, 4, 4, 4, 4, 4,   0, 0, 0, 0, 0, 0, 0,
    3, 3, 3, 3, 3, 3, 3, 4, 4,   0, 0, 0, 0, 0, 0, 0,
    3, 3, 3, 3, 3, 3, 3, 4, 4,   0, 0, 0, 0, 0, 0, 0,
    4, 3, 3, 3, 3, 3, 3, 3, 4,   0, 0, 0, 0, 0, 0, 0,
    4, 3, 3, 3, 3, 3, 3, 3, 4,   0, 0, 0, 0, 0, 0, 0,
    4, 3, 3, 3, 3, 3, 3, 3, 4,   0, 0, 0, 0, 0, 0, 0,
    4, 4, 3, 3, 3, 3, 3, 3, 3,   0, 0, 0, 0, 0, 0, 0,
    4, 4, 3, 3, 3, 3, 3, 3, 3,   0, 0, 0, 0, 0, 0, 0,
    4, 4, 3, 3, 3, 3, 3, 3, 3,   0, 0, 0, 0, 0, 0, 0,
#endif
};

unsigned int staticdBits[1 << (2 * CONTEXT_BITS)];

#if DECODE_TABLE
char decodeTable[1 << CONTEXT_BITS][MAX_CODE_VALUE];
#endif

static int comp_int(const void *p1, const void *p2)
{
  return *((const int *) p1) - *((const int *) p2);
}

static void initBitsTable()
{
    int sorted[CONTEXT_COUNT];
    for (int pc = 0; pc < CONTEXT_COUNT; ++pc) {
        int *p = sorted;
        for (int nc = 0; nc < CONTEXT_COUNT; ++nc) {
            unsigned int bits = staticdCount[(pc << CONTEXT_BITS) + nc];
            *p++ = (bits << CONTEXT_BITS) + nc;
        }
        qsort(sorted, CONTEXT_COUNT, sizeof(int), comp_int);
        unsigned int v = 0;
//        printf("%d: ", pc);
        for (int c = 0; c < CONTEXT_COUNT; ++c) {
            unsigned int bits = sorted[c] >> CONTEXT_BITS;
            unsigned int nc = sorted[c] & bitMask(CONTEXT_BITS);
//            printf("(%d,%d,%2d) ", bits, nc, v);
            staticdBits[(pc << CONTEXT_BITS) + nc] = v;
            ++v;
            if (c < CONTEXT_COUNT - 1) {
                v <<= (sorted[c + 1] >> CONTEXT_BITS) - bits;
            }
        }
//        printf("\n");
    }
}

void initEncodeTable()
{
    initBitsTable();
}

//#define CHECKTABLE

#if DECODE_TABLE
void initDecodeTable()
{
    initBitsTable();
#ifdef CHECKTABLE
    for (unsigned int pl = 0; pl < CONTEXT_COUNT; ++pl) {
        for (unsigned int v = 0; v < MAX_CODE_VALUE; ++v) {
            decodeTable[pl][v] = 100;
        }
    }
#endif
    for (unsigned int pl = 0; pl < CONTEXT_COUNT; ++pl) {
        for (unsigned int v = 0; v < MAX_CODE_VALUE; ++v) {
            for (unsigned int nl = 0; nl < CONTEXT_COUNT; ++nl) {
                int bits = staticdCount[(pl << CONTEXT_BITS) + nl];
                if ((v >> (MAX_CODE_LENGTH - bits)) == staticdBits[(pl << CONTEXT_BITS) + nl]) {
#ifdef CHECKTABLE
                    if (decodeTable[pl][v] == 100) {
                        decodeTable[pl][v] = nl;
                    } else {
                        printf("ERROR duplicate value\n");
                        exit(1);
                    }
#else
                    decodeTable[pl][v] = nl;
                    break;
#endif
                }
            }
        }
    }
#ifdef CHECKTABLE
    for (unsigned int pl = 0; pl < CONTEXT_COUNT; ++pl) {
        printf("%d: ", pl);
        for (unsigned int v = 0; v < MAX_CODE_VALUE; ++v) {
            if (decodeTable[pl][v] == 100) {
                printf("ERROR missing value %d\n", v);
                exit(1);
            }
            printf("%d", decodeTable[pl][v]);
        }
        printf("\n");
    }
#endif
}
#endif

} // namespace IZ
