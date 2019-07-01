
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "inasm64/common.h"
#include "inasm64/ia64.h"
#include "inasm64/runtime.h"
#include "inasm64/assembler.h"
#include "inasm64/cli.h"
#include "inasm64/globvars.h"

namespace console
{
    HANDLE _std_in, _std_out;
    CONSOLE_SCREEN_BUFFER_INFO _std_out_info;
    CONSOLE_SCREEN_BUFFER_INFO _csbiInfo;
    DWORD _std_in_mode;

    constexpr auto kMaxLineLength = 256;

    void Initialise()
    {
        _std_in = GetStdHandle(STD_INPUT_HANDLE);
        _std_out = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(_std_out, &_std_out_info);
        GetConsoleMode(_std_in, &_std_in_mode);
    }

    std::ostream& reset_colours(std::ostream& os)
    {
        SetConsoleTextAttribute(_std_out, _std_out_info.wAttributes);
        return os;
    }

    std::ostream& red(std::ostream& os)
    {
        SetConsoleTextAttribute(_std_out, FOREGROUND_RED | FOREGROUND_INTENSITY);
        return os;
    }

    std::ostream& green(std::ostream& os)
    {
        SetConsoleTextAttribute(_std_out, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        return os;
    }

    std::ostream& blue(std::ostream& os)
    {
        SetConsoleTextAttribute(_std_out, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        return os;
    }

    std::ostream& yellow(std::ostream& os)
    {
        SetConsoleTextAttribute(_std_out, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        return os;
    }

    void read_line(std::string& line)
    {
        const auto mode = _std_in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);
        SetConsoleMode(_std_in, mode);

        char line_buffer[kMaxLineLength];
        auto line_wp = 0;
        auto max_read = 0;

        CONSOLE_SCREEN_BUFFER_INFO start_cs_info;
        GetConsoleScreenBufferInfo(_std_out, &start_cs_info);

        const auto move_cursor_pos = [](short dX) {
            CONSOLE_SCREEN_BUFFER_INFO cs_info;
            GetConsoleScreenBufferInfo(_std_out, &cs_info);
            cs_info.dwCursorPosition.X += dX;
            SetConsoleCursorPosition(_std_out, cs_info.dwCursorPosition);
        };

        auto done = false;
        while(!done)
        {
            INPUT_RECORD input_record;
            DWORD read;
            ReadConsoleInputA(_std_in, &input_record, 1, &read);
            switch(input_record.EventType)
            {
            case KEY_EVENT:
            {
                if(input_record.Event.KeyEvent.bKeyDown)
                {
                    const auto vcode = input_record.Event.KeyEvent.wVirtualKeyCode;
                    // numbers
                    if((vcode >= 0x30 && vcode <= 0x39) ||
                        // letters
                        (vcode >= 0x41 && vcode <= 0x5A) ||
                        // *+- etc...
                        (vcode >= 0x6a && vcode <= 0x6f) ||
                        // OEM character keys
                        (vcode >= 0xba && vcode <= 0xe2) ||
                        vcode == VK_SPACE)
                    {
                        std::cout << input_record.Event.KeyEvent.uChar.AsciiChar;
                        line_buffer[line_wp++] = input_record.Event.KeyEvent.uChar.AsciiChar;
                        max_read = std::max<decltype(max_read)>(max_read, line_wp);
                    }
                    else
                    {
                        switch(vcode)
                        {
                        case VK_LEFT:
                        {
                            if(line_wp)
                            {
                                --line_wp;
                                move_cursor_pos(-1);
                            }
                        }
                        break;
                        case VK_RIGHT:
                        {
                            if(max_read > line_wp)
                            {
                                ++line_wp;
                                move_cursor_pos(+1);
                            }
                        }
                        break;
                        case VK_HOME:
                        {
                            line_wp = 0;
                            SetConsoleCursorPosition(_std_out, start_cs_info.dwCursorPosition);
                        }
                        break;
                        case VK_END:
                        {
                            line_wp = max_read;
                            COORD dpos = start_cs_info.dwCursorPosition;
                            dpos.X += short(max_read);
                            SetConsoleCursorPosition(_std_out, dpos);
                        }
                        break;
                        case VK_DELETE:
                            break;
                        case VK_BACK:
                        {
                            // I'm lazy; only allow this from the back of the line
                            if(line_wp == max_read)
                            {
                                --max_read;
                                --line_wp;
                                move_cursor_pos(-1);
                                std::cout << " ";
                                move_cursor_pos(-1);
                            }
                        }
                        break;
                        case VK_RETURN:
                            line_buffer[line_wp] = 0;
                            line = std::string(line_buffer);
                            done = true;
                            break;
                        case VK_TAB:
                            break;
                        case VK_UP:
                            break;
                        case VK_DOWN:
                            break;
                        default:;
                        }
                    }
                }
            }
            break;
            default:;
            }
        }
        SetConsoleMode(_std_in, _std_in_mode);
    }
}  // namespace console

std::ostream& coutreg(const char* reg)
{
    // perhaps I should just have used printf....
    return std::cout << std::setfill(' ') << std::setw(4) << reg << std::setw(1) << " = 0x" << std::setfill('0') << std::setw(16) << std::hex;
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

    std::cout << "\n";
    coutreg("eflags") << ctx->OsContext->EFlags << std::endl;

    //TODO: XMM, FP, make it selectable, etc.
}

void DumpReg(const char*)
{
    //TODO:
}

void DumpXmmRegisters()
{
    const auto ctx = inasm64::runtime::Context();

    coutreg("xmm0") << ctx->OsContext->Xmm0.Low << ctx->OsContext->Xmm0.High << " ";
    coutreg("xmm1") << ctx->OsContext->Xmm1.Low << ctx->OsContext->Xmm1.High << " " << std::endl;
    coutreg("xmm2") << ctx->OsContext->Xmm2.Low << ctx->OsContext->Xmm2.High << " ";
    coutreg("xmm3") << ctx->OsContext->Xmm3.Low << ctx->OsContext->Xmm3.High << " " << std::endl;
    coutreg("xmm4") << ctx->OsContext->Xmm4.Low << ctx->OsContext->Xmm4.High << " ";
    coutreg("xmm5") << ctx->OsContext->Xmm5.Low << ctx->OsContext->Xmm5.High << " " << std::endl;
    coutreg("xmm6") << ctx->OsContext->Xmm6.Low << ctx->OsContext->Xmm6.High << " ";
    coutreg("xmm7") << ctx->OsContext->Xmm7.Low << ctx->OsContext->Xmm7.High << " " << std::endl;
    coutreg("xmm8") << ctx->OsContext->Xmm8.Low << ctx->OsContext->Xmm8.High << " ";
    coutreg("xmm9") << ctx->OsContext->Xmm9.Low << ctx->OsContext->Xmm9.High << " " << std::endl;
    coutreg("xmm10") << ctx->OsContext->Xmm10.Low << ctx->OsContext->Xmm10.High << " ";
    coutreg("xmm11") << ctx->OsContext->Xmm11.Low << ctx->OsContext->Xmm11.High << " " << std::endl;
    coutreg("xmm12") << ctx->OsContext->Xmm12.Low << ctx->OsContext->Xmm12.High << " ";
    coutreg("xmm13") << ctx->OsContext->Xmm13.Low << ctx->OsContext->Xmm13.High << " " << std::endl;
    coutreg("xmm14") << ctx->OsContext->Xmm14.Low << ctx->OsContext->Xmm14.High << " ";
    coutreg("xmm15") << ctx->OsContext->Xmm15.Low << ctx->OsContext->Xmm15.High << std::endl;
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
                coutreg("ymm0") << Ymm[0].Low << Ymm[0].High << " ";
                coutreg("ymm1") << Ymm[1].Low << Ymm[1].High << " " << std::endl;
                coutreg("ymm2") << Ymm[2].Low << Ymm[2].High << " ";
                coutreg("ymm3") << Ymm[3].Low << Ymm[3].High << " " << std::endl;
                coutreg("ymm4") << Ymm[4].Low << Ymm[4].High << " ";
                coutreg("ymm5") << Ymm[5].Low << Ymm[5].High << " " << std::endl;
                coutreg("ymm6") << Ymm[6].Low << Ymm[6].High << " ";
                coutreg("ymm7") << Ymm[7].Low << Ymm[7].High << " " << std::endl;
                coutreg("ymm8") << Ymm[8].Low << Ymm[8].High << " ";
                coutreg("ymm9") << Ymm[9].Low << Ymm[9].High << " " << std::endl;
                coutreg("ymm10") << Ymm[10].Low << Ymm[10].High << " ";
                coutreg("ymm11") << Ymm[11].Low << Ymm[11].High << " " << std::endl;
                coutreg("ymm12") << Ymm[12].Low << Ymm[12].High << " ";
                coutreg("ymm13") << Ymm[13].Low << Ymm[13].High << " " << std::endl;
                coutreg("ymm14") << Ymm[14].Low << Ymm[14].High << " ";
                coutreg("ymm15") << Ymm[15].Low << Ymm[15].High << std::endl;
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

void DumpInstructionInfo()
{
    const auto ctx = inasm64::runtime::Context();
    for(auto n = 0; n < ctx->InstructionSize; ++n)
    {
        std::cout << std::hex << int(ctx->Instruction[n]) << " ";
    }
    std::cout << std::endl;
}

void DumpMemory(inasm64::cli::DataType, const void* remote_address, size_t size_)
{
    // mvp; max 4 rows, 8 columns of data
    constexpr auto kCols = 8;
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
            if(c < cols)
                std::cout << std::hex << std::setw(2) << std::setfill('0') << int(local_buffer[n + c]) << " ";
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
        cli::OnHelp = []() {
            std::cout << cli::Help() << std::endl;
        };
        cli::OnStep = []() {
            DumpRegs();
            DumpInstructionInfo();
        };
        cli::OnDisplayGPRegisters = DumpRegs;
        cli::OnDisplayXMMRegisters = DumpXmmRegisters;
        cli::OnDisplayYMMRegisters = DumpYmmRegisters;
        cli::OnDumpMemory = DumpMemory;

        auto assembling = false;
        cli::OnStartAssembling = [&assembling]() {
            assembling = true;
        };
        cli::OnAssembling = [](const assembler::AssembledInstructionInfo& asm_info) {
            //TODO: this doesn't really work; need a better (full) console approach to divide the window into a couple of columns so that this can be nicely aligned
            std::cout << "\t\t\t\t";
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
            std::string input;
            if(cli::ActiveMode() == cli::Mode::Assembling)
                std::cout << std::hex << cli::LastAssembledInstructionAddress() << " ";
            else
            {
                assembling = false;
                std::cout << "> ";
            }

            console::read_line(input);
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
