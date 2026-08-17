#include <windows.h>
#include <commctrl.h>
#include "bookwin.h"
#include "bookdb.h"
#include "hintdb.h"        // HintUtilKR/src
#include "skilldb.h"       // SkillUtilKR/src — 언어 이름 · 건물표
#include "cities_data.h"   // TradeUtilKR/src — kCities[226]

// BookUtilKR — 도서관 서적 257권 일람. 읽기 전용이다.
//
//   · 어느 도서관에 꽂혀 있는지(최대 8곳), 함대에서 가까운 순으로
//   · 무슨 힌트를 주는지(최대 8개) + 그 힌트를 이미 얻었는지
//   · 무슨 언어로 쓰였는지 + 우리 함대의 그 언어 최고 수준 (3 이라야 읽는다)
//   · 게임과 똑같은 책등 색 — 게임 함수 0x4716A0 을 그대로 부른다

#define ID_BOOK_OPEN 0xC300u   // "파일>모드>서적"
                               // (… Market=0xBD00, Save=0xBE00, Pic=0xBF00,
                               //  Trade=0xC0xx, Dialog=0xC100, Skill=0xC200 과 안 겹치게)

#define ID_LIST     1001
#define ID_FILTER0  1010
#define ID_FILTER_N 6
#define ID_DETAIL   1020
#define ID_STATUS   1021
#define ID_CITY     1030
#define ID_CITYLBL  1031

#define TOP_H     28
#define DETAIL_H  190
#define BOT_H     24
#define CITY_W    172       // 왼쪽 도시 목록 너비

static HINSTANCE g_hinst = NULL;
static HWND      g_win = NULL, g_list = NULL, g_detail = NULL, g_status = NULL;
static HWND      g_cityList = NULL;
static int       g_filter = 0;
static int       g_map[BOOK_N];
static int       g_rows = 0;

// 왼쪽 도시 목록 — 책이 한 권이라도 놓인 도시만 담는다.
// 콤보박스는 쓰지 않는다. 게임 DirectDraw 화면 위에서 펼칠 때 별도 최상위 창을 띄우고
// 캡처·포커스를 가져가 게임이 죽는다(TradeUtilKR·CharacterUtilKR 이 같은 이유로 피한다).
// 리스트박스는 그냥 자식 창이라 안전하다.
#define CITY_MAX  256
static int       g_cityIds[CITY_MAX];   // 목록 줄(0번 "전체" 제외) -> 도시 번호
static int       g_cityN = 0;
static int       g_cityPick = -1;       // 고른 도시. -1 이면 전체

static const wchar_t* kFilterName[ID_FILTER_N] = {
    L"전체", L"힌트 있는 책", L"파랑(읽으면 힌트)", L"빨강(조건 미달)", L"초록(볼 일 없음)", L"지금 도시"
};
static const wchar_t* kColorName[3] = { L"초록", L"파랑", L"빨강" };

static const wchar_t* CityName(int c)
{
    return (c >= 0 && c < (int)(sizeof(kCities)/sizeof(kCities[0]))) ? kCities[c].name : L"?";
}
static const wchar_t* LangName(int v)
{
    return (v >= 0) ? SkillDb_LangName(v) : L"—";
}

// DebugView 로 도시 고르기가 어디서 어긋나는지 본다.
static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

// 안전한 이어 붙이기 — 넘치면 조용히 자른다.
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

static int InFilter(int k)
{
    switch (g_filter) {
    case 1:  return Book_HintCount(k) > 0;
    case 2:  return Book_Color(k) == BOOK_C_BLUE;
    case 3:  return Book_Color(k) == BOOK_C_RED;
    case 4:  return Book_Color(k) == BOOK_C_GREEN;
    case 5: {
        int cur = Book_CurrentCity(), i;
        if (cur < 0) return 0;
        for (i = 0; i < BOOK_SLOTS; i++) if (Book_City(k, i) == cur) return 1;
        return 0;
    }
    default: return 1;
    }
}

// 고른 도시에 놓인 책인가. 안 골랐으면 다 통과.
static int InCityPick(int k)
{
    int i;
    if (g_cityPick < 0) return 1;
    for (i = 0; i < BOOK_SLOTS; i++)
        if (Book_City(k, i) == g_cityPick) return 1;
    return 0;
}

// 책이 놓인 도시를 모아 왼쪽 목록을 채운다. 도시마다 몇 권인지 같이 적고,
// 도서관 건물이 없는 도시는 ※ 로 표시한다(상세 창의 표기와 같다).
static void BuildCityList(void)
{
    static unsigned char seen[CITY_MAX];
    static short count[CITY_MAX];
    int k, i, c, row;

    if (!g_cityList) return;
    ZeroMemory(seen, sizeof(seen));
    ZeroMemory(count, sizeof(count));
    g_cityN = 0;

    for (k = 0; k < Book_Count(); k++)
        for (i = 0; i < BOOK_SLOTS; i++) {
            c = Book_City(k, i);
            if (c < 0 || c >= CITY_MAX) continue;
            if (!seen[c]) seen[c] = 1;
            count[c]++;
        }

    SendMessageW(g_cityList, LB_RESETCONTENT, 0, 0);
    SendMessageW(g_cityList, LB_ADDSTRING, 0, (LPARAM)L"(전체 도시)");
    for (c = 0; c < CITY_MAX; c++) {
        wchar_t s[96];
        if (!seen[c]) continue;
        wsprintfW(s, L"%s  %d권%s", CityName(c), count[c],
                  Book_CityHasLibrary(c) == 0 ? L"  ※" : L"");
        row = (int)SendMessageW(g_cityList, LB_ADDSTRING, 0, (LPARAM)s);
        // 줄 번호와 도시 번호를 줄 자체에 붙여 둔다. 곁 배열로 맞추면 한 줄만 어긋나도
        // 엉뚱한 도시가 걸리므로, 목록이 스스로 들고 있게 한다.
        if (row >= 0) SendMessageW(g_cityList, LB_SETITEMDATA, (WPARAM)row, (LPARAM)c);
        if (g_cityN < CITY_MAX) g_cityIds[g_cityN++] = c;
    }
    SendMessageW(g_cityList, LB_SETCURSEL, 0, 0);
    g_cityPick = -1;
    LogW(L"[BookUtilKR] 도시 목록 %d곳 (줄 %d개)", g_cityN,
         (int)SendMessageW(g_cityList, LB_GETCOUNT, 0, 0));
}

static void CitiesText(int k, wchar_t* out, int cap)
{
    int i, n = 0;
    out[0] = 0;
    for (i = 0; i < BOOK_SLOTS; i++) {
        int c = Book_City(k, i);
        if (c == -1) continue;
        if (n++) Cat(out, cap, L", ");
        Cat(out, cap, CityName(c));
    }
    if (!n) Cat(out, cap, L"-");
}

static void HintsText(int k, wchar_t* out, int cap)
{
    int i, n = 0;
    out[0] = 0;
    for (i = 0; i < BOOK_SLOTS; i++) {
        int h = Book_Hint(k, i), st;
        if (h == -1) continue;
        if (n++) Cat(out, cap, L", ");
        Cat(out, cap, HintDb_Name(h));
        st = HintDb_State(h);
        if (st >= 0) Cat(out, cap, HINT_IS_DONE(st) ? L"(발견)" : ((st & 1) ? L"(얻음)" : L"(미)"));
    }
    if (!n) Cat(out, cap, L"-");
}

static void SetStatus(void)
{
    wchar_t s[512];
    int k, withHint = 0, todo = 0;
    for (k = 0; k < Book_Count(); k++) {
        if (Book_HintCount(k) > 0) withHint++;
        if (Book_NewHints(k) > 0) todo++;
    }
    if (Book_Live())
        wsprintfW(s, L"서적 %d권 · 힌트 있는 책 %d권 · 아직 못 얻은 힌트를 주는 책 %d권 · 보이는 줄 %d",
                  Book_Count(), withHint, todo, g_rows);
    else
        wsprintfW(s, L"서적 %d권 · 힌트 있는 책 %d권 · (세이브를 불러오기 전이라 색·힌트 상태는 모릅니다)",
                  Book_Count(), withHint);
    if (g_cityPick >= 0) {
        wchar_t t[128];
        wsprintfW(t, L"   ◀ %s 에 놓인 책만", CityName(g_cityPick));
        Cat(s, 512, t);
    }
    SetWindowTextW(g_status, s);
}

static void FillList(void)
{
    int k;
    ListView_DeleteAllItems(g_list);
    g_rows = 0;
    for (k = 0; k < Book_Count(); k++) {
        LVITEMW it;
        wchar_t buf[1024];
        int col;
        if (!InFilter(k) || !InCityPick(k)) continue;
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = g_rows; it.iSubItem = 0;
        col = Book_Color(k);
        it.pszText = (LPWSTR)((col >= 0 && col <= 2) ? kColorName[col] : L"-");
        it.lParam = k;
        ListView_InsertItem(g_list, &it);
        ListView_SetItemText(g_list, g_rows, 1, (LPWSTR)Book_Title(k));
        ListView_SetItemText(g_list, g_rows, 2, (LPWSTR)Book_Author(k));
        ListView_SetItemText(g_list, g_rows, 3, (LPWSTR)LangName(Book_Lang(k)));
        wsprintfW(buf, L"%d", Book_Year(k));
        ListView_SetItemText(g_list, g_rows, 4, buf);
        CitiesText(k, buf, 1024);
        ListView_SetItemText(g_list, g_rows, 5, buf);
        HintsText(k, buf, 1024);
        ListView_SetItemText(g_list, g_rows, 6, buf);
        g_map[g_rows] = k;
        g_rows++;
    }
    SetStatus();
}

static void ShowDetail(int k)
{
    wchar_t s[4096];
    int i, n, col, lv;
    int city[BOOK_SLOTS], dist[BOOK_SLOTS];

    s[0] = 0;
    if (k < 0) { SetWindowTextW(g_detail, L""); return; }

    CatF(s, 4096, L"《%s》  %s   %d년\r\n\r\n", Book_Title(k), Book_Author(k), Book_Year(k));

    // 한 줄에 몰아 쓰면 "3 / 3" 같은 말이 되어 무슨 수인지 알 수 없다. 줄을 갈라 이름을 붙인다.
    lv = Book_LangLevel(k);
    CatF(s, 4096, L"언어        %s 로 쓰인 책\r\n", LangName(Book_Lang(k)));
    CatF(s, 4096, L"읽기 조건   %s %d레벨 이상\r\n", LangName(Book_Lang(k)), BOOK_LANG_LV);
    if (lv >= 0)
        CatF(s, 4096, L"함대 최고   %d레벨  (이 언어를 제일 잘하는 사람)  →  %s\r\n",
             lv, (lv >= BOOK_LANG_LV) ? L"읽을 수 있다" : L"모자라서 못 읽는다");
    else
        Cat(s, 4096, L"함대 최고   (세이브를 불러오기 전이라 모름)\r\n");

    col = Book_Color(k);
    Cat(s, 4096, L"책등        ");
    if (col == BOOK_C_BLUE)       Cat(s, 4096, L"파랑 — 읽으면 새 힌트가 들어온다\r\n");
    else if (col == BOOK_C_RED)   Cat(s, 4096, L"빨강 — 줄 힌트는 남았는데 조건이 모자란다\r\n");
    else if (col == BOOK_C_GREEN) Cat(s, 4096, L"초록 — 이 책이 줄 새 힌트가 없다\r\n");
    else                          Cat(s, 4096, L"(세이브를 불러오기 전이라 모름)\r\n");
    if (Book_Read(k) > 0) Cat(s, 4096, L"읽음        이미 읽은 책이다\r\n");

    // 놓인 도서관 — 함대에서 가까운 순
    n = 0;
    for (i = 0; i < BOOK_SLOTS; i++) {
        int c = Book_City(k, i);
        if (c == -1) continue;
        city[n] = c; dist[n] = Book_CityDistance(c); n++;
    }
    for (i = 1; i < n; i++) {                       // 삽입 정렬 — 여덟 개뿐이다
        int c = city[i], dd = dist[i], j = i - 1;
        while (j >= 0 && dist[j] > dd) { city[j+1] = city[j]; dist[j+1] = dist[j]; j--; }
        city[j+1] = c; dist[j+1] = dd;
    }
    CatF(s, 4096, L"\r\n놓인 도서관 (%d곳)\r\n", n);
    for (i = 0; i < n; i++) {
        int lib = Book_CityHasLibrary(city[i]);
        CatF(s, 4096, L"    %-14s", CityName(city[i]));
        if (dist[i] >= 0) CatF(s, 4096, L"  %6d칸", dist[i]);
        else              Cat(s, 4096, L"        ");
        if (lib == 0) Cat(s, 4096, L"   ※ 도서관 건물이 없다");
        if (city[i] == Book_CurrentCity()) Cat(s, 4096, L"   ← 지금 여기");
        Cat(s, 4096, L"\r\n");
    }

    // 주는 힌트
    n = Book_HintCount(k);
    CatF(s, 4096, L"\r\n주는 힌트 (%d)\r\n", n);
    if (!n) Cat(s, 4096, L"    없다 — 읽어도 얻을 것이 없는 책이다.\r\n");
    for (i = 0; i < BOOK_SLOTS; i++) {
        int h = Book_Hint(k, i), st;
        if (h == -1) continue;
        st = HintDb_State(h);
        CatF(s, 4096, L"    [%3d] %-16s %-6s ", h, HintDb_Name(h), HintDb_CatName(HintDb_Cat(h)));
        if (st < 0)                 Cat(s, 4096, L"상태 모름");
        else if (HINT_IS_DONE(st))  Cat(s, 4096, L"발견까지 마침");
        else if (st & 1)            Cat(s, 4096, L"힌트를 이미 얻음");
        else                        Cat(s, 4096, L"아직 없음   ← 이 책으로 얻을 수 있다");
        Cat(s, 4096, L"\r\n");
    }

    SetWindowTextW(g_detail, s);
}

// ------------------------------------------------------------------ 창

static void CreateChildren(HWND h)
{
    const wchar_t* titles[7] = { L"색", L"제목", L"저자", L"언어", L"등장", L"도서관", L"주는 힌트" };
    int widths[7] = { 44, 180, 140, 100, 52, 280, 320 };
    LVCOLUMNW c;
    int i;

    g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_LIST, g_hinst, NULL);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    for (i = 0; i < 7; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; ListView_InsertColumn(g_list, i, &c); }

    for (i = 0; i < ID_FILTER_N; i++)
        CreateWindowExW(0, L"BUTTON", kFilterName[i],
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
            0, 0, 10, 10, h, (HMENU)(UINT_PTR)(ID_FILTER0 + i), g_hinst, NULL);
    SendMessageW(GetDlgItem(h, ID_FILTER0), BM_SETCHECK, BST_CHECKED, 0);

    CreateWindowExW(0, L"STATIC", L"도시로 추리기 (※ = 도서관 건물 없음)",
                WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_CITYLBL, g_hinst, NULL);
    g_cityList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_GROUP |
                LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_CITY, g_hinst, NULL);

    g_detail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_GROUP |
                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_DETAIL, g_hinst, NULL);

    g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_STATUS, g_hinst, NULL);

    {   // 목록·단추는 기본 GUI 글꼴, 상세는 고정폭이라야 칸이 맞는다.
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND c2 = GetWindow(h, GW_CHILD);
        LOGFONTW lf;
        static HFONT mono = NULL;
        while (c2) { SendMessageW(c2, WM_SETFONT, (WPARAM)f, TRUE); c2 = GetWindow(c2, GW_HWNDNEXT); }
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -12; lf.lfCharSet = HANGEUL_CHARSET;
        lstrcpyW(lf.lfFaceName, L"굴림체");
        if (!mono) mono = CreateFontIndirectW(&lf);
        if (mono) SendMessageW(g_detail, WM_SETFONT, (WPARAM)mono, TRUE);
    }
}

static void LayoutChildren(HWND h, int cw, int ch)
{
    int listH = ch - TOP_H - DETAIL_H - BOT_H - 8;
    int i, dw;
    if (listH < 80) listH = 80;
    for (i = 0; i < ID_FILTER_N; i++)
        MoveWindow(GetDlgItem(h, ID_FILTER0 + i), 8 + i * 138, 5, 134, 20, TRUE);
    MoveWindow(GetDlgItem(h, ID_CITYLBL), 8 + ID_FILTER_N * 138, 7, 300, 18, TRUE);
    MoveWindow(g_cityList, 6, TOP_H, CITY_W, listH, TRUE);
    MoveWindow(g_list, 6 + CITY_W + 6, TOP_H, cw - CITY_W - 24, listH, TRUE);
    dw = cw - 12; if (dw < 100) dw = 100;
    MoveWindow(g_detail, 6, TOP_H + listH + 4, dw, DETAIL_H, TRUE);
    MoveWindow(g_status, 8, ch - BOT_H + 4, dw, 16, TRUE);
}

static LRESULT CALLBACK WinProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_win = h;
        CreateChildren(h);
        BuildCityList();
        FillList();
        ShowDetail(-1);
        return 0;

    case WM_SIZE:
        LayoutChildren(h, LOWORD(l), HIWORD(l));
        return 0;

    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)l;
        if (n->idFrom == ID_LIST && n->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* lv = (NMLISTVIEW*)l;
            if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED))
                ShowDetail((lv->iItem >= 0 && lv->iItem < g_rows) ? g_map[lv->iItem] : -1);
        }
        return 0;
    }

    case WM_COMMAND: {
        UINT id = LOWORD(w);
        if (id >= ID_FILTER0 && id < ID_FILTER0 + ID_FILTER_N) {
            g_filter = (int)(id - ID_FILTER0);
            FillList();
            ShowDetail(-1);
        }
        if (id == ID_CITY && HIWORD(w) == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_cityList, LB_GETCURSEL, 0, 0);
            // 0번 줄이 "(전체 도시)". 도시 번호는 줄에 붙여 둔 값에서 꺼낸다.
            LRESULT dat = (sel > 0) ? SendMessageW(g_cityList, LB_GETITEMDATA, (WPARAM)sel, 0)
                                    : (LRESULT)-1;
            g_cityPick = (sel <= 0 || dat == LB_ERR || dat < 0) ? -1 : (int)dat;
            LogW(L"[BookUtilKR] 도시 고름: 줄=%d 데이터=%d -> 도시=%d",
                 sel, (int)dat, g_cityPick);
            FillList();
            ShowDetail(-1);
            LogW(L"[BookUtilKR] 걸러낸 줄 %d개", g_rows);
        }
        return 0;
    }

    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_DESTROY:
        g_win = NULL; g_list = NULL; g_detail = NULL; g_status = NULL;
        g_cityList = NULL; g_cityPick = -1; g_cityN = 0;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void BookWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { SetForegroundWindow(g_win); return; }
    if (!Book_Load()) {
        wchar_t s[256];
        wsprintfW(s, L"서적표를 못 읽었습니다 (사유 %d).\n한국어판 Ver.1.2.0.0 이 아닌 것 같습니다.",
                  Book_Status());
        MessageBoxW(owner, s, L"서적", MB_ICONWARNING);
        return;
    }
    HintDb_Load();
    SkillDb_Load(g_hinst);      // 도서관 유무 · 언어 이름표. 실패해도 목록은 뜬다

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
        wc.lpszClassName = L"BookUtilKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"BookUtilKRWin", L"서적 — BookUtilKR",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1220, 720,
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
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_BOOK_OPEN) { BookWin_Show(h); return 0; }
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
    OutputDebugStringW(L"[BookUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_BOOK_OPEN)) {
                HMENU modMenu;
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                if (!modMenu) { Sleep(1000); continue; }
                AppendMenuW(modMenu, MF_STRING, ID_BOOK_OPEN, L"서적");
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[BookUtilKR] 서적 menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[BookUtilKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void BookKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
