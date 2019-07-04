
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

auto cout_64_bits(std::ostream& os) -> std::ostream&
{
    return os << std::setfill('0') << std::setw(16) << std::hex;
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
    std::cout << console::green << regName << " = " << std::hex << value << std::endl
              << console::reset_colours;
}

void DumpXmmRegisters()
{
    const auto ctx = inasm64::runtime::Context();

    coutreg("xmm0") << ctx->OsContext->Xmm0.Low << cout_64_bits << ctx->OsContext->Xmm0.High << " ";
    coutreg("xmm1") << ctx->OsContext->Xmm1.Low << cout_64_bits << ctx->OsContext->Xmm1.High << " " << std::endl;
    coutreg("xmm2") << ctx->OsContext->Xmm2.Low << cout_64_bits << ctx->OsContext->Xmm2.High << " ";
    coutreg("xmm3") << ctx->OsContext->Xmm3.Low << cout_64_bits << ctx->OsContext->Xmm3.High << " " << std::endl;
    coutreg("xmm4") << ctx->OsContext->Xmm4.Low << cout_64_bits << ctx->OsContext->Xmm4.High << " ";
    coutreg("xmm5") << ctx->OsContext->Xmm5.Low << cout_64_bits << ctx->OsContext->Xmm5.High << " " << std::endl;
    coutreg("xmm6") << ctx->OsContext->Xmm6.Low << cout_64_bits << ctx->OsContext->Xmm6.High << " ";
    coutreg("xmm7") << ctx->OsContext->Xmm7.Low << cout_64_bits << ctx->OsContext->Xmm7.High << " " << std::endl;
    coutreg("xmm8") << ctx->OsContext->Xmm8.Low << cout_64_bits << ctx->OsContext->Xmm8.High << " ";
    coutreg("xmm9") << ctx->OsContext->Xmm9.Low << cout_64_bits << ctx->OsContext->Xmm9.High << " " << std::endl;
    coutreg("xmm10") << ctx->OsContext->Xmm10.Low << cout_64_bits << ctx->OsContext->Xmm10.High << " ";
    coutreg("xmm11") << ctx->OsContext->Xmm11.Low << cout_64_bits << ctx->OsContext->Xmm11.High << " " << std::endl;
    coutreg("xmm12") << ctx->OsContext->Xmm12.Low << cout_64_bits << ctx->OsContext->Xmm12.High << " ";
    coutreg("xmm13") << ctx->OsContext->Xmm13.Low << cout_64_bits << ctx->OsContext->Xmm13.High << " " << std::endl;
    coutreg("xmm14") << ctx->OsContext->Xmm14.Low << cout_64_bits << ctx->OsContext->Xmm14.High << " ";
    coutreg("xmm15") << ctx->OsContext->Xmm15.Low << cout_64_bits << ctx->OsContext->Xmm15.High << std::endl;
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
                coutreg("ymm0") << Ymm[0].Low << cout_64_bits << Ymm[0].High << " ";
                coutreg("ymm1") << Ymm[1].Low << cout_64_bits << Ymm[1].High << " " << std::endl;
                coutreg("ymm2") << Ymm[2].Low << cout_64_bits << Ymm[2].High << " ";
                coutreg("ymm3") << Ymm[3].Low << cout_64_bits << Ymm[3].High << " " << std::endl;
                coutreg("ymm4") << Ymm[4].Low << cout_64_bits << Ymm[4].High << " ";
                coutreg("ymm5") << Ymm[5].Low << cout_64_bits << Ymm[5].High << " " << std::endl;
                coutreg("ymm6") << Ymm[6].Low << cout_64_bits << Ymm[6].High << " ";
                coutreg("ymm7") << Ymm[7].Low << cout_64_bits << Ymm[7].High << " " << std::endl;
                coutreg("ymm8") << Ymm[8].Low << cout_64_bits << Ymm[8].High << " ";
                coutreg("ymm9") << Ymm[9].Low << cout_64_bits << Ymm[9].High << " " << std::endl;
                coutreg("ymm10") << Ymm[10].Low << cout_64_bits << Ymm[10].High << " ";
                coutreg("ymm11") << Ymm[11].Low << cout_64_bits << Ymm[11].High << " " << std::endl;
                coutreg("ymm12") << Ymm[12].Low << cout_64_bits << Ymm[12].High << " ";
                coutreg("ymm13") << Ymm[13].Low << cout_64_bits << Ymm[13].High << " " << std::endl;
                coutreg("ymm14") << Ymm[14].Low << cout_64_bits << Ymm[14].High << " ";
                coutreg("ymm15") << Ymm[15].Low << cout_64_bits << Ymm[15].High << std::endl;
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

void DumpMemory(inasm64::cli::DataType, const void* remote_address, size_t size_)
{
    // mvp; max 4 rows, 16 columns of data
    constexpr auto kCols = 16;
    constexpr auto kRows = 4;

    auto size = std::min<size_t>(kCols * kRows, size_);
    const auto local_buffer = new char[size];
    inasm64::runtime::ReadBytes(remote_address, local_buffer, size);
    auto rows = size / kCols;
    auto n = 0;
    auto address = reinterpret_cast<const uint8_t*>(remote_address);

    while(size)
    {
        std::cout << std::hex << uintptr_t(address) << " ";
        auto cols = std::min<size_t>(kCols, size);
        for(unsigned c = 0; c < kCols; ++c)
        {
            unsigned val = local_buffer[n + c] & 0xff;
            if(c < cols)
                std::cout << std::hex << std::setw(2) << std::setfill('0') << val << " ";
            else
                std::cout << "   ";
        }
        for(unsigned c = 0; c < cols; ++c)
        {
            const auto ic = local_buffer[n++];
            if(ic >= 0x21 && ic <= 0x7e)
                std::cout << ic;
            else
                std::cout << ".";
        }
        std::cout << std::endl;
        size -= cols;
        --rows;
        address += kCols;
    }
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

    using namespace inasm64;

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
            std::cout << std::hex << address << " " << _asm_history[uintptr_t(address)] << "\n";
            DumpDeltaRegs();
        };
        cli::OnDisplayGPRegisters = DumpRegs;
        cli::OnDisplayXMMRegisters = DumpXmmRegisters;
        cli::OnDisplayYMMRegisters = DumpYmmRegisters;
        cli::OnDumpMemory = DumpMemory;
        cli::OnSetGPRegister = DumpReg;

        std::string input;

        auto assembling = false;
        cli::OnStartAssembling = [&assembling]() {
            assembling = true;
        };
        cli::OnAssembling = [&input](const void* address, const assembler::AssembledInstructionInfo& asm_info) {
            _asm_history[uintptr_t(address)] = input;

            const auto cw = console::Width();
            console::SetCursorX(cw - cw / 2);
            for(unsigned n = 0; n < asm_info.InstructionSize; ++n)
            {
                std::cout << console::green << std::hex << std::setw(2) << std::setfill('0') << int(asm_info.Instruction[n]);
            }
            std::cout << std::endl
                      << console::reset_colours;
        };
        cli::OnStopAssembling = []() {
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

            console::ReadLine(input);
            if(!assembling)
                std::cout << std::endl;

            if(!cli::Execute(input.c_str()))
                std::cerr << console::red << "\n"
                          << inasm64::ErrorMessage(inasm64::GetError()) << console::reset_colours << std::endl;
        }
    }
    else
    {
        std::cerr << console::red << "*failed to start inasm64 runtime!\n";
    }

    std::cout << console::reset_colours;

    return 0;
}
