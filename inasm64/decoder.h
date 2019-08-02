// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
#pragma once

namespace inasm64
{
    namespace decoder
    {
        ///<summary>
        ///contains basic information about an instruction
        ///</summary>
        ///NOTE This should really be in x64.h at some point
        struct InstructionInfo
        {
            enum class InstructionClass
            {
                kBranching,
                kSse,
                kAvx,
                kAvx512,
                kSyscall,
                kVTx,
                kRing0,
                kUnknown,
            };

            InstructionClass _class = InstructionClass::kUnknown;

            InstructionInfo() = default;
        };

        InstructionInfo Decode(const void* instruction, size_t length);

    }  // namespace decoder
}  // namespace inasm64
