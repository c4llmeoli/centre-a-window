// Build: cl /O2 CenterWindow.c /link /SUBSYSTEM:WINDOWS

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

#define IDC_LIST     101
#define IDC_REFRESH  102
#define IDC_CENTER   103
#define IDC_WORKAREA 104
#define IDC_STATUS   105

static HWND g_main, g_list, g_refresh, g_center, g_workarea, g_status;
static HFONT g_font;

static int Scale(int v) { return MulDiv(v, GetDpiForWindow(g_main), 96); }

static void SetStatus(const wchar_t *fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 512, _TRUNCATE, fmt, ap);
    va_end(ap);
    SetWindowTextW(g_status, buf);
}

static BOOL IsCloaked(HWND h)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked;
}

static void ExeName(HWND h, wchar_t *out, DWORD cap)
{
    DWORD pid = 0;
    out[0] = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return;
    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(p, 0, path, &len)) {
        const wchar_t *slash = wcsrchr(path, L'\\');
        wcsncpy_s(out, cap, slash ? slash + 1 : path, _TRUNCATE);
    }
    CloseHandle(p);
}

static BOOL CALLBACK AddWindow(HWND h, LPARAM lp)
{
    (void)lp;
    if (h == g_main || !IsWindowVisible(h) || IsCloaked(h)) return TRUE;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindow(h, GW_OWNER)) return TRUE;

    wchar_t title[256], exe[MAX_PATH], line[400];
    if (GetWindowTextW(h, title, 256) == 0) return TRUE;
    ExeName(h, exe, MAX_PATH);
    swprintf_s(line, 400, L"%s   [%s]", title, exe[0] ? exe : L"?");

    LRESULT i = SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)line);
    SendMessageW(g_list, LB_SETITEMDATA, i, (LPARAM)h);
    return TRUE;
}

static void Refresh(void)
{
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    EnumWindows(AddWindow, 0);
    SetStatus(L"%d windows. Select one and click Centre (or double-click it).",
              (int)SendMessageW(g_list, LB_GETCOUNT, 0, 0));
}

static void CenterSelected(void)
{
    LRESULT sel = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) { SetStatus(L"Select a window first."); return; }
    HWND h = (HWND)SendMessageW(g_list, LB_GETITEMDATA, sel, 0);
    if (!IsWindow(h)) { SetStatus(L"That window is gone. Refreshing."); Refresh(); return; }
    if (IsZoomed(h) || IsIconic(h)) ShowWindow(h, SW_RESTORE);

    RECT wr, vis;
    GetWindowRect(h, &wr);
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof vis)))
        vis = wr;

    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    BOOL useWork = SendMessageW(g_workarea, BM_GETCHECK, 0, 0) == BST_CHECKED;
    RECT area = useWork ? mi.rcWork : mi.rcMonitor;

    int areaW = area.right - area.left, areaH = area.bottom - area.top;
    int visW = vis.right - vis.left, visH = vis.bottom - vis.top;
    int newVisX = area.left + (areaW - visW) / 2;
    int newVisY = area.top + (areaH - visH) / 2;
    int dx = newVisX - vis.left, dy = newVisY - vis.top;

    if (!SetWindowPos(h, NULL, wr.left + dx, wr.top + dy, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        DWORD err = GetLastError();
        SetStatus(L"Move failed (error %lu).%s", err,
                  err == ERROR_ACCESS_DENIED ? L" That program runs as admin; run CenterWindow as admin too." : L"");
        return;
    }

    const wchar_t *odd = ((areaW - visW) & 1) || ((areaH - visH) & 1)
        ? L"  (odd leftover pixel: off by 1px is the closest possible)" : L"";
    SetStatus(L"Window %dx%d on %s %dx%d -> moved to (%d, %d).%s",
              visW, visH, useWork ? L"work area" : L"screen", areaW, areaH,
              newVisX - area.left, newVisY - area.top, odd);
}

static void Layout(void)
{
    RECT rc;
    GetClientRect(g_main, &rc);
    int m = Scale(8), bw = Scale(90), bh = Scale(24), sh = Scale(36);
    int w = rc.right, h = rc.bottom;
    int listBottom = h - m - sh - m - bh - m;

    MoveWindow(g_list, m, m, w - 2 * m, listBottom - m, TRUE);
    int by = listBottom + m;
    MoveWindow(g_refresh, m, by, bw, bh, TRUE);
    MoveWindow(g_center, m + bw + m, by, bw, bh, TRUE);
    MoveWindow(g_workarea, m + 2 * (bw + m), by, w - 3 * m - 2 * bw - m, bh, TRUE);
    MoveWindow(g_status, m, by + bh + m, w - 2 * m, sh, TRUE);
}

static void ApplyFont(void)
{
    if (g_font) DeleteObject(g_font);
    NONCLIENTMETRICSW ncm = { sizeof ncm };
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, GetDpiForWindow(g_main));
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    HWND kids[] = { g_list, g_refresh, g_center, g_workarea, g_status };
    for (int i = 0; i < 5; i++) SendMessageW(kids[i], WM_SETFONT, (WPARAM)g_font, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_main = hwnd;
        HINSTANCE hi = ((CREATESTRUCTW *)lp)->hInstance;
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, hwnd, (HMENU)IDC_LIST, hi, NULL);
        SendMessageW(g_list, LB_SETHORIZONTALEXTENT, 2000, 0);
        g_refresh = CreateWindowW(L"BUTTON", L"&Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_REFRESH, hi, NULL);
        g_center = CreateWindowW(L"BUTTON", L"&Centre", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_CENTER, hi, NULL);
        g_workarea = CreateWindowW(L"BUTTON", L"Ignore &taskbar (centre in work area)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd, (HMENU)IDC_WORKAREA, hi, NULL);
        g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, hi, NULL);
        ApplyFont();
        Layout();
        Refresh();
        return 0;
    }
    case WM_SIZE:
        Layout();
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        ApplyFont();
        Layout();
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 420;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 260;
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REFRESH: Refresh(); break;
        case IDC_CENTER:  CenterSelected(); break;
        case IDC_LIST:    if (HIWORD(wp) == LBN_DBLCLK) CenterSelected(); break;
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_DESTROY:
        if (g_font) DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"CenterWindowClass";
    RegisterClassW(&wc);

    UINT dpi = GetDpiForSystem();
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Centre a Window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(560, dpi, 96), MulDiv(420, dpi, 96),
        NULL, NULL, hi, NULL);
    ShowWindow(hwnd, show);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}
