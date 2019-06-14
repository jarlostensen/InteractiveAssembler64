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
        };
    }  // namespace assembler
}  // namespace inasm64
