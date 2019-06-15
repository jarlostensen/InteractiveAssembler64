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
        SystemError,
    };

    Error GetError();
    const std::string ErrorMessage(Error error);

    namespace detail
    {
        void SetError(Error error);
    }

}  // namespace inasm64
