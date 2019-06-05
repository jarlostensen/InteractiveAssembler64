
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <debugapi.h>

#include <memory>
#include "common.h"
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
                                    detail::SetError(Error::SystemError);
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
                detail::SetError(Error::SystemError);
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

        bool AddCode(const void* code, size_t size)
        {
            if(!_flags._started)
                return false;

            if(_scratch_size - size_t(_scratch_wp - _scratch_memory) < size)
            {
                detail::SetError(Error::CodeBufferFull);
                return false;
            }

            SIZE_T written;
            if(WriteProcessMemory(_process_vm, _scratch_wp, code, SIZE_T(size), &written) == TRUE && size_t(written) == size)
            {
                FlushInstructionCache(_process_vm, _scratch_wp, SIZE_T(size));
                _scratch_wp += size;
                return true;
            }
            detail::SetError(Error::SystemError);
            return false;
        }

        bool Step()
        {
            // not started, or no code loaded
            if(!_flags._started || _scratch_wp == _scratch_memory)
            {
                detail::SetError(Error::NoMoreCode);
                return false;
            }

            // no more code to execute
            if(_code == _scratch_wp)
            {
                detail::SetError(Error::NoMoreCode);
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
                    // this is the only one we care about
                    case EXCEPTION_SINGLE_STEP:
                    {
                        const auto thread = ActiveThread();
                        if(!thread)
                        {
                            detail::SetError(Error::SystemError);
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
                    default:
                        // ignore all else....
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
                detail::SetError(Error::InvalidAddress);
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

            detail::SetError(Error::SystemError);
            return false;
        }

        void SetReg(ByteReg reg, int8_t value)
        {
            if(!_flags._started)
                return;

#define _SETBYTEREG_LO(r)               \
    _active_ctx->##r &= ~DWORD64(0xff); \
    _active_ctx->##r |= value

#define _SETBYTEREG_HI(r)                 \
    _active_ctx->##r &= ~DWORD64(0xff00); \
    _active_ctx->##r |= (DWORD64(value) << 8)
            switch(reg)
            {
            case ByteReg::AL:
                _SETBYTEREG_LO(Rax);
                break;
            case ByteReg::AH:
                _SETBYTEREG_HI(Rax);
                break;
            case ByteReg::BL:
                _SETBYTEREG_LO(Rbx);
                break;
            case ByteReg::BH:
                _SETBYTEREG_HI(Rbx);
                break;
            case ByteReg::CL:
                _SETBYTEREG_LO(Rcx);
                break;
            case ByteReg::CH:
                _SETBYTEREG_HI(Rcx);
                break;
            case ByteReg::DL:
                _SETBYTEREG_LO(Rdx);
                break;
            case ByteReg::DH:
                _SETBYTEREG_HI(Rdx);
                break;
            }
            _ctx_changed = true;
        }

        int8_t GetReg(ByteReg reg)
        {
            int8_t value = 0;
#define _GETBYTEREG_LO(r) \
    value = int8_t(_active_ctx->##r & DWORD64(0xff))
#define _GETBYTEREG_HI(r) \
    value = int8_t((_active_ctx->##r & DWORD64(0xff00)) >> 8)

            switch(reg)
            {
            case ByteReg::AL:
                _GETBYTEREG_LO(Rax);
                break;
            case ByteReg::AH:
                _GETBYTEREG_HI(Rax);
                break;
            case ByteReg::BL:
                _GETBYTEREG_LO(Rbx);
                break;
            case ByteReg::BH:
                _GETBYTEREG_HI(Rbx);
                break;
            case ByteReg::CL:
                _GETBYTEREG_LO(Rcx);
                break;
            case ByteReg::CH:
                _GETBYTEREG_HI(Rcx);
                break;
            case ByteReg::DL:
                _GETBYTEREG_LO(Rdx);
                break;
            case ByteReg::DH:
                _GETBYTEREG_HI(Rdx);
                break;
            }
            return value;
        }

        void SetReg(WordReg reg, int16_t value)
        {
            if(!_flags._started)
                return;

#define _SETWORDREG(r)                    \
    _active_ctx->##r &= ~DWORD64(0xffff); \
    _active_ctx->##r |= DWORD64(value)

            switch(reg)
            {
            case WordReg::AX:
                _SETWORDREG(Rax);
                break;
            case WordReg::BX:
                _SETWORDREG(Rbx);
                break;
            case WordReg::CX:
                _SETWORDREG(Rcx);
                break;
            case WordReg::DX:
                _SETWORDREG(Rdx);
                break;
            case WordReg::BP:
                _SETWORDREG(Rbp);
                break;
            case WordReg::SP:
                _SETWORDREG(Rsp);
                break;
            case WordReg::SI:
                _SETWORDREG(Rsi);
                break;
            case WordReg::DI:
                _SETWORDREG(Rdi);
                break;
            }
            _ctx_changed = true;
        }

        int16_t GetReg(WordReg reg)
        {
            int16_t value = 0;
#define _GETWORDREG(r) \
    value = int16_t(_active_ctx->##r & DWORD64(0xffff))

            switch(reg)
            {
            case WordReg::AX:
                _GETWORDREG(Rax);
                break;
            case WordReg::BX:
                _GETWORDREG(Rbx);
                break;
            case WordReg::CX:
                _GETWORDREG(Rcx);
                break;
            case WordReg::DX:
                _GETWORDREG(Rdx);
                break;
            case WordReg::BP:
                _GETWORDREG(Rbp);
                break;
            case WordReg::SP:
                _GETWORDREG(Rsp);
                break;
            case WordReg::SI:
                _GETWORDREG(Rsi);
                break;
            case WordReg::DI:
                _GETWORDREG(Rdi);
                break;
            }
            return value;
        }

        void SetReg(DWordReg reg, int32_t value)
        {
            if(!_flags._started)
                return;

#define _SETDWORDREG(r)                       \
    _active_ctx->##r &= ~DWORD64(0xffffffff); \
    _active_ctx->##r |= DWORD64(value)

            switch(reg)
            {
            case DWordReg::EAX:
                _SETDWORDREG(Rax);
                break;
            case DWordReg::EBX:
                _SETDWORDREG(Rbx);
                break;
            case DWordReg::ECX:
                _SETDWORDREG(Rcx);
                break;
            case DWordReg::EDX:
                _SETDWORDREG(Rdx);
                break;
            case DWordReg::EBP:
                _SETDWORDREG(Rbp);
                break;
            case DWordReg::ESP:
                _SETDWORDREG(Rsp);
                break;
            case DWordReg::ESI:
                _SETDWORDREG(Rsi);
                break;
            case DWordReg::EDI:
                _SETDWORDREG(Rdi);
                break;
            }
            _ctx_changed = true;
        }

        int32_t GetReg(DWordReg reg)
        {
            int32_t value = 0;
#define _GETDWORDREG(r) \
    value = int32_t(_active_ctx->##r & DWORD64(0xffffffff))

            switch(reg)
            {
            case DWordReg::EAX:
                _GETDWORDREG(Rax);
                break;
            case DWordReg::EBX:
                _GETDWORDREG(Rbx);
                break;
            case DWordReg::ECX:
                _GETDWORDREG(Rcx);
                break;
            case DWordReg::EDX:
                _GETDWORDREG(Rdx);
                break;
            case DWordReg::EBP:
                _GETDWORDREG(Rbp);
                break;
            case DWordReg::ESP:
                _GETDWORDREG(Rsp);
                break;
            case DWordReg::ESI:
                _GETDWORDREG(Rsi);
                break;
            case DWordReg::EDI:
                _GETDWORDREG(Rdi);
                break;
            }
            return value;
        }

        void SetReg(QWordReg reg, int64_t value)
        {
            if(!_flags._started)
                return;

            switch(reg)
            {
            case QWordReg::RAX:
                _active_ctx->Rax = value;
                break;
            case QWordReg::RBX:
                _active_ctx->Rbx = value;
                break;
            case QWordReg::RCX:
                _active_ctx->Rcx = value;
                break;
            case QWordReg::RDX:
                _active_ctx->Rdx = value;
                break;
            case QWordReg::RBP:
                _active_ctx->Rbp = value;
                break;
            case QWordReg::RSP:
                _active_ctx->Rsp = value;
                break;
            case QWordReg::RSI:
                _active_ctx->Rsi = value;
                break;
            case QWordReg::RDI:
                _active_ctx->Rdi = value;
                break;
#define _SET_R_REG(n)              \
    case QWordReg::R##n:           \
        _active_ctx->R##n = value; \
        break
                _SET_R_REG(8);
                _SET_R_REG(9);
                _SET_R_REG(10);
                _SET_R_REG(11);
                _SET_R_REG(12);
                _SET_R_REG(13);
                _SET_R_REG(14);
                _SET_R_REG(15);
            }

            _ctx_changed = true;
        }

        int64_t GetReg(QWordReg reg)
        {
            switch(reg)
            {
            case QWordReg::RAX:
                return _active_ctx->Rax;
            case QWordReg::RBX:
                return _active_ctx->Rbx;
            case QWordReg::RCX:
                return _active_ctx->Rcx;
            case QWordReg::RDX:
                return _active_ctx->Rdx;
            case QWordReg::RBP:
                return _active_ctx->Rbp;
            case QWordReg::RSP:
                return _active_ctx->Rsp;
            case QWordReg::RSI:
                return _active_ctx->Rsi;
            case QWordReg::RDI:
                return _active_ctx->Rdi;
#define _GET_R_REG(n)    \
    case QWordReg::R##n: \
        return _active_ctx->R##n
                _GET_R_REG(8);
                _GET_R_REG(9);
                _GET_R_REG(10);
                _GET_R_REG(11);
                _GET_R_REG(12);
                _GET_R_REG(13);
                _GET_R_REG(14);
                _GET_R_REG(15);
            }
            return 0;
        }

        bool SetReg(const char* regName, int64_t value)
        {
            //TODO: XMM and YMM registers, FP registers

            const auto len = strlen(regName);
            if(len > 4)
            {
                detail::SetError(Error::InvalidCommandFormat);
                return false;
            }
            auto result = true;
            auto firstToken = regName[0];
            if(len == 2 && firstToken != 'R')
            {
                const auto secondToken = regName[1];
                // should be a byte- or word -register
                switch(firstToken)
                {
                    // sadly this magic (https://stackoverflow.com/questions/35525555/c-preprocessor-how-to-create-a-character-literal) does not work with MS141
                    // complains about newline in constant at SINGLEQUOTE
                    //#define CONCAT_H(x, y, z) x##y##z
                    //#define SINGLEQUOTE '
                    //#define CONCAT(x, y, z) CONCAT_H(x, y, z)
                    //#define CHARIFY(x) CONCAT(SINGLEQUOTE, x, SINGLEQUOTE)

#define _SETNAMEDBYTEREG_HL(prefix)                                              \
    if(secondToken == 'L')                                                       \
    {                                                                            \
        runtime::SetReg(runtime::ByteReg::##prefix##L, int8_t(value & 0xff));    \
    }                                                                            \
    else if(secondToken == 'H')                                                  \
    {                                                                            \
        runtime::SetReg(runtime::ByteReg::##prefix##H, int8_t(value & 0xff));    \
    }                                                                            \
    else if(secondToken == 'X')                                                  \
    {                                                                            \
        runtime::SetReg(runtime::WordReg::##prefix##X, int16_t(value & 0xffff)); \
    }                                                                            \
    else                                                                         \
        result = false;                                                          \
    break
                case 'A':
                    _SETNAMEDBYTEREG_HL(A);
                case 'B':
                    _SETNAMEDBYTEREG_HL(B);
                case 'C':
                    _SETNAMEDBYTEREG_HL(C);
                case 'D':
                    _SETNAMEDBYTEREG_HL(D);
                default:
                    if(secondToken == 'I')
                    {
                        if(firstToken == 'D')
                        {
                            runtime::SetReg(runtime::WordReg::DI, int16_t(value & 0xffff));
                        }
                        else if(firstToken == 'S')
                        {
                            runtime::SetReg(runtime::WordReg::SI, int16_t(value & 0xffff));
                        }
                        else
                            result = false;
                    }
                    else if(secondToken == 'P')
                    {
                        if(firstToken == 'B')
                        {
                            runtime::SetReg(runtime::WordReg::BP, int16_t(value & 0xffff));
                        }
                        else if(firstToken == 'S')
                        {
                            runtime::SetReg(runtime::WordReg::SP, int16_t(value & 0xffff));
                        }
                        else
                            result = false;
                    }
                    else
                        result = false;
                    break;
                }
            }
            else if(len == 3 || firstToken == 'R')
            {
                // 32- or 64 -bit registers
                const auto secondToken = regName[2];
                if(firstToken == 'E')
                {
                    firstToken = regName[1];
                    switch(firstToken)
                    {
#define _SETNAMEDDWORDREG(prefix)                                                      \
    if(secondToken == 'X')                                                             \
    {                                                                                  \
        runtime::SetReg(runtime::DWordReg::E##prefix##X, int32_t(value & 0xffffffff)); \
    }                                                                                  \
    else                                                                               \
        result = false;                                                                \
    break
                    case 'A':
                        _SETNAMEDDWORDREG(A);
                    case 'B':
                        _SETNAMEDDWORDREG(B);
                    case 'C':
                        _SETNAMEDDWORDREG(C);
                    case 'D':
                        _SETNAMEDDWORDREG(D);
                    default:
                        if(secondToken == 'I')
                        {
                            if(firstToken == 'D')
                            {
                                runtime::SetReg(runtime::DWordReg::EDI, int32_t(value & 0xffffffff));
                            }
                            else if(firstToken == 'S')
                            {
                                runtime::SetReg(runtime::DWordReg::ESI, int32_t(value & 0xffffffff));
                            }
                            else
                                result = false;
                        }
                        else if(secondToken == 'P')
                        {
                            if(firstToken == 'B')
                            {
                                runtime::SetReg(runtime::DWordReg::EBP, int16_t(value & 0xffffffff));
                            }
                            else if(firstToken == 'S')
                            {
                                runtime::SetReg(runtime::DWordReg::ESP, int16_t(value & 0xffffffff));
                            }
                            else
                                result = false;
                        }
                        else
                            result = false;
                        break;
                    }
                }
                else if(firstToken == 'R')
                {
                    firstToken = regName[1];
                    switch(firstToken)
                    {
#define _SETNAMEDQWORDREG(prefix)                                \
    if(secondToken == 'X')                                       \
    {                                                            \
        runtime::SetReg(runtime::QWordReg::R##prefix##X, value); \
    }                                                            \
    else                                                         \
        result = false;                                          \
    break
                    case 'A':
                        _SETNAMEDQWORDREG(A);
                    case 'B':
                        _SETNAMEDQWORDREG(B);
                    case 'C':
                        _SETNAMEDQWORDREG(C);
                    case 'D':
                        _SETNAMEDQWORDREG(D);
                    default:
                        if(secondToken == 'I')
                        {
                            if(firstToken == 'D')
                            {
                                runtime::SetReg(runtime::QWordReg::RDI, value);
                            }
                            else if(firstToken == 'S')
                            {
                                runtime::SetReg(runtime::QWordReg::RSI, value);
                            }
                            else
                                result = false;
                        }
                        else if(secondToken == 'P')
                        {
                            if(firstToken == 'B')
                            {
                                runtime::SetReg(runtime::QWordReg::RBP, value);
                            }
                            else if(firstToken == 'S')
                            {
                                runtime::SetReg(runtime::QWordReg::RSP, value);
                            }
                            else
                                result = false;
                        }
                        else
                        {
                            // R8-R15?
                            const auto ordinal = ::atol(regName + 1);
                            switch(ordinal)
                            {
#define _SETORDQWORDREG(ord)                               \
    case ord:                                              \
        runtime::SetReg(runtime::QWordReg::R##ord, value); \
        break

                                _SETORDQWORDREG(8);
                                _SETORDQWORDREG(9);
                                _SETORDQWORDREG(10);
                                _SETORDQWORDREG(11);
                                _SETORDQWORDREG(12);
                                _SETORDQWORDREG(13);
                                _SETORDQWORDREG(14);
                                _SETORDQWORDREG(15);
                            default:
                                result = false;
                                break;
                            }
                        }
                        break;
                    }
                }
                else
                    result = false;
            }

            if(!result)
                detail::SetError(Error::UnrecognizedRegisterName);

            return result;
        }
    }  // namespace runtime
}  // namespace inasm64
