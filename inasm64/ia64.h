#pragma once

namespace inasm64
{
    ///<summary>
    ///contains basic information about a register
    ///</summary?
    struct RegisterInfo
    {
        enum class RegClass
        {
            //TODO: Segment, flags, etc....
            kGpr,
            kMmx,
            kXmm,
            kYmm,
            kZmm,
            kInvalid,
        };
        RegClass _class = RegClass::kInvalid;
        short _bit_width = 0;
        RegisterInfo() = default;
        RegisterInfo(RegClass klass, short width)
            : _class{ klass }
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
