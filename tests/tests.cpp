#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../inasm64/common.h"
#include "../inasm64/ia64.h"
#include "../inasm64/runtime.h"
#include "../inasm64/assembler.h"
#include "../inasm64/cli.h"
#include "../inasm64/xed_iclass_instruction_set.h"

#include "../external/Ratpack/ratpak.h"

#include <string>
#include <vector>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

void cinsout(const inasm64::assembler::AssembledInstructionInfo& info)
{
    std::cout << "\tinstruction is " << info._size << " bytes\n\t";
    for(auto i = 0; i < info._size; ++i)
    {
        std::cout << std::setfill('0') << std::setw(2) << std::hex << int(info._instruction[i]) << " ";
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
    if(!assembler::Assemble(statement.c_str(), info))
        std::cerr << inasm64::ErrorMessage(inasm64::GetError()) << std::endl;
    else
        cinsout(info);
}

void generate_instruction_set()
{
    std::ifstream instrf{ "../assets/iclass_instruction_set.txt" };
    if(instrf.is_open())
    {
        std::vector<std::string> _lines;
        std::vector<std::pair<char, size_t>> _prefixes;
        std::string line;
        char _prefix = 0;
        while(instrf >> line)
        {
            // minus trailing ,
            auto trimmed = line.substr(0, line.length() - 1);
            std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), ::tolower);
            _lines.emplace_back(trimmed);
            if(_prefix != trimmed[0])
            {
                _prefix = trimmed[0];
                _prefixes.emplace_back(_prefix, _lines.size() - 1);
            }
        }

        std::ofstream oinstrf{
            "../inasm64/xed_iclass_instruction_set.cpp"
        };
        if(oinstrf.is_open())
        {
            auto& os = oinstrf;

            os << "// this file was auto generated from xed_iclass_enum\n";
            os << "namespace inasm64\n{\n";

            auto cc = 1;
            os << "// instruction names, in XED format\n";
            os << "const char* xed_instruction_table[" << _lines.size() << "] = {\n";
            for(const auto& instr : _lines)
            {
                os << "\"" << instr << "\",";
                if(++cc == 4)
                {
                    os << std::endl;
                    cc = 1;
                }
            }
            os << "};\n\n// indices of each prefix character into the instruction table\n";
            os << "const xed_instruction_prefix_t xed_instruction_prefix_table[" << _prefixes.size() << "] = {\n";
            cc = 1;
            for(auto i : _prefixes)
            {
                os << "{'" << i.first << "'," << i.second << "},";
                if(++cc == 16)
                {
                    os << std::endl;
                    cc = 1;
                }
            }
            os << "};\n}\n";
        }
    }
}

void test_xed_instruction_lookup(const char* xed_instruction)
{
    int prefix_start = -1;
    for(auto p = 0; p < inasm64::kXedInstrutionPrefixTableSize; ++p)
    {
        if(xed_instruction[0] == inasm64::xed_instruction_prefix_table[p]._prefix)
        {
            prefix_start = int(inasm64::xed_instruction_prefix_table[p]._index);
            break;
        }
    }
    if(prefix_start >= 0)
    {
        const auto instr_len = strlen(xed_instruction);
        while(xed_instruction[0] == inasm64::xed_instruction_table[prefix_start][0])
        {
            const auto xed_instr = inasm64::xed_instruction_table[prefix_start];
            if(strlen(xed_instr) >= instr_len)
            {
                if(strncmp(xed_instr, xed_instruction, instr_len) == 0)
                {
                    std::cout << xed_instr << "\n";
                }
            }
            ++prefix_start;
        }
    }
}

int main()
{
    //generate_instruction_set();
    test_xed_instruction_lookup("movs");

    auto reg_info = inasm64::GetRegisterInfo("si");
    reg_info = inasm64::GetRegisterInfo("r11");
    reg_info = inasm64::GetRegisterInfo("ecx");
    reg_info = inasm64::GetRegisterInfo("xmm4");
    reg_info = inasm64::GetRegisterInfo("mm3");
    reg_info = inasm64::GetRegisterInfo("spl");
    reg_info = inasm64::GetRegisterInfo("gs");
    reg_info = inasm64::GetRegisterInfo("eflags");

    //TODO: bespoke tests, this is just to aid development at the moment. Might pull in google test at some point...
    using namespace inasm64;
    assembler::Initialise();
    assembler::AssembledInstructionInfo info;

    test_assemble("lea rax,[0x11223344]");
    test_assemble("divpd xmm0,[0x11223344]");
    test_assemble("xor rcx,0x34");
    test_assemble("shl ebx,2");
    test_assemble("sbb rbx,32");
    test_assemble("mov eax,42");
    test_assemble("xor rax,rax");
    test_assemble("mov rbx,1");
    test_assemble("add rax, rbx");
    test_assemble("rep stosb");
    test_assemble("repne scasw");
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
