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
        std::function<void(const void*, const assembler::AssembledInstructionInfo&)> OnAssembling;
        std::function<void()> OnQuit;
        std::function<void(const help_texts_t&)> OnHelp;
        std::function<void(DataType, const void*, size_t)> OnDisplayData;
        std::function<void(const std::vector<const char*>&)> OnFindInstruction;

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

            const void* _last_dump_address = nullptr;
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

            using type_0_handler_t = std::function<void(const char* cmd, char* params)>;
            using type_1_handler_t = std::function<void(const char* param0, const char* cmd, char* params)>;

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
                    detail::set_error(Error::kCliUnknownCommand);

                return handled && GetError() == Error::kNoError;
            }

            bool read_byte_string(const char* str, std::vector<uint8_t>& bytes)
            {
                const auto str_len = strlen(str);
                static const char* formats[2] = { "%02x", "%2hhu" };
                auto rp = str;
                const auto base = detail::starts_with_hex_number(str, &rp) ? 0 : 1;
                //ZZZ: don't really have a good way to parse non-hex large numbers yet...
                assert(!base);
                while(rp[0])
                {
                    long next_byte = 0;
                    if(!sscanf_s(rp, formats[base], &next_byte))
                        return false;
                    bytes.push_back(uint8_t(next_byte));
                    rp += 2;
                }
                return true;
            }

            // 0x12,0x13,...
            // "hello world",0
            // 42,44,45,...
            //
            // assumes line has no leading whitespace and is 00-terminated
            bool parse_values(DataType type, const char* line, std::vector<uint8_t>& bytes)
            {
                auto result = false;
                auto rp = line;
                while(rp[0] || rp[1])
                {
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
                        char* next = nullptr;
                        long long value;
                        size_t byte_count = 0;

                        switch(type)
                        {
                        case DataType::kFloat32:
                        {
                            float fval = std::strtof(rp, &next);
                            if(!errno)
                            {
                                memcpy(&value, &fval, 4);
                                byte_count = 4;
                                result = true;
                            }
                        }
                        break;
                        case DataType::kFloat64:
                        {
                            double dval = std::strtod(rp, &next);
                            if(!errno)
                            {
                                memcpy(&value, &dval, 8);
                                byte_count = 8;
                                result = true;
                            }
                        }
                        break;
                        case DataType::kXmmWord:
                        {
                            if(read_byte_string(rp, bytes) && bytes.size() <= 16)
                            {
                                result = true;
                                const auto ullage = 16 - bytes.size();
                                if(ullage)
                                    for(unsigned b = 0; b < ullage; ++b)
                                        bytes.push_back(0);
                            }
                        }
                        break;
                        case DataType::kYmmWord:
                        {
                            if(read_byte_string(rp, bytes) && bytes.size() <= 32)
                            {
                                result = true;
                                const auto ullage = 32 - bytes.size();
                                if(ullage)
                                    for(unsigned b = 0; b < ullage; ++b)
                                        bytes.push_back(0);
                            }
                        }
                        break;
                        default:
                        {
                            value = strtoll(rp, &next, 0);
                            if(!errno)
                            {
                                byte_count = static_cast<unsigned>(type);
                                result = true;
                            }
                        }
                        break;
                        }

                        if(byte_count)
                        {
                            auto value_bytes = reinterpret_cast<const uint8_t*>(&value);
                            for(unsigned n = 0; n < byte_count; ++n)
                            {
                                bytes.push_back(*value_bytes++);
                            }
                            rp = next;

                            // move on to the next value
                            while((rp[0] || rp[1]) && !isalpha(int(rp[0])) && !isdigit(int(rp[0])))
                            {
                                ++rp;
                            }
                        }
                        else
                            // done here
                            break;
                    }
                }

                if(!result)
                    detail::set_error(Error::kInvalidCommandFormat);

                return result;
            }

            // return data type for a d[b|w|d|q|x|y|fs|fd...] command
            DataType command_data_type(const char* dcmd)
            {
                DataType type = DataType::kUnknown;
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

            void parse_data_type_values(const char* cmd, const char* value_str, std::vector<uint8_t>& data)
            {
                DataType type = command_data_type(cmd);
                if(type == DataType::kUnknown)
                {
                    detail::set_error(Error::kInvalidCommandFormat);
                    return;
                }
                parse_values(type, value_str, data);
            }

            // command handlers
            void data_value_handler(const char* argname, const char* cmd, const char* params)
            {
                std::vector<uint8_t> data;
                parse_data_type_values(cmd, params, data);
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

            void register_handler(const char* cmd, char* params)
            {
                auto display_reg = false;
                const auto check_getset_param = [&display_reg, params]() -> char* {
                    auto rp = params;
                    display_reg = true;
                    while(rp[0] && rp[0] != ' ')
                        ++rp;
                    if(rp[0])
                    {
                        *rp++ = 0;
                        while(rp[0] && rp[0] == ' ')
                            ++rp;
                        display_reg = rp[0] == 0;
                    }
                    return rp;
                };

                const auto set_xyz_reg = [&params](RegisterInfo::RegClass klass, DataType type) {
                    const auto reg_info = GetRegisterInfo(params);
                    if(!reg_info || reg_info._class != klass)
                    {
                        detail::set_error(Error::kInvalidRegisterName);
                        return;
                    }

                    detail::simple_tokens_t tokens = detail::simple_tokenise(params);
                    // rX xmmN
                    if(tokens._num_tokens == 1)
                    {
                        if(OnDisplayRegister)
                        {
                            OnDisplayRegister(type, reg_info);
                        }
                    }
                    // rX xmmN value
                    else if(tokens._num_tokens == 2)
                    {
                        std::vector<uint8_t> data;
                        if(parse_values(type, params + tokens._token_idx[1], data))
                            runtime::SetReg(reg_info, data.data(), data.size());
                    }
                    // rX xmmN d[b|w|...] value
                    else if(tokens._num_tokens > 2)
                    {
                        const DataType cmd_type = command_data_type(params + tokens._token_idx[1]);
                        if(cmd_type != DataType::kUnknown)
                        {
                            std::vector<uint8_t> data;
                            if(parse_values(cmd_type, params + tokens._token_idx[2], data))
                                runtime::SetReg(reg_info, data.data(), data.size());
                        }
                        else
                        {
                            detail::set_error(Error::kInvalidCommandFormat);
                        }
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
                        set_xyz_reg(RegisterInfo::RegClass::kXmm, DataType::kXmmWord);
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
                        set_xyz_reg(RegisterInfo::RegClass::kYmm, DataType::kYmmWord);
                    }
                    break;
                case 'Z':
                    assert(false);
                    /*if(detail::is_null_or_empty(params))
                    {
                        if(OnDisplayZMMRegisters)
                            OnDisplayZMMRegisters();
                    }
                    else*/
                    {
                        set_xyz_reg(RegisterInfo::RegClass::kZmm, DataType::kZmmWord);
                    }
                    break;
                case 0:
                    if(detail::is_null_or_empty(params))
                    {
                        if(OnDisplayXMMRegisters)
                            OnDisplayXMMRegisters();
                    }
                    else
                    {
                        const auto reg_info = GetRegisterInfo(params);
                        if(!reg_info || reg_info._class != RegisterInfo::RegClass::kGpr)
                        {
                            detail::set_error(Error::kInvalidRegisterName);
                            return;
                        }
                        auto rp = check_getset_param();
                        if(!display_reg)
                        {
                            long long value;
                            if(detail::str_to_ll(rp, value) && runtime::SetReg(reg_info, &value, reg_info._bit_width / 8))
                            {
                                if(OnSetGPRegister)
                                    OnSetGPRegister(params, value);
                            }
                        }
                        else if(OnDisplayRegister)
                        {
                            OnDisplayRegister(BitWidthToIntegerDataType(reg_info._bit_width), reg_info);
                        }
                    }
                default:;
                }
            }  // namespace

            void step_handler(const char*, char* params)
            {
                auto address = runtime::InstructionPointer();
                auto stepped = false;
                if(!detail::is_null_or_empty(params))
                {
                    const auto value = strtoll(params, nullptr, 0);
                    if(value < LLONG_MAX && value > LLONG_MIN)
                    {
                        address = reinterpret_cast<const void*>(value);
                        if(runtime::SetNextInstruction(address))
                        {
                            stepped = runtime::Step();
                        }
                    }
                }
                else
                {
                    stepped = runtime::Step();
                }
                if(stepped && OnStep)
                {
                    OnStep(address);
                }
            }

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

                _initialised = true;
            }
            return _initialised;
        }

        const void* inasm64::cli::NextInstructionAssemblyAddress()
        {
            return reinterpret_cast<const void*>(reinterpret_cast<const uint8_t*>(runtime::InstructionWriteAddress()) + _code_buffer_pos);
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
                    if(_code_buffer_pos)
                    {
                        // submit instructions assembled so far
                        runtime::AddCode(_code_buffer, _code_buffer_pos);
                        _code_buffer_pos = 0;
                    }
                    _mode = Mode::Processing;
                    result = true;
                    if(OnStopAssembling)
                        OnStopAssembling();
                }
                else
                {
                    std::pair<uintptr_t, assembler::AssembledInstructionInfo> asm_info;
                    if(!assembler::Assemble(cmdLineBuffer, asm_info.second))
                    {
                        if(OnAssembleError)
                            _mode = OnAssembleError() ? Mode::Assembling : Mode::Processing;
                        else
                            _mode = Mode::Processing;
                    }
                    else
                    {
                        // cache instructions while they are being entered, we'll submit them to the runtime when we exit assembly mode
                        //TODO: error/overflow handling
                        const auto instruction_address = _code_buffer + _code_buffer_pos;
                        memcpy(instruction_address, asm_info.second.Instruction, asm_info.second.InstructionSize);
                        asm_info.first = uintptr_t((reinterpret_cast<const uint8_t*>(runtime::InstructionWriteAddress()) + _code_buffer_pos));
                        const auto at = _asm_map.emplace(asm_info);
                        _last_instr = at.first;
                        _code_buffer_pos += asm_info.second.InstructionSize;
                        if(OnAssembling)
                            OnAssembling(reinterpret_cast<const void*>(asm_info.first), asm_info.second);
                        result = true;
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