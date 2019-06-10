#include <cstdint>
#include <memory>
#include "xed_assembler_driver.h"

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

        XedAssemblerDriver::XedAssemblerDriver()
        {
        }

        bool XedAssemblerDriver::Initialise()
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

        size_t XedAssemblerDriver::Assemble(const Statement& /*statement*/, uint8_t* buffer)
        {
            memcpy(buffer, _instruction, _instruction_size);
            return _instruction_size;
        }

    }  // namespace assembler
}  // namespace inasm64