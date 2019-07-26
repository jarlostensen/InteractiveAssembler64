// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

// ===================================================================================================================
// At the heart of the runtime is a debugger using the Windows Debug APIs, and remote process memory management
// Also handles register context mapping

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <debugapi.h>
#include <unordered_map>
#include <cassert>

// we use some intrinsics to initialise AVX etc.
#include <immintrin.h>

#include <memory>
#include "common.h"
#include "ia64.h"
#include "runtime.h"

#if !defined(_WIN64)
#error Unsupported on non-Windows, non-64bit platforms
#endif

//https://docs.microsoft.com/en-us/windows/desktop/debug/process-functions-for-debugging
// https://win32assembly.programminghorizon.com/tut28.html
// http://www.codereversing.com/blog/archives/169
// https://www.codeproject.com/Articles/132742/Writing-Windows-CodeRunner-Part-2

namespace inasm64
{
    namespace runtime
    {
        static union {
            STARTUPINFOA _startupinfo_a;
            STARTUPINFOW _startupinfo_w;
        };
        PROCESS_INFORMATION _processinfo = { 0 };
        // process handle with virtual memory access privileges
        HANDLE _process_vm = nullptr;
        // track allocations in process memory
        std::unordered_map<uintptr_t, size_t> _allocations;

        // runtime variables, such as "execip" and "codesize", etc.
        inasm64::detail::char_string_map_t _variables;
        const char* kVariables[] = {
            "execip",
            "codesize",
            "startip",
            "endip"
        };

        struct instruction_line_info_t
        {
            size_t _line;
            uintptr_t _address;
            size_t _instruction_size;
            uint8_t _instruction_bytes[kMaxAssembledInstructionSize];
        };
        std::vector<instruction_line_info_t> _loaded_instructions;
        size_t _instruction_line = 0;
        size_t _first_instruction_line = 0;
        size_t _last_instruction_line = 0;
        size_t _commit_size = 0;

        struct
        {
            bool _started : 1;
            bool _running : 1;
        } _flags = { 0 };

        unsigned char* _scratch_memory = nullptr;
        size_t _scratch_size = 0;
        unsigned char* _code = nullptr;
        unsigned char* _code_end = nullptr;

        DEBUG_EVENT _dbg_event = { 0 };
        DWORD _continue_status = DBG_CONTINUE;

        PCONTEXT _prev_ctx = nullptr;
        PCONTEXT _active_ctx = nullptr;
        DWORD _ctx_flags = 0;
        DWORD _context_size = 0;
        bool _ctx_changed = false;
        // if AVX supported we load these with the Ymm registers in load_context; not these are the *upper* 128 bits of the
        // ymm registers, the lower 128 bits are aliased into the xmm registers
        PM128A _ymm = nullptr;

        constexpr auto kRaxIndex = static_cast<size_t>(RegisterInfo::Register::rax);
        constexpr auto kRegisterCount = static_cast<size_t>(RegisterInfo::Register::kInvalid) - kRaxIndex;
        uint64_t _changed_registers[kRegisterCount];
        size_t _changed_reg_count = 0;
    }  // namespace runtime

    namespace detail
    {
        using iterator = changed_registers::iterator;
        void iterator_advance(iterator& iter)
        {
            while(iter._i < runtime::kRegisterCount && runtime::_changed_registers[iter._i] == 0)
            {
                ++iter._i;
            }
            if(iter._i < runtime::kRegisterCount)
            {
                iter._val = std::make_pair(static_cast<RegisterInfo::Register>(iter._i + runtime::kRaxIndex), runtime::_changed_registers[iter._i]);
            }
        }

        iterator changed_registers::end() const
        {
            iterator iter;
            iter._i = runtime::kRegisterCount;
            return iter;
        }

        iterator changed_registers::begin() const
        {
            iterator iter;
            iterator_advance(iter);
            return iter;
        }

        size_t changed_registers::size() const
        {
            return runtime::_changed_reg_count;
        }

        iterator iterator::operator++()
        {
            ++_i;
            iterator_advance(*this);
            return *this;
        }

        iterator iterator::operator++(int)
        {
            auto now = *this;
            this->operator++();
            return now;
        }

        detail::changed_registers::iterator::reference iterator::operator*()
        {
            return _val;
        }

        detail::changed_registers::iterator::pointer iterator::operator->()
        {
            return &_val;
        }

        bool iterator::operator==(const iterator& rhs)
        {
            return _i == rhs._i;
        }

        bool iterator::operator!=(const iterator& rhs)
        {
            return _i != rhs._i;
        }
    }  // namespace detail

    namespace runtime
    {
        detail::changed_registers ChangedRegisters()
        {
            return {};
        }

        void check_register_changes()
        {
            memset(_changed_registers, 0, sizeof(_changed_registers));

            const auto reg_delta_mask = [](const size_t size, const uint8_t* ra, const uint8_t* rb) -> uint64_t {
                uint64_t mask = 0;
                for(auto n = 0; n < size; ++n)
                    mask |= (ra[n] != rb[n] ? 1 : 0) << n;
                return mask;
            };

#define INASM64_RT_REG_DELTA(reg)                                                                                                                                                                                              \
    if(_active_ctx->R##reg != _prev_ctx->R##reg)                                                                                                                                                                               \
    {                                                                                                                                                                                                                          \
        _changed_registers[static_cast<size_t>(RegisterInfo::Register::r##reg) - kRaxIndex] = reg_delta_mask(8, reinterpret_cast<const uint8_t*>(&_active_ctx->R##reg), reinterpret_cast<const uint8_t*>(&_prev_ctx->R##reg)); \
    }

            INASM64_RT_REG_DELTA(ax);
            INASM64_RT_REG_DELTA(bx);
            INASM64_RT_REG_DELTA(cx);
            INASM64_RT_REG_DELTA(dx);
            INASM64_RT_REG_DELTA(si);
            INASM64_RT_REG_DELTA(di);
            INASM64_RT_REG_DELTA(sp);
            INASM64_RT_REG_DELTA(bp);
            INASM64_RT_REG_DELTA(8);
            INASM64_RT_REG_DELTA(9);
            INASM64_RT_REG_DELTA(10);
            INASM64_RT_REG_DELTA(11);
            INASM64_RT_REG_DELTA(12);
            INASM64_RT_REG_DELTA(13);
            INASM64_RT_REG_DELTA(14);
            INASM64_RT_REG_DELTA(15);

#define INASM64_RT_XREG_DELTA(reg)                                                                                                                                  \
    if(_active_ctx->Xmm##reg.Low != _prev_ctx->Xmm##reg.Low || _active_ctx->Xmm##reg.High != _prev_ctx->Xmm##reg.High)                                              \
    {                                                                                                                                                               \
        auto mask = reg_delta_mask(8, reinterpret_cast<const uint8_t*>(&_active_ctx->Xmm##reg.Low), reinterpret_cast<const uint8_t*>(&_prev_ctx->Xmm##reg.Low));    \
        mask |= reg_delta_mask(8, reinterpret_cast<const uint8_t*>(&_active_ctx->Xmm##reg.High), reinterpret_cast<const uint8_t*>(&_prev_ctx->Xmm##reg.High)) << 8; \
        _changed_registers[static_cast<size_t>(RegisterInfo::Register::xmm##reg) - kRaxIndex] = mask;                                                               \
    }

            INASM64_RT_XREG_DELTA(0);
            INASM64_RT_XREG_DELTA(1);
            INASM64_RT_XREG_DELTA(2);
            INASM64_RT_XREG_DELTA(3);
            INASM64_RT_XREG_DELTA(4);
            INASM64_RT_XREG_DELTA(5);
            INASM64_RT_XREG_DELTA(6);
            INASM64_RT_XREG_DELTA(7);
            INASM64_RT_XREG_DELTA(8);
            INASM64_RT_XREG_DELTA(9);
            INASM64_RT_XREG_DELTA(10);
            INASM64_RT_XREG_DELTA(11);
            INASM64_RT_XREG_DELTA(12);
            INASM64_RT_XREG_DELTA(13);
            INASM64_RT_XREG_DELTA(14);
            INASM64_RT_XREG_DELTA(15);

            if(_active_ctx->EFlags != _prev_ctx->EFlags)
            {
                _changed_registers[static_cast<size_t>(RegisterInfo::Register::eflags) - kRaxIndex] = reg_delta_mask(4, reinterpret_cast<const uint8_t*>(&_active_ctx->EFlags), reinterpret_cast<const uint8_t*>(&_prev_ctx->EFlags));
            }
        }  // namespace runtime

        bool load_context(HANDLE thread)
        {
            if(!_active_ctx)
            {
                // https://docs.microsoft.com/en-us/windows/desktop/debug/working-with-xstate-context

                // initialise the CONTEXT structure so that it can hold AVX extensions if they are available
                const auto feature_mask = GetEnabledXStateFeatures();
                const auto xstate_mask = (feature_mask & XSTATE_MASK_AVX) ? CONTEXT_XSTATE : 0;
                _ctx_flags = CONTEXT_ALL | xstate_mask;
                _context_size = 0;
                InitializeContext(nullptr, _ctx_flags, nullptr, &_context_size);
                auto buffer = malloc(_context_size);
                ZeroMemory(buffer, _context_size);
                InitializeContext(buffer, _ctx_flags, &_active_ctx, &_context_size);

                buffer = malloc(_context_size);
                ZeroMemory(buffer, _context_size);
                _prev_ctx = reinterpret_cast<PCONTEXT>(buffer);
                InitializeContext(buffer, _ctx_flags, &_prev_ctx, &_context_size);

                if(ExtendedCpuFeatureSupported(ExtendedCpuFeature::kAvx))
                {
                    // poke AVX to get the ymm registers enabled in the context (see below)
                    (void)_mm256_setzero_pd();
                }
            }
            _active_ctx->ContextFlags = _ctx_flags;
            //NOTE: unsupported masks are ignored as per documentation of this function, so it is safe to always set them
            SetXStateFeaturesMask(_active_ctx, XSTATE_MASK_AVX | XSTATE_MASK_AVX512);

            // load Ymm registers, if supported
            if(ExtendedCpuFeatureSupported(ExtendedCpuFeature::kAvx))
            {
                DWORD64 featuremask;
                if(GetXStateFeaturesMask(const_cast<PCONTEXT>(_active_ctx), &featuremask))
                {
                    if((featuremask & XSTATE_MASK_AVX) == XSTATE_MASK_AVX)
                    {
                        DWORD featureLength = 0;
                        _ymm = (PM128A)LocateXStateFeature(const_cast<PCONTEXT>(_active_ctx), XSTATE_AVX, &featureLength);
                    }
                    // not set yet
                }
                else
                //zzz: general error, under what conditions can this happen?
                {
                    _ymm = nullptr;
                }
            }

            // update tracking context
            CopyContext(_prev_ctx, _ctx_flags, _active_ctx);
            const auto result = GetThreadContext(thread, _active_ctx) == TRUE;
            if(result)
                check_register_changes();
            return result;
        }

        void set_next_instruction_address(LPCVOID at)
        {
            _active_ctx->Rip = DWORD_PTR(at);
            _ctx_changed = true;
        }

        void enable_trap_flag()
        {
            // set trap flag for next instr.
            _active_ctx->EFlags |= 0x100;
            _ctx_changed = true;
        }

        HANDLE active_thread()
        {
            return OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, _dbg_event.dwThreadId);
        }

        bool Start(size_t scratchPadSize)
        {
            if(_flags._running)
                return false;

            ZeroMemory(&_flags, sizeof(_flags));

            char exeFilePathName[MAX_PATH];
            if(GetModuleFileNameA(nullptr, exeFilePathName, sizeof(exeFilePathName)))
            {
                _startupinfo_a = { 0 };
                _startupinfo_a.cb = sizeof(_startupinfo_a);
                _processinfo = { 0 };

                // see inasm64::kTrapModeArgumentValue
                static auto debuggeeCommandLine = "262";
                _flags._running = CreateProcessA(exeFilePathName, const_cast<LPSTR>(debuggeeCommandLine), nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &_startupinfo_a, &_processinfo) == TRUE ? true : false;

                if(!_flags._running)
                    return false;

                // cycle through debug events to load the application up to the first breakpoint in ntdll!LdrpDoCodeRunnerBreak, then initialise the process scratch memory
                // and leave the process hanging until someone calls Step (or quits)
                while(!_flags._started && _flags._running)
                {
                    WaitForDebugEvent(&_dbg_event, INFINITE);

                    switch(_dbg_event.dwDebugEventCode)
                    {
                    case EXCEPTION_DEBUG_EVENT:
                    {
                        _continue_status = DBG_EXCEPTION_HANDLED;
                        switch(_dbg_event.u.Exception.ExceptionRecord.ExceptionCode)
                        {
                        case EXCEPTION_BREAKPOINT:
                        {
                            if(!_flags._started)
                            {
                                // get full access handle to the process
                                _process_vm = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, _processinfo.dwProcessId);

                                _scratch_size = scratchPadSize;
                                _scratch_memory = _code = _code_end = reinterpret_cast<unsigned char*>(VirtualAllocEx(_process_vm, nullptr, SIZE_T(scratchPadSize), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                                if(_scratch_memory)
                                {
                                    const auto thread = active_thread();

                                    // set the trap flag so that the first instruction in the code scratch area will be intercepted when it executes
                                    if(load_context(thread))
                                    {
                                        // set the next instruction to the beginning of the code scratch area (expecting it will be filled with valid code by someone calling AddCode shortly)
                                        set_next_instruction_address(_code);
                                        enable_trap_flag();
                                        SetThreadContext(thread, _active_ctx);
                                        _ctx_changed = false;

                                        _variables["execip"] = uintptr_t(_code);
                                        _variables["startip"] = uintptr_t(_code);
                                        _variables["endip"] = uintptr_t(_code);
                                        _variables["codesize"] = 0;
                                    }
                                    // else a serious error, report or silentl ignore?

                                    CloseHandle(thread);
                                }
                                else
                                {
                                    detail::set_error(Error::kSystemError);
                                    return false;
                                }

                                _flags._started = true;
                            }
                            // else: we ignore any other breakpoints since we don't set any explicitly ourselves
                        }
                        break;
                        default:
                            break;
                        }
                    }
                    break;

                    case EXIT_PROCESS_DEBUG_EVENT:
                        _flags._running = false;
                        break;

                    default:
                        break;
                    }

                    // only continune until we get the first breakpoint, afterwards we deliberately "hang" the debugeee
                    if(!_flags._started)
                    {
                        ContinueDebugEvent(_dbg_event.dwProcessId,
                            _dbg_event.dwThreadId,
                            _continue_status);
                    }
                }
            }
            else
            {
                detail::set_error(Error::kSystemError);
            }

            return _flags._started;
        }
        void Shutdown()
        {
            if(_flags._running)
            {
                VirtualFreeEx(_process_vm, _scratch_memory, 0, MEM_RELEASE);
                CloseHandle(_process_vm);
                TerminateProcess(_process_vm, 1);
                CloseHandle(_processinfo.hThread);
                CloseHandle(_processinfo.hProcess);
                _code = _scratch_memory = nullptr;
                _scratch_size = 0;
                free(_active_ctx);
                _active_ctx = nullptr;
            }
            ZeroMemory(&_flags, sizeof(_flags));
        }

        void Reset()
        {
            if(!_flags._started)
                return;
            _code = _scratch_memory;
            //ZZZ: untested
            _instruction_line = _first_instruction_line = _last_instruction_line = 0;
        }

        instruction_index_t AddInstruction(const void* bytes, size_t size)
        {
            if(!size)
                return {};

            if(_commit_size + size > _scratch_size)
            {
                detail::set_error(Error::kCodeBufferOverflow);
                return {};
            }

            instruction_line_info_t line;
            line._line = _instruction_line;
            memcpy(line._instruction_bytes, bytes, size);
            line._instruction_size = size;
            // relative to previous instruction, or just start of code buffer
            line._address = _instruction_line ? (_loaded_instructions[_instruction_line - 1]._address + _loaded_instructions[_instruction_line - 1]._instruction_size) : uintptr_t(_code);
            _commit_size += size;

            if(_instruction_line == _last_instruction_line)
            {
                _loaded_instructions.emplace_back(std::move(line));
                _instruction_line = ++_last_instruction_line;
            }
            else
            {
                const auto instruction_size_delta = int(size) - int(_loaded_instructions[_instruction_line]._instruction_size);
                _loaded_instructions[_instruction_line] = std::move(line);
                if(instruction_size_delta)
                {
                    // fix up all subsequent instruction addresses if the new instruction is different in size from what it was previously
                    for(size_t l = _instruction_line + 1ull; l < _last_instruction_line; ++l)
                    {
                        _loaded_instructions[l]._address = uintptr_t((long long)(_loaded_instructions[l]._address) + instruction_size_delta);
                    }
                }
                //NOTE: _first_instruction_line is modified by SetInstructionLine
                ++_instruction_line;
            }
            return { line._line, line._address };
        }

        bool SetInstructionLine(size_t line)
        {
            if(line > _last_instruction_line)
                return false;
            if(line < _first_instruction_line)
                _first_instruction_line = line;
            _instruction_line = line;
            return true;
        }

        instruction_index_t NextInstructionIndex()
        {
            if(_loaded_instructions.empty() || !_instruction_line)
                return {
                    0,
                    uintptr_t(_code)
                };
            return {
                _instruction_line,
                _loaded_instructions[_instruction_line - 1]._address + _loaded_instructions[_instruction_line - 1]._instruction_size
            };
        }

        bool SetNextExecuteLine(size_t line)
        {
            if(line >= _last_instruction_line)
            {
                detail::set_error(Error::kInvalidAddress);
                return false;
            }
            set_next_instruction_address(LPCVOID(_loaded_instructions[line]._address));
            _code = reinterpret_cast<unsigned char*>(_loaded_instructions[line]._address);
            return true;
        }

        bool GetVariable(const char* name, uintptr_t& value)
        {
            // check line number variables first
            if(name[0] == 'l')
            {
                char buffer[12];
                auto rp = name;
                auto bp = buffer;
                while(rp[0] && isdigit(rp[0]))
                {
                    *bp++ = *rp++;
                }
                bp[0] = 0;
                if(rp[0])
                {
                    const auto index = ::strtol(bp, nullptr, 10);
                    if(!errno && index >= 0 && index < _last_instruction_line)
                    {
                        value = uintptr_t(_loaded_instructions[index]._address);
                        return true;
                    }
                }
            }
            else
            {
                const auto i = _variables.find(name);
                if(i != _variables.end())
                {
                    value = i->second;
                    return true;
                }
            }
            detail::set_error(Error::kUndefinedVariable);
            return false;
        }

        bool CommmitInstructions()
        {
            if(_last_instruction_line == _first_instruction_line)
            {
                detail::set_error(Error::kNoMoreCode);
                return false;
            }

            if(!_flags._started)
            {
                detail::set_error(Error::kRuntimeUninitialised);
                return false;
            }

            for(size_t l = _first_instruction_line; l < _last_instruction_line; ++l)
            {
                SIZE_T written;
                if(WriteProcessMemory(_process_vm, LPVOID(_loaded_instructions[l]._address),
                       _loaded_instructions[l]._instruction_bytes,
                       SIZE_T(_loaded_instructions[l]._instruction_size), &written) == TRUE &&
                    size_t(written) != _loaded_instructions[l]._instruction_size)
                {
                    detail::set_error(Error::kSystemError);
                    return false;
                }
            }
            _instruction_line = _first_instruction_line = _last_instruction_line;
            if(_loaded_instructions[_last_instruction_line - 1]._address >= uintptr_t(_code_end))
                _code_end = reinterpret_cast<unsigned char*>(_loaded_instructions[_last_instruction_line - 1]._address + _loaded_instructions[_last_instruction_line - 1]._instruction_size);
            _commit_size = 0;
            return true;
        }

        bool Step()
        {
            // not started
            if(!_flags._started)
            {
                detail::set_error(Error::kRuntimeUninitialised);
                return false;
            }

            // no more code to execute
            if(_code == _code_end)
            {
                detail::set_error(Error::kNoMoreCode);
                return false;
            }

            if(_ctx_changed)
            {
                // update thread context before we execute, if there are changes
                SetThreadContext(active_thread(), _active_ctx);
                _ctx_changed = false;
            }

            auto stepped = false;
            while(!stepped && _flags._running)
            {
                // debuggee is always suspended when we get here, so let it run up to the next event.
                // when stepping this executes the current instruction and traps
                ContinueDebugEvent(_dbg_event.dwProcessId,
                    _dbg_event.dwThreadId,
                    _continue_status);

                WaitForDebugEvent(&_dbg_event, INFINITE);

                switch(_dbg_event.dwDebugEventCode)
                {
                case EXCEPTION_DEBUG_EVENT:
                {
                    _continue_status = DBG_EXCEPTION_HANDLED;
                    switch(_dbg_event.u.Exception.ExceptionRecord.ExceptionCode)
                    {
                    // the ones we care about
                    case EXCEPTION_SINGLE_STEP:
                    {
                        const auto thread = active_thread();
                        if(!thread)
                        {
                            detail::set_error(Error::kSystemError);
                            return false;
                        }

                        // advance the code pointer to the next instruction
                        const auto next_instr = reinterpret_cast<unsigned char*>(_dbg_event.u.Exception.ExceptionRecord.ExceptionAddress);
                        _code = next_instr;

                        // refresh the context and re-set the trap flag
                        if(load_context(thread))
                        {
                            enable_trap_flag();
                            SetThreadContext(thread, _active_ctx);
                        }

                        CloseHandle(thread);
                        stepped = true;
                    }
                    break;
                    case STATUS_ACCESS_VIOLATION:
                        //TODO: handle this nicely, report back etc.
                        detail::set_error(Error::kAccessViolation);
                        _flags._running = false;
                        break;
                    case STATUS_SEGMENT_NOTIFICATION:
                        /*
                        {SEGMENT LOAD}A VIRTUAL DOS MACHINE (VDM) IS LOADING, UNLOADING, OR MOVING AN MS-DOS OR WIN16 PROGRAM SEGMENT IMAGE.
                        AN EXCEPTION IS RAISED SO A DEBUGGER CAN LOAD, UNLOAD OR TRACK SYMBOLS AND BREAKPOINTS WITHIN THESE 16-BIT SEGMENTS.
                        */
                    default:
                        _continue_status = DBG_CONTINUE;
                        break;
                    }
                }
                break;
                case EXIT_PROCESS_DEBUG_EVENT:
                    _flags._running = false;
                default:
                    _continue_status = DBG_CONTINUE;
                    break;
                }
            }

            return _flags._running && stepped;
        }

        const void* InstructionPointer()
        {
            if(!_flags._started)
                return nullptr;

            return _code;
        }

        bool SetNextInstruction(const void* at)
        {
            if(!_flags._started || (at < _scratch_memory || at >= _code_end))
            {
                detail::set_error(Error::kInvalidAddress);
                return false;
            }

            const auto thread = active_thread();
            if(thread)
            {
                set_next_instruction_address(at);
                SetThreadContext(thread, _active_ctx);
                _code = reinterpret_cast<unsigned char*>(_active_ctx->Rip);
                _ctx_changed = false;
                CloseHandle(thread);
                return true;
            }

            detail::set_error(Error::kSystemError);
            return false;
        }

        const void* AllocateMemory(size_t size)
        {
            const auto handle = VirtualAllocEx(_process_vm, nullptr, SIZE_T(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if(handle)
            {
                _allocations[uintptr_t(handle)] = size;
                return handle;
            }
            detail::set_error(Error::kSystemError);
            return nullptr;
        }

        bool WriteBytes(const void* handle, const void* src, size_t length)
        {
            const auto i = _allocations.find(uintptr_t(handle));
            if(i != _allocations.end())
            {
                if(length <= i->second)
                {
                    SIZE_T written;
                    WriteProcessMemory(_process_vm, LPVOID(handle), src, length, &written);
                    return written == length;
                }
                detail::set_error(Error::kMemoryWriteSizeMismatch);
            }
            return false;
        }

        bool ReadBytes(const void* handle, void* dest, size_t length)
        {
            const auto i = _allocations.find(uintptr_t(handle));
            if(i != _allocations.end())
            {
                if(length <= i->second)
                {
                    SIZE_T read;
                    ReadProcessMemory(_process_vm, LPVOID(handle), dest, length, &read);
                    return read == length;
                }
                detail::set_error(Error::kMemoryReadSizeMismatch);
            }
            return false;
        }

        size_t AllocationSize(const void* handle)
        {
            const auto i = _allocations.find(uintptr_t(handle));
            if(i != _allocations.end())
                return i->second;
            return 0;
        }

        bool SetReg(const RegisterInfo& reg, const void* data, size_t size)
        {
            assert(reg._bit_width / 8 <= size);
            assert(data);
            assert(reg._register != RegisterInfo::Register::kInvalid);
            if(!_flags._started)
            {
                detail::set_error(Error::kRuntimeUninitialised);
                return false;
            }

            if(reg._class == RegisterInfo::RegClass::kSegment || reg._class == RegisterInfo::RegClass::kFlags)
                return false;

            auto ok = false;
            char* reg_ptr = nullptr;
            auto data_ptr = reinterpret_cast<const char*>(data);
            switch(reg._greatest_enclosing_register)
            {
#define RT_GPRREG_SET(letter)                                                           \
    case RegisterInfo::Register::r##letter##x:                                          \
        switch(reg._register)                                                           \
        {                                                                               \
        case RegisterInfo::Register::##letter##l:                                       \
            if(size == 1)                                                               \
            {                                                                           \
                _active_ctx->R##letter##x &= ~0xff;                                     \
                _active_ctx->R##letter##x |= DWORD64(data_ptr[0]);                      \
                ok = true;                                                              \
            }                                                                           \
            break;                                                                      \
        case RegisterInfo::Register::##letter##h:                                       \
            if(size == 1)                                                               \
            {                                                                           \
                _active_ctx->R##letter##x &= ~0xff00;                                   \
                _active_ctx->R##letter##x |= DWORD64(data_ptr[0] << 8);                 \
                ok = true;                                                              \
            }                                                                           \
            break;                                                                      \
        case RegisterInfo::Register::##letter##x:                                       \
            if(size == 2)                                                               \
            {                                                                           \
                _active_ctx->R##letter##x &= ~0xffff;                                   \
                _active_ctx->R##letter##x |= DWORD64(data_ptr[0] | (data_ptr[1] << 8)); \
                ok = true;                                                              \
            }                                                                           \
            break;                                                                      \
        case RegisterInfo::Register::e##letter##x:                                      \
        case RegisterInfo::Register::r##letter##x:                                      \
            if(size >= 4)                                                               \
            {                                                                           \
                reg_ptr = reinterpret_cast<char*>(&_active_ctx->R##letter##x);          \
                ok = true;                                                              \
            }                                                                           \
            break;                                                                      \
        default:;                                                                       \
        }                                                                               \
        break
                RT_GPRREG_SET(a);
                RT_GPRREG_SET(b);
                RT_GPRREG_SET(c);
                RT_GPRREG_SET(d);
            case RegisterInfo::Register::rsi:
                switch(reg._register)
                {
                case RegisterInfo::Register::sil:
                    if(size == 1)
                    {
                        _active_ctx->Rsi &= ~0xff;
                        _active_ctx->Rsi |= DWORD64(data_ptr[0]);
                        ok = true;
                    }
                    break;
                case RegisterInfo::Register::esi:
                case RegisterInfo::Register::rsi:
                    if(size >= 4)
                        reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rsi);
                    break;
                }
                break;
            case RegisterInfo::Register::rdi:
                switch(reg._register)
                {
                case RegisterInfo::Register::dil:
                    if(size == 1)
                    {
                        _active_ctx->Rdi &= ~0xff;
                        _active_ctx->Rdi |= DWORD64(data_ptr[0]);
                        ok = true;
                    }
                    break;
                case RegisterInfo::Register::edi:
                case RegisterInfo::Register::rdi:
                    if(size >= 4)
                        reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rdi);
                    break;
                }
                break;
            case RegisterInfo::Register::rsp:
                switch(reg._register)
                {
                case RegisterInfo::Register::spl:
                    if(size == 1)
                    {
                        _active_ctx->Rsp &= ~0xff;
                        _active_ctx->Rsp |= DWORD64(data_ptr[0]);
                        ok = true;
                    }
                    break;
                case RegisterInfo::Register::esp:
                case RegisterInfo::Register::rsp:
                    if(size >= 4)
                        reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rsp);
                    break;
                }
                break;
            case RegisterInfo::Register::rbp:
                switch(reg._register)
                {
                case RegisterInfo::Register::bpl:
                    if(size == 1)
                    {
                        _active_ctx->Rbp &= ~0xff;
                        _active_ctx->Rbp |= DWORD64(data_ptr[0]);
                        ok = true;
                    }
                    break;
                case RegisterInfo::Register::ebp:
                case RegisterInfo::Register::rbp:
                    if(size >= 4)
                        reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rbp);
                    break;
                }
                break;

#define RT_RREG_SET(index)                                                          \
    case RegisterInfo::Register::r##index:                                          \
    {                                                                               \
        switch(reg._register)                                                       \
        {                                                                           \
        case RegisterInfo::Register::r##index##b:                                   \
            if(size == 1)                                                           \
            {                                                                       \
                _active_ctx->R##index &= ~0xff;                                     \
                _active_ctx->R##index |= DWORD64(data_ptr[0]);                      \
                ok = true;                                                          \
            }                                                                       \
            break;                                                                  \
        case RegisterInfo::Register::r##index##w:                                   \
            if(size == 2)                                                           \
            {                                                                       \
                _active_ctx->R##index &= ~0xffff;                                   \
                _active_ctx->R##index |= DWORD64(data_ptr[0] | (data_ptr[1] << 8)); \
                ok = true;                                                          \
            }                                                                       \
            break;                                                                  \
        case RegisterInfo::Register::r##index##d:                                   \
        case RegisterInfo::Register::r##index##:                                    \
            if(size >= 4)                                                           \
                reg_ptr = reinterpret_cast<char*>(&_active_ctx->R##index);          \
            break;                                                                  \
        default:;                                                                   \
        }                                                                           \
    }                                                                               \
    break

                RT_RREG_SET(8);
                RT_RREG_SET(9);
                RT_RREG_SET(10);
                RT_RREG_SET(11);
                RT_RREG_SET(12);
                RT_RREG_SET(13);
                RT_RREG_SET(14);
                RT_RREG_SET(15);

                //TODO:
#define RT_XYZREG_SET(index)                                                 \
    case RegisterInfo::Register::zmm##index:                                 \
    {                                                                        \
        switch(reg._register)                                                \
        {                                                                    \
        case RegisterInfo::Register::xmm##index:                             \
            if(size == 16)                                                   \
                reg_ptr = reinterpret_cast<char*>(&_active_ctx->Xmm##index); \
            break;                                                           \
        default:;                                                            \
        }                                                                    \
    }                                                                        \
    break

                RT_XYZREG_SET(0);
                RT_XYZREG_SET(1);
                RT_XYZREG_SET(2);
                RT_XYZREG_SET(3);
                RT_XYZREG_SET(4);
                RT_XYZREG_SET(5);
                RT_XYZREG_SET(6);
                RT_XYZREG_SET(7);
                RT_XYZREG_SET(8);
                RT_XYZREG_SET(9);
                RT_XYZREG_SET(10);
                RT_XYZREG_SET(11);
                RT_XYZREG_SET(12);
                RT_XYZREG_SET(13);
                RT_XYZREG_SET(14);
                RT_XYZREG_SET(15);

            default:;
            }

            if(reg_ptr)
            {
                memcpy(reg_ptr, data_ptr, size);
                ok = true;
            }

            return _ctx_changed = ok;
        }  // namespace runtime

        bool GetReg(const RegisterInfo& reg, void* data, size_t size)
        {
            assert(size);
            assert(reg._bit_width / 8 <= size);
            assert(data);
            assert(reg._register != RegisterInfo::Register::kInvalid);
            if(!_flags._started)
            {
                detail::set_error(Error::kRuntimeUninitialised);
                return false;
            }

            if(reg._class == RegisterInfo::RegClass::kYmm)
            {
                if(_ymm)
                {
                    const auto ord = static_cast<size_t>(reg._register) - static_cast<size_t>(RegisterInfo::Register::ymm0);
                    const auto data_ptr = reinterpret_cast<uint8_t*>(data);
                    // if this asserts the context structure is not aligned so that we can just index into it
                    assert(offsetof(CONTEXT, Xmm1) - offsetof(CONTEXT, Xmm0) == sizeof(M128A));
                    assert(uintptr_t(_ymm + 1) - uintptr_t(_ymm) == sizeof(M128A));
                    // context xmm holds low 128 bits
                    memcpy(data_ptr, (&_active_ctx->Xmm0 + ord), sizeof(M128A));
                    // context ymm holds high 128 bits
                    memcpy(data_ptr + sizeof(M128A), _ymm + ord, sizeof(M128A));
                    return true;
                }
                else if(!ExtendedCpuFeatureSupported(ExtendedCpuFeature::kAvx))
                {
                    detail::set_error(Error::kUnsupportedCpuFeature);
                    return false;
                }
                // just set them to 0 if not initialised
                memset(data, 0, size);
            }
            //TODO:
            assert(reg._class != RegisterInfo::RegClass::kZmm);

            const uint8_t* reg_ptr = nullptr;
            switch(reg._class)
            {
            case RegisterInfo::RegClass::kSegment:
                assert(size == 2);
                switch(reg._register)
                {
                case RegisterInfo::Register::cs:
                    reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->SegCs);
                    break;
                case RegisterInfo::Register::ds:
                    reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->SegDs);
                    break;
                case RegisterInfo::Register::es:
                    reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->SegEs);
                    break;
                case RegisterInfo::Register::ss:
                    reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->SegSs);
                    break;
                case RegisterInfo::Register::gs:
                    reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->SegGs);
                    break;
                case RegisterInfo::Register::fs:
                    reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->SegFs);
                    break;
                }
                break;
            case RegisterInfo::RegClass::kFlags:
                assert(reg._register == RegisterInfo::Register::eflags);
                reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->EFlags);
                break;
            default:
            {
                // ========================================================================
                // All other register classes

                const auto data_ptr = reinterpret_cast<uint8_t*>(data);
                switch(reg._greatest_enclosing_register)
                {
#define RT_GPRREG_GET(letter)                                                                        \
    case RegisterInfo::Register::r##letter##x:                                                       \
    {                                                                                                \
        switch(reg._register)                                                                        \
        {                                                                                            \
        case RegisterInfo::Register::##letter##l:                                                    \
            data_ptr[0] = uint8_t(_active_ctx->R##letter##x & 0xff);                                 \
            break;                                                                                   \
        case RegisterInfo::Register::##letter##h:                                                    \
            data_ptr[0] = uint8_t((_active_ctx->R##letter##x & 0xff00) >> 8);                        \
            break;                                                                                   \
        case RegisterInfo::Register::##letter##x:                                                    \
            reinterpret_cast<uint16_t*>(data_ptr)[0] = uint16_t(_active_ctx->R##letter##x & 0xffff); \
            break;                                                                                   \
        case RegisterInfo::Register::e##letter##x:                                                   \
            assert(size >= 4);                                                                       \
        case RegisterInfo::Register::r##letter##x:                                                   \
            assert(size >= 4);                                                                       \
            reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->R##letter##x);                  \
            break;                                                                                   \
        }                                                                                            \
    }                                                                                                \
    break
                    RT_GPRREG_GET(a);
                    RT_GPRREG_GET(b);
                    RT_GPRREG_GET(c);
                    RT_GPRREG_GET(d);

                case RegisterInfo::Register::rsi:
                    switch(reg._register)
                    {
                    case RegisterInfo::Register::sil:
                        data_ptr[0] = uint8_t(_active_ctx->Rsi & 0xff);
                        break;
                    case RegisterInfo::Register::esi:
                        assert(size >= 4);
                    case RegisterInfo::Register::rsi:
                        assert(size >= 4);
                        reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->Rsi);
                        break;
                    }
                    break;
                case RegisterInfo::Register::rdi:
                    switch(reg._register)
                    {
                    case RegisterInfo::Register::dil:
                        data_ptr[0] = uint8_t(_active_ctx->Rdi & 0xff);
                        break;
                    case RegisterInfo::Register::edi:
                        assert(size >= 4);
                    case RegisterInfo::Register::rdi:
                        assert(size >= 4);
                        reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->Rdi);
                        break;
                    }
                    break;
                case RegisterInfo::Register::rsp:
                    switch(reg._register)
                    {
                    case RegisterInfo::Register::spl:
                        data_ptr[0] = uint8_t(_active_ctx->Rsp & 0xff);
                        break;
                    case RegisterInfo::Register::sp:
                        assert(size >= 2);
                        reinterpret_cast<uint16_t*>(data_ptr)[0] = uint16_t(_active_ctx->Rsp & 0xffff);
                        break;
                    case RegisterInfo::Register::esp:
                        assert(size >= 4);
                    case RegisterInfo::Register::rsp:
                        assert(size >= 4);
                        reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->Rsp);
                        break;
                    }
                    break;
                case RegisterInfo::Register::rbp:
                    switch(reg._register)
                    {
                    case RegisterInfo::Register::bpl:
                        data_ptr[0] = uint8_t(_active_ctx->Rbp & 0xff);
                        break;
                    case RegisterInfo::Register::bp:
                        assert(size >= 2);
                        reinterpret_cast<uint16_t*>(data_ptr)[0] = uint16_t(_active_ctx->Rbp & 0xffff);
                        break;
                    case RegisterInfo::Register::ebp:
                        assert(size >= 4);
                    case RegisterInfo::Register::rbp:
                        assert(size >= 4);
                        reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->Rbp);
                        break;
                    }
                    break;

#define RT_RREG_GET(index)                                                                       \
    case RegisterInfo::Register::r##index:                                                       \
    {                                                                                            \
        switch(reg._register)                                                                    \
        {                                                                                        \
        case RegisterInfo::Register::r##index##b:                                                \
            data_ptr[0] = uint8_t(_active_ctx->R##index & 0xff);                                 \
            break;                                                                               \
        case RegisterInfo::Register::r##index##w:                                                \
            assert(size >= 2);                                                                   \
            reinterpret_cast<uint16_t*>(data_ptr)[0] = uint16_t(_active_ctx->R##index & 0xffff); \
            break;                                                                               \
        case RegisterInfo::Register::r##index##d:                                                \
            assert(size >= 4);                                                                   \
        case RegisterInfo::Register::r##index##:                                                 \
            assert(size >= 4);                                                                   \
            reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->R##index);                  \
            break;                                                                               \
        default:;                                                                                \
        }                                                                                        \
    }                                                                                            \
    break

                    RT_RREG_GET(8);
                    RT_RREG_GET(9);
                    RT_RREG_GET(10);
                    RT_RREG_GET(11);
                    RT_RREG_GET(12);
                    RT_RREG_GET(13);
                    RT_RREG_GET(14);
                    RT_RREG_GET(15);

#define RT_XYZREG_GET(index)                                                      \
    case RegisterInfo::Register::zmm##index:                                      \
    {                                                                             \
        switch(reg._register)                                                     \
        {                                                                         \
        case RegisterInfo::Register::xmm##index:                                  \
            assert(size >= 16);                                                   \
            reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->Xmm##index); \
            break;                                                                \
        default:                                                                  \
            assert(false);                                                        \
            break;                                                                \
        }                                                                         \
    }                                                                             \
    break
                    RT_XYZREG_GET(0);
                    RT_XYZREG_GET(1);
                    RT_XYZREG_GET(2);
                    RT_XYZREG_GET(3);
                    RT_XYZREG_GET(4);
                    RT_XYZREG_GET(5);
                    RT_XYZREG_GET(6);
                    RT_XYZREG_GET(7);
                    RT_XYZREG_GET(8);
                    RT_XYZREG_GET(9);
                    RT_XYZREG_GET(10);
                    RT_XYZREG_GET(11);
                    RT_XYZREG_GET(12);
                    RT_XYZREG_GET(13);
                    RT_XYZREG_GET(14);
                    RT_XYZREG_GET(15);
                default:;
                }
            }
            break;
            }

            if(reg_ptr)
            {
                memcpy(data, reg_ptr, size);
            }
            return true;
        }  // namespace runtime
    }      // namespace runtime
}  // namespace inasm64
