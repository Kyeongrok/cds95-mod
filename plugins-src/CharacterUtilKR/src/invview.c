#include "invview.h"
#include "inventory.h"
#include "ui.h"
#include "item_names.h"    // TradeUtilKR/src — kItemNames[286]

// 칸마다 한 줄. 소지 16칸을 먼저 늘어놓고 보관 99칸이 이어진다.
// 자식 컨트롤은 안 쓴다(게임 DirectDraw 화면 위에서 불안정) — 목록은 직접 그린다.

#define IV_ROW_H   24
#define IV_ROWS    18
#define IV_Y       (Q_Y + 30)                 // 위 한 줄은 소지금
#define IV_LIST_H  (IV_ROW_H * IV_ROWS)
#define IV_N       (INV_HELD_N + INV_STORE_N)

#define DD_ITEM_H  22
#define DD_COLS    4
#define DD_ROWS    12
#define DD_ITEM_W  150
#define DD_N       (INV_ITEM_N + 1)           // 맨 앞이 "(비움)"

static int  g_scroll = 0;
static int  g_open = -1;        // 아이템 목록을 펼친 칸 번호. -1 = 안 펼침
static int  g_ddScroll = 0;
static RECT g_ddRc;
static wchar_t g_msg[128] = L"";

static const wchar_t* ItemName(int id)
{
    return (id >= 0 && id < (int)(sizeof(kItemNames)/sizeof(kItemNames[0]))) ? kItemNames[id] : L"?";
}

// 목록 i번째 항목이 나타내는 아이템 번호. 0번은 "(비움)".
static int DropValue(int i) { return i - 1; }

static RECT RcReload(void)
{ RECT r; r.left=FRAME+8; r.right=r.left+66; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcInfo(void)
{ RECT r; r.left=FRAME+82; r.right=WIN_W-FRAME-8; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcMoney(void)
{ RECT r; r.left=Q_X+70; r.right=r.left+120; r.top=Q_Y+2; r.bottom=r.top+22; return r; }
// 소지금 [−−][−][+][++]
static RECT RcMoneyBtn(int k)
{
    static const int kx[4] = { 0, 46, 82, 118 };
    static const int kw[4] = { 42, 32, 32, 42 };
    RECT r;
    r.left = Q_X + 200 + kx[k]; r.right = r.left + kw[k];
    r.top = Q_Y + 2; r.bottom = r.top + 22;
    return r;
}
static RECT RcRow(int vis)
{ RECT r; r.left=Q_X; r.right=Q_X+Q_W; r.top=IV_Y+vis*IV_ROW_H; r.bottom=r.top+IV_ROW_H-2; return r; }
static RECT RcSlot(int vis)
{ RECT r; r.left=Q_X+120; r.right=r.left+240; r.top=IV_Y+vis*IV_ROW_H+1; r.bottom=r.top+20; return r; }
static RECT RcClear(int vis)
{ RECT r; r.left=Q_X+370; r.right=r.left+52; r.top=IV_Y+vis*IV_ROW_H+1; r.bottom=r.top+20; return r; }
static RECT RcTrack(void)
{ RECT r; r.right=WIN_W-FRAME-2; r.left=r.right-SB_W; r.top=IV_Y; r.bottom=IV_Y+IV_LIST_H; return r; }

static int MaxScroll(void) { int m = IV_N - IV_ROWS; return m > 0 ? m : 0; }
static int KindOf(int i)   { return i < INV_HELD_N ? INV_HELD : INV_STORE; }
static int IndexOf(int i)  { return i < INV_HELD_N ? i : i - INV_HELD_N; }

static void OpenDrop(int slot, RECT anchor)
{
    int w = DD_COLS * DD_ITEM_W, h = DD_ROWS * DD_ITEM_H;
    RECT p;
    int cur = Inv_Get(KindOf(slot), IndexOf(slot));
    p.left = anchor.left; p.right = p.left + w;
    p.top = anchor.bottom; p.bottom = p.top + h;
    if (p.right > WIN_W - FRAME)  { int d = p.right - (WIN_W - FRAME); p.left -= d; p.right -= d; }
    if (p.left < FRAME)           { p.left = FRAME; p.right = p.left + w; }
    if (p.bottom > WIN_H - FRAME) { p.top = anchor.top - h; p.bottom = p.top + h; }
    if (p.top < FRAME)            { p.top = FRAME; p.bottom = p.top + h; }
    g_ddRc = p;
    g_open = slot;
    {   // 지금 든 아이템이 보이는 자리에서 펼친다
        int maxs = (DD_N + DD_COLS - 1) / DD_COLS - DD_ROWS;
        int row = (cur + 1) / DD_COLS;
        g_ddScroll = (cur >= 0 && row >= DD_ROWS) ? row - DD_ROWS / 2 : 0;
        if (maxs < 0) maxs = 0;
        if (g_ddScroll > maxs) g_ddScroll = maxs;
        if (g_ddScroll < 0) g_ddScroll = 0;
    }
}

static void PaintDrop(HDC dc)
{
    int r, c, cur;
    HBRUSH br;
    if (g_open < 0) return;
    cur = Inv_Get(KindOf(g_open), IndexOf(g_open));
    br = CreateSolidBrush(COL_FACE_TOP); FillRect(dc, &g_ddRc, br); DeleteObject(br);
    UI_Bevel(dc, g_ddRc, FALSE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &g_ddRc, br); DeleteObject(br);

    for (r = 0; r < DD_ROWS; r++) for (c = 0; c < DD_COLS; c++) {
        int i = (g_ddScroll + r) * DD_COLS + c, v;
        RECT ir;
        wchar_t t[80];
        if (i >= DD_N) continue;
        v = DropValue(i);
        ir.left = g_ddRc.left + c * DD_ITEM_W; ir.right = ir.left + DD_ITEM_W;
        ir.top = g_ddRc.top + r * DD_ITEM_H;   ir.bottom = ir.top + DD_ITEM_H;
        if (v == cur) { br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &ir, br); DeleteObject(br); }
        if (v < 0) lstrcpyW(t, L"(비움)");
        else       wsprintfW(t, L"%d %s", v, ItemName(v));
        ir.left += 4;
        UI_Text(dc, ir, t, g_smallFont, v == cur ? COL_LIGHT : COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    }
}

static int DropHit(POINT pt)
{
    int c, r, i;
    if (g_open < 0 || !PtInRect(&g_ddRc, pt)) return -1;
    c = (pt.x - g_ddRc.left) / DD_ITEM_W; if (c >= DD_COLS) c = DD_COLS - 1;
    r = (pt.y - g_ddRc.top) / DD_ITEM_H;
    i = (g_ddScroll + r) * DD_COLS + c;
    return (i >= 0 && i < DD_N) ? i : -1;
}

void Inv_Paint(HDC dc)
{
    wchar_t buf[160];
    int v;

    UI_Button(dc, RcReload(), L"새로고침", FALSE);

    if (!Inv_Ready()) {
        RECT e;
        const wchar_t* why;
        switch (Inv_Status()) {
        case INV_E_EMPTY: why = L"아직 세이브를 불러오지 않았습니다. 게임을 진행한 뒤 새로고침하세요."; break;
        case INV_E_RANGE: why = L"소지품 자리를 찾지 못했습니다(다른 버전의 실행 파일인 듯합니다)."; break;
        case INV_E_MONEY: why = L"소지금이 말이 안 됩니다 — 자리가 어긋난 듯합니다."; break;
        default:          why = L"소지품 자리를 읽지 못했습니다."; break;
        }
        e.left = Q_X; e.right = Q_X + Q_W; e.top = Q_Y + 40; e.bottom = e.top + 40;
        UI_Text(dc, e, why, g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        return;
    }

    wsprintfW(buf, L"소지 %d/%d칸 · 보관 %d/%d칸 · 값은 게임 메모리에 바로 들어갑니다",
              Inv_Used(INV_HELD), INV_HELD_N, Inv_Used(INV_STORE), INV_STORE_N);
    UI_Text(dc, RcInfo(), buf, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    // 소지금
    { RECT l; l.left = Q_X; l.right = Q_X + 62; l.top = Q_Y + 2; l.bottom = l.top + 22;
      UI_Text(dc, l, L"소지금", g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
    v = Inv_Money();
    { RECT m = RcMoney();
      UI_Bevel(dc, m, TRUE);
      wsprintfW(buf, L"%d", v);
      UI_Text(dc, m, buf, g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
    UI_Button(dc, RcMoneyBtn(0), L"◀◀", FALSE);
    UI_Button(dc, RcMoneyBtn(1), L"◀",  FALSE);
    UI_Button(dc, RcMoneyBtn(2), L"▶",  FALSE);
    UI_Button(dc, RcMoneyBtn(3), L"▶▶", FALSE);
    if (g_msg[0]) {
        RECT m; m.left = Q_X + 350; m.right = Q_X + Q_W; m.top = Q_Y + 2; m.bottom = m.top + 22;
        UI_Text(dc, m, g_msg, g_smallFont, COL_WARN_TX, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    // 칸 목록
    { int r;
      for (r = 0; r < IV_ROWS; r++) {
        int i = g_scroll + r, item;
        RECT row, lbl;
        wchar_t name[80];
        HBRUSH br;
        if (i >= IV_N) break;
        row = RcRow(r);
        if (i & 1) { br = CreateSolidBrush(COL_ROW_ALT); FillRect(dc, &row, br); DeleteObject(br); }

        lbl = row; lbl.left += 6; lbl.right = lbl.left + 106;
        if (KindOf(i) == INV_HELD) wsprintfW(name, L"소지 %d", IndexOf(i) + 1);
        else                       wsprintfW(name, L"보관 %d", IndexOf(i) + 1);
        UI_Text(dc, lbl, name, g_font,
                KindOf(i) == INV_HELD ? COL_TEXT : COL_DARK,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        item = Inv_Get(KindOf(i), IndexOf(i));
        if (item < 0) lstrcpyW(name, L"(비움)");
        else          wsprintfW(name, L"%d  %s", item, ItemName(item));
        UI_Select(dc, RcSlot(r), name, g_open == i);
        if (item >= 0) UI_Button(dc, RcClear(r), L"비우기", FALSE);
      } }

    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), IV_ROWS, IV_N);
    PaintDrop(dc);
}

static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    if (row < 0) row = 0;
    if (row > mx) row = mx;
    if (row != g_scroll) { g_scroll = row; g_open = -1; InvalidateRect(h, NULL, FALSE); }
}

void Inv_Activate(HWND h, int active)
{
    if (!active) return;
    // 켤 때마다 다시 잡는다 — 세이브를 나중에 불러왔을 수 있다.
    Inv_Load();
    g_open = -1; g_msg[0] = 0;
    if (g_scroll > MaxScroll()) g_scroll = MaxScroll();
    if (h) InvalidateRect(h, NULL, FALSE);
}

int Inv_Click(HWND h, POINT pt)
{
    RECT r;
    int i;

    // 펼친 목록이 먼저 먹는다. 하나 고르면 닫힌다.
    if (g_open >= 0) {
        int k = DropHit(pt), slot = g_open;
        g_open = -1;
        if (k >= 0) {
            Inv_Set(KindOf(slot), IndexOf(slot), DropValue(k));
            g_msg[0] = 0;
        }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    r = RcReload();
    if (PtInRect(&r, pt)) { Inv_Activate(h, 1); return 1; }
    if (!Inv_Ready()) return 0;

    // 소지금 ±  (작은 단위 1000, 큰 단위 100000)
    for (i = 0; i < 4; i++) {
        static const int kStep[4] = { -100000, -1000, 1000, 100000 };
        r = RcMoneyBtn(i);
        if (!PtInRect(&r, pt)) continue;
        Inv_SetMoney(Inv_Money() + kStep[i]);
        g_msg[0] = 0;
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    r = RcTrack();
    if (PtInRect(&r, pt)) {
        int mid = (r.top + r.bottom) / 2;
        ScrollTo(h, g_scroll + (pt.y < mid ? -IV_ROWS : IV_ROWS));
        return 1;
    }

    { int row;
      for (row = 0; row < IV_ROWS; row++) {
        int slot = g_scroll + row;
        if (slot >= IV_N) break;
        r = RcSlot(row);
        if (PtInRect(&r, pt)) { OpenDrop(slot, r); InvalidateRect(h, NULL, FALSE); return 1; }
        r = RcClear(row);
        if (PtInRect(&r, pt) && Inv_Get(KindOf(slot), IndexOf(slot)) >= 0) {
            Inv_Set(KindOf(slot), IndexOf(slot), -1);
            InvalidateRect(h, NULL, FALSE);
            return 1;
        }
      } }
    return 0;
}

int Inv_Key(HWND h, WPARAM wp)
{
    if (wp == VK_ESCAPE && g_open >= 0) { g_open = -1; InvalidateRect(h, NULL, FALSE); return 1; }
    switch (wp) {
    case VK_UP:    ScrollTo(h, g_scroll - 1); return 1;
    case VK_DOWN:  ScrollTo(h, g_scroll + 1); return 1;
    case VK_PRIOR: ScrollTo(h, g_scroll - IV_ROWS); return 1;
    case VK_NEXT:  ScrollTo(h, g_scroll + IV_ROWS); return 1;
    case VK_HOME:  ScrollTo(h, 0); return 1;
    case VK_END:   ScrollTo(h, MaxScroll()); return 1;
    case 'R':      Inv_Activate(h, 1); return 1;
    }
    return 0;
}

void Inv_Wheel(HWND h, int notches)
{
    if (g_open >= 0) {              // 286개짜리 목록은 휠로 굴린다
        int maxs = (DD_N + DD_COLS - 1) / DD_COLS - DD_ROWS, s;
        if (maxs < 0) maxs = 0;
        s = g_ddScroll - notches;
        if (s < 0) s = 0;
        if (s > maxs) s = maxs;
        if (s != g_ddScroll) { g_ddScroll = s; InvalidateRect(h, NULL, FALSE); }
        return;
    }
    ScrollTo(h, g_scroll - notches);
}
