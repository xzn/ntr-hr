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

#include "fecal.h"
#include "gf256.h"
#include "FecalEncoder.h"

extern "C" {

//------------------------------------------------------------------------------
// Initialization API

static bool m_Initialized = false;

FECAL_EXPORT int fecal_init_(int version)
{
    if (version != FECAL_VERSION)
        return Fecal_InvalidInput;

    if (0 != gf256_init())
        return Fecal_Platform;

    m_Initialized = true;
    return Fecal_Success;
}


//------------------------------------------------------------------------------
// Encoder API

FECAL_EXPORT int fecal_encoder_size(void)
{
    return sizeof(fecal::Encoder);
}

FECAL_EXPORT int fecal_encoder_align(void)
{
    return alignof(fecal::Encoder);
}

FECAL_EXPORT int fecal_encoder_init(FecalEncoder encoder, unsigned input_count, void* const * const input_data, uint64_t total_bytes)
{
    if (input_count <= 0 || !input_data || total_bytes < input_count)
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return Fecal_InvalidInput;
    }

    FECAL_DEBUG_ASSERT(m_Initialized); // Must call fecal_init() first
    if (!m_Initialized)
        return Fecal_InvalidInput;

    return reinterpret_cast<fecal::Encoder *>(encoder)->Initialize(input_count, input_data, total_bytes);
}

FECAL_EXPORT int fecal_encode(FecalEncoder encoder_v, FecalSymbol* symbol)
{
    fecal::Encoder* encoder = reinterpret_cast<fecal::Encoder*>( encoder_v );
    if (!encoder || !symbol)
        return Fecal_InvalidInput;

    return encoder->Encode(*symbol);
}

typedef fecal::CustomBitSet<1 << 12> BitSet4096Impl;
static_assert(sizeof(BitSet4096Impl) == sizeof(BitSet4096Mem));

FECAL_EXPORT void rp_arq_bitset_clear_all(BitSet4096 bs)
{
    ((BitSet4096Impl *)bs)->ClearAll();
}

FECAL_EXPORT void rp_arq_bitset_set(BitSet4096 bs, unsigned b)
{
    ((BitSet4096Impl *)bs)->Set(b);
}

FECAL_EXPORT void rp_arq_bitset_clear(BitSet4096 bs, unsigned b)
{
    ((BitSet4096Impl *)bs)->Clear(b);
}

FECAL_EXPORT bool rp_arq_bitset_check(BitSet4096 bs, unsigned b)
{
    return ((BitSet4096Impl *)bs)->Check(b);
}

FECAL_EXPORT bool rp_arq_bitset_check_n_wrapped(BitSet4096 bs, unsigned b, unsigned n)
{
    if (b + n > BitSet4096Impl::kValidBits) {
        unsigned n_1 = n - (BitSet4096Impl::kValidBits - b);
        return
            ((BitSet4096Impl *)bs)->FindFirstSet(b, BitSet4096Impl::kValidBits) < BitSet4096Impl::kValidBits ||
            ((BitSet4096Impl *)bs)->FindFirstSet(0, n_1) < n_1;
    } else {
        return ((BitSet4096Impl *)bs)->FindFirstSet(b, b + n) < b + n;
    }
}

FECAL_EXPORT unsigned rp_arq_bitset_ffs_n_wrapped(BitSet4096 bs, unsigned b, unsigned n)
{
    if (b + n > BitSet4096Impl::kValidBits) {
        unsigned n_1 = n - (BitSet4096Impl::kValidBits - b);
        unsigned ret = ((BitSet4096Impl *)bs)->FindFirstSet(b, BitSet4096Impl::kValidBits);
        if (ret < BitSet4096Impl::kValidBits) {
            return ret;
        }
        return ((BitSet4096Impl *)bs)->FindFirstSet(0, n_1);
    } else {
        return ((BitSet4096Impl *)bs)->FindFirstSet(b, b + n);
    }
}

FECAL_EXPORT bool rp_arq_bitset_check_all_set_n_wrapped(BitSet4096 bs, unsigned b, unsigned n)
{
    if (b + n > BitSet4096Impl::kValidBits) {
        unsigned n_0 = BitSet4096Impl::kValidBits - b;
        unsigned n_1 = n - n_0;
        return
            ((BitSet4096Impl *)bs)->RangePopcount(b, BitSet4096Impl::kValidBits) == n_0 &&
            ((BitSet4096Impl *)bs)->RangePopcount(0, n_1) == n_1;
    } else {
        return ((BitSet4096Impl *)bs)->RangePopcount(b, b + n) == n;
    }
}

FECAL_EXPORT unsigned rp_arq_bitset_ffc_n_wrapped(BitSet4096 bs, unsigned b, unsigned n)
{
    if (b + n > BitSet4096Impl::kValidBits) {
        unsigned n_1 = n - (BitSet4096Impl::kValidBits - b);
        unsigned ret = ((BitSet4096Impl *)bs)->FindFirstClear(b);
        if (ret < BitSet4096Impl::kValidBits) {
            return ret;
        }
        return (ret = ((BitSet4096Impl *)bs)->FindFirstClear(0)), (ret < n_1 ? ret : n_1);
    } else {
        unsigned ret = ((BitSet4096Impl *)bs)->FindFirstClear(b);
        return ret < b + n ? ret : b + n;
    }
}


} // extern "C"
