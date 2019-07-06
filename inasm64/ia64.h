#pragma once

namespace inasm64
{
    ///<summary>
    ///contains basic information about a register
    ///</summary?
    struct RegisterInfo
    {
        //NOTE: deliberately not following the convention of using a 'k' prefix for enums, to be able to keep register "natural" names
        //WARNING: the order of these is important, GetRegisterInfo depends on it for lookups
        enum class Register
        {
            al,
            ah,
            bl,
            bh,
            cl,
            ch,
            dl,
            dh,
            sil,
            dil,
            spl,
            bpl,
            r8b,
            r9b,
            r10b,
            r11b,
            r12b,
            r13b,
            r14b,
            r15b,
            ax,
            bx,
            cx,
            dx,
            si,
            di,
            sp,
            bp,
            r8w,
            r9w,
            r10w,
            r11w,
            r12w,
            r13w,
            r14w,
            r15w,
            eax,
            ebx,
            ecx,
            edx,
            esi,
            edi,
            esp,
            ebp,
            r8d,
            r9d,
            r10d,
            r11d,
            r12d,
            r13d,
            r14d,
            r15d,
            rax,
            rbx,
            rcx,
            rdx,
            rsi,
            rdi,
            rsp,
            rbp,
            r8,
            r9,
            r10,
            r11,
            r12,
            r13,
            r14,
            r15,
            mm0,
            mm1,
            mm2,
            mm3,
            mm4,
            mm5,
            mm6,
            mm7,
            xmm0,
            xmm1,
            xmm2,
            xmm3,
            xmm4,
            xmm5,
            xmm6,
            xmm7,
            xmm8,
            xmm9,
            xmm10,
            xmm11,
            xmm12,
            xmm13,
            xmm14,
            xmm15,
            ymm0,
            ymm1,
            ymm2,
            ymm3,
            ymm4,
            ymm5,
            ymm6,
            ymm7,
            ymm8,
            ymm9,
            ymm10,
            ymm11,
            ymm12,
            ymm13,
            ymm14,
            ymm15,
            zmm0,
            zmm1,
            zmm2,
            zmm3,
            zmm4,
            zmm5,
            zmm6,
            zmm7,
            zmm8,
            zmm9,
            zmm10,
            zmm11,
            zmm12,
            zmm13,
            zmm14,
            zmm15,
            cs,
            ds,
            es,
            ss,
            fs,
            gs,
            eflags,
            kInvalid,
        };
        enum class RegClass
        {
            kGpr,
            kMmx,
            kXmm,
            kYmm,
            kZmm,
            kSegment,
            kInvalid,
        };
        RegClass _class = RegClass::kInvalid;
        Register _register = Register::kInvalid;
        short _bit_width = 0;
        RegisterInfo() = default;
        RegisterInfo(RegClass klass, Register register_, short width)
            : _class{ klass }
            , _register{ register_ }
            , _bit_width{ width }
        {
        }
        operator bool() const
        {
            return _bit_width && _class != RegClass::kInvalid;
        }
    };

    constexpr RegisterInfo kInvalidRegister = {};
    ///<summary>
    /// given an input *lowercase* register name, return an information structure or kInvalidRegister
    ///</summary>
    RegisterInfo GetRegisterInfo(const char* regName);

    enum class SseLevel
    {
        kSse = 0,
        kSse2,
        kSse3,
        kSsse3,
        kSse4_1,
        kSse4_2,
        kSse4a,
        kSse5,
    };

    bool SseLevelSupported(SseLevel level);
    bool AvxSupported();
}  // namespace inasm64
