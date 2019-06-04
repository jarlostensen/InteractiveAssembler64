#pragma once

namespace inasm64
{
    ///<summary>
    /// parsing and execution of inasm64 commands and management of the assembler and runtime.
    ///</summary>
    namespace cli
    {
        ///<summary>
        /// the last command
        ///</summary>
        enum class Command
        {
            Invalid,
            Quit,
            Step,
            Assemble,
            DisplayAllRegs,
            DisplayFpRegs,
            DisplayAvxRegs,
            SetReg,
            Help,
        };
        ///<summary>
        /// command mode; processing ordinary commands or assembling
        ///</summary>
        enum class Mode
        {
            Processing,
            Assembling,
        };
        ///<summary>
        /// the active command mode
        ///</summary>
        Mode ActiveMode();
        ///<summary>
        /// execute a command line (if valid).
        ///</summary>
        Command Execute(std::string& commandLine);
        ///<summary>
        /// extension of the assembler::AssembledInstructionInfo structure to add the destination address of the assembled instruction
        ///</summary>
        struct AssembledInstructionInfo
            : assembler::AssembledInstructionInfo
        {
            const void* Address = nullptr;
        };
        ///<summary>
        /// if currently in Assemble mode; returns information about the last instruction assembled.
        ///</summary>
        const AssembledInstructionInfo* LastAssembledInstructionInfo();
        ///<summary>
        /// exactly that; a list of available command and their use
        ///</summary>
        std::string Help();
    }  // namespace cli
}  // namespace inasm64
