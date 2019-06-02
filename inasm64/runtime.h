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
            const CONTEXT*  OsContext;
            uint8_t Instruction[kMaxAssembledInstructionSize];
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

        ////////////////////////////////////////////////////

        enum class ByteReg
        {
            AL,
            AH,
            BL,
            BH,
            CL,
            CH,
            DL,
            DH
        };
        enum class WordReg
        {
            AX,
            BX,
            CX,
            DX,
            BP,
            SP,
            SI,
            DI
        };

        enum class DWordReg
        {
            EAX,
            EBX,
            ECX,
            EDX,
            EBP,
            ESP,
            ESI,
            EDI
        };

        enum class QWordReg
        {
            RAX,
            RBX,
            RCX,
            RDX,
            RBP,
            RSP,
            RSI,
            RDI,
            R8,
            R9,
            R10,
            R11,
            R12,
            R13,
            R14,
            R15
        };

        ///<summary>
        /// set the value of a register
        ///</summary>
        void SetReg(ByteReg reg, int8_t value);
        ///<summary>
        /// get the value of a register
        ///</summary>
        int8_t GetReg(ByteReg reg);
        ///<summary>
        /// set the value of a register
        ///</summary>
        void SetReg(WordReg reg, int16_t value);
        ///<summary>
        /// get the value of a register
        ///</summary>
        int16_t GetReg(WordReg reg);
        ///<summary>
        /// set the value of a register
        ///</summary>
        void SetReg(DWordReg reg, int32_t value);
        ///<summary>
        /// get the value of a register
        ///</summary>
        int32_t GetReg(DWordReg reg);
        ///<summary>
        /// set the value of a register
        ///</summary>
        void SetReg(QWordReg reg, int64_t value);
        ///<summary>
        /// get the value of a register
        ///</summary>
        int64_t GetReg(QWordReg reg);
    }  // namespace runtime
}  // namespace inasm64
