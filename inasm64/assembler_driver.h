#pragma once

namespace inasm64
{
    namespace assembler
    {
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

        struct Statement
        {
            // rep prefix
            bool _rep : 1;
            // repne prefix
            bool _repne : 1;
            // lock prefix
            bool _lock : 1;
            // true if both op1 and op2 are used
            bool _op12 : 1;
            static constexpr char kReg = 0;
            static constexpr char kImm = 1;
            static constexpr char kMem = 2;
            // 0 = reg, 1 = imm, 2 = mem
            char _op1_type;
            // 0 = reg, 1 = imm, 2 = mem
            char _op2_type;
            // operand width
            short _op1_width_bits;
            // operand width
            short _op2_width_bits;

            const char* _instruction = nullptr;

            //TODO: what about "dword ptr" etc, do they contribute in any other way than changing a mode somewhere...?

            union op {
                const char* _reg = nullptr;
                uint64_t _imm;
                MemoryOperandTokens _mem;
                //NOTE: non-trivial default constructor
                op()
                {
                }
            } _op1, _op2;

            static constexpr size_t kMaxStatementLength = 256;
            char _input_tokens[kMaxStatementLength] = { 0 };

            Statement() = default;
        };

        struct IAssemblerDriver
        {
            virtual size_t Assemble(const Statement&, uint8_t*, const size_t) = 0;
            virtual size_t MaxInstructionSize() = 0;
        };
    }  // namespace assembler
}  // namespace inasm64