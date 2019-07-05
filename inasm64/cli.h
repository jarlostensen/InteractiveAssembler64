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
        const void* NextInstructionAssemblyAddress();

        // The following is a set of callbacks invoked by the CLI in response to commands, such as requesting
        // a register display, or as instructions are assembled etc.
        ///<summary>
        /// Data type used by value set and dump calllbacks
        ///</summary>
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
        // varname has been set to value (in globvars)
        extern std::function<void(const char* varname, uintptr_t value)> OnDataValueSet;

        // registerName has been set to value
        extern std::function<void(const char* registerName, uint64_t value)> OnSetGPRegister;

        // dump information about the given address, in the given format
        extern std::function<void(DataType, const void* address, size_t bytes)> OnDumpMemory;

        // display contents of registerName
        extern std::function<void(const char* registerName)> OnDisplayGPRegister;

        // display all GPRs
        extern std::function<void()> OnDisplayGPRegisters;

        // display all XMMs
        extern std::function<void()> OnDisplayXMMRegisters;

        // display all YMMs
        extern std::function<void()> OnDisplayYMMRegisters;

        // instruction at address has been executed
        extern std::function<void(const void* address)> OnStep;

        // assembly mode begins
        extern std::function<void()> OnStartAssembling;

        // assembly mode ends
        extern std::function<void()> OnStopAssembling;

        // invoked if an error occurs assembling the current statement
        // if this handler returns false assembly mode is aborted
        extern std::function<bool()> OnAssembleError;

        // input has been assembled and converted to instruction bytes, at runtime address
        extern std::function<void(const void* address, const assembler::AssembledInstructionInfo&)> OnAssembling;

        // CLI quit
        extern std::function<void()> OnQuit;

        // format + description
        using help_texts_t = std::vector<std::pair<const char*, const char*>>;

        // display help
        extern std::function<void(const help_texts_t&)> OnHelp;

        // invoked on instruction find with the list of prefix-matching instructions supported by the driver
        extern std::function<void(const std::vector<const char*>&)> OnFindInstruction;

    }  // namespace cli
}  // namespace inasm64
