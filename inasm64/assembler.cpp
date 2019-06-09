//ZZZ: precompiled headers don't work across directory levels, known VS bug
#include <string>
#include <cstdint>
#include <vector>

#include "common.h"
#include "assembler.h"

extern "C"
{
#include "xed-interface.h"
}

namespace inasm64
{
    namespace assembler
    {
        namespace
        {
            xed_uint8_t _instruction[XED_MAX_INSTRUCTION_BYTES];
            unsigned _instruction_size = 0;
        }  // namespace

        bool Initialise()
        {
            xed_tables_init();

            //TESTING: a basic implementation of using XED to encode the statement INC RAX

            xed_encoder_request_t req;
            xed_state_t estate;
            estate.mmode = XED_MACHINE_MODE_LONG_64;
            //in 64 bit long mode this is ignored; estate.stack_addr_width = XED_ADDRESS_WIDTH_32b;
            xed_encoder_request_zero_set_mode(&req, &estate);
            xed_encoder_request_set_effective_operand_width(&req, 64);
            xed_encoder_request_set_iclass(&req, XED_ICLASS_INC);
            xed_encoder_request_set_reg(&req, XED_OPERAND_REG0, XED_REG_RAX);
            xed_encoder_request_set_operand_order(&req, 0, XED_OPERAND_REG0);
            unsigned ilen = XED_MAX_INSTRUCTION_BYTES;
            auto xed_error = xed_encode(&req, _instruction, ilen, &_instruction_size);
            auto encode_okay = (xed_error == XED_ERROR_NONE);

            // this is a much easier way to build the encoder request, see xed-ex5-enc.c example for details.
            xed_encoder_instruction_t x;
            xed_encoder_request_t enc_req;
            xed_state_t dstate;

            dstate.mmode = XED_MACHINE_MODE_LONG_64;
            //dstate.stack_addr_width = XED_ADDRESS_WIDTH_32b;

            xed_inst1(&x, dstate, XED_ICLASS_INC, 64, xed_reg(XED_REG_RAX));

            xed_encoder_request_zero_set_mode(&enc_req, &dstate);
            encode_okay = xed_convert_to_encoder_request(&enc_req, &x);
            xed_error = xed_encode(&enc_req, _instruction, ilen, &_instruction_size);
            encode_okay = (xed_error == XED_ERROR_NONE);

            return encode_okay;
        }

        enum class ParseMode
        {
            SkipWhitespace,
            ScanUntilWhitespaceOrComma,
            ScanUntilRightBracket
        };

        bool Assemble(const std::string& assembly, AssembledInstructionInfo& info)
        {
            char buffer[1024];
            // strip off any leading whitespace (yes, the leading space below is intentional)
            memcpy(buffer, assembly.c_str(), assembly.length() + 1);
            const auto input_length = strlen(buffer);
            // left of the , (i.e. instruction and 1st operand)
            std::vector<const char*> left_tokens;
            // right of the , (i.e. remaining operands)
            std::vector<const char*> right_tokens;
            std::vector<const char*>* tokens = &left_tokens;

            auto mode = ParseMode::SkipWhitespace;
            size_t rp = 0;
            size_t ts = 0;
            while(rp < input_length)
            {
                switch(mode)
                {
                case ParseMode::SkipWhitespace:
                    while(buffer[rp] && (buffer[rp] == ' ' || buffer[rp] == '\t'))
                    {
                        ++rp;
                    }
                    // switch token list when we hit the , (if we do)
                    if(buffer[rp] == ',')
                    {
                        tokens = &right_tokens;
                        ++rp;
                        //and don't switch mode here
                    }
                    else
                    {
                        ts = rp;
                        mode = ParseMode::ScanUntilWhitespaceOrComma;
                    }
                    break;
                case ParseMode::ScanUntilWhitespaceOrComma:
                    while(buffer[rp] && buffer[rp] != ' ' && buffer[rp] != '\t' && buffer[rp] != ',')
                    {
                        ++rp;
                        if(buffer[rp] == '[')
                        {
                            mode = ParseMode::ScanUntilRightBracket;
                            break;
                        }
                    }
                    if(mode != ParseMode::ScanUntilRightBracket)
                    {
                        tokens->push_back(buffer + ts);
                        if(buffer[rp] == ',')
                            // switch to right side when we hit the ,
                            tokens = &right_tokens;
                        buffer[rp] = 0;
                        mode = ParseMode::SkipWhitespace;
                    }
                    ++rp;
                    break;
                case ParseMode::ScanUntilRightBracket:
                    while(buffer[rp] && buffer[rp] != ']')
                    {
                        ++rp;
                    }
                    tokens->push_back(buffer + ts);
                    buffer[++rp] = 0;
                    mode = ParseMode::SkipWhitespace;
                    break;
                }
            }

            //TESTING:
            memcpy(const_cast<uint8_t*>(info.Instruction), _instruction, _instruction_size);
            const_cast<size_t&>(info.InstructionSize) = _instruction_size;
            const_cast<std::string&>(info.Assembly) = assembly;
            return true;
        }
    }  // namespace assembler
}  // namespace inasm64
