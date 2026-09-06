#include <windows.h>
#include <stdarg.h>
#include "sparwin.h"
#include "sparring.h"
#include "gameskin.h"   // 창을 게임 껍데기로 입힌다

// LandWarKR — 육상전 모의전.
//
//   왼쪽   상대 목록 — [도시 방어부대] 이면 도시 226, [필드 부대] 이면 열여섯 벌
//   오른쪽 싸움터 넷, 되돌리기, [모의전 시작]
//
// 콤보 상자는 쓰지 않는다 — 게임 DirectDraw 화면 위에서 목록을 펼치면 게임이 죽는다
// (TradeUtilKR/src/trade.c 머리말).
//
// 「시작」을 누르면 창을 감추고 **제 창에 메시지를 부친다**(WM_SPAR_RUN). 판은 그 메시지를
// 받는 자리에서 벌어진다 — 단추를 누른 그 자리에서 바로 전투 화면을 열면 우리 창이 그 위에
// 남고, 되돌아올 자리도 우리 스택 한복판이 된다.
//
// 이 창은 게임 스레드가 지었으므로 창 메시지도 게임의 메시지 고리가 돌린다. 곧 여기가
// 게임 창의 WM_COMMAND 와 똑같은 깊이, 고리의 맨 위다. **게임 창에는 손대지 않는다** —
// 게임 창 프로시저는 초당 수백 번 지나는 자리라 거기에 줄을 끼우지 않는 편이 안전하다.

#define ID_SIDE_CITY  1101
#define ID_SIDE_FIELD 1102
#define ID_FILTER     1103
#define ID_LIST       1104
#define ID_TERR0      1110      // 도시 · 초지 · 숲 · 황무지
#define ID_MYMEN      1118      // 내 병력 칸
#define ID_MYREAL     1119      // [지금 함대 그대로]
#define ID_RESTORE    1120
#define ID_START      1121
#define ID_INFO       1130
#define ID_INFO2      1131
#define ID_STATUS     1132

// 우리 창 클래스 안에서만 쓰는 값이라 RegisterWindowMessage 가 필요 없다.
#define WM_SPAR_RUN   (WM_APP + 1)

#define TITLE_H   28
#define LIST_X    12
#define LIST_Y   102
#define LIST_W   320
#define LIST_H   250
#define RIGHT_X  (LIST_X + LIST_W + 16)
#define CLIENT_W (RIGHT_X + 300)

static HINSTANCE g_hinst = NULL;
static HWND  g_win = NULL, g_list = NULL, g_filter = NULL;
static HWND  g_info = NULL, g_info2 = NULL, g_status = NULL;
static HWND  g_myMen = NULL, g_fleetNote = NULL;
static HFONT g_font = NULL;

// 목록 줄 -> 도시 번호(또는 필드 벌 번호). 걸러 보면 어긋나므로 짝을 들고 다닌다.
static int g_map[SP_CITY_N];
static int g_mapN = 0;

// 「시작」이 게임 스레드에 부치는 부탁.
static int g_reqField = 0, g_reqPick = -1, g_reqTerr = 0, g_reqRestore = 1, g_reqMen = 0;

static void SetStatus(const wchar_t* s) { if (g_status) SetWindowTextW(g_status, s); }


static int FieldSide(void)
{
    return g_win && SendMessageW(GetDlgItem(g_win, ID_SIDE_FIELD), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static int TerrainPick(void)
{
    int i;
    if (!g_win) return 0;
    for (i = 0; i < SP_TERRAIN_N; i++)
        if (SendMessageW(GetDlgItem(g_win, ID_TERR0 + i), BM_GETCHECK, 0, 0) == BST_CHECKED) return i;
    return 0;
}

// 숫자 칸을 읽는다. CRT 없이 손으로 센다(앞뒤 빈칸은 넘긴다).
static int TextToInt(const wchar_t* t)
{
    int n = 0, any = 0;
    while (*t == L' ' || *t == L'	') t++;
    while (*t >= L'0' && *t <= L'9') {
        if (n > SP_MEN_MAX) return SP_MEN_MAX;      // 더 볼 것 없다
        n = n * 10 + (*t - L'0');
        t++; any = 1;
    }
    return any ? n : -1;
}

// 내가 끌고 나갈 사람 수. 0 이면 「지금 함대 그대로」다.
static int MyMenPick(void)
{
    wchar_t t[16];
    int n;
    if (!g_win) return 0;
    if (SendMessageW(GetDlgItem(g_win, ID_MYREAL), BM_GETCHECK, 0, 0) == BST_CHECKED) return 0;
    if (!g_myMen) return 0;
    GetWindowTextW(g_myMen, t, 16);
    n = TextToInt(t);
    if (n < 0) return SP_MEN_DEFAULT;               // 비었거나 숫자가 아니면 기본값
    if (n < SP_MEN_MIN) n = SP_MEN_MIN;
    if (n > SP_MEN_MAX) n = SP_MEN_MAX;
    return n;
}

// 「지금 함대 그대로」를 켜면 숫자 칸은 잠근다.
static void SyncMyMen(void)
{
    int real = SendMessageW(GetDlgItem(g_win, ID_MYREAL), BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t s[96];
    int n = Spar_FleetMen();
    if (g_myMen) EnableWindow(g_myMen, !real);
    if (g_fleetNote) {
        if (n > 0) wsprintfW(s, L"(지금 함대로 나가면 %d명)", n);
        else lstrcpyW(s, L"(지금 함대의 사람 수는 아직 못 읽습니다)");
        SetWindowTextW(g_fleetNote, s);
    }
}

static int RestorePick(void)
{
    return g_win && SendMessageW(GetDlgItem(g_win, ID_RESTORE), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// 걸러 보기 칸에 적은 글이 이름 안에 들어 있나.
static int Match(const wchar_t* name, const wchar_t* want)
{
    int i, j;
    if (!want[0]) return 1;
    for (i = 0; name[i]; i++) {
        for (j = 0; want[j] && name[i + j] == want[j]; j++) ;
        if (!want[j]) return 1;
    }
    return 0;
}

static void FillList(void)
{
    wchar_t want[64], line[192], nm[64];
    int i, field = FieldSide();

    if (!g_list) return;
    want[0] = 0;
    if (g_filter) GetWindowTextW(g_filter, want, 64);

    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_mapN = 0;

    if (field) {
        for (i = 0; i < SP_FIELD_N; i++) {
            int r = Spar_FieldRegion(i), c = Spar_FieldCulture(i);
            if (!Spar_FieldLeaderName(i, nm, 64)) lstrcpyW(nm, L"(이름 못 읽음)");
            if (!Match(Spar_RegionName(r), want) && !Match(nm, want)) continue;
            wsprintfW(line, L"%s %d  %s  %d~%d명%s%s",
                      Spar_RegionName(r), (i % 2) + 1, nm,
                      Spar_FieldMenLo(i), Spar_FieldMenHi(i),
                      (c >= 0) ? L"  · " : L"", (c >= 0) ? Spar_CultureName(c) : L"");
            SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)line);
            g_map[g_mapN++] = i;
        }
    } else {
        for (i = 0; i < SP_CITY_N; i++) {
            const wchar_t* name = Spar_CityName(i);
            int c = Spar_CityCulture(i), s = Spar_CityScale(i);
            if (!Match(name, want)) continue;
            if (s >= 0)
                wsprintfW(line, L"%s  규모 %d  %d~%d명  · %s",
                          name, s, Spar_CityMenLo(i), Spar_CityMenHi(i),
                          (c >= 0) ? Spar_CultureName(c) : L"?");
            else
                wsprintfW(line, L"%s  (세이브를 불러오면 규모가 보입니다)", name);
            SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)line);
            g_map[g_mapN++] = i;
        }
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
    if (g_mapN > 0) SendMessageW(g_list, LB_SETCURSEL, 0, 0);
}

static int Picked(void)
{
    int row = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    if (row < 0 || row >= g_mapN) return -1;
    return g_map[row];
}

static void ShowInfo(void)
{
    wchar_t a[256], b[256], nm[64];
    int pick = Picked(), c;

    a[0] = 0; b[0] = 0;
    if (pick >= 0) {
        if (FieldSide()) {
            c = Spar_FieldCulture(pick);
            if (!Spar_FieldLeaderName(pick, nm, 64)) lstrcpyW(nm, L"(이름 못 읽음)");
            wsprintfW(a, L"%s — 대장 %s (인물 %d), 병력 %d~%d명",
                      Spar_RegionName(Spar_FieldRegion(pick)), nm,
                      Spar_FieldLeader(pick), Spar_FieldMenLo(pick), Spar_FieldMenHi(pick));
            if (c >= 0) wsprintfW(b, L"진형은 대장 나라의 문화권(%s)이 정합니다 — %s",
                                  Spar_CultureName(c), Spar_ShapeText(c));
            else lstrcpyW(b, L"세이브를 불러오면 대장과 진형이 보입니다.");
        } else {
            c = Spar_CityCulture(pick);
            wsprintfW(a, L"%s — 규모 %d, 병력 %d~%d명 (대장 능력도 규모가 정합니다)",
                      Spar_CityName(pick), Spar_CityScale(pick),
                      Spar_CityMenLo(pick), Spar_CityMenHi(pick));
            if (c >= 0) wsprintfW(b, L"문화권 %s — %s", Spar_CultureName(c), Spar_ShapeText(c));
            else lstrcpyW(b, L"세이브를 불러오면 규모와 진형이 보입니다.");
        }
    }
    if (g_info)  SetWindowTextW(g_info, a);
    if (g_info2) SetWindowTextW(g_info2, b);
}

static void Start(void)
{
    wchar_t why[192];
    int pick = Picked();

    if (pick < 0) { SetStatus(L"상대를 하나 고르세요."); return; }
    if (!Spar_CanRun(why, 192)) { SetStatus(why); return; }

    g_reqField   = FieldSide();
    g_reqPick    = pick;
    g_reqTerr    = TerrainPick();
    g_reqRestore = RestorePick();
    g_reqMen     = MyMenPick();

    SetStatus(L"판을 벌입니다…");
    ShowWindow(g_win, SW_HIDE);
    PostMessageW(g_win, WM_SPAR_RUN, 0, 0);   // 메시지 고리가 제 차례에 벌인다
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
    int i, y;

    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -12; lf.lfCharSet = HANGEUL_CHARSET;
    lstrcpyW(lf.lfFaceName, L"굴림체");
    g_font = CreateFontIndirectW(&lf);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    Mk(L"STATIC", L"게임의 육상전을 그대로 한 판 벌입니다. 도시 밖(항해·상륙 중)에서만 됩니다.",
       SS_LEFTNOWORDWRAP, 0, LIST_X, 6, CLIENT_W - 24, 18, 0, h);

    Mk(L"STATIC", L"상대", 0, 0, LIST_X, 32, 34, 18, 0, h);
    Mk(L"BUTTON", L"도시 방어부대", BS_AUTORADIOBUTTON | WS_GROUP, 0,
       LIST_X + 40, 30, 120, 20, ID_SIDE_CITY, h);
    Mk(L"BUTTON", L"필드 부대", BS_AUTORADIOBUTTON, 0,
       LIST_X + 166, 30, 100, 20, ID_SIDE_FIELD, h);
    SendMessageW(GetDlgItem(h, ID_SIDE_CITY), BM_SETCHECK, BST_CHECKED, 0);

    Mk(L"STATIC", L"찾기", 0, 0, LIST_X, 60, 34, 18, 0, h);
    g_filter = Mk(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                  LIST_X + 40, 56, 180, 22, ID_FILTER, h);
    Mk(L"STATIC", L"(이름 한 조각)", 0, 0, LIST_X + 228, 60, 104, 18, 0, h);

    g_list = Mk(L"LISTBOX", L"", WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                WS_EX_CLIENTEDGE, LIST_X, LIST_Y, LIST_W, LIST_H, ID_LIST, h);

    Mk(L"STATIC", L"싸움터", 0, 0, RIGHT_X, 32, 60, 18, 0, h);
    y = 54;
    for (i = 0; i < SP_TERRAIN_N; i++) {
        Mk(L"BUTTON", Spar_TerrainName(i), BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0), 0,
           RIGHT_X, y, 110, 20, ID_TERR0 + i, h);
        y += 24;
    }
    SendMessageW(GetDlgItem(h, ID_TERR0), BM_SETCHECK, BST_CHECKED, 0);

    y += 8;
    Mk(L"STATIC", L"내 병력", 0, 0, RIGHT_X, y + 4, 56, 18, 0, h);
    g_myMen = Mk(L"EDIT", L"300", ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
                 RIGHT_X + 58, y, 70, 22, ID_MYMEN, h);
    Mk(L"STATIC", L"명 (제독 포함)", 0, 0, RIGHT_X + 134, y + 4, 120, 18, 0, h);

    y += 26;
    Mk(L"BUTTON", L"지금 함대 그대로", BS_AUTOCHECKBOX | WS_GROUP, 0,
       RIGHT_X, y, 160, 20, ID_MYREAL, h);
    g_fleetNote = Mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0, RIGHT_X + 16, y + 22, 250, 18, 0, h);

    y += 46;
    Mk(L"BUTTON", L"싸움 뒤 되돌린다", BS_AUTOCHECKBOX | WS_GROUP, 0,
       RIGHT_X, y, 150, 20, ID_RESTORE, h);
    SendMessageW(GetDlgItem(h, ID_RESTORE), BM_SETCHECK, BST_CHECKED, 0);
    Mk(L"STATIC", L"소지금·명성·악명과 주인공·부관", 0, 0, RIGHT_X + 16, y + 22, 260, 18, 0, h);
    Mk(L"STATIC", L"능력을 제자리로. 선원은 육상전이", 0, 0, RIGHT_X + 16, y + 40, 260, 18, 0, h);
    Mk(L"STATIC", L"원래 안 건드립니다.", 0, 0, RIGHT_X + 16, y + 58, 260, 18, 0, h);

    Mk(L"BUTTON", L"모의전 시작", WS_GROUP, 0, RIGHT_X, LIST_Y + LIST_H - 32, 130, 30, ID_START, h);

    g_info  = Mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0,
                 LIST_X, LIST_Y + LIST_H + 8, CLIENT_W - 24, 18, ID_INFO, h);
    g_info2 = Mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0,
                 LIST_X, LIST_Y + LIST_H + 28, CLIENT_W - 24, 18, ID_INFO2, h);
    g_status = Mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0,
                  LIST_X, LIST_Y + LIST_H + 52, CLIENT_W - 24, 18, ID_STATUS, h);
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
    case WM_CREATE: {
        wchar_t why[192];
        // CreateWindowExW 가 아직 안 돌아왔으니 g_win 은 비어 있다. 여기서 채워 두어야
        // 아래 도우미들(FieldSide·SyncMyMen …)이 GetDlgItem(NULL) 을 쥐지 않는다.
        g_win = h;
        MakeControls(h);
        GameSkin_Apply(h);
        Guarded(FillList, L"FillList");
        Guarded(ShowInfo, L"ShowInfo");
        Guarded(SyncMyMen, L"SyncMyMen");
        SetStatus(Spar_CanRun(why, 192) ? L"상대와 싸움터를 고르고 [모의전 시작]." : why);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (code == LBN_SELCHANGE && id == ID_LIST) { Guarded(ShowInfo, L"ShowInfo"); return 0; }
        if (code == EN_CHANGE && id == ID_FILTER) {
            Guarded(FillList, L"FillList"); Guarded(ShowInfo, L"ShowInfo"); return 0;
        }
        if (code == BN_CLICKED) {
            if (id == ID_SIDE_CITY || id == ID_SIDE_FIELD) {
                SetWindowTextW(g_filter, L"");
                Guarded(FillList, L"FillList"); Guarded(ShowInfo, L"ShowInfo");
                InvalidateRect(h, NULL, FALSE);
                return 0;
            }
            if (id == ID_MYREAL) { Guarded(SyncMyMen, L"SyncMyMen"); return 0; }
            if (id == ID_START) { Guarded(Start, L"Start"); return 0; }
        }
        break;
    }

    // 판은 여기서 벌어진다 — 메시지 고리의 맨 위이고, 단추를 누른 스택은 이미 풀렸다.
    case WM_SPAR_RUN: {
        static const wchar_t* kEnd[3] = { L"이겼습니다", L"물러났습니다", L"몰살당했습니다" };
        wchar_t msg[256];
        int r;
        if (g_reqPick < 0) return 0;
        r = g_reqField ? Spar_RunField(g_reqPick, g_reqTerr, g_reqRestore, g_reqMen)
                       : Spar_RunCity(g_reqPick, g_reqTerr, g_reqRestore, g_reqMen);
        g_reqPick = -1;
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
        Guarded(SyncMyMen, L"SyncMyMen");
        if (r >= 0 && r <= 2)
            wsprintfW(msg, L"모의전 끝 — %s.%s", kEnd[r],
                      g_reqRestore ? L" 소지금·명성·능력은 제자리로 돌려놓았습니다." : L"");
        else
            lstrcpyW(msg, L"판을 못 벌였습니다(도시 밖인지, 세이브를 불러왔는지 보세요).");
        SetStatus(msg);
        return 0;
    }

    case WM_DRAWITEM:
        if (GameSkin_DrawItem((const DRAWITEMSTRUCT*)l)) return TRUE;
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT t;
        GetClientRect(h, &t); t.left = 8; t.right -= 8; t.top = 3; t.bottom = TITLE_H - 1;
        GameSkin_Title(dc, t, L"육상전 모의전");
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_DESTROY:
        g_win = NULL; g_list = NULL; g_filter = NULL;
        g_info = NULL; g_info2 = NULL; g_status = NULL;
        g_myMen = NULL; g_fleetNote = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// DllMain(로더 락) 안에서 불린다. 여기서는 hinst 만 챙긴다 —
// RegisterWindowMessageW 같은 user32 호출은 로더 락 안에서 하면 안 된다.
void SparWin_Init(HINSTANCE hinst)
{
    g_hinst = hinst;
}


void SparWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { ShowWindow(g_win, SW_SHOW); SetForegroundWindow(g_win); SyncMyMen(); return; }
    if (!Spar_Load()) {
        MessageBoxW(owner, L"육상전 표를 못 읽었습니다.\n한국어판 Ver.1.2.0.0 이 아닌 것 같습니다.",
                    L"육상전 모의전", MB_ICONWARNING);
        return;
    }
    if (!registered) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = WinProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"LandWarKRSparWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"LandWarKRSparWin", L"육상전 모의전 — LandWarKR",
                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT,
                CLIENT_W + 16, LIST_Y + LIST_H + 110 + TITLE_H, owner, NULL, g_hinst, NULL);
    if (g_win) { ShowWindow(g_win, SW_SHOW); UpdateWindow(g_win); }
}

