#include <windows.h>
#include <windowsx.h>
#include "hint.h"
#include "hintdb.h"
#include "disc.h"
#include "uikit.h"     // CharacterUtilKR/src — 세피아 색표와 위젯을 그대로 나눠 쓴다

// HintUtilKR — 게임의 "취득 힌트 일람"은 이름만 여덟 줄 늘어놓고 만다. 여기서는
// [힌트] 186개와 [발견물] 274개를 분류 · 가치 · 상태와 함께 보여 주고 분류로 추린다.
//
// 힌트와 발견물은 서로 다른 목록이고 번호도 다르다 — 힌트 31 은 "트로이" 지만
// 발견물 31 은 "아부심벨 대신전"이다(hintdb.h · disc.h 참고).
// 힌트 상태는 실행 중 메모리에서, 발견 여부는 SAVEDATA.CDS 에서 읽는다. 아무것도 쓰지 않는다.

#define ID_HINT_OPEN 0xBC00u   // Trade=0xB10x, Char=0xB301/0xB310+, Ship=0xB410, Patch=0xB500,
                               // Map=0xB600, Mod=0xB700, QMod=0xB800, Upd=0xB900,
                               // Fatigue=0xBA00, Hotkey=0xBB00 과 안 겹치게.

#define WC_HINT    L"HintUtilKR_Window"
#define CLIENT_W   600
#define ROW_H      22
#define ROWS_VIS   18
#define TAB_Y      (FRAME + TITLE_H + 4)
#define TAB_H      24
#define FILT_Y     (TAB_Y + TAB_H + 4)
#define FILT_H     24
#define LIST_Y     (FILT_Y + FILT_H + 6)
#define LIST_H     (ROW_H * ROWS_VIS)
#define CLIENT_H   (LIST_Y + LIST_H + 34)
#define SBW        12

#define MODE_HINT 0
#define MODE_DISC 1

// 상태 추리기. 뜻은 보는 것에 따라 다르다 — 힌트면 [힌트만]/[발견 완료],
// 발견물이면 [발견]/[아직].
#define F_ALL  0
#define F_A    1
#define F_B    2
#define FILT_N 3
static const wchar_t* kFiltHint[FILT_N] = { L"전체", L"힌트만", L"발견 완료" };
static const wchar_t* kFiltDisc[FILT_N] = { L"전체", L"발견",   L"아직" };

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

static int g_mode = MODE_HINT;
static int g_cat = -1;          // -1 = 분류 전체
static int g_filt = F_A;        // 힌트는 [힌트만] 으로 연다 — 이 창을 여는 목적이 그거라서다
static int g_scroll = 0;
static int g_view[DISC_N];      // 걸러 낸 번호(힌트 186 보다 발견물 274 가 크다)
static int g_viewN = 0;

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

// ---- 지금 보는 것에 따라 갈리는 자리 ----
static int Count(void)            { return g_mode == MODE_DISC ? Disc_Count() : HINT_N; }
static const wchar_t* Name(int i) { return g_mode == MODE_DISC ? Disc_Name(i) : HintDb_Name(i); }
static int Cat(int i)             { return g_mode == MODE_DISC ? Disc_Cat(i) : HintDb_Cat(i); }
static int Value(int i)           { return g_mode == MODE_DISC ? Disc_Value(i) : HintDb_Value(i); }

static RECT RcMode(int i)
{
    RECT r;
    r.left = FRAME + 8 + i * 62; r.right = r.left + 58;
    r.top = FILT_Y; r.bottom = FILT_Y + FILT_H - 2;
    return r;
}
static RECT RcTab(int i)        // i = 0 이면 [전체], 그 밖은 분류 i-1
{
    RECT r;
    int w = (CLIENT_W - 2*FRAME - 16 - 8 * 2) / (HINT_CAT_N + 1);
    r.left = FRAME + 8 + i * (w + 2); r.right = r.left + w;
    r.top = TAB_Y; r.bottom = TAB_Y + TAB_H - 2;
    return r;
}
static RECT RcFilt(int i)
{
    RECT r;
    r.left = FRAME + 8 + 132 + i * 84; r.right = r.left + 80;
    r.top = FILT_Y; r.bottom = FILT_Y + FILT_H - 2;
    return r;
}
static RECT RcList(void)
{ RECT r; r.left=FRAME+8; r.right=CLIENT_W-FRAME-8-SBW; r.top=LIST_Y; r.bottom=LIST_Y+LIST_H; return r; }
static RECT RcTrack(void)
{ RECT r; r.right=CLIENT_W-FRAME-8; r.left=r.right-SBW; r.top=LIST_Y; r.bottom=LIST_Y+LIST_H; return r; }
static RECT RcRow(int v)
{ RECT r = RcList(); r.top = LIST_Y + v*ROW_H; r.bottom = r.top + ROW_H; return r; }

static int MaxScroll(void) { int m = g_viewN - ROWS_VIS; return m > 0 ? m : 0; }

static int Keep(int i)
{
    if (g_filt == F_ALL) return 1;
    if (g_mode == MODE_DISC) {
        int f = Disc_Found(i);
        if (f == DISC_UNKNOWN) return 1;                 // 세이브가 없으면 추리지 않는다
        return (g_filt == F_A) ? (f != DISC_NOT) : (f == DISC_NOT);
    }
    {   int st = HintDb_State(i);
        if (st < 0) return 1;                            // 상태를 못 읽으면 추리지 않는다
        return (g_filt == F_A) ? (st == HINT_GOT) : (st == HINT_DONE);
    }
}

static void Rebuild(void)
{
    int i, n = Count();
    g_viewN = 0;
    for (i = 0; i < n; i++) {
        if (g_cat >= 0 && Cat(i) != g_cat) continue;
        if (!Keep(i)) continue;
        g_view[g_viewN++] = i;
    }
    if (g_scroll > MaxScroll()) g_scroll = MaxScroll();
}

// 줄 오른쪽에 적을 상태 한 마디. 눈에 띄게 할지(done) 흐리게 할지(dim)도 함께 정한다.
static const wchar_t* StateText(int i, int* done, int* dim)
{
    *done = 0; *dim = 0;
    if (g_mode == MODE_DISC) {
        switch (Disc_Found(i)) {
        case DISC_REPORTED: *done = 1; return L"발견 · 발표";
        case DISC_FOUND:    *done = 1; return L"발견";
        case DISC_NOT:      *dim = 1;  return L"—";
        default:                       return L"세이브 없음";
        }
    }
    switch (HintDb_State(i)) {
    case HINT_DONE: *done = 1; return L"발견 완료";
    case HINT_GOT:             return L"힌트 있음";
    case HINT_NONE: *dim = 1;  return L"—";
    case -1:                   return L"?";
    default:                   return L"·";      // 8/13/15 말고 9·11 도 나온다(뜻은 아직 모른다)
    }
}

static void Paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    UiBuf b;
    HDC dc;
    RECT rc, cb, r;
    HBRUSH br;
    int i;
    wchar_t buf[96];

    GetClientRect(h, &rc);
    dc = UI_BufBegin(&b, hdc, rc.right, rc.bottom);
    br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
    UI_WindowFrame(dc, rc, g_mode == MODE_DISC ? L"발견물 274개" : L"힌트 186개", &cb);

    for (i = 0; i <= HINT_CAT_N; i++)
        UI_Button(dc, RcTab(i), i == 0 ? L"전체" : HintDb_CatName(i - 1),
                  g_cat == (i == 0 ? -1 : i - 1));
    UI_Button(dc, RcMode(0), L"힌트",   g_mode == MODE_HINT);
    UI_Button(dc, RcMode(1), L"발견물", g_mode == MODE_DISC);
    for (i = 0; i < FILT_N; i++)
        UI_Button(dc, RcFilt(i), (g_mode == MODE_DISC ? kFiltDisc : kFiltHint)[i], g_filt == i);

    if (g_mode == MODE_DISC)
        wsprintfW(buf, Disc_HaveSave() ? L"%d개 · 세이브 기준"
                                       : L"%d개 — SAVEDATA.CDS 를 못 읽었습니다", g_viewN);
    else
        wsprintfW(buf, HintDb_Live() ? L"%d개" : L"%d개 — 세이브를 불러오면 상태가 나옵니다", g_viewN);
    r.left = FRAME + 8 + 132 + FILT_N*84; r.right = CLIENT_W - FRAME - 10;
    r.top = FILT_Y; r.bottom = FILT_Y + FILT_H - 2;
    UI_Text(dc, r, buf, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);

    // 목록
    r = RcList();
    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &r, br); DeleteObject(br);
    for (i = 0; i < ROWS_VIS; i++) {
        int k = g_scroll + i, id, done, dim;
        const wchar_t* st;
        RECT row, t;
        COLORREF fg;
        if (k >= g_viewN) break;
        id = g_view[k];
        st = StateText(id, &done, &dim);
        row = RcRow(i);
        if (i & 1) { br = CreateSolidBrush(COL_ROW_ALT); FillRect(dc, &row, br); DeleteObject(br); }
        fg = dim ? COL_DARK : COL_TEXT;

        t = row; t.left += 8; t.right = t.left + 36;
        wsprintfW(buf, L"%d", id);
        UI_Text(dc, t, buf, g_smallFont, COL_DARK, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        t.left = row.left + 48; t.right = t.left + 210;
        UI_Text(dc, t, Name(id), g_font, fg,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

        t.left = row.left + 262; t.right = t.left + 70;
        UI_Text(dc, t, HintDb_CatName(Cat(id)), g_font, COL_LANG_TX,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        t.left = row.left + 336; t.right = t.left + 90;
        { int v = Value(id);
          if (v > 0) wsprintfW(buf, L"%d", v); else lstrcpyW(buf, L"-"); }
        UI_Text(dc, t, buf, g_smallFont, fg, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        t.left = row.left + 440; t.right = row.right - 6;
        UI_Text(dc, t, st, g_smallFont, done ? COL_WARN_TX : fg,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    r = RcList();
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &r, br); DeleteObject(br);
    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), ROWS_VIS, g_viewN);

    r.left = FRAME + 10; r.right = CLIENT_W - FRAME - 10;
    r.top = LIST_Y + LIST_H + 6; r.bottom = r.top + 22;
    UI_Text(dc, r,
            g_mode == MODE_DISC
              ? L"발견 여부는 SAVEDATA.CDS 를 읽습니다 — 저장한 뒤 F5 를 누르면 갱신됩니다."
              : L"힌트 상태는 실행 중인 게임에서 바로 읽습니다. 값은 고치지 않습니다.",
            g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    UI_BufEnd(&b);
    EndPaint(h, &ps);
}

static void ScrollTo(HWND h, int v)
{
    int mx = MaxScroll();
    if (v < 0) v = 0;
    if (v > mx) v = mx;
    if (v != g_scroll) { g_scroll = v; InvalidateRect(h, NULL, FALSE); }
}

static void Reload(HWND h)
{
    HintDb_Load();
    Disc_Load();          // 세이브를 다시 읽는다
    Rebuild();
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK HintProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        HintDb_Load();
        Disc_Load();
        Rebuild();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(h); return 0;
    case WM_MOUSEWHEEL:
        ScrollTo(h, g_scroll - (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 3 : -3));
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb, r;
        int i;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        GetClientRect(h, &rc);
        cb.right = rc.right - FRAME - 4; cb.left = cb.right - 22;
        cb.top = FRAME + 4; cb.bottom = cb.top + 18;
        if (PtInRect(&cb, pt)) { ShowWindow(h, SW_HIDE); return 0; }
        for (i = 0; i < 2; i++) {
            r = RcMode(i);
            if (!PtInRect(&r, pt)) continue;
            if (g_mode != i) {
                g_mode = i;
                g_filt = (i == MODE_HINT) ? F_A : F_ALL;   // 힌트는 [힌트만], 발견물은 [전체]
                g_scroll = 0; Rebuild(); InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        for (i = 0; i <= HINT_CAT_N; i++) {
            r = RcTab(i);
            if (!PtInRect(&r, pt)) continue;
            g_cat = (i == 0) ? -1 : i - 1;
            g_scroll = 0; Rebuild(); InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        for (i = 0; i < FILT_N; i++) {
            r = RcFilt(i);
            if (!PtInRect(&r, pt)) continue;
            g_filt = i;
            g_scroll = 0; Rebuild(); InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        r = RcTrack();
        if (PtInRect(&r, pt)) {
            int mid = (r.top + r.bottom) / 2;
            ScrollTo(h, g_scroll + (pt.y < mid ? -ROWS_VIS : ROWS_VIS));
            return 0;
        }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        switch (w) {
        case VK_UP:     ScrollTo(h, g_scroll - 1); return 0;
        case VK_DOWN:   ScrollTo(h, g_scroll + 1); return 0;
        case VK_PRIOR:  ScrollTo(h, g_scroll - ROWS_VIS); return 0;
        case VK_NEXT:   ScrollTo(h, g_scroll + ROWS_VIS); return 0;
        case VK_HOME:   ScrollTo(h, 0); return 0;
        case VK_END:    ScrollTo(h, MaxScroll()); return 0;
        case VK_TAB:    g_mode = !g_mode; g_filt = (g_mode == MODE_HINT) ? F_A : F_ALL; g_scroll = 0;
                        Rebuild(); InvalidateRect(h, NULL, FALSE); return 0;
        case VK_F5:     Reload(h); return 0;
        case VK_ESCAPE: ShowWindow(h, SW_HIDE); return 0;
        }
        return 0;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY:
        UI_DestroyFonts();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowHintWindow(void)
{
    static BOOL reg = FALSE;
    RECT orc;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;

    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = HintProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.lpszClassName = WC_HINT;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        // 게임이 전체화면이라 게임 창을 주인으로 걸어야 위에 뜬다.
        if (g_gameHwnd && GetWindowRect(g_gameHwnd, &orc)) {
            x = orc.left + ((orc.right - orc.left) - CLIENT_W) / 2;
            y = orc.top  + ((orc.bottom - orc.top) - CLIENT_H) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
        }
        g_wnd = CreateWindowExW(0, WC_HINT, L"힌트", WS_POPUP,
                    x, y, CLIENT_W, CLIENT_H, g_gameHwnd, NULL, g_hinst, NULL);
    } else {
        Reload(g_wnd);      // 그 사이 저장했거나 세이브를 불러왔을 수 있다
    }
    if (g_wnd) {
        ShowWindow(g_wnd, SW_SHOW);
        UpdateWindow(g_wnd);
        SetForegroundWindow(g_wnd);
        SetFocus(g_wnd);
    }
}

// ------------------------------------------------------------------ 메뉴 붙이기

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_HINT_OPEN) {
        ShowHintWindow();
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
    LogW(L"[HintUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_HINT_OPEN)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(target, MF_STRING, ID_HINT_OPEN, L"힌트");
                DrawMenuBar(g_gameHwnd);
                LogW(L"[HintUtilKR] \"힌트\" 메뉴 설치.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
            }
        }
        Sleep(1000);
    }
}

void HintKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    LogW(L"[HintUtilKR] init.");
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
