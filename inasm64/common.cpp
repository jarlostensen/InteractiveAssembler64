// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include "common.h"
#include <cassert>
#include <string>

namespace inasm64
{
    namespace detail
    {
        Error _error = Error::kNoError;

        void set_error(Error error)
        {
            _error = error;
        }

        number_format_t starts_with_integer(char* at, char** first)
        {
            if(!at[0])
                return number_format_t::kUnknown;
            if(at[0] == '-' || at[0] == '+')
                ++at;
            if(first)
                *first = at;
            if(at[0] == '0')
            {
                if(at[1] == 'x' || at[1] == 'b')
                {
                    at += 2;
                    if(first)
                        *first = at;
                }
                // 0xabcde....
                if(at[-1] == 'x')
                {
                    return isdigit(at[0]) || (at[0] >= 'a' && at[0] <= 'f') ? number_format_t::kHexadecimal : number_format_t::kUnknown;
                }
                // 0b10110011...
                else if(at[-1] == 'b')
                {
                    return (at[0] == '1' || at[0] == '0') ? number_format_t::kBinary : number_format_t::kUnknown;
                }
            }
            // pure decimal integer of at least one digit
            return isdigit(at[0]) ? number_format_t::kDecimal : number_format_t::kUnknown;
        }

        bool starts_with_decimal_integer(const char* at)
        {
            if(!at[0])
                return false;
            while(at[0] && isdigit(at[0]))
                ++at;
            return !at[0] || at[0] == ' ';
        }

        bool starts_with_hex_number(const char* at, const char** first)
        {
            if(first)
                *first = at;
            if(at[0] == '0' && at[1] == 'x')
            {
                at += 2;
                if(first)
                    *first = at;
                return isdigit(at[0]) || (at[0] >= 'a' && at[0] <= 'f');
            }
            return false;
        }

        bool is_null_or_empty(const char* str)
        {
            if(!str)
                return true;
            while(str[0] && str[0] == ' ')
                ++str;
            return !str[0];
        }

        simple_tokens_t simple_tokenise(const char* str_, size_t max_tokens)
        {
            const auto str_len = strlen(str_);
            if(!str_len)
                return {};

            auto str = str_;
            simple_tokens_t stokens;
            do
            {
                // skip whitespace
                while(str[0] && str[0] == ' ')
                    ++str;
                const auto rp = str;
                // skip past token
                while(str[0] && str[0] != ' ')
                    ++str;
                if(rp[0])
                {
                    stokens._token_idx[stokens._num_tokens] = (unsigned char)(rp - str_);
                    stokens._token_end_idx[stokens._num_tokens] = (unsigned char)(str - str_);
                    ++str;
                    ++stokens._num_tokens;
                }
                assert(stokens._num_tokens <= std::size(stokens._token_idx));
            } while(str[0] && stokens._num_tokens <= max_tokens);
            return stokens;
        }

    }  // namespace detail

    Error GetError()
    {
        return detail::_error;
    }

    const std::string ErrorMessage(Error error)
    {
        switch(error)
        {
        case Error::kNoError:
            return "no error";
        case Error::kCliInputLengthExceeded:
            return "Max CLI input length exceeded";
        case Error::kRuntimeUninitialised:
            return "Runtime has not been properly notinitialised";
        case Error::kUndefinedVariable:
            return "Undefined variable";
        case Error::kSystemError:
            return "general system error";
        case Error::kCodeBufferFull:
            return "code buffer is full; unable to assemble more instructions";
        case Error::kInvalidAddress:
            return "invalid address";
        case Error::kAccessViolation:
            return "access violation";
        case Error::kInvalidCommandFormat:
            return "invalid or unrecognized command format";
        case Error::kNoMoreCode:
            return "no more code to execute";
        case Error::kCodeBufferOverflow:
            return "no more room for code";
        case Error::kUnrecognizedRegisterName:
            return "unrecognized register name";
        case Error::kInvalidInstructionFormat:
            return "Invalid instruction format";
        case Error::kInvalidOperandFormat:
            return "Invalid operand format";
        case Error::kInvalidOperandScale:
            return "Invalid operand scale";
        case Error::kInvalidDestRegistername:
            return "Invalid destination register name";
        case Error::kInvalidInstructionName:
            return "Invalid instruction name";
        case Error::kOperandSizesMismatch:
            return "Operand sizes mismatch";
        case Error::kEmptyInput:
            return "Invalid, empty, input";
        case Error::UnsupportedInstructionFormat:
            return "Unsupported instruction format; not implemented yet!";
        case Error::kMemoryWriteSizeMismatch:
            return "Attempting to overwrite memory";
        case Error::kMemoryReadSizeMismatch:
            return "Attempting to read more memory than available";
        case Error::kInvalidImmediateOperandBitWidth:
            return "Immediate operand bit width is incorrect for this instruction";
        case Error::kCliUninitialised:
            return "CLI hasn't been properly initialised";
        case Error::kCliUnknownCommand:
            return "Unknown CLI command";
        case Error::kInvalidRegisterName:
            return "invalid register for this operation";
        case Error::kInvalidInputValueFormat:
            return "input number format is invalid";
        case Error::kUnsupportedCpuFeature:
            return "this feature is unsupported on this CPU";
        case Error::kUnsupportedInstructionType:
            return "unsupported instruction (for now)";
        case Error::kEncodeError:
            return "General encoder error";
        default:
            return "";
        }
    }

}  // namespace inasm64
