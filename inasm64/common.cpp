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

        bool str_to_ll(const char* str, long long& value_)
        {
            const auto base = (detail::starts_with_hex_number(str) ? 16 : 0);
            const long long value = ::strtoll(str, nullptr, base);
            if(value != LLONG_MAX && value != LLONG_MIN)
            {
                value_ = value;
                return true;
            }
            return false;
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
            do
            {
                if(!isdigit(at[0]) && (at[0] < 'a' || at[0] > 'f'))
                    return false;
                ++at;
            } while(at[0] && at[0] != 'h');
            return true;
        }

        const char* next_word_or_number(const char* str)
        {
            while(str[0] && str[0] != ' ')
                ++str;
            while(str[0] && str[0] == ' ')
                ++str;
            return str[0] ? str : nullptr;
        }

        bool is_null_or_empty(const char* str)
        {
            if(!str)
                return true;
            while(str[0] && str[0] == ' ')
                ++str;
            return !str[0];
        }

        simple_tokens_t simple_tokenise(const char* str_)
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
            } while(str[0]);
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
        case Error::kEncodeError:
            return "General encoder error";
        default:
            return "";
        }
    }

}  // namespace inasm64
