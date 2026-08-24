#include <windows.h>
#include <commctrl.h>
#include "tavwin.h"
#include "tavdb.h"
#include "hint_rows.h"     // HintUtilKR/src — kHints[186] 이름·분류
#include "cities_data.h"   // TradeUtilKR/src — kCities[226] 이름
#include "modmenu.h"       // common/ — 모드 창 등록부

// TavernInfoKR — 술집 "정보를 듣는다" 대사 191줄이 **어느 도시에서 들리는지**를 고친다.
//
// 자리와 게임이 이 표를 쓰는 법은 tavdb.h 머리에, 뜯은 과정은
// note/TavernInfo-0x42E780.md 에 적어 뒀다.
//
// 유적 코드로 힌트 이름을 붙여 보여 준다 — 힌트 표(0x4D8E88, 80바이트 x 186)의 +0x00 이
// 이 표의 코드와 같은 값이라, 그 코드를 가진 힌트의 이름을 kHints 에서 꺼내면 된다.
// 한 코드에 힌트가 둘 붙는 줄도 있어(코드 2 = 힌트 2·161) 그때는 둘 다 적는다.

#define ID_TAV_OPEN 0xCA00u   // "파일 > 모드 > 술집 정보" (modmenu.h 의 다음 빈 자리)

#define HTAB_RVA   0xD8E88u   // 힌트 표 — discinst.h 의 DINST_HTAB_RVA 와 같은 자리
#define HTAB_SZ    80

#define ID_LIST      1001
#define ID_FILTER0   1010
#define ID_FILTER_N  4
#define ID_CODE      1020
#define ID_CITY0     1021     // +0..3
#define ID_CITYNAME0 1030     // +0..3
#define ID_APPLY     1040
#define ID_REVERT    1041
#define ID_HERE      1042
#define ID_CLEAR     1043
#define ID_SAVE      1050
#define ID_LOADFILE  1051
#define ID_REVERTALL 1052
#define ID_NAME      1060
#define ID_TEXT      1061
#define ID_ORIG      1062
#define ID_STATUS    1063
#define ID_LBL0      1070

#define TOP_H   28
#define BOT_H   62
#define PANE_W  330
#define CITY_N  TAV_CITY_N

static HINSTANCE g_hinst = NULL;
static HWND g_win = NULL, g_list = NULL, g_status = NULL;
static HWND g_name = NULL, g_text = NULL, g_orig = NULL;
static int  g_filter = 0;
static int  g_map[TAV_N];
static int  g_rows = 0;
static int  g_pick = -1;

static const wchar_t* kFilterName[ID_FILTER_N] = {
    L"전체", L"지금 도시에서 들림", L"도시 없음", L"고친 것"
};

static const wchar_t* CityName(int c)
{
    return (c >= 0 && c < (int)(sizeof(kCities)/sizeof(kCities[0]))) ? kCities[c].name : L"?";
}

// ------------------------------------------------------------------ 유적 코드 -> 힌트 이름

// 힌트 표에서 그 코드를 쓰는 힌트를 찾아 이름을 잇는다. 없으면 L"—".
static const wchar_t* HintNameOfCode(int code)
{
    static wchar_t buf[96];
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    MEMORY_BASIC_INFORMATION mbi;
    int h, n = 0;

    buf[0] = 0;
    if (!base) return L"—";
    if (!VirtualQuery(base + HTAB_RVA, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return L"—";

    for (h = 0; h < HINT_ROW_N; h++) {
        if (*(const int*)(base + HTAB_RVA + (unsigned)h * HTAB_SZ) != code) continue;
        if (n) lstrcatW(buf, L" · ");
        lstrcpynW(buf + lstrlenW(buf), kHints[h].name, 40);
        if (++n >= 2) break;                 // 둘까지만 — 칸이 좁다
    }
    return buf[0] ? buf : L"—";
}

// ------------------------------------------------------------------ 목록

static void CitiesText(const TavRow* r, wchar_t* out, int cap)
{
    int k, n = 0;
    out[0] = 0;
    for (k = 0; k < CITY_N; k++) {
        wchar_t one[48];
        if (r->city[k] == TAV_NONE) continue;
        wsprintfW(one, L"%s%s", n ? L", " : L"", CityName(r->city[k]));
        if (lstrlenW(out) + lstrlenW(one) + 1 < cap) lstrcatW(out, one);
        n++;
    }
    if (!n) lstrcpyW(out, L"— (아무 도시에서나)");
}

static int HasCity(const TavRow* r, int c)
{
    int k;
    for (k = 0; k < CITY_N; k++) if (r->city[k] == c) return 1;
    return 0;
}

static int InFilter(int i)
{
    TavRow r;
    if (!TavDb_Get(i, &r)) return 0;
    switch (g_filter) {
    case 1: { int c = TavDb_CurCity(); return c >= 0 && HasCity(&r, c); }
    case 2: { int k; for (k = 0; k < CITY_N; k++) if (r.city[k] != TAV_NONE) return 0; return 1; }
    case 3: return TavDb_Changed(i);
    default: return 1;
    }
}

static void SetStatus(void)
{
    wchar_t s[256];
    int c = TavDb_CurCity();
    wsprintfW(s, L"%d줄 보임 · 고친 것 %d개   지금 도시 %s",
              g_rows, TavDb_ChangedCount(),
              (c >= 0) ? CityName(c) : L"— (항해 중이거나 세이브 전)");
    SetWindowTextW(g_status, s);
}

static void FillList(void)
{
    int i;
    ListView_DeleteAllItems(g_list);
    g_rows = 0;
    for (i = 0; i < TAV_N; i++) {
        LVITEMW it;
        wchar_t buf[512];
        TavRow r;
        if (!InFilter(i) || !TavDb_Get(i, &r)) continue;

        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = g_rows; it.iSubItem = 0;
        wsprintfW(buf, L"%s%d", TavDb_Changed(i) ? L"★ " : L"", i);
        it.pszText = buf;
        it.lParam = i;
        ListView_InsertItem(g_list, &it);

        wsprintfW(buf, L"%d", r.code);
        ListView_SetItemText(g_list, g_rows, 1, buf);
        ListView_SetItemText(g_list, g_rows, 2, (LPWSTR)HintNameOfCode(r.code));
        CitiesText(&r, buf, 512);
        ListView_SetItemText(g_list, g_rows, 3, buf);
        ListView_SetItemText(g_list, g_rows, 4, (LPWSTR)TavDb_Text(i));

        g_map[g_rows] = i;
        g_rows++;
    }
    SetStatus();
}

static void ReselectPick(void)
{
    int r;
    for (r = 0; r < g_rows; r++) {
        if (g_map[r] != g_pick) continue;
        ListView_SetItemState(g_list, r, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_list, r, FALSE);
        return;
    }
}

// ------------------------------------------------------------------ 편집판

#define BAD_NUM 0x7FFFFFFF

static void SetNum(int id, int v)
{
    wchar_t s[24];
    if (v == TAV_NONE) s[0] = 0; else wsprintfW(s, L"%d", v);
    SetDlgItemTextW(g_win, id, s);
}

static int GetNum(int id)
{
    wchar_t s[24];
    const wchar_t* p = s;
    int sign = 1, v = 0, digits = 0;
    if (!GetDlgItemTextW(g_win, id, s, 24)) return TAV_NONE;
    while (*p == L' ') p++;
    if (*p == L'-') { sign = -1; p++; }
    while (*p >= L'0' && *p <= L'9') { v = v * 10 + (*p - L'0'); p++; digits++; }
    while (*p == L' ') p++;
    if (!digits || *p) return digits ? BAD_NUM : TAV_NONE;
    return sign * v;
}

// 도시 칸 옆에 이름을 적어 준다 — 번호만 보고는 어디인지 알 수 없다.
static void RefreshCityNames(void)
{
    int k;
    for (k = 0; k < CITY_N; k++) {
        int v = GetNum(ID_CITY0 + k);
        const wchar_t* nm = (v == TAV_NONE) ? L"(비움)"
                          : (v == BAD_NUM)  ? L"?"
                          : CityName(v);
        SetDlgItemTextW(g_win, ID_CITYNAME0 + k, nm);
    }
}

static void ShowPick(int i)
{
    wchar_t s[512];
    TavRow now, org;
    int k;

    g_pick = i;
    if (i < 0 || !TavDb_Get(i, &now) || !TavDb_Orig(i, &org)) {
        SetWindowTextW(g_name, L"(왼쪽에서 줄을 고르세요)");
        SetWindowTextW(g_text, L"");
        SetWindowTextW(g_orig, L"");
        SetNum(ID_CODE, TAV_NONE);
        for (k = 0; k < CITY_N; k++) SetNum(ID_CITY0 + k, TAV_NONE);
        RefreshCityNames();
        return;
    }

    wsprintfW(s, L"[%d] 유적 코드 %d — %s", i, now.code, HintNameOfCode(now.code));
    SetWindowTextW(g_name, s);
    SetWindowTextW(g_text, TavDb_Text(i));

    CitiesText(&org, s, 512);
    {
        wchar_t t[600];
        wsprintfW(t, L"원본  코드 %d · %s%s", org.code, s,
                  TavDb_Changed(i) ? L"   ★ 고쳐진 상태" : L"");
        SetWindowTextW(g_orig, t);
    }

    SetNum(ID_CODE, now.code);
    for (k = 0; k < CITY_N; k++) SetNum(ID_CITY0 + k, now.city[k]);
    RefreshCityNames();
}

static void DoApply(void)
{
    TavRow r;
    int k, n = 0;
    if (g_pick < 0) return;

    r.code = GetNum(ID_CODE);
    for (k = 0; k < CITY_N; k++) r.city[k] = GetNum(ID_CITY0 + k);

    if (r.code == BAD_NUM || r.code == TAV_NONE) {
        MessageBoxW(g_win, L"유적 코드는 비울 수 없습니다. 숫자를 넣어 주세요.",
                    L"술집 정보", MB_ICONWARNING);
        return;
    }
    for (k = 0; k < CITY_N; k++)
        if (r.city[k] == BAD_NUM) {
            MessageBoxW(g_win, L"도시 칸에는 숫자만 넣어 주세요. 비우면 그 칸을 안 씁니다.",
                        L"술집 정보", MB_ICONWARNING);
            return;
        }
    // 빈 칸이 가운데 끼면 게임이 그 뒤를 안 본다 — 앞으로 몰아 준다.
    for (k = 0; k < CITY_N; k++) if (r.city[k] != TAV_NONE) r.city[n++] = r.city[k];
    while (n < CITY_N) r.city[n++] = TAV_NONE;

    if (!TavDb_RowOk(&r)) {
        wchar_t s[256];
        wsprintfW(s, L"넣을 수 없는 값입니다.\n\n도시는 0~%d 이어야 합니다.", TAV_CITY_MAX);
        MessageBoxW(g_win, s, L"술집 정보", MB_ICONWARNING);
        return;
    }
    if (!TavDb_Set(g_pick, &r)) {
        MessageBoxW(g_win, L"표에 쓰지 못했습니다.", L"술집 정보", MB_ICONWARNING);
        return;
    }
    FillList();
    ReselectPick();
    ShowPick(g_pick);
}

// 지금 도시를 빈 칸에 넣는다. 빈 칸이 없으면 첫 칸을 바꾼다.
static void DoHere(void)
{
    int c = TavDb_CurCity(), k;
    if (c < 0) {
        MessageBoxW(g_win, L"지금 도시를 모릅니다.\n항구에 들어간 뒤에 눌러 주세요.",
                    L"술집 정보", MB_ICONINFORMATION);
        return;
    }
    for (k = 0; k < CITY_N; k++)
        if (GetNum(ID_CITY0 + k) == TAV_NONE) { SetNum(ID_CITY0 + k, c); RefreshCityNames(); return; }
    SetNum(ID_CITY0, c);
    RefreshCityNames();
}

static void DoClear(void)
{
    int k;
    for (k = 0; k < CITY_N; k++) SetNum(ID_CITY0 + k, TAV_NONE);
    RefreshCityNames();
}

static void DoSave(void)
{
    wchar_t s[MAX_PATH + 200], path[MAX_PATH];
    int n = TavDb_Save(g_hinst);
    TavDb_JsonPath(g_hinst, path, MAX_PATH);
    if (n < 0) {
        wsprintfW(s, L"파일을 못 썼습니다.\n\n%s\n\n폴더에 쓰기 권한이 있는지 보세요.", path);
        MessageBoxW(g_win, s, L"술집 정보", MB_ICONWARNING);
        return;
    }
    wsprintfW(s, L"고친 %d줄을 적었습니다.\n\n%s\n\n다음에 게임을 켤 때 이 파일을 읽어 그대로 넣습니다.",
              n, path);
    MessageBoxW(g_win, s, L"술집 정보", MB_ICONINFORMATION);
}

static void DoLoadFile(void)
{
    wchar_t s[MAX_PATH + 200], path[MAX_PATH];
    int n = TavDb_Apply(g_hinst);
    TavDb_JsonPath(g_hinst, path, MAX_PATH);
    FillList();
    ReselectPick();
    ShowPick(g_pick);
    if (n > 0) wsprintfW(s, L"%d줄을 파일에서 읽어 넣었습니다.\n\n%s", n, path);
    else       wsprintfW(s, L"읽을 것이 없습니다.\n\n%s\n\n파일이 없거나 비어 있습니다.", path);
    MessageBoxW(g_win, s, L"술집 정보", MB_ICONINFORMATION);
}

static void DoRevertAll(void)
{
    if (!TavDb_ChangedCount()) return;
    if (MessageBoxW(g_win, L"고친 것을 모두 게임 원본으로 되돌릴까요?\n"
                           L"(파일은 그대로 둡니다 — 다시 켜면 파일 값이 또 들어갑니다)",
                    L"술집 정보", MB_ICONQUESTION | MB_YESNO) != IDYES) return;
    TavDb_RevertAll();
    FillList();
    ReselectPick();
    ShowPick(g_pick);
}

// ------------------------------------------------------------------ 창

static HWND Lbl(HWND h, const wchar_t* s, int id)
{
    return CreateWindowExW(0, L"STATIC", s, WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                           0, 0, 10, 10, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
}
static void Btn(HWND h, const wchar_t* s, int id)
{
    CreateWindowExW(0, L"BUTTON", s, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 10, 10, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
}
static void Num(HWND h, int id)
{
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                    0, 0, 10, 10, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
}

static void CreateChildren(HWND h)
{
    const wchar_t* titles[5] = { L"번호", L"코드", L"힌트", L"들리는 도시", L"대사" };
    int widths[5] = { 56, 52, 150, 240, 420 };
    LVCOLUMNW c;
    int i;

    g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_LIST, g_hinst, NULL);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    for (i = 0; i < 5; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; ListView_InsertColumn(g_list, i, &c); }

    for (i = 0; i < ID_FILTER_N; i++)
        CreateWindowExW(0, L"BUTTON", kFilterName[i],
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
            0, 0, 10, 10, h, (HMENU)(UINT_PTR)(ID_FILTER0 + i), g_hinst, NULL);
    SendMessageW(GetDlgItem(h, ID_FILTER0), BM_SETCHECK, BST_CHECKED, 0);

    g_name = Lbl(h, L"(왼쪽에서 줄을 고르세요)", ID_NAME);
    g_text = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_TEXT, g_hinst, NULL);

    Lbl(h, L"유적 코드", ID_LBL0 + 0);
    Num(h, ID_CODE);
    Lbl(h, L"들리는 도시 (비우면 그 칸은 안 씁니다)", ID_LBL0 + 1);
    for (i = 0; i < CITY_N; i++) { Num(h, ID_CITY0 + i); Lbl(h, L"", ID_CITYNAME0 + i); }

    Btn(h, L"지금 도시 넣기", ID_HERE);
    Btn(h, L"도시 비우기", ID_CLEAR);
    Btn(h, L"적용", ID_APPLY);
    Btn(h, L"이 줄 원본으로", ID_REVERT);
    g_orig = Lbl(h, L"", ID_ORIG);

    Btn(h, L"파일에 저장", ID_SAVE);
    Btn(h, L"파일에서 넣기", ID_LOADFILE);
    Btn(h, L"모두 원본으로", ID_REVERTALL);
    g_status = Lbl(h, L"", ID_STATUS);

    {
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND c2 = GetWindow(h, GW_CHILD);
        while (c2) { SendMessageW(c2, WM_SETFONT, (WPARAM)f, TRUE); c2 = GetWindow(c2, GW_HWNDNEXT); }
    }
}

static void LayoutChildren(HWND h, int cw, int ch)
{
    int listW = cw - PANE_W - 18;
    int listH = ch - TOP_H - BOT_H;
    int px = cw - PANE_W - 6, y, i;

    if (listW < 200) listW = 200;
    if (listH < 100) listH = 100;

    for (i = 0; i < ID_FILTER_N; i++)
        MoveWindow(GetDlgItem(h, ID_FILTER0 + i), 8 + i * 168, 5, 164, 20, TRUE);
    MoveWindow(g_list, 6, TOP_H, listW, listH, TRUE);

    y = TOP_H;
    MoveWindow(g_name, px, y, PANE_W - 12, 20, TRUE);          y += 24;
    MoveWindow(g_text, px, y, PANE_W - 12, 74, TRUE);          y += 80;
    MoveWindow(GetDlgItem(h, ID_LBL0 + 0), px, y + 4, 66, 18, TRUE);
    MoveWindow(GetDlgItem(h, ID_CODE),     px + 70, y, 70, 22, TRUE);
    y += 30;
    MoveWindow(GetDlgItem(h, ID_LBL0 + 1), px, y, PANE_W - 12, 18, TRUE);
    y += 22;
    for (i = 0; i < CITY_N; i++) {
        MoveWindow(GetDlgItem(h, ID_CITY0 + i),     px,      y, 70, 22, TRUE);
        MoveWindow(GetDlgItem(h, ID_CITYNAME0 + i), px + 78, y + 4, PANE_W - 96, 18, TRUE);
        y += 26;
    }
    y += 4;
    MoveWindow(GetDlgItem(h, ID_HERE),  px,       y, 150, 24, TRUE);
    MoveWindow(GetDlgItem(h, ID_CLEAR), px + 158, y, 150, 24, TRUE);
    y += 30;
    MoveWindow(GetDlgItem(h, ID_APPLY),  px,       y, 150, 26, TRUE);
    MoveWindow(GetDlgItem(h, ID_REVERT), px + 158, y, 150, 26, TRUE);
    y += 32;
    MoveWindow(g_orig, px, y, PANE_W - 12, 34, TRUE);

    y = ch - BOT_H + 4;
    MoveWindow(GetDlgItem(h, ID_SAVE),      8,   y, 124, 26, TRUE);
    MoveWindow(GetDlgItem(h, ID_LOADFILE),  140, y, 124, 26, TRUE);
    MoveWindow(GetDlgItem(h, ID_REVERTALL), 272, y, 124, 26, TRUE);
    MoveWindow(g_status, 8, y + 32, cw - 16, 18, TRUE);
}

static LRESULT CALLBACK WinProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_win = h;
        CreateChildren(h);
        FillList();
        ShowPick(-1);
        return 0;

    case WM_SIZE:
        LayoutChildren(h, LOWORD(l), HIWORD(l));
        return 0;

    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)l;
        if (n->idFrom == ID_LIST && n->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* lv = (NMLISTVIEW*)l;
            if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED))
                ShowPick((lv->iItem >= 0 && lv->iItem < g_rows) ? g_map[lv->iItem] : -1);
        }
        return 0;
    }

    case WM_COMMAND: {
        UINT id = LOWORD(w);
        if (id >= ID_FILTER0 && id < ID_FILTER0 + ID_FILTER_N) {
            g_filter = (int)(id - ID_FILTER0);
            FillList();
            ReselectPick();
            return 0;
        }
        // 도시 번호를 치는 대로 옆의 이름을 갱신한다
        if (id >= ID_CITY0 && id < ID_CITY0 + CITY_N && HIWORD(w) == EN_CHANGE) {
            RefreshCityNames();
            return 0;
        }
        switch (id) {
        case ID_APPLY:     DoApply();     return 0;
        case ID_HERE:      DoHere();      return 0;
        case ID_CLEAR:     DoClear();     return 0;
        case ID_REVERT:
            if (g_pick >= 0) { TavDb_Revert(g_pick); FillList(); ReselectPick(); ShowPick(g_pick); }
            return 0;
        case ID_SAVE:      DoSave();      return 0;
        case ID_LOADFILE:  DoLoadFile();  return 0;
        case ID_REVERTALL: DoRevertAll(); return 0;
        }
        return 0;
    }

    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_DESTROY:
        g_win = NULL; g_list = NULL; g_status = NULL;
        g_name = NULL; g_text = NULL; g_orig = NULL;
        g_pick = -1; g_rows = 0;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void TavWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { SetForegroundWindow(g_win); return; }
    if (!TavDb_Load()) {
        MessageBoxW(owner, L"술집 대사표를 못 읽었습니다.\n한국어판 Ver.1.2.0.0 이 아닌 것 같습니다.",
                    L"술집 정보", MB_ICONWARNING);
        return;
    }

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
        wc.lpszClassName = L"TavernInfoKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"TavernInfoKRWin", L"술집 정보 — TavernInfoKR",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1260, 680,
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
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_TAV_OPEN) { TavWin_Show(h); return 0; }
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
    OutputDebugStringW(L"[TavernInfoKR] menu monitor started.");

    // 표는 .rdata 라 프로세스가 뜬 그 순간부터 있다 — 세이브를 기다릴 것 없이 바로 넣는다.
    {
        int n = TavDb_Apply(g_hinst);
        if (n > 0) {
            wchar_t s[96];
            wsprintfW(s, L"[TavernInfoKR] tavern_info.json 에서 %d줄 적용.", n);
            OutputDebugStringW(s);
        }
    }

    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!(MenuHasId(target, ID_TAV_OPEN) || ModMenu_HasId(g_gameHwnd, ID_TAV_OPEN))) {
                HMENU modMenu;
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                if (!modMenu) { Sleep(1000); continue; }
                AppendMenuW(modMenu, MF_STRING, ID_TAV_OPEN, L"술집 정보");
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[TavernInfoKR] 술집 정보 menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[TavernInfoKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void TavernKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
