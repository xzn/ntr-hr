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

#include "FecalEncoder.h"
#include "constants.h"

namespace fecal {


//------------------------------------------------------------------------------
// EncoderAppDataWindow


//------------------------------------------------------------------------------
// Encoder

// This optimization speeds up encoding by about 5%
#ifdef FECAL_ADD2_OPT
#define FECAL_ADD2_ENC_SETUP_OPT
#endif

FecalResult Encoder::Initialize(unsigned input_count, void* const * const input_data, uint64_t total_bytes)
{
    // Validate input and set parameters
    if (!Window.SetParameters(input_count, total_bytes))
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return Fecal_InvalidInput;
    }
    Window.OriginalData = input_data;

    const unsigned symbolBytes = SymbolSize;

    // Allocate lane sums
    for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
    {
        for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
        {
            // Clear memory in each lane sum
            memset(LaneSums[laneIndex][sumIndex].Data, 0, symbolBytes);
        }
    }

    // Allocate workspace

    // TBD: Unroll first set of 8 lanes to avoid the extra memset above?
    // TBD: Use GetLaneSum() approach do to minimal work for small output?

#ifdef FECAL_ADD2_ENC_SETUP_OPT
    for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
    {
        // Sum[0] += Data
        XORSummer sum;
        sum.Initialize(LaneSums[laneIndex][0].Data, symbolBytes);

        const unsigned columnEnd = input_count - 1;

        for (unsigned column = laneIndex; column < columnEnd; column += kColumnLaneCount)
        {
            const uint8_t* columnData = reinterpret_cast<const uint8_t*>(input_data[column]);
            sum.Add(columnData);
        }

        if ((columnEnd % kColumnLaneCount) == laneIndex)
        {
            const uint8_t* columnData = reinterpret_cast<const uint8_t*>(input_data[columnEnd]);
            gf256_add_mem(LaneSums[laneIndex][0].Data, columnData, Window.FinalBytes);
        }

        sum.Finalize();
    }
#endif

    // For each input column:
    for (unsigned column = 0; column < input_count; ++column)
    {
        const uint8_t* columnData = reinterpret_cast<const uint8_t*>(input_data[column]);
        const unsigned columnBytes = Window.GetColumnBytes(column);
        const unsigned laneIndex = column % kColumnLaneCount;
        const uint8_t CX = GetColumnValue(column);
        const uint8_t CX2 = gf256_sqr(CX);

#ifndef FECAL_ADD2_ENC_SETUP_OPT
        // Sum[0] += Data
        gf256_add_mem(LaneSums[laneIndex][0].Data, columnData, columnBytes);
#endif

        // Sum[1] += CX * Data
        gf256_muladd_mem(LaneSums[laneIndex][1].Data, CX, columnData, columnBytes);

        // Sum[2] += CX^2 * Data
        gf256_muladd_mem(LaneSums[laneIndex][2].Data, CX2, columnData, columnBytes);
    }

    return Fecal_Success;

    static_assert(kColumnSumCount == 3, "Update this");
}

FecalResult Encoder::Encode(FecalSymbol& symbol)
{
    // If encoder is not initialized:

    const unsigned symbolBytes = SymbolSize;
    if (symbol.Bytes != symbolBytes)
        return Fecal_InvalidInput;

    // Load parameters
    const unsigned count = Window.InputCount;
    uint8_t* outputSum = reinterpret_cast<uint8_t*>( symbol.Data );
    uint8_t* outputProduct = ProductWorkspace.Data;

    const unsigned row = symbol.Index;

    // Initialize LDPC
    PCGRandom prng;
    prng.Seed(row, count);

    // Accumulate original data into the two sums
    const unsigned pairCount = (Window.InputCount + kPairAddRate - 1) / kPairAddRate;
    // Unrolled first loop:
    {
        const unsigned element1 = prng.Next() % count;
        const uint8_t* original1 = reinterpret_cast<const uint8_t*>(Window.OriginalData[element1]);

        const unsigned elementRX = prng.Next() % count;
        const uint8_t* originalRX = reinterpret_cast<const uint8_t*>(Window.OriginalData[elementRX]);

        // Sum = Original[element1]
        if (Window.IsFinalColumn(element1))
        {
            memcpy(outputSum, original1, Window.FinalBytes);
            memset(outputSum + Window.FinalBytes, 0, symbolBytes - Window.FinalBytes);
        }
        else
            memcpy(outputSum, original1, symbolBytes);

        // Product = Original[elementRX]
        if (Window.IsFinalColumn(elementRX))
        {
            memcpy(outputProduct, originalRX, Window.FinalBytes);
            memset(outputProduct + Window.FinalBytes, 0, symbolBytes - Window.FinalBytes);
        }
        else
            memcpy(outputProduct, originalRX, symbolBytes);
    }

    XORSummer sum;
    sum.Initialize(outputSum, symbolBytes);
    XORSummer prod;
    prod.Initialize(outputProduct, symbolBytes);

    for (unsigned i = 1; i < pairCount; ++i)
    {
        const unsigned element1   = prng.Next() % count;
        const uint8_t* original1  = reinterpret_cast<const uint8_t*>(Window.OriginalData[element1]);

        const unsigned elementRX  = prng.Next() % count;
        const uint8_t* originalRX = reinterpret_cast<const uint8_t*>(Window.OriginalData[elementRX]);

        // Sum += Original[element1]
        if (Window.IsFinalColumn(element1))
            gf256_add_mem(outputSum, original1, Window.FinalBytes);
        else
            sum.Add(original1);

        // Product += Original[elementRX]
        if (Window.IsFinalColumn(elementRX))
            gf256_add_mem(outputProduct, originalRX, Window.FinalBytes);
        else
            prod.Add(originalRX);
    }

    // For each lane:
    for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
    {
        // Compute the operations to run for this lane and row
        unsigned opcode = GetRowOpcode(laneIndex, row);

        // Sum += Random Lanes
        unsigned mask = 1;
        for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex, mask <<= 1)
            if (opcode & mask)
                sum.Add(LaneSums[laneIndex][sumIndex].Data);

        // Product += Random Lanes
        for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex, mask <<= 1)
            if (opcode & mask)
                prod.Add(LaneSums[laneIndex][sumIndex].Data);
    }

    sum.Finalize();
    prod.Finalize();

    // Sum += RX * Product
    gf256_muladd_mem(outputSum, GetRowValue(row), outputProduct, symbolBytes);

    return Fecal_Success;
}


} // namespace fecal
