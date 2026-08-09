#include <windows.h>
#include "fatigue.h"

// FatigueUtilKR — 피로도 덜어내기. 창 하나에 입력칸과 버튼 하나뿐이다.
//
// 왜 이 자리인가 — 피로도는 함대 정보 블록의 첫 칸이다. ce/CDS_95.CT 의 "함대 정보"
// 묶음이 피로도 0x5B3950, 규칙 0x5B3954, 물 0x5B395C, 식량 0x5B3960 … 으로 이어져
// 있어 4바이트 정수 하나로 읽고 쓰면 된다. 절대주소가 아니라 모듈 베이스 + RVA 로
// 잡는다(다른 KR 플러그인과 같은 방식 — livechar.c 의 FAME_RVA 참고).
//
// 시간경과 쪽은 안 건드린다. 항해하면 게임이 하던 대로 다시 쌓는다(0x474060 에 일수x3).
// 이 창은 지금 쌓인 값을 그 자리에서 덜어낼 뿐이다.

#define ID_FATIGUE_OPEN 0xBA00u   // Trade=0xB101/0xB102/0xC0xx, Char=0xB301, Ship=0xB410,
                                  // Patch=0xB500, Map=0xB600, Mod=0xB700, QMod=0xB800,
                                  // Upd=0xB900 과 안 겹치게.

#define ID_CUR     1001
#define ID_LBL     1002
#define ID_AMOUNT  1003
#define ID_APPLY   1004
#define ID_STATUS  1005

#define FATIGUE_RVA  0x1B3950u    // ce/CDS_95.CT "함대 정보 > 피로도" (CDS_95.EXE+1B3950)
#define FATIGUE_MAX  100          // 0x474038 의 "최대 피로도"
#define AMOUNT_DEF   20           // 기본값

#define WC_FATIGUE L"FatigueUtilKR_Window"
#define CLIENT_W   348
#define CLIENT_H   156

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL;
static HFONT     g_font = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;
static WNDPROC   g_editProc = NULL;

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

// ------------------------------------------------------------------ 피로도 읽고 쓰기

// 세이브를 아직 안 불러왔거나 주소가 이 빌드와 안 맞으면 NULL. 읽기 전에 반드시 확인한다
// (.data 뒷부분이라 실행 중에만 커밋돼 있다).
static int* FatiguePtr(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* base = (unsigned char*)GetModuleHandleW(NULL);
    void* p;
    if (!base) return NULL;
    p = base + FATIGUE_RVA;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return NULL;
    if (mbi.State != MEM_COMMIT) return NULL;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return NULL;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return NULL;
    return (int*)p;
}

// 지금 피로도. 못 읽거나 말이 안 되는 값이면 -1.
static int FatigueGet(void)
{
    int* p = FatiguePtr();
    int v;
    if (!p) return -1;
    v = *p;
    // 게임이 쓰는 범위를 크게 벗어나면 아직 함대 정보가 안 찬 것으로 본다.
    if (v < 0 || v > 1000) return -1;
    return v;
}

static int FatigueSet(int v)
{
    int* p = FatiguePtr();
    if (!p) return 0;
    *p = v;
    return 1;
}

// ------------------------------------------------------------------ 창

static void RefreshCurrent(HWND h)
{
    wchar_t t[64];
    int cur = FatigueGet();
    if (cur < 0) lstrcpyW(t, L"지금 피로도 : 아직 못 읽음");
    else         wsprintfW(t, L"지금 피로도 : %d / %d", cur, FATIGUE_MAX);
    SetDlgItemTextW(h, ID_CUR, t);
}

static void DoReduce(HWND h)
{
    wchar_t msg[128];
    BOOL ok = FALSE;
    int amount, cur, next;

    cur = FatigueGet();
    if (cur < 0) {
        SetDlgItemTextW(h, ID_STATUS,
            L"피로도를 읽지 못했습니다.\n세이브를 불러온 뒤에 눌러 주세요.");
        RefreshCurrent(h);
        return;
    }

    amount = (int)GetDlgItemInt(h, ID_AMOUNT, &ok, FALSE);
    if (!ok || amount <= 0) {
        SetDlgItemTextW(h, ID_STATUS, L"줄일 값은 1 이상의 수로 적어 주세요.");
        return;
    }
    if (amount > FATIGUE_MAX) amount = FATIGUE_MAX;

    if (cur == 0) {
        SetDlgItemTextW(h, ID_STATUS, L"피로도가 이미 0 입니다.");
        return;
    }

    next = cur - amount;
    if (next < 0) next = 0;
    if (!FatigueSet(next)) {
        SetDlgItemTextW(h, ID_STATUS, L"피로도를 쓰지 못했습니다.");
        return;
    }

    wsprintfW(msg, L"피로도 %d → %d  (%d 줄임)\n화면 숫자는 다음에 다시 그릴 때 바뀝니다.",
              cur, next, cur - next);
    SetDlgItemTextW(h, ID_STATUS, msg);
    RefreshCurrent(h);
    LogW(L"[FatigueUtilKR] 피로도 줄임.");
}

// 입력칸에서 엔터를 쳐도 [줄이기] 와 같게 동작시킨다. 게임 메시지 루프는 우리 창에
// IsDialogMessage 를 돌려주지 않아서(우리 창이 아니라 게임 창의 루프다) 직접 받는다.
static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN && w == VK_RETURN) {
        SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_APPLY, BN_CLICKED), (LPARAM)h);
        return 0;
    }
    if (msg == WM_CHAR && w == VK_RETURN) return 0;   // 엔터 삑 소리 막기
    return CallWindowProcW(g_editProc, h, msg, w, l);
}

static HWND MakeCtl(HWND h, const wchar_t* cls, const wchar_t* text, DWORD style,
                    int x, int y, int cw, int ch, int id)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, cw, ch, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
    if (c && g_font) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK FatigueProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        wchar_t def[16];
        HWND edit;
        // 창마다 제 글꼴을 만들어 쓰고 창과 함께 지운다(공유하면 먼저 닫힌 창이 지워 버린다).
        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"맑은 고딕");

        MakeCtl(h, L"STATIC", L"", 0,                        14, 14, CLIENT_W - 28, 22, ID_CUR);
        MakeCtl(h, L"STATIC", L"줄일 값 :", 0,               14, 52, 62, 22, ID_LBL);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
                    80, 48, 68, 26, h, (HMENU)(UINT_PTR)ID_AMOUNT, g_hinst, NULL);
        if (edit) {
            if (g_font) SendMessageW(edit, WM_SETFONT, (WPARAM)g_font, TRUE);
            wsprintfW(def, L"%d", AMOUNT_DEF);
            SetWindowTextW(edit, def);
            g_editProc = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
        }
        MakeCtl(h, L"BUTTON", L"줄이기", WS_TABSTOP | BS_DEFPUSHBUTTON,
                160, 47, 92, 28, ID_APPLY);
        MakeCtl(h, L"STATIC", L"", 0,                        14, 90, CLIENT_W - 28, 50, ID_STATUS);

        RefreshCurrent(h);
        SetTimer(h, 1, 500, NULL);      // 게임이 값을 바꿔도 표시가 따라가게
        return 0;
    }
    case WM_TIMER:
        RefreshCurrent(h);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_COMMAND:
        if (LOWORD(w) == ID_APPLY) DoReduce(h);
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        g_wnd = NULL; g_editProc = NULL;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void ShowFatigueWindow(void)
{
    static BOOL reg = FALSE;
    RECT r, orc;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, ww, wh;

    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = FatigueProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = WC_FATIGUE;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        ww = r.right - r.left; wh = r.bottom - r.top;
        // 게임 창 한가운데. 게임이 전체화면이라 소유자로 걸어야 위에 뜬다.
        if (g_gameHwnd && GetWindowRect(g_gameHwnd, &orc)) {
            x = orc.left + ((orc.right - orc.left) - ww) / 2;
            y = orc.top  + ((orc.bottom - orc.top) - wh) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
        }
        g_wnd = CreateWindowExW(0, WC_FATIGUE, L"피로도 — 줄일 값을 적고 [줄이기]",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                    x, y, ww, wh, g_gameHwnd, NULL, g_hinst, NULL);
    } else {
        RefreshCurrent(g_wnd);
    }
    if (g_wnd) {
        ShowWindow(g_wnd, SW_SHOW);
        SetForegroundWindow(g_wnd);
        SetFocus(GetDlgItem(g_wnd, ID_AMOUNT));
    }
}

// ------------------------------------------------------------------ 메뉴 붙이기

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_FATIGUE_OPEN) {
        ShowFatigueWindow();
        return 0;
    }
    if (m == WM_NCDESTROY && h == g_subHwnd) {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_gameHwnd = NULL;
    }
    return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0;
    (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_gameHwnd = h; return FALSE; }
    return TRUE;
}

// 최상위 메뉴바에서 "파일" 팝업을 찾는다. 실제 라벨엔 니모닉이 붙어 접두어로 본다.
static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i;
    WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}

static BOOL MenuHasId(HMENU m, UINT id)
{
    int n, i;
    if (!m) return FALSE;
    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, (UINT)i);
        if (sub) { if (MenuHasId(sub, id)) return TRUE; continue; }
        if (GetMenuItemID(m, (UINT)i) == id) return TRUE;
    }
    return FALSE;
}

// "파일" 안에 KR 플러그인 항목(0xB000~0xCFFF)이 아직 없으면 구분선을 먼저 하나 둔다.
static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
}

static DWORD WINAPI MenuThread(LPVOID p)
{
    (void)p;
    LogW(L"[FatigueUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_FATIGUE_OPEN)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(target, MF_STRING, ID_FATIGUE_OPEN, L"피로도");
                DrawMenuBar(g_gameHwnd);
                LogW(L"[FatigueUtilKR] \"피로도\" 메뉴 설치.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
            }
        }
        Sleep(1000);
    }
}

void FatigueUtilKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    LogW(L"[FatigueUtilKR] init.");
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
