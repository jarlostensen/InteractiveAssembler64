#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include "console.h"

namespace console
{
    namespace detail
    {
        HANDLE _std_in, _std_out;
        CONSOLE_SCREEN_BUFFER_INFO _std_out_info;
        DWORD _std_in_mode;
    }  // namespace detail

    constexpr auto kMaxLineLength = 256;
    using namespace detail;

    void Initialise()
    {
        _std_in = GetStdHandle(STD_INPUT_HANDLE);
        _std_out = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(_std_out, &_std_out_info);
        GetConsoleMode(_std_in, &_std_in_mode);
    }

    short Width()
    {
        return short(_std_out_info.dwSize.X);
    }

    short Height()
    {
        return short(_std_out_info.dwSize.Y);
    }

    void SetCursorX(short x)
    {
        CONSOLE_SCREEN_BUFFER_INFO cs_info;
        GetConsoleScreenBufferInfo(_std_out, &cs_info);
        cs_info.dwCursorPosition.X = x;
        SetConsoleCursorPosition(_std_out, cs_info.dwCursorPosition);
    }

    void ReadLine(std::string& line)
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
            INPUT_RECORD input_records[128];
            DWORD read;
            ReadConsoleInputA(_std_in, input_records, DWORD(std::size(input_records)), &read);

            for(unsigned r = 0; r < read; ++r)
            {
                const auto& input_record = input_records[r];
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
                                if(line_wp && line_wp == max_read)
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
                case WINDOW_BUFFER_SIZE_EVENT:
                {
                    _std_out_info.dwSize = input_record.Event.WindowBufferSizeEvent.dwSize;
                }
                break;
                default:;
                }
            }
        }
        SetConsoleMode(_std_in, _std_in_mode);
    }
}  // namespace console
