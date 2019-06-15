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
            return "invalid, empty, input";
        default:
            return "";
        }
    }

}  // namespace inasm64
