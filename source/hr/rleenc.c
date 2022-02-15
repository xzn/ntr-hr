#include <rlecodec.h>
#include "rledef.h"

int rle_encode(uint8_t *dst, const uint8_t *src, int src_size)
{
    const uint8_t *dst_begin = dst, *src_end = src + src_size;
    int curr_val = -1, next_val = 0, curr_len = 0, next_len = 0, next_good = 1;

    uint8_t *next_dst = dst++;

#define RLE_ENCODE_STOP_RUN                           \
    do                                                \
    {                                                 \
        if (next_len > 0)                             \
        {                                             \
            *next_dst = next_len - RLE_MIN_LIT - 128; \
            next_len = 0;                             \
        }                                             \
        else                                          \
        {                                             \
            --dst;                                    \
        }                                             \
        *dst++ = curr_len - RLE_MIN_RUN;              \
        *dst++ = curr_val;                            \
        next_dst = dst++;                             \
    } while (0)

    do
    {
        next_good = src != src_end;
        next_val = next_good ? *src++ : -2;

        // start run
        if (next_val == curr_val)
        {
            ++curr_len;
            if (curr_len == RLE_MAX_RUN)
            {
                // restart run
                RLE_ENCODE_STOP_RUN;
                curr_val = -1;
                curr_len = 0;
            }
        }
        else
        {
            if (curr_len >= RLE_MIN_RUN)
            {
                // stop run
                RLE_ENCODE_STOP_RUN;
            }
            else
            {
                // no run
                while (curr_len)
                {
                    *dst++ = curr_val;
                    --curr_len;

                    ++next_len;
                    if (next_len == RLE_MAX_LIT || (!next_good && !curr_len))
                    {
                        *next_dst = next_len - RLE_MIN_LIT - 128;
                        next_len = 0;
                        next_dst = dst++;
                    }
                }
            }

            curr_val = next_val;
            curr_len = 1;
        }
    } while (next_good);

#undef RLE_ENCODE_STOP_RUN

    return --dst - dst_begin;
}

int rle_max_compressed_size(int src_size)
{
    return (src_size + RLE_MAX_LIT - 1) / RLE_MAX_LIT + src_size;
}
