#pragma once

namespace inasm64
{
    namespace assembler
    {
        ///<summary>
        /// information about an assembled instruction
        ///</summary>
        struct AssembledInstructionInfo
        {
            // the source assembly statement
            const std::string Assembly = {};
            // the instruction bytes
            const uint8_t Instruction[kMaxAssembledInstructionSize] = { 0 };
            // the number of instruction bytes
            const size_t InstructionSize = 0;
        };

        bool Initialise();

        ///<summary>
        /// assemble a given, single line, input statement into IA 64 instruction bytes
        ///</summary>
        bool Assemble(const std::string& assembly, AssembledInstructionInfo& asm_info);
    }  // namespace assembler
}  // namespace inasm64
