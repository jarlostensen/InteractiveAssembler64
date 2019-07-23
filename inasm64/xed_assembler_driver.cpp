// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

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
            if(statement._operand_count)
                xed_encoder_request_set_effective_operand_width(&req, statement._operands[0]._width_bits);

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
                detail::set_error(Error::kInvalidInstructionName);
                return 0;
            }

            xed_encoder_request_set_iclass(&req, xed_instruction);

            // AVX512vl mode depends on the type of instruction and the register operands. We try to detect it here based on the operand register classes (see xed_enc_lang.c)
            xed_int_t vl = -1;
            const auto build_xed_op = [&req, &uc_string, &uc_buffer, &statement, &vl](unsigned op_order, char type, short width_bits, const Statement::operand& op) -> bool {
                switch(type)
                {
                case Statement::kReg:
                {
                    uc_string(op._op._reg);
                    const auto op1_xed_reg = str2xed_reg_enum_t(uc_buffer);
                    if(op1_xed_reg == XED_REG_INVALID)
                    {
                        detail::set_error(Error::kInvalidDestRegistername);
                        return false;
                    }
                    const auto operand_reg = static_cast<xed_operand_enum_t>(static_cast<char>(XED_OPERAND_REG0) + op_order);
                    xed_encoder_request_set_reg(&req, operand_reg, op1_xed_reg);
                    xed_encoder_request_set_operand_order(&req, op_order, operand_reg);
                    const auto reg_class = xed_reg_class(op1_xed_reg);
                    // see xed_enc_lang.c; this is part of a heuristic to determine "vl" settings of the instruction
                    if(reg_class == XED_REG_CLASS_XMM)
                        vl = 0;
                    //TODO: ymm,zmm
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
                            detail::set_error(Error::kInvalidDestRegistername);
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
                            detail::set_error(Error::kInvalidDestRegistername);
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
                        switch(statement._operands[0]._width_bits)
                        {
                        case 8:
                            if(width_bits > 8)
                            {
                                detail::set_error(Error::kInvalidImmediateOperandBitWidth);
                                return false;
                            }
                            break;
                        case 16:
                            if(width_bits > 16)
                            {
                                detail::set_error(Error::kInvalidImmediateOperandBitWidth);
                                return false;
                            }
                            //has to be clamped to 16
                            width_bits = 16;
                            break;
                        case 32:
                            if(width_bits > 32)
                            {
                                detail::set_error(Error::kInvalidImmediateOperandBitWidth);
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

            for(auto op = 0; op < statement._operand_count; ++op)
            {
                if(!build_xed_op(op, statement._operands[op]._type, statement._operands[op]._width_bits, statement._operands[op]))
                    return 0;
            }

            //NOTE: what if the instruction isn't an AVX512 instruction, why does this not have a negative effect?
            if(vl >= 0)
                xed3_operand_set_vl(&req, (xed_uint_t)vl);

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
                detail::set_error(Error::kCodeBufferFull);
            }
            else
            {
                detail::set_error(Error::kEncodeError);
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