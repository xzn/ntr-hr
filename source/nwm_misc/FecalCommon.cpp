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

#include "FecalCommon.h"

namespace fecal {


//------------------------------------------------------------------------------
// AppDataWindow

bool AppDataWindow::SetParameters(unsigned input_count, uint64_t total_bytes)
{
    if (input_count <= 0 || total_bytes < input_count)
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return false;
    }

    InputCount = input_count;
    TotalBytes = total_bytes;

    FinalBytes = static_cast<unsigned>(total_bytes % SymbolSize);
    if (FinalBytes <= 0)
        FinalBytes = SymbolSize;

    FECAL_DEBUG_ASSERT(SymbolSize >= FinalBytes && FinalBytes != 0);

    return true;
}


//------------------------------------------------------------------------------
// AlignedDataBuffer

bool AlignedDataBuffer::Allocate(unsigned bytes)
{
    if (bytes == SymbolSize) {
        memset(DataMem, 0, sizeof(DataMem));
        uint8_t *data = DataMem;
        unsigned offset = (unsigned)((uintptr_t)data % kAlignmentBytes);
        data += kAlignmentBytes - offset;
        Data = data;
        return true;
    }
    return false;
}


//------------------------------------------------------------------------------
// GrowingAlignedByteMatrix


} // namespace fecal
