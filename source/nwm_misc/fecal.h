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

#ifndef CAT_FECAL_H
#define CAT_FECAL_H

/*
    FEC-AL: Forward Error Correction at the Application Layer
    Block erasure code based on math from the Siamese library.
*/

// Library version
#define FECAL_VERSION 2

// Tweak if the functions are exported or statically linked
//#define FECAL_DLL /* Defined when building/linking as DLL */
//#define FECAL_BUILDING /* Defined by the library makefile */

#if defined(FECAL_BUILDING)
# if defined(FECAL_DLL)
    #define FECAL_EXPORT __declspec(dllexport)
# else
    #define FECAL_EXPORT
# endif
#else
# if defined(FECAL_DLL)
    #define FECAL_EXPORT __declspec(dllimport)
# else
    #define FECAL_EXPORT extern
# endif
#endif

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


//------------------------------------------------------------------------------
// Initialization API
//
// Perform static initialization for the library, verifying that the platform
// is supported.
//
// Returns 0 on success and other values on failure.

FECAL_EXPORT int fecal_init_(int version);
#define fecal_init() fecal_init_(FECAL_VERSION)


//------------------------------------------------------------------------------
// Shared Constants / Datatypes

// Results
typedef enum FecalResultT
{
    Fecal_NeedMoreData      =  1, // More data is needed for this operation to succeed

    Fecal_Success           =  0,

    Fecal_InvalidInput      = -1, // A function parameter was invalid
    Fecal_Platform          = -2, // Platform is unsupported
    Fecal_OutOfMemory       = -3, // Out of memory error occurred
    Fecal_Unexpected        = -4, // Unexpected error - Software bug?
} FecalResult;

// Encoder and Decoder object types
typedef struct FecalEncoderImpl { int impl; }*FecalEncoder;

// Data or Recovery symbol
typedef struct FecalSymbolT
{
    // User-provided data pointer allocated by application.
    void* Data;

    // User-provided number of bytes in the data buffer, for validation.
    unsigned Bytes;

    // Zero-based index in the data array,
    // or a larger number for recovery data.
    unsigned Index;
} FecalSymbol;

// Recovered data
typedef struct RecoveredSymbolsT
{
    // Array of symbols
    FecalSymbol* Symbols;

    // Number of symbols in the array
    unsigned Count;
} RecoveredSymbols;


//------------------------------------------------------------------------------
// Encoder API

FECAL_EXPORT int fecal_encoder_size(void);
FECAL_EXPORT int fecal_encoder_align(void);

/*
    fecal_encoder_create()

    Create an encoder and set the input data.

    input_count: Number of input_data[] buffers provided.
    input_data:  Array of pointers to input data.
    total_bytes: Sum of the total bytes in all buffers.

    Buffer data must be available until the decoder is freed with fecal_free().
    Buffer data does not need to be aligned.
    Buffer data will not be modified, only read.

    Each buffer should have the same number of bytes except for the last one,
    which can be shorter.

    Let symbol_bytes = The number of bytes in each input_data buffer:

        input_count = static_cast<unsigned>(
            (total_bytes + symbol_bytes - 1) / symbol_bytes);

    Or if the number of pieces is known:

        symbol_bytes = static_cast<unsigned>(
            (total_bytes + input_count - 1) / input_count);

    Let final_bytes = The final piece of input data size in bytes:

        final_bytes = static_cast<unsigned>(total_bytes % symbol_bytes);
        if (final_bytes <= 0)
            final_bytes = symbol_bytes;

    Returns NULL on failure.
*/
FECAL_EXPORT int fecal_encoder_init(FecalEncoder encoder, unsigned input_count, void* const * const input_data, uint64_t total_bytes);

/*
    fecal_encode()

    Generate a recovery symbol.

    encoder:       Encoder from fecal_encoder_create().
    symbol->Index: Application provided recovery symbol index starting from 0.
    symbol->Data:  Application provided buffer to write the symbol to.
    symbol->Bytes: Application provided number of bytes in the symbol buffer.

    Given total_bytes and input_count from fecal_encoder_create():

        symbol->Bytes = static_cast<unsigned>(
            (total_bytes + input_count - 1) / input_count);

    Returns Fecal_Success on success.
    Returns Fecal_InvalidInput if the symbol parameter was invalid, or the
        codec is not initialized yet.
*/
FECAL_EXPORT int fecal_encode(FecalEncoder encoder, FecalSymbol* symbol);

/*
    fecal_free()

    Free memory associated with the created encoder or decoder.

    codec: Pointer returned by fecal_encoder_create() or fecal_decoder_create()
*/

#define BitSet4096ValidBits (1 << 12)
#define BitSetWordT uint64_t
#define BitSetWordBits (sizeof(BitSetWordT) * 8)
#define BitSet4096Words ((BitSet4096ValidBits + BitSetWordBits - 1) / BitSetWordBits)

typedef struct BitSet4096Mem { BitSetWordT Words[BitSet4096Words]; }*BitSet4096;

FECAL_EXPORT void rp_arq_bitset_clear_all(BitSet4096);
FECAL_EXPORT void rp_arq_bitset_set(BitSet4096, unsigned);
FECAL_EXPORT void rp_arq_bitset_clear(BitSet4096, unsigned);
FECAL_EXPORT bool rp_arq_bitset_check(BitSet4096, unsigned);
FECAL_EXPORT bool rp_arq_bitset_check_n_wrapped(BitSet4096, unsigned, unsigned);
FECAL_EXPORT unsigned rp_arq_bitset_ffs_n_wrapped(BitSet4096, unsigned, unsigned);


#ifdef __cplusplus
}
#endif


#endif // CAT_FECAL_H
