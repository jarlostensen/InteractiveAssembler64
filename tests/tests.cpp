#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../inasm64/common.h"
#include "../inasm64/runtime.h"
#include "../inasm64/assembler.h"
#include "../inasm64/cli.h"

#include <iostream>

int main()
{
    //TODO: bespoke tests, this is just to aid development at the moment. Might pull in google test at some point...
    using namespace inasm64;
    assembler::Initialise();
    assembler::AssembledInstructionInfo info;
    if(!assembler::Assemble("add rax, 0x44332211", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("push qword [eax]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("inc rax", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("add rax,rbx", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("add eax , dword fs:[eax + esi*2 - 11223344h]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("add eax , dword fs:[ eax + esi * 4 ]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("add rax , dword fs:[eax + esi ]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("add eax , dword fs:[eax]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("add eax , dword [eax]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("add eax, dword es:[rdx - 0x11223344]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("jmp dword fs:[0x11223344]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
    if(!assembler::Assemble("mov ax, word [ebx]", info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << "\n";
}
