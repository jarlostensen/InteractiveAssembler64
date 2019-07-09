
#include <string>
#include "ia64.h"

#include <intrin.h>

namespace inasm64
{
    namespace detail
    {
        const char* kGpr8[] = {
            "al", "ah", "bl", "bh", "cl", "ch", "dl", "dh", "sil", "dil", "spl", "bpl", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"
        };
        const char* kGpr16[] = {
            "ax", "bx", "cx", "dx", "si", "di", "sp", "bp", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
        };
        const char* kGpr32[] = {
            "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
        };
        const char* kGpr64[] = {
            "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
        };
        const char* kSegmentRegisters[] = {
            "cs", "ds", "es", "ss", "fs", "gs"
        };

        // all of this checking is based on https://gist.github.com/hi2p-perim/7855506

        struct sys_flags_t
        {
            bool _checked : 1;
            bool _sse : 1;
            bool _sse2 : 1;
            bool _sse3 : 1;
            bool _ssse3 : 1;
            bool _sse4_1 : 1;
            bool _sse4_2 : 1;
            bool _sse4a : 1;
            bool _sse5 : 1;
            bool _avx : 1;

            // https://software.intel.com/en-us/blogs/2011/04/14/is-avx-enabled/
            bool _os_xsave_xstor : 1;
        } _sys_flags = { 0 };

        void cpuid(int eax, int ecx, int* regs)
        {
            __cpuidex(regs, eax, ecx);
        }

        void check_system()
        {
            if(_sys_flags._checked)
                return;

            int regs[4] = { 0 };
            cpuid(1, 0, regs);

            _sys_flags._sse = (regs[3] & (1 << 25)) != 0;
            _sys_flags._sse2 = (regs[3] & (1 << 26)) != 0;
            _sys_flags._sse3 = (regs[2] & 1) != 0;
            _sys_flags._ssse3 = (regs[2] & (1 << 9)) != 0;
            _sys_flags._sse4_1 = (regs[2] & (1 << 19)) != 0;
            _sys_flags._sse4_2 = (regs[2] & (1 << 20)) != 0;
            _sys_flags._avx = (regs[2] & (1 << 28)) != 0;
            _sys_flags._os_xsave_xstor = (regs[2] & (1 << 27)) != 0;

            if(_sys_flags._os_xsave_xstor && _sys_flags._avx)
            {
                // check xcr0 register for xmm and/or ymm enabled
                const auto xcr0 = uint32_t(_xgetbv(0));
                // if the OS doesn't support XSAVE/XSTOR we need to disable this
                _sys_flags._avx = (xcr0 & 6) == 6;
            }

            // check for SSE4a and SSE5
            cpuid(0x80000000, 0, regs);
            const auto num_extended_ids = regs[0];
            if(num_extended_ids >= 0x80000001)
            {
                cpuid(0x80000001, 0, regs);
                _sys_flags._sse4a = (regs[2] & (1 << 6)) != 0;
                _sys_flags._sse5 = (regs[2] & (1 << 11)) != 0;
            }

            _sys_flags._checked = true;
        }
    }  // namespace detail
    using namespace detail;

    RegisterInfo::RegisterInfo(RegClass klass, Register register_, short width)
        : _class{ klass }
        , _register{ register_ }
        , _greatest_enclosing_register{ register_ }
        , _bit_width{ width }
    {
        if(register_ != Register::kInvalid)
        {
            const auto ord = static_cast<int>(_register);
            if(ord >= static_cast<int>(Register::zmm0))
            {
                _greatest_enclosing_register = _register;
            }
            else if(ord >= static_cast<int>(Register::ymm0))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::ymm0) + static_cast<int>(Register::zmm0));
            }
            else if(ord >= static_cast<int>(Register::xmm0))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::xmm0) + static_cast<int>(Register::zmm0));
            }
            else if(ord >= static_cast<int>(Register::rax))
            {
                _greatest_enclosing_register = _register;
            }
            else if(ord >= static_cast<int>(Register::r8d))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::r8d) + static_cast<int>(Register::r8));
            }
            else if(ord >= static_cast<int>(Register::eax))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::eax) + static_cast<int>(Register::rax));
            }
            else if(ord >= static_cast<int>(Register::r8w))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::r8w) + static_cast<int>(Register::r8));
            }
            else if(ord >= static_cast<int>(Register::ax))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::ax) + static_cast<int>(Register::rax));
            }
            else if(ord >= static_cast<int>(Register::r8b))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::r8b) + static_cast<int>(Register::r8));
            }
            else
            {
                switch(_register)
                {
                case Register::al:
                case Register::ah:
                    _greatest_enclosing_register = Register::rax;
                    break;
                case Register::bl:
                case Register::bh:
                    _greatest_enclosing_register = Register::rbx;
                    break;
                case Register::cl:
                case Register::ch:
                    _greatest_enclosing_register = Register::rcx;
                    break;
                case Register::dl:
                case Register::dh:
                    _greatest_enclosing_register = Register::rdx;
                    break;
                default:
                {
                    // byte special registers (sil, dil, etc)
                    _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::sil) + static_cast<int>(Register::rsi));
                }
                break;
                }
            }
        }
    }

    RegisterInfo GetRegisterInfo(const char* reg)
    {
        if(reg[1] == 'm')
        {
            const auto reg_len = strlen(reg);
            if(reg_len > 3 && reg[2] == 'm')
            {
                // XMM, YMM, and ZMM. Depending on mode there are different numbers of these, but the width is the same across all
                // https://en.wikipedia.org/wiki/AVX-512#Encoding_and_features

                const auto id = strtol(reg + 3, nullptr, 10);
                if(errno && (id < 0 || id > 15))
                    return kInvalidRegister;

                if(reg[0] == 'x')
                    return { RegisterInfo::RegClass::kXmm, static_cast<RegisterInfo::Register>(id + static_cast<int>(RegisterInfo::Register::xmm0)), 128 };
                if(reg[0] == 'y')
                    return { RegisterInfo::RegClass::kYmm, static_cast<RegisterInfo::Register>(id + static_cast<int>(RegisterInfo::Register::ymm0)), 256 };
                if(reg[0] == 'z')
                    return { RegisterInfo::RegClass::kZmm, static_cast<RegisterInfo::Register>(id + static_cast<int>(RegisterInfo::Register::zmm0)), 512 };
            }
            // otherwise it's invalid
            return kInvalidRegister;
        }

        if(reg[0] == 'r')
        {
            for(auto r = 0; r < std::size(kGpr64); ++r)
            {
                if(strncmp(reg, kGpr64[r], strlen(kGpr64[r])) == 0)
                {
                    return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::rax)), 64 };
                }
            }
        }

        for(auto r = 0; r < std::size(kGpr32); ++r)
        {
            if(strncmp(reg, kGpr32[r], strlen(kGpr32[r])) == 0)
            {
                return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::eax)), 32 };
            }
        }

        for(auto r = 0; r < std::size(kGpr16); ++r)
        {
            if(strncmp(reg, kGpr16[r], strlen(kGpr16[r])) == 0)
            {
                return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::ax)), 16 };
            }
        }
        for(auto r = 0; r < std::size(kGpr8); ++r)
        {
            if(strncmp(reg, kGpr8[r], strlen(kGpr8[r])) == 0)
            {
                return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::al)), 8 };
            }
        }
        for(auto r = 0; r < std::size(kSegmentRegisters); ++r)
        {
            if(strncmp(reg, kSegmentRegisters[r], strlen(kSegmentRegisters[r])) == 0)
            {
                return { RegisterInfo::RegClass::kSegment, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::cs)), 16 };
            }
        }
        if(strncmp(reg, "eflags", 6) == 0)
            //TODO: there are more flags, different sizes, etc.
            return { RegisterInfo::RegClass::kFlags, RegisterInfo::Register::eflags, 32 };

        return kInvalidRegister;
    }

    bool SseLevelSupported(SseLevel level)
    {
        check_system();
        switch(level)
        {
        case SseLevel::kSse:
            return _sys_flags._sse;
        case SseLevel::kSse2:
            return _sys_flags._sse2;
        case SseLevel::kSse3:
            return _sys_flags._sse3;
        case SseLevel::kSsse3:
            return _sys_flags._ssse3;
        case SseLevel::kSse4_1:
            return _sys_flags._sse4_1;
        case SseLevel::kSse4_2:
            return _sys_flags._sse4_2;
        case SseLevel::kSse4a:
            return _sys_flags._sse4a;
        case SseLevel::kSse5:
            return _sys_flags._sse5;
        }
        return false;
    }

    bool AvxSupported()
    {
        check_system();
        return _sys_flags._avx;
    }
}  // namespace inasm64