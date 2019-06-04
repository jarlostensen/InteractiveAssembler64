
#include <iostream>
#include <iomanip>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "inasm64/common.h"
#include "inasm64/runtime.h"
#include "inasm64/assembler.h"
#include "inasm64/cli.h"

void DumpRegs()
{
    const auto ctx = inasm64::runtime::Context();

    const auto coutreg = [](const char* name) -> std::ostream& {
        return std::cout << std::setfill(' ') << std::setw(4) << name << std::setw(1) << " = 0x" << std::setfill('0') << std::setw(16) << std::hex;
    };

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
    DWORD64 featuremask;
    if(GetXStateFeaturesMask(const_cast<PCONTEXT>(ctx->OsContext), &featuremask))
    {
        if((featuremask & XSTATE_MASK_AVX) == XSTATE_MASK_AVX)
        {
            DWORD featureLength = 0;
            const auto Ymm = (PM128A)LocateXStateFeature(const_cast<PCONTEXT>(ctx->OsContext), XSTATE_AVX, &featureLength);
            if(Ymm)
                for(auto i = 0; i < featureLength / sizeof(Ymm[0]); i++)
                {
                    std::cout << "Ymm" << std::dec << i << ": " << std::hex << Ymm[i].Low << " " << Ymm[i].High << "\n";
                }
        }
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

    std::cout << "inasm64: The IA 64 Interactive Assembler\n";
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

    if(inasm64::runtime::Start())
    {
        std::cout << "started, enter a command or \'h\' for help\\n\n";
        inasm64::cli::Command cmd = inasm64::cli::Command::Invalid;

        while(cmd != inasm64::cli::Command::Quit)
        {
            std::string input;

            if(inasm64::cli::ActiveMode() == inasm64::cli::Mode::Assembling)
                std::cout << std::hex << inasm64::cli::LastAssembledInstructionInfo()->Address << " ";
            else
                std::cout << "> ";
            std::getline(std::cin, input);

            cmd = inasm64::cli::Execute(input);
            switch(cmd)
            {
            case inasm64::cli::Command::Step:
                DumpRegs();
                DumpInstructionInfo();
                break;
            case inasm64::cli::Command::DisplayAllRegs:
                DumpRegs();
                break;
            case inasm64::cli::Command::Help:
                std::cout << inasm64::cli::Help() << std::endl;
                break;
            case inasm64::cli::Command::Invalid:
                std::cerr << "Error: " << static_cast<int>(inasm64::GetError()) << std::endl;
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