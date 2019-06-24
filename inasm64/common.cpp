#include "common.h"
#include <string>

namespace inasm64
{
    namespace detail
    {
        Error _error = Error::NoError;

        void SetError(Error error)
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

        bool starts_with_hex_number(const char* at)
        {
            if(at[0] == '0' && at[1] == 'x')
            {
                at += 2;
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

    }  // namespace detail

    Error GetError()
    {
        return detail::_error;
    }

    const std::string ErrorMessage(Error error)
    {
        switch(error)
        {
        case Error::NoError:
            return "no error";
        case Error::CliInputLengthExceeded:
            return "Max CLI input length exceeded";
        case Error::UndefinedVariable:
            return "Undefined variable";
        case Error::SystemError:
            return "general system error";
        case Error::CodeBufferFull:
            return "code buffer is full; unable to assemble more instructions";
        case Error::InvalidAddress:
            return "invalid address";
        case Error::InvalidCommandFormat:
            return "invalid or unrecognized command format";
        case Error::NoMoreCode:
            return "no more code to execute";
        case Error::UnrecognizedRegisterName:
            return "unrecognized or invalid register name";
        case Error::InvalidInstructionFormat:
            return "Invalid instruction format";
        case Error::InvalidOperandFormat:
            return "Invalid operand format";
        case Error::InvalidOperandScale:
            return "Invalid operand scale";
        case Error::InvalidDestRegistername:
            return "Invalid destination register name";
        case Error::InvalidInstructionName:
            return "Invalid instruction name";
        case Error::OperandSizesMismatch:
            return "Operand sizes mismatch";
        case Error::EmptyInput:
            return "Invalid, empty, input";
        case Error::UnsupportedInstructionFormat:
            return "Unsupported instruction format; not implemented yet!";
        case Error::MemoryWriteSizeMismatch:
            return "Attempting to overwrite memory";
        case Error::MemoryReadSizeMismatch:
            return "Attempting to read more memory than available";
        case Error::EncodeError:
            return "General encoder error";
        default:
            return "";
        }
    }

}  // namespace inasm64
