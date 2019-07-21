// MIT License
// Copyright 2019 Jarl Ostensen
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

// =============================================================================================
// Windows console rendering and input helpers

#pragma once

namespace console
{
    namespace detail
    {
        extern HANDLE _std_in;
        extern HANDLE _std_out;
        extern CONSOLE_SCREEN_BUFFER_INFO _std_out_info;
    }  // namespace detail

    void Initialise();
    short Width();
    short Height();
    void SetCursorX(short x);
    short GetCursorX();
    void ReadLine(std::string& line, bool clearLineOnKey);

    inline std::ostream& reset_colours(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, detail::_std_out_info.wAttributes);
        return os;
    }

    inline std::ostream& red(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_RED | FOREGROUND_INTENSITY);
        return os;
    }

    inline std::ostream& green(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        return os;
    }

    inline std::ostream& blue(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        return os;
    }

    inline std::ostream& yellow(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        return os;
    }

    inline std::ostream& red_lo(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_RED);
        return os;
    }

    inline std::ostream& green_lo(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_GREEN);
        return os;
    }

    inline std::ostream& blue_lo(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_BLUE);
        return os;
    }

    inline std::ostream& yellow_lo(std::ostream& os)
    {
        SetConsoleTextAttribute(detail::_std_out, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        return os;
    }

}  // namespace console
