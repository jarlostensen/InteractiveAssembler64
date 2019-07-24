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
        kUnsupportedCpuFeature,
        kAccessViolation,
        kSystemError,
    };

    Error GetError();
    const std::string ErrorMessage(Error error);

    namespace detail
    {
        // used by char* map
        struct hash_32_fnv1a
        {
            static constexpr uint32_t val_32_const = 0x811c9dc5;
            static constexpr uint32_t prime_32_const = 0x1000193;
            uint32_t hash_32_fnv1a_const(const char* const str, const uint32_t value = val_32_const) const
            {
                return (str[0] == 0) ? value : hash_32_fnv1a_const(str + 1, uint32_t(1ull * (value ^ uint32_t(str[0])) * prime_32_const));
            }
            int operator()(const char* str) const
            {
                return hash_32_fnv1a_const(str);
            }
        };
        // used by char* map
        struct striequal
        {
            bool operator()(const char* __x, const char* __y) const
            {
                return _stricmp(__x, __y) == 0;
            }
        };
        using char_string_map_t = std::unordered_map<const char*, uintptr_t, hash_32_fnv1a, striequal>;

        void set_error(Error error);

        enum class number_format_t
        {
            kBinary,
            kDecimal,
            kHexadecimal,
            kUnknown
        };
        number_format_t starts_with_integer(char* str, char** first = nullptr);

        // either 0x followed by at least one hex digit, or hex digits followed by a 'h'
        bool starts_with_hex_number(const char* str, const char** first = nullptr);
        // a sequence of base-10 digits
        bool starts_with_decimal_integer(const char* at);

        // true if string is just whitespace, or 0 length
        bool is_null_or_empty(const char* str);

        // stores a small set of tokenised string components, split by 0
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
