//ZZZ: precompiled headers don't work across directory levels, known VS bug
#include <string>
#include <cstdint>

#include "configuration.h"
#include "assembler.h"

namespace inasm64
{
    namespace assembler
    {
        bool Assemble(const std::string& assembly, AssembledInstructionInfo& info)
        {
            //ZZZ: until we've got XED integrated
            static const uint8_t inc_rax[] = { 0x48, 0xFF, 0xC0 };
            memcpy(const_cast<uint8_t*>(info.Instruction), inc_rax, sizeof(inc_rax));
            const_cast<size_t&>(info.InstructionSize) = sizeof(inc_rax);
            const_cast<std::string&>(info.Assembly) = assembly;
            return true;
        }
    }
}  // namespace inasm64
