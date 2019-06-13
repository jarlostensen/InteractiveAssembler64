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
            // 32 bit constant displacement
            int _displacement;

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

            // 1,2,4,8,16,32
            char _op1_width;
            // 1,2,4,8,16,32
            char _op2_width;

            const char* _instruction = nullptr;

            //TODO: what about "dword ptr" etc, do they contribute in any other way than changing a mode somewhere...?

            union op {
                const char* _reg = nullptr;
                const char* _imm;
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
            virtual size_t Assemble(const Statement&, uint8_t*) = 0;
        };
    }  // namespace assembler
}  // namespace inasm64