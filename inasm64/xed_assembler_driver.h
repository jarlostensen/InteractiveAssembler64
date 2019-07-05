#pragma once
#include "assembler_driver.h"

namespace inasm64
{
    namespace assembler
    {
        struct XedAssemblerDriver : IAssemblerDriver
        {
            XedAssemblerDriver();
            bool Initialise();
            size_t Assemble(const Statement& statement, uint8_t* buffer, const size_t bufferSize) override;
            size_t MaxInstructionSize() override;
            void FindMatchingInstructions(const char* namePrefix, std::vector<const char*>& instructions) override;
        };
    }  // namespace assembler
}  // namespace inasm64
