// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <iomanip>
#include "console.h"

namespace console
{
    namespace detail
    {
        HANDLE _std_in, _std_out;
        CONSOLE_SCREEN_BUFFER_INFO _std_out_info;
        DWORD _std_in_mode;

        struct MultiLineBuffer
        {
            std::vector<std::string> _lines;
            size_t _line = 0;

            void Commit(std::string&& line)
            {
                if(_line == _lines.size())
                    _lines.emplace_back(std::move(line));
                else
                {
                    _lines[_line] = std::move(line);
                    if(_line + 1 < _lines.size())
                        line = _lines[_line + 1];
                    else
                        _lines[_line] = std::move(line);
                }
                ++_line;
            }

            bool Prev()
            {
                if(_line)
                {
                    --_line;
                    return true;
                }
                return false;
            }

            bool Next()
            {
                if(_line < _lines.size())
                {
                    ++_line;
                    return true;
                }
                return false;
            }

            const std::string& Line() const
            {
                if(_line < _lines.size())
                    return _lines[_line];
                static std::string kEmptyString = "";
                return kEmptyString;
            }
        };

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

    void SetCursorY(short y)
    {
        CONSOLE_SCREEN_BUFFER_INFO cs_info;
        GetConsoleScreenBufferInfo(_std_out, &cs_info);
        cs_info.dwCursorPosition.Y = y;
        SetConsoleCursorPosition(_std_out, cs_info.dwCursorPosition);
    }

    bool MoveCursorY(short dy)
    {
        CONSOLE_SCREEN_BUFFER_INFO cs_info;
        GetConsoleScreenBufferInfo(_std_out, &cs_info);

        cs_info.dwCursorPosition.Y += dy;

        if(dy < 0 && cs_info.dwCursorPosition.Y < 0)
            return false;

        if(dy >= 0 && cs_info.dwCursorPosition.Y >= cs_info.srWindow.Bottom)
            return false;

        SetConsoleCursorPosition(_std_out, cs_info.dwCursorPosition);
        return true;
    }

    short GetCursorX()
    {
        CONSOLE_SCREEN_BUFFER_INFO cs_info;
        GetConsoleScreenBufferInfo(_std_out, &cs_info);
        return cs_info.dwCursorPosition.X;
    }

    void ClearLineToEnd()
    {
        std::cout << std::setw(Width() - GetCursorX()) << std::setfill(' ') << " ";
    }

    std::pair<ReadLineResult, bool> ReadLine(std::string& line, bool clearLineOnKey)
    {
        const auto mode = _std_in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);
        SetConsoleMode(_std_in, mode);

        char line_buffer[kMaxLineLength];
        size_t line_wp = line.length() ? line.length() : 0;
        size_t max_read = line_wp;

        CONSOLE_SCREEN_BUFFER_INFO start_cs_info;
        GetConsoleScreenBufferInfo(_std_out, &start_cs_info);

        if(line_wp)
        {
            memcpy(line_buffer, line.c_str(), line_wp);
            std::cout << line;
        }

        const auto move_cursor_pos = [](short dX) {
            CONSOLE_SCREEN_BUFFER_INFO cs_info;
            GetConsoleScreenBufferInfo(_std_out, &cs_info);
            cs_info.dwCursorPosition.X += dX;
            SetConsoleCursorPosition(_std_out, cs_info.dwCursorPosition);
        };

        ReadLineResult result = ReadLineResult::kReturn;
        auto changed = false;
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
                            if(clearLineOnKey)
                            {
                                ClearLineToEnd();
                                SetCursorX(start_cs_info.dwCursorPosition.X);
                                clearLineOnKey = false;
                            }
                            changed = true;
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
                                    changed = true;
                                }
                            }
                            break;
                            case VK_RETURN:
                                result = ReadLineResult::kReturn;
                                done = true;
                                break;
                            case VK_TAB:
                                result = ReadLineResult::kTab;
                                done = true;
                                break;
                            case VK_UP:
                                result = ReadLineResult::kUp;
                                done = true;
                                break;
                            case VK_DOWN:
                                result = ReadLineResult::kDown;
                                done = true;
                                break;
                            case VK_ESCAPE:
                                result = ReadLineResult::kEsc;
                                done = true;
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
        // always commit what's been read
        line_buffer[line_wp] = 0;
        line = std::string(line_buffer);
        SetConsoleMode(_std_in, _std_in_mode);
        return std::make_pair(result,changed);
    }

    void ReadLines(std::vector<std::string>& lines_)
    {
        CONSOLE_SCREEN_BUFFER_INFO start_cs_info;
        GetConsoleScreenBufferInfo(_std_out, &start_cs_info);

        MultiLineBuffer lines;
        std::string line;
        std::pair<ReadLineResult, bool> result = { ReadLineResult::kReturn, false };
        do
        {
            result = ReadLine(line, false);
            switch(result.first)
            {
            case ReadLineResult::kReturn:
            case ReadLineResult::kEsc:
            {
                lines.Commit(std::move(line));
                std::cout << std::endl;
            }
            break;
            case ReadLineResult::kUp:
            {
                if(lines.Prev())
                {
                    MoveCursorY(-1);
                    SetCursorX(0);
                    line = lines.Line();
                }
            }
            break;
            case ReadLineResult::kDown:
            {
                if(lines.Next())
                {
                    MoveCursorY(+1);
                    SetCursorX(0);
                    line = lines.Line();
                }
            }
            break;
            default:;
            }
        } while(result.first != ReadLineResult::kEsc);

        lines_ = std::move(lines._lines);
    }

}  // namespace console
