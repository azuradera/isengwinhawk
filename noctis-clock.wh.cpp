// ==WindhawkMod==
// @id              noctis-clock
// @name            NOCTIS CLOCK
// @description     Elegant multi-style desktop clock overlay.
// @version         1.7.0
// @author          Bayu
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lgdi32 -luser32
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- ClockStyle: "noctis"
  $name: Gaya Jam
  $description: Pilih tampilan NOCTIS CLOCK.
  $options:
  - noctis: Noctis
  - thursday: Thursday
  - japanese: Japanese
  - overlap: Overlap
- ColorTheme: "light"
  $name: Tema Warna
  $description: Pilih warna widget agar kontras dengan wallpaper.
  $options:
  - light: Terang (putih, untuk wallpaper gelap)
  - dark: Gelap (hitam, untuk wallpaper terang)
- EnableDrag: true
  $name: Aktifkan Drag
  $description: Izinkan widget digeser menggunakan mouse.
- EnableResize: true
  $name: Aktifkan Resize
  $description: Izinkan widget diperbesar/diperkecil. Scroll mouse di atas widget, atau tahan klik kanan lalu geser kiri/kanan. Klik tengah untuk reset ke 100%.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <cwchar>

static HWND g_window = nullptr;
static HANDLE g_thread = nullptr;
static DWORD g_threadId = 0;
static volatile bool g_unloading = false;

static const COLORREF KEY_COLOR = RGB(1, 0, 1);

static const int DEFAULT_POS_X = 100;
static const int DEFAULT_POS_Y = 390;

static int g_posX = DEFAULT_POS_X;
static int g_posY = DEFAULT_POS_Y;

// Ukuran dasar (pada skala 100%)
static const int g_width = 520;
static const int g_height = 300;

static const int SCALE_MIN = 50;
static const int SCALE_MAX = 300;
static const int SCALE_DEFAULT = 100;
static const int SCALE_STEP = 5;

static int g_scale = SCALE_DEFAULT; // dalam persen

static bool g_dragging = false;
static bool g_resizing = false;
static bool g_enableDrag = true;
static bool g_enableResize = true;
static int g_clockStyle = 0;
static bool g_darkTheme = false;
static POINT g_dragOffset = {0, 0};
static int g_resizeStartX = 0;
static int g_resizeStartScale = SCALE_DEFAULT;

static const UINT WM_NOCTIS_SETTINGS_CHANGED = WM_APP + 1;

static const wchar_t *Days[] = {
    L"MIN", L"SEN", L"SEL", L"RAB",
    L"KAM", L"JUM", L"SAB"};

static const wchar_t *EnglishDays[] = {
    L"SUN", L"MON", L"TUE", L"WED",
    L"THU", L"FRI", L"SAT"};

static const wchar_t *Months[] = {
    L"JANUARI", L"FEBRUARI", L"MARET",
    L"APRIL", L"MEI", L"JUNI",
    L"JULI", L"AGUSTUS", L"SEPTEMBER",
    L"OKTOBER", L"NOVEMBER", L"DESEMBER"};

static int ClampScale(int value)
{
    if (value < SCALE_MIN)
        return SCALE_MIN;
    if (value > SCALE_MAX)
        return SCALE_MAX;
    return value;
}

static int ScaledWidth()
{
    return g_width * g_scale / 100;
}

static int ScaledHeight()
{
    return g_height * g_scale / 100;
}

// Tema gelap = semua warna dibalik (putih -> hitam, abu terang -> abu gelap).
// Nilai minimum 24 dijaga agar tidak mendekati KEY_COLOR (transparan).
static COLORREF Themed(COLORREF c)
{
    if (!g_darkTheme)
    {
        return c;
    }

    int r = 255 - GetRValue(c);
    int g = 255 - GetGValue(c);
    int b = 255 - GetBValue(c);

    if (r < 24)
        r = 24;
    if (g < 24)
        g = 24;
    if (b < 24)
        b = 24;

    return RGB(r, g, b);
}

static void LoadSettings()
{
    g_enableDrag = Wh_GetIntSetting(L"EnableDrag") != 0;
    g_enableResize = Wh_GetIntSetting(L"EnableResize") != 0;

    PCWSTR style = Wh_GetStringSetting(L"ClockStyle");

    if (wcscmp(style, L"thursday") == 0 || wcscmp(style, L"1") == 0)
    {
        g_clockStyle = 1;
    }
    else if (wcscmp(style, L"japanese") == 0 || wcscmp(style, L"2") == 0)
    {
        g_clockStyle = 2;
    }
    else if (wcscmp(style, L"overlap") == 0 || wcscmp(style, L"3") == 0)
    {
        g_clockStyle = 3;
    }
    else
    {
        g_clockStyle = 0;
    }

    Wh_FreeStringSetting(style);

    PCWSTR theme = Wh_GetStringSetting(L"ColorTheme");
    g_darkTheme = wcscmp(theme, L"dark") == 0;
    Wh_FreeStringSetting(theme);
}

static void LoadPosition()
{
    g_posX = Wh_GetIntValue(L"PosX", DEFAULT_POS_X);
    g_posY = Wh_GetIntValue(L"PosY", DEFAULT_POS_Y);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (g_posX < vx || g_posX > vx + vw - 50)
    {
        g_posX = DEFAULT_POS_X;
    }

    if (g_posY < vy || g_posY > vy + vh - 50)
    {
        g_posY = DEFAULT_POS_Y;
    }

    Wh_Log(L"NOCTIS: Loaded position X=%d Y=%d", g_posX, g_posY);
}

static void SavePosition()
{
    Wh_SetIntValue(L"PosX", g_posX);
    Wh_SetIntValue(L"PosY", g_posY);
}

static void LoadScale()
{
    g_scale = ClampScale(Wh_GetIntValue(L"Scale", SCALE_DEFAULT));
    Wh_Log(L"NOCTIS: Loaded scale %d%%", g_scale);
}

static void SaveScale()
{
    Wh_SetIntValue(L"Scale", g_scale);
}

static void ApplyScale(HWND hwnd, int newScale)
{
    newScale = ClampScale(newScale);

    if (newScale == g_scale)
    {
        return;
    }

    g_scale = newScale;

    SetWindowPos(
        hwnd,
        nullptr,
        0,
        0,
        ScaledWidth(),
        ScaledHeight(),
        SWP_NOMOVE |
            SWP_NOZORDER |
            SWP_NOACTIVATE);

    InvalidateRect(hwnd, nullptr, FALSE);
}

static void ApplyInteractionSettings(HWND hwnd)
{
    if (!g_enableDrag && g_dragging)
    {
        g_dragging = false;
        ReleaseCapture();
    }

    if (!g_enableResize && g_resizing)
    {
        g_resizing = false;
        ReleaseCapture();
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    // Klik tembus hanya jika drag DAN resize dua-duanya dimatikan
    if (g_enableDrag || g_enableResize)
    {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    else
    {
        exStyle |= WS_EX_TRANSPARENT;
    }

    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(
        hwnd,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE |
            SWP_NOSIZE |
            SWP_NOZORDER |
            SWP_NOACTIVATE |
            SWP_FRAMECHANGED);
}

static HFONT CreateClockFont(
    int size,
    int weight,
    const wchar_t *family)
{
    return CreateFontW(
        -size,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        family);
}

static void DrawTextStyled(
    HDC dc,
    const wchar_t *text,
    int x,
    int y,
    int size,
    int weight,
    COLORREF color,
    const wchar_t *family)
{
    HFONT font = CreateClockFont(size, weight, family);

    if (!font)
    {
        return;
    }

    HFONT oldFont = (HFONT)SelectObject(dc, font);

    SetTextColor(dc, Themed(color));
    SetBkMode(dc, TRANSPARENT);

    TextOutW(dc, x, y, text, (int)wcslen(text));

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

static void DrawTextCentered(
    HDC dc,
    const wchar_t *text,
    int centerX,
    int y,
    int size,
    int weight,
    COLORREF color,
    const wchar_t *family)
{
    HFONT font = CreateClockFont(size, weight, family);

    if (!font)
    {
        return;
    }

    HFONT oldFont = (HFONT)SelectObject(dc, font);

    SIZE textSize = {};
    GetTextExtentPoint32W(
        dc,
        text,
        (int)wcslen(text),
        &textSize);

    SetTextColor(dc, Themed(color));
    SetBkMode(dc, TRANSPARENT);

    TextOutW(
        dc,
        centerX - textSize.cx / 2,
        y,
        text,
        (int)wcslen(text));

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

static void DrawLine(
    HDC dc,
    int x1,
    int y1,
    int x2,
    int y2,
    int thickness,
    COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, thickness, Themed(color));

    if (!pen)
    {
        return;
    }

    HPEN oldPen = (HPEN)SelectObject(dc, pen);

    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void DrawNoctis(HDC dc, const SYSTEMTIME &time)
{
    int hour = time.wHour % 12;

    if (hour == 0)
    {
        hour = 12;
    }

    wchar_t clock[32];
    wchar_t date[64];

    swprintf_s(
        clock,
        L"%d:%02d %s",
        hour,
        time.wMinute,
        time.wHour >= 12 ? L"PM" : L"AM");

    swprintf_s(
        date,
        L"%02d %s %04d",
        time.wDay,
        Months[time.wMonth - 1],
        time.wYear);

    POINT panel[] = {
        {235, 72},
        {455, 72},
        {455, 141},
        {205, 141}};

    HBRUSH white = CreateSolidBrush(Themed(RGB(248, 248, 248)));
    HPEN whitePen = CreatePen(PS_SOLID, 1, Themed(RGB(248, 248, 248)));

    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, white);
    HPEN oldPen = (HPEN)SelectObject(dc, whitePen);

    Polygon(dc, panel, 4);

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);

    DeleteObject(white);
    DeleteObject(whitePen);

    DrawTextStyled(
        dc,
        Days[time.wDayOfWeek],
        0,
        104,
        88,
        FW_BOLD,
        RGB(245, 245, 245),
        L"Arial");

    DrawTextStyled(
        dc,
        clock,
        263,
        89,
        35,
        FW_BOLD,
        RGB(55, 55, 55),
        L"Arial");

    DrawTextStyled(
        dc,
        date,
        225,
        153,
        15,
        FW_BOLD,
        RGB(245, 245, 245),
        L"Arial");

    DrawLine(
        dc,
        226,
        39,
        130,
        255,
        2,
        RGB(245, 245, 245));
}

static void DrawThursday(HDC dc, const SYSTEMTIME &time)
{
    wchar_t day[32];
    wchar_t date[64];
    wchar_t clock[32];

    swprintf_s(
        day,
        L"%s",
        Days[time.wDayOfWeek]);

    swprintf_s(
        date,
        L"%02d %s %04d",
        time.wDay,
        Months[time.wMonth - 1],
        time.wYear);

    int hour = time.wHour % 12;

    if (hour == 0)
    {
        hour = 12;
    }

    swprintf_s(
        clock,
        L"%02d:%02d",
        hour,
        time.wMinute);

    DrawTextCentered(
        dc,
        day,
        260,
        35,
        100,
        600,
        RGB(245, 245, 245),
        L"Oxanium");

    DrawTextCentered(
        dc,
        date,
        260,
        139,
        16,
        500,
        RGB(190, 190, 190),
        L"Oxanium");

    DrawLine(
        dc,
        175,
        170,
        345,
        170,
        1,
        RGB(130, 130, 130));

    DrawTextCentered(
        dc,
        clock,
        260,
        184,
        43,
        400,
        RGB(245, 245, 245),
        L"Oxanium");

    DrawTextCentered(
        dc,
        time.wHour >= 12 ? L"PM" : L"AM",
        260,
        238,
        13,
        500,
        RGB(175, 175, 175),
        L"Oxanium");
}

static void DrawJapanese(HDC dc, const SYSTEMTIME &time)
{
    wchar_t date[64];
    wchar_t clock[32];

    int hour = time.wHour % 12;

    if (hour == 0)
    {
        hour = 12;
    }

    swprintf_s(
        date,
        L"%04d . %02d . %02d",
        time.wYear,
        time.wMonth,
        time.wDay);

    swprintf_s(
        clock,
        L"%02d:%02d",
        hour,
        time.wMinute);

    DrawLine(
        dc,
        260,
        24,
        260,
        55,
        1,
        RGB(220, 220, 220));

    DrawTextCentered(
        dc,
        time.wHour < 12 ? L"GOOD MORNING" : time.wHour < 18 ? L"GOOD AFTERNOON"
                                                            : L"GOOD EVENING",
        260,
        66,
        13,
        700,
        RGB(220, 220, 220),
        L"JAPAN RAMEN");

    DrawTextCentered(
        dc,
        EnglishDays[time.wDayOfWeek],
        260,
        99,
        72,
        700,
        RGB(245, 245, 245),
        L"JAPAN RAMEN");

    DrawTextCentered(
        dc,
        date,
        260,
        190,
        14,
        500,
        RGB(190, 190, 190),
        L"Zen Kaku Gothic New");

    DrawTextCentered(
        dc,
        clock,
        260,
        216,
        28,
        700,
        RGB(245, 245, 245),
        L"Zen Kaku Gothic New");

    DrawLine(
        dc,
        260,
        260,
        260,
        283,
        1,
        RGB(220, 220, 220));
}

static void DrawOverlap(HDC dc, const SYSTEMTIME &time)
{
    wchar_t hourText[8];
    wchar_t minuteText[8];
    wchar_t date[64];

    int hour = time.wHour % 12;

    if (hour == 0)
    {
        hour = 12;
    }

    swprintf_s(hourText, L"%02d", hour);
    swprintf_s(minuteText, L"%02d", time.wMinute);

    swprintf_s(
        date,
        L"%s   %02d %s %04d",
        Days[time.wDayOfWeek],
        time.wDay,
        Months[time.wMonth - 1],
        time.wYear);

    DrawTextStyled(
        dc,
        hourText,
        35,
        45,
        160,
        200,
        RGB(245, 245, 245),
        L"Montserrat");

    DrawTextStyled(
        dc,
        minuteText,
        205,
        45,
        160,
        200,
        RGB(245, 245, 245),
        L"Montserrat");

    DrawTextStyled(
        dc,
        time.wHour >= 12 ? L"PM" : L"AM",
        365,
        105,
        54,
        400,
        RGB(220, 220, 220),
        L"Allura");

    DrawLine(
        dc,
        42,
        220,
        478,
        220,
        1,
        RGB(110, 110, 110));

    DrawTextStyled(
        dc,
        date,
        48,
        235,
        15,
        500,
        RGB(225, 225, 225),
        L"Montserrat");
}

static void DrawClock(HDC dc)
{
    SYSTEMTIME time;
    GetLocalTime(&time);

    switch (g_clockStyle)
    {
    case 1:
        DrawThursday(dc, time);
        break;

    case 2:
        DrawJapanese(dc, time);
        break;

    case 3:
        DrawOverlap(dc, time);
        break;

    default:
        DrawNoctis(dc, time);
        break;
    }
}

static LRESULT CALLBACK ClockProc(
    HWND hwnd,
    UINT msg,
    WPARAM wp,
    LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        SetTimer(hwnd, 1, 1000, nullptr);
        ApplyInteractionSettings(hwnd);
        Wh_Log(L"NOCTIS: Clock window initialized");
        return 0;

    case WM_TIMER:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_NOCTIS_SETTINGS_CHANGED:
        ApplyInteractionSettings(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);

        Wh_Log(
            L"NOCTIS: Settings changed. Style=%d Dark=%d Drag=%d Resize=%d",
            g_clockStyle,
            g_darkTheme,
            g_enableDrag,
            g_enableResize);

        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        if (width > 0 && height > 0)
        {
            HDC memoryDC = CreateCompatibleDC(dc);

            if (memoryDC)
            {
                HBITMAP bitmap = CreateCompatibleBitmap(
                    dc,
                    width,
                    height);

                if (bitmap)
                {
                    HBITMAP oldBitmap = (HBITMAP)SelectObject(
                        memoryDC,
                        bitmap);

                    HBRUSH background = CreateSolidBrush(KEY_COLOR);

                    if (background)
                    {
                        FillRect(memoryDC, &rect, background);
                        DeleteObject(background);
                    }

                    // Skala seluruh gambar (font, garis, bentuk) lewat world transform
                    SetGraphicsMode(memoryDC, GM_ADVANCED);

                    XFORM xf = {};
                    xf.eM11 = g_scale / 100.0f;
                    xf.eM22 = g_scale / 100.0f;
                    SetWorldTransform(memoryDC, &xf);

                    DrawClock(memoryDC);

                    ModifyWorldTransform(memoryDC, nullptr, MWT_IDENTITY);

                    BitBlt(
                        dc,
                        0,
                        0,
                        width,
                        height,
                        memoryDC,
                        0,
                        0,
                        SRCCOPY);

                    SelectObject(memoryDC, oldBitmap);
                    DeleteObject(bitmap);
                }

                DeleteDC(memoryDC);
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!g_enableDrag || g_resizing)
        {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        g_dragging = true;
        SetCapture(hwnd);

        g_dragOffset.x = (short)LOWORD(lp);
        g_dragOffset.y = (short)HIWORD(lp);

        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        if (!g_enableResize || g_dragging)
        {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        POINT cursor;
        GetCursorPos(&cursor);

        g_resizing = true;
        g_resizeStartX = cursor.x;
        g_resizeStartScale = g_scale;
        SetCapture(hwnd);

        return 0;
    }

    case WM_MBUTTONDOWN:
    {
        if (g_enableResize)
        {
            ApplyScale(hwnd, SCALE_DEFAULT);
            SaveScale();
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_MOUSEWHEEL:
    {
        if (!g_enableResize)
        {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int steps = delta / WHEEL_DELTA;

        if (steps == 0)
        {
            steps = delta > 0 ? 1 : -1;
        }

        ApplyScale(hwnd, g_scale + steps * SCALE_STEP);
        SaveScale();

        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (g_dragging && g_enableDrag)
        {
            HWND parent = GetParent(hwnd);

            POINT cursor;
            GetCursorPos(&cursor);
            ScreenToClient(parent, &cursor);

            SetWindowPos(
                hwnd,
                nullptr,
                cursor.x - g_dragOffset.x,
                cursor.y - g_dragOffset.y,
                0,
                0,
                SWP_NOSIZE |
                    SWP_NOZORDER |
                    SWP_NOACTIVATE);
        }
        else if (g_resizing && g_enableResize)
        {
            POINT cursor;
            GetCursorPos(&cursor);

            // Geser sejauh lebar dasar = +100%
            int deltaPercent =
                (cursor.x - g_resizeStartX) * 100 / g_width;

            ApplyScale(hwnd, g_resizeStartScale + deltaPercent);
        }

        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (g_dragging)
        {
            g_dragging = false;
            ReleaseCapture();

            RECT r;
            GetWindowRect(hwnd, &r);

            POINT pos = {r.left, r.top};
            ScreenToClient(GetParent(hwnd), &pos);

            g_posX = pos.x;
            g_posY = pos.y;

            SavePosition();

            Wh_Log(
                L"NOCTIS: new position X=%ld Y=%ld",
                g_posX,
                g_posY);
        }

        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (g_resizing)
        {
            g_resizing = false;
            ReleaseCapture();

            SaveScale();

            Wh_Log(L"NOCTIS: new scale %d%%", g_scale);
        }

        return 0;
    }

    case WM_CAPTURECHANGED:
        g_dragging = false;
        g_resizing = false;
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        Wh_Log(L"NOCTIS: Clock window destroyed");
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL CALLBACK FindWorkerCallback(
    HWND hwnd,
    LPARAM parameter)
{
    HWND defView = FindWindowExW(
        hwnd,
        nullptr,
        L"SHELLDLL_DefView",
        nullptr);

    if (!defView)
    {
        return TRUE;
    }

    HWND worker = FindWindowExW(
        nullptr,
        hwnd,
        L"WorkerW",
        nullptr);

    if (worker)
    {
        *(HWND *)parameter = worker;
        return FALSE;
    }

    return TRUE;
}

static HWND FindDesktopWorkerW()
{
    HWND progman = FindWindowW(L"Progman", nullptr);

    if (!progman)
    {
        return nullptr;
    }

    DWORD_PTR result = 0;

    SendMessageTimeoutW(
        progman,
        0x052C,
        0xD,
        0,
        SMTO_NORMAL,
        1000,
        &result);

    HWND worker = nullptr;

    EnumWindows(
        FindWorkerCallback,
        (LPARAM)&worker);

    return worker;
}

static bool CreateClockWindow()
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = ClockProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NoctisClockWindow";

    if (!RegisterClassW(&wc))
    {
        DWORD error = GetLastError();

        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            Wh_Log(L"NOCTIS: RegisterClass failed: %lu", error);
            return false;
        }
    }

    HWND parent = FindDesktopWorkerW();

    if (!parent)
    {
        Wh_Log(L"NOCTIS: WorkerW not found, trying Progman");
        parent = FindWindowW(L"Progman", nullptr);
    }

    if (!parent)
    {
        Wh_Log(L"NOCTIS: No desktop parent available");
        return false;
    }

    Wh_Log(L"NOCTIS: Desktop parent: %p", parent);

    DWORD exStyle = WS_EX_LAYERED |
                    WS_EX_TOOLWINDOW |
                    WS_EX_NOACTIVATE;

    if (!g_enableDrag && !g_enableResize)
    {
        exStyle |= WS_EX_TRANSPARENT;
    }

    g_window = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        L"NOCTIS CLOCK",
        WS_CHILD |
            WS_VISIBLE,
        g_posX,
        g_posY,
        ScaledWidth(),
        ScaledHeight(),
        parent,
        nullptr,
        wc.hInstance,
        nullptr);

    if (!g_window)
    {
        Wh_Log(L"NOCTIS: CreateWindowEx failed: %lu", GetLastError());
        return false;
    }

    if (!SetLayeredWindowAttributes(
            g_window,
            KEY_COLOR,
            0,
            LWA_COLORKEY))
    {
        Wh_Log(
            L"NOCTIS: SetLayeredWindowAttributes failed: %lu",
            GetLastError());

        DestroyWindow(g_window);
        g_window = nullptr;
        return false;
    }

    SetWindowPos(
        g_window,
        HWND_TOP,
        g_posX,
        g_posY,
        ScaledWidth(),
        ScaledHeight(),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);

    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    UpdateWindow(g_window);

    Wh_Log(L"NOCTIS: Clock overlay displayed");

    return true;
}

static DWORD WINAPI ClockThread(LPVOID)
{
    PeekMessageW(nullptr, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    while (!g_unloading)
    {
        if (!CreateClockWindow())
        {
            for (int i = 0; i < 20 && !g_unloading; i++)
            {
                Sleep(500);
            }

            continue;
        }

        MSG msg;

        while (
            !g_unloading &&
            GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g_window)
        {
            DestroyWindow(g_window);
            g_window = nullptr;
        }

        break;
    }

    UnregisterClassW(
        L"NoctisClockWindow",
        GetModuleHandleW(nullptr));

    Wh_Log(L"NOCTIS: Clock thread stopped");

    return 0;
}

BOOL Wh_ModInit()
{
    g_unloading = false;

    LoadSettings();
    LoadPosition();
    LoadScale();

    Wh_Log(L"NOCTIS: Mod initialization started");

    return TRUE;
}

void Wh_ModAfterInit()
{
    Wh_Log(L"NOCTIS: Explorer initialization completed");

    g_thread = CreateThread(
        nullptr,
        0,
        ClockThread,
        nullptr,
        0,
        &g_threadId);

    if (!g_thread)
    {
        Wh_Log(
            L"NOCTIS: Failed to create thread: %lu",
            GetLastError());

        return;
    }

    Wh_Log(L"NOCTIS: Clock thread started");
}

void Wh_ModSettingsChanged()
{
    LoadSettings();

    if (g_window && IsWindow(g_window))
    {
        PostMessageW(
            g_window,
            WM_NOCTIS_SETTINGS_CHANGED,
            0,
            0);
    }
}

void Wh_ModUninit()
{
    Wh_Log(L"NOCTIS: Mod shutdown started");

    g_unloading = true;

    if (g_threadId)
    {
        PostThreadMessageW(
            g_threadId,
            WM_QUIT,
            0,
            0);
    }

    if (g_thread)
    {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }

    g_window = nullptr;
    g_threadId = 0;

    Wh_Log(L"NOCTIS: Mod shutdown completed");
}