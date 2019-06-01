#pragma once

#include <cstdint>

namespace inasm64
{
    bool Start(size_t scratchPadSize = 8192);
    void Shutdown();

    bool AddCode(const void* code, size_t size);

    bool Step();

    const CONTEXT* Context();

    const void* InstructionPointer();

    bool SetNextInstruction(const void*);

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

    void SetReg(ByteReg reg, int8_t value);
    int8_t GetReg(ByteReg reg);

    void SetReg(WordReg reg, int16_t value);
    int16_t GetReg(WordReg reg);

    void SetReg(DWordReg reg, int32_t value);
    int32_t GetReg(DWordReg reg);

    void SetReg(QWordReg reg, int64_t value);
    int64_t GetReg(QWordReg reg);
}  // namespace AssemblerExplorer
