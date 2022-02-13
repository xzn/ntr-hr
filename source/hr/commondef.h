#include <stdint.h>

#define HR_MALLOC rpMalloc
#define HR_FREE rpFree
#define HR_REALLOC
extern uint8_t *rpAllocBuff;

#if !defined(HR_MALLOC) || !defined(HR_FREE) || !defined(HR_REALLOC)
#include <stdlib.h>
#define HR_MALLOC malloc
#define HR_FREE free
#define HR_REALLOC realloc
#endif

#define av_log(...)
#define av_assert0(...)
#define av_assert1(...)
#define av_assert2(...)
#define ff_dlog(...)

#define FFMIN(a, b) ((a) > (b) ? (b) : (a))
#define FFMAX(a, b) ((a) > (b) ? (a) : (b))

#define FFSWAP(type, a, b) \
    do                     \
    {                      \
        type SWAP_tmp = b; \
        b = a;             \
        a = SWAP_tmp;      \
    } while (0)

#define AV_WL32(p, val)                  \
    do                                   \
    {                                    \
        uint32_t d = (val);              \
        ((uint8_t *)(p))[0] = (d);       \
        ((uint8_t *)(p))[1] = (d) >> 8;  \
        ((uint8_t *)(p))[2] = (d) >> 16; \
        ((uint8_t *)(p))[3] = (d) >> 24; \
    } while (0)

#define AV_WB32(p, val)                  \
    do                                   \
    {                                    \
        uint32_t d = (val);              \
        ((uint8_t *)(p))[3] = (d);       \
        ((uint8_t *)(p))[2] = (d) >> 8;  \
        ((uint8_t *)(p))[1] = (d) >> 16; \
        ((uint8_t *)(p))[0] = (d) >> 24; \
    } while (0)

#define AV_WLBUF AV_WL32
#define AV_WBBUF AV_WB32
