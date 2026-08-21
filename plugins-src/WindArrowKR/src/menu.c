#include <windows.h>
#include "hook.h"

// 게임 창 "파일" 메뉴에 "풍향 화살표" 항목을 달고 체크로 켜고 끈다.
// 다른 KR 플러그인(WorldMapKR/ShipSkinKR)과 같은 방식이다 — 1초 폴링으로 게임 창을 찾아
// AppendMenu 하고, 서브클래싱해서 WM_COMMAND 를 ID 로 가로챈다.

#define ID_ARROW_TOGGLE 0xB800u   // Trade=0xB10x/0xC0xx, Char=0xB301, Ship=0xB410,
                                  // Patch=0xB500, Map=0xB600, Mod=0xB700 과 비충돌
#define MENU_LABEL L"풍향 화살표"

static HINSTANCE g_hinst;
static HWND      g_hwnd, g_subHwnd;
static WNDPROC   g_origProc;

static void SyncCheck(HWND h)
{
    HMENU bar = GetMenu(h);
    if (bar) CheckMenuItem(bar, ID_ARROW_TOGGLE,
                           MF_BYCOMMAND | (Overlay_Enabled() ? MF_CHECKED : MF_UNCHECKED));
}

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;
    if (msg == WM_COMMAND && HIWORD(wp) == 0 && LOWORD(wp) == ID_ARROW_TOGGLE) {
        Overlay_SetEnabled(!Overlay_Enabled());
        SyncCheck(h);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_hwnd = NULL;
        return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
    }
    return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0; (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_hwnd = h; return FALSE; }
    return TRUE;
}

static BOOL HasOurMenu(HMENU m)
{
    int n = GetMenuItemCount(m), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(m, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, MENU_LABEL) == 0)
            return TRUE;
    return FALSE;
}

// 최상위 메뉴바에서 "파일" 팝업을 찾는다. 실제 라벨은 "파일 (&F)" 처럼 니모닉이 붙는다.
static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}

// "파일" 안에 KR 플러그인 항목(ID 0xB000~0xCFFF)이 이미 있는지 → 최초 설치 플러그인만 구분선.
static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
}

// 플러그인들이 저마다 구분선을 넣는 race 로 여러 개가 생겨도 다음 폴링에서 하나로 접는다.
static BOOL CollapseSeparators(HMENU m)
{
    BOOL changed = FALSE; int i;
    for (i = GetMenuItemCount(m) - 1; i > 0; i--) {
        UINT a = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        UINT b = GetMenuState(m, (UINT)(i - 1), MF_BYPOSITION);
        if ((a & MF_SEPARATOR) && (b & MF_SEPARATOR)) { RemoveMenu(m, (UINT)i, MF_BYPOSITION); changed = TRUE; }
    }
    return changed;
}

static DWORD WINAPI MenuThread(LPVOID param)
{
    (void)param;
    OutputDebugStringW(L"[WindArrowKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_hwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_hwnd && (bar = GetMenu(g_hwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!HasOurMenu(target)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(target, MF_STRING, ID_ARROW_TOGGLE, MENU_LABEL);
                SyncCheck(g_hwnd);
                DrawMenuBar(g_hwnd);
                OutputDebugStringW(L"[WindArrowKR] menu installed.");
            }
            if (fileMenu && CollapseSeparators(fileMenu)) DrawMenuBar(g_hwnd);
            if (g_subHwnd != g_hwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_hwnd;
            }
        }
        Sleep(1000);
    }
}

void ArrowMenu_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
