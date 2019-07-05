//ZZZ: precompiled headers don't work across directory levels, known VS bug
#include <string>
#include <cstdint>
#include <vector>
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
#ifdef _DEBUG
            void printstatement(const Statement& statement)
            {
                switch(statement._op1._type)
                {
                case Statement::kReg:
                    std::cout << "op1 is Register";
                    break;
                case Statement::kImm:
                    std::cout << "op1 is Immediate";
                    break;
                case Statement::kMem:
                    std::cout << "op1 is Memory";
                    break;
                }

                if(statement._op1._width_bits)
                    std::cout << ", width is " << int(statement._op1._width_bits) << " bytes";

                if(statement._operand_count == 2)
                {
                    std::cout << " ";
                    switch(statement._op2._type)
                    {
                    case Statement::kReg:
                        std::cout << "op2 is Register";
                        break;
                    case Statement::kImm:
                        std::cout << "op2 is Immediate";
                        break;
                    case Statement::kMem:
                        std::cout << "op2 is Memory";
                        break;
                    }
                    if(statement._op2._width_bits)
                        std::cout << ", width is " << int(statement._op2._width_bits) << " bits";
                }

                std::cout << std::endl
                          << "\t";

                if(statement._lock)
                    std::cout << "lock ";
                if(statement._rep)
                    std::cout << "rep ";
                if(statement._repne)
                    std::cout << "repne ";
                std::cout << statement._instruction << " ";

                const auto coutop = [](char type, short width, const Statement::operand& op) {
                    switch(width)
                    {
                    case 1:
                        std::cout << "byte ";
                        break;
                    case 2:
                        std::cout << "word ";
                        break;
                    case 4:
                        std::cout << "dword ";
                        break;
                    case 8:
                        std::cout << "qword ";
                        break;
                    case 0:
                    default:
                        break;
                    }
                    switch(type)
                    {
                    case Statement::kReg:
                        std::cout << op._op._reg;
                        break;
                    case Statement::kImm:
                        std::cout << op._op._imm;
                        break;
                    case Statement::kMem:
                        if(op._op._mem._seg)
                            std::cout << op._op._mem._seg << ":";
                        std::cout << "[";
                        if(op._op._mem._base)
                            std::cout << op._op._mem._base;
                        if(op._op._mem._index)
                            std::cout << "+" << op._op._mem._index;
                        if(op._op._mem._scale)
                            std::cout << "*" << int(op._op._mem._scale) << " ";
                        if(op._op._mem._displacement)
                            std::cout << "+" << std::hex << op._op._mem._displacement;
                        std::cout << "]";
                        break;
                    }
                };

                coutop(statement._op1._type, statement._op1._width_bits, statement._op1);
                for(auto on = 1; on < statement._operand_count; ++on)
                {
                    std::cout << ", ";
                    coutop(statement._op2._type, statement._op2._width_bits, statement._op2);
                }
                std::cout << std::endl;
            }
#endif

            enum class ParseMode
            {
                SkipWhitespace,
                ScanUntilWhitespaceOrComma,
                ScanUntilRightBracket
            };
            // first pass tokeniser; split on whitespaces and statement parts separated by ','
            bool Tokenise(const char* assembly, std::vector<char*>* part, size_t part_size, char* buffer)
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
                                detail::SetError(Error::InvalidInstructionFormat);
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
                                    detail::SetError(Error::InvalidInstructionFormat);
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

            struct TokenisedOperand
            {
                char _reg_imm[20] = { 0 };
                char _seg[4] = { 0 };
                char _base[8] = { 0 };
                char _index[8] = { 0 };
                char _scale[4] = { 0 };
                char _displacement[20] = { 0 };
            };

            // a very bespoke "hand rolled" parser for an operand, i.e. variants of
            // fs:[eax + esi * 4 - 0x11223344]
            // fs:[eax + esi*4]
            // fs:[eax]
            // [eax]
            // [0x11223344]
            // ...etc.
            bool TokeniseOperand(char* operand, TokenisedOperand& op)
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
                            detail::SetError(Error::InvalidOperandFormat);
                            return false;
                        }
                        operand[rp] = 0;
                        rp = rp0;

                        // base+index*scale+offset

                        if(skip_until_alphanum())
                        {
                            if(plus_op_cnt || min_op_cnt || mul_op_cnt)
                            {
                                detail::SetError(Error::InvalidOperandFormat);
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
                                    detail::SetError(Error::InvalidOperandFormat);
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
                                                    detail::SetError(Error::InvalidOperandScale);
                                                    return false;
                                                }
                                            }
                                            else
                                            {
                                                detail::SetError(Error::InvalidOperandFormat);
                                                return false;
                                            }
                                        }
                                        // continue until displacement (or end)
                                        skip_until_alphanum();
                                        if(plus_op_cnt > 1 || min_op_cnt > 1 || mul_op_cnt)
                                        {
                                            detail::SetError(Error::InvalidOperandFormat);
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
                        detail::SetError(Error::InvalidOperandFormat);
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

        bool Assemble(const char* assembly, AssembledInstructionInfo& info)
        {
            auto result = false;

            Statement statement;
            TokenisedOperand op1;
            TokenisedOperand op2;

            memset(&statement, 0, sizeof(statement));
            std::vector<char*> part[3];
            if(Tokenise(assembly, part, 3, statement._input_tokens))
            {
                if(!part[2].empty())
                {
                    //ZZZ: not supported yet
                    detail::SetError(Error::UnsupportedInstructionFormat);
                    return false;
                }
                result = true;

                // check for prefixes
                size_t tl = 0;
                if(strncmp(part[0][tl], "rep", 3) == 0)
                {
                    if(!part[0][tl][3])
                        statement._rep = true;
                    else if(part[0][tl][3] == 'e' && !part[0][tl][4])
                        statement._repe = true;
                    else if(part[0][tl][3] == 'n' && part[0][tl][4] == 'e')
                        statement._repne = true;
                    else
                    {
                        detail::SetError(Error::UnsupportedInstructionFormat);
                        return false;
                    }
                    ++tl;
                }
                else if(strncmp(part[0][tl], "lock", 4) == 0)
                {
                    statement._lock = true;
                    ++tl;
                }

                if(tl < part[0].size())
                {
                    const auto check_operand_bit_size_prefix = [&tl](const std::vector<char*>& tokens) -> char {
                        if((tl + 1) < tokens.size())
                        {
                            if(strncmp(tokens[tl], "byte", 4) == 0)
                            {
                                ++tl;
                                return 8;
                            }
                            if(strncmp(tokens[tl], "word", 4) == 0)
                            {
                                ++tl;
                                return 16;
                            }
                            if(strncmp(tokens[tl], "dword", 5) == 0)
                            {
                                ++tl;
                                return 32;
                            }
                            if(strncmp(tokens[tl], "qword", 5) == 0)
                            {
                                ++tl;
                                return 64;
                            }
                            // xmmword etc.
                        }
                        return 0;
                    };

                    // instruction
                    statement._operand_count = 0;
                    statement._instruction = part[0][tl++];
                    if(tl < part[0].size())
                    {
                        // assume error
                        result = false;
                        detail::SetError(Error::InvalidInstructionFormat);

                        // op1
                        statement._op1._width_bits = check_operand_bit_size_prefix(part[0]);
                        if(TokeniseOperand(part[0][tl], op1))
                        {
                            statement._operand_count = 1;
                            result = (op1._base[0] || op1._reg_imm[0] || op1._displacement[0]);

                            if(result && !part[1].empty())
                            {
                                tl = 0;
                                statement._op2._width_bits = check_operand_bit_size_prefix(part[1]);
                                result = TokeniseOperand(part[1][tl], op2);
                                // sanity check; if for example the input has whitespace between the segment register and the :, the segment register would be misread as base.
                                result = result && (op2._base[0] || op2._displacement[0] || (op2._reg_imm[0] && part[1].size() == 1));
                                statement._operand_count = 2;
                            }

                            if(result)
                            {
                                // simple heuristic for each operand (the assembler driver will have the final say in verifying this)
                                statement._op1._type = op1._reg_imm[0] ? (isalpha(op1._reg_imm[0]) ? Statement::kReg : Statement::kImm)
                                                                       : Statement::kMem;
                                statement._op2._type = op2._reg_imm[0] ? (isalpha(op2._reg_imm[0]) ? Statement::kReg : Statement::kImm)
                                                                       : Statement::kMem;

                                const auto setup_statement = [](char type, Statement::operand& op, const TokenisedOperand& op_info) -> short {
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
                                            const auto base = (op_info._displacement[disp_len - 1] != 'h') ? 0 : 16;
                                            //NOTE: we can use strtol here because the displacement can never be > 32 bits
                                            op._op._mem._displacement = ::strtol(op_info._displacement, nullptr, base);
                                            unsigned long index;
                                            _BitScanReverse64(&index, op._op._mem._displacement > 0 ? op._op._mem._displacement : -op._op._mem._displacement);
                                            op._op._mem._disp_width_bits = char((index + 8) & ~7);
                                        }
                                        // implicitly 32 bits, may be overridden by prefix or the size of op1
                                        width_bits = 32;
                                    }
                                    break;
                                    }
                                    return width_bits;
                                };

                                statement._op1._width_bits = std::max<short>(setup_statement(statement._op1._type, statement._op1, op1), statement._op1._width_bits);
                                if(statement._operand_count == 2)
                                {
                                    statement._op2._width_bits = std::max<short>(setup_statement(statement._op2._type, statement._op2, op2), statement._op2._width_bits);
                                    if(statement._op2._type == Statement::kMem && statement._op2._width_bits != statement._op1._width_bits)
                                    {
                                        // implicit override by reg size, we don't require a ptr modifier
                                        statement._op2._width_bits = statement._op1._width_bits;
                                    }
                                }
                            }
                        }
                    }
                    else if(!part[1].empty())
                    {
                        result = false;
                        // "instr , something" is wrong, obviously
                        detail::SetError(Error::InvalidInstructionFormat);
                    }
                }
                else
                {
                    detail::SetError(Error::InvalidInstructionFormat);
                }
            }

            if(result)
            {
                const auto buffer = reinterpret_cast<uint8_t*>(_alloca(_assembler_driver->MaxInstructionSize()));
                const auto instr_len = _assembler_driver->Assemble(statement, buffer, _assembler_driver->MaxInstructionSize());
                result = instr_len > 0;
                if(result)
                {
                    memcpy(const_cast<uint8_t*>(info.Instruction), buffer, instr_len);
                    const_cast<size_t&>(info.InstructionSize) = instr_len;
                }
            }
            return result;
        }
        IAssemblerDriver* Driver()
        {
            return _assembler_driver;
        }
    }  // namespace assembler
}  // namespace inasm64
