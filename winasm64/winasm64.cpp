
#include "winasm64.h"
#include "framework.h"
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

using namespace Gdiplus;
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Dwmapi.lib")

namespace
{
    struct TmpDc
    {
        explicit TmpDc(HWND hWnd)
        {
            _dc = GetDC(_hwnd = hWnd);
        }

        ~TmpDc()
        {
            ReleaseDC(_hwnd, _dc);
        }

        operator HDC() const
        {
            return _dc;
        }

        HWND _hwnd;
        HDC _dc;
    };
}

namespace app
{
    enum class Panel
    {
        kAsmEditor,
        kLineCounter,
        kCodes,
        kOutput
    };

    HWND _winasm64 = nullptr;
    HWND _asm_editor = nullptr;
    HWND _linecounter = nullptr;
    HWND _codes = nullptr;
    HWND _output = nullptr;
    HWND _glowBorder = nullptr;

    GdiplusStartupInput _gdiplusStartupInput;
    ULONG_PTR _gdiplusToken = 0;

    short lines = 0;
    short max_line = 0;

    struct
    {
        static constexpr unsigned short kBottomBorderHeight = 1u;
        unsigned short _window_width;
        unsigned short _window_height;
        unsigned short _top_panel_height;
        unsigned short _linecount_window_width;
        unsigned short _asm_window_width;
        unsigned short _code_window_width;
        unsigned short _output_window_height;

        unsigned short _caption_height;
        RECT _minimise_hit_area;
        RECT _capacityimise_hit_area;
        RECT _close_hit_area;
        RECT _resize_hit_area;

    } _layout;

    struct
    {
        int _horisontal;
        int _vertical;
    } _dpi;

    struct
    {
        bool _mouse_on_minimise : 1;
        bool _mouse_on_capacityimise : 1;
        bool _mouse_on_close : 1;

        bool _needs_restore : 1;
        bool _has_focus : 1;

    } _flags = { 0 };

    namespace dark_theme
    {
        const auto _almostBlack = Color(255, 25, 25, 28);
        const auto _darkGray = Color(255, 45, 45, 48);
        const auto _dullBlue = Color(255, 0xff, 0x77, 0x77);
        const auto _codeGreen = Color(255, 0x77, 0xff, 0x77);
        const auto _lightGray = Color(255, 0xaa, 0xaa, 0xaa);
       
        FontFamily* _consolasFontFamily = nullptr;
        Font* _consolasRegular = nullptr;
        Font* _consolasSmall = nullptr;
        SolidBrush* _blackBrush = nullptr;
        SolidBrush* _darkGrayBrush = nullptr;
        SolidBrush* _lightGrayBrush = nullptr;
        SolidBrush* _redBrush = nullptr;
        Pen* _whitePen = nullptr;

        // we need to use good old GDI for a few things too, in particular for cutom font rendering since GDI+ is incapable of it
        //NOTE: we need these to manage controls we don't render ourselves
        HBRUSH _legacyBlackBrush = nullptr;
        constexpr COLORREF kDullBlue = COLORREF(0xff7777);
        constexpr COLORREF kAlmostBlack = RGB(25, 25, 28);
        constexpr COLORREF kCodeGreen = COLORREF(0x77ff77);
        constexpr COLORREF kLightGray = COLORREF(0xaaaaaa);
        constexpr COLORREF kDarkGray = RGB(45, 45, 48);       
        HPEN _lightGrayPen = 0;
        HFONT _consolasRegularGdi = 0;

        void Shutdown()
        {
            DeleteObject(_consolasRegularGdi);
            DeleteObject(dark_theme::_legacyBlackBrush);
            delete _redBrush;
            delete _lightGrayBrush;
            delete _darkGrayBrush;
            delete _blackBrush;
            delete _consolasSmall;
            delete _consolasRegular;
            delete _consolasFontFamily;
            delete _whitePen;
        }

        HBRUSH Select(HDC hdc, Panel panel)
        {
            switch(panel)
            {
            case Panel::kAsmEditor:
                SetTextColor(hdc, kDullBlue);
                SetBkColor(hdc, kAlmostBlack);
                return _legacyBlackBrush;
            case Panel::kLineCounter:
                return (HBRUSH)(COLOR_WINDOW + 1);
            case Panel::kCodes:
                SetTextColor(hdc, kCodeGreen);
                SetBkColor(hdc, kAlmostBlack);
                return _legacyBlackBrush;
            case Panel::kOutput:
                SetTextColor(hdc, kLightGray);
                SetBkColor(hdc, kDarkGray);
                return _legacyBlackBrush;
            default:;
            }
            return _legacyBlackBrush;
        }

        void DrawFrameControl(HDC dc, UINT type)
        {
            const RECT* rect = nullptr;
            bool activated = false;
            switch(type)
            {
            case DFCS_CAPTIONCLOSE:
                rect = &_layout._close_hit_area;
                activated = _flags._mouse_on_close;
                break;
            case DFCS_CAPTIONMIN:
                rect = &_layout._minimise_hit_area;
                activated = _flags._mouse_on_minimise;
                break;
            case DFCS_CAPTIONMAX:
                rect = &_layout._capacityimise_hit_area;
                activated = _flags._mouse_on_capacityimise;
                break;
            default:;
            }

            Graphics graphics(dc);

            int frame_width = rect->right - rect->left;
            int frame_height = rect->bottom - rect->top;
            if(_dpi._horisontal > _dpi._vertical)
                frame_width = MulDiv(frame_height, _dpi._horisontal, _dpi._vertical);
            else
                frame_height = MulDiv(frame_width, _dpi._vertical, _dpi._horisontal);

            Gdiplus::Rect gdirect{ rect->left, rect->top, frame_width, frame_height };

            // draw background rectangle
            if(activated)
                graphics.FillRectangle(type != DFCS_CAPTIONCLOSE ? _lightGrayBrush : _redBrush, gdirect);
            else
                graphics.FillRectangle(_darkGrayBrush, gdirect);

            gdirect.Inflate(-8, -8);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);

            Point pt0;
            gdirect.GetLocation(&pt0);
            switch(type)
            {
            case DFCS_CAPTIONCLOSE:
            {
                graphics.DrawLine(_whitePen, pt0, Point(gdirect.GetRight(), gdirect.GetBottom()));
                graphics.DrawLine(_whitePen, Point(gdirect.GetLeft(), gdirect.GetBottom()), Point(gdirect.GetRight(), gdirect.GetTop()));
            }
            break;
            case DFCS_CAPTIONMIN:
            {
                graphics.DrawLine(_whitePen, Point(gdirect.GetLeft(), gdirect.GetBottom()), Point(gdirect.GetRight(), gdirect.GetBottom()));
            }
            break;
            case DFCS_CAPTIONMAX:
            {
                graphics.DrawRectangle(_whitePen, gdirect);
            }
            break;
            default:;
            }
        }

        void CheckDisableFrameControl(HDC dc, UINT dfcsId)
        {
            switch(dfcsId)
            {
            case DFCS_CAPTIONCLOSE:
                if(_flags._mouse_on_close)
                {
                    _flags._mouse_on_close = false;
                    dark_theme::DrawFrameControl(dc, DFCS_CAPTIONCLOSE);
                }
                break;
            case DFCS_CAPTIONMAX:
                if(_flags._mouse_on_capacityimise)
                {
                    _flags._mouse_on_capacityimise = false;
                    dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMAX);
                }
                break;
            case DFCS_CAPTIONMIN:
                if(_flags._mouse_on_minimise)
                {
                    _flags._mouse_on_minimise = false;
                    dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMIN);
                }
                break;
            default:;
            }
        }

        void CheckDisableFrameControls(HDC dc)
        {
            if(_flags._mouse_on_close)
            {
                _flags._mouse_on_close = false;
                dark_theme::DrawFrameControl(dc, DFCS_CAPTIONCLOSE);
            }
            else if(_flags._mouse_on_capacityimise)
            {
                _flags._mouse_on_capacityimise = false;
                dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMAX);
            }
            else if(_flags._mouse_on_minimise)
            {
                _flags._mouse_on_minimise = false;
                dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMIN);
            }
        }
    }

    void AppendText(HWND hwnd, const TCHAR* text)
    {
        int outLength = GetWindowTextLength(hwnd) + lstrlen(text) + 1;

        TCHAR* buf = (TCHAR*)GlobalAlloc(GPTR, outLength * sizeof(TCHAR));
        if(!buf)
            return;

        GetWindowText(hwnd, buf, outLength);

        _tcscat_s(buf, outLength, text);

        SetWindowText(hwnd, buf);
        GlobalFree(buf);
    }

    void AppendLineNumber()
    {
        TCHAR linenum[8] = { 0 };
        swprintf_s(linenum, L"%d\r\n", lines);
        AppendText(_linecounter, linenum);
        ++lines;
    }
}

namespace LineEditor
{
    struct Line
    {
        wchar_t* _buffer = nullptr;
        size_t _capacity = 0;
        size_t _length = 0;
        // position of caret at current end of string
        int _caret_end = 0;
    };

    struct State
    {
        std::vector<Line> _lines;

        size_t _line = 0;
        size_t _cursor = 0;
        size_t _max_line = 0;
        // values are in logical pixels
        int _avg_char_width = 0;
        int _char_height = 0;
        int _caret_x = 0;
        int _caret_y = 0;

        std::function<void(const wchar_t*)> _on_command;
    };

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        // based on https://docs.microsoft.com/en-us/windows/win32/inputdev/using-keyboard-input#processing-character-messages

        auto state = reinterpret_cast<State*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        if(!state)
        {
            state = new State;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, LONG_PTR(state));
        }

        switch(message)
        {
        case WM_CREATE:
        {
            const auto dc = GetDC(hWnd);
            TEXTMETRIC tm;
            GetTextMetrics(dc, &tm);
            ReleaseDC(hWnd, dc);

            state->_char_height = tm.tmHeight;
            state->_avg_char_width = tm.tmAveCharWidth;
        }
        break;
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            for(const auto& line : state->_lines)
            {
                delete[] line._buffer;
            }
            delete state;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        }
        break;
        case WM_SIZE:
        {
            if(!state->_lines.empty())
            {
                const auto line_width = 1 + LOWORD(lParam) / state->_avg_char_width;
                // only modify the current line, other lines are untouched
                auto& line = state->_lines[state->_line];
                if(line._capacity < line_width)
                {
                    const auto old_capacity = line._capacity;
                    line._capacity = line_width + line_width / 4;
                    auto tmp = new wchar_t[line._capacity];
                    memcpy(tmp, line._buffer, old_capacity);
                    delete[] line._buffer;
                    line._buffer = tmp;
                }
            }
        }
        break;
        case WM_SETFOCUS:
        {
            CreateCaret(hWnd, HBITMAP(1), 0, state->_char_height);
            SetCaretPos(state->_caret_x, state->_caret_y * state->_char_height);
            ShowCaret(hWnd);
        }
        break;
        case WM_KILLFOCUS:
        {
            HideCaret(hWnd);
            DestroyCaret();
        }
        break;
        case WM_KEYDOWN:
        {
            switch(wParam)
            {
            case VK_LEFT:
            {
                if(state->_cursor)
                {
                    HideCaret(hWnd);

                    // move caret back one character
                    const auto prev_char = state->_lines[state->_line]._buffer[--state->_cursor];
                    int char_width = 0;
                    GetCharWidth32(TmpDc(hWnd), prev_char, prev_char, &char_width);
                    state->_caret_x = std::max<int>(0, state->_caret_x - char_width);

                    ShowCaret(hWnd);
                    SetCaretPos(state->_caret_x, state->_caret_y);
                }
            }
            break;
            case VK_RIGHT:
            {
                if(state->_cursor < state->_lines[state->_line]._length)
                {
                    HideCaret(hWnd);
                    const auto this_char = state->_lines[state->_line]._buffer[state->_cursor++];
                    int char_width = 0;
                    GetCharWidth32(TmpDc(hWnd), this_char, this_char, &char_width);
                    state->_caret_x += char_width;
                    ShowCaret(hWnd);
                    SetCaretPos(state->_caret_x, state->_caret_y);
                }
            }
            break;
            case VK_HOME:
            {
                state->_cursor = 0;
                state->_caret_x = 0;
                SetCaretPos(state->_caret_x, state->_caret_y);
            }
            break;
            case VK_END:
            {
                if(!state->_lines.empty())
                {
                    state->_cursor = state->_lines[state->_line]._length;
                    state->_caret_x = state->_lines[state->_line]._caret_end;
                    SetCaretPos(state->_caret_x, state->_caret_y);
                }
            }
            break;
            default:;
            }
        }
        break;
        case WM_CHAR:
        {
            if(state->_lines.empty())
                state->_lines.push_back({});

            auto& line = state->_lines[state->_line];
            const auto growline = [&state, &line]() {
                const auto old_capacity = line._capacity;
                line._capacity += 32;
                auto tmp = new wchar_t[line._capacity];
                memcpy(tmp, line._buffer, old_capacity * sizeof(wchar_t));
                delete[] line._buffer;
                line._buffer = tmp;
            };

            if(line._capacity - line._length == 0)
                growline();

            switch(wParam)
            {
            case VK_BACK:
            //linefeed? case 0x0a:
            case VK_ESCAPE:
            case VK_TAB:
            case VK_RETURN:
                //TODO:
                MessageBeep(UINT(-1));
                break;
            default:
            {
                const auto ch = TCHAR(wParam);

                if(line._length == line._capacity - 1)
                    growline();

                RECT cr;
                GetClientRect(hWnd, &cr);

                HideCaret(hWnd);
                const auto dc = GetDC(hWnd);
                Graphics graphics(dc);

                if(app::dark_theme::_consolasRegularGdi == 0)
                {
                    LOGFONTW logFont;
                    app::dark_theme::_consolasRegular->GetLogFontW(&graphics, &logFont);
                    app::dark_theme::_consolasRegularGdi = CreateFontIndirectW(&logFont);
                }
                const auto currFont = SelectObject(dc, app::dark_theme::_consolasRegularGdi);
                int char_width;
                GetCharWidth32(dc, UINT(ch), UINT(ch), &char_width);

                if(state->_caret_x + char_width <= cr.right)
                {
                    //TODO: insert
                    line._buffer[line._length] = ch;
                    line._buffer[++line._length] = 0;
                    ++state->_cursor;

                    state->_caret_x += char_width;

                    //NOTE: officialy GDI+'s font rendering is garbage. This is a known fact.
                    //graphics.DrawString((const WCHAR*)(line._buffer + line._length - 1), 1, app::dark_theme::_consolasRegular, PointF(Gdiplus::REAL(state->_caret_x - char_width), 0.0), app::dark_theme::_lightGrayBrush);

                    const auto bkMode = GetBkMode(dc);
                    SetBkMode(dc, TRANSPARENT); 
                    const auto currTextColor = GetTextColor(dc);
                    SetTextColor(dc, app::dark_theme::kLightGray);
                    TextOut(dc, state->_caret_x - char_width, state->_caret_y, (const WCHAR*)(line._buffer + line._length - 1), 1);
                    SetTextColor(dc, currTextColor);
                    SetBkMode(dc, bkMode); 

                    line._caret_end = state->_caret_x;
                    SetCaretPos(state->_caret_x, state->_caret_y);
                }
                else
                {
                    MessageBeep(UINT(-1));
                }
                SelectObject(dc, currFont);
                ShowCaret(hWnd);
            }
            break;
            }
            break;
        }
        break;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            Graphics graphics(hdc);

            //TODO: how do we clip this, which part do we render?

            EndPaint(hWnd, &ps);
        }
        break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }

        return LRESULT(0);
    }

    HWND Create(HWND parent, HINSTANCE instance)
    {
        static ATOM klass = 0;
        if(!klass)
        {
            WNDCLASSEXW wcex;
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProc;
            wcex.cbClsExtra = 0;
            wcex.cbWndExtra = 0;
            wcex.hInstance = instance;
            wcex.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDC_WINASM64));
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.hbrBackground = app::dark_theme::_legacyBlackBrush;
            wcex.lpszMenuName = nullptr;
            wcex.lpszClassName = TEXT("ASMEDIT");
            wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
            klass = RegisterClassExW(&wcex);
        }

        return CreateWindowEx(0, TEXT("ASMEDIT"), nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 0, 0,
            parent,
            nullptr,
            instance,
            nullptr);
    }
}

namespace app
{
    LRESULT CALLBACK AsmEditorWndProc(HWND hWnd, UINT message, WPARAM wParam,
        LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        static auto handling_linebreak = false;
        switch(message)
        {
        case WM_KEYDOWN:
        {
            const auto line = DWORD(SendMessage(_asm_editor, EM_LINEFROMCHAR, WPARAM(-1), 0));
            switch(wParam)
            {
            case VK_RETURN:
            {
                if(line == (lines - 1))
                {
                    AppendLineNumber();
                    AppendText(_codes, L"blahblahblahblahblah\r\n");
                }
                else
                {
                    handling_linebreak = true;
                    return 0;
                }
            }
            break;
            default:
                break;
            }
            break;
        }
        break;
        case WM_CHAR:
        {
            if(wParam == VK_RETURN && handling_linebreak)
            {
                handling_linebreak = false;
                return 0;
            }
        }
        break;
        case WM_RBUTTONDOWN:
        {
            // get position of nearest character under cursor
            const auto charpos = SendMessage(_asm_editor, EM_CHARFROMPOS, 0, lParam);
            const auto x = LOWORD(charpos);
            const auto y = HIWORD(charpos);
            // use EM_GETLINE to get line y of text...look up character at x
        }
        break;
        default:;
        }
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void UpdateDpiInfo()
    {
        const auto desktopDc = GetDC(nullptr);
        _dpi._horisontal = GetDeviceCaps(desktopDc, LOGPIXELSX);
        _dpi._vertical = GetDeviceCaps(desktopDc, LOGPIXELSY);
    }

    bool Initialise(HINSTANCE instance, int nCmdShow)
    {
        // Initialize GDI+.
        GdiplusStartup(&_gdiplusToken, &_gdiplusStartupInput, nullptr);

        constexpr auto kMaxLoadstring = 100;
        WCHAR szTitle[kMaxLoadstring];
        WCHAR szWindowClass[kMaxLoadstring];
        LoadStringW(instance, IDS_APP_TITLE, szTitle, kMaxLoadstring);
        LoadStringW(instance, IDC_WINASM64, szWindowClass, kMaxLoadstring);

        dark_theme::_legacyBlackBrush = CreateSolidBrush(dark_theme::kAlmostBlack);
        dark_theme::_consolasFontFamily = new FontFamily(L"Consolas");
        dark_theme::_consolasRegular = new Font(dark_theme::_consolasFontFamily, 14, FontStyleRegular, UnitPixel);
        dark_theme::_consolasSmall = new Font(dark_theme::_consolasFontFamily, 11, FontStyleRegular, UnitPixel);
        dark_theme::_blackBrush = new SolidBrush(dark_theme::_almostBlack);
        dark_theme::_darkGrayBrush = new SolidBrush(dark_theme::_darkGray);
        dark_theme::_lightGrayBrush = new SolidBrush(dark_theme::_lightGray);
        dark_theme::_redBrush = new SolidBrush(Color::Red);
        dark_theme::_whitePen = new Pen(Color(255, 255, 255, 255));        
        dark_theme::_lightGrayPen = CreatePen(PS_SOLID,1,dark_theme::kLightGray);

        UpdateDpiInfo();

        WNDCLASSEXW wcex;
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = instance;
        wcex.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDC_WINASM64));
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = dark_theme::_legacyBlackBrush;
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = szWindowClass;
        wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

        if(RegisterClassExW(&wcex))
        {
            const auto style = 0;  //WS_THICKFRAME | WS_SYSMENU;

            constexpr auto kWindowWidth = 1920;
            constexpr auto kWindowHeight = 1080;

            _winasm64 = CreateWindow(szWindowClass, szTitle, style, CW_USEDEFAULT,
                CW_USEDEFAULT, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, nullptr);
            if(!_winasm64)
                return false;

            /*_asm_editor = CreateWindowEx(0, L"EDIT",
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT |
                    ES_MULTILINE | ES_AUTOVSCROLL,
                0, 0, 0, 0,
                _winasm64,
                nullptr,
                instance,
                nullptr);
            if(!_asm_editor)
                return false;
                SetWindowSubclass(_asm_editor, AsmEditorWndProc, 0, 0);
                */
            _asm_editor = LineEditor::Create(_winasm64, instance);

            _linecounter = CreateWindowEx(0, L"EDIT",
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT |
                    ES_MULTILINE | ES_AUTOVSCROLL,
                0, 0, 0, 0,
                _winasm64,
                nullptr,
                instance,
                nullptr);
            if(!_linecounter)
                return false;
            SendMessage(_linecounter, EM_SETREADONLY, TRUE, 0);
            AppendLineNumber();

            _codes = CreateWindowEx(0, L"EDIT",
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT |
                    ES_MULTILINE | ES_AUTOVSCROLL,
                0, 0, 0, 0,
                _winasm64,
                nullptr,
                instance,
                nullptr);
            if(!_codes)
                return false;
            SendMessage(_codes, EM_SETREADONLY, TRUE, 0);

            _output = CreateWindowEx(0, L"EDIT",
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT |
                    ES_MULTILINE | ES_AUTOVSCROLL,
                0, 0, 0, 0,
                _winasm64,
                nullptr,
                instance,
                nullptr);
            if(!_output)
                return false;
            SendMessage(_output, EM_SETREADONLY, TRUE, 0);

            // the non-client area font is nice and crisp so we'll use it for everything
            NONCLIENTMETRICS ncm;
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            HFONT hNewFont = CreateFontIndirect(&ncm.lfMessageFont);
            SendMessage(_asm_editor, WM_SETFONT, (WPARAM)hNewFont, 0);
            SendMessage(_linecounter, WM_SETFONT, (WPARAM)hNewFont, 0);
            SendMessage(_codes, WM_SETFONT, (WPARAM)hNewFont, 0);
            SendMessage(_output, WM_SETFONT, (WPARAM)hNewFont, 0);

#ifdef WIP
            WNDCLASS wc{ sizeof(wc) };
            wc.hInstance = instance;
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.hCursor = nullptr;
            wc.hbrBackground = nullptr;
            wc.lpszClassName = TEXT("GlowFrame");
            wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
                switch(msg)
                {
                case WM_PAINT:
                {
                    PAINTSTRUCT ps{};
                    HDC hdc = BeginPaint(hwnd, &ps);
                    Graphics graphics(hdc);
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    Pen blue{ Color::CornflowerBlue, 20.0f };
                    SolidBrush brush{ Color::CornflowerBlue };
                    Rect gr{ rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top };
                    graphics.DrawRectangle(&blue, gr);
                    EndPaint(hwnd, &ps);
                }
                break;

                case WM_DESTROY:
                    PostQuitMessage(0);
                    break;

                case WM_NCHITTEST:
                    return HTCAPTION;  // to be able to drag the window around
                    break;

                default:
                    return DefWindowProcW(hwnd, msg, wp, lp);
                }

                return 0;
            };
            RegisterClass(&wc);

            RECT mainRect;
            GetWindowRect(_winasm64, &mainRect);
            _glowBorder = CreateWindowEx(WS_EX_LAYERED, wc.lpszClassName, L"", WS_POPUP,
                mainRect.top - 1, mainRect.left - 1, kWindowWidth + 1, kWindowHeight + 2, nullptr, nullptr, instance, nullptr);
            SetLayeredWindowAttributes(_glowBorder, RGB(255, 0, 255), 255, LWA_COLORKEY);
            ShowWindow(_glowBorder, nCmdShow);
#endif

            ShowWindow(_winasm64, nCmdShow);
            UpdateWindow(_winasm64);

            // ==============================================================
            // EXPERIMENTAL

            // blur-behind (not working?)
            DWM_BLURBEHIND bb = { 0 };
            bb.dwFlags = DWM_BB_ENABLE;
            bb.fEnable = true;
            bb.hRgnBlur = NULL;
            DwmEnableBlurBehindWindow(_winasm64, &bb);

            // enable "sheet-of-glass" effect, extend client area to all of window
            MARGINS margins = { -1 };
            BOOL composition_enabled = FALSE;
            if(DwmIsCompositionEnabled(&composition_enabled) == S_OK)
                DwmExtendFrameIntoClientArea(_winasm64, &margins);

            return true;
        }
        return false;
    }

    void Shutdown()
    {
        DestroyWindow(_asm_editor);
        DestroyWindow(_linecounter);
        DestroyWindow(_codes);
        DestroyWindow(_output);

        dark_theme::Shutdown();
        if(_gdiplusToken)
            GdiplusShutdown(_gdiplusToken);
    }

    void Layout(HWND hWnd, WPARAM wParam, LPARAM lParam)
    {
        _layout._window_width = LOWORD(lParam);
        _layout._window_height = HIWORD(lParam);
        _layout._output_window_height = _layout._window_height / 4;
        _layout._top_panel_height = _layout._window_height - _layout._output_window_height;

        const auto hdc = GetDC(hWnd);
        TEXTMETRIC textMetric;
        GetTextMetrics(hdc, &textMetric);
        _layout._linecount_window_width = 2 + (unsigned short)(textMetric.tmMaxCharWidth) + 2;
        _layout._code_window_width = _layout._window_width / 4;
        _layout._asm_window_width = _layout._window_width - _layout._code_window_width - _layout._linecount_window_width;

        MoveWindow(_linecounter, 0, 0,
            _layout._linecount_window_width, _layout._top_panel_height, TRUE);
        ShowScrollBar(_linecounter, SB_BOTH, FALSE);

        MoveWindow(_asm_editor, _layout._linecount_window_width, 0,
            _layout._asm_window_width, _layout._top_panel_height, TRUE);

        MoveWindow(_codes, _layout._asm_window_width + _layout._linecount_window_width, 0,
            _layout._code_window_width, _layout._top_panel_height, TRUE);
        ShowScrollBar(_codes, SB_BOTH, FALSE);

        MoveWindow(_output, 0, _layout._top_panel_height,
            _layout._window_width, _layout._window_height / 4, TRUE);
        ShowScrollBar(_output, SB_BOTH, FALSE);
    }

    LRESULT NcPaint(HWND hWnd, WPARAM wParam, LPARAM lParam)
    {
        // known issue; straight forward rendering using the region apparently doesn't work
        // https://stackoverflow.com/questions/50132757/how-to-correctly-draw-simple-non-client-area-4-px-red-border
        RECT rect;
        GetWindowRect(hWnd, &rect);
        HRGN region = NULL;
        if(wParam == NULLREGION)
        {
            region = CreateRectRgn(rect.left, rect.top, rect.right, rect.bottom);
        }
        else
        {
            HRGN copy = CreateRectRgn(0, 0, 0, 0);
            if(CombineRgn(copy, (HRGN)wParam, NULL, RGN_COPY))
            {
                region = copy;
            }
            else
            {
                DeleteObject(copy);
            }
        }
        HDC dc = GetDCEx(hWnd, region, DCX_WINDOW | DCX_CACHE | DCX_INTERSECTRGN | DCX_LOCKWINDOWUPDATE);
        if(!dc && region)
        {
            DeleteObject(region);
            return 0;
        }

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        RECT cr;
        GetClientRect(hWnd, &cr);
        _layout._caption_height = height - (unsigned short)(cr.bottom) - 1;

        /*HGDIOBJ old = SelectObject(dc, HBRUSH(COLOR_WINDOW+1));
		Rectangle(dc, 0, 0, width, height);
		SelectObject(dc, old);*/

        Rect captionRect(0, 0, width, _layout._caption_height);
        {
            Graphics graphics(dc);
            graphics.FillRectangle(dark_theme::_darkGrayBrush, captionRect);
            graphics.DrawString(TEXT("inasm64"), -1, dark_theme::_consolasRegular, PointF(8.0, 4.0), dark_theme::_lightGrayBrush);

            _layout._resize_hit_area = { width - 32, height - 32, width, height };

            width -= 4;
            _layout._close_hit_area = { width - _layout._caption_height, 0, width - 2, _layout._caption_height };
            dark_theme::DrawFrameControl(dc, DFCS_CAPTIONCLOSE);
            width -= _layout._caption_height + 4;
            _layout._capacityimise_hit_area = { width - _layout._caption_height, 0, width - 2, _layout._caption_height };
            dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMAX);
            width -= _layout._caption_height + 4;
            _layout._minimise_hit_area = { width - _layout._caption_height, 0, width - 2, _layout._caption_height };
            dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMIN);
        }

        dark_theme::CheckDisableFrameControls(dc);

        ReleaseDC(hWnd, dc);
        return 0;
    }

    LRESULT NcCalc(HWND hWnd, WPARAM wParam, LPARAM lParam)
    {
        LRESULT result = 0;
        if(wParam == TRUE)
        {
            LPNCCALCSIZE_PARAMS pncc = LPNCCALCSIZE_PARAMS(lParam);
            // on entry:
            //pncc->rgrc[0] is the new rectangle
            //pncc->rgrc[1] is the old rectangle
            //pncc->rgrc[2] is the old client rectangle
            result = DefWindowProc(hWnd, WM_NCCALCSIZE, wParam, lParam);

            // on exit:
            //pncc->rgrc[0] is the new client rectangle
            //pncc->rgrc[1] is the new rectangle
            //pncc->rgrc[2] is the old rectangle

            // set client area to the whole width of the window
            pncc->rgrc[0].left = pncc->rgrc[1].left;
            pncc->rgrc[0].right = pncc->rgrc[1].right;
            //pncc->rgrc[0].top += 8;
            pncc->rgrc[0].bottom += 8;
        }
        else
        {
            LPRECT prect = LPRECT(lParam);
            // on entry:
            // prect is the new window rectangle
            const auto prev_left = prect->left;
            const auto prev_right = prect->right;
            result = DefWindowProc(hWnd, WM_NCCALCSIZE, wParam, lParam);
            // on exit:
            // prect is the new client area (in screen coordinates)

            // set client area to the whole width of the window
            prect->left = prev_left;
            prect->right = prev_right;
            prect->bottom += 8;
        }

        return result;
    }

    LRESULT NcHittest(HWND hWnd, WPARAM wParam, LPARAM lParam)
    {
        auto pt = MAKEPOINTS(lParam);

        const auto check_pt = [&pt](const RECT& rect) -> bool {
            return pt.x >= rect.left && pt.x < rect.right && pt.y >= rect.top && pt.y < rect.bottom;
        };

        const auto dc = GetWindowDC(hWnd);
        RECT wr;
        GetWindowRect(hWnd, &wr);

        if(check_pt(wr))
        {
            pt.x -= (unsigned short)(wr.left);
            pt.y -= (unsigned short)(wr.top);
            // we only care about the non-client caption area, everything else we'll send to DefWindowProc
            if(pt.y <= _layout._caption_height)
            {
                if(check_pt(_layout._minimise_hit_area))
                {
                    dark_theme::CheckDisableFrameControl(dc, DFCS_CAPTIONMAX);
                    dark_theme::CheckDisableFrameControl(dc, DFCS_CAPTIONCLOSE);
                    if(!_flags._mouse_on_minimise)
                    {
                        _flags._mouse_on_minimise = true;
                        dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMIN);
                    }
                    return HTMINBUTTON;
                }
                if(check_pt(_layout._capacityimise_hit_area))
                {
                    dark_theme::CheckDisableFrameControl(dc, DFCS_CAPTIONMIN);
                    dark_theme::CheckDisableFrameControl(dc, DFCS_CAPTIONCLOSE);
                    if(!_flags._mouse_on_capacityimise)
                    {
                        _flags._mouse_on_capacityimise = true;
                        dark_theme::DrawFrameControl(dc, DFCS_CAPTIONMAX);
                    }
                    return HTMAXBUTTON;
                }
                if(check_pt(_layout._close_hit_area))
                {
                    dark_theme::CheckDisableFrameControl(dc, DFCS_CAPTIONMAX);
                    dark_theme::CheckDisableFrameControl(dc, DFCS_CAPTIONMIN);
                    if(!_flags._mouse_on_close)
                    {
                        _flags._mouse_on_close = true;
                        dark_theme::DrawFrameControl(dc, DFCS_CAPTIONCLOSE);
                    }
                    return HTCLOSE;
                }

                dark_theme::CheckDisableFrameControls(dc);
                return HTCAPTION;
            }
            // not in caption area

            if(check_pt(_layout._resize_hit_area))
            {
                return HTBOTTOMRIGHT;
            }
        }

        // if we're leaving, or not in, the caption area we need to make sure frame controls are unselected otherwise they'll just stay hangin'
        dark_theme::CheckDisableFrameControls(dc);

        return DefWindowProc(hWnd, WM_NCHITTEST, wParam, lParam);
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch(message)
        {
        case WM_CTLCOLOREDIT:
        {
            // this message is sent for the non-readonly control (asm window) only

            HWND hwnd = HWND(lParam);
            HDC hdc = HDC(wParam);
            if(hwnd == _asm_editor)
                return LRESULT(dark_theme::Select(hdc, Panel::kAsmEditor));
        }
        break;
        case WM_CTLCOLORSTATIC:
        {
            HWND hwnd = HWND(lParam);
            HDC hdc = HDC(wParam);
            if(hwnd == _codes)
                return LRESULT(dark_theme::Select(hdc, Panel::kCodes));
            if(hwnd == _output)
                return LRESULT(dark_theme::Select(hdc, Panel::kOutput));
        }
        break;
        case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch(wmId)
            {
            case IDM_ABOUT:
                //TODO: DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:;
            }
        }
        break;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;
        case WM_SETFOCUS:
        {
            _flags._has_focus = true;
            SetFocus(_asm_editor);
        }
        break;
        case WM_KILLFOCUS:
        {
            _flags._has_focus = false;
        }
        break;
        // --------------------------------------------------------------------------
        // here follows all the NC handling we have to do to support a custom theme
        case WM_NCHITTEST:
        {
            return NcHittest(hWnd, wParam, lParam);
        }
        break;
        case WM_NCCALCSIZE:
        {
            return NcCalc(hWnd, wParam, lParam);
        }
        break;
        case WM_NCPAINT:
        {
            return NcPaint(hWnd, wParam, lParam);
        }
        break;
        case WM_NCACTIVATE:
            RedrawWindow(hWnd, NULL, NULL, RDW_UPDATENOW);
            return 0;
        case WM_NCMOUSELEAVE:
            dark_theme::CheckDisableFrameControls(GetWindowDC(hWnd));
            return 0;
        case WM_NCLBUTTONDOWN:
        {
            if(_flags._mouse_on_close || _flags._mouse_on_capacityimise || _flags._mouse_on_minimise)
            {
                if(_flags._mouse_on_capacityimise)
                {
                    SendMessage(hWnd, WM_SYSCOMMAND, _flags._needs_restore ? SC_RESTORE : SC_MAXIMIZE, 0);
                    _flags._needs_restore = !_flags._needs_restore;
                }
                else if(_flags._mouse_on_minimise)
                {
                    SendMessage(hWnd, WM_SYSCOMMAND, _flags._needs_restore ? SC_RESTORE : SC_MINIMIZE, 0);
                    _flags._needs_restore = !_flags._needs_restore;
                }
                else
                {
                    // it's got to be close then...
                    SendMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
                }
            }
        }
        break;
        case WM_SIZE:
        {
            Layout(hWnd, wParam, lParam);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

}  // namespace app

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    if(!app::Initialise(hInstance, nCmdShow))
        return FALSE;

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINASM64));
    MSG msg;
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        if(!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    app::Shutdown();
    return (int)msg.wParam;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch(message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if(LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
