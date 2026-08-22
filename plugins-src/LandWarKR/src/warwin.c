#include <windows.h>
#include <stdarg.h>
#include "warwin.h"
#include "landwar.h"
#include "gameskin.h"   // 창을 게임 껍데기로 입힌다
#include "modmenu.h"   // common/ — 모드 창 등록부(걷어 간 항목을 여기서 본다)

// LandWarKR — 육상전 부대의 병종을 갈아 끼운다.
//
//   왼쪽   부대 12칸 (아군 6 / 적 6) — 지금 병종, 무력·지력, 예약한 병종
//   오른쪽 병종 24종 — 고르면 밑에 설명이 나온다
//   [지금 바꾸기]        전투 중에 그 칸의 병종을 바로 바꾼다.
//                        그림은 전투 시작 때 이미 읽어 둔 것이라 그대로다(능력·행동만 바뀐다)
//   [전투 시작 때]       아군 칸에 예약해 둔다. 다음 전투가 시작될 때 그림까지 그 병종으로 읽힌다
//                        (0x0044A0D0 훅 — landwar.c 머리말)
//
// 콤보 상자는 쓰지 않는다 — 게임 DirectDraw 화면 위에서 목록을 펼치면 게임이 죽는다
// (TradeUtilKR/src/trade.c 머리말, ButtonMakerKR 이 같은 함정에 빠졌었다).

#define ID_LAND_OPEN 0xC600u   // "파일>모드>육상전 부대"
                               // (… Book=0xC300, ShipInfo=0xC400, ButtonMaker=0xC500 과 안 겹치게)

#define ID_UNITS   1001
#define ID_TYPES   1002
#define ID_NOW     1010
#define ID_PRESET  1011
#define ID_CLEAR   1012
#define ID_ALLOWBIG 1013
#define ID_SAVE    1014
#define ID_TGT0    1030      // 예약 대상: 모든 적장 공통
#define ID_TGT1    1031      // 예약 대상: 이 적장만
#define ID_DESC    1020
#define ID_ENEMY   1022
#define ID_STATUS  1021

#define TITLE_H  28        // 위쪽 제목 띠 몫
#define UNIT_W   300
#define TYPE_W   170
#define LIST_H   240

static HINSTANCE g_hinst = NULL;
static HWND g_win = NULL, g_units = NULL, g_types = NULL, g_desc = NULL, g_status = NULL;
static HWND g_enemy = NULL;
static HFONT g_font = NULL;

static void SetStatus(const wchar_t* s) { if (g_status) SetWindowTextW(g_status, s); }

// 예약을 어느 벌에 넣을까 — [이 적장만] 을 골랐고 적장을 읽을 수 있으면 그 id, 아니면 공통.
static int CurTargetId(void)
{
    int id;
    if (!g_win) return LW_ID_COMMON;
    if (SendMessageW(GetDlgItem(g_win, ID_TGT1), BM_GETCHECK, 0, 0) != BST_CHECKED)
        return LW_ID_COMMON;
    id = LandWar_EnemyId();
    return (id >= 0) ? id : LW_ID_COMMON;
}

// 위쪽에 이번 전투의 적장을 적는다.
static void ShowEnemy(void)
{
    wchar_t s[160], nm[64];
    int id = LandWar_EnemyId();
    if (!g_enemy) return;
    if (id < 0) lstrcpyW(s, L"적장: (전투 밖이라 모름) — 지금은 [공통] 예약만 됩니다");
    else if (LandWar_EnemyName(id, nm, 64))
        wsprintfW(s, L"적장: %s   (id 0x%X · 갈래 %d · 번호 %d)", nm, id, id >> 12, id & 0xFFF);
    else
        wsprintfW(s, L"적장: id 0x%X (갈래 %d · 번호 %d)", id, id >> 12, id & 0xFFF);
    SetWindowTextW(g_enemy, s);
}

// 게임 함수는 부르지 않는다 — 전투 밖에서 부르면 객체가 안 서 있어 게임이 죽는다.
// 부대 레코드를 그냥 읽어서 보여 준다(메모리 읽기라 언제든 안전하다).
static void FillUnits(void)
{
    int i, sel, live;
    if (!g_units) return;
    live = LandWar_Active();
    sel = (int)SendMessageW(g_units, LB_GETCURSEL, 0, 0);
    SendMessageW(g_units, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < LW_UNIT_N; i++) {
        wchar_t s[160], tail[64];
        int t = LandWar_Type(i);
        int pre = LandWar_Preset(CurTargetId(), i);
        tail[0] = 0;
        if (pre >= 0) wsprintfW(tail, L"   → %s (예약)", LandWar_TypeName(pre));
        if (live && t >= 0 && t < LW_TYPE_N)
            wsprintfW(s, L"%s%d  %2d %-10s%s",
                      (i < LW_MINE_N) ? L"아군" : L"적 ", (i % LW_MINE_N) + 1,
                      t, LandWar_TypeName(t), tail);
        else
            wsprintfW(s, L"%s%d  (전투 밖)%s",
                      (i < LW_MINE_N) ? L"아군" : L"적 ", (i % LW_MINE_N) + 1, tail);
        SendMessageW(g_units, LB_ADDSTRING, 0, (LPARAM)s);
    }
    if (sel >= 0 && sel < LW_UNIT_N) SendMessageW(g_units, LB_SETCURSEL, (WPARAM)sel, 0);
}

static void ShowDesc(void)
{
    int t = (int)SendMessageW(g_types, LB_GETCURSEL, 0, 0);
    wchar_t s[320];
    if (t < 0 || t >= LW_TYPE_N) { SetWindowTextW(g_desc, L""); return; }
    wsprintfW(s, L"%s — %s", LandWar_TypeName(t), LandWar_TypeDesc(t));
    SetWindowTextW(g_desc, s);
}

// 고른 칸의 40바이트 레코드를 그대로 보여 준다 — 어느 칸이 병사수인지 같이 짚어 보려고.
static void ShowRecord(void)
{
    int slot = (int)SendMessageW(g_units, LB_GETCURSEL, 0, 0);
    wchar_t s[256], one[24];
    int j;
    if (slot < 0 || slot >= LW_UNIT_N) return;
    wsprintfW(s, L"%d칸 레코드:", slot);
    for (j = 0; j < LW_UNIT_SZ / 4; j++) {
        wsprintfW(one, L" %d", LandWar_Word(slot, j));
        lstrcatW(s, one);
    }
    SetStatus(s);
}

static void Refresh(void)
{
    wchar_t s[256];
    FillUnits();
    if (!LandWar_Ready()) { SetStatus(L"부대 배열을 못 읽었습니다."); return; }
    if (LandWar_Active())
        wsprintfW(s, L"육상전이 서 있습니다 — [지금 바꾸기]가 바로 먹습니다.%s",
                  LandWar_BigAllowed() ? L"   (큰 그림 병종 풀림)" : L"");
    else
        wsprintfW(s, L"지금은 육상전이 아닙니다 — [전투 시작 때]로 예약해 두세요.%s",
                  LandWar_BigAllowed() ? L"   (큰 그림 병종 풀림)" : L"");
    SetStatus(s);
}

static void ApplyNow(void)
{
    int slot = (int)SendMessageW(g_units, LB_GETCURSEL, 0, 0);
    int t    = (int)SendMessageW(g_types, LB_GETCURSEL, 0, 0);
    wchar_t s[256];
    if (slot < 0 || t < 0) { SetStatus(L"칸과 병종을 하나씩 고르세요."); return; }
    if (!LandWar_TypeOkForSlot(slot, t)) {
        wsprintfW(s, L"%s 는 그림이 커서 아군 칸에 넣으면 옆 칸 그림이 깨집니다. "
                     L"그래도 넣으려면 아래 [큰 그림 병종 허용]을 켜세요.", LandWar_TypeName(t));
        SetStatus(s);
        return;
    }
    if (LandWar_SetType(slot, t)) {
        wsprintfW(s, L"%d칸을 %s 로 바꿨습니다. (그림은 이번 전투 동안 그대로입니다)",
                  slot, LandWar_TypeName(t));
        SetStatus(s);
        FillUnits();
    } else SetStatus(L"바꾸지 못했습니다.");
}

static void ApplyPreset(void)
{
    int slot = (int)SendMessageW(g_units, LB_GETCURSEL, 0, 0);
    int t    = (int)SendMessageW(g_types, LB_GETCURSEL, 0, 0);
    wchar_t s[256];
    if (slot < 0 || t < 0) { SetStatus(L"칸과 병종을 하나씩 고르세요."); return; }
    if (!LandWar_TypeOkForSlot(slot, t)) {
        wsprintfW(s, L"%s 는 그림이 커서 아군 칸에 못 넣습니다. [큰 그림 병종 허용]을 켜세요.",
                  LandWar_TypeName(t));
        SetStatus(s);
        return;
    }
    {
        int id = CurTargetId();
        wchar_t nm[64];
        if (!LandWar_SetPreset(id, slot, t)) { SetStatus(L"예약을 못 넣었습니다(벌이 가득 찼을 수 있습니다)."); return; }
        if (id == LW_ID_COMMON) lstrcpyW(nm, L"모든 적장");
        else if (!LandWar_EnemyName(id, nm, 64)) wsprintfW(nm, L"적장 0x%X", id);
        wsprintfW(s, L"[%s] %s %d칸 → %s. 다음 전투부터 그림까지 그 병종으로 나옵니다.",
                  nm, (slot < LW_MINE_N) ? L"아군" : L"적",
                  (slot % LW_MINE_N) + 1, LandWar_TypeName(t));
    }
    SetStatus(s);
    FillUnits();
}

// ---------------------------------------------------------------- 창

static HWND Mk(const wchar_t* cls, const wchar_t* txt, DWORD st, DWORD ex,
               int x, int y, int w, int h, int id, HWND par)
{
    HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st,
                             x, y + TITLE_H, w, h, par, (HMENU)(INT_PTR)id, g_hinst, NULL);
    if (c && g_font) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void MakeControls(HWND h)
{
    LOGFONTW lf;
    int t, yList = 50, yBtn;

    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -12; lf.lfCharSet = HANGEUL_CHARSET;
    lstrcpyW(lf.lfFaceName, L"굴림체");                 // 칸이 맞아야 읽기 좋다
    g_font = CreateFontIndirectW(&lf);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    g_enemy = Mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0, 12, 8, UNIT_W + TYPE_W + 12, 18, ID_ENEMY, h);

    Mk(L"STATIC", L"부대 12칸 (아군 1~6 · 적 1~6)", 0, 0, 12, 30, 260, 18, 0, h);
    g_units = Mk(L"LISTBOX", L"", WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                 WS_EX_CLIENTEDGE, 12, yList, UNIT_W, LIST_H, ID_UNITS, h);

    Mk(L"STATIC", L"병종 24종", 0, 0, 12 + UNIT_W + 12, 30, 160, 18, 0, h);
    g_types = Mk(L"LISTBOX", L"", WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                 WS_EX_CLIENTEDGE, 12 + UNIT_W + 12, yList, TYPE_W, LIST_H, ID_TYPES, h);

    for (t = 0; t < LW_TYPE_N; t++) {
        wchar_t s[64];
        wsprintfW(s, L"%2d %s%s", t, LandWar_TypeName(t),
                  (t == LW_BIG_A || t == LW_BIG_B) ? L"  ※큰그림" : L"");
        SendMessageW(g_types, LB_ADDSTRING, 0, (LPARAM)s);
    }

    g_desc = Mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0, 12, yList + LIST_H + 6,
                UNIT_W + TYPE_W + 12, 34, ID_DESC, h);

    // 예약 대상 — 콤보 대신 라디오. 게임 화면 위에서 목록을 펼치면 게임이 죽는다.
    yBtn = yList + LIST_H + 44;
    Mk(L"STATIC", L"예약 대상", 0, 0, 12, yBtn + 3, 60, 18, 0, h);
    Mk(L"BUTTON", L"모든 적장 공통", BS_AUTORADIOBUTTON | WS_GROUP, 0, 76, yBtn + 2, 118, 20, ID_TGT0, h);
    Mk(L"BUTTON", L"이 적장만",     BS_AUTORADIOBUTTON, 0, 198, yBtn + 2, 96, 20, ID_TGT1, h);
    SendMessageW(GetDlgItem(h, ID_TGT0), BM_SETCHECK, BST_CHECKED, 0);
    Mk(L"BUTTON", L"큰 그림 병종 허용(옆 칸 그림 깨짐)", BS_AUTOCHECKBOX | WS_GROUP, 0,
       302, yBtn + 2, 230, 20, ID_ALLOWBIG, h);

    yBtn += 26;
    Mk(L"BUTTON", L"지금 바꾸기",  WS_GROUP, 0,  12, yBtn, 108, 26, ID_NOW, h);
    Mk(L"BUTTON", L"전투 시작 때", 0, 0, 128, yBtn, 108, 26, ID_PRESET, h);
    Mk(L"BUTTON", L"이 벌 지우기", 0, 0, 244, yBtn, 108, 26, ID_CLEAR, h);
    Mk(L"BUTTON", L"파일로 남기기", 0, 0, 360, yBtn, 116, 26, ID_SAVE, h);

    g_status = Mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0, 12, yBtn + 32,
                  UNIT_W + TYPE_W + 12, 18, ID_STATUS, h);
}

// 1997년 게임 프로세스 안이라 여기서 터지면 게임까지 같이 간다. 잡아서 로그만 남긴다.
static void Guarded(void (*fn)(void), const wchar_t* what)
{
    __try { fn(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        wchar_t s[160];
        wsprintfW(s, L"[LandWarKR] !! %s 에서 예외 0x%08X", what, GetExceptionCode());
        OutputDebugStringW(s);
        SetStatus(L"창을 그리다 문제가 생겼습니다(로그 참고).");
    }
}

static LRESULT CALLBACK WinProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        MakeControls(h);
        GameSkin_Apply(h);          // 밀어넣기 단추를 게임 띠로
        SendMessageW(g_types, LB_SETCURSEL, 0, 0);
        ShowDesc();
        Guarded(Refresh, L"Refresh");
        Guarded(ShowEnemy, L"ShowEnemy");
        SetTimer(h, 1, 1000, NULL);      // 전투가 서면 목록이 저절로 채워지게
        return 0;

    case WM_TIMER:
        Guarded(FillUnits, L"FillUnits");
        Guarded(ShowEnemy, L"ShowEnemy");
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (code == LBN_SELCHANGE && id == ID_TYPES) { ShowDesc(); return 0; }
        if (code == LBN_SELCHANGE && id == ID_UNITS) { Guarded(ShowRecord, L"ShowRecord"); return 0; }
        if (code == BN_CLICKED) {
            if (id == ID_NOW)    { ApplyNow();    return 0; }
            if (id == ID_PRESET) { ApplyPreset(); return 0; }
            if (id == ID_CLEAR)  { LandWar_ClearPreset(CurTargetId()); FillUnits();
                                   SetStatus(L"이 벌의 예약을 지웠습니다."); return 0; }
            if (id == ID_SAVE)   { SetStatus(LandWar_Save() ? L"CDS95Util\\landwar.txt 에 남겼습니다."
                                                            : L"파일로 남기지 못했습니다."); return 0; }
            if (id == ID_TGT0 || id == ID_TGT1) { FillUnits(); InvalidateRect(h, NULL, FALSE); return 0; }
            if (id == ID_ALLOWBIG) {
                LandWar_AllowBig(SendMessageW(GetDlgItem(h, ID_ALLOWBIG), BM_GETCHECK, 0, 0) == BST_CHECKED);
                Refresh();
                return 0;
            }
        }
        break;
    }

    case WM_DRAWITEM:
        if (GameSkin_DrawItem((const DRAWITEMSTRUCT*)l)) return TRUE;
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT t;
        GetClientRect(h, &t); t.left = 8; t.right -= 8; t.top = 3; t.bottom = TITLE_H - 1;
        GameSkin_Title(dc, t, L"육상전 부대");
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        g_win = NULL; g_units = NULL; g_types = NULL; g_desc = NULL; g_status = NULL; g_enemy = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void LandWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { SetForegroundWindow(g_win); return; }
    if (!LandWar_Load()) {
        MessageBoxW(owner, L"육상전 부대 배열을 못 읽었습니다.\n한국어판 Ver.1.2.0.0 이 아닌 것 같습니다.",
                    L"육상전 부대", MB_ICONWARNING);
        return;
    }
    if (!registered) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = WinProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"LandWarKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"LandWarKRWin", L"육상전 부대 — LandWarKR",
                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT, 680, 470 + TITLE_H, owner, NULL, g_hinst, NULL);
    if (g_win) { ShowWindow(g_win, SW_SHOW); UpdateWindow(g_win); }
}

// ================================================================== 메뉴 설치 + 서브클래싱

static HWND    g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static int     g_pass = 0;

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_LAND_OPEN) { LandWin_Show(h); return 0; }
    if (m == WM_NCDESTROY) {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_gameHwnd = NULL;
        return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0; (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_gameHwnd = h; return FALSE; }
    return TRUE;
}
static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}
static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
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
static HMENU FindOrCreateModMenu(HMENU fileMenu, BOOL mayCreate)
{
    int i; WCHAR s[64]; HMENU first = NULL, sub;
    if (!fileMenu) return NULL;
    for (i = GetMenuItemCount(fileMenu) - 1; i >= 0; i--) {
        if (GetMenuStringW(fileMenu, (UINT)i, s, 64, MF_BYPOSITION) <= 0) continue;
        if (lstrcmpW(s, L"모드") != 0) continue;
        sub = GetSubMenu(fileMenu, (UINT)i);
        if (first && sub && GetMenuItemCount(sub) == 0) { RemoveMenu(fileMenu, (UINT)i, MF_BYPOSITION); continue; }
        first = sub;
    }
    if (first || !mayCreate) return first;
    sub = CreatePopupMenu();
    if (!sub) return NULL;
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)sub, L"모드");
    return sub;
}

static DWORD WINAPI MenuThread(LPVOID pv)
{
    (void)pv;
    OutputDebugStringW(L"[LandWarKR] menu monitor started.");
    LandWar_Load();
    // 훅은 여기서 걸지 않는다 — 사용자가 병종을 예약할 때(LandWar_SetPreset) 그때 건다.
    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!(MenuHasId(target, ID_LAND_OPEN) || ModMenu_HasId(g_gameHwnd, ID_LAND_OPEN))) {
                HMENU modMenu;
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                if (!modMenu) { Sleep(1000); continue; }
                AppendMenuW(modMenu, MF_STRING, ID_LAND_OPEN, L"육상전 부대");
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[LandWarKR] menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[LandWarKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void LandKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
