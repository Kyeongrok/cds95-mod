#include <windows.h>
#include <commctrl.h>
#include "dcwin.h"
#include "dccoord.h"
#include "disc.h"      // HintUtilKR/src — 이름 · 분류 · 발견 여부
#include "hintdb.h"    // HintUtilKR/src — 분류 이름표(게임 말투를 그대로 쓴다)
#include "modmenu.h"   // common/ — 모드 창 등록부(걷어 간 항목을 여기서 본다)

// DiscoveryEditKR — 발견물 274개의 좌표를 실행 중에 고친다.
//
// 표를 고치면 게임이 다음 판정부터 그대로 읽는다(까닭은 dccoord.h 머리에).
// 다만 좌표로 찾아지는 것은 앞 107개뿐이라, 목록의 [판정] 칸으로 그것을 갈라 보여 준다.
//
// 좌표를 어떻게 알아내나 — 게임에서 그 자리로 배를 몰고 간 다음 [지금 함대 자리]
// 를 누르면 된다. 함대의 경도·위도(0x1B63B0/0x1B63B4)를 16으로 나눈 것이 곧 표의
// 칸 번호라, 눈으로 본 자리가 그대로 들어간다. 지도에서 자리를 고르고 싶으면
// WorldMapKR 의 "지도" 창에서 마우스를 얹어 칸 번호를 읽어 와도 된다.

#define ID_DC_OPEN  0xC900u   // "파일 > 모드 > 발견물 좌표" (modmenu.h 의 다음 빈 자리)

#define ID_LIST      1001
#define ID_FILTER0   1010
#define ID_FILTER_N  5
#define ID_X1        1020
#define ID_Y1        1021
#define ID_X2        1022
#define ID_Y2        1023
#define ID_APPLY     1030
#define ID_REVERT    1031
#define ID_FLEET     1032
#define ID_POINT     1033
#define ID_SAVE      1040
#define ID_LOADFILE  1041
#define ID_REVERTALL 1042
#define ID_NAME      1050
#define ID_INFO      1051
#define ID_ORIG      1052
#define ID_STATUS    1053
#define ID_LBL0      1060

#define TOP_H    28
#define BOT_H    62        // 아래 단추줄 + 상태줄
#define PANE_W   306       // 오른쪽 편집판
#define ROW_H    24

static HINSTANCE g_hinst = NULL;
static HWND g_win = NULL, g_list = NULL, g_status = NULL;
static HWND g_name = NULL, g_info = NULL, g_orig = NULL;
static int  g_filter = 0;
static int  g_map[DC_N];    // 목록 줄 -> 발견물 번호
static int  g_rows = 0;
static int  g_pick = -1;    // 지금 고른 발견물 번호

static const wchar_t* kFilterName[ID_FILTER_N] = {
    L"전체", L"좌표로 찾는 것(0~106)", L"좌표 있음", L"좌표 없음", L"고친 것"
};

// ------------------------------------------------------------------ 글

static void BoxText(const DcBox* b, wchar_t* out)
{
    if (b->x1 == DC_NONE) { lstrcpyW(out, L"—"); return; }
    if (b->x1 == b->x2 && b->y1 == b->y2) wsprintfW(out, L"%d, %d", b->x1, b->y1);
    else wsprintfW(out, L"%d, %d ~ %d, %d", b->x1, b->y1, b->x2, b->y2);
}

static const wchar_t* FoundText(int i)
{
    switch (Disc_Found(i)) {
    case DISC_REPORTED: return L"보고까지";
    case DISC_FOUND:    return L"발견함";
    case DISC_HINTED:   return L"힌트만";
    case DISC_NOT:      return L"아직";
    case DISC_NOLINK:   return L"—";
    default:            return L"?";
    }
}

// ------------------------------------------------------------------ 목록

static int InFilter(int i)
{
    DcBox b;
    if (!DC_Get(i, &b)) return 0;
    switch (g_filter) {
    case 1: return DC_Judged(i);
    case 2: return b.x1 != DC_NONE;
    case 3: return b.x1 == DC_NONE;
    case 4: return DC_Changed(i);
    default: return 1;
    }
}

static void SetStatus(void)
{
    wchar_t s[512], path[MAX_PATH];
    int fx, fy;
    DC_JsonPath(g_hinst, path, MAX_PATH);
    wsprintfW(s, L"%d줄 보임 · 고친 것 %d개", g_rows, DC_ChangedCount());
    if (DC_FleetCell(&fx, &fy)) {
        wchar_t t[64];
        wsprintfW(t, L"   지금 함대 %d, %d", fx, fy);
        lstrcatW(s, t);
    } else {
        lstrcatW(s, L"   (세이브를 불러오기 전이라 함대 자리는 모릅니다)");
    }
    SetWindowTextW(g_status, s);
}

static void FillList(void)
{
    int i;
    ListView_DeleteAllItems(g_list);
    g_rows = 0;
    for (i = 0; i < DC_N; i++) {
        LVITEMW it;
        wchar_t buf[128];
        DcBox now, org;
        if (!InFilter(i)) continue;
        DC_Get(i, &now);
        DC_Orig(i, &org);

        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = g_rows; it.iSubItem = 0;
        wsprintfW(buf, L"%s%d", DC_Changed(i) ? L"★ " : L"", i);
        it.pszText = buf;
        it.lParam = i;
        ListView_InsertItem(g_list, &it);

        ListView_SetItemText(g_list, g_rows, 1, (LPWSTR)Disc_Name(i));
        ListView_SetItemText(g_list, g_rows, 2,
            (LPWSTR)(Disc_Cat(i) >= 0 ? HintDb_CatName(Disc_Cat(i)) : L"—"));
        ListView_SetItemText(g_list, g_rows, 3, (LPWSTR)(DC_Judged(i) ? L"받음" : L"—"));
        BoxText(&now, buf);
        ListView_SetItemText(g_list, g_rows, 4, buf);
        BoxText(&org, buf);
        ListView_SetItemText(g_list, g_rows, 5, buf);
        ListView_SetItemText(g_list, g_rows, 6, (LPWSTR)FoundText(i));

        g_map[g_rows] = i;
        g_rows++;
    }
    SetStatus();
}

// 고쳐 놓고 목록을 다시 채워도 같은 줄에 그대로 서 있게 한다.
static void ReselectPick(void)
{
    int r;
    for (r = 0; r < g_rows; r++) {
        if (g_map[r] != g_pick) continue;
        ListView_SetItemState(g_list, r, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_list, r, FALSE);
        return;
    }
}

// ------------------------------------------------------------------ 편집판

static void SetNum(int id, int v)
{
    wchar_t s[24];
    if (v == DC_NONE) s[0] = 0; else wsprintfW(s, L"%d", v);
    SetDlgItemTextW(g_win, id, s);
}

// 빈 칸은 "좌표 없음"으로 본다. 숫자가 아니면 NOVAL 을 내어 적용을 막는다.
#define BAD_NUM 0x7FFFFFFF
static int GetNum(int id)
{
    wchar_t s[24];
    const wchar_t* p = s;
    int sign = 1, v = 0, digits = 0;
    if (!GetDlgItemTextW(g_win, id, s, 24)) return DC_NONE;
    while (*p == L' ') p++;
    if (*p == L'-') { sign = -1; p++; }
    while (*p >= L'0' && *p <= L'9') { v = v * 10 + (*p - L'0'); p++; digits++; }
    while (*p == L' ') p++;
    if (!digits || *p) return digits ? BAD_NUM : DC_NONE;
    return sign * v;
}

static void ShowPick(int i)
{
    wchar_t s[512], b1[64];
    DcBox now, org;

    g_pick = i;
    if (i < 0 || !DC_Get(i, &now) || !DC_Orig(i, &org)) {
        SetWindowTextW(g_name, L"(왼쪽에서 발견물을 고르세요)");
        SetWindowTextW(g_info, L"");
        SetWindowTextW(g_orig, L"");
        SetNum(ID_X1, DC_NONE); SetNum(ID_Y1, DC_NONE);
        SetNum(ID_X2, DC_NONE); SetNum(ID_Y2, DC_NONE);
        return;
    }

    wsprintfW(s, L"[%d] %s", i, Disc_Name(i));
    SetWindowTextW(g_name, s);

    if (DC_Judged(i)) {
        lstrcpyW(s, L"좌표로 찾아지는 줄입니다.\r\n"
                    L"여기에 넣은 칸에 배가 들어가면 그 자리에서 발견됩니다.");
        if (now.x1 == DC_NONE)
            lstrcatW(s, L"\r\n지금은 좌표가 없어 이 방법으로는 못 찾습니다.");
    } else {
        wsprintfW(s, L"※ 좌표 판정을 안 받는 줄입니다(번호 %d ≥ %d).\r\n"
                     L"게임이 좌표로 훑는 것은 발견물 인스턴스가 있는\r\n"
                     L"앞 %d개뿐이라, 여기 좌표를 넣어도 안 걸립니다.",
                  i, DC_JUDGED_N, DC_JUDGED_N);
    }
    SetWindowTextW(g_info, s);

    BoxText(&org, b1);
    wsprintfW(s, L"원본  %s%s", b1, DC_Changed(i) ? L"   ★ 고쳐진 상태" : L"");
    SetWindowTextW(g_orig, s);

    SetNum(ID_X1, now.x1); SetNum(ID_Y1, now.y1);
    SetNum(ID_X2, now.x2); SetNum(ID_Y2, now.y2);
}

static void DoApply(void)
{
    DcBox b;
    if (g_pick < 0) return;
    b.x1 = GetNum(ID_X1); b.y1 = GetNum(ID_Y1);
    b.x2 = GetNum(ID_X2); b.y2 = GetNum(ID_Y2);

    if (b.x1 == BAD_NUM || b.y1 == BAD_NUM || b.x2 == BAD_NUM || b.y2 == BAD_NUM) {
        MessageBoxW(g_win, L"숫자만 넣어 주세요. 빈 칸으로 두면 좌표 없음이 됩니다.",
                    L"발견물 좌표", MB_ICONWARNING);
        return;
    }
    // 끝을 비워 두면 점 하나로 본다 — 유적처럼 한 칸짜리를 넣을 때가 대부분이다.
    if (b.x2 == DC_NONE && b.x1 != DC_NONE) b.x2 = b.x1;
    if (b.y2 == DC_NONE && b.y1 != DC_NONE) b.y2 = b.y1;
    // 하나라도 비면 넷 다 없음으로 맞춘다(반쪽짜리는 표에 들어가면 안 된다).
    if (b.x1 == DC_NONE || b.y1 == DC_NONE) { b.x1 = b.y1 = b.x2 = b.y2 = DC_NONE; }

    if (!DC_BoxOk(&b)) {
        wchar_t s[256];
        wsprintfW(s, L"넣을 수 없는 값입니다.\n\nx 는 0~%d, y 는 0~%d 이어야 하고\n"
                     L"끝 좌표가 시작 좌표보다 작으면 안 됩니다.", DC_X_MAX, DC_Y_MAX);
        MessageBoxW(g_win, s, L"발견물 좌표", MB_ICONWARNING);
        return;
    }
    if (!DC_Set(g_pick, &b)) {
        MessageBoxW(g_win, L"표에 쓰지 못했습니다.", L"발견물 좌표", MB_ICONWARNING);
        return;
    }
    FillList();
    ReselectPick();
    ShowPick(g_pick);
}

static void DoFleet(void)
{
    int x, y;
    if (!DC_FleetCell(&x, &y)) {
        MessageBoxW(g_win, L"함대 자리를 못 읽었습니다.\n세이브를 불러온 뒤에 눌러 주세요.",
                    L"발견물 좌표", MB_ICONINFORMATION);
        return;
    }
    SetNum(ID_X1, x); SetNum(ID_Y1, y);
    SetNum(ID_X2, x); SetNum(ID_Y2, y);
}

static void DoPoint(void)
{
    int x = GetNum(ID_X1), y = GetNum(ID_Y1);
    if (x == BAD_NUM || y == BAD_NUM || x == DC_NONE || y == DC_NONE) return;
    SetNum(ID_X2, x); SetNum(ID_Y2, y);
}

static void DoSave(void)
{
    wchar_t s[MAX_PATH + 160], path[MAX_PATH];
    int n = DC_Save(g_hinst);
    DC_JsonPath(g_hinst, path, MAX_PATH);
    if (n < 0) {
        wsprintfW(s, L"파일을 못 썼습니다.\n\n%s\n\n폴더에 쓰기 권한이 있는지 보세요.", path);
        MessageBoxW(g_win, s, L"발견물 좌표", MB_ICONWARNING);
        return;
    }
    wsprintfW(s, L"고친 %d줄을 적었습니다.\n\n%s\n\n다음에 게임을 켤 때 이 파일을 읽어 그대로 넣습니다.",
              n, path);
    MessageBoxW(g_win, s, L"발견물 좌표", MB_ICONINFORMATION);
}

static void DoLoadFile(void)
{
    wchar_t s[MAX_PATH + 160], path[MAX_PATH];
    int n = DC_Apply(g_hinst);
    DC_JsonPath(g_hinst, path, MAX_PATH);
    FillList();
    ReselectPick();
    ShowPick(g_pick);
    if (n > 0) wsprintfW(s, L"%d줄을 파일에서 읽어 넣었습니다.\n\n%s", n, path);
    else       wsprintfW(s, L"읽을 것이 없습니다.\n\n%s\n\n파일이 없거나 비어 있습니다.", path);
    MessageBoxW(g_win, s, L"발견물 좌표", MB_ICONINFORMATION);
}

static void DoRevertAll(void)
{
    int n = DC_ChangedCount();
    if (!n) return;
    if (MessageBoxW(g_win, L"고친 것을 모두 게임 원본 좌표로 되돌릴까요?\n"
                           L"(파일은 그대로 둡니다 — 다시 켜면 파일 값이 또 들어갑니다)",
                    L"발견물 좌표", MB_ICONQUESTION | MB_YESNO) != IDYES) return;
    DC_RevertAll();
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
static HWND Btn(HWND h, const wchar_t* s, int id)
{
    return CreateWindowExW(0, L"BUTTON", s, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           0, 0, 10, 10, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
}
static HWND Num(HWND h, int id)
{
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                           0, 0, 10, 10, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
}

static void CreateChildren(HWND h)
{
    const wchar_t* titles[7] = { L"번호", L"이름", L"분류", L"판정", L"지금 좌표", L"원본 좌표", L"발견" };
    int widths[7] = { 56, 190, 68, 52, 160, 160, 74 };
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

    g_name = Lbl(h, L"(왼쪽에서 발견물을 고르세요)", ID_NAME);
    g_info = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                0, 0, 10, 10, h, (HMENU)(UINT_PTR)ID_INFO, g_hinst, NULL);
    Lbl(h, L"시작  x", ID_LBL0 + 0); Num(h, ID_X1);
    Lbl(h, L"y",       ID_LBL0 + 1); Num(h, ID_Y1);
    Lbl(h, L"끝    x", ID_LBL0 + 2); Num(h, ID_X2);
    Lbl(h, L"y",       ID_LBL0 + 3); Num(h, ID_Y2);
    Lbl(h, L"x 0~2499 · y 0~1249 (칸 번호). 빈 칸 = 좌표 없음", ID_LBL0 + 4);
    Btn(h, L"지금 함대 자리", ID_FLEET);
    Btn(h, L"끝을 시작과 같게", ID_POINT);
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
    int half = (PANE_W - 18) / 2;

    if (listW < 200) listW = 200;
    if (listH < 100) listH = 100;

    for (i = 0; i < ID_FILTER_N; i++)
        MoveWindow(GetDlgItem(h, ID_FILTER0 + i), 8 + i * 156, 5, 152, 20, TRUE);
    MoveWindow(g_list, 6, TOP_H, listW, listH, TRUE);

    y = TOP_H;
    MoveWindow(g_name, px, y, PANE_W - 12, 20, TRUE);              y += 24;
    MoveWindow(g_info, px, y, PANE_W - 12, 52, TRUE);              y += 58;
    MoveWindow(GetDlgItem(h, ID_LBL0 + 0), px,           y + 4, 48, 18, TRUE);
    MoveWindow(GetDlgItem(h, ID_X1),       px + 50,      y,     half - 62, 22, TRUE);
    MoveWindow(GetDlgItem(h, ID_LBL0 + 1), px + half,    y + 4, 14, 18, TRUE);
    MoveWindow(GetDlgItem(h, ID_Y1),       px + half+18, y,     half - 30, 22, TRUE);
    y += ROW_H + 2;
    MoveWindow(GetDlgItem(h, ID_LBL0 + 2), px,           y + 4, 48, 18, TRUE);
    MoveWindow(GetDlgItem(h, ID_X2),       px + 50,      y,     half - 62, 22, TRUE);
    MoveWindow(GetDlgItem(h, ID_LBL0 + 3), px + half,    y + 4, 14, 18, TRUE);
    MoveWindow(GetDlgItem(h, ID_Y2),       px + half+18, y,     half - 30, 22, TRUE);
    y += ROW_H + 4;
    MoveWindow(GetDlgItem(h, ID_LBL0 + 4), px, y, PANE_W - 12, 18, TRUE);   y += 24;
    MoveWindow(GetDlgItem(h, ID_FLEET), px,            y, half + 6, 24, TRUE);
    MoveWindow(GetDlgItem(h, ID_POINT), px + half + 12, y, half + 6, 24, TRUE);
    y += 30;
    MoveWindow(GetDlgItem(h, ID_APPLY),  px,            y, half + 6, 26, TRUE);
    MoveWindow(GetDlgItem(h, ID_REVERT), px + half + 12, y, half + 6, 26, TRUE);
    y += 32;
    MoveWindow(g_orig, px, y, PANE_W - 12, 18, TRUE);

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
        switch (id) {
        case ID_APPLY:     DoApply();     return 0;
        case ID_FLEET:     DoFleet();     return 0;
        case ID_POINT:     DoPoint();     return 0;
        case ID_REVERT:
            if (g_pick >= 0) { DC_Revert(g_pick); FillList(); ReselectPick(); ShowPick(g_pick); }
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
        g_name = NULL; g_info = NULL; g_orig = NULL;
        g_pick = -1; g_rows = 0;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void DcWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { SetForegroundWindow(g_win); return; }
    if (!DC_Load()) {
        MessageBoxW(owner, L"발견물 좌표표를 못 읽었습니다.\n한국어판 Ver.1.2.0.0 이 아닌 것 같습니다.",
                    L"발견물 좌표", MB_ICONWARNING);
        return;
    }
    Disc_Load();      // 이름 · 분류 · 발견 여부. 실패해도 좌표는 고칠 수 있다
    HintDb_Load();

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
        wc.lpszClassName = L"DiscoveryEditKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"DiscoveryEditKRWin", L"발견물 좌표 — DiscoveryEditKR",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1140, 660,
                owner, NULL, g_hinst, NULL);
    if (g_win) { ShowWindow(g_win, SW_SHOW); UpdateWindow(g_win); }
}

// ================================================================== 메뉴 설치 + 서브클래싱
// BookUtilKR 과 같은 방식이다 — 1초 폴링으로 게임 창을 찾아 "파일 > 모드" 아래에 달고,
// 서브클래싱해서 WM_COMMAND 를 ID 로 가로챈다.

static HWND    g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static int     g_pass = 0;

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_DC_OPEN) { DcWin_Show(h); return 0; }
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
    OutputDebugStringW(L"[DiscoveryEditKR] menu monitor started.");

    // 표는 .rdata 라 프로세스가 뜬 그 순간부터 있다 — 세이브를 기다릴 것 없이 바로 넣는다.
    {
        int n = DC_Apply(g_hinst);
        if (n > 0) {
            wchar_t s[96];
            wsprintfW(s, L"[DiscoveryEditKR] disc_coords.json 에서 %d줄 적용.", n);
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
            if (!(MenuHasId(target, ID_DC_OPEN) || ModMenu_HasId(g_gameHwnd, ID_DC_OPEN))) {
                HMENU modMenu;
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                if (!modMenu) { Sleep(1000); continue; }
                AppendMenuW(modMenu, MF_STRING, ID_DC_OPEN, L"발견물 좌표");
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[DiscoveryEditKR] 발견물 좌표 menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[DiscoveryEditKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void DiscEditKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
