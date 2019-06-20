//NOTE: this is to workaround a known problem/bug with intellisense and PCH's in vs2019
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "common.h"
#include "runtime.h"
#include "assembler.h"
#include "globvars.h"
#include "cli.h"

namespace inasm64
{
    namespace cli
    {
        //TODO: determine if this should be dynamic, or if this is even the right approach. Need to get the proper assembler up and running first.
        uint8_t _code_buffer[kMaxAssembledInstructionSize * 128];
        size_t _code_buffer_pos = 0;
        //TODO: error handling and error paths
        Mode _mode = Mode::Processing;

        using asm_map_t = std::unordered_map<uintptr_t, assembler::AssembledInstructionInfo>;
        asm_map_t _asm_map;
        asm_map_t::iterator _last_instr = _asm_map.end();

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

        const void* inasm64::cli::LastAssembledInstructionAddress()
        {
            return reinterpret_cast<const void*>(reinterpret_cast<const uint8_t*>(runtime::InstructionWriteAddress()) + _code_buffer_pos);
        }

        Mode ActiveMode()
        {
            return _mode;
        }

        Command Execute(std::string& commandLine_)
        {
            auto commandLine = commandLine_;
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
                    std::pair<uintptr_t, assembler::AssembledInstructionInfo> asm_info;
                    if(!assembler::Assemble(commandLine, asm_info.second))
                    {
                        //TODO: proper error handling and error reporting
                        _mode = Mode::Processing;
                        result = Command::Invalid;
                    }
                    else
                    {
                        // cache instructions while they are being entered, we'll submit them to the runtime when we exit assembly mode
                        //TODO: error/overflow handling
                        memcpy(_code_buffer + _code_buffer_pos, asm_info.second.Instruction, asm_info.second.InstructionSize);
                        asm_info.first = uintptr_t((reinterpret_cast<const uint8_t*>(runtime::InstructionWriteAddress()) + _code_buffer_pos));
                        const auto at = _asm_map.emplace(asm_info);
                        _last_instr = at.first;
                        _code_buffer_pos += asm_info.second.InstructionSize;
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
                    const auto cmd = char(::tolower(tokens[0][0]));
                    switch(cmd)
                    {
                    case 'r':
                    {
                        if(tokens.size() == 1)
                        {
                            if(tokens[0].length() > 1)
                            {
                                if(tokens[0][1] == 'F')
                                {
                                    result = Command::DisplayFpRegs;
                                }
                                else if(tokens[0][1] == 'X')
                                {
                                    result = Command::DisplayAvxRegs;
                                }
                                else
                                {
                                    detail::SetError(Error::InvalidCommandFormat);
                                    result = Command::Invalid;
                                }
                            }
                            else
                            {
                                result = Command::DisplayAllRegs;
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
                    case 'a':
                        _mode = Mode::Assembling;
                        result = Command::Assemble;
                        break;
                    case 'q':
                        runtime::Shutdown();
                        result = Command::Quit;
                        break;
                    case 'p':
                    {
                        if(tokens.size() > 1)
                        {
                            const long long value = ::strtoll(tokens[1].c_str(), nullptr, 16);
                            if(value != LLONG_MAX && value != LLONG_MIN)
                            {
                                if(runtime::SetNextInstruction(reinterpret_cast<const void*>(value)))
                                {
                                    result = runtime::Step() ? Command::Step : Command::Invalid;
								}
                            }
                        }
                        else
							result = runtime::Step() ? Command::Step : Command::Invalid;
                    }
                    break;
                    case 'h':
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