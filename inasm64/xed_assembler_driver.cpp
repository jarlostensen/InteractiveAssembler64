#include <cstdint>
#include <memory>
#include "common.h"
#include "xed_assembler_driver.h"

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
            xed_state_t _state64;

        }  // namespace

        XedAssemblerDriver::XedAssemblerDriver()
        {
        }

        bool XedAssemblerDriver::Initialise()
        {
            xed_tables_init();
            _state64.mmode = XED_MACHINE_MODE_LONG_64;
            //in 64 bit long mode this is ignored; estate.stack_addr_width = XED_ADDRESS_WIDTH_32b;
            return true;
        }

        size_t XedAssemblerDriver::Assemble(const Statement& statement, uint8_t* buffer, size_t bufferSize)
        {
            xed_encoder_request_t req;
            xed_encoder_request_zero_set_mode(&req, &_state64);
            xed_encoder_request_set_effective_operand_width(&req, statement._op1_width << 3);

            char uc_buffer[64];

            const auto uc_string = [&uc_buffer](const char* str) {
                const auto rname_len = strlen(str) + 1;
                memcpy(uc_buffer, str, rname_len);
                _strupr_s(uc_buffer, rname_len);
            };

            uc_string(statement._instruction);
            const auto xed_instruction = str2xed_iclass_enum_t(uc_buffer);
            if(xed_instruction == XED_ICLASS_INVALID)
            {
                detail::SetError(Error::InvalidInstructionName);
                return 0;
            }

            xed_encoder_request_set_iclass(&req, xed_instruction);

            switch(statement._op1_type)
            {
            case Statement::kReg:
            {
                uc_string(statement._op1._reg);
                const auto op1_xed_reg = str2xed_reg_enum_t(uc_buffer);
                if(op1_xed_reg == XED_REG_INVALID)
                {
                    detail::SetError(Error::InvalidDestRegistername);
                    return 0;
                }
                xed_encoder_request_set_reg(&req, XED_OPERAND_REG0, op1_xed_reg);
                xed_encoder_request_set_operand_order(&req, 0, XED_OPERAND_REG0);
            }
            break;
            default:
                break;
            }

            if(statement._op12)
            {
                switch(statement._op2_type)
                {
                case Statement::kReg:
                {
                    uc_string(statement._op2._reg);
                    const auto op2_xed_reg = str2xed_reg_enum_t(uc_buffer);
                    if(op2_xed_reg == XED_REG_INVALID)
                    {
                        detail::SetError(Error::InvalidDestRegistername);
                        return 0;
                    }
                    xed_encoder_request_set_reg(&req, XED_OPERAND_REG1, op2_xed_reg);
                    xed_encoder_request_set_operand_order(&req, 1, XED_OPERAND_REG1);
                }
                break;
                default:
                    break;
                }
            }
            else
            {
            }

            unsigned char ibuffer[XED_MAX_INSTRUCTION_BYTES];
            unsigned ilen = XED_MAX_INSTRUCTION_BYTES;
            unsigned instruction_size;
            auto xed_error = xed_encode(&req, ibuffer, ilen, &instruction_size);
            auto encode_okay = (xed_error == XED_ERROR_NONE);
            if(encode_okay)
            {
                if(instruction_size <= bufferSize)
                {
                    memcpy(buffer, ibuffer, instruction_size);
                    return instruction_size;
                }
                detail::SetError(Error::CodeBufferFull);
            }
            return false;
        }

        size_t XedAssemblerDriver::MaxInstructionSize()
        {
            return size_t(XED_MAX_INSTRUCTION_BYTES);
        }

    }  // namespace assembler
}  // namespace inasm64