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
        /// tokens for a seg:[base+index*scale+displacement] operand
        ///</summary>
        struct MemoryOperandTokens
        {
            // any of the x86 architecture segment registers
            const char* _seg = nullptr;
            // any 32- or 64 -bit register
            const char* _base = nullptr;
            // any 32- or 64 -bit register
            const char* _index = nullptr;
            // scale is a factor by which index is to be multipled before being added to base to specify the address of the operand.Scale can have the value of 1, 2, 4, or 8. If scale is not specified, the default value is 1.
            unsigned char _scale = 1;
            // up to 32 bit constant displacement
            int _displacement = 0;
            // actual displacement bit width
            char _disp_width_bits = 0;
            MemoryOperandTokens() = default;
        };
        ///<summary>
        /// a statment describing an assembly instruction, with up to 3 operands
        ///</summary>
        struct Statement
        {
            // rep prefix
            bool _rep : 1;
            // repne prefix
            bool _repne : 1;
            // repe prefix
            bool _repe : 1;
            // lock prefix
            bool _lock : 1;
            // 3 is currently max
            char _operand_count;

            const char* _instruction = nullptr;

            static constexpr char kReg = 0;
            static constexpr char kImm = 1;
            static constexpr char kMem = 2;

            struct operand
            {
                // 0 = reg, 1 = imm, 2 = mem
                char _type;
                short _width_bits;

                union op_ {
                    const char* _reg = nullptr;
                    uint64_t _imm;
                    MemoryOperandTokens _mem;
                    op_()
                    {
                    }
                } _op;
            }* _operands = nullptr;

            static constexpr size_t kMaxStatementLength = 256;
            char _input_tokens[kMaxStatementLength] = { 0 };

            Statement() = default;
        };

        // This API is implemented by driver (currently XED)
        namespace driver
        {
            bool Initialise();
            size_t Assemble(const Statement&, uint8_t*, const size_t);
            size_t MaxInstructionSize();
            void FindMatchingInstructions(const char* namePrefix, std::vector<const char*>& instructions);
        };  // namespace driver
    }       // namespace assembler
}  // namespace inasm64