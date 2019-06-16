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
            // either 0x followed by at least one hex digit, or hex digits followed by a 'h'
            bool starts_with_hex_number(const char* at)
            {
                if(at[0] == '0' && at[1] == 'x')
                {
                    at += 2;
                    return isdigit(at[0]) || (at[0] >= 'a' && at[0] <= 'f');
                }
                do
                {
                    if(!isdigit(at[0]) && (at[0] < 'a' || at[0] > 'f'))
                        return false;
                    ++at;
                } while(at[0] && at[0] != 'h');
                return true;
            }

#ifdef _DEBUG
            void printstatement(const Statement& statement)
            {
                switch(statement._op1_type)
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

                if(statement._op1_width)
                    std::cout << ", width is " << int(statement._op1_width) << " bytes";

                if(statement._op12)
                {
                    std::cout << " ";
                    switch(statement._op2_type)
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
                    if(statement._op2_width)
                        std::cout << ", width is " << int(statement._op2_width) << " bytes";
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

                const auto coutop = [](char type, char width, const Statement::op& op) {
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
                        std::cout << op._reg;
                        break;
                    case Statement::kImm:
                        std::cout << op._imm;
                        break;
                    case Statement::kMem:
                        if(op._mem._seg)
                            std::cout << op._mem._seg << ":";
                        std::cout << "[";
                        if(op._mem._base)
                            std::cout << op._mem._base;
                        if(op._mem._index)
                            std::cout << "+" << op._mem._index;
                        if(op._mem._scale)
                            std::cout << "*" << int(op._mem._scale) << " ";
                        if(op._mem._displacement)
                            std::cout << "+" << std::hex << op._mem._displacement;
                        std::cout << "]";
                        break;
                    }
                };

                coutop(statement._op1_type, statement._op1_width, statement._op1);
                if(statement._op12)
                {
                    std::cout << ", ";
                    coutop(statement._op2_type, statement._op2_width, statement._op2);
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

            // basic parse step; split instruction into left- and right -parts delmeted by , (if any), convert to lowercase, tokenise
            bool TokeniseAssemblyInstruction(const std::string& assembly, std::vector<char*>& left_tokens, std::vector<char*>& right_tokens, char* buffer)
            {
                const auto input_len = assembly.length();
                memcpy(buffer, assembly.c_str(), input_len + 1);
                _strlwr_s(buffer, input_len + 1);
                std::vector<char*>* tokens = &left_tokens;

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
                        // switch token list when we hit the , (if we do)
                        if(buffer[rp] == ',')
                        {
                            tokens = &right_tokens;
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
                            ++rp;
                            if(buffer[rp] == '[')
                            {
                                mode = ParseMode::ScanUntilRightBracket;
                                break;
                            }
                        }
                        if(mode != ParseMode::ScanUntilRightBracket)
                        {
                            tokens->push_back(buffer + ts);
                            if(buffer[rp] == ',')
                                // switch to right side when we hit the ,
                                tokens = &right_tokens;
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
                return buffer;
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
                            if(starts_with_hex_number(operand + rp0))
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

                                if(!starts_with_hex_number(operand + rp))
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
                                if(starts_with_hex_number(operand + rp))
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

        bool Assemble(const std::string& assembly, AssembledInstructionInfo& info)
        {
            auto result = false;

            Statement statement;
            TokenisedOperand op1;
            TokenisedOperand op2;

            memset(&statement, 0, sizeof(statement));
            std::vector<char*> left_tokens;
            std::vector<char*> right_tokens;
            if(TokeniseAssemblyInstruction(assembly, left_tokens, right_tokens, statement._input_tokens) && !left_tokens.empty())
            {
                result = true;

                // check for prefixes
                size_t tl = 0;
                if(strncmp(left_tokens[tl], "rep", 3) == 0)
                {
                    statement._rep = true;
                    ++tl;
                }
                else if(strncmp(left_tokens[tl], "repne", 5) == 0)
                {
                    statement._repne = true;
                    ++tl;
                }
                else if(strncmp(left_tokens[tl], "lock", 4) == 0)
                {
                    statement._lock = true;
                    ++tl;
                }

                if(tl < left_tokens.size())
                {
                    const auto check_operand_size_prefix = [&tl](const std::vector<char*>& tokens) -> char {
                        if((tl + 1) < tokens.size())
                        {
                            if(strncmp(tokens[tl], "byte", 4) == 0)
                            {
                                ++tl;
                                return 1;
                            }
                            if(strncmp(tokens[tl], "word", 4) == 0)
                            {
                                ++tl;
                                return 2;
                            }
                            if(strncmp(tokens[tl], "dword", 5) == 0)
                            {
                                ++tl;
                                return 4;
                            }
                            if(strncmp(tokens[tl], "qword", 5) == 0)
                            {
                                ++tl;
                                return 8;
                            }
                        }
                        return 0;
                    };

                    // instruction
                    statement._instruction = left_tokens[tl++];
                    if(tl < left_tokens.size())
                    {
                        result = false;

                        // op1
                        statement._op1_width = check_operand_size_prefix(left_tokens);
                        if(TokeniseOperand(left_tokens[tl], op1))
                        {
                            result = (op1._base[0] || op1._reg_imm[0] || op1._displacement[0]);

                            if(result && !right_tokens.empty())
                            {
                                tl = 0;
                                statement._op2_width = check_operand_size_prefix(right_tokens);
                                result = TokeniseOperand(right_tokens[tl], op2);
                                // sanity check; if for example the input has whitespace between the segment register and the :, the segment register would be misread as base.
                                result = result && (op2._base[0] || op1._displacement[0] || (op2._reg_imm[0] && right_tokens.size() == 1));
                                statement._op12 = true;
                            }

                            if(result)
                            {
                                // simple heuristic for each operand (the assembler driver will have the final say in verifying this)
                                statement._op1_type = op1._reg_imm[0] ? (isalpha(op1._reg_imm[0]) ? Statement::kReg : Statement::kImm)
                                                                      : Statement::kMem;
                                statement._op2_type = op2._reg_imm[0] ? (isalpha(op2._reg_imm[0]) ? Statement::kReg : Statement::kImm)
                                                                      : Statement::kMem;

                                const auto setup_statement = [](char type, Statement::op& op, const TokenisedOperand& op_info) -> char {
                                    char width = 0;
                                    switch(type)
                                    {
                                    case Statement::kReg:
                                        //NOTE: we can safely pass these pointers around here, they'll never leave the scope of the parent function and it's descendants
                                        op._reg = op_info._reg_imm;
                                        width = char(GetRegisterInfo(op._reg)._bit_width >> 3);
                                        break;
                                    case Statement::kImm:
                                    {
                                        const auto imm_len = strlen(op_info._reg_imm);
                                        const auto base = (op_info._reg_imm[imm_len - 1] != 'h') ? 0 : 16;
                                        op._imm = strtol(op_info._reg_imm, nullptr, base);
                                        // simply the number of digits / 2 (minus trailing 'h' or leading '0x'), rounded up
                                        width = char(((base ? imm_len - 1 : imm_len - 2) + 1) / 2);
                                    }
                                    break;
                                    case Statement::kMem:
                                    {
                                        op._mem._seg = (op_info._seg[0] ? op_info._seg : nullptr);
                                        op._mem._base = (op_info._base[0] ? op_info._base : nullptr);
                                        op._mem._index = (op_info._index[0] ? op_info._index : nullptr);
                                        op._mem._scale = char(strtol(op_info._scale, nullptr, 0));
                                        if(op_info._displacement[0] != 0)
                                        {
                                            const auto base = (op_info._displacement[strlen(op_info._displacement) - 1] != 'h') ? 0 : 16;
                                            op._mem._displacement = strtol(op_info._displacement, nullptr, base);
                                        }
                                    }
                                    break;
                                    }
                                    return width;
                                };

                                statement._op1_width = std::max<char>(setup_statement(statement._op1_type, statement._op1, op1), statement._op1_width);
                                if(statement._op12)
                                {
                                    statement._op2_width = std::max<char>(setup_statement(statement._op2_type, statement._op2, op2), statement._op2_width);

                                    const auto statement_width = std::max<char>(statement._op1_width, statement._op2_width);
                                    statement._op1_width = std::max<char>(statement._op1_width, statement_width);
                                    statement._op2_width = std::max<char>(statement._op2_width, statement_width);
                                }

#if _DEBUG
                                printstatement(statement);
#endif
                            }
                        }

                        if(!result)
                            detail::SetError(Error::InvalidInstructionFormat);
                    }
                    else if(!right_tokens.empty())
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
    }  // namespace assembler
}  // namespace inasm64
