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
    namespace assembler
    {
        ///<summary>
        /// information about an assembled instruction
        ///</summary>
        struct AssembledInstructionInfo
        {
            // the instruction bytes
            const uint8_t _instruction[kMaxAssembledInstructionSize] = { 0 };
            // the number of instruction bytes
            const size_t _size = 0;
        };

        ///<summary>
        /// initialise the assembler and the assembler driver
        ///</summary>
        bool Initialise();

        ///<summary>
        /// assemble a given, single line, input statement into x64 instruction bytes
        ///</summary>
        // NOTE: if instructionRip != 0 it will be used to generate a RIP relative address, iff the instruction requires it.
        bool Assemble(const char* assembly, AssembledInstructionInfo& asm_info, uintptr_t instrutionRip);
    }  // namespace assembler
}  // namespace inasm64
