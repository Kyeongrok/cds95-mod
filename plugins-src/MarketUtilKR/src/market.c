#include <windows.h>
#include <windowsx.h>
#include "market.h"
#include "marketdb.h"
#include "itempic.h"   // CharacterUtilKR/src — ITEM.CDS 그림
#include "uikit.h"     // CharacterUtilKR/src — 세피아 색표와 위젯

// MarketUtilKR — 교역소 매매.
//   왼쪽: 지금 도시가 파는 것 (그림 + 이름 + 단가 + 공급량 + 담은 수량)
//   오른쪽: 내 짐 (그림 + 갯수 + 원산지 + 여기 단가)
//
// 게임 구입창과 같은 방식이다 — 담아 두었다가 [결정] 을 눌러야 실제로 산다.
//   · 줄을 두 번 누르면 "총량의 절반" 을 담는다. 두 번 하면 전량이 된다
//   · [-] [+] 는 아래에서 고른 단위(1 / 10 / 100 / 절반)만큼 담고 뺀다
//   · [결정] 을 누를 때 소지금 · 재고 · 짐을 고친다(세이브 파일은 그대로)

#define ID_MARKET_OPEN 0xBD00u   // Trade=0xB10x, Char=0xB301/0xB310+, Ship=0xB410, Patch=0xB500,
                                 // Map=0xB600, Mod=0xB700, QMod=0xB800, Upd=0xB900,
                                 // Fatigue=0xBA00, Hotkey=0xBB00, Hint=0xBC00 과 안 겹치게.

#define WC_MARKET  L"MarketUtilKR_Window"
#define PIC        56                    // 줄 그림
#define ROW_H      62
#define ROWS_VIS   7
#define COL_W      420
#define LIST_Y     (FRAME + TITLE_H + 30)
#define LIST_H     (ROW_H * ROWS_VIS)
#define LEFT_X     (FRAME + 8)
#define RIGHT_X    (LEFT_X + COL_W + 12)
#define CLIENT_W   (RIGHT_X + COL_W + FRAME + 8)
#define CLIENT_H   (LIST_Y + LIST_H + 114)

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

#define MKT_ROWS_MAX 16
#define STEP_N 4
static const wchar_t* kStepName[STEP_N] = { L"1", L"10", L"100", L"절반" };
static const int      kStepVal[STEP_N]  = { 1, 10, 100, -1 };   // -1 = 총량의 절반

static int g_city = -1;
static int g_rows = 0, g_cargo = 0;
static int g_qty[MKT_ROWS_MAX];          // 왼쪽 줄마다 담은 수량(살 것)
static int g_sell[32];                   // 오른쪽 줄마다 담은 수량(팔 것)
static int g_step = 3;                   // 기본은 절반
static int g_spent = 0;                  // 이 창을 연 뒤 쓴 돈
static wchar_t g_msg[160] = L"";
static int g_msgWarn = 0;

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

static RECT RcRow(int side, int v)       // side 0 = 왼쪽(살 것), 1 = 오른쪽(내 짐)
{
    RECT r;
    r.left  = side ? RIGHT_X : LEFT_X;
    r.right = r.left + COL_W;
    r.top   = LIST_Y + v * ROW_H;
    r.bottom = r.top + ROW_H - 4;
    return r;
}

static void Say(const wchar_t* s, int warn) { lstrcpynW(g_msg, s, 160); g_msgWarn = warn; }

static RECT RcMinus(int v)
{ RECT r = RcRow(0, v); RECT b; b.right = r.right - 92; b.left = b.right - 26;
  b.top = r.top + 18; b.bottom = b.top + 24; return b; }
static RECT RcPlus(int v)
{ RECT r = RcRow(0, v); RECT b; b.right = r.right - 8; b.left = b.right - 26;
  b.top = r.top + 18; b.bottom = b.top + 24; return b; }
static RECT RcMinus2(int v)
{ RECT r = RcRow(1, v); RECT b; b.right = r.right - 92; b.left = b.right - 26;
  b.top = r.top + 18; b.bottom = b.top + 24; return b; }
static RECT RcPlus2(int v)
{ RECT r = RcRow(1, v); RECT b; b.right = r.right - 8; b.left = b.right - 26;
  b.top = r.top + 18; b.bottom = b.top + 24; return b; }
static RECT RcStep(int i)
{ RECT r; r.left = LEFT_X + 92 + i * 46; r.right = r.left + 42;
  r.top = LIST_Y + LIST_H + 54; r.bottom = r.top + 24; return r; }
static RECT RcApply(void)
{ RECT r; r.right = RIGHT_X + COL_W; r.left = r.right - 90;
  r.top = LIST_Y + LIST_H + 54; r.bottom = r.top + 26; return r; }
static RECT RcClear(void)
{ RECT r; r.right = RIGHT_X + COL_W - 98; r.left = r.right - 90;
  r.top = LIST_Y + LIST_H + 54; r.bottom = r.top + 26; return r; }

// 담은 것의 총액.
static int CartCost(void)
{
    int i, sum = 0;
    for (i = 0; i < g_rows && i < MKT_ROWS_MAX; i++) {
        const MktRow* w = Mkt_At(i);
        if (w && g_qty[i] > 0) sum += g_qty[i] * w->price;
    }
    return sum;
}
static void CartClear(void)
{
    int i;
    for (i = 0; i < MKT_ROWS_MAX; i++) g_qty[i] = 0;
    for (i = 0; i < 32; i++) g_sell[i] = 0;
}

// 매각 담기 — 짐 칸 하나에서 뺄 수량.
static void SellAdd(int v, int n)
{
    const MktCargo* c = Mkt_CargoAt(v);
    if (!c || v < 0 || v >= 32) return;
    g_sell[v] += n;
    if (g_sell[v] > c->count) g_sell[v] = c->count;
    if (g_sell[v] < 0) g_sell[v] = 0;
}
static int SellStepOf(int v)
{
    const MktCargo* c = Mkt_CargoAt(v);
    if (kStepVal[g_step] > 0) return kStepVal[g_step];
    return c ? (c->count + 1) / 2 : 1;
}
// 왼쪽에서 담은 것 중 이 교역품 · 이 원산지에 해당하는 수량. 오른쪽에 미리 비춰 준다.
static int PendingFor(int kind, int origin)
{
    int i, sum = 0;
    for (i = 0; i < g_rows && i < MKT_ROWS_MAX; i++) {
        const MktRow* w = Mkt_At(i);
        if (w && g_qty[i] > 0 && w->kind == kind && w->origin == origin) sum += g_qty[i];
    }
    return sum;
}

static int SellGain(void)
{
    int i, sum = 0;
    for (i = 0; i < g_cargo && i < 32; i++) {
        const MktCargo* c = Mkt_CargoAt(i);
        if (c && g_sell[i] > 0) {
            int pr = Mkt_SellPrice(g_city, c->kind);
            if (pr > 0) sum += g_sell[i] * pr;
        }
    }
    return sum;
}

// 그 줄에 n 만큼 담는다(음수면 뺀다). 총량을 넘지 않고 0 밑으로도 안 간다.
static void CartAdd(int v, int n)
{
    const MktRow* w = Mkt_At(v);
    if (!w || v < 0 || v >= MKT_ROWS_MAX) return;
    g_qty[v] += n;
    if (g_qty[v] > w->supply) g_qty[v] = w->supply;
    if (g_qty[v] < 0) g_qty[v] = 0;
}
static int StepOf(int v)
{
    const MktRow* w = Mkt_At(v);
    if (kStepVal[g_step] > 0) return kStepVal[g_step];
    return w ? (w->supply + 1) / 2 : 1;      // 절반 — 두 번이면 전량이 되게 올림
}

static void Reload(HWND h)
{
    g_city = Mkt_CurrentCity();
    g_rows = (g_city >= 0) ? Mkt_BuildList(g_city) : 0;
    if (g_rows > MKT_ROWS_MAX) g_rows = MKT_ROWS_MAX;
    g_cargo = Mkt_LoadCargo();
    if (h) InvalidateRect(h, NULL, FALSE);
}

// 한 줄 그리기. 왼쪽은 살 것, 오른쪽은 내 짐.
static void PaintRow(HDC dc, int side, int v, int kind, int a, int b, int origin)
{
    RECT r = RcRow(side, v), t, box;
    HBRUSH br;
    wchar_t buf[96];

    if (v & 1) { br = CreateSolidBrush(COL_ROW_ALT); FillRect(dc, &r, br); DeleteObject(br); }

    box.left = r.left + 4; box.top = r.top + 2;
    box.right = box.left + PIC; box.bottom = box.top + PIC;
    if (!ItemPic_Draw(dc, box.left, box.top, PIC, PIC, Mkt_GoodsPic(kind))) {
        br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &box, br); DeleteObject(br);
        UI_Bevel(dc, box, TRUE);
    }
    { RECT f = box; InflateRect(&f, 1, 1);
      br = CreateSolidBrush(COL_DARK); FrameRect(dc, &f, br); DeleteObject(br); }

    t = r; t.left = box.right + 12; t.right = r.right - 128;
    t.top = r.top + 4; t.bottom = t.top + 22;
    UI_Text(dc, t, Mkt_GoodsName(kind), g_font, COL_TEXT,
            DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    t.top = t.bottom; t.bottom = t.top + 18;
    if (side == 0) {
        wsprintfW(buf, L"단가 %d닢 · 공급 %d", a, b);
        UI_Text(dc, t, buf, g_smallFont, COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    else {
        // 매각가와 원산지 매입가를 나란히 — 얼마 남고 얼마 밑지는지 바로 보이게.
        int buyp = Mkt_BuyPriceAt(origin, kind), diff = (buyp > 0) ? b - buyp : 0;
        if (a > 0 && buyp > 0)
            wsprintfW(buf, L"%d개 · 매각 %d닢 · 매입 %d닢 (%s%d)", a, b, buyp,
                      diff >= 0 ? L"+" : L"−", diff >= 0 ? diff : -diff);
        else if (a > 0) wsprintfW(buf, L"%d개 · 매각 %d닢", a, b);
        else            wsprintfW(buf, L"아직 없음 · 매각 %d닢", b > 0 ? b : 0);
        UI_Text(dc, t, buf, g_smallFont,
                (a > 0 && buyp > 0) ? (diff >= 0 ? COL_LANG_TX : COL_WARN_TX) : COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }

    t.top = t.bottom; t.bottom = t.top + 18;
    if (side == 0) {
        int q = (v < MKT_ROWS_MAX) ? g_qty[v] : 0;
        if (q > 0) wsprintfW(buf, L"%s산 · 담은 것 %d개 (%d닢)", Mkt_CityName(origin), q, q * a);
        else       wsprintfW(buf, L"%s산 · 두 번 누르면 %d개 담김", Mkt_CityName(origin), (b + 1) / 2);
        UI_Text(dc, t, buf, g_smallFont, q > 0 ? COL_WARN_TX : COL_DARK,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        UI_Button(dc, RcMinus(v), L"−", FALSE);
        UI_Button(dc, RcPlus(v),  L"+", FALSE);
    } else {
        int q = (v < 32) ? g_sell[v] : 0;
        int pend = PendingFor(kind, origin);
        if (q > 0)         wsprintfW(buf, L"%s산 · 팔 것 %d개 (%d닢)", Mkt_CityName(origin), q, q * b);
        else if (pend > 0) wsprintfW(buf, L"%s산 · 담는 중 +%d개", Mkt_CityName(origin), pend);
        else               wsprintfW(buf, L"%s산 · 두 번 누르면 %d개 담김",
                                     origin >= 0 ? Mkt_CityName(origin) : L"?", (a + 1) / 2);
        UI_Text(dc, t, buf, g_smallFont, (q > 0 || pend > 0) ? COL_WARN_TX : COL_DARK,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if (a > 0) {     // 아직 없는 물건(담는 중만 있는 줄)에는 파는 단추를 안 붙인다
            UI_Button(dc, RcMinus2(v), L"−", FALSE);
            UI_Button(dc, RcPlus2(v),  L"+", FALSE);
        }
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
    int i, money = Mkt_Money();
    wchar_t buf[160];

    GetClientRect(h, &rc);
    dc = UI_BufBegin(&b, hdc, rc.right, rc.bottom);
    br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
    if (g_city >= 0) wsprintfW(buf, L"매매 — %s", Mkt_CityName(g_city));
    else             lstrcpyW(buf, L"매매");
    UI_WindowFrame(dc, rc, buf, &cb);

    r.left = LEFT_X; r.right = LEFT_X + COL_W; r.top = FRAME + TITLE_H + 4; r.bottom = r.top + 22;
    UI_Text(dc, r, L"이 도시가 파는 것", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    r.left = RIGHT_X; r.right = RIGHT_X + COL_W;
    UI_Text(dc, r, L"내 짐", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    { RECT p; p.left = LEFT_X; p.right = LEFT_X + COL_W; p.top = LIST_Y; p.bottom = LIST_Y + LIST_H;
      br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
      for (i = 0; i < ROWS_VIS && i < g_rows; i++) {
          const MktRow* w = Mkt_At(i);
          if (w) PaintRow(dc, 0, i, w->kind, w->price, w->supply, w->origin);
      }
      br = CreateSolidBrush(COL_DARK); FrameRect(dc, &p, br); DeleteObject(br);
      if (g_city < 0)
          UI_Text(dc, p, L"항해 중입니다 — 도시에 정박해야 합니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
      else if (!g_rows)
          UI_Text(dc, p, L"이 도시에는 교역소가 없거나 파는 것이 없습니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }

    { RECT p; p.left = RIGHT_X; p.right = RIGHT_X + COL_W; p.top = LIST_Y; p.bottom = LIST_Y + LIST_H;
      br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
      { int v = 0;
        for (i = 0; i < g_cargo && v < ROWS_VIS; i++) {
            const MktCargo* c = Mkt_CargoAt(i);
            int here;
            if (!c) continue;
            here = Mkt_SellPrice(g_city, c->kind);
            PaintRow(dc, 1, v++, c->kind, c->count, here > 0 ? here : 0, c->origin);
        }
        // 짐에 아직 없는데 담아 둔 것 — 살 것이 어디에 얼마나 얹히는지 미리 보인다.
        for (i = 0; i < g_rows && i < MKT_ROWS_MAX && v < ROWS_VIS; i++) {
            const MktRow* w = Mkt_At(i);
            int j, have = 0;
            if (!w || g_qty[i] <= 0) continue;
            for (j = 0; j < g_cargo; j++) {
                const MktCargo* c = Mkt_CargoAt(j);
                if (c && c->kind == w->kind && c->origin == w->origin) { have = 1; break; }
            }
            if (have) continue;
            PaintRow(dc, 1, v++, w->kind, 0, Mkt_SellPrice(g_city, w->kind), w->origin);
        } }
      br = CreateSolidBrush(COL_DARK); FrameRect(dc, &p, br); DeleteObject(br);
      if (!g_cargo) {
          int raw[4];
          Mkt_CargoRaw(0, raw);
          if (raw[0] < 0 || raw[1] <= 0) {        // 빈 칸 모양(-1 0 -1 0) — 정말 비었다
              UI_Text(dc, p, L"실은 것이 없습니다.", g_font, COL_TEXT,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
          } else {
              // 뭔가 들어 있는데 못 읽었다 — 그 값을 보여 줘야 자리를 고칠 수 있다.
              RECT d = p; int k;
              d.top += 20; d.bottom = d.top + 22;
              UI_Text(dc, d, L"짐을 못 읽었습니다 — 아래 값을 알려 주세요.", g_font, COL_WARN_TX,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
              for (k = 0; k < 5; k++) {
                  d.top = d.bottom; d.bottom = d.top + 18;
                  Mkt_CargoRaw(k, raw);
                  wsprintfW(buf, L"짐 %d칸: %d %d %d %d", k, raw[0], raw[1], raw[2], raw[3]);
                  UI_Text(dc, d, buf, g_smallFont, COL_DARK,
                          DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
              }
          }
      } }

    r.left = LEFT_X; r.right = CLIENT_W - FRAME - 8;
    r.top = LIST_Y + LIST_H + 6; r.bottom = r.top + 22;
    if (money >= 0) wsprintfW(buf, L"소지금 %d닢 · 이 창에서 쓴 돈 %d닢", money, g_spent);
    else            lstrcpyW(buf, L"소지금을 읽지 못했습니다 — 세이브를 불러온 뒤에 열어 주세요.");
    UI_Text(dc, r, buf, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    r.top = r.bottom; r.bottom = r.top + 20;
    UI_Text(dc, r, g_msg[0] ? g_msg
                            : L"줄을 두 번 누르면 총량의 절반이 담깁니다(두 번이면 전량). [결정] 에 사고 팝니다. 매각가는 어림값입니다.",
            g_smallFont, g_msgWarn ? COL_WARN_TX : COL_DARK,
            DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    {   // 담기 단위 · 구입소계 · 결정
        int cost = CartCost(), k;
        RECT lb;
        lb.left = LEFT_X; lb.right = LEFT_X + 84;
        lb.top = LIST_Y + LIST_H + 54; lb.bottom = lb.top + 24;
        UI_Text(dc, lb, L"담기 단위", g_font, COL_TEXT,
                DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        for (k = 0; k < STEP_N; k++) UI_Button(dc, RcStep(k), kStepName[k], g_step == k);

        lb.left = RcStep(STEP_N - 1).right + 16; lb.right = RcClear().left - 12;
        { int gain = SellGain();
          wsprintfW(buf, L"지출 %d닢 · 수입 %d닢", cost, gain);
          UI_Text(dc, lb, buf, g_font, (cost || gain) ? COL_WARN_TX : COL_TEXT,
                  DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
        UI_Button(dc, RcClear(), L"비우기", FALSE);
        UI_Button(dc, RcApply(), L"결정", (cost > 0 || SellGain() > 0));
    }

    UI_BufEnd(&b);
    EndPaint(h, &ps);
}

// 담아 둔 것을 실제로 산다. 한 줄이라도 실패하면 그 줄에서 멈추고 무엇이 문제인지 적는다
// (이미 산 줄은 그대로 둔다 — 소지금과 짐은 줄 단위로 맞아 있다).
static void CartApply(HWND h)
{
    int i, total = 0, bought = 0;
    (void)0;
    wchar_t buf[160];

    // 먼저 판다 — 번 돈으로 이어서 살 수 있게. 뒤에서부터 팔아야 칸이 당겨져도 번호가 안 어긋난다.
    for (i = (g_cargo < 32 ? g_cargo : 32) - 1; i >= 0; i--) {
        int got;
        if (g_sell[i] <= 0) continue;
        got = Mkt_Sell(g_city, i, g_sell[i]);
        if (got > 0) { total -= got; bought += g_sell[i]; }
    }
    Mkt_LoadCargo();          // 칸이 당겨졌을 수 있다

    for (i = 0; i < g_rows && i < MKT_ROWS_MAX; i++) {
        const MktRow* w = Mkt_At(i);
        int got;
        if (!w || g_qty[i] <= 0) continue;
        got = Mkt_Buy(g_city, i, g_qty[i]);
        if (got < 0) {
            const wchar_t* why =
                got == MKT_E_MONEY  ? L"소지금이 모자랍니다" :
                got == MKT_E_FULL   ? L"짐 칸이 없습니다" :
                got == MKT_E_SUPPLY ? L"공급량이 모자랍니다" : L"자리를 읽지 못했습니다";
            wsprintfW(buf, L"%s 에서 멈췄습니다 — %s. (%d개 %d닢까지 샀습니다)",
                      Mkt_GoodsName(w->kind), why, bought, total);
            Say(buf, 1);
            CartClear();
            Reload(h);
            return;
        }
        total += got; bought += g_qty[i];
    }
    if (!bought) { Say(L"담은 것이 없습니다.", 1); InvalidateRect(h, NULL, FALSE); return; }
    g_spent += total;
    if (total >= 0) wsprintfW(buf, L"%d개 · 지출 %d닢.", bought, total);
    else            wsprintfW(buf, L"%d개 · %d닢 남았습니다.", bought, -total);
    Say(buf, 0);
    CartClear();
    Reload(h);
}

static LRESULT CALLBACK MarketProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        ItemPic_Load();
        g_msg[0] = 0; g_spent = 0;
        CartClear();
        Reload(NULL);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(h); return 0;
    case WM_LBUTTONDBLCLK:
    {
        POINT pt; int v;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        for (v = 0; v < ROWS_VIS && v < g_rows; v++) {
            RECT r = RcRow(0, v);
            const MktRow* w;
            if (!PtInRect(&r, pt)) continue;
            w = Mkt_At(v);
            if (w) CartAdd(v, (w->supply + 1) / 2);   // 총량의 절반 — 두 번이면 전량
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        for (v = 0; v < ROWS_VIS && v < g_cargo; v++) {
            RECT r = RcRow(1, v);
            const MktCargo* c;
            if (!PtInRect(&r, pt)) continue;
            c = Mkt_CargoAt(v);
            if (c) SellAdd(v, (c->count + 1) / 2);
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        GetClientRect(h, &rc);
        cb.right = rc.right - FRAME - 4; cb.left = cb.right - 22;
        cb.top = FRAME + 4; cb.bottom = cb.top + 18;
        if (PtInRect(&cb, pt)) { ShowWindow(h, SW_HIDE); return 0; }
        {   int v;
            for (v = 0; v < ROWS_VIS && v < g_rows; v++) {
                RECT a = RcMinus(v), b2 = RcPlus(v);
                if (PtInRect(&a, pt))  { CartAdd(v, -StepOf(v)); InvalidateRect(h, NULL, FALSE); return 0; }
                if (PtInRect(&b2, pt)) { CartAdd(v,  StepOf(v)); InvalidateRect(h, NULL, FALSE); return 0; }
            }
            for (v = 0; v < ROWS_VIS && v < g_cargo; v++) {
                RECT a = RcMinus2(v), b2 = RcPlus2(v);
                if (PtInRect(&a, pt))  { SellAdd(v, -SellStepOf(v)); InvalidateRect(h, NULL, FALSE); return 0; }
                if (PtInRect(&b2, pt)) { SellAdd(v,  SellStepOf(v)); InvalidateRect(h, NULL, FALSE); return 0; }
            }
            for (v = 0; v < STEP_N; v++) {
                RECT s = RcStep(v);
                if (PtInRect(&s, pt)) { g_step = v; InvalidateRect(h, NULL, FALSE); return 0; }
            }
        }
        { RECT a = RcApply(); if (PtInRect(&a, pt)) { CartApply(h); return 0; } }
        { RECT c = RcClear(); if (PtInRect(&c, pt)) { CartClear(); Say(L"담은 것을 비웠습니다.", 0);
                                                     InvalidateRect(h, NULL, FALSE); return 0; } }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_RETURN) { CartApply(h); return 0; }
        if (w == VK_F5)     { Say(L"", 0); CartClear(); Reload(h); return 0; }
        if (w == VK_ESCAPE) { ShowWindow(h, SW_HIDE); return 0; }
        return 0;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY:
        UI_DestroyFonts();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowMarketWindow(void)
{
    static BOOL reg = FALSE;
    RECT orc;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;

    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = MarketProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.style = CS_DBLCLKS;              // 더블클릭을 받으려면 이게 있어야 한다
            wc.lpszClassName = WC_MARKET;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        if (g_gameHwnd && GetWindowRect(g_gameHwnd, &orc)) {
            x = orc.left + ((orc.right - orc.left) - CLIENT_W) / 2;
            y = orc.top  + ((orc.bottom - orc.top) - CLIENT_H) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
        }
        g_wnd = CreateWindowExW(0, WC_MARKET, L"매매", WS_POPUP,
                    x, y, CLIENT_W, CLIENT_H, g_gameHwnd, NULL, g_hinst, NULL);
    } else {
        CartClear();
        Reload(g_wnd);
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
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_MARKET_OPEN) {
        ShowMarketWindow();
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
    LogW(L"[MarketUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_MARKET_OPEN)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(target, MF_STRING, ID_MARKET_OPEN, L"매매");
                DrawMenuBar(g_gameHwnd);
                LogW(L"[MarketUtilKR] \"매매\" 메뉴 설치.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
            }
        }
        Sleep(1000);
    }
}

void MarketKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    LogW(L"[MarketUtilKR] init.");
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
