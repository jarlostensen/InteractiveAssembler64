#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include "../inasm64/common.h"
#include "../inasm64/runtime.h"
#include "../inasm64/assembler.h"
#include "../inasm64/cli.h"

#include <iomanip>
#include <iostream>

void cinsout(const inasm64::assembler::AssembledInstructionInfo& info)
{
    std::cout << "\tinstruction is " << info.InstructionSize << " bytes\n\t";
    for(auto i = 0; i < info.InstructionSize; ++i)
    {
        std::cout << std::setfill('0') << std::setw(2) << std::hex << int(info.Instruction[i]) << " ";
    }
    std::cout << "\n"
              << std::endl;
}

void test_assemble(const std::string& statement)
{
    std::cout << statement << "\n";
    using namespace inasm64;
    assembler::Initialise();
    assembler::AssembledInstructionInfo info;
    if(!assembler::Assemble(statement, info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << std::endl;
    else
        cinsout(info);
}

int main()
{
    //TODO: bespoke tests, this is just to aid development at the moment. Might pull in google test at some point...
    using namespace inasm64;
    assembler::Initialise();
    assembler::AssembledInstructionInfo info;
    test_assemble("add rax, 0x44332211");
    test_assemble("mov eax,[ebx]");
    test_assemble("inc rax");
    test_assemble("add rax,rbx");
    //TODO: support pre-scale format test_assemble("mov edx, dword [esi+4*ebx] ");
    test_assemble("mov edx, dword [esi+ebx*4] ");
    //test_assemble("add eax , dword fs:[eax + esi*2 - 11223344h]");
    test_assemble("add rax, [eax + esi*2 + 11223344h]");
    test_assemble("add eax, dword fs:[ eax + esi * 4 ]");
    test_assemble("add rax, dword fs:[eax + esi ]");
    test_assemble("add eax, dword fs:[eax]");
    test_assemble("add eax, dword [eax]");
    test_assemble("add eax, dword es:[rdx - 0x11223344]");
    test_assemble("jmp qword [0x11223344]");
    test_assemble("mov ax, word [ebx]");
}
