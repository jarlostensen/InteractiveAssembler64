#pragma once

namespace inasm64
{
    //NOTE: the most XED will do
    constexpr size_t kMaxAssembledInstructionSize = 15;

    enum class Error
    {
        NoError,
        EmptyInput,
        NoMoreCode,
        CodeBufferFull,
        InvalidAddress,
        SystemError,
    };

    Error GetError();

    namespace detail
    {
        void SetError(Error error);
    }

}  // namespace inasm64
