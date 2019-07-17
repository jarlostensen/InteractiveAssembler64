#pragma once

namespace inasm64
{
    //NOTE: the most XED will do
    constexpr size_t kMaxAssembledInstructionSize = 15;
    // value of single argument passed back to running application when used as a debuggee
    constexpr int kTrapModeArgumentValue = 262;

    enum class Error
    {
        kNoError,
        kCliUninitialised,
        kCliInputLengthExceeded,
        kRuntimeUninitialised,
        kUndefinedVariable,
        UnsupportedInstructionFormat,
        kEmptyInput,
        kNoMoreCode,
        kCodeBufferFull,
        kInvalidCommandFormat,
        kUnrecognizedRegisterName,
        kInvalidAddress,
        kInvalidInstructionFormat,
        kInvalidOperandFormat,
        kInvalidOperandScale,
        kCodeBufferOverflow,
        kInvalidDestRegistername,
        kInvalidInstructionName,
        kOperandSizesMismatch,
        kEncodeError,
        kMemoryReadSizeMismatch,
        kMemoryWriteSizeMismatch,
        kInvalidImmediateOperandBitWidth,
        kInvalidRegisterName,
        kCliUnknownCommand,
        kAccessViolation,
        kSystemError,
    };

    Error GetError();
    const std::string ErrorMessage(Error error);

    namespace detail
    {
        void set_error(Error error);
        // converts valid 0x or h representations, as well as whatever stroll supports
        // does not modify value if str is not a valid number
        bool str_to_ll(const char* str, long long& value);
        // either 0x followed by at least one hex digit, or hex digits followed by a 'h'
        bool starts_with_hex_number(const char* str, const char** first = nullptr);
        // a sequence of base-10 digits
        bool starts_with_decimal_integer(const char* at);
        // true if string is just whitespace, or 0 length
        bool is_null_or_empty(const char* str);
        // returns pointer to *second* word, or nullptr
        const char* next_word_or_number(const char* str);

        struct simple_tokens_t
        {
            // indices to tokens
            unsigned char _token_idx[8];
            unsigned char _token_end_idx[8];
            unsigned char _num_tokens = 0;
        };
        // in-place 0 terminates tokens
        simple_tokens_t simple_tokenise(const char* str, size_t max_tokens = sizeof(simple_tokens_t::_token_idx));

    }  // namespace detail

}  // namespace inasm64
