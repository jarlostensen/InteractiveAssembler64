#include <cstdint>
#include <memory>
#include <algorithm>

#include "common.h"
#include "xed_assembler_driver.h"
#include "xed_iclass_instruction_set.h"

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
            xed_encoder_request_set_effective_operand_width(&req, statement._op1._width_bits);

            //ZZZ: this doesn't seem to work, or have the intended effect, the 0xf2 or 0xf3 prefixes don't get emitted by XED by just setting these
            //     instead we have to use the special rep/ne_ instruction prefix for the iclass (see below)
            /*if(statement._rep)
                xed_encoder_request_set_rep(&req);
            else if(statement._repne)
                xed_encoder_request_set_repne(&req);*/

            char uc_buffer[64];
            const auto uc_string = [&uc_buffer](const char* str, const char* prefix = nullptr) {
                auto rname_len = strlen(str) + 1;
                if(prefix)
                {
                    const auto pf_len = strlen(prefix);
                    memcpy(uc_buffer + pf_len, str, rname_len);
                    memcpy(uc_buffer, prefix, pf_len);
                    rname_len += pf_len;
                }
                else
                {
                    memcpy(uc_buffer, str, rname_len);
                }
                _strupr_s(uc_buffer, rname_len);
            };

            // add prefix code to the instruction if needed (XED requires this)
            if(statement._rep)
            {
                uc_string(statement._instruction, "rep_");
            }
            else if(statement._repe)
            {
                uc_string(statement._instruction, "repe_");
            }
            else if(statement._repne)
            {
                uc_string(statement._instruction, "repne_");
            }
            else
            {
                uc_string(statement._instruction);
            }

            const auto xed_instruction = str2xed_iclass_enum_t(uc_buffer);
            if(xed_instruction == XED_ICLASS_INVALID)
            {
                detail::SetError(Error::InvalidInstructionName);
                return 0;
            }

            xed_encoder_request_set_iclass(&req, xed_instruction);

            const auto build_xed_op = [&req, &uc_string, &uc_buffer, &statement](unsigned op_order, char type, short width_bits, const Statement::operand& op) -> bool {
                switch(type)
                {
                case Statement::kReg:
                {
                    uc_string(op._op._reg);
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
                    const auto instr = xed_encoder_request_get_iclass(&req);
                    //ZZZ: this is what XED calls an "AGEN" (Address Generation) but it's not at all clear to me
                    //	   and also...what other instructions use this?
                    if(instr == XED_ICLASS_LEA)
                    {
                        xed_encoder_request_set_agen(&req);
                        xed_encoder_request_set_operand_order(
                            &req, op_order, XED_OPERAND_AGEN);
                    }
                    else
                    {
                        //TODO: there is a mem1 as well, but need to understand exactly how it's used
                        xed_encoder_request_set_mem0(&req);
                        xed_encoder_request_set_operand_order(&req, op_order, XED_OPERAND_MEM0);
                    }

                    auto seg = XED_REG_INVALID;
                    if(op._op._mem._seg)
                    {
                        uc_string(op._op._mem._seg);
                        seg = str2xed_reg_enum_t(uc_buffer);
                    }
                    xed_encoder_request_set_seg0(&req, seg);

                    auto base = XED_REG_INVALID;
                    if(op._op._mem._base)
                    {
                        uc_string(op._op._mem._base);
                        base = str2xed_reg_enum_t(uc_buffer);
                        if(base == XED_REG_INVALID)
                        {
                            detail::SetError(Error::InvalidDestRegistername);
                            return false;
                        }
                    }
                    xed_encoder_request_set_base0(&req, base);

                    auto index = XED_REG_INVALID;
                    if(op._op._mem._index)
                    {
                        uc_string(op._op._mem._index);
                        index = str2xed_reg_enum_t(uc_buffer);
                        if(base == XED_REG_INVALID)
                        {
                            detail::SetError(Error::InvalidDestRegistername);
                            return false;
                        }
                    }
                    xed_encoder_request_set_index(&req, index);
                    xed_encoder_request_set_scale(&req, op._op._mem._scale);

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

                    if(op._op._mem._displacement)
                    {
                        xed_encoder_request_set_memory_displacement(&req, op._op._mem._displacement, op._op._mem._disp_width_bits / 8);
                    }
                }
                break;
                case Statement::kImm:
                {
                    // firstly clamp to allowed bit widths, regardless of what the actual bit width of the immediate is
                    if(width_bits > 32)
                        width_bits = 64;
                    else if(width_bits > 16)
                        width_bits = 32;
                    else if(width_bits > 8)
                        width_bits = 16;
                    else
                        width_bits = 8;

                    //NOTE: furthermore there are special cases of what widths are allowed for immediate operands depending on instructions
                    //      Intel® 64 and IA-32 Architectures Software Developer’s Manual Vol.2B 4 - 35
                    // SEE notes about sign extension http://home.myfairpoint.net/fbkotler/nasmdocc.html#section-A.4.3
                    // -> should probably follow the same model, i.e. you need the BYTE modifier on the immediate value to generate the sign extension version,
                    //    otherwise it does what we're doing in the code below (i.e. mov ax,0x80 -> ax = 0x0080, instead of 0xff80)

                    const auto instr = xed_encoder_request_get_iclass(&req);
                    switch(instr)
                    {
                    case XED_ICLASS_MOV:
                        switch(statement._op1._width_bits)
                        {
                        case 8:
                            if(width_bits > 8)
                            {
                                detail::SetError(Error::InvalidImmediateOperandBitWidth);
                                return false;
                            }
                            break;
                        case 16:
                            if(width_bits > 16)
                            {
                                detail::SetError(Error::InvalidImmediateOperandBitWidth);
                                return false;
                            }
                            //has to be clamped to 16
                            width_bits = 16;
                            break;
                        case 32:
                            if(width_bits > 32)
                            {
                                detail::SetError(Error::InvalidImmediateOperandBitWidth);
                                return false;
                            }
                            width_bits = 32;
                        case 64:
                            // can be either 64- or 32-bits in long mode (REX byte controlled)
                            width_bits = std::max<short>(width_bits, 32);
                            break;
                        }

                        break;
                    default:;
                    }

                    xed_encoder_request_set_uimm0_bits(&req, op._op._imm, width_bits);
                    xed_encoder_request_set_operand_order(&req, op_order, XED_OPERAND_IMM0);
                }
                break;
                default:
                    break;
                }
                return true;
            };

            if(statement._operand_count)
            {
                if(!build_xed_op(0, statement._op1._type, statement._op1._width_bits, statement._op1))
                    return 0;

                if(statement._operand_count == 2)
                {
                    if(!build_xed_op(1, statement._op2._type, statement._op2._width_bits, statement._op2))
                        return 0;
                }
                else if(statement._operand_count > 3)
                {
                    detail::SetError(Error::UnsupportedInstructionFormat);
                    return 0;
                }
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

        void XedAssemblerDriver::FindMatchingInstructions(const char* namePrefix, std::vector<const char*>& instructions)
        {
            int prefix_start = -1;
            for(auto p = 0; p < inasm64::kXedInstrutionPrefixTableSize; ++p)
            {
                if(namePrefix[0] == inasm64::xed_instruction_prefix_table[p]._prefix)
                {
                    prefix_start = int(inasm64::xed_instruction_prefix_table[p]._index);
                    break;
                }
            }

            if(prefix_start >= 0)
            {
                const auto instr_len = strlen(namePrefix);
                while(namePrefix[0] == inasm64::xed_instruction_table[prefix_start][0])
                {
                    const auto xed_instr = inasm64::xed_instruction_table[prefix_start];
                    if(strlen(xed_instr) >= instr_len)
                    {
                        if(strncmp(xed_instr, namePrefix, instr_len) == 0)
                        {
                            instructions.push_back(xed_instr);
                        }
                    }
                    ++prefix_start;
                }
            }
        }
    }  // namespace assembler
}  // namespace inasm64