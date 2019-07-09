
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
    return std::cout << std::setfill(' ') << std::setw(4) << reg << std::setw(1) << " = 0x" << cout_64_bits << std::hex;
}

std::ostream& coutflags(DWORD flags, DWORD prev = 0)
{
    // https://en.wikipedia.org/wiki/FLAGS_register
#define INASM_IF_EFLAG_DIFF(bitmask, name)    \
    if((flags & bitmask) != (prev & bitmask)) \
    std::cout << name

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
    return std::cout;
}

void DumpRegs()
{
    const auto ctx = inasm64::runtime::Context();

    coutreg("rax") << ctx->OsContext->Rax << " ";
    coutreg("rbx") << ctx->OsContext->Rbx << " ";
    coutreg("rcx") << ctx->OsContext->Rcx << " ";
    coutreg("rdx") << ctx->OsContext->Rdx << std::endl;

    coutreg("rsi") << ctx->OsContext->Rsi << " ";
    coutreg("rdi") << ctx->OsContext->Rdi << " ";
    coutreg("rbp") << ctx->OsContext->Rbp << " ";
    coutreg("rsp") << ctx->OsContext->Rsp << std::endl;

    coutreg("r8") << ctx->OsContext->R8 << " ";
    coutreg("r9") << ctx->OsContext->R9 << " ";
    coutreg("r10") << ctx->OsContext->R10 << " ";
    coutreg("r11") << ctx->OsContext->R11 << std::endl;
    coutreg("r12") << ctx->OsContext->R12 << " ";
    coutreg("r13") << ctx->OsContext->R13 << " ";
    coutreg("r14") << ctx->OsContext->R14 << " ";
    coutreg("r15") << ctx->OsContext->R15 << std::endl;

    std::cout << "\n   ";
    coutflags(ctx->OsContext->EFlags) << std::endl;
}

void DumpDeltaRegs()
{
    static CONTEXT _prev_context = { 0 };
    static bool _first = true;
    const auto ctx = inasm64::runtime::Context();

    std::cout << "\n";

    if(_first)
    {
        DumpRegs();
        _first = false;
    }
    else
    {
        std::cout << console::green_lo;

        auto newline = false;
        if(ctx->OsContext->Rax != _prev_context.Rax)
        {
            coutreg("rax") << ctx->OsContext->Rax << " ";
            newline = true;
        }
        if(ctx->OsContext->Rbx != _prev_context.Rbx)
        {
            coutreg("rbx") << ctx->OsContext->Rbx << " ";
            newline = true;
        }
        if(ctx->OsContext->Rcx != _prev_context.Rcx)
        {
            coutreg("rcx") << ctx->OsContext->Rcx << " ";
            newline = true;
        }
        if(ctx->OsContext->Rdx != _prev_context.Rdx)
        {
            coutreg("rdx") << ctx->OsContext->Rdx;
            newline = true;
        }

        if(newline)
        {
            std::cout << std::endl;
            newline = false;
        }

        if(ctx->OsContext->Rsi != _prev_context.Rsi)
        {
            coutreg("rsi") << ctx->OsContext->Rsi << " ";
            newline = true;
        }
        if(ctx->OsContext->Rdi != _prev_context.Rdi)
        {
            coutreg("rdi") << ctx->OsContext->Rdi << " ";
            newline = true;
        }
        if(ctx->OsContext->Rbp != _prev_context.Rbp)
        {
            coutreg("rbp") << ctx->OsContext->Rbp << " ";
            newline = true;
        }
        if(ctx->OsContext->Rsp != _prev_context.Rsp)
        {
            coutreg("rsp") << ctx->OsContext->Rsp;
            newline = true;
        }

        if(newline)
        {
            std::cout << std::endl;
            newline = false;
        }

        if(ctx->OsContext->R8 != _prev_context.R8)
        {
            coutreg("r8") << ctx->OsContext->R8 << " ";
            newline = true;
        }
        if(ctx->OsContext->R9 != _prev_context.R9)
        {
            coutreg("r9") << ctx->OsContext->R9 << " ";
            newline = true;
        }
        if(ctx->OsContext->R10 != _prev_context.R10)
        {
            coutreg("r10") << ctx->OsContext->R10 << " ";
            newline = true;
        }
        if(ctx->OsContext->R11 != _prev_context.R11)
        {
            coutreg("r11") << ctx->OsContext->R11;
            newline = true;
        }

        if(newline)
        {
            std::cout << std::endl;
            newline = false;
        }

        if(ctx->OsContext->R12 != _prev_context.R12)
        {
            coutreg("r12") << ctx->OsContext->R12 << " ";
            newline = true;
        }
        if(ctx->OsContext->R13 != _prev_context.R13)
        {
            coutreg("r13") << ctx->OsContext->R13 << " ";
            newline = true;
        }
        if(ctx->OsContext->R14 != _prev_context.R14)
        {
            coutreg("r14") << ctx->OsContext->R14 << " ";
            newline = true;
        }
        if(ctx->OsContext->R15 != _prev_context.R15)
        {
            coutreg("r15") << ctx->OsContext->R15;
            newline = true;
        }

        if(newline)
            std::cout << std::endl;
        std::cout << "   ";
        coutflags(ctx->OsContext->EFlags, _prev_context.EFlags) << std::endl;

        std::cout << console::reset_colours;
    }
    memcpy(&_prev_context, ctx->OsContext, sizeof(_prev_context));
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
#define DUMP_XMM_REGISTER(index) coutreg(STRINGIZE(xmm##index)) << ctx->OsContext->Xmm##index.High << cout_64_bits << ctx->OsContext->Xmm##index.Low << " "
    const auto ctx = inasm64::runtime::Context();
    DUMP_XMM_REGISTER(0);
    DUMP_XMM_REGISTER(1) << std::endl;
    DUMP_XMM_REGISTER(2);
    DUMP_XMM_REGISTER(3) << std::endl;
    DUMP_XMM_REGISTER(4);
    DUMP_XMM_REGISTER(5) << std::endl;
    DUMP_XMM_REGISTER(6);
    DUMP_XMM_REGISTER(7) << std::endl;
    DUMP_XMM_REGISTER(8);
    DUMP_XMM_REGISTER(9) << std::endl;
    DUMP_XMM_REGISTER(10);
    DUMP_XMM_REGISTER(11) << std::endl;
    DUMP_XMM_REGISTER(12);
    DUMP_XMM_REGISTER(13) << std::endl;
    DUMP_XMM_REGISTER(14);
    DUMP_XMM_REGISTER(15) << std::endl;
}

void DumpYmmRegisters()
{
    const auto ctx = inasm64::runtime::Context();
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
    }
}

void DisplayMemoryAsType(inasm64::cli::DataType type, unsigned type_size, const char* memory, size_t size)
{
    using namespace inasm64::cli;
    for(unsigned c = 0; c < size; c += type_size)
    {
        const auto rp = memory + c;
        switch(type)
        {
        case DataType::kByte:
        {
            const unsigned val = *rp & 0xff;
            std::cout << std::hex << std::setw(2) << std::setfill('0') << val << " ";
        }
        break;
        case DataType::kWord:
        {
            const auto val = *reinterpret_cast<const unsigned short*>(rp) & 0xffff;
            std::cout << std::hex << std::setw(4) << std::setfill('0') << val << " ";
        }
        break;
        case DataType::kDWord:
        {
            const auto val = *reinterpret_cast<const uint32_t*>(rp);
            std::cout << std::hex << std::setw(8) << std::setfill('0') << val << " ";
        }
        break;
        case DataType::kQWord:
        {
            const auto val = *reinterpret_cast<const uint64_t*>(rp);
            std::cout << std::hex << std::setw(16) << std::setfill('0') << val << " ";
        }
        break;
        case DataType::kFloat32:
        {
            const auto val = *reinterpret_cast<const float*>(rp);
            std::cout << std::setprecision(7) << val << " ";
        }
        break;
        case DataType::kFloat64:
        {
            const auto val = *reinterpret_cast<const double*>(rp);
            std::cout << std::setprecision(7) << val << " ";
        }
        break;
        }
    }
}

void DumpMemory(inasm64::cli::DataType type, const void* remote_address, size_t size_)
{
    //NOTE: all commands are being invoked on the same line as the input, a bit cumbersome but...
    std::cout << "\n";

    using namespace inasm64::cli;
    constexpr auto kBytesPerRow = 16;
    constexpr auto kRows = 4;

    // we don't display all the data (yet), so pick smallest of window
    auto size = std::min<size_t>(kBytesPerRow * kRows, size_);
    // create padding to nearest 8 bytes, so that we can 0 out top bytes in >1 byte types if they go outside the range
    const auto buffer_size = (size + 8) & ~7;
    const auto local_buffer = new char[buffer_size];
    // clear the buffer
    memset(local_buffer, 0, buffer_size);
    inasm64::runtime::ReadBytes(remote_address, local_buffer, size);
    const auto type_size = inasm64::cli::DataTypeToBitWidth(type);
    auto n = 0;
    auto address = reinterpret_cast<const uint8_t*>(remote_address);

    while(size)
    {
        std::cout << std::hex << uintptr_t(address) << " ";
        std::cout << console::yellow_lo;
        const auto bytes_to_read = std::min<size_t>(kBytesPerRow, size);
        DisplayMemoryAsType(type, type_size, local_buffer + n, bytes_to_read);
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

void DisplayRegister(inasm64::cli::DataType type, const inasm64::RegisterInfo& reg_info)
{
    using namespace inasm64;
    const auto type_size = cli::DataTypeToBitWidth(type) >> 3;
    std::cout << "\n   ";
    switch(reg_info._class)
    {
    case RegisterInfo::RegClass::kSegment:
    {
        uint16_t val;
        runtime::GetReg(reg_info, &val, sizeof(val));
        std::cout << std::hex << std::setw(4) << std::setfill('0') << val << " ";
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
        cout_bytes_as_number(std::cout, val, reg_info._bit_width / 8);
    }
    break;
    case RegisterInfo::RegClass::kXmm:
    {
        uint8_t val[16];
        runtime::GetReg(reg_info, val, sizeof(val));
        cout_bytes_as_number(std::cout, val, sizeof(val));
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
    auto sse_supported = false;
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSse))
    {
        std::cout << "SSE ";
        sse_supported = true;
    }
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSse2))
    {
        std::cout << "SSE2 ";
        sse_supported = true;
    }
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSse3))
    {
        std::cout << "SSE3 ";
        sse_supported = true;
    }
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSsse3))
    {
        std::cout << "SSSE3 ";
        sse_supported = true;
    }
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSse4_1))
    {
        std::cout << "SSE4_1 ";
        sse_supported = true;
    }
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSse4_2))
    {
        std::cout << "SSE4_2 ";
        sse_supported = true;
    }
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSse4a))
    {
        std::cout << "SSE4a ";
        sse_supported = true;
    }
    if(inasm64::SseLevelSupported(inasm64::SseLevel::kSse5))
    {
        std::cout << "SSE5 ";
        sse_supported = true;
    }
    if(sse_supported)
        std::cout << "supported\n";

    DWORD64 featureFlags = GetEnabledXStateFeatures();
    if(featureFlags & (XSTATE_MASK_AVX512 | XSTATE_MASK_AVX))
    {
        if((featureFlags & XSTATE_MASK_AVX) == XSTATE_MASK_AVX)
            std::cout << "AVX ";
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
        if(key == inasm64::kTrapModeArgumentValue)
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
            const auto cw = console::Width();
            for(const auto& help : help_texts)
            {
                std::cout << help.first;
                console::SetCursorX(cw - cw / 2);
                std::cout << help.second << std::endl;
            }
        };
        cli::OnStep = [](const void* address) {
            std::cout << "\n"
                      << std::hex << address << " " << _asm_history[uintptr_t(address)] << "\n";
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
        cli::OnAssembling = [&input](const void* address, const assembler::AssembledInstructionInfo& asm_info) {
            _asm_history[uintptr_t(address)] = input;
            to_right_column();
            for(unsigned n = 0; n < asm_info.InstructionSize; ++n)
            {
                std::cout << console::green << std::hex << std::setw(2) << std::setfill('0') << int(asm_info.Instruction[n]);
            }
            std::cout << console::reset_colours;
        };
        cli::OnAssembleError = [&input, &input_start_cursor_x, &clear_next_input_on_key]() -> bool {
            to_right_column();
            std::cerr << console::red << "\t" << inasm64::ErrorMessage(inasm64::GetError()) << console::reset_colours;
            console::SetCursorX(0);
            clear_next_input_on_key = true;
            return true;
        };
        cli::OnStopAssembling = []() {
            std::cout << std::endl;
        };

        cli::OnFindInstruction = [](const std::vector<const char*>& instructions) {
            std::cout << "there are " << instructions.size() << " instruction matches:\n";
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
                std::cout << std::hex << uintptr_t(cli::NextInstructionAssemblyAddress()) << " ";
            }
            else
            {
                assembling = false;
                std::cout << "> ";
            }
            input_start_cursor_x = console::GetCursorX();
            console::ReadLine(input, clear_next_input_on_key);

            if(!cli::Execute(input.c_str()) && cli::ActiveMode() != cli::Mode::Assembling)
            {
                to_right_column();
                std::cerr << console::red << inasm64::ErrorMessage(inasm64::GetError()) << console::reset_colours;
                console::SetCursorX(0);
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
