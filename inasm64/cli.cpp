//NOTE: this is to workaround a known problem/bug with intellisense and PCH's in vs2019
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

#include "common.h"
#include "runtime.h"
#include "assembler.h"
#include "cli.h"

namespace inasm64
{
    namespace cli
    {
        AssembledInstructionInfo _asm_info;
        //TODO: determine if this should be dynamic, or if this is even the right approach. Need to get the proper assembler up and running first.
        uint8_t _code_buffer[kMaxAssembledInstructionSize * 128];
        size_t _code_buffer_pos = 0;
        //TODO: error handling and error paths
        Mode _mode = Mode::Processing;

        namespace
        {
            //TODO: helper, could be elsewhere
            std::vector<std::string> split(const std::string& s, char delimiter)
            {
                std::vector<std::string> tokens;
                std::string token;
                std::istringstream tokenStream(s);
                while(std::getline(tokenStream, token, delimiter))
                {
                    tokens.push_back(token);
                }
                return tokens;
            }

            //TODO: perhaps this should just be a part of runtime?

        }  // namespace

        std::string Help()
        {
            static const std::string _help =
                "h\t\tshowh help\na\t\tstart assembling instructions, empty input ends\np\t\tsingle step one instruction\n"
                "r <reg> [value]\tread or set register value\n"
                "q\t\tquit";
            return _help;
        }

        const AssembledInstructionInfo* inasm64::cli::LastAssembledInstructionInfo()
        {
            if(_mode == Mode::Assembling)
                return &_asm_info;
            return nullptr;
        }

        Mode ActiveMode()
        {
            return _mode;
        }

        Command Execute(std::string& commandLine_)
        {
            //TODO: not at all happy with this code and all the early out returns; refactor and clean up once flow is clear

            auto commandLine = commandLine_;
            std::transform(commandLine.begin(), commandLine.end(), commandLine.begin(), ::toupper);

            Command result = Command::Invalid;

            static std::string _whitespace = " \t";
            const auto n = commandLine.find_first_not_of(_whitespace);

            // if we're in assembly mode we treat every non-empty input as an assembly instruction
            if(_mode == Mode::Assembling)
            {
                if(n == std::string::npos)
                {
                    if(_code_buffer_pos)
                    {
                        // submit instructions assembled so far
                        runtime::AddCode(_code_buffer, _code_buffer_pos);
                        _code_buffer_pos = 0;
                    }
                    _mode = Mode::Processing;
                    result = Command::Assemble;
                }
                else
                {
                    if(!assembler::Assemble(commandLine, _asm_info))
                    {
                        //TODO: proper error handling and error reporting
                        _mode = Mode::Processing;
                        result = Command::Invalid;
                    }
                    else
                    {
                        // cache instructions while they are being entered, we'll submi them to the runtime when we exit assembly mode
                        //TODO: error/overflow handling
                        memcpy(_code_buffer + _code_buffer_pos, _asm_info.Instruction, _asm_info.InstructionSize);
                        _code_buffer_pos += _asm_info.InstructionSize;
                        _asm_info.Address = reinterpret_cast<const void*>(reinterpret_cast<const uint8_t*>(runtime::InstructionWriteAddress()) + _code_buffer_pos);
                        result = Command::Assemble;
                    }
                }
            }
            else
            {
                if(n == std::string::npos)
                {
                    //TODO: proper error handling
                    _mode = Mode::Processing;
                    result = Command::Invalid;
                }
                else
                {
                    const auto tokens = split(commandLine, ' ');
                    //TODO: make this more resilient (and bark on invalid commands, like "raaaa"...)
                    const auto cmd = char(tokens[0][0]);
                    switch(cmd)
                    {
                    case 'R':
                    {
                        if(tokens.size() == 1)
                        {
                            result = Command::DisplayAllRegs;
                        }
                        else if(tokens.size() == 2)
                        {
                            if(tokens[1] == "FP")
                                result = Command::DisplayFpRegs;
                            else if(tokens[1] == "AVX")
                                result = Command::DisplayAvxRegs;
                            else
                            {
                                detail::SetError(Error::InvalidCommandFormat);
                                result = Command::Invalid;
                            }
                        }
                        else
                        {
                            //ZZZ: assumes hex input
                            const int64_t value = ::strtoll(tokens[2].c_str(), nullptr, 16);
                            result = runtime::SetReg(tokens[1].c_str(), value) ? Command::SetReg : Command::Invalid;
                        }
                    }
                    break;
                    case 'A':
                        _mode = Mode::Assembling;
                        _asm_info.Address = runtime::InstructionWriteAddress();
                        result = Command::Assemble;
                        break;
                    case 'Q':
                        runtime::Shutdown();
                        result = Command::Quit;
                        break;
                    case 'P':
                        result = runtime::Step() ? Command::Step : Command::Invalid;
                        break;
                    case 'H':
                        result = Command::Help;
                        break;
                    default:
                        break;
                    }
                }
            }
            return result;
        }
    }  // namespace cli
}  // namespace inasm64