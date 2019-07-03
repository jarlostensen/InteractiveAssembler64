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
#include <cerrno>

#include "common.h"
#include "runtime.h"
#include "assembler.h"
#include "globvars.h"
#include "cli.h"

namespace inasm64
{
    namespace cli
    {
        std::function<void(const char*, uintptr_t)> OnDataValueSet;
        std::function<void(const char*, uint64_t)> OnSetGPRegister;
        std::function<void(const char*)> OnDisplayGPRegister;
        std::function<void()> OnDisplayGPRegisters;
        std::function<void()> OnDisplayXMMRegisters;
        std::function<void()> OnDisplayYMMRegisters;
        std::function<void(const void*)> OnStep;
        std::function<void()> OnStartAssembling;
        std::function<void()> OnStopAssembling;
        std::function<void(const void*, const assembler::AssembledInstructionInfo&)> OnAssembling;
        std::function<void()> OnQuit;
        std::function<void()> OnHelp;
        std::function<void(DataType, const void*, size_t)> OnDumpMemory;

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

                void set_names(int count, ...)
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

            //NOTE: expects that cmd1Line is stripped of leading whitespace and terminated by double-0
            // modifies cmd1Line in-place
            bool process_command(char* cmd1Line)
            {
                auto rp = cmd1Line;
                auto rp0 = rp;
                // first token
                while(rp[0] && rp[0] != ' ' && rp[0] != '\t')
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
                            while(rp[0] && (rp[0] == ' ' || rp[0] == '\t'))
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
                    while(rp[0] && rp[0] != ' ' && rp[0] != '\t')
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
                                    while(rp[0] && (rp[0] == ' ' || rp[0] == '\t'))
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
                return handled && GetError() == Error::NoError;
            }

            enum class ValueType
            {
                kByte = 1,
                kWord = 2,
                kDWord = 4,
                kQWord = 8,
                kF32 = 0x40,
                kF64 = 0x80,
                kUnsupported
            };

            // 0x12,0x13,...
            // "hello world",0
            // 42,44,45,...
            //
            // assumes line has no leading whitespace and is 00-terminated
            void parse_values(ValueType type, const char* line, std::vector<uint8_t>& bytes)
            {
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
                        case ValueType::kF32:
                        {
                            float fval = std::strtof(rp, &next);
                            if(!errno)
                            {
                                memcpy(&value, &fval, 4);
                                byte_count = 4;
                            }
                        }
                        break;
                        case ValueType::kF64:
                        {
                            if(!errno)
                            {
                                double dval = std::strtod(rp, &next);
                                memcpy(&value, &dval, 8);
                                byte_count = 8;
                            }
                        }
                        break;
                        default:
                        {
                            value = strtoll(rp, &next, 0);
                            if(!errno)
                                byte_count = static_cast<unsigned>(type);
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
                            //TODO: should really set an error here
                            break;
                    }
                }
            }

            bool is_null_or_empty(const char* str)
            {
                if(!str)
                    return true;
                while(str[0] && (str[0] == ' ' || str[0] == '\t'))
                    ++str;
                return !str[0];
            }

            // command handlers
            void data_value_handler(const char* argname, const char* cmd, const char* params)
            {
                std::vector<uint8_t> data;
                ValueType type = ValueType::kUnsupported;
                switch(cmd[1])
                {
                case 'b':
                    type = ValueType::kByte;
                    break;
                case 'w':
                    type = ValueType::kWord;
                    break;
                case 'd':
                    type = ValueType::kDWord;
                    break;
                case 'q':
                    type = ValueType::kQWord;
                    break;
                case 'f':
                {
                    switch(cmd[2])
                    {
                    // fs = 32 bit
                    case 's':
                        type = ValueType::kF32;
                        break;
                    // fd = 64 bit
                    case 'd':
                        type = ValueType::kF64;
                        break;
                    case 0:
                    default:
                    {
                        detail::SetError(Error::InvalidCommandFormat);
                    }
                    break;
                    }
                }
                break;
                default:;
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
                    detail::SetError(Error::InvalidCommandFormat);
                }
            }

            void register_handler(const char* cmd, char* params)
            {
                switch(cmd[1])
                {
                case 'X':
                    if(is_null_or_empty(params))
                    {
                        if(OnDisplayXMMRegisters)
                            OnDisplayXMMRegisters();
                    }
                    //TODO: set
                    break;
                case 'Y':
                    if(is_null_or_empty(params))
                    {
                        if(OnDisplayYMMRegisters)
                            OnDisplayYMMRegisters();
                    }
                    //TODO: set
                    break;
                case 0:
                    if(is_null_or_empty(params))
                    {
                        if(OnDisplayGPRegisters)
                            OnDisplayGPRegisters();
                    }
                    else
                    {
                        auto rp = params;
                        while(rp[0] && rp[0] != ' ' && rp[0] != '\t')
                            ++rp;
                        auto display_reg = true;
                        if(rp[0])
                        {
                            *rp++ = 0;
                            while(rp[0] && (rp[0] == ' ' || rp[0] == '\t'))
                                ++rp;
                            if(rp[0])
                            {
                                const auto value = ::strtoll(rp, nullptr, 0);
                                _strupr_s(params, rp - params);
                                if(runtime::SetReg(params, value))
                                {
                                    if(OnSetGPRegister)
                                        OnSetGPRegister(params, value);
                                    display_reg = false;
                                }
                            }
                        }
                        if(display_reg)
                        {
                            if(OnDisplayGPRegister)
                                OnDisplayGPRegister(params);
                        }
                    }
                default:;
                }
            }

            void step_handler(const char*, char* params)
            {
                auto address = runtime::InstructionPointer();
                auto stepped = false;
                if(!is_null_or_empty(params))
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

            void dump_memory_handler(const char* cmd, char* params)
            {
                if(!OnDumpMemory)
                    return;

                const auto has_params = !is_null_or_empty(params);
                const void* address = _last_dump_address;
                if(!address && !has_params)
                {
                    return;
                }

                if(has_params)
                {
                    const auto arg = strtoll(params, nullptr, 0);
                    if(arg == LLONG_MAX || arg == LLONG_MIN)
                        return;
                    _last_dump_address = address = (const void*)(arg);
                }

                const auto size = runtime::AllocationSize((const void*)(address));
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

                    OnDumpMemory(type, address, size);
                }
            }
        }  // namespace

        bool Initialise()
        {
            if(!_initialised)
            {
                Type1Command cmd1;
                cmd1.set_names(6, "db", "dw", "dd", "dq", "dfs", "dfd");
                cmd1._handler = data_value_handler;
                _type_1_handlers.emplace_back(std::move(cmd1));

                Type0Command cmd0;
                cmd0.set_names(3, "r", "rX", "rY");
                cmd0._handler = register_handler;
                _type_0_handlers.emplace_back(std::move(cmd0));

                //NOTE: not the same as the type-1 above, this is for display
                cmd0.set_names(4, "db", "dw", "dd", "dq");
                cmd0._handler = dump_memory_handler;
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_names(2, "p", "P");
                cmd0._handler = step_handler;
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_names(2, "a", "A");
                cmd0._handler = [](const char*, char*) {
                    _mode = Mode::Assembling;
                    if(OnStartAssembling)
                        OnStartAssembling();
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_names(2, "q", "Q");
                cmd0._handler = [](const char*, char*) {
                    runtime::Shutdown();
                    if(OnQuit)
                        OnQuit();
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

                cmd0.set_names(3, "h", "H", "?");
                cmd0._handler = [](const char*, char*) {
                    if(OnHelp)
                        OnHelp();
                };
                _type_0_handlers.emplace_back(std::move(cmd0));

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
                detail::SetError(Error::CliUninitialised);
                return false;
            }

            const auto commandLineLength = strlen(commandLine_);
            if(commandLineLength > kMaxCommandLineLength)
            {
                detail::SetError(Error::CliInputLengthExceeded);
                return false;
            }

            auto result = false;

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
                    while(cmdLinePtr[nv] && (cmdLinePtr[nv] != ' ' && cmdLinePtr[nv] != '\t' && cmdLinePtr[nv] != ',' && cmdLinePtr[nv] != ']'))
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
                            detail::SetError(Error::UndefinedVariable);
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
                        //TODO: proper error handling and error reporting
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