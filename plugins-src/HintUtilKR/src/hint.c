#include <windows.h>
#include <windowsx.h>
#include "hint.h"
#include "hintdb.h"
#include "disc.h"
#include "discinst.h"
#include "patronpick.h"
#include "chardb.h"   // CharacterUtilKR/src — 후원자 이름·도시·직업·자금
#include "patrons.h"  // CharacterUtilKR/src — 실행 중 친밀도
#include "faces.h"    // CharacterUtilKR/src — 초상화(MALE/FEMALE.CDS)
#include "uikit.h"     // CharacterUtilKR/src — 세피아 색표와 위젯을 그대로 나눠 쓴다

// HintUtilKR — 게임의 "취득 힌트 일람"은 이름만 여덟 줄 늘어놓고 만다. 여기서는
// [힌트] 186개와 [발견물] 274개를 분류 · 가치 · 상태와 함께 보여 주고 분류로 추린다.
//
// 힌트와 발견물은 서로 다른 목록이고 번호도 다르다 — 힌트 31 은 "트로이" 지만
// 발견물 31 은 "아부심벨 대신전"이다(hintdb.h · disc.h 참고).
// 힌트 상태도 발견 여부도 실행 중 메모리에서 읽는다. 아무것도 쓰지 않는다.
//
// ★ 발견과 보고는 다른 일이다 — 힌트 배열의 "발견" 비트는 후원자에게 보고해야 켜진다.
//   발견만 하고 아직 보고를 안 한 것은 발견물 인스턴스(discinst.h)의 사람 칸에만 남는다.
//   그래서 이 창은 인스턴스를 먼저 보고, 그것이 없는 줄만 힌트 배열로 가른다.

#define ID_HINT_OPEN 0xBC00u   // Trade=0xB10x, Char=0xB301/0xB310+, Ship=0xB410, Patch=0xB500,
                               // Map=0xB600, Mod=0xB700, QMod=0xB800, Upd=0xB900,
                               // Fatigue=0xBA00, Hotkey=0xBB00 과 안 겹치게.

#define WC_HINT    L"HintUtilKR_Window"
#define ROW_H      22
#define ROWS_VIS   18
#define TAB_Y      (FRAME + TITLE_H + 4)
#define TAB_H      24
#define FILT_Y     (TAB_Y + TAB_H + 4)
#define FILT_H     24
#define LIST_X     (FRAME + 8)
#define LIST_W     566
#define LIST_Y     (FILT_Y + FILT_H + 6)
#define LIST_H     (ROW_H * ROWS_VIS)
#define SBW        12
// 오른쪽 판 — 고른 줄의 분류를 좋아하는 후원자. 도시를 누르면 그리로 워프한다.
#define PANEL_X    (LIST_X + LIST_W + SBW + 12)
#define PANEL_W    330
#define PANEL_HEAD_H 62        // 판 위 제목 세 줄(맨 아래가 발견·보고 줄)
#define PROW_H     58          // 이름 / 도시·자금 / 친밀도 세 줄
#define PFACE_W    38          // 초상화(원본 80x96 의 비를 지킨다)
#define PFACE_H    46
#define PROWS_VIS  ((LIST_H - PANEL_HEAD_H) / PROW_H)
// 줄을 고르기 전에는 판을 아예 안 낸다 — 창이 목록 너비로 좁아진다.
#define CLIENT_W_NARROW (LIST_X + LIST_W + SBW + FRAME + 8)
#define CLIENT_W   (PANEL_X + PANEL_W + FRAME + 8)
#define CLIENT_H   (LIST_Y + LIST_H + 34)
#define ID_WARP_BASE 0xC000u   // TradeUtilKR 이 가로채는 워프 커맨드 = 이 값 + 도시 번호

#define MODE_HINT 0
#define MODE_DISC 1

// 상태 추리기. 뜻은 보는 것에 따라 다르다 — 힌트면 [힌트만]/[발견함],
// 발견물이면 [발견]/[아직]. 여기서 [발견] 은 보고 전이라도 발견한 것을 말한다.
#define F_ALL  0
#define F_A    1
#define F_B    2
#define FILT_N 3
static const wchar_t* kFiltHint[FILT_N] = { L"전체", L"힌트만", L"발견함" };
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
static int g_pick = -1;         // 오른쪽 판에 띄운 줄(힌트/발견물 번호). -1 = 아직 안 고름
static int g_pscroll = 0;       // 오른쪽 판 스크롤

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

// 줄을 고르면 판만큼 넓히고, 풀면 도로 좁힌다.
// 친밀도가 높으면 눈에 띄게. 게임에서 후원 문턱이 대략 이쯤이라 눈대중으로 나눴다.
static COLORREF im_col(int row)
{
    int v = Patron_Intimacy(row);
    if (v < 0)  return COL_DARK;
    if (v >= 60) return COL_WARN_TX;
    return COL_TEXT;
}

static void SizeToPick(HWND h)
{
    RECT rc;
    int want = (g_pick >= 0) ? CLIENT_W : CLIENT_W_NARROW;
    if (!h || !GetWindowRect(h, &rc)) return;
    if (rc.right - rc.left == want) return;
    SetWindowPos(h, NULL, 0, 0, want, CLIENT_H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(h, NULL, TRUE);
}

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
    int w = (LIST_W + SBW - 8 * 2) / (HINT_CAT_N + 1);
    r.left = LIST_X + i * (w + 2); r.right = r.left + w;
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
{ RECT r; r.left=LIST_X; r.right=LIST_X+LIST_W; r.top=LIST_Y; r.bottom=LIST_Y+LIST_H; return r; }
static RECT RcTrack(void)
{ RECT r; r.left=LIST_X+LIST_W; r.right=r.left+SBW; r.top=LIST_Y; r.bottom=LIST_Y+LIST_H; return r; }
// 오른쪽 판
static RECT RcPanel(void)
{ RECT r; r.left=PANEL_X; r.right=PANEL_X+PANEL_W; r.top=LIST_Y; r.bottom=LIST_Y+LIST_H; return r; }
static RECT RcPRow(int v)
{ RECT r=RcPanel(); r.top=LIST_Y+v*PROW_H; r.bottom=r.top+PROW_H-2; return r; }
// 워프 단추에는 갈 도시 이름을 적는다 — 어디로 가는지 보이게.
static RECT RcPWarp(int v)
{ RECT r=RcPRow(v); RECT b; b.right=r.right-8; b.left=b.right-92; b.top=r.top+18; b.bottom=b.top+24; return b; }
static RECT RcRow(int v)
{ RECT r = RcList(); r.top = LIST_Y + v*ROW_H; r.bottom = r.top + ROW_H; return r; }

static int MaxScroll(void) { int m = g_viewN - ROWS_VIS; return m > 0 ? m : 0; }

// 힌트 한 줄이 어디까지 왔나. 인스턴스를 먼저 본다 — 발견 비트는 보고해야 켜지니까.
#define HS_UNKNOWN  -1
#define HS_NONE      0      // 힌트도 없다
#define HS_GOT       1      // 힌트만 얻었다
#define HS_FOUND     2      // 발견했지만 아직 보고 안 했다
#define HS_REPORTED  3      // 보고까지 마쳤다
static int HintStage(int i)
{
    int st = HintDb_State(i);
    if (st < 0) return HS_UNKNOWN;
    if (HINT_IS_DONE(st))          return HS_REPORTED;   // 발견 비트 = 보고까지 마쳤다
    if (DInst_HintReported(i) > 0) return HS_REPORTED;
    if (DInst_HintFound(i) > 0)    return HS_FOUND;
    return (st & 1) ? HS_GOT : HS_NONE;
}

static int Keep(int i)
{
    if (g_filt == F_ALL) return 1;
    if (g_mode == MODE_DISC) {
        int f = Disc_Found(i);
        int found = (f == DISC_FOUND || f == DISC_REPORTED);
        if (f == DISC_UNKNOWN) return 1;                 // 아직 못 읽으면 추리지 않는다
        if (f == DISC_NOLINK) return 0;                  // 발견이라는 개념이 없는 줄
        return (g_filt == F_A) ? found : !found;
    }
    {   int stage = HintStage(i);
        if (stage == HS_UNKNOWN) return 1;               // 상태를 못 읽으면 추리지 않는다
        // [힌트만] 은 "아직 안 찾은 힌트" 다 — 찾아 놓고 보고만 안 한 줄은 여기서 빠진다.
        return (g_filt == F_A) ? (stage == HS_GOT) : (stage >= HS_FOUND);
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
        case DISC_REPORTED: *done = 1; return L"발견 · 보고함";
        case DISC_FOUND:    *done = 1; return L"발견 · 보고 전";
        case DISC_HINTED:              return L"힌트 있음";
        case DISC_NOT:      *dim = 1;  return Disc_Taken(i) > 0 ? L"— 남이 선취" : L"—";
        case DISC_NOLINK:   *dim = 1;  return L"";     // 발견이라는 개념이 없는 줄(교역품 따위)
        default:                       return L"세이브 전";
        }
    }
    switch (HintStage(i)) {
    case HS_REPORTED: *done = 1; return L"발견 · 보고함";
    case HS_FOUND:    *done = 1; return L"발견 · 보고 전";
    case HS_GOT:                 return L"힌트 있음";
    case HS_NONE:     *dim = 1;  return DInst_HintTaken(i) > 0 ? L"— 남이 선취" : L"—";
    default:                     return L"?";
    }
}

// 고른 줄에 딸린 발견물 인스턴스. 힌트에 여럿 걸리면 내가 발견한 것을 앞세운다.
static int PickInst(void)
{
    int k, n, best = -1;
    if (g_pick < 0) return -1;
    if (g_mode == MODE_DISC) return Disc_Inst(g_pick);
    n = DInst_CountOfHint(g_pick);
    for (k = 0; k < n; k++) {
        int inst = DInst_OfHint(g_pick, k);
        if (best < 0) best = inst;
        if (DInst_Filled(inst, DINST_ME)) return inst;
    }
    return best;
}

// 판 셋째 줄 — 언제 발견했고 보고는 했는지. 인스턴스가 없는 줄은 그렇다고 적는다.
static void PickTrace(wchar_t* out)
{
    int inst = PickInst();
    if (inst < 0)      { lstrcpyW(out, L"이 줄은 발견물 칸이 없어 힌트 상태로만 봅니다"); return; }
    if (!DInst_Live()) { lstrcpyW(out, L"발견 자취는 세이브를 불러온 뒤에 나옵니다"); return; }

    if (DInst_Filled(inst, DINST_ME)) {
        if (DInst_Filled(inst, DINST_REPORT))
            wsprintfW(out, L"발견 %d년 %d월 · 보고 %d년 %d월",
                      DInst_Year(inst, DINST_ME), DInst_Month(inst, DINST_ME),
                      DInst_Year(inst, DINST_REPORT), DInst_Month(inst, DINST_REPORT));
        else
            wsprintfW(out, L"발견 %d년 %d월 · 아직 보고 안 함 — 후원자에게 보고해야 값을 받습니다",
                      DInst_Year(inst, DINST_ME), DInst_Month(inst, DINST_ME));
        return;
    }
    if (DInst_Filled(inst, DINST_OTHER))
        wsprintfW(out, L"아직 못 찾음 — %s 이(가) %d년에 먼저 발견",
                  DInst_Who(inst, DINST_OTHER), DInst_Year(inst, DINST_OTHER));
    else
        lstrcpyW(out, L"아직 못 찾음");
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
        wsprintfW(buf, Disc_Live() ? L"%d개 · 발견 여부가 있는 줄 %d개"
                                   : L"%d개 — 세이브를 불러오면 상태가 나옵니다",
                  g_viewN, DINST_N);
    else
        wsprintfW(buf, HintDb_Live() ? L"%d개" : L"%d개 — 세이브를 불러오면 상태가 나옵니다", g_viewN);
    r.left = LIST_X + 132 + FILT_N*84; r.right = LIST_X + LIST_W + SBW;
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

    // ---- 오른쪽 판: 고른 줄의 분류를 좋아하는 후원자 ----
    if (g_pick >= 0) {
        RECT p = RcPanel(), t2;
        int n = PPick_Count(), v;
        br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);

        t2 = p; t2.left += 8; t2.right -= 8; t2.top += 4; t2.bottom = t2.top + 20;
        {
            wsprintfW(buf, L"%s · %s", Name(g_pick), HintDb_CatName(Cat(g_pick)));
            UI_Text(dc, t2, buf, g_font, COL_TEXT,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            t2.top += 19; t2.bottom += 19;
            // 지금 연도를 읽었으면 등장한 사람이 몇인지 같이 적는다(그 사람들이 위에 있다).
            if (PPick_Year() > 0)
                wsprintfW(buf, L"이 분류를 좋아하는 스폰서 %d명 — %d년 현재 %d명",
                          n, PPick_Year(), PPick_LiveCount());
            else
                wsprintfW(buf, L"이 분류를 좋아하는 스폰서 %d명 — 오른쪽 도시를 누르면 워프", n);
            UI_Text(dc, t2, buf, g_smallFont, COL_DARK,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            // 셋째 줄 — 언제 발견했고 보고는 했는지(힌트 배열만으로는 안 보이던 것이다).
            t2.top += 18; t2.bottom += 18;
            PickTrace(buf);
            UI_Text(dc, t2, buf, g_smallFont, COL_LANG_TX,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }

        for (v = 0; v < PROWS_VIS; v++) {
            int k = g_pscroll + v, row2, wi, lv;
            RECT pr, tt;
            if (g_pick < 0 || k >= n) break;
            row2 = PPick_Row(k);
            wi   = PPick_WarpIndex(k);
            lv   = PPick_Live(k);
            pr = RcPRow(v); pr.top += PANEL_HEAD_H; pr.bottom += PANEL_HEAD_H;
            if (pr.bottom > p.bottom) break;
            if (v & 1) { br = CreateSolidBrush(COL_ROW_ALT); FillRect(dc, &pr, br); DeleteObject(br); }
            Face_Draw(dc, pr.left + 6, pr.top + 3, PFACE_W, PFACE_H,
                      CharDb_PatronGender(row2), CharDb_PatronFace(row2));
            tt = pr; tt.left = pr.left + PFACE_W + 12; tt.right = pr.right - 104;
            tt.top = pr.top + 2; tt.bottom = tt.top + 19;
            // 아직 안 나왔거나 이미 물러난 사람은 이름부터 흐리게 — 찾아가도 못 만난다.
            UI_Text(dc, tt, CharDb_PatronName(row2), g_font,
                    lv == PPICK_NOW ? COL_TEXT : COL_DARK,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            tt.top = tt.bottom; tt.bottom = tt.top + 17;
            if (lv == PPICK_LATER)
                wsprintfW(buf, L"%s · %d만 · %d년 등장", CharDb_PatronCity(row2),
                          CharDb_PatronWealthAt(row2) / 10000, PPick_Appear(k));
            else if (lv == PPICK_GONE)
                wsprintfW(buf, L"%s · %d만 · %d년 은퇴", CharDb_PatronCity(row2),
                          CharDb_PatronWealthAt(row2) / 10000, PPick_Retire(k));
            else
                wsprintfW(buf, L"%s · %d만", CharDb_PatronCity(row2),
                          CharDb_PatronWealthAt(row2) / 10000);
            UI_Text(dc, tt, buf, g_smallFont, COL_DARK,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            // 친밀도는 실행 중에만 있는 값이라 세이브를 불러오기 전에는 "—" 다.
            tt.top = tt.bottom; tt.bottom = tt.top + 17;
            { int im = Patron_Intimacy(row2);
              if (im >= 0) wsprintfW(buf, L"나와의 친밀도 %d", im);
              else         lstrcpyW(buf, L"나와의 친밀도 —"); }
            UI_Text(dc, tt, buf, g_smallFont, im_col(row2),
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            { RECT wb = RcPWarp(v); wb.top += PANEL_HEAD_H; wb.bottom += PANEL_HEAD_H;
              if (wi >= 0) UI_Button(dc, wb, CharDb_PatronCity(row2), FALSE);
              else UI_Text(dc, wb, CharDb_PatronCity(row2), g_smallFont, COL_DARK,
                           DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX); }
        }
        if (g_pick >= 0 && n > PROWS_VIS) {
            t2 = p; t2.left += 8; t2.right -= 8; t2.bottom = p.bottom - 2; t2.top = t2.bottom - 18;
            wsprintfW(buf, L"%d/%d — 판 위에서 휠", g_pscroll + 1, n);
            UI_Text(dc, t2, buf, g_smallFont, COL_DARK,
                    DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        br = CreateSolidBrush(COL_DARK); FrameRect(dc, &p, br); DeleteObject(br);
    }

    r.left = FRAME + 10; r.right = rc.right - FRAME - 10;
    r.top = LIST_Y + LIST_H + 6; r.bottom = r.top + 22;
    UI_Text(dc, r,
            g_mode == MODE_DISC
              ? L"발견 여부는 발견물 칸에서 바로 읽습니다 — 보고는 그 다음 일입니다."
              : L"발견하면 보고 전에도 [힌트만] 에서 빠집니다. 값은 고치지 않습니다.",
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
    Disc_Load();
    DInst_Load();
    Rebuild();
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK HintProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        Face_Load();          // 오른쪽 판의 스폰서 초상화
        HintDb_Load();
        Disc_Load();
        DInst_Load();
        Rebuild();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(h); return 0;
    case WM_MOUSEWHEEL:
    {
        POINT pt; RECT pr = RcPanel();
        int d = (GET_WHEEL_DELTA_WPARAM(w) > 0 ? -3 : 3);
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        ScreenToClient(h, &pt);
        if (PtInRect(&pr, pt)) {                        // 판 위에서는 판을 굴린다
            int mx = PPick_Count() - PROWS_VIS;
            g_pscroll += d;
            if (g_pscroll > mx) g_pscroll = mx;
            if (g_pscroll < 0) g_pscroll = 0;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        ScrollTo(h, g_scroll + d);
        return 0;
    }
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
                g_scroll = 0; g_pick = -1; Rebuild(); SizeToPick(h); InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        for (i = 0; i <= HINT_CAT_N; i++) {
            r = RcTab(i);
            if (!PtInRect(&r, pt)) continue;
            g_cat = (i == 0) ? -1 : i - 1;
            g_scroll = 0; g_pick = -1; Rebuild(); SizeToPick(h); InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        for (i = 0; i < FILT_N; i++) {
            r = RcFilt(i);
            if (!PtInRect(&r, pt)) continue;
            g_filt = i;
            g_scroll = 0; g_pick = -1; Rebuild(); SizeToPick(h); InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        r = RcTrack();
        if (PtInRect(&r, pt)) {
            int mid = (r.top + r.bottom) / 2;
            ScrollTo(h, g_scroll + (pt.y < mid ? -ROWS_VIS : ROWS_VIS));
            return 0;
        }
        r = RcList();                                   // 줄을 고르면 오른쪽 판을 채운다
        if (PtInRect(&r, pt)) {
            int k = g_scroll + (pt.y - LIST_Y) / ROW_H;
            if (k >= 0 && k < g_viewN) {
                g_pick = g_view[k];
                g_pscroll = 0;
                PPick_Build(Cat(g_pick));
                SizeToPick(h);
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        r = RcPanel();                                  // 판의 [워프]
        if (PtInRect(&r, pt) && g_pick >= 0) {
            int v;
            for (v = 0; v < PROWS_VIS; v++) {
                RECT wb = RcPWarp(v);
                int wi = PPick_WarpIndex(g_pscroll + v);
                wb.top += PANEL_HEAD_H; wb.bottom += PANEL_HEAD_H;
                if (!PtInRect(&wb, pt) || wi < 0) continue;
                // 워프는 TradeUtilKR 이 게임 창에서 가로챈다 — 그쪽이 없으면 아무 일도 안 난다.
                if (g_gameHwnd)
                    PostMessageW(g_gameHwnd, WM_COMMAND, MAKEWPARAM(ID_WARP_BASE + wi, 0), 0);
                return 0;
            }
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
                        g_pick = -1; Rebuild(); SizeToPick(h); InvalidateRect(h, NULL, FALSE); return 0;
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
            x = orc.left + ((orc.right - orc.left) - CLIENT_W_NARROW) / 2;
            y = orc.top  + ((orc.bottom - orc.top) - CLIENT_H) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
        }
        g_wnd = CreateWindowExW(0, WC_HINT, L"힌트", WS_POPUP,
                    x, y, CLIENT_W_NARROW, CLIENT_H, g_gameHwnd, NULL, g_hinst, NULL);
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
