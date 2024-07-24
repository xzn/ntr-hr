/*
    Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of Fecal nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

/*
    This module provides core tools and constants used by the codec:

    + Debugging macros
    + Alignment
    + PCGRandom, Int32Hash
    + Parameters of the Siamese and Cauchy matrix structures
    + ICodec base class for Encoder and Decoder
    + EncoderAppDataWindow and DecoderAppDataWindow structures
    + Growing matrix structure
    + CustomBitSet
*/

#ifdef _WIN32
    #include <intrin.h>
#endif

#include "fecal.h"
#include "gf256.h"
#include "constants.h"

#include <array>
#include <algorithm>

namespace fecal {


//------------------------------------------------------------------------------
// Debug

// Some bugs only repro in release mode, so this can be helpful
//#define FECAL_DEBUG_IN_RELEASE

#if defined(_DEBUG) || defined(DEBUG) || defined(FECAL_DEBUG_IN_RELEASE)
    #define FECAL_DEBUG
    #ifdef _WIN32
        #define FECAL_DEBUG_BREAK __debugbreak()
    #else
        #define FECAL_DEBUG_BREAK __builtin_trap()
    #endif
    #define FECAL_DEBUG_ASSERT(cond) { if (!(cond)) { FECAL_DEBUG_BREAK; } }
#else
    #define FECAL_DEBUG_BREAK ;
    #define FECAL_DEBUG_ASSERT(cond) ;
#endif


//------------------------------------------------------------------------------
// PCG PRNG
// From http://www.pcg-random.org/

class PCGRandom
{
public:
    inline void Seed(uint64_t y, uint64_t x = 0)
    {
        State = 0;
        Inc = (y << 1u) | 1u;
        Next();
        State += x;
        Next();
    }

    inline uint32_t Next()
    {
        const uint64_t oldstate = State;
        State = oldstate * UINT64_C(6364136223846793005) + Inc;
        const uint32_t xorshifted = (uint32_t)(((oldstate >> 18) ^ oldstate) >> 27);
        const uint32_t rot = oldstate >> 59;
        return (xorshifted >> rot) | (xorshifted << ((uint32_t)(-(int32_t)rot) & 31));
    }

    uint64_t State = 0, Inc = 0;
};


//------------------------------------------------------------------------------
// Int32Hash

// Thomas Wang's 32-bit -> 32-bit integer hash function
// http://burtleburtle.net/bob/hash/integer.html
inline uint32_t Int32Hash(uint32_t key)
{
    key += ~(key << 15);
    key ^= (key >> 10);
    key += (key << 3);
    key ^= (key >> 6);
    key += ~(key << 11);
    key ^= (key >> 16);
    return key;
}


//------------------------------------------------------------------------------
// Code Parameters

// Number of values 3..255 that we cycle through
static const unsigned kColumnValuePeriod = 253;

// Number of values 1..255 that we cycle through
static const unsigned kRowValuePeriod = 255;


GF256_FORCE_INLINE uint8_t GetColumnValue(unsigned column)
{
    // Note: This LCG visits each value exactly once
    return (uint8_t)(3 + (column * 199) % kColumnValuePeriod);
}

GF256_FORCE_INLINE uint8_t GetRowValue(unsigned row)
{
    return (uint8_t)(1 + (row + 1) % kRowValuePeriod);
}


// Number of parallel lanes to run
// Lane#(Column) = Column % kColumnLaneCount
static const unsigned kColumnLaneCount = 1;

// Number of running sums of original data
// Note: This cannot be tuned without making code changes
static const unsigned kColumnSumCount = 3;
// Sum 0 = Parity XOR of all input data
// Sum 1 = Product #1 sum XOR of all input data times its GetColumnValue()
// Sum 2 = Product #2 sum XOR of all input data times its GetColumnValue() squared

// Rate at which we add random pairs of data
static const unsigned kPairAddRate = 16;


// Calculate operation code for the given row and lane
GF256_FORCE_INLINE unsigned GetRowOpcode(unsigned lane, unsigned row)
{
    FECAL_DEBUG_ASSERT(lane < kColumnLaneCount);
    static const uint32_t kSumMask = (1 << (kColumnSumCount * 2)) - 1;
    static const uint32_t kZeroValue = (1 << ((kColumnSumCount - 1) * 2));

    // This offset tunes the quality of the upper left of the generated matrix,
    // which is encountered in practice for the first block of input data
    static const unsigned kArbitraryOffset = 3;

    const uint32_t opcode = Int32Hash(lane + (row + kArbitraryOffset) * kColumnLaneCount) & kSumMask;
    return (opcode == 0) ? kZeroValue : (unsigned)opcode;
}


//------------------------------------------------------------------------------
// ICodec


//------------------------------------------------------------------------------
// AlignedDataBuffer
//
// Aligned to cache-line boundaries for SIMD

struct AlignedDataBuffer
{
    uint8_t Data[ARP_DATA_SIZE] ALIGNED(sizeof(GF256_M128));
};


//------------------------------------------------------------------------------
// GrowingAlignedByteMatrix
//
// This is a matrix of bytes where the elements are stored in row-first order
// and the first byte element of each row is aligned to cache-line boundaries.
// Furthermore the matrix can grow in rows or columns, keeping existing data.


//------------------------------------------------------------------------------
// Portable Intrinsics

// Returns number of bits set in the 64-bit value
GF256_FORCE_INLINE unsigned PopCount64(uint64_t x)
{
#ifdef _MSC_VER
#ifdef _WIN64
    return (unsigned)__popcnt64(x);
#else
    return (unsigned)(__popcnt((uint32_t)x) + __popcnt((uint32_t)(x >> 32)));
#endif
#else // GCC
    return (unsigned)__builtin_popcountll(x);
#endif
}

// Returns lowest bit index 0..63 where the first non-zero bit is found
// Precondition: x != 0
GF256_FORCE_INLINE unsigned FirstNonzeroBit64(uint64_t x)
{
#ifdef _MSC_VER
#ifdef _WIN64
    unsigned long index;
    // Note: Ignoring result because x != 0
    _BitScanForward64(&index, x);
    return (unsigned)index;
#else
    unsigned long index;
    if (0 != _BitScanForward(&index, (uint32_t)x))
        return (unsigned)index;
    // Note: Ignoring result because x != 0
    _BitScanForward(&index, (uint32_t)(x >> 32));
    return (unsigned)index + 32;
#endif
#else
    // Note: Ignoring return value of 0 because x != 0
    return (unsigned)__builtin_ffsll(x) - 1;
#endif
}


//------------------------------------------------------------------------------
// CustomBitSet

// Custom std::bitset implementation for speed
template<unsigned N>
struct CustomBitSet
{
    static const unsigned kValidBits = N;
    typedef uint64_t WordT;
    static const unsigned kWordBits = sizeof(WordT) * 8;
    static const unsigned kWords = (kValidBits + kWordBits - 1) / kWordBits;
    static const WordT kAllOnes = UINT64_C(0xffffffffffffffff);

    WordT Words[kWords];


    CustomBitSet()
    {
        ClearAll();
    }

    void ClearAll()
    {
        for (unsigned i = 0; i < kWords; ++i)
            Words[i] = 0;
    }
    void SetAll()
    {
        for (unsigned i = 0; i < kWords; ++i)
            Words[i] = kAllOnes;
    }
    void Set(unsigned bit)
    {
        const unsigned word = bit / kWordBits;
        const WordT mask = (WordT)1 << (bit % kWordBits);
        Words[word] |= mask;
    }
    void Clear(unsigned bit)
    {
        const unsigned word = bit / kWordBits;
        const WordT mask = (WordT)1 << (bit % kWordBits);
        Words[word] &= ~mask;
    }
    bool Check(unsigned bit) const
    {
        const unsigned word = bit / kWordBits;
        const WordT mask = (WordT)1 << (bit % kWordBits);
        return 0 != (Words[word] & mask);
    }

    /*
        Returns the popcount of the bits within the given range.

        bitStart < kValidBits: First bit to test
        bitEnd <= kValidBits: Bit to stop at (non-inclusive)
    */
    unsigned RangePopcount(unsigned bitStart, unsigned bitEnd)
    {
        static_assert(kWordBits == 64, "Update this");

        if (bitStart >= bitEnd)
            return 0;

        unsigned wordIndex = bitStart / kWordBits;
        const unsigned wordEnd = bitEnd / kWordBits;

        // Eliminate low bits of first word
        WordT word = Words[wordIndex] >> (bitStart % kWordBits);

        // Eliminate high bits of last word if there is just one word
        if (wordEnd == wordIndex)
            return PopCount64(word << (kWordBits - (bitEnd - bitStart)));

        // Count remainder of first word
        unsigned count = PopCount64(word);

        // Accumulate popcount of full words
        while (++wordIndex < wordEnd)
            count += PopCount64(Words[wordIndex]);

        // Eliminate high bits of last word if there is one
        unsigned lastWordBits = bitEnd - wordIndex * kWordBits;
        if (lastWordBits > 0)
            count += PopCount64(Words[wordIndex] << (kWordBits - lastWordBits));

        return count;
    }

    /*
        Returns the bit index where the first cleared bit is found.
        Returns kValidBits if all bits are set.

        bitStart < kValidBits: Index to start looking
    */
    unsigned FindFirstClear(unsigned bitStart)
    {
        static_assert(kWordBits == 64, "Update this");

        unsigned wordStart = bitStart / kWordBits;

        WordT word = ~Words[wordStart] >> (bitStart % kWordBits);
        if (word != 0)
        {
            unsigned offset = 0;
            if ((word & 1) == 0)
                offset = FirstNonzeroBit64(word);
            return bitStart + offset;
        }

        for (unsigned i = wordStart + 1; i < kWords; ++i)
        {
            word = ~Words[i];
            if (word != 0)
                return i * kWordBits + FirstNonzeroBit64(word);
        }

        return kValidBits;
    }

    /*
        Returns the bit index where the first set bit is found.
        Returns 'bitEnd' if all bits are clear.

        bitStart < kValidBits: Index to start looking
        bitEnd <= kValidBits: Index to stop looking at
    */
    unsigned FindFirstSet(unsigned bitStart, unsigned bitEnd = kValidBits)
    {
        static_assert(kWordBits == 64, "Update this");

        unsigned wordStart = bitStart / kWordBits;

        WordT word = Words[wordStart] >> (bitStart % kWordBits);
        if (word != 0)
        {
            unsigned offset = 0;
            if ((word & 1) == 0)
                offset = FirstNonzeroBit64(word);
            return bitStart + offset;
        }

        const unsigned wordEnd = (bitEnd + kWordBits - 1) / kWordBits;

        for (unsigned i = wordStart + 1; i < wordEnd; ++i)
        {
            word = Words[i];
            if (word != 0)
                return i * kWordBits + FirstNonzeroBit64(word);
        }

        return bitEnd;
    }

    /*
        Set a range of bits

        bitStart < kValidBits: Index at which to start setting
        bitEnd <= kValidBits: Bit to stop at (non-inclusive)
    */
    void SetRange(unsigned bitStart, unsigned bitEnd)
    {
        if (bitStart >= bitEnd)
            return;

        unsigned wordStart = bitStart / kWordBits;
        const unsigned wordEnd = bitEnd / kWordBits;

        bitStart %= kWordBits;

        if (wordEnd == wordStart)
        {
            // This implies x=(bitStart % kWordBits) and y=(bitEnd % kWordBits)
            // are in the same word.  Also: x < y, y < 64, y - x < 64.
            bitEnd %= kWordBits;
            WordT mask = ((WordT)1 << (bitEnd - bitStart)) - 1; // 1..63 bits
            mask <<= bitStart;
            Words[wordStart] |= mask;
            return;
        }

        // Set the end of the first word
        Words[wordStart] |= kAllOnes << bitStart;

        // Whole words at a time
        for (unsigned i = wordStart + 1; i < wordEnd; ++i)
            Words[i] = kAllOnes;

        // Set first few bits of the last word
        unsigned lastWordBits = bitEnd - wordEnd * kWordBits;
        if (lastWordBits > 0)
        {
            WordT mask = ((WordT)1 << lastWordBits) - 1; // 1..63 bits
            Words[wordEnd] |= mask;
        }
    }

    /*
        Clear a range of bits

        bitStart < kValidBits: Index at which to start clearing
        bitEnd <= kValidBits: Bit to stop at (non-inclusive)
    */
    void ClearRange(unsigned bitStart, unsigned bitEnd)
    {
        if (bitStart >= bitEnd)
            return;

        unsigned wordStart = bitStart / kWordBits;
        const unsigned wordEnd = bitEnd / kWordBits;

        bitStart %= kWordBits;

        if (wordEnd == wordStart)
        {
            // This implies x=(bitStart % kWordBits) and y=(bitEnd % kWordBits)
            // are in the same word.  Also: x < y, y < 64, y - x < 64.
            bitEnd %= kWordBits;
            WordT mask = ((WordT)1 << (bitEnd - bitStart)) - 1; // 1..63 bits
            mask <<= bitStart;
            Words[wordStart] &= ~mask;
            return;
        }

        // Clear the end of the first word
        Words[wordStart] &= ~(kAllOnes << bitStart);

        // Whole words at a time
        for (unsigned i = wordStart + 1; i < wordEnd; ++i)
            Words[i] = 0;

        // Clear first few bits of the last word
        unsigned lastWordBits = bitEnd - wordEnd * kWordBits;
        if (lastWordBits > 0)
        {
            WordT mask = ((WordT)1 << lastWordBits) - 1; // 1..63 bits
            Words[wordEnd] &= ~mask;
        }
    }
};


//------------------------------------------------------------------------------
// SIMD-Safe Aligned Memory Allocations


//------------------------------------------------------------------------------
// AppDataWindow

// Base class for app data window shared between encoder and decoder
struct AppDataWindow
{
    // Application parameters
    unsigned InputCount = 0;   // Number of input symbols
    uint64_t TotalBytes = 0;   // Total number of input bytes
    unsigned FinalBytes = 0;   // Number of bytes in the final symbol


    // Set parameter for the window (should be done first)
    // Returns false if input is invalid
    bool SetParameters(unsigned input_count, uint64_t total_bytes);

    GF256_FORCE_INLINE bool IsFinalColumn(unsigned column)
    {
        return (column == InputCount - 1);
    }

    // Helper function
    GF256_FORCE_INLINE unsigned GetColumnBytes(unsigned column)
    {
        return IsFinalColumn(column) ? FinalBytes : ARP_DATA_SIZE;
    }
};


//------------------------------------------------------------------------------
// XORSummer

// This optimization speeds up the codec by 15%
#define FECAL_ADD2_OPT

class XORSummer
{
public:
    // Set the addition destination and byte count
    GF256_FORCE_INLINE void Initialize(uint8_t* dest, unsigned bytes)
    {
        DestBuffer = dest;
        Bytes = bytes;
        Waiting = nullptr;
    }

    // Accumulate some source data
    GF256_FORCE_INLINE void Add(const uint8_t* src)
    {
#ifdef FECAL_ADD2_OPT
        if (Waiting)
        {
            gf256_add2_mem(DestBuffer, src, Waiting, Bytes);
            Waiting = nullptr;
        }
        else
            Waiting = src;
#else
        gf256_add_mem(DestBuffer, src, Bytes);
#endif
    }

    // Finalize in the destination buffer
    GF256_FORCE_INLINE void Finalize()
    {
#ifdef FECAL_ADD2_OPT
        if (Waiting)
            gf256_add_mem(DestBuffer, Waiting, Bytes);
#endif
    }

protected:
    uint8_t* DestBuffer;
    unsigned Bytes;
    const uint8_t* Waiting;
};


} // namespace fecal
