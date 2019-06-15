
#include <string>
#include "ia64.h"

namespace inasm64
{
    namespace
    {
        const char* kGpr8[] = {
            "al", "ah", "bl", "bh", "cl", "ch", "dl", "dh", "sil", "dil", "spl", "bpl", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"
        };
        const char* kGpr16[] = {
            "ax", "bx", "dx", "si", "di", "sp", "bp", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
        };
        const char* kGpr32[] = {
            "eax", "ebx", "edx", "esi", "edi", "esp", "ebp", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
        };
        const char* kGpr64[] = {
            "rax", "rbx", "rdx", "rsi", "rdi", "rsp", "rbp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
        };

    }  // namespace

    RegisterInfo GetRegisterInfo(const char* reg)
    {
        if(reg[1] == 'm')
        {
            const auto reg_len = strlen(reg);
            if(reg_len > 3 && reg[2] == 'm')
            {
                // XMM, YMM, and ZMM. Depending on mode there are different numbers of these, but the width is the same across all
                // https://en.wikipedia.org/wiki/AVX-512#Encoding_and_features

                if(reg[0] == 'x')
                    return { RegisterInfo::RegisterClass::kXmm, 128 };
                if(reg[0] == 'y')
                    return { RegisterInfo::RegisterClass::kYmm, 256 };
                if(reg[0] == 'z')
                    return { RegisterInfo::RegisterClass::kZmm, 512 };
            }
            if(reg[0] == 'm')
            {
                // mmx registers (0-7)
                // http://softpixel.com/~cwright/programming/simd/mmx.php
                return { RegisterInfo::RegisterClass::kMmx, 64 };
            }
            // otherwise it's invalid
            return kInvalidRegister;
        }

        if(reg[0] == 'r')
        {
            for(auto r = 0; r < std::size(kGpr64); ++r)
            {
                if(strcmp(reg, kGpr64[r]) == 0)
                {
                    return { RegisterInfo::RegisterClass::kGpr, 64 };
                }
            }
        }

        for(auto r = 0; r < std::size(kGpr32); ++r)
        {
            if(strcmp(reg, kGpr32[r]) == 0)
            {
                return { RegisterInfo::RegisterClass::kGpr, 32 };
            }
        }

        for(auto r = 0; r < std::size(kGpr16); ++r)
        {
            if(strcmp(reg, kGpr16[r]) == 0)
            {
                return { RegisterInfo::RegisterClass::kGpr, 16 };
            }
        }
        for(auto r = 0; r < std::size(kGpr8); ++r)
        {
            if(strcmp(reg, kGpr8[r]) == 0)
            {
                return { RegisterInfo::RegisterClass::kGpr, 8 };
            }
        }
        return kInvalidRegister;
    }
}  // namespace inasm64