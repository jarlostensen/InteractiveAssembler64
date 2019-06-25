#pragma once

namespace inasm64
{
    //NOTE: the most XED will do
    constexpr size_t kMaxAssembledInstructionSize = 15;
    // value of single argument passed back to running application when used as a debuggee
    constexpr int kTrapModeArgumentValue = 262;

    enum class Error
    {
        NoError,
        CliUninitialised,
        CliInputLengthExceeded,
        UndefinedVariable,
        UnsupportedInstructionFormat,
        EmptyInput,
        NoMoreCode,
        CodeBufferFull,
        InvalidCommandFormat,
        UnrecognizedRegisterName,
        InvalidAddress,
        InvalidInstructionFormat,
        InvalidOperandFormat,
        InvalidOperandScale,
        InvalidDestRegistername,
        InvalidInstructionName,
        OperandSizesMismatch,
        EncodeError,
        MemoryReadSizeMismatch,
        MemoryWriteSizeMismatch,
        SystemError,
    };

    Error GetError();
    const std::string ErrorMessage(Error error);

    namespace detail
    {
        void SetError(Error error);
        // converts valid 0x or h representations, as well as whatever stroll supports
        // does not modify value if str is not a valid number
        bool str_to_ll(const char* str, long long& value);
        // either 0x followed by at least one hex digit, or hex digits followed by a 'h'
        bool starts_with_hex_number(const char* str);
    }  // namespace detail

}  // namespace inasm64
