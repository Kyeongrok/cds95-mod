#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "skillwin.h"
#include "skilldb.h"

// SkillUtilKR — 도시마다 배울 수 있는 기능·언어를 조합/교회/학자 저택으로 갈라 고친다.
//
// 왼쪽 목록에서 한 줄을 고르면 오른쪽 체크칸이 그 자리의 마스크를 펼쳐 보인다.
// 체크를 누르는 즉시 메모리에 쓰고 skills.json 에 적는다(따로 [저장] 을 누를 필요 없다).
//
// 쓰는 자리가 둘이다 — 자세한 것은 skilldb.h 머리말.
//   · .rdata  : 다음 새 게임부터
//   · 런타임  : 지금 하는 게임에 바로 (세이브에도 들어간다)

#define ID_SKILL_OPEN 0xC200u   // "파일>모드>기능수련"
                                // (Trade=0xB10x/0xC0xx, Char=0xB301, Ship=0xB410, Patch=0xB500,
                                //  Map=0xB600, Mod=0xB700, QMod=0xB800, Upd=0xB900, Fatigue=0xBA00,
                                //  Hotkey=0xBB00, Hint=0xBC00, Market=0xBD00, Save=0xBE00,
                                //  Pic=0xBF00, Dialog=0xC100 과 안 겹치게)

#define ID_LIST      1001
#define ID_FILTER0   1010       // 조합 / 교회 / 학자저택 / 전체
#define ID_FILTER_N  4
#define ID_RESET     1020
#define ID_ALLON     1021
#define ID_ALLOFF    1022
#define ID_RESETALL  1023
#define ID_PUSHLIVE  1024
#define ID_OPENDIR   1025
#define ID_STATUS    1030
#define ID_GRP_SKILL 1040
#define ID_GRP_LANG  1041
#define ID_SKILL0    2000       // + 비트 (0~12)
#define ID_LANG0     2100       // + 비트 (0~13)

#define RIGHT_W      380        // 오른쪽 체크칸 폭
#define BOT_H        34
#define TOP_H        30
#define CHK_H        21

static HINSTANCE g_hinst = NULL;
static HWND      g_win = NULL, g_list = NULL, g_status = NULL;
static int       g_filter = 0;
static int       g_map[BLD_COUNT];      // 목록 줄 -> 표의 k
static int       g_rows = 0;
static int       g_sel = -1;            // 지금 고른 k. 없으면 -1
static int       g_syncing = 0;         // 체크칸을 코드가 건드리는 중 (WM_COMMAND 무시)

static const wchar_t* kFilterName[ID_FILTER_N] = { L"조합", L"교회", L"학자 저택", L"전체" };

static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

// 지금 눈에 보이는 값. 게임 중이면 런타임 사본이 진짜다.
static unsigned EffMask(int k)
{
    return SkillDb_GameLoaded() ? SkillDb_LiveMask(k) : SkillDb_Mask(k);
}

static int InFilter(int k)
{
    int code = SkillDb_Code(k);
    switch (g_filter) {
        case 0:  return code == BLD_GUILD;
        case 1:  return code == BLD_CHURCH;
        case 2:  return code >= BLD_HOUSE0 && code <= BLD_HOUSE1;
        default: return code == BLD_GUILD || code == BLD_CHURCH ||
                        (code >= BLD_HOUSE0 && code <= BLD_HOUSE1);
    }
}

// 마스크를 "항해술 회계 …" 처럼 편다. 아무것도 없으면 "-".
static void MaskText(unsigned mask, int wantLang, wchar_t* out, int cch)
{
    int i, n = 0, pos = 0;
    int cnt = wantLang ? LANG_N : SKILL_N;
    out[0] = 0;
    for (i = 0; i < cnt; i++) {
        const wchar_t* name;
        int need;
        if (!(mask >> (wantLang ? SKILL_N + i : i) & 1u)) continue;
        name = wantLang ? SkillDb_LangName(i) : SkillDb_SkillName(i);
        need = lstrlenW(name) + (n ? 1 : 0);
        if (pos + need + 1 >= cch) break;          // 남는 자리가 없으면 거기까지만
        if (n) out[pos++] = L' ';
        lstrcpyW(out + pos, name);
        pos += lstrlenW(name);
        n++;
    }
    out[pos] = 0;
    if (!n) lstrcpyW(out, L"-");
}

static void SetStatusText(void)
{
    wchar_t s[768], path[MAX_PATH];
    int k, changed = 0;
    for (k = 0; k < BLD_COUNT; k++) if (SkillDb_Changed(k)) changed++;
    SkillDb_JsonPath(path, MAX_PATH);
    wsprintfW(s, L"%s   |   고친 곳 %d   |   %s",
              SkillDb_GameLoaded()
                  ? L"게임 진행 중 — 고치면 바로 먹고 세이브에도 들어갑니다"
                  : L"세이브 전 — 고친 값은 다음 새 게임부터 먹습니다",
              changed, path);
    SetWindowTextW(g_status, s);
}

static void RefreshRow(int row)
{
    int k = g_map[row];
    unsigned m = EffMask(k);
    wchar_t buf[512];
    MaskText(m, 0, buf, 512); ListView_SetItemText(g_list, row, 3, buf);
    MaskText(m, 1, buf, 512); ListView_SetItemText(g_list, row, 4, buf);
    ListView_SetItemText(g_list, row, 5, (LPWSTR)(SkillDb_Changed(k) ? L"고침" : L""));
}

static void FillList(void)
{
    int k;
    ListView_DeleteAllItems(g_list);
    g_rows = 0;
    for (k = 0; k < SkillDb_Count(); k++) {
        LVITEMW it;
        wchar_t buf[512];
        if (!InFilter(k)) continue;
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = g_rows;
        it.iSubItem = 0;
        lstrcpynW(buf, SkillDb_CityName(k), 512);
        it.pszText = buf;
        it.lParam = k;
        ListView_InsertItem(g_list, &it);
        ListView_SetItemText(g_list, g_rows, 1, (LPWSTR)SkillDb_KindName(k));
        ListView_SetItemText(g_list, g_rows, 2, (LPWSTR)SkillDb_Name(k));
        g_map[g_rows] = k;
        RefreshRow(g_rows);
        g_rows++;
    }
    g_sel = -1;
}

// 고른 줄의 마스크를 체크칸에 펼친다. 줄이 없으면 전부 끄고 잠근다.
static void SyncChecks(void)
{
    unsigned m = (g_sel >= 0) ? EffMask(g_sel) : 0u;
    BOOL on = (g_sel >= 0);
    int i;
    g_syncing = 1;
    for (i = 0; i < SKILL_N; i++) {
        HWND h = GetDlgItem(g_win, ID_SKILL0 + i);
        SendMessageW(h, BM_SETCHECK, (m >> i & 1u) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(h, on);
    }
    for (i = 0; i < LANG_N; i++) {
        HWND h = GetDlgItem(g_win, ID_LANG0 + i);
        SendMessageW(h, BM_SETCHECK, (m >> (SKILL_N + i) & 1u) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(h, on);
    }
    EnableWindow(GetDlgItem(g_win, ID_RESET), on);
    EnableWindow(GetDlgItem(g_win, ID_ALLON), on);
    EnableWindow(GetDlgItem(g_win, ID_ALLOFF), on);
    g_syncing = 0;
}

// 고른 줄을 이 마스크로 바꾸고, 목록·체크칸·파일을 맞춘다.
static void ApplyMask(unsigned mask)
{
    int row;
    if (g_sel < 0) return;
    if (!SkillDb_SetMask(g_sel, mask)) {
        MessageBoxW(g_win, L"메모리에 쓰지 못했습니다.", L"기능수련", MB_ICONWARNING);
        return;
    }
    for (row = 0; row < g_rows; row++) if (g_map[row] == g_sel) { RefreshRow(row); break; }
    SyncChecks();
    SkillDb_SaveJson();
    SetStatusText();
}

static unsigned MaskFromChecks(void)
{
    unsigned m = 0;
    int i;
    for (i = 0; i < SKILL_N; i++)
        if (SendMessageW(GetDlgItem(g_win, ID_SKILL0 + i), BM_GETCHECK, 0, 0) == BST_CHECKED) m |= 1u << i;
    for (i = 0; i < LANG_N; i++)
        if (SendMessageW(GetDlgItem(g_win, ID_LANG0 + i), BM_GETCHECK, 0, 0) == BST_CHECKED) m |= 1u << (SKILL_N + i);
    return m;
}

static void ResetAll(void)
{
    int k, n = 0;
    if (MessageBoxW(g_win, L"고친 곳을 전부 EXE 원래 값으로 되돌립니다. 할까요?",
                    L"기능수련", MB_ICONQUESTION | MB_YESNO) != IDYES) return;
    for (k = 0; k < SkillDb_Count(); k++)
        if (SkillDb_Changed(k) && SkillDb_SetMask(k, SkillDb_OrigMask(k))) n++;
    FillList();
    SyncChecks();
    SkillDb_SaveJson();
    SetStatusText();
    LogW(L"[SkillUtilKR] %d곳을 원래대로 되돌렸다.", n);
}

// 세이브를 불러온 뒤에는 런타임 사본이 그 세이브에 든 값이다. 고친 값(.rdata / skills.json)
// 을 지금 하는 게임에도 밀어 넣는다.
static void PushToLive(void)
{
    int k, n = 0;
    wchar_t s[128];
    if (!SkillDb_GameLoaded()) {
        MessageBoxW(g_win, L"세이브를 불러온 뒤에 쓸 수 있습니다.", L"기능수련", MB_ICONINFORMATION);
        return;
    }
    for (k = 0; k < SkillDb_Count(); k++)
        if (SkillDb_LiveMask(k) != SkillDb_Mask(k) && SkillDb_SetMask(k, SkillDb_Mask(k))) n++;
    FillList();
    SyncChecks();
    SetStatusText();
    wsprintfW(s, L"%d곳을 지금 게임에 넣었습니다.", n);
    MessageBoxW(g_win, s, L"기능수련", MB_ICONINFORMATION);
}

static void OpenDataDir(HWND owner)
{
    wchar_t path[MAX_PATH];
    wchar_t* slash = path;
    wchar_t* q;
    SkillDb_JsonPath(path, MAX_PATH);
    for (q = path; *q; q++) if (*q == L'\\') slash = q;
    *slash = 0;
    ShellExecuteW(owner, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

// ------------------------------------------------------------------ 창

static void CreateChildren(HWND h)
{
    const wchar_t* titles[6] = { L"도시", L"종류", L"이름", L"기능", L"언어", L"상태" };
    int widths[6] = { 92, 76, 168, 210, 210, 48 };
    LVCOLUMNW c;
    int i;

    g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_LIST, g_hinst, NULL);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    for (i = 0; i < 6; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; ListView_InsertColumn(g_list, i, &c); }

    for (i = 0; i < ID_FILTER_N; i++)
        CreateWindowExW(0, L"BUTTON", kFilterName[i],
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
            0, 0, 10, 10, h, (HMENU)(UINT_PTR)(ID_FILTER0 + i), g_hinst, NULL);
    SendMessageW(GetDlgItem(h, ID_FILTER0), BM_SETCHECK, BST_CHECKED, 0);

    CreateWindowExW(0, L"BUTTON", L"기능",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX | WS_GROUP, 0, 0, 10, 10, h,
        (HMENU)(UINT_PTR)ID_GRP_SKILL, g_hinst, NULL);
    for (i = 0; i < SKILL_N; i++)
        CreateWindowExW(0, L"BUTTON", SkillDb_SkillName(i),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 10, 10, h,
            (HMENU)(UINT_PTR)(ID_SKILL0 + i), g_hinst, NULL);

    CreateWindowExW(0, L"BUTTON", L"언어",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 10, 10, h,
        (HMENU)(UINT_PTR)ID_GRP_LANG, g_hinst, NULL);
    for (i = 0; i < LANG_N; i++)
        CreateWindowExW(0, L"BUTTON", SkillDb_LangName(i),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 10, 10, h,
            (HMENU)(UINT_PTR)(ID_LANG0 + i), g_hinst, NULL);

    CreateWindowExW(0, L"BUTTON", L"이 줄 원래대로", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_RESET, g_hinst, NULL);
    CreateWindowExW(0, L"BUTTON", L"모두 켜기", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_ALLON, g_hinst, NULL);
    CreateWindowExW(0, L"BUTTON", L"모두 끄기", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_ALLOFF, g_hinst, NULL);
    CreateWindowExW(0, L"BUTTON", L"전체 되돌리기", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_RESETALL, g_hinst, NULL);
    CreateWindowExW(0, L"BUTTON", L"지금 게임에 넣기", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_PUSHLIVE, g_hinst, NULL);
    CreateWindowExW(0, L"BUTTON", L"skills.json 폴더", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_OPENDIR, g_hinst, NULL);

    g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
        0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_STATUS, g_hinst, NULL);

    {   // 게임 기본 글꼴을 자식 전부에 물린다(안 하면 시스템 고정폭이 나온다).
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND c2 = GetWindow(h, GW_CHILD);
        while (c2) { SendMessageW(c2, WM_SETFONT, (WPARAM)f, TRUE); c2 = GetWindow(c2, GW_HWNDNEXT); }
    }
}

static void LayoutChildren(HWND h, int cw, int ch)
{
    int listW = cw - RIGHT_W - 12;
    int listH = ch - TOP_H - BOT_H - 8;
    int rx, gw, gh, statW;
    int i, y;

    if (listW < 240) listW = 240;
    if (listH < 80)  listH = 80;
    rx = 6 + listW + 6;
    gw = (RIGHT_W - 14) / 2;                       // 상자 하나의 폭
    gh = LANG_N * CHK_H + 28;                      // 긴 쪽(언어)에 맞춘 높이

    for (i = 0; i < ID_FILTER_N; i++)
        MoveWindow(GetDlgItem(h, ID_FILTER0 + i), 8 + i * 92, 6, 88, 20, TRUE);
    MoveWindow(g_list, 6, TOP_H, listW, listH, TRUE);

    // 기능 · 언어 두 상자를 나란히. 체크칸은 상자 안쪽에 얹는다.
    MoveWindow(GetDlgItem(h, ID_GRP_SKILL), rx,          TOP_H, gw, gh, TRUE);
    MoveWindow(GetDlgItem(h, ID_GRP_LANG),  rx + gw + 6, TOP_H, gw, gh, TRUE);
    for (i = 0; i < SKILL_N; i++)
        MoveWindow(GetDlgItem(h, ID_SKILL0 + i),
                   rx + 10, TOP_H + 18 + i * CHK_H, gw - 18, CHK_H - 2, TRUE);
    for (i = 0; i < LANG_N; i++)
        MoveWindow(GetDlgItem(h, ID_LANG0 + i),
                   rx + gw + 16, TOP_H + 18 + i * CHK_H, gw - 18, CHK_H - 2, TRUE);

    y = ch - BOT_H + 4;
    MoveWindow(GetDlgItem(h, ID_RESET),    6,   y, 110, 24, TRUE);
    MoveWindow(GetDlgItem(h, ID_ALLON),    120, y, 80,  24, TRUE);
    MoveWindow(GetDlgItem(h, ID_ALLOFF),   204, y, 80,  24, TRUE);
    MoveWindow(GetDlgItem(h, ID_RESETALL), 288, y, 110, 24, TRUE);
    MoveWindow(GetDlgItem(h, ID_PUSHLIVE), 402, y, 130, 24, TRUE);
    MoveWindow(GetDlgItem(h, ID_OPENDIR),  536, y, 130, 24, TRUE);
    statW = cw - 680;
    if (statW < 40) statW = 40;
    MoveWindow(g_status, 674, y + 4, statW, 18, TRUE);
}

static LRESULT CALLBACK WinProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_win = h;
        CreateChildren(h);
        FillList();
        SyncChecks();
        SetStatusText();
        return 0;

    case WM_SIZE:
        LayoutChildren(h, LOWORD(l), HIWORD(l));
        return 0;

    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)l;
        if (n->idFrom == ID_LIST && n->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* lv = (NMLISTVIEW*)l;
            if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)) {
                g_sel = (lv->iItem >= 0 && lv->iItem < g_rows) ? g_map[lv->iItem] : -1;
                SyncChecks();
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        UINT id = LOWORD(w);
        if (id >= ID_FILTER0 && id < ID_FILTER0 + ID_FILTER_N) {
            g_filter = (int)(id - ID_FILTER0);
            FillList();
            SyncChecks();
            return 0;
        }
        if (!g_syncing &&
            ((id >= ID_SKILL0 && id < ID_SKILL0 + SKILL_N) ||
             (id >= ID_LANG0  && id < ID_LANG0  + LANG_N))) {
            ApplyMask(MaskFromChecks());
            return 0;
        }
        switch (id) {
        case ID_RESET:    if (g_sel >= 0) ApplyMask(SkillDb_OrigMask(g_sel)); return 0;
        case ID_ALLON:    ApplyMask(MASK_ALL); return 0;
        case ID_ALLOFF:   ApplyMask(0u); return 0;
        case ID_RESETALL: ResetAll(); return 0;
        case ID_PUSHLIVE: PushToLive(); return 0;
        case ID_OPENDIR:  OpenDataDir(h); return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        g_win = NULL; g_list = NULL; g_status = NULL; g_sel = -1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void SkillWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { SetForegroundWindow(g_win); return; }
    if (!SkillDb_Load(g_hinst)) {
        wchar_t s[256];
        wsprintfW(s, L"건물표를 못 읽었습니다 (사유 %d).\n한국어판 Ver.1.2.0.0 이 아닌 것 같습니다.",
                  SkillDb_Status());
        MessageBoxW(owner, s, L"기능수련", MB_ICONWARNING);
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
        wc.lpszClassName = L"SkillUtilKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"SkillUtilKRWin", L"기능수련 — SkillUtilKR",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 620,
                owner, NULL, g_hinst, NULL);
    if (g_win) { ShowWindow(g_win, SW_SHOW); UpdateWindow(g_win); }
}

// ================================================================== 메뉴 설치 + 서브클래싱
// DialogUtilKR/menu 와 같은 방식이다 — 1초 폴링으로 게임 창을 찾아 "파일 > 모드" 에 붙이고
// WM_COMMAND 를 서브클래싱으로 가로챈다.

static HWND    g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static int     g_pass = 0;

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_SKILL_OPEN) {
        SkillWin_Show(h);
        return 0;
    }
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

// "파일 > 모드" 를 찾거나(먼저 뜬 플러그인이 만들어 둔다) 두 바퀴째에 만든다.
static HMENU FindOrCreateModMenu(HMENU fileMenu, BOOL mayCreate)
{
    int i;
    WCHAR s[64];
    HMENU first = NULL, sub;
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
    OutputDebugStringW(L"[SkillUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_SKILL_OPEN)) {
                HMENU modMenu;
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                if (!modMenu) { Sleep(1000); continue; }   // 아직 "모드" 가 없다 — 다음 바퀴에
                AppendMenuW(modMenu, MF_STRING, ID_SKILL_OPEN, L"기능수련");
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[SkillUtilKR] 기능수련 menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[SkillUtilKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void SkillKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    // 표는 .rdata 라 게임이 뜨자마자 읽을 수 있다. 여기서 원본을 떠 두고 skills.json 을
    // 발라 둬야 이 다음 "새 게임" 이 고친 값으로 시작한다.
    if (SkillDb_Load(hinst)) SkillDb_ApplyJson();
    else LogW(L"[SkillUtilKR] 건물표 로드 실패 (사유 %d) — 메뉴만 단다.", SkillDb_Status());
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
