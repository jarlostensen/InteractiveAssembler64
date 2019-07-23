// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <cassert>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "console.h"

#include "inasm64/common.h"
#include "inasm64/ia64.h"
#include "inasm64/runtime.h"
#include "inasm64/assembler.h"
#include "inasm64/cli.h"
#include "inasm64/globvars.h"

// ====================================================================================

using namespace inasm64;
//

// each line of assembled input, against its address.
std::unordered_map<uintptr_t, std::string> _asm_history;

// move cursor to the midle of the console window
void to_right_column()
{
    const auto cw = console::Width();
    console::SetCursorX(cw - cw / 2);
}

auto cout_64_bits(std::ostream& os) -> std::ostream&
{
    return os << std::setfill('0') << std::setw(16) << std::hex;
};

auto cout_bytes_as_number(std::ostream& os, const uint8_t* bytes, size_t count) -> std::ostream&
{
    os << "0x" << std::hex;
    for(auto c = count; c > 0; --c)
    {
        //NOTE: reverse, since we're little endian...
        os << std::setw(2) << std::setfill('0') << int((bytes[c - 1]) & 0xff);
    }
    return os;
};

auto coutreg(const char* reg) -> std::ostream&
{
    // perhaps I should just have used printf....
    return std::cout << std::setfill(' ') << std::setw(4) << reg << std::setw(1) << " = 0x" << cout_64_bits;
}

auto coutreg(const RegisterInfo& reg_info) -> std::ostream&
{
    // perhaps I should just have used printf....
    return std::cout << std::setfill(' ') << std::setw(4) << reg_info._name << std::setw(1) << " = 0x" << std::setfill('0') << std::setw(reg_info._bit_width / 8) << std::hex;
}

std::ostream& coutflags(DWORD flags, DWORD prev = 0)
{
    auto changed = false;
    // https://en.wikipedia.org/wiki/FLAGS_register
#define INASM_IF_EFLAG_DIFF(bitmask, name)    \
    if((flags & bitmask) != (prev & bitmask)) \
    {                                         \
        std::cout << name;                    \
        changed = true;                       \
    }

    INASM_IF_EFLAG_DIFF(0x01, "CF ");
    INASM_IF_EFLAG_DIFF(0x04, "PF ");
    INASM_IF_EFLAG_DIFF(0x10, "AF ");
    INASM_IF_EFLAG_DIFF(0x40, "ZF ");
    INASM_IF_EFLAG_DIFF(0x80, "SF ");
    INASM_IF_EFLAG_DIFF(0x100, "TF ");
    INASM_IF_EFLAG_DIFF(0x200, "IF ");
    INASM_IF_EFLAG_DIFF(0x400, "DF ");
    INASM_IF_EFLAG_DIFF(0x800, "OF ");
    //INASM_IF_EFLAG_DIFF(0x10000, "RF ");
    //INASM_IF_EFLAG_DIFF(0x20000, "VM ");

    if(changed)
        std::cout << "\n";

    return std::cout;
}

const RegisterInfo kRegisterInfos[] = {
    RegisterInfo{ RegisterInfo::Register::rax },
    RegisterInfo{ RegisterInfo::Register::rbx },
    RegisterInfo{ RegisterInfo::Register::rcx },
    RegisterInfo{ RegisterInfo::Register::rdx },
    RegisterInfo{ RegisterInfo::Register::rsi },
    RegisterInfo{ RegisterInfo::Register::rdi },
    RegisterInfo{ RegisterInfo::Register::rbp },
    RegisterInfo{ RegisterInfo::Register::rsp },
    RegisterInfo{ RegisterInfo::Register::r8 },
    RegisterInfo{ RegisterInfo::Register::r9 },
    RegisterInfo{ RegisterInfo::Register::r10 },
    RegisterInfo{ RegisterInfo::Register::r11 },
    RegisterInfo{ RegisterInfo::Register::r12 },
    RegisterInfo{ RegisterInfo::Register::r13 },
    RegisterInfo{ RegisterInfo::Register::r14 },
    RegisterInfo{ RegisterInfo::Register::r15 },
    RegisterInfo{ RegisterInfo::Register::eflags },
};

void DumpRegs()
{
    std::cout << "\n";
    using namespace runtime;
    for(auto idx = 0; idx < std::size(kRegisterInfos); ++idx)
    {
        uint64_t val;
        GetReg(kRegisterInfos[idx], val);
        if(kRegisterInfos[idx]._class != RegisterInfo::RegClass::kFlags)
            coutreg(kRegisterInfos[idx]._name) << val << " ";
        else
            coutflags(DWORD(val), 0);
        if(idx && (idx & 3) == 3)
            std::cout << std::endl;
    }
}

void DumpDeltaRegs()
{
    const auto changed_regs = runtime::ChangedRegisters();
    if(changed_regs.size())
        std::cout << "\n";
    auto idx = 0;
    for(const auto& changed : changed_regs)
    {
        RegisterInfo reg_info{ changed.first };
        if(reg_info._class == RegisterInfo::RegClass::kXmm)
        {
            uint8_t val[16];
            runtime::GetReg(reg_info, &val, sizeof(val));
            std::cout << reg_info._name << " ";
            cout_bytes_as_number(std::cout, val, sizeof(val));
        }
        else
        {
            uint64_t val;
            runtime::GetReg(reg_info, val);
            if(reg_info._class != RegisterInfo::RegClass::kFlags)
                coutreg(reg_info) << val << " ";
            else
            {
                std::cout << "\n";
                coutflags(DWORD(val), 0);
            }
        }
        if(idx && (idx & 3) == 3)
            std::cout << std::endl;
        ++idx;
    }
}

void DumpReg(const char* regName_, uint64_t value)
{
    char regName[64];
    const auto regNameLen = strlen(regName_);
    assert(regNameLen < std::size(regName));
    memcpy(regName, regName_, regNameLen + 1);
    _strlwr_s(regName, regNameLen + 1);
    std::cout << console::green << "\n"
              << regName << " = 0x" << std::hex << value << std::endl
              << console::reset_colours;
}

// used by the macros below to generate register names
#define STRINGIZE(A) #A

void DumpXmmRegisters()
{
    std::cout << "\n";
    using namespace inasm64;
    for(auto i = 0; i < 16; ++i)
    {
        RegisterInfo reg_info{
            RegisterInfo::RegClass::kXmm,
            static_cast<RegisterInfo::Register>(static_cast<int>(RegisterInfo::Register::xmm0) + i),
            128
        };
        uint8_t val[16];
        runtime::GetReg(reg_info, val, sizeof(val));
        std::cout << "xmm" << std::dec << i << " ";
        if(i < 10)
            std::cout << " ";
        cout_bytes_as_number(std::cout, val, sizeof(val)) << " ";
        if(i & 1)
            std::cout << std::endl;
    }
}

void DumpYmmRegisters()
{
    //TODO:
    /*assert(false);
    const auto ctx = runtime::Context();
    DWORD64 featuremask;
    if(GetXStateFeaturesMask(const_cast<PCONTEXT>(ctx->OsContext), &featuremask))
    {
        if((featuremask & XSTATE_MASK_AVX) == XSTATE_MASK_AVX)
        {
            DWORD featureLength = 0;
            const auto Ymm = (PM128A)LocateXStateFeature(const_cast<PCONTEXT>(ctx->OsContext), XSTATE_AVX, &featureLength);
            if(Ymm)
            {
#define DUMP_YMM_REGISTER(index) coutreg(STRINGIZE(ymm##index)) << Ymm[index].High << cout_64_bits << Ymm[index].Low << " "
                DUMP_YMM_REGISTER(0);
                DUMP_YMM_REGISTER(1) << std::endl;
                DUMP_YMM_REGISTER(2);
                DUMP_YMM_REGISTER(3) << std::endl;
                DUMP_YMM_REGISTER(4);
                DUMP_YMM_REGISTER(5) << std::endl;
                DUMP_YMM_REGISTER(6);
                DUMP_YMM_REGISTER(7) << std::endl;
                DUMP_YMM_REGISTER(8);
                DUMP_YMM_REGISTER(9) << std::endl;
                DUMP_YMM_REGISTER(10);
                DUMP_YMM_REGISTER(11) << std::endl;
                DUMP_YMM_REGISTER(12);
                DUMP_YMM_REGISTER(13) << std::endl;
                DUMP_YMM_REGISTER(14);
                DUMP_YMM_REGISTER(15) << std::endl;
            }
        }
        else
        {
            std::cout << "AVX registers not initialised (all zero)\n";
        }
    }
    else
    {
        std::cerr << "AVX not supported on this hardware\n";
    }*/
}

void DisplayMemoryAsType(cli::DataType type, const char* memory, size_t size)
{
    using namespace cli;
    for(unsigned c = 0; c < size;)
    {
        const auto rp = memory + c;
        switch(type)
        {
        case DataType::kByte:
        {
            const unsigned val = *rp & 0xff;
            std::cout << std::hex << std::setw(2) << std::setfill('0') << val;
            ++c;
        }
        break;
        case DataType::kWord:
        {
            const auto val = *reinterpret_cast<const unsigned short*>(rp) & 0xffff;
            std::cout << std::hex << std::setw(4) << std::setfill('0') << val;
            c += 2;
        }
        break;
        case DataType::kDWord:
        {
            const auto val = *reinterpret_cast<const uint32_t*>(rp);
            std::cout << std::hex << std::setw(8) << std::setfill('0') << val;
            c += 4;
        }
        break;
        case DataType::kQWord:
        {
            const auto val = *reinterpret_cast<const uint64_t*>(rp);
            std::cout << std::hex << std::setw(16) << std::setfill('0') << val;
            c += 8;
        }
        break;
        case DataType::kFloat32:
        {
            const auto val = *reinterpret_cast<const float*>(rp);
            std::cout << std::setprecision(7) << val << "f";
            c += 4;
        }
        break;
        case DataType::kFloat64:
        {
            const auto val = *reinterpret_cast<const double*>(rp);
            std::cout << std::setprecision(7) << val;
            c += 8;
        }
        break;
        }
        if(c < size)
            std::cout << ", ";
    }
}

void DumpMemory(cli::DataType type, const void* remote_address, size_t size_)
{
    //NOTE: all commands are being invoked on the same line as the input, a bit cumbersome but...
    std::cout << "\n";

    using namespace cli;
    constexpr auto kBytesPerRow = 16;
    constexpr auto kRows = 4;

    // we don't display all the data (yet), so pick smallest of window
    auto size = std::min<size_t>(kBytesPerRow * kRows, size_);
    // create padding to nearest 8 bytes, so that we can 0 out top bytes in >1 byte types if they go outside the range
    const auto buffer_size = (size + 8) & ~7;
    const auto local_buffer = new char[buffer_size];
    // clear the buffer
    memset(local_buffer, 0, buffer_size);
    runtime::ReadBytes(remote_address, local_buffer, size);
    auto n = 0;
    auto address = reinterpret_cast<const uint8_t*>(remote_address);

    while(size)
    {
        std::cout << std::hex << uintptr_t(address) << " ";
        std::cout << console::yellow_lo;
        const auto bytes_to_read = std::min<size_t>(kBytesPerRow, size);
        DisplayMemoryAsType(type, local_buffer + n, bytes_to_read);
        std::cout << console::reset_colours;
        const auto cw = console::Width();
        console::SetCursorX(cw - cw / 2);
        for(unsigned c = 0; c < bytes_to_read; ++c)
        {
            const auto ic = local_buffer[n++];
            if(ic >= 0x21 && ic <= 0x7e)
                std::cout << ic;
            else
                std::cout << ".";
        }
        std::cout << std::endl;
        size -= bytes_to_read;
        address += kBytesPerRow;
    }
}

void DisplayRegister(cli::DataType type, const RegisterInfo& reg_info)
{
    using namespace inasm64;
    std::cout << "\n\t" << reg_info._name << ": ";
    switch(reg_info._class)
    {
    case RegisterInfo::RegClass::kSegment:
    {
        uint16_t val;
        runtime::GetReg(reg_info, &val, sizeof(val));
        std::cout << std::hex << std::setw(4) << std::setfill('0') << val << ", ";
    }
    break;
    case RegisterInfo::RegClass::kFlags:
    {
        uint32_t val;
        runtime::GetReg(reg_info, &val, sizeof(val));
        coutflags(val) << std::endl;
    }
    break;
    case RegisterInfo::RegClass::kGpr:
    {
        uint8_t val[8];
        runtime::GetReg(reg_info, val, sizeof(val));
        if(type == cli::DataType::kDWord)
        {
            cout_bytes_as_number(std::cout, val, reg_info._bit_width / 8);
        }
        else
        {
            DisplayMemoryAsType(type, reinterpret_cast<const char*>(val), reg_info._bit_width / 8);
        }
    }
    break;
    case RegisterInfo::RegClass::kXmm:
    {
        uint8_t val[16];
        runtime::GetReg(reg_info, val, sizeof(val));
        if(type == cli::DataType::kXmmWord)
        {
            cout_bytes_as_number(std::cout, val, sizeof(val));
        }
        else
        {
            DisplayMemoryAsType(type, reinterpret_cast<const char*>(val), sizeof(val));
        }
    }
    break;
    case RegisterInfo::RegClass::kYmm:
    {
        std::cout << "TODO: YMM";
    }
    break;
    case RegisterInfo::RegClass::kZmm:
    {
        std::cout << "TODO: ZMM";
    }
    break;
    default:;
    }
}

void DisplaySystemInformation()
{
    auto supported = false;
    if(SseLevelSupported(SseLevel::kSse))
    {
        std::cout << "SSE ";
        supported = true;
    }
    if(SseLevelSupported(SseLevel::kSse2))
    {
        std::cout << "SSE2 ";
        supported = true;
    }
    if(SseLevelSupported(SseLevel::kSse3))
    {
        std::cout << "SSE3 ";
        supported = true;
    }
    if(SseLevelSupported(SseLevel::kSsse3))
    {
        std::cout << "SSSE3 ";
        supported = true;
    }
    if(SseLevelSupported(SseLevel::kSse4_1))
    {
        std::cout << "SSE4_1 ";
        supported = true;
    }
    if(SseLevelSupported(SseLevel::kSse4_2))
    {
        std::cout << "SSE4_2 ";
        supported = true;
    }
    if(SseLevelSupported(SseLevel::kSse4a))
    {
        std::cout << "SSE4a ";
        supported = true;
    }
    if(SseLevelSupported(SseLevel::kSse5))
    {
        std::cout << "SSE5 ";
        supported = true;
    }
    if(ExtendedCpuFeatureSupported(ExtendedCpuFeature::kAes))
    {
        std::cout << "AES ";
        supported = true;
    }
    if(supported)
        std::cout << "supported\n";

    DWORD64 featureFlags = GetEnabledXStateFeatures();
    if(featureFlags & (XSTATE_MASK_AVX512 | XSTATE_MASK_AVX))
    {
        if((featureFlags & XSTATE_MASK_AVX) == XSTATE_MASK_AVX)
        {
            std::cout << "AVX ";
            if(ExtendedCpuFeatureSupported(ExtendedCpuFeature::kAvx2))
                std::cout << "AVX2 ";
        }
        if((featureFlags & XSTATE_MASK_AVX512) == XSTATE_MASK_AVX512)
            std::cout << "AVX512 ";
        std::cout << "supported\n";
    }

    std::cout << std::endl;
}

int main(int argc, char* argv[])
{
    if(argc == 2)
    {
        // when running as debuggee we should never get here
        const auto key = ::strtoll(argv[1], nullptr, 10);
        if(key == kTrapModeArgumentValue)
        {
            std::cerr << console::red << "Fatal runtime error: faulty trap\n";
            return -1;
        }
    }

    console::Initialise();

    std::cout << console::yellow << "inasm64: The IA 64 Interactive Assembler\n\n"
              << console::reset_colours;
    using namespace inasm64;
    DisplaySystemInformation();

    if(assembler::Initialise() && runtime::Start() && cli::Initialise())
    {
        cli::OnDataValueSet = [](const char* name, uintptr_t value) {
            std::cout << "\t$" << name << " is set to 0x" << std::hex << value << std::endl;
        };

        std::cout << console::green << "started, enter a command or \'h\' for help\n\n"
                  << console::reset_colours;

        auto done = false;
        cli::OnQuit = [&done]() { done = true; };
        cli::OnHelp = [](const cli::help_texts_t& help_texts) {
            std::cout << "\n";
            const auto cw = console::Width();
            for(const auto& help : help_texts)
            {
                std::cout << help.first;
                console::SetCursorX(cw - cw / 2);
                std::cout << help.second << std::endl;
            }
        };
        cli::OnStep = [](const void* /*address*/) {
            std::cout << "\n";
            DumpDeltaRegs();
        };
        cli::OnDisplayGPRegisters = DumpRegs;
        cli::OnDisplayXMMRegisters = DumpXmmRegisters;
        cli::OnDisplayYMMRegisters = DumpYmmRegisters;
        cli::OnDisplayRegister = DisplayRegister;
        cli::OnDisplayData = DumpMemory;
        cli::OnSetGPRegister = DumpReg;

        std::string input;
        short input_start_cursor_x;
        auto clear_next_input_on_key = false;

        auto assembling = false;
        cli::OnStartAssembling = [&assembling]() {
            assembling = true;
        };
        cli::OnAssembling = [&input, &input_start_cursor_x](const runtime::instruction_index_t& index, const assembler::AssembledInstructionInfo& asm_info) {
            _asm_history[index._address] = input;
            // yeah....
            std::remove_const_t<decltype(std::string::npos)> epos = 0;
            // first (or only) word on the input line is the instruction name, highlight it
            while(input[epos] != ' ' && epos < input.length())
                ++epos;
            console::SetCursorX(input_start_cursor_x);
            std::cout << console::green << input.substr(0, epos);

            to_right_column();
            for(unsigned n = 0; n < asm_info._size; ++n)
            {
                std::cout << console::green << std::hex << std::setw(2) << std::setfill('0') << int(asm_info._instruction[n]);
            }
            std::cout << console::reset_colours;
        };
        cli::OnAssembleError = [&input, &input_start_cursor_x, &clear_next_input_on_key]() -> bool {
            to_right_column();
            std::cerr << console::red << "\t" << ErrorMessage(GetError()) << console::reset_colours;
            console::SetCursorX(0);
            clear_next_input_on_key = true;
            return true;
        };
        cli::OnStopAssembling = []() {
            std::cout << std::endl;
        };

        cli::OnFindInstruction = [](const std::vector<const char*>& instructions) {
            std::cout << "\nthere are " << instructions.size() << " instruction matches:\n";
            int n = 1;
            for(const auto instr : instructions)
            {
                std::cout << "\t" << instr;
                if(n < instructions.size())
                    std::cout << ",\n";
                ++n;
            }
            std::cout << std::endl;
        };

        while(!done)
        {
            if(cli::ActiveMode() == cli::Mode::Assembling)
            {
                const auto index = runtime::NextInstructionIndex();
                std::cout << std::hex << index._address << ":@" << index._line << " ";
            }
            else
            {
                assembling = false;
                std::cout << "> ";
            }
            input_start_cursor_x = console::GetCursorX();
            console::ReadLine(input, clear_next_input_on_key);

            if(!cli::Execute(input.c_str()))
            {
                if(cli::ActiveMode() != cli::Mode::Assembling)
                {
                    to_right_column();
                    std::cerr << console::red << ErrorMessage(GetError()) << console::reset_colours;
                    console::SetCursorX(0);
                }
                clear_next_input_on_key = true;
            }
            else
            {
                clear_next_input_on_key = false;
                std::cout << std::endl;
            }
        }
    }
    else
    {
        std::cerr << console::red << "*failed to start inasm64 runtime!\n";
    }

    std::cout << console::reset_colours;

    return 0;
}
