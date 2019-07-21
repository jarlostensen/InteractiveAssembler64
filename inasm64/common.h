// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

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
        kInvalidInputValueFormat,
        kAccessViolation,
        kSystemError,
    };

    Error GetError();
    const std::string ErrorMessage(Error error);

    namespace detail
    {
        void set_error(Error error);

        enum class number_format_t
        {
            kBinary,
            kDecimal,
            kHexadecimal,
            kUnknown
        };
        number_format_t starts_with_integer(char* str, char** first = nullptr);

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
