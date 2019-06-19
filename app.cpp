
#include <iostream>
#include <iomanip>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "inasm64/common.h"
#include "inasm64/ia64.h"
#include "inasm64/runtime.h"
#include "inasm64/assembler.h"
#include "inasm64/cli.h"

std::ostream& coutreg(const char* reg)
{
    // perhaps I should just have used printf....
    return std::cout << std::setfill(' ') << std::setw(4) << reg << std::setw(1) << " = 0x" << std::setfill('0') << std::setw(16) << std::hex;
}

void DumpRegs()
{
    const auto ctx = inasm64::runtime::Context();

    coutreg("rax") << ctx->OsContext->Rax << " ";
    coutreg("rbx") << ctx->OsContext->Rbx << " ";
    coutreg("rcx") << ctx->OsContext->Rcx << " ";
    coutreg("rdx") << ctx->OsContext->Rdx << std::endl;

    coutreg("rsi") << ctx->OsContext->Rsi << " ";
    coutreg("rdi") << ctx->OsContext->Rdi << " ";
    coutreg("rbp") << ctx->OsContext->Rbp << " ";
    coutreg("rsp") << ctx->OsContext->Rsp << std::endl;

    coutreg("r8") << ctx->OsContext->R8 << " ";
    coutreg("r9") << ctx->OsContext->R9 << " ";
    coutreg("r10") << ctx->OsContext->R10 << " ";
    coutreg("r11") << ctx->OsContext->R11 << std::endl;
    coutreg("r12") << ctx->OsContext->R12 << " ";
    coutreg("r13") << ctx->OsContext->R13 << " ";
    coutreg("r14") << ctx->OsContext->R14 << " ";
    coutreg("r15") << ctx->OsContext->R15 << std::endl;

    std::cout << "\n";
    coutreg("eflags") << ctx->OsContext->EFlags << std::endl;

    //TODO: XMM, FP, make it selectable, etc.
}

void DumpFpRegisters()
{
    const auto ctx = inasm64::runtime::Context();

    coutreg("xmm0") << ctx->OsContext->Xmm0.Low << ctx->OsContext->Xmm0.High << " ";
    coutreg("xmm1") << ctx->OsContext->Xmm1.Low << ctx->OsContext->Xmm1.High << " " << std::endl;
    coutreg("xmm2") << ctx->OsContext->Xmm2.Low << ctx->OsContext->Xmm2.High << " ";
    coutreg("xmm3") << ctx->OsContext->Xmm3.Low << ctx->OsContext->Xmm3.High << " " << std::endl;
    coutreg("xmm4") << ctx->OsContext->Xmm4.Low << ctx->OsContext->Xmm4.High << " ";
    coutreg("xmm5") << ctx->OsContext->Xmm5.Low << ctx->OsContext->Xmm5.High << " " << std::endl;
    coutreg("xmm6") << ctx->OsContext->Xmm6.Low << ctx->OsContext->Xmm6.High << " ";
    coutreg("xmm7") << ctx->OsContext->Xmm7.Low << ctx->OsContext->Xmm7.High << " " << std::endl;
    coutreg("xmm8") << ctx->OsContext->Xmm8.Low << ctx->OsContext->Xmm8.High << " ";
    coutreg("xmm9") << ctx->OsContext->Xmm9.Low << ctx->OsContext->Xmm9.High << " " << std::endl;
    coutreg("xmm10") << ctx->OsContext->Xmm10.Low << ctx->OsContext->Xmm10.High << " ";
    coutreg("xmm11") << ctx->OsContext->Xmm11.Low << ctx->OsContext->Xmm11.High << " " << std::endl;
    coutreg("xmm12") << ctx->OsContext->Xmm12.Low << ctx->OsContext->Xmm12.High << " ";
    coutreg("xmm13") << ctx->OsContext->Xmm13.Low << ctx->OsContext->Xmm13.High << " " << std::endl;
    coutreg("xmm14") << ctx->OsContext->Xmm14.Low << ctx->OsContext->Xmm14.High << " ";
    coutreg("xmm15") << ctx->OsContext->Xmm15.Low << ctx->OsContext->Xmm15.High << std::endl;
}

void DumpAvxRegisters()
{
    const auto ctx = inasm64::runtime::Context();
    DWORD64 featuremask;
    if(GetXStateFeaturesMask(const_cast<PCONTEXT>(ctx->OsContext), &featuremask))
    {
        if((featuremask & XSTATE_MASK_AVX) == XSTATE_MASK_AVX)
        {
            DWORD featureLength = 0;
            const auto Ymm = (PM128A)LocateXStateFeature(const_cast<PCONTEXT>(ctx->OsContext), XSTATE_AVX, &featureLength);
            if(Ymm)
            {
                coutreg("ymm0") << Ymm[0].Low << Ymm[0].High << " ";
                coutreg("ymm1") << Ymm[1].Low << Ymm[1].High << " " << std::endl;
                coutreg("ymm2") << Ymm[2].Low << Ymm[2].High << " ";
                coutreg("ymm3") << Ymm[3].Low << Ymm[3].High << " " << std::endl;
                coutreg("ymm4") << Ymm[4].Low << Ymm[4].High << " ";
                coutreg("ymm5") << Ymm[5].Low << Ymm[5].High << " " << std::endl;
                coutreg("ymm6") << Ymm[6].Low << Ymm[6].High << " ";
                coutreg("ymm7") << Ymm[7].Low << Ymm[7].High << " " << std::endl;
                coutreg("ymm8") << Ymm[8].Low << Ymm[8].High << " ";
                coutreg("ymm9") << Ymm[9].Low << Ymm[9].High << " " << std::endl;
                coutreg("ymm10") << Ymm[10].Low << Ymm[10].High << " ";
                coutreg("ymm11") << Ymm[11].Low << Ymm[11].High << " " << std::endl;
                coutreg("ymm12") << Ymm[12].Low << Ymm[12].High << " ";
                coutreg("ymm13") << Ymm[13].Low << Ymm[13].High << " " << std::endl;
                coutreg("ymm14") << Ymm[14].Low << Ymm[14].High << " ";
                coutreg("ymm15") << Ymm[15].Low << Ymm[15].High << std::endl;
            }
        }
        else
        {
            std::cout << "AVX registers not initialised (all zero)\n";
        }
    }
    else
    {
        std::cerr << "AVX not supported on this hardware\n";
    }
}

void DumpInstructionInfo()
{
    const auto ctx = inasm64::runtime::Context();
    for(auto n = 0; n < ctx->InstructionSize; ++n)
    {
        std::cout << std::hex << int(ctx->Instruction[n]) << " ";
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[])
{
    if(argc == 2)
    {
        // when running as debuggee we should never get here
        const auto key = ::strtoll(argv[1], nullptr, 10);
        if(key == inasm64::kTrapModeArgumentValue)
        {
            std::cerr << "Fatal runtime error: faulty trap\n";
            return -1;
        }
    }

    std::cout << "inasm64: The IA 64 Interactive Assembler\n\n";

    if(inasm64::IsSSESupported())
        std::cout << "SSE supported\n";

    DWORD64 featureFlags = GetEnabledXStateFeatures();
    if(featureFlags & (XSTATE_MASK_AVX512 | XSTATE_MASK_AVX))
    {
        if((featureFlags & XSTATE_MASK_AVX) == XSTATE_MASK_AVX)
            std::cout << "AVX ";
        if((featureFlags & XSTATE_MASK_AVX512) == XSTATE_MASK_AVX512)
            std::cout << "AVX512 ";
        std::cout << "supported\n";
    }
    std::cout << std::endl;

    using namespace inasm64;

    if(assembler::Initialise() && runtime::Start())
    {
        std::cout << "started, enter a command or \'h\' for help\\n\n";
        cli::Command cmd = cli::Command::Invalid;

        while(cmd != cli::Command::Quit)
        {
            std::string input;

            if(cli::ActiveMode() == cli::Mode::Assembling)
                std::cout << std::hex << cli::LastAssembledInstructionAddress() << " ";
            else
                std::cout << "> ";
            std::getline(std::cin, input);

            cmd = cli::Execute(input);
            switch(cmd)
            {
            case cli::Command::Step:
                DumpRegs();
                DumpInstructionInfo();
                break;
            case cli::Command::DisplayAllRegs:
                DumpRegs();
                break;
            case cli::Command::DisplayFpRegs:
                DumpFpRegisters();
                break;
            case cli::Command::DisplayAvxRegs:
                DumpAvxRegisters();
                break;
            case cli::Command::Help:
                std::cout << cli::Help() << std::endl;
                break;
            case cli::Command::Invalid:
                std::cerr << "error: " << inasm64::ErrorMessage(inasm64::GetError()) << std::endl;
                break;
            default:
                break;
            }
        }
    }
    else
    {
        std::cerr << "*failed to start inasm64 runtime!\n";
    }

    return 0;
}