// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include <string>
#include "ia64.h"

#include <intrin.h>

namespace inasm64
{
    namespace detail
    {
        struct register_lut_entry
        {
            const char* _name;
            size_t _len;
        };

        register_lut_entry kGpr8[] = {
            { "al", 2 },
            { "ah", 2 },
            { "bl", 2 },
            { "bh", 2 },
            { "cl", 2 },
            { "ch", 2 },
            { "dl", 2 },
            { "dh", 2 },
            { "sil", 3 },
            { "dil", 3 },
            { "spl", 3 },
            { "bpl", 3 },
            { "r8b", 3 },
            { "r9b", 3 },
            { "r10b", 4 },
            { "r11b", 4 },
            { "r12b", 4 },
            { "r13b", 4 },
            { "r14b", 4 },
            { "r15b", 4 }
        };
        register_lut_entry kGpr16[] = {
            { "ax", 2 }, { "bx", 2 }, { "cx", 2 }, { "dx", 2 }, { "si", 2 }, { "di", 2 }, { "sp", 2 }, { "bp", 2 }, { "r8w", 3 }, { "r9w", 3 }, { "r10w", 4 }, { "r11w", 4 }, { "r12w", 4 }, { "r13w", 4 }, { "r14w", 4 }, { "r15w", 4 }
        };
        register_lut_entry kGpr32[] = {
            { "eax", 3 }, { "ebx", 3 }, { "ecx", 3 }, { "edx", 3 }, { "esi", 3 }, { "edi", 3 }, { "esp", 3 }, { "ebp", 3 }, { "r8d", 3 }, { "r9d", 3 }, { "r10d", 4 }, { "r11d", 4 }, { "r12d", 4 }, { "r13d", 4 }, { "r14d", 4 }, { "r15d", 4 }
        };
        register_lut_entry kGpr64[] = {
            { "rax", 3 }, { "rbx", 3 }, { "rcx", 3 }, { "rdx", 3 }, { "rsi", 3 }, { "rdi", 3 }, { "rsp", 3 }, { "rbp", 3 }, { "r8", 2 }, { "r9", 2 }, { "r10", 3 }, { "r11", 3 }, { "r12", 3 }, { "r13", 3 }, { "r14", 3 }, { "r15", 3 }
        };
        const char* kXmmRegisters[] = {
            "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
        };
        const char* kYmmRegisters[] = {
            "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7", "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15"
        };
        const char* kZmmRegisters[] = {
            "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15"
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

    RegisterInfo::RegisterInfo(RegClass klass, Register register_, short width, const char* name)
        : _class{ klass }
        , _register{ register_ }
        , _greatest_enclosing_register{ register_ }
        , _bit_width{ width }
        , _name{ name }
    {
        if(register_ != Register::kInvalid)
        {
            const auto ord = static_cast<int>(_register);
            if(ord >= static_cast<int>(Register::zmm0))
            {
                _greatest_enclosing_register = _register;
                _name = kZmmRegisters[ord - static_cast<int>(Register::zmm0)];
            }
            else if(ord >= static_cast<int>(Register::ymm0))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::ymm0) + static_cast<int>(Register::zmm0));
                _name = kYmmRegisters[ord - static_cast<int>(Register::ymm0)];
            }
            else if(ord >= static_cast<int>(Register::xmm0))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::xmm0) + static_cast<int>(Register::zmm0));
                _name = kXmmRegisters[ord - static_cast<int>(Register::xmm0)];
            }
            else if(ord >= static_cast<int>(Register::rax))
            {
                _greatest_enclosing_register = _register;
                _name = kGpr64[ord - static_cast<int>(Register::rax)]._name;
            }
            else if(ord >= static_cast<int>(Register::r8d))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::r8d) + static_cast<int>(Register::r8));
                _name = kGpr32[ord - static_cast<int>(Register::eax)]._name;
            }
            else if(ord >= static_cast<int>(Register::eax))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::eax) + static_cast<int>(Register::rax));
                _name = kGpr32[ord - static_cast<int>(Register::eax)]._name;
            }
            else if(ord >= static_cast<int>(Register::r8w))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::r8w) + static_cast<int>(Register::r8));
                _name = kGpr16[ord - static_cast<int>(Register::ax)]._name;
            }
            else if(ord >= static_cast<int>(Register::ax))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::ax) + static_cast<int>(Register::rax));
                _name = kGpr16[ord - static_cast<int>(Register::ax)]._name;
            }
            else if(ord >= static_cast<int>(Register::r8b))
            {
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<int>(Register::r8b) + static_cast<int>(Register::r8));
                _name = kGpr8[ord - static_cast<int>(Register::al)]._name;
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
                _name = kGpr8[ord - static_cast<int>(Register::al)]._name;
            }
        }
    }

    RegisterInfo::RegisterInfo(Register register_)
        : _register{ register_ }
    {
        if(register_ == Register::eflags)
        {
            _class = RegClass::kFlags;
            _greatest_enclosing_register = register_;
            _name = "eflags";
            _bit_width = 32;
        }
        else
        {
            const auto ord = static_cast<size_t>(register_);
            if(ord >= static_cast<size_t>(Register::cs))
            {
                _class = RegClass::kSegment;
                _greatest_enclosing_register = register_;
                _name = kSegmentRegisters[ord - static_cast<size_t>(Register::cs)];
                _bit_width = 16;
            }
            else if(ord >= static_cast<size_t>(Register::zmm0))
            {
                _class = RegClass::kZmm;
                _greatest_enclosing_register = register_;
                _name = kZmmRegisters[ord - static_cast<size_t>(Register::zmm0)];
                _bit_width = 512;
            }
            else if(ord >= static_cast<size_t>(Register::ymm0))
            {
                _class = RegClass::kYmm;
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<size_t>(Register::ymm0) + static_cast<size_t>(Register::zmm0));
                _name = kYmmRegisters[ord - static_cast<size_t>(Register::ymm0)];
                _bit_width = 256;
            }
            else if(ord >= static_cast<size_t>(Register::xmm0))
            {
                _class = RegClass::kXmm;
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<size_t>(Register::xmm0) + static_cast<size_t>(Register::zmm0));
                _name = kXmmRegisters[ord - static_cast<size_t>(Register::xmm0)];
                _bit_width = 128;
            }
            else if(ord >= static_cast<size_t>(Register::rax))
            {
                _class = RegClass::kGpr;
                _greatest_enclosing_register = _register;
                _name = kGpr64[ord - static_cast<size_t>(Register::rax)]._name;
                _bit_width = 64;
            }
            else if(ord >= static_cast<size_t>(Register::eax))
            {
                _class = RegClass::kGpr;
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<size_t>(Register::eax) + static_cast<size_t>(Register::rax));
                _name = kGpr32[ord - static_cast<size_t>(Register::eax)]._name;
                _bit_width = 32;
            }
            else if(ord >= static_cast<size_t>(Register::ax))
            {
                _class = RegClass::kGpr;
                _greatest_enclosing_register = static_cast<Register>(ord - static_cast<size_t>(Register::ax) + static_cast<size_t>(Register::rax));
                _name = kGpr16[ord - static_cast<size_t>(Register::ax)]._name;
                _bit_width = 16;
            }
            else
            {
                _class = RegClass::kGpr;
                _bit_width = 8;
                _name = kGpr8[ord]._name;

                switch(register_)
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
                case Register::sil:
                    _greatest_enclosing_register = Register::rsi;
                    break;
                case Register::dil:
                    _greatest_enclosing_register = Register::rdi;
                    break;
                case Register::spl:
                    _greatest_enclosing_register = Register::rsp;
                    break;
                case Register::bpl:
                    _greatest_enclosing_register = Register::rbp;
                    break;
                default:
                {
                    _greatest_enclosing_register = static_cast<Register>(ord - static_cast<size_t>(Register::r8b) + static_cast<size_t>(Register::r8));
                }
                break;
                }
            }
        }
    }

    RegisterInfo GetRegisterInfo(const char* reg)
    {
        //NOTE: we might get the name as part of a longer string, so calculate length as the first white-space separated word
        auto str = reg;
        while(str[0] && str[0] != ' ')
            ++str;
        const auto reg_len = size_t(str - reg);
        if(reg[1] == 'm')
        {
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
                if(strncmp(reg, kGpr64[r]._name, kGpr64[r]._len) == 0)
                {
                    return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::rax)), 64 };
                }
            }
        }

        for(auto r = 0; r < std::size(kGpr32); ++r)
        {
            if(strncmp(reg, kGpr32[r]._name, reg_len) == 0)
            {
                return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::eax)), 32 };
            }
        }

        for(auto r = 0; r < std::size(kGpr16); ++r)
        {
            if(strncmp(reg, kGpr16[r]._name, reg_len) == 0)
            {
                return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::ax)), 16 };
            }
        }
        for(auto r = 0; r < std::size(kGpr8); ++r)
        {
            if(strncmp(reg, kGpr8[r]._name, reg_len) == 0)
            {
                return { RegisterInfo::RegClass::kGpr, static_cast<RegisterInfo::Register>(r + static_cast<int>(RegisterInfo::Register::al)), 8 };
            }
        }
        for(auto r = 0; r < std::size(kSegmentRegisters); ++r)
        {
            if(strncmp(reg, kSegmentRegisters[r], reg_len) == 0)
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