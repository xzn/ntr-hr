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
    Encoder

    The encoder builds up sums of input data on Initialize().

    When Encode() is called it will combine these sums in a deterministic way.

    Encode returns a pointer to the Sum workspace.
*/

#include "FecalCommon.h"

namespace fecal {


//------------------------------------------------------------------------------
// EncoderAppDataWindow

// Encoder-specialized app data window
struct EncoderAppDataWindow : AppDataWindow
{
    // Original data
    void* const* OriginalData;
};


//------------------------------------------------------------------------------
// Encoder

class Encoder
{
public:
    // Initialize the encoder
    FecalResult Initialize(unsigned input_count, void* const * const input_data, uint64_t total_bytes);

    // Generate the next recovery packet for the data
    FecalResult Encode(FecalSymbol& symbol);

protected:
    // Application data set
    EncoderAppDataWindow Window;

    // Sums for each lane
    AlignedDataBuffer LaneSums[kColumnLaneCount][kColumnSumCount];

    // Output workspace
    AlignedDataBuffer ProductWorkspace;
};


} // namespace fecal
