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
    //inasm64::assembler::Assemble(" xacquire lock xor dword ptr [edx+0x11] , ecx ", info);
    assembler::Assemble("add eax , dword ptr fs:[eax + esi * 4 + 0x11223344]", info);
    assembler::Assemble(" rep cmpsq qword ptr [rsi], qword ptr [rdi]", info);
}
