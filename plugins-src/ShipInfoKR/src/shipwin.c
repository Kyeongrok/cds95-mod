#include <windows.h>
#include <commctrl.h>
#include "shipwin.h"
#include "hulldb.h"
#include "shipstil.h"
#include "cities_data.h"   // TradeUtilKR/src — kCities[226]

// ShipInfoKR — 선체 도감. 조선소 구입 창에 배 그림이 없어서 만들었다. 읽기 전용이다.
//
// 왼쪽에서 선체를 고르면 오른쪽에 SHIPSTIL.CDS 의 배 그림과 스탯, 그리고 그 선체를
// 파는 조선소 도시가 나온다.

#define ID_SHIP_OPEN 0xC400u   // "파일>모드>선체"
                               // (… Dialog=0xC100, Skill=0xC200, Book=0xC300 과 안 겹치게)
#define ID_LIST    1001
#define ID_DETAIL  1010
#define ID_PREV    1011
#define ID_NEXT    1012
#define ID_PICNUM  1013

#define CITY_N  ((int)(sizeof(kCities)/sizeof(kCities[0])))
#define LIST_W  200
#define PIC_X   (LIST_W + 14)
#define PIC_Y   10

// 선체 8종 -> SHIPSTIL 그림 번호(0~5). -1 이면 그림 없음.
//
// ★ 이 짝은 그림을 눈으로 보고 맞춘 것이고 인게임으로 확인하지 않았다. 게임 EXE 안에
//   "SHIPSTIL" 이라는 문자열이 아예 없어서(찾아봤다) 짝을 알려 주는 코드가 없다 —
//   이 파일을 읽는 것은 조선소 편집기(SEDITOR.EXE)뿐일 수도 있다.
//   그래서 창에 [◀ 그림 ▶] 을 두어 눈으로 바꿔 볼 수 있게 했다. 틀린 짝을 찾으면
//   여기만 고치면 된다.
static int g_still[HULL_N] = {
    0,   // 0 코구        — 돛대 하나, 포문 없음(대포 0)
    1,   // 1 카라벨      — 작고 날렵, 대포 2
    2,   // 2 대형카라벨  — 포문 한 줄, 대포 8
    3,   // 3 카락        — 뱃머리에 둥근 성채, 대포 6
    -1,  // 4 대형카락    — 그림 없음(6장뿐)
    4,   // 5 중카락      — 세 돛대에 포문 많음, 대포 24
    5,   // 6 갤리온      — 가장 크고 꾸밈이 많음, 대포 24
    -1,  // 7 다우        — 그림 없음
};

static HINSTANCE g_hinst = NULL;
static HWND      g_win = NULL, g_list = NULL, g_detail = NULL, g_picnum = NULL;
static int       g_sel = -1;

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

static void Cat(wchar_t* buf, int cap, const wchar_t* s)
{
    int n = lstrlenW(buf), m = lstrlenW(s);
    if (n + m + 1 >= cap) m = cap - n - 1;
    if (m <= 0) return;
    lstrcpynW(buf + n, s, m + 1);
}
static void CatF(wchar_t* buf, int cap, const wchar_t* fmt, ...)
{
    wchar_t tmp[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(tmp, fmt, ap);
    va_end(ap);
    Cat(buf, cap, tmp);
}

static void UpdatePicNum(void)
{
    wchar_t s[64];
    int p = (g_sel >= 0) ? g_still[g_sel] : -1;
    if (p < 0) lstrcpyW(s, L"그림 없음");
    else       wsprintfW(s, L"그림 %d / %d", p + 1, ShipStil_Count());
    SetWindowTextW(g_picnum, s);
    EnableWindow(GetDlgItem(g_win, ID_PREV), g_sel >= 0);
    EnableWindow(GetDlgItem(g_win, ID_NEXT), g_sel >= 0);
}

static void ShowDetail(void)
{
    wchar_t s[4096];
    int k = g_sel, city, n = 0;

    s[0] = 0;
    if (k < 0) { SetWindowTextW(g_detail, L""); UpdatePicNum(); return; }

    CatF(s, 4096, L"%s\r\n\r\n", Hull_Name(k));
    CatF(s, 4096, L"내구력   %5d        추진력   %5d\r\n", Hull_Dura(k), Hull_Speed(k));
    CatF(s, 4096, L"적재용량 %5d        적재중량 %5d\r\n", Hull_Volume(k), Hull_Weight(k));
    CatF(s, 4096, L"필요승인 %5d        대포수   %5d\r\n", Hull_Crew(k), Hull_Guns(k));

    if (!Hull_CitiesReady()) {
        Cat(s, 4096, L"\r\n파는 조선소: (세이브를 불러오기 전이라 모릅니다)\r\n");
    } else {
        wchar_t line[512];
        line[0] = 0;
        for (city = 0; city < CITY_N; city++) {
            if (Hull_CitySells(city, k) != 1) continue;
            if (n++) Cat(line, 512, L", ");
            Cat(line, 512, kCities[city].name);
            if ((n % 6) == 0) { Cat(line, 512, L"\r\n    "); }
        }
        CatF(s, 4096, L"\r\n파는 조선소 (%d곳)\r\n    %s\r\n", n, n ? line : L"없다");
    }
    SetWindowTextW(g_detail, s);
    UpdatePicNum();
}

static void FillList(void)
{
    int k;
    ListView_DeleteAllItems(g_list);
    for (k = 0; k < HULL_N; k++) {
        LVITEMW it;
        wchar_t buf[32];
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = k; it.iSubItem = 0;
        it.pszText = (LPWSTR)Hull_Name(k);
        it.lParam = k;
        ListView_InsertItem(g_list, &it);
        wsprintfW(buf, L"%d", Hull_Dura(k));
        ListView_SetItemText(g_list, k, 1, buf);
        wsprintfW(buf, L"%d", Hull_Guns(k));
        ListView_SetItemText(g_list, k, 2, buf);
    }
}

static void CreateChildren(HWND h)
{
    const wchar_t* titles[3] = { L"선체", L"내구", L"대포" };
    int widths[3] = { 108, 44, 44 };
    LVCOLUMNW c;
    int i;

    g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_LIST, g_hinst, NULL);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    for (i = 0; i < 3; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; ListView_InsertColumn(g_list, i, &c); }

    CreateWindowExW(0, L"BUTTON", L"◀", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_PREV, g_hinst, NULL);
    g_picnum = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_PICNUM, g_hinst, NULL);
    CreateWindowExW(0, L"BUTTON", L"▶", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_NEXT, g_hinst, NULL);

    g_detail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_DETAIL, g_hinst, NULL);

    {   // 상세는 고정폭이라야 숫자 칸이 맞는다.
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND c2 = GetWindow(h, GW_CHILD);
        LOGFONTW lf;
        static HFONT mono = NULL;
        while (c2) { SendMessageW(c2, WM_SETFONT, (WPARAM)f, TRUE); c2 = GetWindow(c2, GW_HWNDNEXT); }
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -13; lf.lfCharSet = HANGEUL_CHARSET;
        lstrcpyW(lf.lfFaceName, L"굴림체");
        if (!mono) mono = CreateFontIndirectW(&lf);
        if (mono) SendMessageW(g_detail, WM_SETFONT, (WPARAM)mono, TRUE);
    }
}

static void LayoutChildren(HWND h, int cw, int ch)
{
    int dy = PIC_Y + SHIPSTIL_H + 30;
    int dw = cw - PIC_X - 10, dh = ch - dy - 10;
    if (dw < 120) dw = 120;
    if (dh < 60)  dh = 60;
    MoveWindow(g_list, 6, 6, LIST_W, ch - 12, TRUE);
    MoveWindow(GetDlgItem(h, ID_PREV), PIC_X, PIC_Y + SHIPSTIL_H + 4, 30, 22, TRUE);
    MoveWindow(g_picnum,               PIC_X + 34, PIC_Y + SHIPSTIL_H + 7, 100, 18, TRUE);
    MoveWindow(GetDlgItem(h, ID_NEXT), PIC_X + 138, PIC_Y + SHIPSTIL_H + 4, 30, 22, TRUE);
    MoveWindow(g_detail, PIC_X, dy, dw, dh, TRUE);
}

static void PaintPic(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT r;
    COLORREF bg = GetSysColor(COLOR_BTNFACE);
    int pic = (g_sel >= 0) ? g_still[g_sel] : -1;

    r.left = PIC_X; r.top = PIC_Y;
    r.right = PIC_X + SHIPSTIL_W; r.bottom = PIC_Y + SHIPSTIL_H;
    if (pic < 0 || !ShipStil_Draw(dc, r.left, r.top, SHIPSTIL_W, SHIPSTIL_H, pic, bg)) {
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dc, &r, br);
        DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, (pic < 0) ? L"이 선체는 그림이 없습니다"
                                : L"SHIPSTIL.CDS 를 못 읽었습니다",
                  -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    FrameRect(dc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
    EndPaint(h, &ps);
}

static LRESULT CALLBACK WinProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_win = h;
        CreateChildren(h);
        FillList();
        ShowDetail();
        return 0;

    case WM_SIZE:
        LayoutChildren(h, LOWORD(l), HIWORD(l));
        return 0;

    case WM_PAINT:
        PaintPic(h);
        return 0;

    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)l;
        if (n->idFrom == ID_LIST && n->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* lv = (NMLISTVIEW*)l;
            if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)) {
                g_sel = (lv->iItem >= 0 && lv->iItem < HULL_N) ? lv->iItem : -1;
                ShowDetail();
                InvalidateRect(h, NULL, TRUE);
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        UINT id = LOWORD(w);
        if ((id == ID_PREV || id == ID_NEXT) && g_sel >= 0) {
            int n = ShipStil_Count();
            if (n > 0) {
                int p = g_still[g_sel];
                p = (p < 0) ? 0 : (p + ((id == ID_NEXT) ? 1 : n - 1)) % n;
                g_still[g_sel] = p;
                UpdatePicNum();
                InvalidateRect(h, NULL, TRUE);
            }
        }
        return 0;
    }

    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_DESTROY: g_win = NULL; g_list = NULL; g_detail = NULL; g_picnum = NULL; g_sel = -1; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShipWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { SetForegroundWindow(g_win); return; }
    if (!Hull_Load()) {
        wchar_t s[256];
        wsprintfW(s, L"선체표를 못 읽었습니다 (사유 %d).\n한국어판 Ver.1.2.0.0 이 아닌 것 같습니다.",
                  Hull_Status());
        MessageBoxW(owner, s, L"선체", MB_ICONWARNING);
        return;
    }
    ShipStil_Load();

    if (!registered) {
        WNDCLASSW wc;
        INITCOMMONCONTROLSEX ic;
        ic.dwSize = sizeof(ic); ic.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&ic);
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = WinProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ShipInfoKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"ShipInfoKRWin", L"선체 — ShipInfoKR",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
                owner, NULL, g_hinst, NULL);
    if (g_win) { ShowWindow(g_win, SW_SHOW); UpdateWindow(g_win); }
}

// ================================================================== 메뉴 설치 + 서브클래싱

static HWND    g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static int     g_pass = 0;

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_SHIP_OPEN) { ShipWin_Show(h); return 0; }
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
    LogW(L"[ShipInfoKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_SHIP_OPEN)) {
                HMENU modMenu;
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                if (!modMenu) { Sleep(1000); continue; }
                AppendMenuW(modMenu, MF_STRING, ID_SHIP_OPEN, L"선체");
                DrawMenuBar(g_gameHwnd);
                LogW(L"[ShipInfoKR] 선체 menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                LogW(L"[ShipInfoKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void ShipInfoKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
