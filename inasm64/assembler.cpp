//ZZZ: precompiled headers don't work across directory levels, known VS bug
#include <string>
#include <cstdint>

#include "common.h"
#include "assembler.h"

extern "C"
{
#include "xed-interface.h"
}

namespace inasm64
{
    namespace assembler
    {
        namespace
        {
            xed_uint8_t _instruction[XED_MAX_INSTRUCTION_BYTES];
            unsigned _instruction_size = 0;
        }  // namespace

        bool Initialise()
        {
            xed_tables_init();

            //TESTING: a basic implementation of using XED to encode the statement INC RAX

            xed_encoder_request_t req;
            xed_state_t estate;
            estate.mmode = XED_MACHINE_MODE_LONG_64;
            estate.stack_addr_width = XED_ADDRESS_WIDTH_32b;
            xed_encoder_request_zero_set_mode(&req, &estate);
            xed_encoder_request_set_effective_operand_width(&req, 64);
            xed_encoder_request_set_iclass(&req, XED_ICLASS_INC);
            xed_encoder_request_set_reg(&req, XED_OPERAND_REG0, XED_REG_RAX);
            xed_encoder_request_set_operand_order(&req, 0, XED_OPERAND_REG0);
            unsigned ilen = XED_MAX_INSTRUCTION_BYTES;
            auto xed_error = xed_encode(&req, _instruction, ilen, &_instruction_size);
            auto encode_okay = (xed_error == XED_ERROR_NONE);

            return encode_okay;
        }

        bool Assemble(const std::string& assembly, AssembledInstructionInfo& info)
        {
            //TESTING:
            memcpy(const_cast<uint8_t*>(info.Instruction), _instruction, _instruction_size);
            const_cast<size_t&>(info.InstructionSize) = _instruction_size;
            const_cast<std::string&>(info.Assembly) = assembly;
            return true;
        }
    }  // namespace assembler
}  // namespace inasm64
