//NOTE: this is to workaround a known problem/bug with intellisense and PCH's in vs2019
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <sstream>
#include <vector>

#include "configuration.h"
#include "runtime.h"
#include "assembler.h"
#include "cli.h"

namespace inasm64
{
    namespace cli
    {
        AssembledInstructionInfo _asm_info;
        //TODO: determine if this should be dynamic, or if this is even the right approach. Need to get the proper assembler up and running first.
        uint8_t _code_buffer[kMaxAssembledInstructionSize * 128];
        size_t _code_buffer_pos = 0;
        //TODO: error handling and error paths
        Mode _mode = Mode::Processing;

        namespace
        {
            //TODO: helper, could be elsewhere
            std::vector<std::string> split(const std::string& s, char delimiter)
            {
                std::vector<std::string> tokens;
                std::string token;
                std::istringstream tokenStream(s);
                while(std::getline(tokenStream, token, delimiter))
                {
                    tokens.push_back(token);
                }
                return tokens;
            }

            //TODO: perhaps this should just be a part of runtime?
            bool setreg(const char* regName, int64_t value)
            {
                const auto len = strlen(regName);
                if(len > 3)
                    return false;
                auto result = true;
                auto prefixHi = char(::toupper(regName[0]));
                if(len == 2 && prefixHi != 'R')
                {
                    // should be a byte- or word -register
                    switch(prefixHi)
                    {

// sadly this magic (https://stackoverflow.com/questions/35525555/c-preprocessor-how-to-create-a-character-literal) does not work with MS141
// complains about newline in constant at SINGLEQUOTE
//#define CONCAT_H(x, y, z) x##y##z
//#define SINGLEQUOTE ' 
//#define CONCAT(x, y, z) CONCAT_H(x, y, z)
//#define CHARIFY(x) CONCAT(SINGLEQUOTE, x, SINGLEQUOTE)

#define _SETNAMEDBYTEREG_HL(prefix)                                                  \
        if(regName[1] == 'l' || regName[1] == 'L')                                   \
        {                                                                            \
            runtime::SetReg(runtime::ByteReg::##prefix##L, int8_t(value & 0xff));    \
        }                                                                            \
        else if(regName[1] == 'h' || regName[1] == 'H')                              \
        {                                                                            \
            runtime::SetReg(runtime::ByteReg::##prefix##H, int8_t(value & 0xff));    \
        }                                                                            \
        else if(regName[1] == 'x' || regName[1] == 'X')                              \
        {                                                                            \
            runtime::SetReg(runtime::WordReg::##prefix##X, int16_t(value & 0xffff)); \
        }                                                                            \
        else                                                                         \
            result = false;                                                          \
        break
                    case 'A':
                        _SETNAMEDBYTEREG_HL(A);
                    case 'B':
                        _SETNAMEDBYTEREG_HL(B);
                    case 'C':
                        _SETNAMEDBYTEREG_HL(C);
                    case 'D':
                        _SETNAMEDBYTEREG_HL(D);
                    default:
                        if(regName[1] == 'i' || regName[1] == 'I')
                        {
                            if(prefixHi == 'D')
                            {
                                runtime::SetReg(runtime::WordReg::DI, int16_t(value & 0xffff));
                            }
                            else if(prefixHi == 'S')
                            {
                                runtime::SetReg(runtime::WordReg::SI, int16_t(value & 0xffff));
                            }
                            else
                                result = false;
                        }
                        else if(regName[1] == 'p' || regName[1] == 'P')
                        {
                            if(prefixHi == 'B')
                            {
                                runtime::SetReg(runtime::WordReg::BP, int16_t(value & 0xffff));
                            }
                            else if(prefixHi == 'S')
                            {
                                runtime::SetReg(runtime::WordReg::SP, int16_t(value & 0xffff));
                            }
                            else
                                result = false;
                        }
                        else
                            result = false;
                        break;
                    }
                }
                else if(len == 3 || prefixHi == 'R')
                {
                    // 32- or 64 -bit registers
                    if(prefixHi == 'E')
                    {
                        prefixHi = char(::toupper(regName[1]));
                        switch(prefixHi)
                        {
#define _SETNAMEDDWORDREG(prefix)                                                          \
        if(regName[2] == 'x' || regName[2] == 'X')                                         \
        {                                                                                  \
            runtime::SetReg(runtime::DWordReg::E##prefix##X, int32_t(value & 0xffffffff)); \
        }                                                                                  \
        else                                                                               \
            result = false;                                                                \
        break
                        case 'A':
                            _SETNAMEDDWORDREG(A);
                        case 'B':
                            _SETNAMEDDWORDREG(B);
                        case 'C':
                            _SETNAMEDDWORDREG(C);
                        case 'D':
                            _SETNAMEDDWORDREG(D);
                        default:
                            if(regName[2] == 'i' || regName[2] == 'I')
                            {
                                if(prefixHi == 'D')
                                {
                                    runtime::SetReg(runtime::DWordReg::EDI, int32_t(value & 0xffffffff));
                                }
                                else if(prefixHi == 'S')
                                {
                                    runtime::SetReg(runtime::DWordReg::ESI, int32_t(value & 0xffffffff));
                                }
                                else
                                    result = false;
                            }
                            else if(regName[2] == 'p' || regName[2] == 'P')
                            {
                                if(prefixHi == 'B')
                                {
                                    runtime::SetReg(runtime::DWordReg::EBP, int16_t(value & 0xffffffff));
                                }
                                else if(prefixHi == 'S')
                                {
                                    runtime::SetReg(runtime::DWordReg::ESP, int16_t(value & 0xffffffff));
                                }
                                else
                                    result = false;
                            }
                            else
                                result = false;
                            break;
                        }
                    }
                    else if(prefixHi == 'R')
                    {
                        prefixHi = char(::toupper(regName[1]));
                        switch(prefixHi)
                        {
#define _SETNAMEDQWORDREG(prefix)                                    \
        if(regName[2] == 'x' || regName[2] == 'X')                   \
        {                                                            \
            runtime::SetReg(runtime::QWordReg::R##prefix##X, value); \
        }                                                            \
        else                                                         \
            result = false;                                          \
        break
                        case 'A':
                            _SETNAMEDQWORDREG(A);
                        case 'B':
                            _SETNAMEDQWORDREG(B);
                        case 'C':
                            _SETNAMEDQWORDREG(C);
                        case 'D':
                            _SETNAMEDQWORDREG(D);
                        default:
                            if(regName[2] == 'i' || regName[2] == 'I')
                            {
                                if(prefixHi == 'D')
                                {
                                    runtime::SetReg(runtime::QWordReg::RDI, value);
                                }
                                else if(prefixHi == 'S')
                                {
                                    runtime::SetReg(runtime::QWordReg::RSI, value);
                                }
                                else
                                    result = false;
                            }
                            else if(regName[2] == 'p' || regName[2] == 'P')
                            {
                                if(prefixHi == 'B')
                                {
                                    runtime::SetReg(runtime::QWordReg::RBP, value);
                                }
                                else if(prefixHi == 'S')
                                {
                                    runtime::SetReg(runtime::QWordReg::RSP, value);
                                }
                                else
                                    result = false;
                            }
                            else
                            {
                                // R8-R15?
                                const auto ordinal = ::atol(regName + 1);
                                switch(ordinal)
                                {
#define _SETORDQWORDREG(ord)\
    case ord:\
        runtime::SetReg(runtime::QWordReg::R##ord, value)

                                _SETORDQWORDREG(8);
                                _SETORDQWORDREG(9);
                                _SETORDQWORDREG(10);
                                _SETORDQWORDREG(11);
                                _SETORDQWORDREG(12);
                                _SETORDQWORDREG(13);
                                _SETORDQWORDREG(14);
                                _SETORDQWORDREG(15);
                                default:
                                    result = false;
                                break;
                                }                                
                            }
                            break;
                        }
                    }
                    else
                        result = false;
                }

                return result;
            }

        }  // namespace

        std::string Help()
        {
            static const std::string _help = "h:\tthis text\na:\tstart assembling instructions, empty input ends\np:\tsingle step one instruction\nq:\tquit";
            return _help;
        }

        const AssembledInstructionInfo* inasm64::cli::LastAssembledInstructionInfo()
        {
            if(_mode == Mode::Assembling)
                return &_asm_info;
            return nullptr;
        }

        Mode ActiveMode()
        {
            return _mode;
        }

        Command Execute(std::string& commandLine)
        {
            //TODO: not at all happy with this code and all the early out returns; refactor and clean up once flow is clear

            Command result = Command::Invalid;

            static std::string _whitespace = " \t";
            const auto n = commandLine.find_first_not_of(_whitespace);

            // if we're in assembly mode we treat every non-empty input as an assembly instruction
            if(_mode == Mode::Assembling)
            {
                if(n == std::string::npos)
                {
                    if(_code_buffer_pos)
                    {
                        // submit instructions assembled so far
                        runtime::AddCode(_code_buffer, _code_buffer_pos);
                        _code_buffer_pos = 0;
                    }
                    _mode = Mode::Processing;
                    return Command::Assemble;
                }

                if(!assembler::Assemble(commandLine, _asm_info))
                {
                    //TODO: proper error handling
                    _mode = Mode::Processing;
                    return Command::Invalid;
                }

                // cache instructions while they are being entered, we'll submi them to the runtime when we exit assembly mode
                //TODO: error/overflow handling
                memcpy(_code_buffer + _code_buffer_pos, _asm_info.Instruction, _asm_info.InstructionSize);
                _code_buffer_pos += _asm_info.InstructionSize;
                _asm_info.Address = reinterpret_cast<const void*>(reinterpret_cast<const uint8_t*>(runtime::InstructionWriteAddress()) + _code_buffer_pos);
            }
            else
            {
                if(n == std::string::npos)
                {
                    //TODO: proper error handling
                    _mode = Mode::Processing;
                    return Command::Invalid;
                }

                const auto tokens = split(commandLine, ' ');
                //TODO: make this more resilient (and bark on invalid commands, like "raaaa"...)
                const auto cmd = char(::tolower(tokens[0][0]));
                switch(cmd)
                {
                case 'r':
                {
                    if(tokens.size() != 3)
                    {
                        //TODO: proper error handling
                        return Command::Invalid;
                    }
                    
                    //ZZZ: assumes hex input
                    const int64_t value = ::strtoll(tokens[2].c_str(),nullptr,16);
                    //TODO: error/invalid number handling
                    setreg(tokens[1].c_str(), value);

                    return Command::Reg;
                }
                case 'a':
                    _mode = Mode::Assembling;
                    _asm_info.Address = runtime::InstructionWriteAddress();
                    return Command::Assemble;
                case 'q':
                    runtime::Shutdown();
                    return Command::Quit;
                case 'p':
                    return runtime::Step() ? Command::Step : Command::Invalid;
                case 'h':
                    return Command::Help;
                default:
                    break;
                }
            }
            return result;
        }
    }  // namespace cli
}  // namespace inasm64