#include "character.h"
#include "ui.h"
#include "faces.h"
#include "chardb.h"
#include "maids.h"
#include "patrons.h"
#include "charstate.h"
#include "navview.h"
#include "questview.h"
#include "invview.h"
#include "questdb.h"     // Quest_Init — 창을 열기 전에 quests.json 을 반영해야 한다
#include <windowsx.h>

// fb15/fb16: 인물(얼굴) 코드 브라우저 — 갤러리(2열, 스크롤) + 남/여/카테고리 필터.
//   얼굴 = 80x96 8bpp(LS12 디코드), kFacePalette 로 컬러화(게임 캡처 역산 근사 팔레트).
//   상단 "파일" 메뉴에 "정보" 항목 추가(서브클래싱으로 클릭 가로챔) → 브라우저 오픈.
// fb31: 창을 탭으로 나눔 — [도감] 은 기존 얼굴 브라우저, [항해사 찾기] 는
//   SAVEDATA.CDS 를 읽어 고용 가능한 인물을 특기/레벨로 추려 보여준다(navview.c, 읽기 전용).
//   ui.h 의 CHARKR_SHOW_NAV_TAB=0 으로 두면 [항해사 찾기] 만 빠진다.
// fb32: [여급] 을 도감의 카테고리 버튼에서 빼내 독립 탭으로 올림. 여급은 얼굴이 아니라
//   CDS_95.EXE 여급 표의 "행"이 단위다(maids.c) — 얼굴 3개(23·34·77)를 여급 두 명이
//   나눠 쓰기 때문에 얼굴로 세면 뒤쪽 한 명이 통째로 빠진다. 상세도 char_info.h 대신
//   그 표에서 바로 뽑고, 생년은 select box 로 메모리에 직접 써넣는다.
//   갤러리 격자/스크롤은 [도감] 과 그대로 공유한다(g_filt 항목 종류만 다르다).

#define ID_CHAR   0xB301
#define WC_CHAR   L"CharUtilKR_Browser"

#define TAB_GALLERY 0
#define TAB_NAV     1
#define TAB_MAID    2
#define TAB_PATRON  3
#define TAB_QUEST   4
#define TAB_INV     5
// 아래 둘은 이 창의 탭이 아니라 TradeUtilKR 의 창을 여는 단추다. 메뉴에 항목이 너무 많아져
// "교역" · "교역품" 을 여기로 옮겼다. 누르면 게임 창에 그쪽 커맨드를 보내 창을 띄운다.
#define TAB_SISE    6
#define TAB_GOODS   7
#define ID_TRADE_SISE  0xB101u
#define ID_TRADE_GOODS 0xB102u

static HINSTANCE g_hinst = NULL;
static HWND    g_gameHwnd = NULL;
static HWND    g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static HWND    g_wnd = NULL;
#if CHARKR_SHOW_NAV_TAB
static int     g_tab = TAB_NAV;       // 이 창을 여는 목적이 대개 항해사 찾기라 이쪽을 기본으로 연다
#else
static int     g_tab = TAB_MAID;      // 항해사 찾기를 뺀 빌드에서는 여급이 맨 왼쪽 탭이다
#endif
static int     g_gender = FACE_MALE;  // [도감] 탭에서만 쓴다. [여급] 은 전원 여성이라 고정
static int     g_scroll = 0;   // 맨 위에 보이는 행
static int     g_catFilter = 0;// 0=전체 1=인물 4=기타 (여급·스폰서는 독립 탭으로 뺐다)
static int     g_prefFilter = 0; // 스폰서 취향 추리기. 0=전체, 그 밖은 (취향비트 + 1)
// 스폰서 정렬. 0=자금 많은 순(기본) 1=자금 적은 순 2=얼굴코드 순
#define SORT_N 3
static int     g_sponsorSort = 0;
static const wchar_t* kSortBtn[SORT_N] = { L"자금 ↓", L"자금 ↑", L"번호순" };

// 초상화를 어느 표에서 꺼낼지. [여급] 탭은 성별 버튼 없이 항상 여성이다.
// ([스폰서] 탭은 남녀가 섞여 있어 칸마다 다르다 — EntryGender 를 쓴다.)
static int CurGender(void) { return g_tab == TAB_MAID ? FACE_FEMALE : g_gender; }
// 갤러리 한 칸. 셋 다 -1 이면 얼굴코드 한 개가 한 칸(인물/기타),
// maid >= 0 이면 여급 표의 그 행, patron >= 0 이면 후원자 표의 그 행이 한 칸이다.
// (여급도 후원자도 여러 명이 얼굴 하나를 나눠 써서, 얼굴로 세면 사람이 빠진다.)
typedef struct { short face; short maid; short patron; } GalEntry;
static GalEntry g_filt[600];   // 현재 필터에 맞는 항목 목록
static int     g_filtCount = 0;
static int     g_maidsOk = 0;  // 여급 표를 메모리에서 읽는 데 성공했는지

// 그 칸의 초상화를 어느 표에서 꺼낼지. 스폰서는 남녀가 섞여 있어 행마다 다르다.
static int EntryGender(int gi)
{
    return g_filt[gi].patron >= 0 ? CharDb_PatronGender(g_filt[gi].patron) : CurGender();
}

static int TotalRows(void) { return (g_filtCount + COLS - 1) / COLS; }
static int MaxScroll(void) { int m = TotalRows() - ROWS_VIS; return m < 0 ? 0 : m; }

static void Emit(int face, int maid, int patron)
{
    if (g_filtCount >= (int)(sizeof(g_filt)/sizeof(g_filt[0]))) return;
    g_filt[g_filtCount].face   = (short)face;
    g_filt[g_filtCount].maid   = (short)maid;
    g_filt[g_filtCount].patron = (short)patron;
    g_filtCount++;
}

// 현재 탭에 맞는 항목 목록을 g_filt 에 채운다.
// [여급]/[스폰서] 는 EXE 표를 행 순서대로 펼친다(얼굴을 나눠 쓰는 사람들이 따로 나온다).
// [도감] 은 얼굴코드 한 개가 한 칸인 얼굴 카탈로그다.
static void RebuildFilter(void)
{
    int i;
    g_filtCount = 0;
    if (g_tab == TAB_MAID) {
        for (i = 0; i < Maid_Count(); i++) Emit(Maid_At(i)->face, i, -1);
    } else if (g_tab == TAB_PATRON) {
        for (i = 0; i < CharDb_PatronCount(); i++) {
            if (g_prefFilter > 0 && !(CharDb_PatronPrefAt(i) >> (g_prefFilter - 1) & 1)) continue;
            Emit(CharDb_PatronFace(i), -1, i);
        }
        // 자금으로 줄 세운다. 자금을 모르는 후원자(-1)는 늘 맨 뒤로.
        // 80여 개라 삽입 정렬로 충분하다.
        if (g_sponsorSort != 2) {
            int a, b;
            for (a = 1; a < g_filtCount; a++) {
                GalEntry v = g_filt[a];
                int vw = CharDb_PatronWealthAt(v.patron);
                for (b = a - 1; b >= 0; b--) {
                    int w = CharDb_PatronWealthAt(g_filt[b].patron);
                    int before = (vw < 0) ? 0 : (w < 0) ? 1 :
                                 (g_sponsorSort == 0 ? vw > w : vw < w);
                    if (!before) break;
                    g_filt[b + 1] = g_filt[b];
                }
                g_filt[b + 1] = v;
            }
        }
    } else {
        int total = Face_Count(g_gender);
        for (i = 0; i < total; i++) {
            int cat = CharDb_Cat(g_gender, i);
            int ok = (g_catFilter == 0) ? 1 :
                     (g_catFilter == 4) ? (cat == 0) : (cat == g_catFilter);
            if (ok) Emit(i, -1, -1);
        }
    }
    g_scroll = 0;
}

// 카테고리 버튼. 여급·스폰서는 독립 탭으로 뺐으므로 여기서는 빠진다
// (라벨 순서 != 카테고리 번호).
static const struct { const wchar_t* label; int cat; } kCatBtn[] = {
    { L"전체", 0 }, { L"인물", 1 }, { L"기타", 4 },
};
#define CAT_N ((int)(sizeof(kCatBtn)/sizeof(kCatBtn[0])))
static RECT CloseRect(RECT c) { RECT r; r.right=c.right-FRAME-4; r.left=r.right-22; r.top=FRAME+4; r.bottom=r.top+18; return r; }
// 탭은 [항해사 찾기][여급][도감] 순으로 왼쪽부터 — 자주 쓰는 쪽을 왼쪽에 둔다
// (탭 번호 순서와는 별개다). CHARKR_SHOW_NAV_TAB=0 이면 맨 앞 하나만 빠진다.
static const struct { int id; const wchar_t* label; int w; } kTabs[] = {
#if CHARKR_SHOW_NAV_TAB
    { TAB_NAV,     L"항해사 찾기", 110 },
#endif
    { TAB_QUEST,   L"퀘스트",       70 },
    { TAB_INV,     L"소지품",       70 },
    { TAB_MAID,    L"여급",         60 },
    { TAB_PATRON,  L"스폰서",       70 },
    { TAB_GALLERY, L"도감",         70 },
    { TAB_SISE,    L"교역",         60 },
    { TAB_GOODS,   L"교역품",       70 },
};
#define TAB_N ((int)(sizeof(kTabs)/sizeof(kTabs[0])))

static RECT TabRectAt(int i)
{
    RECT r; int k, x = 13;
    for (k = 0; k < i; k++) x += kTabs[k].w + 6;
    r.left = x; r.right = x + kTabs[i].w;
    r.top = TAB_Y + 2; r.bottom = TAB_Y + TAB_H - 2;
    return r;
}
static RECT MaleRect(void)   { RECT r; r.left=FRAME+8;  r.top=FILTER_Y; r.right=r.left+40; r.bottom=r.top+22; return r; }
static RECT FemaleRect(void) { RECT r; r.left=FRAME+50; r.top=FILTER_Y; r.right=r.left+40; r.bottom=r.top+22; return r; }
static RECT CatRect(int i)   { RECT r; r.left=FRAME+100 + i*54; r.right=r.left+50; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT SbTrack(void)    { RECT r; r.right=WIN_W-FRAME-2; r.left=r.right-SB_W; r.top=GY; r.bottom=GY+GAL_H; return r; }

// ---- 여급 값 편집 (직접 그린 select box + 펼침 목록) ----
// 자식 COMBOBOX 는 게임 DirectDraw 화면 위에서 불안정해서 navview 와 같은 방식으로 직접 그린다.
// 목록 셋은 격자 크기만 다르고 나머지는 같아서 DropGeom() 하나로 묶어 둔다.
//   DROP_YEAR 1470~1530 중 하나 고르면 닫힘
//   DROP_LANG 14종 체크박스 — 여러 개를 켰다 껐다 해야 하니 열어 둔 채 토글만
//   DROP_CITY 226개 — 한 화면에 안 들어가서 이 목록만 휠로 스크롤한다
//   DROP_PREF 스폰서 취향 추리기 — 필터바에 있고 여급과 무관하다
#define DROP_NONE 0
#define DROP_YEAR 1
#define DROP_LANG 2
#define DROP_CITY 3
#define DROP_PREF 4
//   DROP_PYEAR 스폰서 등장연도 — 여급 생년과 같은 격자를 쓰되 기준연도만 다르다
#define DROP_PYEAR 5

#define DD_ITEM_H 22

#define YR_COLS   6
#define YR_ROWS   11                                  // 6x11=66 칸에 61개 연도(1470~1530)
#define YR_ITEM_W 46
#define LG_COLS   2
#define LG_ROWS   7                                   // 2x7=14
#define LG_ITEM_W 130
#define CT_COLS   5
#define CT_ROWS   12                                  // 한 번에 60개씩 보이고 나머지는 스크롤
#define CT_ITEM_W 100
#define PF_COLS   1
#define PF_ROWS   (CHARDB_PREF_N + 1)                 // (전체) + 취향 8종
#define PF_ITEM_W 100

// 목록 크기를 늘리고 행 수를 안 맞추면 뒤쪽 항목이 조용히 사라진다. 여기서 막는다.
typedef char YearGridFits[(YR_COLS * YR_ROWS >= MAID_YEAR_N) ? 1 : -1];
typedef char LangGridFits[(LG_COLS * LG_ROWS >= MAID_LANG_N) ? 1 : -1];

static int  g_drop = DROP_NONE;
static int  g_dropRow = -1;   // 목록을 펼친 여급 행
static RECT g_ddPanel;        // 펼칠 때 한 번만 잰다(스크롤하면 닫으므로 다시 안 잰다)
static int  g_ddScroll = 0;   // DROP_CITY 전용. 맨 위에 보이는 행

static int DropItemCount(int kind)
{
    return kind == DROP_YEAR ? MAID_YEAR_N :
           kind == DROP_LANG ? MAID_LANG_N :
           kind == DROP_CITY ? Maid_CityCount() :
           kind == DROP_PREF ? (CHARDB_PREF_N + 1) :
           kind == DROP_PYEAR ? PATRON_YEAR_N : 0;
}
static void DropGeom(int kind, int* cols, int* rows, int* iw)
{
    if (kind == DROP_YEAR || kind == DROP_PYEAR)
                                { *cols=YR_COLS; *rows=YR_ROWS; *iw=YR_ITEM_W; }
    else if (kind == DROP_LANG) { *cols=LG_COLS; *rows=LG_ROWS; *iw=LG_ITEM_W; }
    else if (kind == DROP_PREF) { *cols=PF_COLS; *rows=PF_ROWS; *iw=PF_ITEM_W; }
    else                        { *cols=CT_COLS; *rows=CT_ROWS; *iw=CT_ITEM_W; }
}

// 취향 추리기 상자의 라벨. 0 = 전체.
static const wchar_t* PrefText(int i) { return i == 0 ? L"(전체)" : CharDb_PrefName(i - 1); }
static RECT PrefRect(void) { RECT r; r.left=FRAME+56;  r.right=r.left+100; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT SortRect(void) { RECT r; r.left=FRAME+164; r.right=r.left+70;  r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static int DropTotalRows(int kind)
{
    int cols, rows, iw;
    DropGeom(kind, &cols, &rows, &iw);
    return (DropItemCount(kind) + cols - 1) / cols;
}
static int DropMaxScroll(int kind)
{
    int m;
    if (kind != DROP_CITY) return 0;   // 연도/언어는 한 화면에 다 들어간다
    m = DropTotalRows(kind) - CT_ROWS;
    return m < 0 ? 0 : m;
}

// 셀(x,y) 안 정보 패널 아래쪽의 편집 줄.
//   맨 아랫줄: "생년" 라벨 + 연도 상자 (+ CHARKR_EDIT_LANG 이면 그 오른쪽에 [언어 수정])
//   그 윗줄  : CHARKR_EDIT_CITY 일 때만 "도시" 라벨 + 도시 상자
// 도시 편집이 꺼져 있으면 도시는 본문 글씨로 내려가고 편집 줄이 하나로 줄어, 그만큼
// 본문(언어 목록)이 넓어진다.
#if CHARKR_EDIT_CITY
#define MAID_CTRL_H 55
#else
#define MAID_CTRL_H 30
#endif

static RECT MaidYearRect(int x, int y)
{
    RECT r;
    r.left   = x + PORT_W + 8 + 36;
    r.right  = r.left + 96;
    r.top    = y + PORT_H - 27;
    r.bottom = r.top + DD_ITEM_H;
    return r;
}
static RECT MaidCityRect(int x, int y)
{
    RECT r = MaidYearRect(x, y);
    r.top -= 25; r.bottom -= 25;
    return r;
}
static RECT MaidLabelRect(RECT box, int x)
{
    RECT r = box;
    r.right = r.left - 4;
    r.left  = x + PORT_W + 8 + 2;
    return r;
}
static RECT MaidLangRect(int x, int y)
{
    RECT r = MaidYearRect(x, y);
    r.left  = r.right + 8;
    r.right = x + PORT_W + 8 + INFO_W - 4;
    return r;
}

// 펼친 목록. 갤러리 위에 덮어 그리므로 PaintGallery 맨 끝에서 부른다.
static void PaintDropPanel(HDC dc)
{
    RECT p = g_ddPanel, it;
    HBRUSH br;
    int cols, rows, iw, i, n, first;
    const MaidInfo* m;

    if (g_drop == DROP_NONE) return;
    m = Maid_At(g_dropRow);
    DropGeom(g_drop, &cols, &rows, &iw);
    n = DropItemCount(g_drop);
    first = g_ddScroll * cols;

    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK);    FrameRect(dc, &p, br); DeleteObject(br);

    for (i = first; i < n && i - first < cols * rows; i++) {
        int k = i - first, cur = 0;
        const wchar_t* label;
        wchar_t buf[16];

        it.left   = p.left + (k % cols) * iw + 1;
        it.right  = it.left + iw - 2;
        it.top    = p.top  + (k / cols) * DD_ITEM_H + 1;
        it.bottom = it.top + DD_ITEM_H - 1;

        if (g_drop == DROP_PREF) {
            cur = (i == g_prefFilter);
            label = PrefText(i);
        } else if (g_drop == DROP_PYEAR) {
            cur = (PATRON_YEAR_MIN + i == Patron_Year(g_dropRow));
            wsprintfW(buf, L"%d", PATRON_YEAR_MIN + i);
            label = buf;
        } else if (g_drop == DROP_YEAR) {
            cur = (MAID_YEAR_MIN + i == Maid_Year(m));
            wsprintfW(buf, L"%d", MAID_YEAR_MIN + i);
            label = buf;
        } else if (g_drop == DROP_CITY) {
            cur = (i == m->city);
            label = Maid_CityName(i);
        } else {
            cur = (m->lang >> i) & 1;
            label = Maid_LangName(i);
        }

        if (cur && g_drop != DROP_LANG) {
            br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &it, br); DeleteObject(br);
        }

        if (g_drop == DROP_LANG) {
            // 켜진 언어는 [v], 꺼진 언어는 빈 상자
            RECT bx;
            bx.left = it.left + 5; bx.right = bx.left + 13;
            bx.top  = it.top + 4;  bx.bottom = bx.top + 13;
            br = CreateSolidBrush(cur ? COL_SEL_BG : COL_LIGHT); FillRect(dc, &bx, br); DeleteObject(br);
            UI_Bevel(dc, bx, TRUE);
            if (cur) UI_Text(dc, bx, L"v", g_smallFont, RGB(250,244,228),
                             DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            it.left = bx.right + 6;
            UI_Text(dc, it, label, g_smallFont, COL_TEXT,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        } else {
            int centered = (g_drop == DROP_YEAR || g_drop == DROP_PYEAR);
            if (!centered) it.left += 6;   // 왼쪽 정렬은 글씨가 테두리에 붙지 않게
            UI_Text(dc, it, label, g_smallFont, cur ? RGB(250,244,228) : COL_TEXT,
                    (centered ? DT_CENTER : DT_LEFT)
                    | DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
    }

    if (g_drop == DROP_CITY) {
        RECT tr = p;
        tr.left = p.left + cols * iw; tr.right = p.right;
        UI_Scrollbar(dc, tr, g_ddScroll, DropMaxScroll(g_drop), CT_ROWS, DropTotalRows(g_drop));
    }
}

// 펼친 목록에서 클릭 지점의 항목 번호. 목록 밖이면 -1.
static int PanelHit(POINT pt)
{
    int cols, rows, iw, col, row, i;
    if (g_drop == DROP_NONE || !PtInRect(&g_ddPanel, pt)) return -1;
    DropGeom(g_drop, &cols, &rows, &iw);
    col = (pt.x - g_ddPanel.left) / iw;
    row = (pt.y - g_ddPanel.top)  / DD_ITEM_H;
    if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
    i = (g_ddScroll + row) * cols + col;
    return (i >= 0 && i < DropItemCount(g_drop)) ? i : -1;
}

// 누른 상자 바로 아래에 펼치되, 창 밖으로 나가면 안으로 밀어 넣는다
// (아래쪽 줄의 칸은 위로 펼친다).
static void OpenDrop(HWND h, int kind, int maidRow, RECT anchor)
{
    int cols, rows, iw, w, hgt;
    RECT p;

    DropGeom(kind, &cols, &rows, &iw);
    w   = cols * iw + (kind == DROP_CITY ? SB_W : 0);
    hgt = rows * DD_ITEM_H;

    p.left = anchor.left;   p.right  = p.left + w;
    p.top  = anchor.bottom; p.bottom = p.top + hgt;
    if (p.right > WIN_W - FRAME) { int d = p.right - (WIN_W - FRAME); p.left -= d; p.right -= d; }
    if (p.left < FRAME)          { p.left = FRAME; p.right = p.left + w; }
    if (p.bottom > WIN_H - FRAME) { p.top = anchor.top - hgt; p.bottom = p.top + hgt; }
    if (p.top < FRAME)            { p.top = FRAME; p.bottom = p.top + hgt; }

    g_drop = kind;
    g_dropRow = maidRow;
    g_ddPanel = p;
    // 도시 목록은 지금 도시가 보이는 자리에서 열어 준다(226개를 처음부터 훑지 않도록).
    g_ddScroll = 0;
    if (kind == DROP_CITY) {
        int want = Maid_At(maidRow)->city / cols - rows / 2;
        int mx = DropMaxScroll(kind);
        g_ddScroll = want < 0 ? 0 : (want > mx ? mx : want);
    }
    InvalidateRect(h, NULL, FALSE);
}

// 갤러리 좌표 -> 항목 번호. 셀 밖이면 -1. 맞으면 그 셀의 좌상단을 cx/cy 로 돌려준다.
static int CellHit(POINT pt, int* cx, int* cy)
{
    int r, c;
    for (r = 0; r < ROWS_VIS; r++) {
        for (c = 0; c < COLS; c++) {
            int x = GX + c*(CELL_W+GAP);
            int y = GY + r*ROW_PITCH;
            if (pt.x >= x && pt.x < x + CELL_W && pt.y >= y && pt.y < y + CELL_H) {
                int gi = (g_scroll + r)*COLS + c;
                if (gi >= g_filtCount) return -1;
                *cx = x; *cy = y;
                return gi;
            }
        }
    }
    return -1;
}

static void PaintGallery(HDC dc)
{
    int r, c;
    RECT ir;
    wchar_t cnt[32];

    // 탭마다 필터바가 다르다. [여급] 은 고를 게 없어 안내만, [스폰서] 는 취향/정렬만 둔다.
    if (g_tab == TAB_MAID) {
        ir.left=FRAME+8; ir.right=FRAME+430; ir.top=FILTER_Y; ir.bottom=FILTER_Y+22;
        UI_Text(dc, ir, L"CDS_95.EXE 여급 표 — 생년은 눌러서 바꾸면 메모리에 바로 반영됩니다",
                g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    } else if (g_tab == TAB_PATRON) {
        ir.left=FRAME+8; ir.right=FRAME+52; ir.top=FILTER_Y; ir.bottom=FILTER_Y+22;
        UI_Text(dc, ir, L"취향", g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        UI_Select(dc, PrefRect(), PrefText(g_prefFilter), g_drop == DROP_PREF);
        UI_Button(dc, SortRect(), kSortBtn[g_sponsorSort], g_sponsorSort != 2);
    } else {
        UI_Button(dc, MaleRect(),   L"남", g_gender==FACE_MALE);
        UI_Button(dc, FemaleRect(), L"여", g_gender==FACE_FEMALE);
        { int bi; for (bi=0;bi<CAT_N;bi++)
            UI_Button(dc, CatRect(bi), kCatBtn[bi].label, g_catFilter==kCatBtn[bi].cat); }
    }

    wsprintfW(cnt, L"%d명", g_filtCount);
    ir.left=FRAME+430; ir.right=WIN_W-FRAME-8; ir.top=FILTER_Y; ir.bottom=FILTER_Y+22;
    UI_Text(dc, ir, cnt, g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    if (g_tab == TAB_MAID && !g_maidsOk) {
        RECT e;
        e.left = GX; e.right = WIN_W - FRAME - GAP; e.top = GY; e.bottom = GY + 60;
        UI_Text(dc, e, L"CDS_95.EXE 에서 여급 표를 찾지 못했습니다(다른 버전의 실행 파일인 듯합니다).",
                g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        return;
    }

    for (r = 0; r < ROWS_VIS; r++) {
        int row = g_scroll + r;
        for (c = 0; c < COLS; c++) {
            int gi = row*COLS + c, face;
            int x = GX + c*(CELL_W+GAP);
            int y = GY + r*ROW_PITCH;
            if (gi >= g_filtCount) continue;
            face = g_filt[gi].face;
            Face_Draw(dc, x, y, PORT_W, PORT_H, EntryGender(gi), face);
            // 초상화 오른쪽 정보 패널 (이름/코드 + 카테고리별 상세)
            { int ix = x + PORT_W + 8, maid = g_filt[gi].maid, prow = g_filt[gi].patron;
              RECT lr; wchar_t hd[80]; wchar_t mb[256];
              const MaidInfo* m = maid >= 0 ? Maid_At(maid) : NULL;
              const wchar_t* nm; const wchar_t* nf;
              if (m) {
                  nm = m->name;
                  Maid_FormatInfo(m, mb, (int)(sizeof(mb)/sizeof(mb[0])));
                  nf = mb;
              } else if (prow >= 0) {
                  // 후계자가 선대 초상화를 물려받아 얼굴이 겹치므로 이름은 표에서 가져온다.
                  nm = CharDb_PatronName(prow);
                  CharDb_FormatPatronRow(prow, mb, (int)(sizeof(mb)/sizeof(mb[0])));
                  nf = mb;
              } else {
                  nm = CharDb_Name(EntryGender(gi), face);
                  nf = CharDb_Info(EntryGender(gi), face);
              }
              lr.left=ix-2; lr.top=y; lr.right=ix+INFO_W; lr.bottom=y+PORT_H;
              { HBRUSH b2=CreateSolidBrush(COL_DISP_BG); FillRect(dc,&lr,b2); DeleteObject(b2);
                b2=CreateSolidBrush(COL_DARK); FrameRect(dc,&lr,b2); DeleteObject(b2); }
              // 혈액형은 못 고치는 값이라 머리글에 붙여 아래 두 줄을 편집용으로 비운다.
              if (m) wsprintfW(hd, L"%s  #%d · %s형", nm[0]?nm:L"(무명)", face, Maid_BloodName(m->blood));
              else   wsprintfW(hd, L"%s  #%d", nm[0]?nm:L"(무명)", face);
              lr.left=ix+2; lr.right=ix+INFO_W-2; lr.top=y+3; lr.bottom=y+20;
              UI_Text(dc, lr, hd, g_font, COL_TEXT, DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
              // 여급/스폰서 칸은 아래쪽을 편집 컨트롤에 내준다.
              lr.top=y+22;
              lr.bottom = (m || prow >= 0) ? y+PORT_H-MAID_CTRL_H : y+PORT_H-3;
              UI_Text(dc, lr, nf, g_smallFont, COL_TEXT, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
              if (!m && prow >= 0) {   // 스폰서 등장연도(관련) — CHARKR_EDIT_PATRON_YEAR 참고
                  RECT yb = MaidYearRect(x,y);
                  wchar_t ys[16];
                  // 실행 중 표를 읽었으면 그쪽 값(고친 게 보이도록), 아니면 구운 값.
                  int py = Patron_Ready() ? Patron_Year(prow) : 0;
                  wsprintfW(ys, L"%d년", py ? py : CharDb_PatronAppear(prow));
                  UI_Text(dc, MaidLabelRect(yb,x), L"등장", g_smallFont, COL_TEXT,
                          DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
#if CHARKR_EDIT_PATRON_YEAR
                  UI_Select(dc, yb, ys, g_drop==DROP_PYEAR && g_dropRow==prow);
#else
                  yb.left += 6;   // 못 고치는 값이라 상자 없이 글자만
                  UI_Text(dc, yb, ys, g_smallFont, COL_TEXT,
                          DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
#endif
              }
              if (m) {
                  RECT yb = MaidYearRect(x,y);
                  wchar_t ys[16];
#if CHARKR_EDIT_CITY
                  RECT cb = MaidCityRect(x,y);
                  UI_Text(dc, MaidLabelRect(cb,x), L"도시", g_smallFont, COL_TEXT,
                          DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
                  UI_Select(dc, cb, Maid_CityName(m->city), g_drop==DROP_CITY && g_dropRow==maid);
#endif
#if CHARKR_EDIT_LANG
                  UI_Button(dc, MaidLangRect(x,y), L"언어 수정", g_drop==DROP_LANG && g_dropRow==maid);
#endif
                  wsprintfW(ys, L"%d년", Maid_Year(m));
                  UI_Text(dc, MaidLabelRect(yb,x), L"생년", g_smallFont, COL_TEXT,
                          DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
                  UI_Select(dc, yb, ys, g_drop==DROP_YEAR && g_dropRow==maid);
              } }
        }
    }

    UI_Scrollbar(dc, SbTrack(), g_scroll, MaxScroll(), ROWS_VIS, TotalRows());
    PaintDropPanel(dc);   // 펼친 목록은 갤러리 위에 덮어 그린다
}

static void OnPaint(HWND h)
{
    PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
    RECT rc, tb, tr; HBRUSH br;
    UiBuf buf; HDC dc;

    GetClientRect(h, &rc);
    // 메모리에 다 그린 뒤 한 번에 옮긴다. 화면에 바로 그리면 스크롤할 때마다 깜빡인다.
    dc = UI_BufBegin(&buf, hdc, rc.right, rc.bottom);
    br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);

    // 타이틀바
    tb.left=FRAME; tb.top=FRAME; tb.right=rc.right-FRAME; tb.bottom=FRAME+TITLE_H;
    UI_VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT); UI_Bevel(dc, tb, FALSE);
    tr = tb; tr.left += 8;
    UI_Text(dc, tr, L"정보", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    UI_Button(dc, CloseRect(rc), L"×", FALSE);

    { int i; for (i = 0; i < TAB_N; i++)
        UI_Button(dc, TabRectAt(i), kTabs[i].label, g_tab == kTabs[i].id); }

    if      (g_tab == TAB_NAV)   Nav_Paint(dc);
    else if (g_tab == TAB_QUEST) Quest_Paint(dc);
    else if (g_tab == TAB_INV)   Inv_Paint(dc);
    else                         PaintGallery(dc);   // [여급] 과 [도감] 이 격자를 공유한다

    UI_BufEnd(&buf);
    EndPaint(h, &ps);
}

// 펼친 목록 위치는 열 때 한 번만 재므로, 그 아래가 움직이는 일은 전부 닫고 시작한다.
static int  AnyDropOpen(void) { return g_drop != DROP_NONE; }
static void CloseDrops(void)  { g_drop = DROP_NONE; g_dropRow = -1; g_ddScroll = 0; }

static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    int wasOpen = AnyDropOpen();
    if (row < 0) row = 0;
    if (row > mx) row = mx;
    CloseDrops();
    // 끝까지 스크롤된 상태에서 또 굴려도, 목록을 닫았으면 다시 그려야 지워진다.
    if (row != g_scroll || wasOpen) { g_scroll = row; InvalidateRect(h, NULL, FALSE); }
}
static void SetGender(HWND h, int g)
{
    if (g==g_gender) return;
    g_gender = g;
    RebuildFilter(); InvalidateRect(h,NULL,FALSE);
}
static void SetCat(HWND h, int c)
{
    if (c==g_catFilter) return;
    g_catFilter = c;
    RebuildFilter(); InvalidateRect(h,NULL,FALSE);
}
// TradeUtilKR 의 창을 띄운다. 그쪽이 게임 창을 서브클래싱해 이 ID 를 가로챈다.
// 플러그인끼리 직접 부르지 않고 메시지로만 이어 두면 한쪽이 없어도 아무 일도 안 일어난다.
static BOOL PostToGame(UINT id)
{
    HWND game = g_wnd ? GetWindow(g_wnd, GW_OWNER) : NULL;   // 이 창은 게임 창을 주인으로 뜬다
    if (!game) return FALSE;
    PostMessageW(game, WM_COMMAND, MAKEWPARAM(id, 0), 0);
    return TRUE;
}

static void SetTab(HWND h, int t)
{
    if (t == TAB_SISE || t == TAB_GOODS) {          // 탭이 아니라 딴 창을 여는 단추다
        PostToGame(t == TAB_SISE ? ID_TRADE_SISE : ID_TRADE_GOODS);
        return;
    }
    if (t == g_tab) return;
    g_tab = t;
    CloseDrops();
    Nav_Activate(h, t == TAB_NAV);
    Quest_Activate(h, t == TAB_QUEST);   // 켤 때마다 세이브를 다시 읽는다
    Inv_Activate(h, t == TAB_INV);       // 켤 때마다 소지품 자리를 다시 잡는다
    if (t != TAB_NAV && t != TAB_QUEST && t != TAB_INV)
        RebuildFilter();                 // [여급] <-> [도감] 은 목록 내용이 아예 다르다
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK CharProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        Face_Load();
        g_maidsOk = Maid_Load();   // 실패하면 여급도 예전처럼 얼굴코드 단위 + char_info.h 로
        Patron_Load();             // 실패하면 스폰서 등장연도 상자를 아예 안 띄운다
        RebuildFilter();
#if CHARKR_SHOW_NAV_TAB
        Nav_Init(h, g_hinst);
#endif
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_COMMAND:
        if (Nav_Command(h, wp)) return 0;
        return DefWindowProcW(h, m, wp, lp);
    case WM_MOUSEWHEEL: {
        int notches = GET_WHEEL_DELTA_WPARAM(wp) / 120;
        // 226개짜리 도시 목록이 펼쳐져 있으면 휠은 갤러리가 아니라 그 목록을 굴린다.
        if (g_drop == DROP_CITY) {
            int mx = DropMaxScroll(g_drop), s = g_ddScroll - notches;
            if (s < 0) s = 0;
            if (s > mx) s = mx;
            if (s != g_ddScroll) { g_ddScroll = s; InvalidateRect(h, NULL, FALSE); }
            return 0;
        }
        if      (g_tab == TAB_NAV)   Nav_Wheel(h, notches);
        else if (g_tab == TAB_QUEST) Quest_Wheel(h, notches);
        else if (g_tab == TAB_INV)   Inv_Wheel(h, notches);
        else                         ScrollTo(h, g_scroll - notches);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt; RECT rc; pt.x=GET_X_LPARAM(lp); pt.y=GET_Y_LPARAM(lp);
        GetClientRect(h,&rc);
        // 이름 검색칸(EDIT)에 포커스가 남아 있으면 방향키 스크롤이 먹지 않는다.
        // 창 바닥을 누른 시점에 포커스를 되가져온다(자식 클릭은 여기로 오지 않는다).
        SetFocus(h);
        // 펼친 목록이 있으면 그쪽이 클릭을 먼저 먹는다. 연도/도시는 하나 고르면 닫히고,
        // 언어는 여러 개를 켰다 껐다 해야 하니 열어 둔 채로 토글만 한다(바깥 클릭 = 닫기).
        if (AnyDropOpen()) {
            int i = PanelHit(pt), kind = g_drop, row = g_dropRow;
            if (kind == DROP_LANG) {
                if (i >= 0) { Maid_ToggleLang(row, i); CharState_Save(g_hinst); }
                else        CloseDrops();
            } else {
                CloseDrops();
                if (i >= 0) {
                    // 여급·스폰서는 EXE 표를 고치는 것이라 다음 실행 때 되살릴 수 있다.
                    if      (kind == DROP_YEAR)  { Maid_SetYear(row, MAID_YEAR_MIN + i);        CharState_Save(g_hinst); }
                    else if (kind == DROP_CITY)  { Maid_SetCity(row, i);                        CharState_Save(g_hinst); }
                    else if (kind == DROP_PYEAR) { Patron_SetYear(row, PATRON_YEAR_MIN + i);    CharState_Save(g_hinst); }
                    else    { g_prefFilter = i; RebuildFilter(); }
                }
            }
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        { RECT cb=CloseRect(rc); if (PtInRect(&cb,pt)) { DestroyWindow(h); return 0; } }
        { int i; for (i = 0; i < TAB_N; i++) {
            RECT r = TabRectAt(i);
            if (PtInRect(&r, pt)) { SetTab(h, kTabs[i].id); return 0; } } }
        if (g_tab == TAB_NAV) {
            if (Nav_Click(h, pt)) return 0;
        } else if (g_tab == TAB_QUEST) {
            if (Quest_Click(h, pt)) return 0;
        } else if (g_tab == TAB_INV) {
            if (Inv_Click(h, pt)) return 0;
        } else {
            if (g_tab == TAB_GALLERY) {
                { RECT r=MaleRect();   if (PtInRect(&r,pt)) { SetGender(h,FACE_MALE); return 0; } }
                { RECT r=FemaleRect(); if (PtInRect(&r,pt)) { SetGender(h,FACE_FEMALE); return 0; } }
                { int bi; for (bi=0;bi<CAT_N;bi++){ RECT r=CatRect(bi);
                    if (PtInRect(&r,pt)) { SetCat(h,kCatBtn[bi].cat); return 0; } } }
            }
            if (g_tab == TAB_PATRON) {
                { RECT r=PrefRect();
                  if (PtInRect(&r,pt)) { OpenDrop(h, DROP_PREF, -1, r); return 0; } }
                { RECT r=SortRect();
                  if (PtInRect(&r,pt)) {   // 자금↓ -> 자금↑ -> 번호순 -> 자금↓
                      g_sponsorSort = (g_sponsorSort + 1) % SORT_N;
                      RebuildFilter(); InvalidateRect(h,NULL,FALSE); return 0; } }
#if CHARKR_EDIT_PATRON_YEAR
                // 스폰서 칸의 등장연도 상자
                { int cx, cy, gi = CellHit(pt, &cx, &cy);
                  if (gi >= 0 && g_filt[gi].patron >= 0) {
                      RECT r = MaidYearRect(cx, cy);
                      if (PtInRect(&r, pt)) { OpenDrop(h, DROP_PYEAR, g_filt[gi].patron, r); return 0; }
                  } }
#endif
            }
            { RECT sb=SbTrack(); if (PtInRect(&sb,pt)) {   // 트랙 클릭 = 페이지 이동
                int mid=(sb.top+sb.bottom)/2;
                ScrollTo(h, g_scroll + (pt.y<mid?-ROWS_VIS:ROWS_VIS)); return 0; } }
            // 여급 칸의 도시/생년 상자와 [언어 수정] 버튼
            { int cx, cy, gi = CellHit(pt, &cx, &cy);
              if (gi >= 0 && g_filt[gi].maid >= 0) {
                  int maid = g_filt[gi].maid;
                  RECT r;
#if CHARKR_EDIT_CITY
                  r = MaidCityRect(cx, cy);
                  if (PtInRect(&r, pt)) { OpenDrop(h, DROP_CITY, maid, r); return 0; }
#endif
#if CHARKR_EDIT_LANG
                  r = MaidLangRect(cx, cy);
                  if (PtInRect(&r, pt)) { OpenDrop(h, DROP_LANG, maid, r); return 0; }
#endif
                  r = MaidYearRect(cx, cy);
                  if (PtInRect(&r, pt)) { OpenDrop(h, DROP_YEAR, maid, r); return 0; }
              } }
        }
        if (pt.y < FRAME+TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        // 목록이 펼쳐져 있으면 ESC 는 창이 아니라 목록을 닫는다.
        if (AnyDropOpen() && wp == VK_ESCAPE) {
            CloseDrops(); InvalidateRect(h, NULL, FALSE); return 0;
        }
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        // TAB = 다음 탭으로 (표시 순서대로 순환)
        if (wp == VK_TAB) {
            int i; for (i = 0; i < TAB_N; i++) if (kTabs[i].id == g_tab) break;
            SetTab(h, kTabs[(i + 1) % TAB_N].id);
            return 0;
        }
        if (g_tab == TAB_NAV) {
            if (Nav_Key(h, wp)) return 0;
            return 0;
        }
        if (g_tab == TAB_QUEST) {
            Quest_Key(h, wp);
            return 0;
        }
        if (g_tab == TAB_INV) {
            Inv_Key(h, wp);
            return 0;
        }
        switch (wp) {
        case VK_UP:    ScrollTo(h, g_scroll-1); return 0;
        case VK_DOWN:  ScrollTo(h, g_scroll+1); return 0;
        case VK_PRIOR: ScrollTo(h, g_scroll-ROWS_VIS); return 0;
        case VK_NEXT:  ScrollTo(h, g_scroll+ROWS_VIS); return 0;
        case VK_HOME:  ScrollTo(h, 0); return 0;
        case VK_END:   ScrollTo(h, MaxScroll()); return 0;
        case 'M':      if (g_tab == TAB_GALLERY) SetGender(h,FACE_MALE);   return 0;
        case 'F':      if (g_tab == TAB_GALLERY) SetGender(h,FACE_FEMALE); return 0;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        Nav_Destroy();
        UI_DestroyFonts();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void CharKR_ShowWindow(HWND owner, HINSTANCE hinst)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT; RECT orc;
    g_hinst = hinst;
    if (g_wnd) { SetForegroundWindow(g_wnd); return; }
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = CharProc; wc.hInstance = hinst; wc.lpszClassName = WC_CHAR;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc); reg = TRUE;
    }
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right-orc.left)-WIN_W)/2;
        y = orc.top  + ((orc.bottom-orc.top)-WIN_H)/2;
        if (x < 0) x = 0; if (y < 0) y = 0;
    }
    g_wnd = CreateWindowExW(0, WC_CHAR, L"정보", WS_POPUP, x, y, WIN_W, WIN_H, owner, NULL, hinst, NULL);
    if (g_wnd) {
#if CHARKR_SHOW_NAV_TAB
        Nav_Activate(g_wnd, g_tab == TAB_NAV);
#endif
        Quest_Activate(g_wnd, g_tab == TAB_QUEST);   // 창을 닫았다 다시 열 때 탭이 유지된다
        Inv_Activate(g_wnd, g_tab == TAB_INV);
        ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd); SetFocus(g_wnd);
    }
}

// ---------------- 메뉴 통합 (서브클래싱) ----------------

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;
    if (msg == WM_COMMAND && LOWORD(wp) == ID_CHAR && HIWORD(wp) == 0) {
        CharKR_ShowWindow(h, g_hinst);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_gameHwnd = NULL;
        return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
    }
    return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0; (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_gameHwnd = h; return FALSE; }
    return TRUE;
}
static BOOL HasOurMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, L"정보") == 0) return TRUE;
    return FALSE;
}
// 최상위 메뉴바에서 "파일" 팝업을 찾아 그 서브메뉴 핸들을 돌려준다. 없으면 NULL.
// (KR 플러그인 3종이 각자 최상위에 버튼을 붙이던 것을 "파일" 드롭다운 안으로 모으기 위함.)
static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    // 실제 라벨은 "파일 (&F)" 처럼 니모닉이 붙으므로 접두어로 매칭한다.
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}
// "파일" 안에 KR 플러그인 항목(ID 0xB000~0xCFFF)이 이미 하나라도 있는지.
// 세 플러그인 중 가장 먼저 설치되는 쪽만 구분선을 넣도록 하는 판정에 쓴다(구분선 중복 방지).
static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
}
// 연속된 구분선을 1개로 접는다(변경했으면 TRUE). 세 플러그인 스레드가 동시에 폴링하며
// 각자 구분선을 넣는 race 로 구분선이 2~3개 생겨도, 다음 폴링에서 자동으로 하나로 수렴시킨다.
static BOOL CollapseSeparators(HMENU m)
{
    BOOL changed = FALSE; int i;
    for (i = GetMenuItemCount(m) - 1; i > 0; i--) {
        UINT a = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        UINT b = GetMenuState(m, (UINT)(i - 1), MF_BYPOSITION);
        if ((a & MF_SEPARATOR) && (b & MF_SEPARATOR)) {
            RemoveMenu(m, (UINT)i, MF_BYPOSITION);
            changed = TRUE;
        }
    }
    return changed;
}
static DWORD WINAPI MonitorThread(LPVOID param)
{
    (void)param;
    OutputDebugStringW(L"[CharacterUtilKR] monitor thread started.");
    // 지난번에 고친 여급·스폰서 값을 되살린다. 창을 열지 않아도 반영되도록 여기서 한다
    // (표를 읽으려면 얼굴 개수가 필요해 Face_Load 가 먼저다. DllMain 에서 파일을 읽지 않으려고
    //  이 스레드로 미뤘고, 게임이 그 표를 보는 시점보다는 한참 앞선다).
    Face_Load();
    Maid_Load();
    Patron_Load();
    CharState_Apply(g_hinst);
    // quests.json 대로 퀘스트 이벤트 파일을 다시 만들어 둔다. 게임이 그 파일을 읽기 전에
    // 끝나야 해서 창을 여는 시점이 아니라 여기서 한다(원본은 <이름>.CDS.orig 로 남는다).
    Quest_Init(g_hinst);
    for (;;) {
        HMENU bar;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd) {
            bar = GetMenu(g_gameHwnd);
            if (bar) {
                // "파일" 드롭다운이 있으면 그 안에, 없으면(라벨 불일치 대비) 예전처럼 최상위에 붙인다.
                HMENU fileMenu = FindFileMenu(bar);
                HMENU target = fileMenu ? fileMenu : bar;
                if (!HasOurMenu(target)) {
                    if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                        AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL); // 게임 원래 항목과 구분(최초 1회)
                    AppendMenuW(target, MF_STRING, ID_CHAR, L"정보");
                    DrawMenuBar(g_gameHwnd);
                    OutputDebugStringW(L"[CharacterUtilKR] 정보 menu (re)installed.");
                }
                if (fileMenu && CollapseSeparators(fileMenu)) DrawMenuBar(g_gameHwnd); // race 로 생긴 중복 구분선 정리
                if (g_subHwnd != g_gameHwnd) {
                    g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                    g_subHwnd = g_gameHwnd;
                    OutputDebugStringW(L"[CharacterUtilKR] window subclassed.");
                }
            }
        }
        Sleep(1000);
    }
}
void CharKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    OutputDebugStringW(L"[CharacterUtilKR] init.");
    t = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
