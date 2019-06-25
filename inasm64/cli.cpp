//NOTE: this is to workaround a known problem/bug with intellisense and PCH's in vs2019
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <sstream>
#include <vector>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <varargs.h>

//TESTING
#include <iostream>

#include "common.h"
#include "runtime.h"
#include "assembler.h"
#include "globvars.h"
#include "cli.h"

namespace inasm64
{
    namespace cli
    {
        namespace
        {
            //TODO: determine if this should be dynamic, or if this is even the right approach. Need to get the proper assembler up and running first.
            uint8_t _code_buffer[kMaxAssembledInstructionSize * 128];
            size_t _code_buffer_pos = 0;
            //TODO: error handling and error paths
            Mode _mode = Mode::Processing;

            using asm_map_t = std::unordered_map<uintptr_t, assembler::AssembledInstructionInfo>;
            asm_map_t _asm_map;
            asm_map_t::iterator _last_instr = _asm_map.end();

            auto _initialised = false;

            //TODO: helper, could be elsewhere?
            std::vector<const char*> split_by_space(char* s)
            {
                std::vector<const char*> tokens;
                auto rp = s;
                auto rp0 = rp;
                while(rp[0])
                {
                    rp0 = rp;
                    while(rp[0] && rp[0] == ' ')
                        ++rp;
                    if(rp[0])
                    {
                        rp0 = rp;
                        while(rp[0] && rp[0] != ' ')
                            ++rp;
                        if(rp[0])
                        {
                            ++rp;
                            rp[-1] = 0;
                        }
                        tokens.push_back(rp0);
                    }
                }
                return tokens;
            }

            using type_0_handler_t = std::function<void(const char* cmd, const char* params)>;
            using type_1_handler_t = std::function<void(const char* param0, const char* cmd, const char* params)>;

            struct CommandInfo
            {
                // 0 delimeted, 00-terminated
                const char* _names;
                virtual ~CommandInfo()
                {
                    delete[] _names;
                }

                void set_names(int count, ...)
                {
                    va_list vsn;
                    size_t total_size = 0;
                    va_start(vsn, count);
                    for(int n = 0; n < count; ++n)
                    {
                        total_size += strlen(va_arg(vsn, const char*)) + 1;
                    }
                    va_end(vsn);
                    _names = new char[total_size + 1];
                    va_start(vsn, count);
                    auto wp = const_cast<char*>(_names);
                    for(int n = 0; n < count; ++n)
                    {
                        const auto name = va_arg(vsn, const char*);
                        const auto name_len = strlen(name) + 1;
                        memcpy(wp, name, name_len);
                        wp += name_len;
                    }
                    va_end(vsn);
                    // double-0
                    wp[0] = 0;
                }
            };

            struct Type0Command : CommandInfo
            {
                type_0_handler_t _handler;

                Type0Command() = default;
                Type0Command(Type0Command&& rhs)
                {
                    _names = rhs._names;
                    _handler = std::move(rhs._handler);
                    rhs._names = nullptr;
                }
            };

            struct Type1Command : CommandInfo
            {
                type_1_handler_t _handler;

                Type1Command() = default;
                Type1Command(Type1Command&& rhs)
                {
                    _names = rhs._names;
                    _handler = std::move(rhs._handler);
                    rhs._names = nullptr;
                }
            };

            std::vector<Type0Command> _type_0_handlers;
            std::vector<Type1Command> _type_1_handlers;

            //NOTE: expects that cmdLine is stripped of leading whitespace and terminated by double-0
            // modifies cmdLine in-place
            bool process_command(char* cmdLine)
            {
                auto rp = cmdLine;
                auto rp0 = rp;
                // first token
                while(rp[0] && rp[0] != ' ' && rp[0] != '\t')
                    ++rp;
                rp[0] = 0;
                auto handled = false;
                for(auto& command : _type_0_handlers)
                {
                    auto cmd_name = command._names;
                    while(cmd_name[0])
                    {
                        if(strcmp(rp0, cmd_name) == 0)
                        {
                            command._handler(cmd_name, rp + 1);
                            handled = true;
                            break;
                        }
                        // skip past next 0
                        while(cmd_name[0])
                            ++cmd_name;
                        ++cmd_name;
                    }
                    if(handled)
                        break;
                }

                if(!handled)
                {
                    rp0 = ++rp;
                    // second token
                    while(rp[0] && rp[0] != ' ' && rp[0] != '\t')
                        ++rp;
                    if(rp[0])
                    {
                        rp[0] = 0;
                        for(auto& command : _type_1_handlers)
                        {
                            auto cmd_name = command._names;
                            while(cmd_name[0])
                            {
                                if(strcmp(rp0, cmd_name) == 0)
                                {
                                    command._handler(cmdLine, cmd_name, rp + 1);
                                    handled = true;
                                    break;
                                }
                                // skip past next 0
                                while(cmd_name[0])
                                    ++cmd_name;
                                ++cmd_name;
                            }
                            if(handled)
                                break;
                        }
                    }
                }
                return handled;
            }
        }  // namespace

        bool Initialise()
        {
            if(!_initialised)
            {
				//TESTING:
                Type1Command cmd1;
                cmd1.set_names(4, "db", "dw", "dd", "dq");
                cmd1._handler = [](const char* param0, const char* cmd, const char* params) {
                    std::cout << param0 << ", " << cmd << ", " << params << std::endl;
                };
                _type_1_handlers.emplace_back(std::move(cmd1));

                _initialised = true;
            }            
            return _initialised;
        }

        std::string Help()
        {
            static const std::string _help =
                "h\t\tshowh help\na\t\tstart assembling instructions, empty input ends\np [addr]\t\tsingle step one instruction\n"
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

        Command Execute(const char* commandLine_)
        {
            if(!_initialised)
            {
                detail::SetError(Error::CliUninitialised);
                return Command::Invalid;
			}

            const auto commandLineLength = strlen(commandLine_);
            if(commandLineLength > kMaxCommandLineLength)
            {
                detail::SetError(Error::CliInputLengthExceeded);
                return Command::Invalid;
            }

            auto result = Command::Invalid;

            //NOTE: upper bound on a fully expanded string of meta variables, each expanding to a 16 digit hex (16/2 for each meta var "$x")
            // we perform in-place tokenising, with tokens separated by 0 bytes and a double-0 terminator
            const auto cmdLineBuffer = reinterpret_cast<char*>(_malloca(commandLineLength * 8 + 2 + 2));
            size_t nv = 0;
            size_t wp = 0;
            const auto cmdLinePtr = commandLine_;

            // skip past leading whitespace
            while(cmdLinePtr[nv] && (cmdLinePtr[nv] != ' ' && cmdLinePtr[nv] != '\t'))
            {
                cmdLineBuffer[wp++] = cmdLinePtr[nv++];
            }
            auto is_empty = !wp;

            // replace meta variables $<name>
            while(cmdLinePtr[nv])
            {
                while(cmdLinePtr[nv] && cmdLinePtr[nv] != '$')
                {
                    cmdLineBuffer[wp++] = cmdLinePtr[nv++];
                }
                if(cmdLinePtr[nv])
                {
                    auto nv1 = nv + 1;
                    while(cmdLinePtr[nv] && (cmdLinePtr[nv] != ' ' && cmdLinePtr[nv] != '\t'))
                        ++nv;
                    size_t wl = 0;
                    if(cmdLinePtr[nv])
                        wl = nv - nv1;
                    else
                        wl = commandLineLength - nv1;

                    if(wl)
                    {
                        char strbuff[globvars::kMaxVarLength + 1];
                        memcpy(strbuff, cmdLinePtr + nv1, wl);
                        strbuff[wl] = 0;
                        uintptr_t val = 0;
                        if(globvars::Get(strbuff, val))
                        {
                            char numbuff[64] = { '0', 'x' };
                            const auto conv_count = sprintf_s(numbuff + 2, sizeof(numbuff) - 2, "%llx", val);
                            if(conv_count)
                            {
                                memcpy(cmdLineBuffer + wp, numbuff, conv_count);
                                wp += conv_count;
                            }
                        }
                        else
                        {
                            detail::SetError(Error::UndefinedVariable);
                            return Command::Invalid;
                        }
                    }
                }
            }
            // double-0 terminator
            cmdLineBuffer[wp] = 0;
            cmdLineBuffer[wp + 1] = 0;

			//TESTING:
            process_command(cmdLineBuffer);

            // if we're in assembly mode we treat every non-empty input as an assembly instruction
            if(_mode == Mode::Assembling)
            {
                if(is_empty)
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
                    if(!assembler::Assemble(cmdLineBuffer, asm_info.second))
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
                if(is_empty)
                {
                    // just ignore empty lines
                    _mode = Mode::Processing;
                    result = Command::Invalid;
                }
                else
                {
                    const auto tokens = split_by_space(cmdLineBuffer);

                    //TODO: more consistent command handling, types of commands, sizes etc.

                    const auto cmd = char(::tolower(tokens[0][0]));
                    switch(cmd)
                    {
                    case 'r':
                    {
                        if(tokens.size() == 1)
                        {
                            if(strlen(tokens[0]) > 1)
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
                            const int64_t value = ::strtoll(tokens[2], nullptr, 16);
                            result = runtime::SetReg(tokens[1], value) ? Command::SetReg : Command::Invalid;
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
                            long long value;
                            if(detail::str_to_ll(tokens[1], value))
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
            _freea(cmdLineBuffer);
            return result;
        }
    }  // namespace cli
}  // namespace inasm64