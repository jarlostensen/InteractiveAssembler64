#pragma once

namespace inasm64
{
    namespace assembler
    {
        struct IAssemblerDriver;

        ///<summary>
        /// information about an assembled instruction
        ///</summary>
        struct AssembledInstructionInfo
        {
            // the instruction bytes
            const uint8_t _instruction[kMaxAssembledInstructionSize] = { 0 };
            // the number of instruction bytes
            const size_t _size = 0;
        };

        ///<summary>
        /// initialise the assembler and the assembler driver
        ///</summary>
        bool Initialise();

        ///<summary>
        /// assemble a given, single line, input statement into IA 64 instruction bytes
        ///</summary>
        bool Assemble(const char* assembly, AssembledInstructionInfo& asm_info);
        ///<summary>
        /// the active driver
        ///</summary>
        IAssemblerDriver* Driver();
    }  // namespace assembler
}  // namespace inasm64
