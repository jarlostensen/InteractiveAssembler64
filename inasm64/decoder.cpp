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
#include "x64.h"
#include "decoder.h"

#include "xed_iclass_instruction_set.h"

extern "C" {
#include "xed-interface.h"
}

#include <stdio.h>

namespace inasm64
{
    namespace decoder
    {
        const xed_state_t _dstate = { XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b };

        void print_misc(xed_decoded_inst_t* xedd)
        {
            int i;
            const xed_operand_values_t* ov = xed_decoded_inst_operands_const(xedd);
            const xed_inst_t* xi = xed_decoded_inst_inst(xedd);
            xed_exception_enum_t e = xed_inst_exception(xi);
            xed_uint_t np = xed_decoded_inst_get_nprefixes(xedd);
            xed_isa_set_enum_t isaset = xed_decoded_inst_get_isa_set(xedd);
            if(xed_operand_values_has_real_rep(ov))
            {
                xed_iclass_enum_t norep =
                    xed_rep_remove(xed_decoded_inst_get_iclass(xedd));
                printf("REAL REP ");
                printf("\tcorresponding no-rep iclass: %s\n",
                    xed_iclass_enum_t2str(norep));
            }
            if(xed_operand_values_has_rep_prefix(ov))
            {
                printf("F3 PREFIX\n");
            }
            if(xed_operand_values_has_repne_prefix(ov))
            {
                printf("F2 PREFIX\n");
            }
            if(xed_operand_values_has_address_size_prefix(ov))
            {
                printf("67 PREFIX\n");
            }
            if(xed_operand_values_has_operand_size_prefix(ov))
            {
                /* this 66 prefix is not part of the opcode */
                printf("66-OSZ PREFIX\n");
            }
            if(xed_operand_values_has_66_prefix(ov))
            {
                /* this is any 66 prefix including the above */
                printf("ANY 66 PREFIX\n");
            }
            if(xed_decoded_inst_get_attribute(xedd, XED_ATTRIBUTE_RING0))
            {
                printf("RING0 only\n");
            }
            if(e != XED_EXCEPTION_INVALID)
            {
                printf("EXCEPTION TYPE: %s\n", xed_exception_enum_t2str(e));
            }
            if(xed_decoded_inst_is_broadcast(xedd))
                printf("BROADCAST\n");

            if(xed_classify_sse(xedd) || xed_classify_avx(xedd) || xed_classify_avx512(xedd))
            {
                if(xed_classify_avx512_maskop(xedd))
                    printf("AVX512 KMASK-OP\n");
                else
                {
                    xed_bool_t sse = 0;
                    if(xed_classify_sse(xedd))
                    {
                        sse = 1;
                        printf("SSE\n");
                    }
                    else if(xed_classify_avx(xedd))
                        printf("AVX\n");
                    else if(xed_classify_avx512(xedd))
                        printf("AVX512\n");

                    if(xed_decoded_inst_get_attribute(xedd, XED_ATTRIBUTE_SIMD_SCALAR))
                        printf("SCALAR\n");
                    else
                    {
                        // xed_decoded_inst_vector_length_bits is only for VEX/EVEX instr.
                        // This will print 128 vl for FXSAVE and LD/ST MXCSR which is unfortunate.
                        xed_uint_t vl_bits = sse ? 128 : xed_decoded_inst_vector_length_bits(xedd);
                        printf("Vector length: %u \n", vl_bits);
                    }
                }
            }
            // does not include instructions that have XED_ATTRIBUTE_MASK_AS_CONTROL.
            // does not include vetor instructions that have k0 as a mask register.
            if(xed_decoded_inst_masked_vector_operation(xedd))
                printf("WRITE-MASKING\n");
            if(np)
                printf("Number of legacy prefixes: %u \n", np);
            printf("ISA SET: [%s]\n", xed_isa_set_enum_t2str(isaset));
            for(i = 0; i < XED_MAX_CPUID_BITS_PER_ISA_SET; i++)
            {
                xed_cpuid_bit_enum_t cpuidbit;
                xed_cpuid_rec_t crec;
                int r;
                cpuidbit = xed_get_cpuid_bit_for_isa_set(isaset, i);
                if(cpuidbit == XED_CPUID_BIT_INVALID)
                    break;
                printf("%d\tCPUID BIT NAME: [%s]\n", i, xed_cpuid_bit_enum_t2str(cpuidbit));
                r = xed_get_cpuid_rec(cpuidbit, &crec);
                if(r)
                {
                    printf("\tLeaf 0x%08x, subleaf 0x%08x, %s[%u]\n",
                        crec.leaf, crec.subleaf, xed_reg_enum_t2str(crec.reg),
                        crec.bit);
                }
                else
                {
                    printf("Could not find cpuid leaf information\n");
                }
            }
        }

        InstructionInfo Decode(const void* instr, size_t length)
        {
            xed_decoded_inst_t xedd;
            xed_decoded_inst_zero_set_mode(&xedd, &_dstate);
            xed_decoded_inst_set_input_chip(&xedd, XED_CHIP_ALL);
            const auto xed_error = xed_decode(&xedd, XED_REINTERPRET_CAST(const xed_uint8_t*, instr), (const unsigned int)(length));
            if(xed_error == XED_ERROR_NONE)
            {
                InstructionInfo info;
                const auto iclass = xed_decoded_inst_get_iclass(&xedd);
                const auto isaset = xed_decoded_inst_get_isa_set(&xedd);
				// assume supported
                info._supported = true;

                // check if this instruction is supported natively by the hardware
                Cpuid cpuid;
                for(auto i = 0; i < XED_MAX_CPUID_BITS_PER_ISA_SET; i++)
                {
                    xed_cpuid_bit_enum_t cpuidbit = xed_get_cpuid_bit_for_isa_set(isaset, i);
                    if(cpuidbit == XED_CPUID_BIT_INVALID)
                        break;
                    xed_cpuid_rec_t crec;
                    int r = xed_get_cpuid_rec(cpuidbit, &crec);
                    if(r)
                    {
                        cpuid(crec.leaf, crec.subleaf);
                        switch(crec.reg)
                        {
                        case XED_REG_EAX:
                            info._supported = (cpuid._regs[0] & (1 << crec.bit)) != 0;
                            break;
                        case XED_REG_EBX:
                            info._supported = (cpuid._regs[1] & (1 << crec.bit)) != 0;
                            break;
                        case XED_REG_ECX:
                            info._supported = (cpuid._regs[2] & (1 << crec.bit)) != 0;
                            break;
                        case XED_REG_EDX:
                            info._supported = (cpuid._regs[3] & (1 << crec.bit)) != 0;
                            break;
                        default:;
                        }
                    }
                }

                info._ring0 = xed_decoded_inst_get_attribute(&xedd, XED_ATTRIBUTE_RING0) != 0;

                //ZZZ: the XED enums are interleaved with some VT-X instructions, is this check going to be reliable or is there a XED function to get the ranges?
                if((iclass >= XED_ICLASS_INT && iclass <= XED_ICLASS_INTO) || (iclass >= XED_ICLASS_IRET && iclass <= XED_ICLASS_JZ) ||
                    (iclass == XED_ICLASS_CALL_FAR || iclass == XED_ICLASS_CALL_NEAR))
                {
                    info._class = InstructionInfo::InstructionClass::kBranching;
                }
                else if(iclass == XED_ICLASS_SYSCALL || iclass == XED_ICLASS_SYSCALL_AMD)
                {
                    info._class = InstructionInfo::InstructionClass::kSyscall;
                }
                else if(iclass == XED_ICLASS_VMCALL)
                {
                    info._class = InstructionInfo::InstructionClass::kVmcall;
                }
                else
                {
                    if(xed_classify_sse(&xedd))
                    {
                        info._class = InstructionInfo::InstructionClass::kSse;
                    }
                    else if(xed_classify_avx(&xedd))
                    {
                        info._class = InstructionInfo::InstructionClass::kAvx;
                    }
                    else if(xed_classify_avx512(&xedd))
                    {
                        info._class = InstructionInfo::InstructionClass::kAvx512;
                    }
                }
                return info;
            }
            return {};
        }
    }  // namespace decoder
}  // namespace inasm64