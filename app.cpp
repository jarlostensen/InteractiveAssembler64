
#include <iostream>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "inasm64/common.h"
#include "inasm64/runtime.h"
#include "inasm64/assembler.h"
#include "inasm64/cli.h"

void PrintContext()
{
    const auto ctx = inasm64::runtime::Context();
    std::cout << "rax = 0x" << std::hex << ctx->OsContext->Rax << "\trip = 0x" << std::hex
              << ctx->OsContext->Rip << "\n";

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
        else
        {
            std::cout << "AVX in INIT state\n";
        }
    }

    for(auto n = 0; n < ctx->InstructionSize; ++n)
    {
        std::cout << std::hex << int(ctx->Instruction[n]) << " ";
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[])
{
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
                PrintContext();
                break;
            case inasm64::cli::Command::Help:
                std::cout << inasm64::cli::Help() << std::endl;
            case inasm64::cli::Command::Invalid:
                std::cerr << "Error: " << static_cast<int>(inasm64::GetError()) << std::endl;
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