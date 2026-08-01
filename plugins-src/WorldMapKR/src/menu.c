#include "mapwin.h"

// WorldMapKR — 게임 창 "파일" 메뉴에 "지도" 항목 추가 → 클릭 시 세계지도 창 오픈.
// 다른 KR 플러그인(CharacterUtilKR/ShipSkinKR)과 같은 방식이다:
// 1초 폴링으로 게임 창을 찾아 AppendMenu → 서브클래싱해서 WM_COMMAND 를 ID 로 가로챈다.

#define ID_MAP_OPEN 0xB600u   // Trade=0xB10x/0xC0xx, Char=0xB301, Ship=0xB410, Patch=0xB500 과 비충돌

static HINSTANCE g_hinst = NULL;
static HWND      g_hwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;
    if (msg == WM_COMMAND && HIWORD(wp) == 0 && LOWORD(wp) == ID_MAP_OPEN) {
        MapWin_Show(h, g_hinst);
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

static BOOL HasOurMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, L"지도") == 0) return TRUE;
    return FALSE;
}
// 최상위 메뉴바에서 "파일" 팝업 서브메뉴를 찾는다. 없으면 NULL.
static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    // 실제 라벨은 "파일 (&F)" 처럼 니모닉이 붙으므로 접두어로 매칭한다.
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}
// "파일" 안에 KR 플러그인 항목(ID 0xB000~0xCFFF)이 이미 있는지 → 최초 설치 플러그인만 구분선 추가.
static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
}
// 연속된 구분선을 1개로 접는다(변경했으면 TRUE). 플러그인 스레드들이 동시에 폴링하며
// 각자 구분선을 넣는 race 로 2~3개가 생겨도 다음 폴링에서 하나로 수렴시킨다.
static BOOL CollapseSeparators(HMENU m)
{
    BOOL changed = FALSE; int i;
    for (i = GetMenuItemCount(m) - 1; i > 0; i--) {
        UINT a = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        UINT b = GetMenuState(m, (UINT)(i - 1), MF_BYPOSITION);
        if ((a & MF_SEPARATOR) && (b & MF_SEPARATOR)) {
            RemoveMenu(m, (UINT)i, MF_BYPOSITION);
            changed = TRUE;
        }
    }
    return changed;
}

static DWORD WINAPI MapMenuThread(LPVOID param)
{
    (void)param;
    OutputDebugStringW(L"[WorldMapKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_hwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_hwnd && (bar = GetMenu(g_hwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!HasOurMenu(target)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);   // 게임 원래 항목과 구분(최초 1회)
                AppendMenuW(target, MF_STRING, ID_MAP_OPEN, L"지도");
                DrawMenuBar(g_hwnd);
                OutputDebugStringW(L"[WorldMapKR] 지도 menu installed.");
            }
            if (fileMenu && CollapseSeparators(fileMenu)) DrawMenuBar(g_hwnd);
            if (g_subHwnd != g_hwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_hwnd;
                OutputDebugStringW(L"[WorldMapKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void MapMenu_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MapMenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
