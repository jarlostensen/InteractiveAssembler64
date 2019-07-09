
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <debugapi.h>
#include <unordered_map>
#include <cassert>

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

        struct
        {
            bool _started : 1;
            bool _running : 1;
        } _flags = { 0 };

        unsigned char* _scratch_memory = nullptr;
        unsigned char* _scratch_wp = nullptr;
        size_t _scratch_size = 0;
        unsigned char* _code = nullptr;

        DEBUG_EVENT _dbg_event = { 0 };
        DWORD _continue_status = DBG_CONTINUE;

        PCONTEXT _active_ctx = nullptr;
        DWORD _ctx_flags = 0;
        DWORD _context_size = 0;
        bool _ctx_changed = false;
        ExecutionContext _rt_context = { 0 };

        bool LoadContext(HANDLE thread)
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
                const auto buffer = malloc(_context_size);
                ZeroMemory(buffer, _context_size);
                InitializeContext(buffer, _ctx_flags, &_active_ctx, &_context_size);
                _rt_context.OsContext = _active_ctx;
            }
            _active_ctx->ContextFlags = _ctx_flags;
            //NOTE: unsupported masks are ignored as per documentation of this function, so it is safe to always set them
            SetXStateFeaturesMask(_active_ctx, XSTATE_MASK_AVX | XSTATE_MASK_AVX512);
            return GetThreadContext(thread, _active_ctx) == TRUE;
        }

        void SetNexInstructionAddress(LPCVOID at)
        {
            _active_ctx->Rip = DWORD_PTR(at);
            _ctx_changed = true;
        }

        void EnableTrapFlag()
        {
            // set trap flag for next instr.
            _active_ctx->EFlags |= 0x100;
            _ctx_changed = true;
        }

        HANDLE ActiveThread()
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
                                _scratch_memory = _scratch_wp = _code = reinterpret_cast<unsigned char*>(VirtualAllocEx(_process_vm, nullptr, SIZE_T(scratchPadSize), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                                if(_scratch_memory)
                                {
                                    const auto thread = ActiveThread();

                                    // set the trap flag so that the first instruction in the code scratch area will be intercepted when it executes
                                    if(LoadContext(thread))
                                    {
                                        // set the next instruction to the beginning of the code scratch area (expecting it will be filled with valid code by someone calling AddCode shortly)
                                        SetNexInstructionAddress(_code);
                                        EnableTrapFlag();
                                        SetThreadContext(thread, _active_ctx);
                                        _ctx_changed = false;
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
                _code = _scratch_memory = _scratch_wp = nullptr;
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

            _scratch_wp = _code = _scratch_memory;
        }

        bool AddCode(const void* code, size_t size)
        {
            if(!_flags._started)
                return false;

            if(_scratch_size - size_t(_scratch_wp - _scratch_memory) < size)
            {
                detail::set_error(Error::kCodeBufferFull);
                return false;
            }

            SIZE_T written;
            if(WriteProcessMemory(_process_vm, _scratch_wp, code, SIZE_T(size), &written) == TRUE && size_t(written) == size)
            {
                FlushInstructionCache(_process_vm, _scratch_wp, SIZE_T(size));
                _scratch_wp += size;
                return true;
            }
            detail::set_error(Error::kSystemError);
            return false;
        }

        bool Step()
        {
            // not started, or no code loaded
            if(!_flags._started || _scratch_wp == _scratch_memory)
            {
                detail::set_error(Error::kNoMoreCode);
                return false;
            }

            // no more code to execute
            if(_code == _scratch_wp)
            {
                detail::set_error(Error::kNoMoreCode);
                return false;
            }

            if(_ctx_changed)
            {
                // update thread context before we execute, if there are changes
                SetThreadContext(ActiveThread(), _active_ctx);
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
                        const auto thread = ActiveThread();
                        if(!thread)
                        {
                            detail::set_error(Error::kSystemError);
                            return false;
                        }

                        // advance the code pointer to the next instruction
                        const auto next_instr = reinterpret_cast<unsigned char*>(_dbg_event.u.Exception.ExceptionRecord.ExceptionAddress);
                        _rt_context.InstructionSize = size_t(next_instr - _code);
                        //TODO: check that instruction size isn't > max
                        //TODO: when we have the assembler running we will get this information when the actual code is assembled and store it
                        //      in an map keyed by address.
                        SIZE_T written;
                        ReadProcessMemory(_process_vm, _code, _rt_context.Instruction, _rt_context.InstructionSize, &written);
                        //TODO: check for error conditions
                        _code = next_instr;

                        // refresh the context and re-set the trap flag
                        if(LoadContext(thread))
                        {
                            EnableTrapFlag();
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

        const ExecutionContext* Context()
        {
            if(!_flags._started)
                return nullptr;

            return &_rt_context;
        }

        const void* InstructionPointer()
        {
            if(!_flags._started)
                return nullptr;

            return _code;
        }

        const void* InstructionWriteAddress()
        {
            return _scratch_wp;
        }

        bool SetNextInstruction(const void* at)
        {
            if(!_flags._started || (at < _scratch_memory || at >= _scratch_wp))
            {
                detail::set_error(Error::kInvalidAddress);
                return false;
            }

            const auto thread = ActiveThread();
            if(thread)
            {
                SetNexInstructionAddress(at);
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

            char* reg_ptr = nullptr;
            auto data_ptr = reinterpret_cast<const char*>(data);
            switch(reg._greatest_enclosing_register)
            {
#define RT_GPRREG_SET(letter)                                                       \
    case RegisterInfo::Register::r##letter##x:                                      \
        switch(reg._register)                                                       \
        {                                                                           \
        case RegisterInfo::Register::##letter##l:                                   \
            assert(size == 1);                                                      \
            _active_ctx->R##letter##x &= ~0xff;                                     \
            _active_ctx->R##letter##x |= DWORD64(data_ptr[0]);                      \
            break;                                                                  \
        case RegisterInfo::Register::##letter##h:                                   \
            assert(size == 1);                                                      \
            _active_ctx->R##letter##x &= ~0xff00;                                   \
            _active_ctx->R##letter##x |= DWORD64(data_ptr[0] << 8);                 \
            break;                                                                  \
        case RegisterInfo::Register::##letter##x:                                   \
            assert(size == 2);                                                      \
            _active_ctx->R##letter##x &= ~0xffff;                                   \
            _active_ctx->R##letter##x |= DWORD64(data_ptr[0] | (data_ptr[1] << 8)); \
            break;                                                                  \
        case RegisterInfo::Register::e##letter##x:                                  \
            assert(size == 4);                                                      \
        case RegisterInfo::Register::r##letter##x:                                  \
            assert(size >= 4);                                                      \
            reg_ptr = reinterpret_cast<char*>(&_active_ctx->R##letter##x);          \
            break;                                                                  \
        default:;                                                                   \
        }                                                                           \
        break
                RT_GPRREG_SET(a);
                RT_GPRREG_SET(b);
                RT_GPRREG_SET(c);
                RT_GPRREG_SET(d);
            case RegisterInfo::Register::rsi:
                switch(reg._register)
                {
                case RegisterInfo::Register::sil:
                    assert(size == 1);
                    _active_ctx->Rsi &= ~0xff;
                    _active_ctx->Rsi |= DWORD64(data_ptr[0]);
                    break;
                case RegisterInfo::Register::esi:
                    assert(size == 4);
                case RegisterInfo::Register::rsi:
                    assert(size >= 4);
                    reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rsi);
                    break;
                }
                break;
            case RegisterInfo::Register::rdi:
                switch(reg._register)
                {
                case RegisterInfo::Register::dil:
                    assert(size == 1);
                    _active_ctx->Rdi &= ~0xff;
                    _active_ctx->Rdi |= DWORD64(data_ptr[0]);
                    break;
                case RegisterInfo::Register::edi:
                    assert(size == 4);
                case RegisterInfo::Register::rdi:
                    assert(size >= 4);
                    reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rdi);
                    break;
                }
                break;
            case RegisterInfo::Register::rsp:
                switch(reg._register)
                {
                case RegisterInfo::Register::spl:
                    assert(size == 1);
                    _active_ctx->Rsp &= ~0xff;
                    _active_ctx->Rsp |= DWORD64(data_ptr[0]);
                    break;
                case RegisterInfo::Register::esp:
                    assert(size == 4);
                case RegisterInfo::Register::rsp:
                    assert(size >= 4);
                    reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rsp);
                    break;
                }
                break;
            case RegisterInfo::Register::rbp:
                switch(reg._register)
                {
                case RegisterInfo::Register::bpl:
                    assert(size == 1);
                    _active_ctx->Rbp &= ~0xff;
                    _active_ctx->Rbp |= DWORD64(data_ptr[0]);
                    break;
                case RegisterInfo::Register::ebp:
                    assert(size == 4);
                case RegisterInfo::Register::rbp:
                    assert(size >= 4);
                    reg_ptr = reinterpret_cast<char*>(&_active_ctx->Rbp);
                    break;
                }
                break;

#define RT_RREG_SET(index)                                                      \
    case RegisterInfo::Register::r##index:                                      \
    {                                                                           \
        switch(reg._register)                                                   \
        {                                                                       \
        case RegisterInfo::Register::r##index##b:                               \
            assert(size == 1);                                                  \
            _active_ctx->R##index &= ~0xff;                                     \
            _active_ctx->R##index |= DWORD64(data_ptr[0]);                      \
            break;                                                              \
        case RegisterInfo::Register::r##index##w:                               \
            assert(size == 2);                                                  \
            _active_ctx->R##index &= ~0xffff;                                   \
            _active_ctx->R##index |= DWORD64(data_ptr[0] | (data_ptr[1] << 8)); \
            break;                                                              \
        case RegisterInfo::Register::r##index##d:                               \
            assert(size == 4);                                                  \
        case RegisterInfo::Register::r##index##:                                \
            assert(size >= 4);                                                  \
            reg_ptr = reinterpret_cast<char*>(&_active_ctx->R##index);          \
            break;                                                              \
        default:;                                                               \
        }                                                                       \
    }                                                                           \
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
#define RT_XYZREG_SET(index)                                             \
    case RegisterInfo::Register::zmm##index:                             \
    {                                                                    \
        switch(reg._register)                                            \
        {                                                                \
        case RegisterInfo::Register::xmm##index:                         \
            assert(size == 16);                                          \
            reg_ptr = reinterpret_cast<char*>(&_active_ctx->Xmm##index); \
            break;                                                       \
        default:                                                         \
            assert(false);                                               \
            break;                                                       \
        }                                                                \
    }                                                                    \
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
            }

            _ctx_changed = true;

            return true;
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
            assert(size == 4);                                                                       \
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
                        assert(size == 1);
                        data_ptr[0] = uint8_t(_active_ctx->Rsi & 0xff);
                        break;
                    case RegisterInfo::Register::esi:
                        assert(size == 4);
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
                        assert(size == 1);
                        data_ptr[0] = uint8_t(_active_ctx->Rdi & 0xff);
                        break;
                    case RegisterInfo::Register::edi:
                        assert(size == 4);
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
                        assert(size == 1);
                        data_ptr[0] = uint8_t(_active_ctx->Rsp & 0xff);
                        break;
                    case RegisterInfo::Register::esp:
                        assert(size == 4);
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
                        assert(size == 1);
                        data_ptr[0] = uint8_t(_active_ctx->Rbp & 0xff);
                        break;
                    case RegisterInfo::Register::ebp:
                        assert(size == 4);
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
            assert(size == 1);                                                                   \
            data_ptr[0] = uint8_t(_active_ctx->R##index & 0xff);                                 \
            break;                                                                               \
        case RegisterInfo::Register::r##index##w:                                                \
            assert(size == 2);                                                                   \
            reinterpret_cast<uint16_t*>(data_ptr)[0] = uint16_t(_active_ctx->R##index & 0xffff); \
            break;                                                                               \
        case RegisterInfo::Register::r##index##d:                                                \
            assert(size == 4);                                                                   \
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

#define RT_XREG_GET(index)                                                    \
    case RegisterInfo::Register::xmm##index:                                  \
        assert(size == 16);                                                   \
        reg_ptr = reinterpret_cast<const uint8_t*>(&_active_ctx->Xmm##index); \
        break
                    RT_XREG_GET(0);
                    RT_XREG_GET(1);
                    RT_XREG_GET(2);
                    RT_XREG_GET(3);
                    RT_XREG_GET(4);
                    RT_XREG_GET(5);
                    RT_XREG_GET(6);
                    RT_XREG_GET(7);
                    RT_XREG_GET(8);
                    RT_XREG_GET(9);
                    RT_XREG_GET(10);
                    RT_XREG_GET(11);
                    RT_XREG_GET(12);
                    RT_XREG_GET(13);
                    RT_XREG_GET(14);
                    RT_XREG_GET(15);
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
        }
    }  // namespace runtime
}  // namespace inasm64
