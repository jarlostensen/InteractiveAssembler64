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
            size_t Assemble(const Statement& statement, uint8_t* buffer) override;
        };
    }  // namespace assembler
}  // namespace inasm64
