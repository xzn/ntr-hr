#ifndef INTMACROS_H
#define INTMACROS_H

#include <climits>

// Shift to replicate the sign bit into all bits
#define SS (sizeof(int) * CHAR_BIT - 1)

static inline unsigned int bitMask(unsigned int bitCount)
{
    // return pow(2, bitCount) - 1
    return (1U << bitCount) - 1;
}

// return signBit
static inline unsigned int signBit(int v)
{
//    return v < 0 ? 1 : 0;
    return (unsigned int)(v) >> SS;
}

// return -1, if exactly one of v1 and v2 is negative, 0 otherwise
static inline int oppositeSign(int v1, int v2)
{
//    return (v1 ^ v2) < 0 ? -1 : 0;
    return (v1 ^ v2) >> SS;
}

// return v0 if s is 0, return v1 if s is -1, otherwise the behavior is undefined
static inline int selectVal(int s, int v0, int v1)
{
//    return !s ? v0 : v1;
    return (~s & v0) | (s & v1);
}

// clamp to a maximum value
static inline int clampMax(int v, int max)
{
//    return v > max ? max : v;
    return max + ((v - max) & ((v - max) >> SS));
}

// clamp to a minimum value
static inline int clampMin(int v, int min)
{
//    return v < min ? min : v;
    return min - ((min - v) & ((min - v) >> SS));
}

// clamp to zero
static inline int clamp0(int v)
{
//    return v < 0 ? 0 : v;
    return v & (-v >> SS);
}

// clamp to unsigned byte (0 ... 255)
static inline int clampByte(int v)
{
//    return v < 0 ? 0 : v > 255 ? 255 : v;
    return clampMax(clamp0(v), 255);
}

// cancel the value, if the condition value is not positive
static inline int cancelValue(int v, int v0)
{
//    return v0 > 0 ? v : 0;
    return v & (-v0 >> SS);
}

// return absolute value
static inline unsigned int absValue(int v)
{
//    return v < 0 ? -v : v;
    return (v + (v >> SS)) ^ (v >> SS);
}

// bit scan reverse
static inline unsigned int bsr(unsigned int w)
{
//    asm ("bsr %1,%0" : "=r" (w) : "rm" (w)); return w;
//    int k = -1; while (w > 0) { w >>= 1; ++k; } return k;
    return __builtin_clz(w) ^ SS;
}

// number of bits required for unsigned storage (0->0, 1->1, 2...3->2, 4...7->3, etc.
static inline unsigned int numBits(unsigned int v)
{
//    return v > 0 ? 1 + log2(v) : 0;
//    unsigned int k = 0; while (v > 0) { v >>= 1; ++k; } return k;
    return cancelValue(1 + bsr(v), v);
}

// signed to unsigned storage
static inline unsigned int s2u(int s)
{
//    return s > 0 ? (s << 1) : (-s << 1) - 1;
    return (s << 1) ^ (s >> SS);
}

// unsigned storage to signed
static inline int u2s(unsigned int u)
{
//    return u & 1 ? -(u >> 1) - 1 : (u >> 1);
    return (u >> 1) ^ (-(u & 1));
}

#endif
