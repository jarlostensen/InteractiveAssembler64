// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

// ========================================================================================
// Assembler front-end
// The code in this file deals with parsing of an input statement to crate a Statement structure
// that can be passed to the assembler driver to convert into instruction bytes

//ZZZ: precompiled headers don't work across directory levels, known VS bug
#include <string>
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>

#include <iostream>

#include "common.h"
#include "ia64.h"
#include "assembler.h"
#include "xed_assembler_driver.h"

namespace inasm64
{
    namespace assembler
    {
        XedAssemblerDriver _xed_assembler_driver;
        IAssemblerDriver* _assembler_driver = nullptr;

        namespace
        {
            enum class ParseMode
            {
                SkipWhitespace,
                ScanUntilWhitespaceOrComma,
                ScanUntilRightBracket
            };
            // first pass tokeniser; split on whitespaces and statement parts separated by ','
            bool tokenise(const char* assembly, std::vector<char*>* part, size_t part_size, char* buffer)
            {
                const auto input_len = strlen(assembly);
                memcpy(buffer, assembly, input_len + 1);
                _strlwr_s(buffer, input_len + 1);
                auto nparts = 0;
                auto tokens = &part[0];

                auto mode = ParseMode::SkipWhitespace;
                size_t rp = 0;
                size_t ts = 0;
                while(rp < input_len)
                {
                    switch(mode)
                    {
                    case ParseMode::SkipWhitespace:
                        while(buffer[rp] && (buffer[rp] == ' ' || buffer[rp] == '\t'))
                        {
                            ++rp;
                        }
                        // switch token list when we hit a , (if we do)
                        if(buffer[rp] == ',')
                        {
                            if(nparts < part_size)
                                tokens = &part[++nparts];
                            else
                            {
                                detail::set_error(Error::kInvalidInstructionFormat);
                                return false;
                            }
                            ++rp;
                            //and don't switch mode here
                        }
                        else
                        {
                            ts = rp;
                            mode = ParseMode::ScanUntilWhitespaceOrComma;
                        }
                        break;
                    case ParseMode::ScanUntilWhitespaceOrComma:
                        while(buffer[rp] && buffer[rp] != ' ' && buffer[rp] != '\t' && buffer[rp] != ',')
                        {
                            if(buffer[rp++] == '[')
                            {
                                mode = ParseMode::ScanUntilRightBracket;
                                break;
                            }
                        }
                        if(mode != ParseMode::ScanUntilRightBracket)
                        {
                            tokens->push_back(buffer + ts);
                            if(buffer[rp] == ',')
                                if(nparts < part_size)
                                    tokens = &part[++nparts];
                                else
                                {
                                    detail::set_error(Error::kInvalidInstructionFormat);
                                    return false;
                                }
                            buffer[rp] = 0;
                            mode = ParseMode::SkipWhitespace;
                        }
                        ++rp;
                        break;
                    case ParseMode::ScanUntilRightBracket:
                        while(buffer[rp] && buffer[rp] != ']')
                        {
                            ++rp;
                        }
                        tokens->push_back(buffer + ts);
                        buffer[++rp] = 0;

                        mode = ParseMode::SkipWhitespace;
                        break;
                    }
                }
                return !part[0].empty();
            }

            // intermediate structure holding a tokenized operand
            struct TokenisedOperand
            {
                char _reg_imm[20] = { 0 };
                char _seg[4] = { 0 };
                char _base[8] = { 0 };
                char _index[8] = { 0 };
                char _scale[4] = { 0 };
                char _displacement[20] = { 0 };
            };

            // parser for an instruction operand
            // all sorts, including...
            // fs:[eax + esi * 4 - 0x11223344]
            // fs:[eax + esi*4]
            // fs:[eax]
            // [eax]
            // [0x11223344]
            // ...etc.
            bool tokenise_operand(char* operand, TokenisedOperand& op)
            {
                // seg:[base + index*scale + offset], or just a register/immediate
                const auto op_len = strlen(operand);
                auto rp = 0;
                char plus_op_cnt = 0;
                char min_op_cnt = 0;
                char mul_op_cnt = 0;
                op._scale[0] = '1';

                // skip until we find an alphanumeric character, return false if end of string
                // also counts number of + - and * characters found for error checking
                const auto skip_until_alphanum = [&rp, operand, &plus_op_cnt, &min_op_cnt, &mul_op_cnt]() -> bool {
                    plus_op_cnt = min_op_cnt = mul_op_cnt = 0;
                    while(operand[rp] && !isalpha(int(operand[rp])) && !isdigit(int(operand[rp])))
                    {
                        if(operand[rp] == '+')
                            ++plus_op_cnt;
                        else if(operand[rp] == '-')
                            ++min_op_cnt;
                        else if(operand[rp] == '*')
                            ++mul_op_cnt;
                        ++rp;
                    }
                    return operand[rp] != 0;
                };
                // skip until we find non-alpha numeric character
                const auto skip_until_non_alphanum = [&rp, operand]() {
                    while(operand[rp] && (isalpha(int(operand[rp])) || isdigit(int(operand[rp]))))
                        ++rp;
                };

                // start by finding either the end of a segment prefix, or the start of the memory operand
                while(operand[rp] && operand[rp] != ':' && operand[rp] != '[')
                {
                    ++rp;
                }
                if(operand[rp])
                {
                    // seg:
                    if(operand[rp] == ':')
                    {
                        memcpy_s(op._seg, sizeof(op._seg), operand, rp);
                        op._seg[rp++] = 0;
                    }
                    while(operand[rp] && operand[rp] != '[')
                    {
                        ++rp;
                    }
                    if(operand[rp] == '[')
                    {
                        ++rp;
                        auto rp0 = rp;
                        while(operand[rp] && operand[rp] != ']')
                            ++rp;
                        if(!operand[rp])
                        {
                            detail::set_error(Error::kInvalidOperandFormat);
                            return false;
                        }
                        operand[rp] = 0;
                        rp = rp0;

                        // base+index*scale+offset

                        if(skip_until_alphanum())
                        {
                            if(plus_op_cnt || min_op_cnt || mul_op_cnt)
                            {
                                detail::set_error(Error::kInvalidOperandFormat);
                                return false;
                            }

                            rp0 = rp;
                            skip_until_non_alphanum();

                            // check first if this is a pure displacement, i.e. something like [0xabcdef]
                            if(detail::starts_with_hex_number(operand + rp0))
                            {
                                // special case, we'll just take this value as our displacement and exit
                                memcpy_s(op._displacement, sizeof(op._displacement), operand + rp0, rp - rp0);
                                return true;
                            }

                            // otherwise we'll interpret it as the base register
                            memcpy_s(op._base, sizeof(op._base), operand + rp0, rp - rp0);
                            op._base[rp - rp0] = 0;
                            ++rp;

                            // look for index (but could also be displacement)
                            if(skip_until_alphanum())
                            {
                                if((plus_op_cnt && min_op_cnt) || plus_op_cnt > 1 || min_op_cnt > 1 || mul_op_cnt)
                                {
                                    detail::set_error(Error::kInvalidOperandFormat);
                                    return false;
                                }

                                if(!detail::starts_with_hex_number(operand + rp))
                                {
                                    // probably an index register
                                    rp0 = rp;
                                    skip_until_non_alphanum();
                                    memcpy(op._index, operand + rp0, rp - rp0);
                                    op._index[rp - rp0] = 0;

                                    // go past whitespace, until perhaps * or +
                                    while(operand[rp] && operand[rp] == ' ')
                                    {
                                        ++rp;
                                    }
                                    if(operand[rp])
                                    {
                                        // scale?
                                        if(operand[rp] == '*')
                                        {
                                            // scale should follow
                                            if(skip_until_alphanum())
                                            {
                                                rp0 = rp;
                                                skip_until_non_alphanum();
                                                if(rp - rp0 < sizeof(op._scale))
                                                {
                                                    memcpy_s(op._scale, sizeof(op._scale), operand + rp0, rp - rp0);
                                                    op._scale[rp - rp0] = 0;
                                                    ++rp;
                                                }
                                                else
                                                {
                                                    detail::set_error(Error::kInvalidOperandScale);
                                                    return false;
                                                }
                                            }
                                            else
                                            {
                                                detail::set_error(Error::kInvalidOperandFormat);
                                                return false;
                                            }
                                        }
                                        // continue until displacement (or end)
                                        skip_until_alphanum();
                                        if(plus_op_cnt > 1 || min_op_cnt > 1 || mul_op_cnt)
                                        {
                                            detail::set_error(Error::kInvalidOperandFormat);
                                            return false;
                                        }
                                    }
                                }
                                //NOTE: potentially doing this twice for pure displacements, but that's a sacrifice we can make
                                if(detail::starts_with_hex_number(operand + rp))
                                {
                                    // offset, rest of operand
                                    //BUT first...scan backwards to see if it is signed
                                    auto rrp = rp;
                                    auto is_negative = 0;
                                    while(rrp > rp0)
                                    {
                                        if(operand[rrp] == '-')
                                        {
                                            op._displacement[0] = '-';
                                            is_negative = 1;
                                            break;
                                        }
                                        --rrp;
                                    }
                                    // copy number past sign, if needed
                                    strcpy_s(op._displacement + is_negative, sizeof(op._displacement) - is_negative, operand + rp);
                                }
                            }
                        }
                    }
                    else
                    {
                        detail::set_error(Error::kInvalidOperandFormat);
                        return false;
                    }
                }
                else
                {
                    memcpy_s(op._reg_imm, sizeof(op._reg_imm), operand, op_len);
                }
                return (op._base[0] != 0 || op._reg_imm[0] != 0);
            }

        }  // namespace

        bool Initialise()
        {
            _assembler_driver = &_xed_assembler_driver;
            return _xed_assembler_driver.Initialise();
        }

        bool Assemble(const char* assembly, AssembledInstructionInfo& info, uintptr_t instructionRip)
        {
            constexpr size_t kMaxOperands = 6;
            auto result = false;

            Statement statement;
            memset(&statement, 0, sizeof(statement));
            std::vector<char*> part[kMaxOperands];
            if(tokenise(assembly, part, 3, statement._input_tokens))
            {
                result = true;

                // check for prefixes
                size_t instr_index = 0;
                if(strncmp(part[0][instr_index], "rep", 3) == 0)
                {
                    if(!part[0][instr_index][3])
                        statement._rep = true;
                    else if(part[0][instr_index][3] == 'e' && !part[0][instr_index][4])
                        statement._repe = true;
                    else if(part[0][instr_index][3] == 'n' && part[0][instr_index][4] == 'e')
                        statement._repne = true;
                    else
                    {
                        detail::set_error(Error::UnsupportedInstructionFormat);
                        return false;
                    }
                    ++instr_index;
                }
                else if(strncmp(part[0][instr_index], "lock", 4) == 0)
                {
                    statement._lock = true;
                    ++instr_index;
                }

                if(instr_index < part[0].size())
                {
                    const auto check_operand_bit_size_prefix = [&instr_index](const std::vector<char*>& tokens) -> short {
                        if((instr_index + 1) < tokens.size())
                        {
                            if(strncmp(tokens[instr_index], "byte", 4) == 0)
                            {
                                ++instr_index;
                                return 8;
                            }
                            if(strncmp(tokens[instr_index], "word", 4) == 0)
                            {
                                ++instr_index;
                                return 16;
                            }
                            if(strncmp(tokens[instr_index], "dword", 5) == 0)
                            {
                                ++instr_index;
                                return 32;
                            }
                            if(strncmp(tokens[instr_index], "qword", 5) == 0)
                            {
                                ++instr_index;
                                return 64;
                            }
                            if(strncmp(tokens[instr_index], "xmmword", 7) == 0)
                            {
                                ++instr_index;
                                return 128;
                            }
                            if(strncmp(tokens[instr_index], "ymmword", 7) == 0)
                            {
                                ++instr_index;
                                return 256;
                            }
                            if(strncmp(tokens[instr_index], "zmmword", 7) == 0)
                            {
                                ++instr_index;
                                return 512;
                            }
                            // xmmword etc.
                        }
                        return 0;
                    };

                    std::array<TokenisedOperand, kMaxOperands> op_tokens;
                    // instruction
                    statement._instruction = part[0][instr_index++];
                    statement._operand_count = 0;
                    if(instr_index < part[0].size())
                    {
                        // assume error
                        result = false;

                        auto p = 0;
                        while(!part[p].empty())
                        {
                            ++statement._operand_count;
                            ++p;
                        }

                        if(!statement._operand_count)
                        {
                            detail::set_error(Error::kInvalidInstructionFormat);
                            return false;
                        }

                        const auto setup_statement = [instructionRip](char type, Statement::operand& op, const TokenisedOperand& op_info) -> short {
                            //NOTE: we can safely pass pointers to op_info fields around here, they'll never leave the scope of the parent function and it's descendants
                            short width_bits = 0;
                            switch(type)
                            {
                            case Statement::kReg:
                                op._op._reg = op_info._reg_imm;
                                width_bits = GetRegisterInfo(op._op._reg)._bit_width;
                                break;
                            case Statement::kImm:
                            {
                                const auto imm_len = strlen(op_info._reg_imm);
                                const auto base = (op_info._reg_imm[imm_len - 1] != 'h') ? 0 : 16;
                                op._op._imm = ::strtoll(op_info._reg_imm, nullptr, base);
                                unsigned long index;
                                _BitScanReverse64(&index, op._op._imm);
                                width_bits = short((index + 8) & ~7);
                            }
                            break;
                            case Statement::kMem:
                            {
                                op._op._mem._seg = (op_info._seg[0] ? op_info._seg : nullptr);
                                op._op._mem._base = (op_info._base[0] ? op_info._base : nullptr);
                                op._op._mem._index = (op_info._index[0] ? op_info._index : nullptr);
                                op._op._mem._scale = char(::strtol(op_info._scale, nullptr, 0));
                                if(op_info._displacement[0] != 0)
                                {
                                    const auto disp_len = strlen(op_info._displacement);
                                    if(disp_len > 8)
                                    {
                                        // > 32 bit displacements need to be converted into RIP relative offsets

                                        // only allowed for pure displacements (check this...)
                                        if(!instructionRip || op._op._mem._base || op._op._mem._seg || op._op._mem._index || op._op._mem._scale!=1)
                                        {
                                            detail::set_error(Error::kInvalidAddress);
                                            return 0;
                                        }
                                        const auto long_disp = ::strtoll(op_info._displacement, nullptr, 0);
                                        if(errno)
                                        {
                                            detail::set_error(Error::kInvalidAddress);
                                            return 0;
                                        }

                                        op._op._mem._displacement = int(long_disp - (long long)(instructionRip));
                                        op._op._mem._base = "rip";
                                    }
                                    else
                                    {
                                        //NOTE: we can use strtol here because the displacement can never be > 32 bits
                                        op._op._mem._displacement = ::strtol(op_info._displacement, nullptr, 0);
                                    }
                                    unsigned long index;
                                    _BitScanReverse64(&index, op._op._mem._displacement > 0 ? op._op._mem._displacement : -op._op._mem._displacement);
                                    op._op._mem._disp_width_bits = char((index + 8) & ~7);
                                }
                                else
                                {
                                    op._op._mem._displacement = 0;
                                    op._op._mem._disp_width_bits = 0;
                                }
                                // impliciinstr_indexy 32 bits, may be overridden by prefix or the size of op1
                                width_bits = 32;
                            }
                            break;
                            }
                            return width_bits;
                        };

                        statement._operands = new Statement::operand[statement._operand_count];
                        for(p = 0; p < statement._operand_count; ++p)
                        {
                            if(part[p].empty())
                                break;
                            statement._operands[p]._width_bits = check_operand_bit_size_prefix(part[p]);
                            result = tokenise_operand(part[p][instr_index], op_tokens[p]);
                            result = result && (op_tokens[p]._base[0] || op_tokens[p]._displacement[0] || op_tokens[p]._reg_imm[0]);
                            if(!result)
                                break;
                            statement._operands[p]._type = op_tokens[p]._reg_imm[0] ? (isalpha(op_tokens[p]._reg_imm[0]) ? Statement::kReg : Statement::kImm) : Statement::kMem;
                            // break down each operand, and aggregate and adjust the widths of each to match the overall statement (addressing width, operand widths)
                            statement._operands[p]._width_bits = std::max<short>(setup_statement(statement._operands[p]._type, statement._operands[p], op_tokens[p]), statement._operands[p]._width_bits);
                            if(GetError() != Error::kNoError)
                                return false;

                            // implicit override by reg size of dest operand, we don't require a ptr modifier
                            if(p && statement._operands[p]._type == Statement::kMem && statement._operands[p]._width_bits != statement._operands[0]._width_bits)
                            {
                                statement._operands[1]._width_bits = statement._operands[0]._width_bits;
                            }

                            // only used for p=0
                            instr_index = 0;
                        }
                    }
                    else if(!part[1].empty())
                    {
                        result = false;
                        // "instr , something" is wrong, obviously
                        detail::set_error(Error::kInvalidInstructionFormat);
                    }
                }
                else
                {
                    detail::set_error(Error::kInvalidInstructionFormat);
                }
            }

            if(result)
            {
                const auto buffer = reinterpret_cast<uint8_t*>(_malloca(_assembler_driver->MaxInstructionSize()));
                const auto instr_len = _assembler_driver->Assemble(statement, buffer, _assembler_driver->MaxInstructionSize());
                result = instr_len > 0;
                if(result)
                {
                    memcpy(const_cast<uint8_t*>(info._instruction), buffer, instr_len);
                    const_cast<size_t&>(info._size) = instr_len;
                }
                _freea(buffer);
            }
            return result;
        }
        IAssemblerDriver* Driver()
        {
            return _assembler_driver;
        }
    }  // namespace assembler
}  // namespace inasm64
