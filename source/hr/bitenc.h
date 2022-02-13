#include <stdint.h>

// #define BITSTREAM_WRITER_LE

typedef uint32_t BitBuf;
#define BUF_BITS (8 * sizeof(BitBuf))
typedef struct PutBitContext
{
    BitBuf bit_buf;
    int bit_left;
    uint8_t *buf, *buf_ptr;
} PutBitContext;

static inline void init_put_bits(PutBitContext *s, uint8_t *buffer)
{
    s->buf = buffer;
    s->buf_ptr = s->buf;
    s->bit_left = BUF_BITS;
    s->bit_buf = 0;
}

static inline void put_bits(PutBitContext *s, int n, BitBuf value)
{
    BitBuf bit_buf;
    int bit_left;

    bit_buf = s->bit_buf;
    bit_left = s->bit_left;

    /* XXX: optimize */
#ifdef BITSTREAM_WRITER_LE
    bit_buf |= value << (BUF_BITS - bit_left);
    if (n >= bit_left)
    {
        AV_WLBUF(s->buf_ptr, bit_buf);
        s->buf_ptr += sizeof(BitBuf);

        bit_buf = value >> bit_left;
        bit_left += BUF_BITS;
    }
    bit_left -= n;
#else
    if (n < bit_left)
    {
        bit_buf = (bit_buf << n) | value;
        bit_left -= n;
    }
    else
    {
        bit_buf <<= bit_left;
        bit_buf |= value >> (n - bit_left);

        AV_WBBUF(s->buf_ptr, bit_buf);
        s->buf_ptr += sizeof(BitBuf);

        bit_left += BUF_BITS - n;
        bit_buf = value;
    }
#endif

    s->bit_buf = bit_buf;
    s->bit_left = bit_left;
}

static inline int put_bits_count(PutBitContext *s)
{
    return (s->buf_ptr - s->buf) * 8 + BUF_BITS - s->bit_left;
}

static inline void flush_put_bits(PutBitContext *s)
{
#ifndef BITSTREAM_WRITER_LE
    if (s->bit_left < BUF_BITS)
        s->bit_buf <<= s->bit_left;
#endif
    while (s->bit_left < BUF_BITS)
    {
#ifdef BITSTREAM_WRITER_LE
        *s->buf_ptr++ = s->bit_buf;
        s->bit_buf >>= 8;
#else
        *s->buf_ptr++ = s->bit_buf >> (BUF_BITS - 8);
        s->bit_buf <<= 8;
#endif
        s->bit_left += 8;
    }
    s->bit_left = BUF_BITS;
    s->bit_buf = 0;
}

static inline int put_bytes_output(const PutBitContext *s)
{
    return s->buf_ptr - s->buf;
}
