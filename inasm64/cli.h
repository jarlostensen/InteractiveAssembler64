// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
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

        // The following is a set of callbacks invoked by the CLI in response to commands, such as requesting
        // a register display, or as instructions are assembled etc.
        ///<summary>
        /// Data type used by value set and dump calllbacks
        ///</summary>
        enum class DataType
        {
            kUnknown = 0,
            kByte = 1,
            kWord = 2,
            kDWord = 4,
            kQWord = 8,
            kFloat32,
            kFloat64,
            kXmmWord,
            kYmmWord,
            kZmmWord
        };
        // helper for the DataType enum
        inline DataType BitWidthToIntegerDataType(short bit_width)
        {
            switch(bit_width)
            {
            case 8:
                return DataType::kByte;
            case 16:
                return DataType::kWord;
            case 32:
                return DataType::kDWord;
            case 64:
                return DataType::kQWord;
            case 128:
                return DataType::kXmmWord;
            case 256:
                return DataType::kYmmWord;
            case 512:
                return DataType::kYmmWord;
            default:
                return DataType::kUnknown;
            }
        }
        // helper for the DataType enum
        inline unsigned DataTypeToBitWidth(DataType type)
        {
            switch(type)
            {
            case DataType::kByte:
                return 8;
            case DataType::kWord:
                return 16;
            case DataType::kDWord:
            case DataType::kFloat32:
                return 32;
            case DataType::kQWord:
            case DataType::kFloat64:
                return 64;
            case DataType::kXmmWord:
                return 128;
            case DataType::kYmmWord:
                return 256;
            case DataType::kZmmWord:
                return 512;
            default:;
            }
            return 0;
        }

        // varname has been set to value (in globvars)
        extern std::function<void(const char* varname, uintptr_t value)> OnDataValueSet;

        // registerName has been set to value
        extern std::function<void(const char* registerName, uint64_t value)> OnSetGPRegister;

        // dump information about the given address, in the given format
        extern std::function<void(DataType, const void* address, size_t bytes)> OnDisplayData;

        // display contents of register in the given format (type)
        extern std::function<void(DataType, const RegisterInfo&)> OnDisplayRegister;

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
        extern std::function<void(const runtime::instruction_index_t&, const assembler::AssembledInstructionInfo&)> OnAssembling;

        // CLI quit
        extern std::function<void()> OnQuit;

        // format + description
        using help_texts_t = std::vector<std::pair<const char*, const char*>>;

        // display help
        extern std::function<void(const help_texts_t&)> OnHelp;

        // invoked on instruction find with the list of prefix-matching instructions supported by the driver
        extern std::function<void(const std::vector<const char*>&)> OnFindInstruction;

        extern std::function<bool(const char*)> OnUnknownCommand;

    }  // namespace cli
}  // namespace inasm64
