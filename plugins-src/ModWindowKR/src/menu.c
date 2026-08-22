#include <windows.h>
#include "modwin.h"
#include "modmenu.h"   // common/ — 등록부 약속(플러그인들과 나눠 쓴다)

// ModWindowKR — 파일 메뉴에 흩어져 붙은 KR 플러그인 항목을 걷어 "모드" 단추 하나로 모은다.
// 누르면 창이 떠 그 항목들을 단추로 늘어놓는다.
//
// ## 짜임새
//
//   파일 메뉴 :  게임 원래 항목  +  "모드"   ← 하위 메뉴가 없는 보통 항목이다
//   등록부    :  메뉴바에 걸리지 않은 떠 있는 메뉴. 걷어 온 항목이 여기 쌓인다
//
// 등록부를 파일 메뉴 밖에 두는 까닭은 "모드" 를 **누르게** 하기 위해서다. 하위 메뉴가
// 달린 항목은 마우스를 얹기만 해도 펼쳐지고 클릭으로는 WM_COMMAND 가 오지 않는다.
// 보통 항목으로 만들어야 눌러서 여는 모양이 된다.
//
// ## 걷어도 되붙지 않게
//
// 플러그인은 1초마다 "내 항목이 메뉴에 있나" 를 보고 없으면 다시 단다. 그래서 각
// 플러그인이 붙이기 전에 등록부도 함께 보도록 고쳤다(common/modmenu.h 의 ModMenu_HasId).
// 붙이는 자리는 예전 그대로 파일 메뉴여도 된다 — 여기서 곧 걷어간다.
//
// 사람이 파일 메뉴를 여는 순간에도 한 번 걷으므로(WM_INITMENUPOPUP) 옮겨지는 모습이
// 눈에 띄지 않는다.

#define KR_ID_LO       0xB000u     // KR 플러그인이 쓰기로 한 메뉴 ID 대역
#define KR_ID_HI       0xCFFFu
#define ID_MOD_WINDOW  0xC800u     // 쓰이는 ID 표는 common/modmenu.h 에 있다
                                   // Map=0xB600, Mod=0xB700, WindArrow=0xB800, CityPic=0xBF00 과 비충돌
#define MOD_LABEL      L"모드"

static HINSTANCE g_hinst;
static HWND      g_hwnd, g_subHwnd;
static WNDPROC   g_origProc;
static HMENU     g_reg;            // 등록부. 창에 프로퍼티로도 걸어 둔다

// 최상위 메뉴바에서 "파일" 팝업. 실제 라벨은 "파일 (&F)" 처럼 니모닉이 붙는다.
static HMENU FindFileMenu(HMENU bar)
{
    int n, i; WCHAR s[64];
    if (!bar) return NULL;
    n = GetMenuItemCount(bar);
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}

// 이 메뉴(하위까지)에 KR 대역 ID 가 하나라도 있나. 팝업이 우리 것인지 가리는 데 쓴다 —
// TradeUtilKR 의 "워프" 처럼 팝업 자체는 ID 가 없고 속에만 있는 경우가 있다.
static BOOL HasKrId(HMENU m)
{
    int n, i;
    if (!m) return FALSE;
    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, (UINT)i);
        UINT id;
        if (sub) { if (HasKrId(sub)) return TRUE; continue; }
        id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= KR_ID_LO && id <= KR_ID_HI) return TRUE;
    }
    return FALSE;
}

// 등록부에 그 ID 가 이미 있나. 되붙은 것을 걷을 때 같은 항목이 겹쳐 쌓이지 않게 본다.
static BOOL RegHasId(UINT id)
{
    return (id && id != (UINT)-1) ? ModMenu_MenuHasId(g_reg, id) : FALSE;
}

// 파일 메뉴의 한 자리가 "걷어 갈 우리 것"인가.
static BOOL ShouldTake(HMENU fileMenu, int pos)
{
    HMENU sub = GetSubMenu(fileMenu, (UINT)pos);
    UINT id, state = GetMenuState(fileMenu, (UINT)pos, MF_BYPOSITION);

    if (state == (UINT)-1 || (state & MF_SEPARATOR)) return FALSE;
    if (sub) return HasKrId(sub);                  // 속에 우리 ID 가 있으면 우리 팝업이다
    id = GetMenuItemID(fileMenu, (UINT)pos);
    if (id == ID_MOD_WINDOW) return FALSE;         // "모드" 단추 자신은 그대로 둔다
    return id != (UINT)-1 && id >= KR_ID_LO && id <= KR_ID_HI;
}

// 팝업 하나를 통째로 등록부에 쏟는다. ModUtilKR 이 세운 "모드" 서브메뉴가 이 꼴이다 —
// 그 안의 항목을 하나씩 옮겨야지, 팝업째 등록부에 붙이면 창에 단추 하나로 뭉뚱그려진다.
static int PourInto(HMENU popup)
{
    int moved = 0;
    while (GetMenuItemCount(popup) > 0) {
        WCHAR label[128];
        HMENU sub = GetSubMenu(popup, 0);
        UINT id = GetMenuItemID(popup, 0);
        UINT state = GetMenuState(popup, 0, MF_BYPOSITION);

        if (state != (UINT)-1 && (state & MF_SEPARATOR)) { RemoveMenu(popup, 0, MF_BYPOSITION); continue; }
        if (GetMenuStringW(popup, 0, label, 128, MF_BYPOSITION) <= 0) { RemoveMenu(popup, 0, MF_BYPOSITION); continue; }

        RemoveMenu(popup, 0, MF_BYPOSITION);
        if (sub) {
            if (!HasKrId(sub) || !RegHasId(GetMenuItemID(sub, 0)))
                AppendMenuW(g_reg, MF_POPUP, (UINT_PTR)sub, label);
        } else if (!RegHasId(id)) {
            AppendMenuW(g_reg, MF_STRING | (state & MF_CHECKED), id, label);
        }
        moved++;
    }
    return moved;
}

// 파일 메뉴의 우리 항목을 등록부로 걷는다. 걷은 개수.
static int TakeAll(HMENU fileMenu)
{
    int moved = 0, pos = 0;

    // 앞에서부터 훑는다 — 원래 붙어 있던 차례가 등록부 안에서도 그대로 이어지게.
    while (pos < GetMenuItemCount(fileMenu)) {
        WCHAR label[128];
        HMENU sub;
        UINT id, state;

        if (!ShouldTake(fileMenu, pos)) { pos++; continue; }

        sub = GetSubMenu(fileMenu, (UINT)pos);
        id = GetMenuItemID(fileMenu, (UINT)pos);
        state = GetMenuState(fileMenu, (UINT)pos, MF_BYPOSITION);
        if (GetMenuStringW(fileMenu, (UINT)pos, label, 128, MF_BYPOSITION) <= 0) { pos++; continue; }

        // RemoveMenu 는 팝업을 부수지 않는다(DeleteMenu 와 다르다).
        RemoveMenu(fileMenu, (UINT)pos, MF_BYPOSITION);
        if (sub) {
            // 예전 "모드" 서브메뉴면 그 속을 쏟아 붓고 껍데기는 버린다.
            if (lstrcmpW(label, MOD_LABEL) == 0) {
                moved += PourInto(sub);
                DestroyMenu(sub);
            } else if (!RegHasId(GetMenuItemID(sub, 0))) {
                AppendMenuW(g_reg, MF_POPUP, (UINT_PTR)sub, label);   // "워프" 처럼 제 하위를 가진 것
                moved++;
            } else {
                DestroyMenu(sub);
            }
        } else if (!RegHasId(id)) {
            AppendMenuW(g_reg, MF_STRING | (state & MF_CHECKED), id, label);   // 체크 표시는 살린다
            moved++;
        }
    }
    return moved;
}

// 파일 메뉴 끝에 남은 구분선을 정리한다. 항목을 다 걷고 나면 구분선만 덩그러니 남는다.
static void TidySeparators(HMENU m)
{
    int i;
    // GetMenuState 는 못 읽으면 -1 을 준다. 그 값에는 MF_SEPARATOR 비트도 서 있어서
    // 그냥 & 로 보면 "구분선" 으로 잘못 읽고 멀쩡한 항목을 지운다. 먼저 걸러 낸다.
    for (i = GetMenuItemCount(m) - 1; i > 0; i--) {
        UINT a = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        UINT b = GetMenuState(m, (UINT)(i - 1), MF_BYPOSITION);
        if (a == (UINT)-1 || b == (UINT)-1) continue;
        if ((a & MF_SEPARATOR) && (b & MF_SEPARATOR)) RemoveMenu(m, (UINT)i, MF_BYPOSITION);
    }
    i = GetMenuItemCount(m) - 1;
    if (i >= 0) {
        UINT a = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        if (a != (UINT)-1 && (a & MF_SEPARATOR)) RemoveMenu(m, (UINT)i, MF_BYPOSITION);
    }
}

// 파일 메뉴에 "모드" 단추가 있나(하위 메뉴 없는 보통 항목으로).
static BOOL HasModButton(HMENU fileMenu)
{
    int n = GetMenuItemCount(fileMenu), i;
    for (i = 0; i < n; i++)
        if (!GetSubMenu(fileMenu, (UINT)i) && GetMenuItemID(fileMenu, (UINT)i) == ID_MOD_WINDOW) return TRUE;
    return FALSE;
}

// 한 바퀴 정리. 걷고, 단추가 없으면 달고, 구분선을 다듬는다.
static BOOL Sweep(HWND h)
{
    HMENU bar = GetMenu(h);
    HMENU fileMenu = FindFileMenu(bar);
    int moved;
    BOOL added = FALSE;

    if (!fileMenu || !g_reg) return FALSE;

    moved = TakeAll(fileMenu);
    if (!HasModButton(fileMenu)) {
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(fileMenu, MF_STRING, ID_MOD_WINDOW, MOD_LABEL);
        added = TRUE;
        OutputDebugStringW(L"[ModWindowKR] \"모드\" 단추 설치.");
    }
    TidySeparators(fileMenu);
    if (moved) {
        WCHAR msg[80];
        wsprintfW(msg, L"[ModWindowKR] %d 항목을 등록부로 걷었다.", moved);
        OutputDebugStringW(msg);
    }
    return moved > 0 || added;
}

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;

    if (msg == WM_COMMAND && HIWORD(wp) == 0 && LOWORD(wp) == ID_MOD_WINDOW) {
        Sweep(h);                       // 열기 직전에 한 번 더 걷는다 — 목록이 최신이 되게
        ModWin_Show(h, g_reg, g_hinst);
        return 0;
    }
    // 사람이 파일 메뉴를 여는 참이다. 그 사이 새로 붙은 것을 지금 걷는다.
    if (msg == WM_INITMENUPOPUP && !HIWORD(lp)) {
        HMENU bar = GetMenu(h);
        if ((HMENU)wp == FindFileMenu(bar)) Sweep(h);
    }
    if (msg == WM_NCDESTROY) {
        RemovePropW(h, MODMENU_PROP);
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

static DWORD WINAPI MenuThread(LPVOID p)
{
    (void)p;
    OutputDebugStringW(L"[ModWindowKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_hwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_hwnd && (bar = GetMenu(g_hwnd)) != NULL) {
            if (!g_reg) g_reg = CreatePopupMenu();
            // 프로퍼티로 걸어 두면 다른 플러그인이 GetProp 로 찾아 제 항목이 이미
            // 걷혔는지 볼 수 있다(common/modmenu.h). 창이 바뀌면 다시 건다.
            if (g_reg && ModMenu_Handle(g_hwnd) != g_reg)
                SetPropW(g_hwnd, MODMENU_PROP, (HANDLE)g_reg);

            if (g_subHwnd != g_hwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_hwnd;
                OutputDebugStringW(L"[ModWindowKR] window subclassed.");
            }
            // 바뀐 게 있을 때만 다시 그린다 — 매초 그리면 메뉴바가 깜빡일 수 있다.
            if (Sweep(g_hwnd)) DrawMenuBar(g_hwnd);
        }
        Sleep(1000);
    }
}

void ModWindow_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
