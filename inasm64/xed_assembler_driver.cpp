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
            _state64.stack_addr_width = XED_ADDRESS_WIDTH_32b;
            return true;
        }

        size_t XedAssemblerDriver::Assemble(const Statement& statement, uint8_t* buffer, size_t bufferSize)
        {
            xed_encoder_request_t req;
            xed_encoder_request_zero_set_mode(&req, &_state64);
            xed_encoder_request_set_effective_operand_width(&req, statement._op1_width_bits);

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

            const auto build_xed_op = [&req, &uc_string, &uc_buffer](unsigned op_order, char type, short width_bits, const Statement::op& op) -> bool {
                switch(type)
                {
                case Statement::kReg:
                {
                    uc_string(op._reg);
                    const auto op1_xed_reg = str2xed_reg_enum_t(uc_buffer);
                    if(op1_xed_reg == XED_REG_INVALID)
                    {
                        detail::SetError(Error::InvalidDestRegistername);
                        return false;
                    }
                    const auto operand_reg = static_cast<xed_operand_enum_t>(static_cast<char>(XED_OPERAND_REG0) + op_order);
                    xed_encoder_request_set_reg(&req, operand_reg, op1_xed_reg);
                    xed_encoder_request_set_operand_order(&req, op_order, operand_reg);
                }
                break;
                case Statement::kMem:
                {
                    //TODO: there is a mem1 as well, but need to understand exactly how it's used
                    xed_encoder_request_set_mem0(&req);
                    xed_encoder_request_set_operand_order(&req, op_order, XED_OPERAND_MEM0);

                    auto seg = XED_REG_INVALID;
                    if(op._mem._seg)
                    {
                        uc_string(op._mem._seg);
                        seg = str2xed_reg_enum_t(uc_buffer);
                    }
                    xed_encoder_request_set_seg0(&req, seg);

                    auto base = XED_REG_INVALID;
                    if(op._mem._base)
                    {
                        uc_string(op._mem._base);
                        base = str2xed_reg_enum_t(uc_buffer);
                        if(base == XED_REG_INVALID)
                        {
                            detail::SetError(Error::InvalidDestRegistername);
                            return false;
                        }
                    }
                    xed_encoder_request_set_base0(&req, base);

                    auto index = XED_REG_INVALID;
                    if(op._mem._index)
                    {
                        uc_string(op._mem._index);
                        index = str2xed_reg_enum_t(uc_buffer);
                        if(base == XED_REG_INVALID)
                        {
                            detail::SetError(Error::InvalidDestRegistername);
                            return false;
                        }
                    }
                    xed_encoder_request_set_index(&req, index);
                    xed_encoder_request_set_scale(&req, op._mem._scale);

                    // from xed/examples/xed-enc-lang.c
                    const auto rc = xed_gpr_reg_class(base);
                    const auto rci = xed_gpr_reg_class(index);
                    if(base == XED_REG_EIP)
                        xed_encoder_request_set_effective_address_size(&req, 32);
                    else if(rc == XED_REG_CLASS_GPR32 || rci == XED_REG_CLASS_GPR32)
                        xed_encoder_request_set_effective_address_size(&req, 32);
                    else if(rc == XED_REG_CLASS_GPR16 || rci == XED_REG_CLASS_GPR16)
                        xed_encoder_request_set_effective_address_size(&req, 16);
                    //else, don't set at all?

                    xed_encoder_request_set_memory_operand_length(&req, width_bits >> 3);

                    if(op._mem._displacement)
                    {
                        xed_encoder_request_set_memory_displacement(&req, op._mem._displacement, op._mem._disp_width_bits / 8);
                    }
                }
                break;
                case Statement::kImm:
                {
					//TODO: we need to set the uimm0 bit with based on 
					// 1. the instruction (some only support 8 bit)
					// 2. the bit width of op1 (and possibly the value itself)
                    const auto instr = xed_encoder_request_get_iclass(&req);
                    switch(instr)
                    {
                    case XED_ICLASS_SHL:                        
                    case XED_ICLASS_SHR:
                        xed_encoder_request_set_uimm0_bits(&req, op._imm, 8);
                        break;
                    default:
                        xed_encoder_request_set_uimm0_bits(&req, op._imm, 32);
                        break;
					}
                    
                    xed_encoder_request_set_operand_order(&req, op_order, XED_OPERAND_IMM0);
                }
                break;
                default:
                    break;
                }
                return true;
            };

            if(!build_xed_op(0, statement._op1_type, statement._op1_width_bits, statement._op1))
                return 0;

            if(statement._op12)
            {
                if(!build_xed_op(1, statement._op2_type, statement._op2_width_bits, statement._op2))
                    return 0;
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
            else
            {
                detail::SetError(Error::EncodeError);
            }
            return false;
        }

        size_t XedAssemblerDriver::MaxInstructionSize()
        {
            return size_t(XED_MAX_INSTRUCTION_BYTES);
        }

    }  // namespace assembler
}  // namespace inasm64