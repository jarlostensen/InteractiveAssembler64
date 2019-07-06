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
