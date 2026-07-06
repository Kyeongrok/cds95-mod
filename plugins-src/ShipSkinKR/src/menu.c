#include <windows.h>

// ShipSkinKR — 게임 창 메뉴바에 "함선" 항목 추가 → 클릭 시 함선 스프라이트 창(ship_window.c) 오픈.
// 기존 플러그인(TradeUtilKR/CharacterUtilKR) 패턴: MonitorThread 1초폴링 → EnumWindows 게임창 →
// AppendMenu(MF_STRING) → SetWindowLongPtr 서브클래스 → WM_COMMAND 를 ID로 분기.
// (fb5-1: 형태 선택/내보내기/불러오기는 전부 창 안에서 처리.)

#define ID_SHIP_OPEN  0xB410u          // Trade=0xB10x/0xC0xx, Char=0xB301 과 비충돌

void ShipWin_Show(HWND owner, HINSTANCE hinst);   // ship_window.c

static HINSTANCE g_hinst = NULL;
static HWND      g_hwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;
    if (msg == WM_COMMAND && HIWORD(wp) == 0 && LOWORD(wp) == ID_SHIP_OPEN)
    {
        ShipWin_Show(h, g_hinst);
        return 0;
    }
    if (msg == WM_NCDESTROY)
    {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_hwnd = NULL;
        return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
    }
    return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD wp = 0; (void)l;
    GetWindowThreadProcessId(h, &wp);
    if (wp == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_hwnd = h; return FALSE; }
    return TRUE;
}

static BOOL HasOurMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, L"함선") == 0) return TRUE;
    return FALSE;
}

static DWORD WINAPI ShipMenuThread(LPVOID param)
{
    (void)param;
    OutputDebugStringW(L"[ShipSkinKR] menu monitor started.");
    for (;;)
    {
        HMENU bar;
        g_hwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_hwnd && (bar = GetMenu(g_hwnd)) != NULL)
        {
            if (!HasOurMenu(bar))
            {
                AppendMenuW(bar, MF_STRING, ID_SHIP_OPEN, L"함선");   // 클릭 즉시 창 오픈(드롭다운 아님)
                DrawMenuBar(g_hwnd);
                OutputDebugStringW(L"[ShipSkinKR] 함선 menu installed.");
            }
            if (g_subHwnd != g_hwnd)
            {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_hwnd;
                OutputDebugStringW(L"[ShipSkinKR] window subclassed (menu).");
            }
        }
        Sleep(1000);
    }
}

void ShipMenu_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, ShipMenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
