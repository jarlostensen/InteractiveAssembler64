
#include <iostream>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "inasm64/configuration.h"
#include "inasm64/runtime.h"
#include "inasm64/assembler.h"
#include "inasm64/cli.h"

void PrintContext()
{
    const auto ctx = inasm64::runtime::Context();
    std::cout << "rax = 0x" << std::hex << ctx->OsContext->Rax << "\trip = 0x" << std::hex
              << ctx->OsContext->Rip << "\n";
    for(auto n = 0; n < ctx->InstructionSize; ++n)
    {
        std::cout << std::hex << int(ctx->Instruction[n]) << " ";
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[])
{
    std::cout << "inasm64: The IA 64 Interactive Assembler\nby Jarl Ostensen\n";

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