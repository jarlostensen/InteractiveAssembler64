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
                    //NOTE: non-trivial default constructor
                    op_()
                    {
                    }
                } _op;
            } _op1, _op2, _op3;

            static constexpr size_t kMaxStatementLength = 256;
            char _input_tokens[kMaxStatementLength] = { 0 };

            Statement() = default;
        };

        struct IAssemblerDriver
        {
            virtual size_t Assemble(const Statement&, uint8_t*, const size_t) = 0;
            virtual size_t MaxInstructionSize() = 0;
            virtual void FindMatchingInstructions(const char* namePrefix, std::vector<const char*>& instructions) = 0;
        };
    }  // namespace assembler
}  // namespace inasm64