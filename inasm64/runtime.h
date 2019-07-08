#pragma once

//TODO: sort out PCH/Intellisense issues (but some are known bugs in VS)
#include <cstdint>

namespace inasm64
{
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
        /// add code in the form of valid IA 64 instructions to be executed.
        ///</summary>
        bool AddCode(const void* code, size_t size);
        ///<summary>
        /// execute one instruction from the current execute address and advance pointers
        ///</summary>
        /// Use Context() to get information about registers, the executed instruction bytes, etc.
        bool Step();
        ///<summary>
        /// execution context, valid after each Step()
        ///</summary>
        struct ExecutionContext
        {
            ///<summary>
            /// OS thread context, extension dependant. See GetEnabledXStateFeatures
            ///</summary>
            const CONTEXT* OsContext;
            ///<summary>
            /// instruction bytes of currently executed
            ///</summary>
            uint8_t Instruction[kMaxAssembledInstructionSize];
            ///<summary>
            /// number of bytes in instruction
            ///</summary>
            size_t InstructionSize;
        };
        ///<summary>
        /// get current execution context
        ///</summary>
        const ExecutionContext* Context();
        ///<summary>
        /// address of next instruction to be executed
        ///</summary>
        ///NOTE: this address is *not* in the memory space of this process, and accessing it will cause an exception
        const void* InstructionPointer();
        ///<summary>
        /// address of next instruction that would be written by a call to AddCode
        ///</summary>
        ///NOTE: this address is *not* in the memory space of this process, and accessing it will cause an exception
        const void* InstructionWriteAddress();
        ///<summary>
        /// set the next instruction address. This MUST be based on a valid runtime address obtained by calling InstructionPointer/WriteAddress
        ///</summary>
        bool SetNextInstruction(const void*);
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

        bool SetReg(const RegisterInfo& reg, const void* data, size_t size);
        bool GetReg(const RegisterInfo& reg, void* data, size_t size);

    }  // namespace runtime
}  // namespace inasm64
