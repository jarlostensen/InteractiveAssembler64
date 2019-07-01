#pragma once

namespace inasm64
{
    constexpr size_t kMaxCommandLineLength = 256;

    ///<summary>
    /// parsing and execution of inasm64 commands and management of the assembler and runtime.
    ///</summary>
    namespace cli
    {
        ///<summary>
        /// command mode; processing ordinary commands or assembling
        ///</summary>
        enum class Mode
        {
            Processing,
            Assembling,
        };
        ///<summary>
        /// initialise the CLI
        ///</summary>
        bool Initialise();
        ///<summary>
        /// the active command mode
        ///</summary>
        Mode ActiveMode();
        ///<summary>
        /// execute a command line (if valid).
        ///</summary>
        bool Execute(const char* commandLine);
        ///<summary>
        /// if currently in Assemble mode; returns information about the last instruction assembled.
        ///</summary>
        const void* LastAssembledInstructionAddress();
        ///<summary>
        /// exactly that; a list of available command and their use
        ///</summary>
        std::string Help();

        enum class DataType
        {
            kUnknown,
            kByte,
            kWord,
            kDWord,
            kQWord,
            kFloat32,
            kFloat64,
        };

        extern std::function<void(const char*, uintptr_t)> OnDataValueSet;
        extern std::function<void(const char*, uint64_t)> OnSetGPRegisters;
        extern std::function<void(DataType, const void*, size_t)> OnDumpMemory;
        extern std::function<void(const char*)> OnDisplayGPRegister;
        extern std::function<void()> OnDisplayGPRegisters;
        extern std::function<void()> OnDisplayXMMRegisters;
        extern std::function<void()> OnDisplayYMMRegisters;
        extern std::function<void()> OnStep;
        ///<summary>
        /// invoked when assembly mode is invoked
        ///</summary>
        extern std::function<void()> OnStartAssembling;
        ///<summary>
        /// invoked when leaving assembly mode
        ///</summary>
        extern std::function<void()> OnStopAssembling;
        ///<summary>
        /// invoked when a line of assembly input has been succesfully converted to instruction bytes and uploaded to the runtime
        ///<summary>
        extern std::function<void(const assembler::AssembledInstructionInfo&)> OnAssembling;
        extern std::function<void()> OnQuit;
        extern std::function<void()> OnHelp;

    }  // namespace cli
}  // namespace inasm64
