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

//TODO: sort out PCH/Intellisense issues (but some are known bugs in VS)
#include <cstdint>

namespace inasm64
{
    namespace detail
    {
        // used internally to iterate over registers that have changed between calls to Step
        struct changed_registers
        {
            struct iterator
            {
                using value_type = std::pair<RegisterInfo::Register, uint64_t>;
                using reference = value_type&;
                using pointer = const value_type*;
                using difference_type = intptr_t;
                using iterator_category = std::forward_iterator_tag;
                iterator() = default;

                iterator operator++();
                iterator operator++(int);
                reference operator*();
                pointer operator->();
                bool operator==(const iterator& rhs);
                bool operator!=(const iterator& rhs);

                value_type _val;
                size_t _i = 0;
            };

            changed_registers() = default;
            iterator begin() const;
            iterator end() const;
            size_t size() const;
        };
    }  // namespace detail

    ///<summary>
    /// code execution and single-stepping, code injection
    ///</summary>
    namespace runtime
    {
        ///<summary>
        /// start the runtime with the given memory size for assembled instructions
        ///</summary>
        /// This will launch a copy of this process in suspended mode to use as a target for the runtime single stepping debuggger.
        bool Start(size_t scratchPadSize = 8192);
        ///<summary>
        /// terminate the runtime process
        ///</summary>
        void Shutdown();
        ///<summary>
        /// reset all existing code (but does not discard allocated memory)
        ///</summary>
        void Reset();
        ///<summary>
        /// information about a single instruction added for commit
        ///</summary>
        struct instruction_index_t
        {
            // logical line number
            size_t _line = 0;
            // target address
            uintptr_t _address = 0;
        };
        ///<summary>
        /// add an instruction at the next line number
        ///</summary>
        /// returns information about the logical line, and target address, of the instruction
        /// to commit instructions to runtime memory, call CommitInstructions
        instruction_index_t AddInstruction(const void* bytes, size_t size);
        ///<summary>
        /// set the line number for the next AddInstruction
        ///</summary>
        bool SetInstructionLine(size_t line);
        ///<summary>
        /// set the line number for the next Step
        ///</summary>
        bool SetNextExecuteLine(size_t line);
        ///<summary>
        /// commit instructions added so far to runtime memory
        ///</summary>
        bool CommmitInstructions();
        ///<summary>
        /// returns information about the next instruction index, i.e. for the next AddInstruction
        ///</summary>
        instruction_index_t NextInstructionIndex();
        ///<summary>
        /// returns the value of a runtime variable, if exists
        ///<summary>
        bool GetVariable(const char* name, uintptr_t& value);
        ///<summary>
        /// execute one instruction from the current execute address and advance pointers
        ///</summary>
        /// Use Context() to get information about registers, the executed instruction bytes, etc.
        bool Step();
        ///<summary>
        /// iterator over changed registers between last two calls to Step
        ///</summary>
        detail::changed_registers ChangedRegisters();
        ///<summary>
        /// address of next instruction to be executed
        ///</summary>
        ///NOTE: this address is *not* in the memory space of this process, and accessing it will cause an exception
        const void* InstructionPointer();
        ///<summary>
        /// allocates a block of memory in the execution context and returns a handle to it
        ///</summary>
        ///NOTE: use WriteBytes/ReadBytes to access, the handle itself is not usable
        const void* AllocateMemory(size_t);
        ///<summary>
        /// write length bytes from src into the memory location managed by handle
        ///</summary>
        bool WriteBytes(const void* handle, const void* src, size_t length);
        ///<summary>
        /// read length bytes into dest into the memory location managed by handle
        ///</summary>
        bool ReadBytes(const void* handle, void* dest, size_t length);
        ///<summary>
        /// returns the size of the given allocation, or 0 if not found
        ///</summary>
        size_t AllocationSize(const void* handle);
        ///<summary>
        /// set the value of the given register in the runtime context
        ///</summary>
        bool SetReg(const RegisterInfo& reg, const void* data, size_t size);
        ///<summary>
        /// get the value of the given register in the runtime context
        ///</summary>
        bool GetReg(const RegisterInfo& reg, void* data, size_t size);

        template <typename T>
        bool GetReg(const RegisterInfo& reg, T& val)
        {
            return GetReg(reg, &val, sizeof(T));
        }

    }  // namespace runtime
}  // namespace inasm64
