// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

// ========================================================================================================================
// CLI Command Line Interface
// Handles commands that drive the runtime and assembler.
// The code in this file is mostly dealing with parsing and error checking of commands, delegating the generation of code, single stepping, and display output to others (runtime, assembler, application)

//NOTE: this is to workaround a known problem/bug with intellisense and PCH's in vs2019
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <sstream>
#include <vector>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <varargs.h>
#include <cerrno>

// the Microsoft calculator for large numbers
#include "..//external/Ratpack/ratpak.h"

#include "common.h"
#include "ia64.h"
#include "runtime.h"
#include "assembler.h"
#include "assembler_driver.h"
#include "globvars.h"
#include "cli.h"

namespace inasm64
{
    namespace cli
    {
        std::function<void(const char*, uintptr_t)> OnDataValueSet;
        std::function<void(const char*, uint64_t)> OnSetGPRegister;
        std::function<void(DataType, const RegisterInfo&)> OnDisplayRegister;
        std::function<void()> OnDisplayGPRegisters;
        std::function<void()> OnDisplayXMMRegisters;
        std::function<void()> OnDisplayYMMRegisters;
        std::function<void(const void*)> OnStep;
        std::function<void()> OnStartAssembling;
        std::function<void()> OnStopAssembling;
        std::function<bool()> OnAssembleError;
        std::function<void(const runtime::instruction_index_t&, const assembler::AssembledInstructionInfo&)> OnAssembling;
        std::function<void()> OnQuit;
        std::function<void(const help_texts_t&)> OnHelp;
        std::function<void(DataType, const void*, size_t)> OnDisplayData;
        std::function<void(const std::vector<const char*>&)> OnFindInstruction;
        std::function<bool(const char*)> OnUnknownCommand;

        namespace
        {
            Mode _mode = Mode::Processing;
            using asm_map_t = std::unordered_map<uintptr_t, assembler::AssembledInstructionInfo>;
            asm_map_t _asm_map;
            asm_map_t::iterator _last_instr = _asm_map.end();

            const void* _last_dump_address = nullptr;
            auto _initialised = false;

            // commands are of two types:
            //  type 0 are a command followed by parameters, i.e. "r eax 1234"
            using type_0_handler_t = std::function<void(const char* cmd, char* params)>;
            //  type 1 are a variable name followed by a command followed by parameters, i.e. "myvec db  1.0,1.0,1.0,1.0"
            using type_1_handler_t = std::function<void(const char* param0, const char* cmd, char* params)>;

            // contains information about acommand, including it's handler and the aliases used to identify it
            struct CommandInfo
            {
                // 0 delimeted, 00-terminated
                const char* _names;
                virtual ~CommandInfo()
                {
                    delete[] _names;
                }

                void set_aliases(int count, ...)
                {
                    va_list vsn;
                    size_t total_size = 0;
                    va_start(vsn, count);
                    for(int n = 0; n < count; ++n)
                    {
                        const auto arg = va_arg(vsn, const char*);
                        total_size += strlen(arg) + 1;
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
            help_texts_t _help_texts;

            //NOTE: expects that cmd1Line is stripped of leading whitespace and terminated by double-0
            // modifies cmd1Line in-place
            bool process_command(char* cmd1Line)
            {
                auto rp = cmd1Line;
                auto rp0 = rp;
                // first token
                while(rp[0] && rp[0] != ' ')
                    ++rp;
                rp[0] = 0;
                auto handled = false;

                for(auto& command : _type_0_handlers)
                {
                    auto cmd1_name = command._names;
                    while(cmd1_name[0])
                    {
                        if(strcmp(rp0, cmd1_name) == 0)
                        {
                            ++rp;
                            while(rp[0] && rp[0] == ' ')
                                ++rp;
                            command._handler(cmd1_name, rp);
                            handled = true;
                            break;
                        }
                        // skip past next 0
                        while(cmd1_name[0])
                            ++cmd1_name;
                        ++cmd1_name;
                    }
                    if(handled)
                        break;
                }

                if(!handled)
                {
                    rp0 = ++rp;
                    // second token
                    while(rp[0] && rp[0] != ' ')
                        ++rp;
                    if(rp[0])
                    {
                        rp[0] = 0;
                        for(auto& command : _type_1_handlers)
                        {
                            auto cmd1_name = command._names;
                            while(cmd1_name[0])
                            {
                                if(strcmp(rp0, cmd1_name) == 0)
                                {
                                    ++rp;
                                    while(rp[0] && rp[0] == ' ')
                                        ++rp;
                                    command._handler(cmd1Line, cmd1_name, rp);
                                    handled = true;
                                    break;
                                }
                                // skip past next 0
                                while(cmd1_name[0])
                                    ++cmd1_name;
                                ++cmd1_name;
                            }
                            if(handled)
                                break;
                        }
                    }
                }

                if(!handled)
                {
                    if(OnUnknownCommand)
                        handled = OnUnknownCommand(cmd1Line);

                    if(!handled)
                        detail::set_error(Error::kCliUnknownCommand);
                }

                return handled && GetError() == Error::kNoError;
            }

            // read a number in 0x, 0b, or decimal format at str
            // reads number up until end of string, whitespace, or ,
            // next is next character after number, past whitespace or , , or nullptr if end of string
            bool read_number_string(char* str, std::vector<uint8_t>& bytes, char** next)
            {
                const auto str_len = strlen(str);
                auto rp = str;
                const auto format = detail::starts_with_integer(str, &rp);
                if(format == detail::number_format_t::kUnknown)
                    return false;
                uint32_t radix = 0;
                switch(format)
                {
                case detail::number_format_t::kBinary:
                    radix = 2;
                    break;
                case detail::number_format_t::kDecimal:
                    radix = 10;
                    break;
                case detail::number_format_t::kHexadecimal:
                    radix = 16;
                    break;
                }

                const auto str_segment = reinterpret_cast<char*>(_malloca(strlen(str) + 1));
                auto n = 0;
                while(rp[n] && (rp[n] != ' ' && rp[n] != ','))
                {
                    str_segment[n] = rp[n];
                    ++n;
                }
                str_segment[n] = 0;
                *next = (rp[n] ? rp + n + 1 : nullptr);

                /*RATPAK*/ ChangeConstants(radix, 128);
                auto anumber = /*RATPAK*/ StringToNumber(str_segment, radix, 128);
                _freea(str_segment);

                if(!anumber)
                    return false;
                /*RATPAK*/ PNUMBER number_base_16 = anumber;
                if(radix != 16)
                {
                    // always convert to base 16 internally
                    auto bnumber = /*RATPAK*/ numtonRadixx(anumber, radix);
                    number_base_16 = /*RATPAK*/ nRadixxtonum(bnumber, 16, 128);
                    destroynum(anumber);
                    destroynum(bnumber);
                }

                // now pack digits into bytes, adding exponent first as low bytes
                auto zeros = number_base_16->exp;
                while(zeros > 1)
                {
                    bytes.push_back(0);
                    // nibbles
                    zeros -= 2;
                }
                auto mant = number_base_16->mant;
                auto cdigit = number_base_16->cdigit;
                if(zeros)
                {
                    // remaining bytes will effectively be shifted up by 4 bits to account for this zero
                    bytes.push_back((*mant++ & 0xff) << 4);
                    --cdigit;
                }

                uint8_t nd = 0;
                for(auto d = 0; d < cdigit; ++d)
                {
                    if(d & 1)
                    {
                        nd |= uint8_t((mant[d] & 0xf)) << 4;
                        bytes.push_back(nd);
                        nd = 0;
                    }
                    else
                    {
                        nd |= uint8_t((mant[d] & 0xf));
                    }
                }
                if(nd)
                    bytes.push_back(nd);

                destroynum(number_base_16);

                return true;
            }

            // read a string of floats, comma separated, as 32- or 64 -bit values
            // NOTE: this is a separate function from read_number_string because ratpak converts floats to the most effective numbers, which isn't always what we want
            //       i.e. we want 1.0 as 0x3f800000, not just 1
            bool read_float_string(DataType format, char* str, std::vector<uint8_t>& bytes, char** next)
            {
                assert(format == DataType::kFloat32 || format == DataType::kFloat64);
                auto rp = str;
                if(detail::starts_with_integer(str, &rp) != detail::number_format_t::kDecimal)
                    return false;
                const auto str_len = strlen(str);
                while(rp[0])
                {
                    switch(format)
                    {
                    case DataType::kFloat32:
                    {
                        const auto val = ::strtof(rp, &rp);
                        if(!errno)
                        {
                            const auto val_ptr = reinterpret_cast<const uint8_t*>(&val);
                            for(auto b = 0; b < 4; ++b)
                                bytes.push_back(val_ptr[b]);
                        }
                    }
                    break;
                    case DataType::kFloat64:
                    {
                        const auto val = ::strtold(rp, &rp);
                        if(!errno)
                        {
                            const auto val_ptr = reinterpret_cast<const uint8_t*>(&val);
                            for(auto b = 0; b < 8; ++b)
                                bytes.push_back(val_ptr[b]);
                        }
                    }
                    break;
                    default:;
                    }
                    while(rp[0] && rp[0] == ' ' || rp[0] == ',')
                        ++rp;
                }
                if(next)
                    *next = (rp[0] ? rp : nullptr);
                return true;
            }

            // parses values expected to be of the given type
            // i.e.
            // 0x12,0x13,...
            // "hello world",0
            // 1.0,2.0,3.0,
            // etc.
            //
            // Will fill the bytes vector to the required number of bytes (i.e. backfill with 0s)
            //
            // assumes line has no leading whitespace and is 00-terminated
            bool parse_values(DataType type, char* line, std::vector<uint8_t>& bytes)
            {
                size_t required_bytes = 0;
                auto as_float = false;
                switch(type)
                {
                case DataType::kFloat32:
                    required_bytes = 4;
                    as_float = true;
                    break;
                case DataType::kFloat64:
                    required_bytes = 8;
                    as_float = true;
                    break;
                case DataType::kXmmWord:
                    required_bytes = 16;
                    break;
                case DataType::kYmmWord:
                    required_bytes = 32;
                    break;
                case DataType::kByte:
                    required_bytes = 1;
                    break;
                case DataType::kWord:
                    required_bytes = 2;
                    break;
                case DataType::kDWord:
                    required_bytes = 4;
                    break;
                case DataType::kQWord:
                    required_bytes = 8;
                    break;
                default:;
                }

                if(!required_bytes)
                    return false;
                auto result = true;

                auto rp = line;
                if(as_float)
                {
                    result = read_float_string(type, rp, bytes, &rp);
                }
                else
                {
                    while(rp && (rp[0] || rp[1]))
                    {
                        // used to check that we read <= the required number of bytes in each chunk
                        auto prev_size = bytes.size();

                        if(rp[0] == '"')
                        {
                            // read string, always interpreted as 8 bit bytes
                            ++rp;
                            while((rp[0] || rp[1]) && rp[0] != '"')
                            {
                                bytes.push_back(*rp++);
                            }
                            if(rp[0] == '"')
                                ++rp;
                        }
                        else
                        {
                            result = read_number_string(rp, bytes, &rp) && (bytes.size() - prev_size) <= required_bytes;
                            if(result)
                            {
                                if(bytes.size() < required_bytes)
                                {
                                    // pad
                                    auto ullage = required_bytes - bytes.size();
                                    while(ullage--)
                                        bytes.push_back(0);
                                }
                                // move on to the next value
                                while(rp && (rp[0] || rp[1]) && !isalpha(int(rp[0])) && !isdigit(int(rp[0])) && rp[0] != '-' && rp[0] != '+')
                                {
                                    ++rp;
                                }
                            }
                        }
                    }
                }

                if(!result)
                    detail::set_error(Error::kInvalidInputValueFormat);

                return result;
            }  // namespace

            // return data type for a d[b|w|d|q|x|y|fs|fd...] command
            DataType command_data_type(const char* dcmd)
            {
                DataType type = DataType::kUnknown;
                if(dcmd[0] != 'd')
                    return type;
                switch(dcmd[1])
                {
                case 'b':
                    type = DataType::kByte;
                    break;
                case 'w':
                    type = DataType::kWord;
                    break;
                case 'd':
                    type = DataType::kDWord;
                    break;
                case 'q':
                    type = DataType::kQWord;
                    break;
                case 'x':
                    type = DataType::kXmmWord;
                    break;
                case 'y':
                    type = DataType::kYmmWord;
                    break;
                case 'f':
                {
                    switch(dcmd[2])
                    {
                    // fs = 32 bit
                    case 's':
                        type = DataType::kFloat32;
                        break;
                    // fd = 64 bit
                    case 'd':
                        type = DataType::kFloat64;
                        break;
                    case 0:
                    default:;
                    }
                }
                break;
                default:;
                }
                return type;
            }

            // =========================================================================================
            // command handlers

            // <varname> d[b|w|d...] <values>
            void data_value_handler(const char* argname, const char* cmd, char* params)
            {
                std::vector<uint8_t> data;
                DataType type = command_data_type(cmd);
                if(type == DataType::kUnknown)
                {
                    detail::set_error(Error::kInvalidCommandFormat);
                    return;
                }
                parse_values(type, params, data);

                if(!data.empty())
                {
                    const auto handle = runtime::AllocateMemory(data.size());
                    if(handle)
                    {
                        runtime::WriteBytes(handle, data.data(), data.size());
                        //ZZZ: should probably "try", since otherwise any original data is leaked...
                        globvars::Set(argname, uintptr_t(handle));
                        if(OnDataValueSet)
                            OnDataValueSet(argname, uintptr_t(handle));
                    }
                }
                else
                {
                    detail::set_error(Error::kInvalidCommandFormat);
                }
            }

            // r <regname> [value| d[b|w|...] values]
            void register_handler(const char* cmd, char* params)
            {
                const auto reg_info = GetRegisterInfo(params);
                if(!reg_info)
                {
                    detail::set_error(Error::kInvalidRegisterName);
                    return;
                }
                const auto set_or_display_reg = [&reg_info, &params](DataType type) {
                    detail::simple_tokens_t tokens = detail::simple_tokenise(params, 3);
                    // display using natural register format: rX xmmN
                    if(tokens._num_tokens == 1)
                    {
                        if(OnDisplayRegister)
                            OnDisplayRegister(type, reg_info);
                    }
                    else if(tokens._num_tokens >= 2)
                    {
                        // set: rX xmmN value
                        // OR
                        // set: rX xmmN d[b|w...] values
                        // OR
                        // display: rX xmmN d[b|w...]
                        auto set = true;
                        DataType cmd_type = command_data_type(params + tokens._token_idx[1]);
                        if(cmd_type == DataType::kUnknown)
                        {
                            // set: rX xmmN value
                            cmd_type = BitWidthToIntegerDataType(reg_info._bit_width);
                            if(tokens._num_tokens > 2)
                            {
                                detail::set_error(Error::kInvalidCommandFormat);
                                return;
                            }
                        }
                        else if(tokens._num_tokens == 2)
                        {
                            // display: rX xmmN d[b|w...]
                            set = false;
                        }
                        if(set)
                        {
                            std::vector<uint8_t> data;
                            if(parse_values(cmd_type, params + tokens._token_idx[tokens._num_tokens > 2 ? 2 : 1], data))
                            {
                                runtime::SetReg(reg_info, data.data(), data.size());
                            }
                        }
                        OnDisplayRegister(cmd_type, reg_info);
                    }
                    else
                    {
                        detail::set_error(Error::kInvalidCommandFormat);
                    }
                };

                switch(cmd[1])
                {
                case 'X':
                {
                    if(detail::is_null_or_empty(params))
                    {
                        if(OnDisplayXMMRegisters)
                            OnDisplayXMMRegisters();
                    }
                    else
                    {
                        set_or_display_reg(DataType::kXmmWord);
                    }
                }
                break;
                case 'Y':
                    if(detail::is_null_or_empty(params))
                    {
                        if(OnDisplayYMMRegisters)
                            OnDisplayYMMRegisters();
                    }
                    else
                    {
                        set_or_display_reg(DataType::kYmmWord);
                    }
                    break;
                case 'Z':
                    assert(false);
                    break;
                case 0:
                    if(detail::is_null_or_empty(params))
                    {
                        if(OnDisplayGPRegisters)
                            OnDisplayGPRegisters();
                    }
                    else
                    {
                        set_or_display_reg(BitWidthToIntegerDataType(reg_info._bit_width));
                    }
                default:;
                }
            }  // namespace

            // s, step <address>
            void step_handler(const char*, char* params)
            {
                auto address = runtime::InstructionPointer();
                auto stepped = false;
                assert(detail::is_null_or_empty(params));
                stepped = runtime::Step();
                if(stepped && OnStep)
                {
                    OnStep(address);
                }
            }

            // varname d[b|w|....]
            void display_data_handler(const char* cmd, char* params)
            {
                if(!OnDisplayData)
                    return;

                const auto has_params = !detail::is_null_or_empty(params);
                const void* address = _last_dump_address;
                if(!address && !has_params)
                {
                    return;
                }
                auto is_memory = true;
                RegisterInfo reg_info;
                if(has_params)
                {
                    // could be a register name, or an address
                    if(detail::starts_with_hex_number(params))
                    {
                        const auto arg = strtoll(params, nullptr, 0);
                        if(arg == LLONG_MAX || arg == LLONG_MIN)
                            return;
                        _last_dump_address = address = (const void*)(arg);
                    }
                    else
                    {
                        if(!OnDisplayRegister)
                            return;
                        is_memory = false;
                        reg_info = GetRegisterInfo(params);
                        if(!reg_info)
                        {
                            detail::set_error(Error::kInvalidRegisterName);
                            return;
                        }
                    }
                }

                const auto size = is_memory ? runtime::AllocationSize((const void*)(address)) : reg_info._bit_width / 8;
                if(size)
                {
                    DataType type = DataType::kUnknown;
                    switch(cmd[1])
                    {
                    case 'b':
                        type = DataType::kByte;
                        break;
                    case 'w':
                        type = DataType::kWord;
                        break;
                    case 'd':
                        type = DataType::kDWord;
                        break;
                    case 'q':
                        type = DataType::kQWord;
                        break;
                    case 'f':
                    {
                        if(cmd[2])
                        {
                            switch(cmd[2])
                            {
                            case 's':
                                type = DataType::kFloat32;
                                break;
                            case 'd':
                                type = DataType::kFloat64;
                            default:;
                            }
                        }
                    }
                    break;
                    }

                    if(is_memory)
                        OnDisplayData(type, address, size);
                    else
                        OnDisplayRegister(type, reg_info);
                }
            }
        }  // namespace

        bool
        Initialise()
        {
            if(!_initialised)
            {
                Type1Command cmd1;
                cmd1.set_aliases(6, "db", "dw", "dd", "dq", "dx", "dy", "dfs", "dfd");
                _help_texts.emplace_back("varname d[b|w|d|q|fs|fd] <data...>", "create a variable \"$varname\" pointing to data");
                cmd1._handler = data_value_handler;
                _type_1_handlers.emplace_back(std::move(cmd1));

                Type0Command cmd0;
                cmd0.set_aliases(3, "r", "rX", "rY");
                _help_texts.emplace_back("r[X|Y] [regName] <value>", "display or set GPR, XMM, or YMM register(s)");
                cmd0._handler = register_handler;
                _type_0_handlers.emplace_back(std::move(cmd0));

                //NOTE: not the same as the type-1 above, this is for display
                cmd0.set_aliases(6, "db", "dw", "dd", "dq", "dfs", "dfd");
                _help_texts.emplace_back("d[b|w|d|q] $<varname>", "display data pointed to by varname");
                cmd0._handler = display_data_handler;
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_aliases(2, "p", "step");
                _help_texts.emplace_back("p|step [address]", "single-step next instruction, or at address");
                cmd0._handler = step_handler;
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_aliases(2, "a", "asm");
                _help_texts.emplace_back("a|asm", "enter assembly mode");
                cmd0._handler = [](const char*, char*) {
                    _mode = Mode::Assembling;
                    if(OnStartAssembling)
                        OnStartAssembling();
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_aliases(2, "q", "quit");
                _help_texts.emplace_back("q|quit", "quit inasm64");
                cmd0._handler = [](const char*, char*) {
                    runtime::Shutdown();
                    if(OnQuit)
                        OnQuit();
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_aliases(2, "h", "help");
                _help_texts.emplace_back("h|help", "display help on commands");
                cmd0._handler = [](const char*, char*) {
                    if(OnHelp)
                        OnHelp(_help_texts);
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_aliases(2, "cc", "clearcode");
                _help_texts.emplace_back("cc|clearcode", "clear and reset all assembled code");
                cmd0._handler = [](const char*, char*) {
                    runtime::Reset();
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_aliases(2, "fi", "find");
                _help_texts.emplace_back("fi|find <prefix>", "find and list all supported instructions begining with prefix");
                cmd0._handler = [](const char*, char* prefix) {
                    std::vector<const char*> instructions;
                    assembler::Driver()->FindMatchingInstructions(prefix, instructions);
                    if(!instructions.empty() && OnFindInstruction)
                        OnFindInstruction(instructions);
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

                // ===============================================================
                //initialise Ratpak; default input to base 10, 128 bit precision
                ChangeConstants(10, 128);

                _initialised = true;
            }
            return _initialised;
        }

        Mode ActiveMode()
        {
            return _mode;
        }

        bool Execute(const char* commandLine_)
        {
            if(!_initialised)
            {
                detail::set_error(Error::kCliUninitialised);
                return false;
            }

            const auto commandLineLength = strlen(commandLine_);
            if(commandLineLength > kMaxCommandLineLength)
            {
                detail::set_error(Error::kCliInputLengthExceeded);
                return false;
            }

            detail::set_error(Error::kNoError);
            auto result = false;

            //NOTE: upper bound on a fully expanded string of meta variables, each expanding to a 16 digit hex (16/2 for each meta var "$x")
            // we perform in-place tokenising, with tokens separated by 0 bytes and a double-0 terminator
            const auto cmdLineBuffer = reinterpret_cast<char*>(_malloca(commandLineLength * 8 + 2 + 2));
            size_t nv = 0;
            size_t wp = 0;
            const auto cmdLinePtr = commandLine_;

            // skip past leading whitespace
            while(cmdLinePtr[nv] && cmdLinePtr[nv] != ' ')
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
                    while(cmdLinePtr[nv] && (cmdLinePtr[nv] != ' ' && cmdLinePtr[nv] != ',' && cmdLinePtr[nv] != ']'))
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
                                memcpy(cmdLineBuffer + wp, numbuff, conv_count + 2);
                                wp += conv_count + 2;
                            }
                        }
                        else
                        {
                            detail::set_error(Error::kUndefinedVariable);
                            return false;
                        }
                    }
                }
            }
            // double-0 terminator
            cmdLineBuffer[wp] = 0;
            cmdLineBuffer[wp + 1] = 0;

            // if we're in assembly mode we treat every non-empty input as an assembly instruction
            if(_mode == Mode::Assembling)
            {
                if(is_empty)
                {
                    runtime::CommmitInstructions();
                    _mode = Mode::Processing;
                    result = true;
                    if(OnStopAssembling)
                        OnStopAssembling();
                }
                else
                {
                    assembler::AssembledInstructionInfo asm_info;
                    if(!assembler::Assemble(cmdLineBuffer, asm_info))
                    {
                        if(OnAssembleError)
                            _mode = OnAssembleError() ? Mode::Assembling : Mode::Processing;
                        else
                            _mode = Mode::Processing;
                    }
                    else
                    {
                        const auto index = runtime::AddInstruction(asm_info._instruction, asm_info._size);
                        result = index._address != 0;
                        if(result && OnAssembling)
                            OnAssembling(index, asm_info);
                    }
                }
            }
            else
            {
                if(is_empty)
                {
                    // just ignore empty lines
                    _mode = Mode::Processing;
                }
                else
                {
                    result = process_command(cmdLineBuffer);
                }
            }
            _freea(cmdLineBuffer);
            return result;
        }
    }  // namespace cli
}  // namespace inasm64