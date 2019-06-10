//ZZZ: precompiled headers don't work across directory levels, known VS bug
#include <string>
#include <cstdint>
#include <vector>

#include "common.h"
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

            // basic parse step; split instruction into left- and right -parts delmeted by , (if any), convert to lowercase, tokenise
            bool TokeniseAssemblyInstruction(const std::string& assembly, std::vector<const char*>& left_tokens, std::vector<const char*>& right_tokens, char* buffer)
            {
                //TODO: error handling
                const auto input_len = assembly.length() + 1;
                memcpy(buffer, assembly.c_str(), input_len);
                _strlwr_s(buffer, input_len);
                std::vector<const char*>* tokens = &left_tokens;

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

                        //TODO: tokenize sub-expression

                        mode = ParseMode::SkipWhitespace;
                        break;
                    }
                }
                return buffer;
            }

            bool BuildStatement(const std::string& assembly, Statement& statement)
            {
                auto result = false;
                memset(&statement, 0, sizeof(statement));
                std::vector<const char*> left_tokens;
                std::vector<const char*> right_tokens;
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
                    //TODO: others?

                    // instruction
                    if(tl < left_tokens.size())
                    {
                        //zzz: can we always assume it's just the first, or second token?
                        statement._instruction = left_tokens[tl++];
                        if(tl < left_tokens.size())
                        {
                            // op1

                            // check for ptrs
                        }
                        else if(!right_tokens.empty())
                        {
                            // "instr , something" is wrong, obviously
                            detail::SetError(Error::InvalidInstructionFormat);
                        }
                    }
                    else
                    {
                        detail::SetError(Error::InvalidInstructionFormat);
                    }
                }

                return result;
            }

        }  // namespace

        bool Initialise()
        {
            _assembler_driver = &_xed_assembler_driver;
            return _xed_assembler_driver.Initialise();
        }

        bool Assemble(const std::string& assembly, AssembledInstructionInfo& info)
        {
            Statement statement;
            if(BuildStatement(assembly, statement))
            {
                uint8_t buffer[15];
                const auto instr_len = _assembler_driver->Assemble(statement, buffer);
                memcpy(const_cast<uint8_t*>(info.Instruction), buffer, instr_len);
                const_cast<size_t&>(info.InstructionSize) = instr_len;
                return true;
            }
            return false;
        }
    }  // namespace assembler
}  // namespace inasm64
