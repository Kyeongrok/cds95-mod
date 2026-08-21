#include <windows.h>
#include <windowsx.h>
#include "market.h"
#include "marketdb.h"
#include "itempic.h"   // CharacterUtilKR/src — ITEM.CDS 그림
#include "uikit.h"     // CharacterUtilKR/src — 세피아 색표와 위젯
#include "gameskin.h"  // ButtonMakerKR/src — 단추를 게임 껍데기(MISC.CDS 파트 4)로 그린다
#include "band.h"      // ButtonMakerKR/src — 띠 폭 규칙(16 + 8n + 16)

// MarketUtilKR — 교역소 매매.
//   왼쪽: 지금 도시가 파는 것 (그림 + 이름 + 단가 + 공급량 + 담은 수량)
//   오른쪽: 내 짐 (그림 + 갯수 + 원산지 + 여기 단가)
//
// 게임 구입창과 같은 방식이다 — 담아 두었다가 [결정] 을 눌러야 실제로 산다.
//   · 줄을 두 번 누르면 "총량의 절반" 을 담는다. 두 번 하면 전량이 된다
//   · 줄마다 [1] [10] [100] 이 그만큼 담고, [모두] 는 전량을 담는다(다시 누르면 도로 뺀다)
//   · 담아 두기만 한 줄은 [비움] 하나로 되돌린다. 위 [비우기] 는 담은 것을 통째로 비운다
//   · [결정] 을 누를 때 소지금 · 재고 · 짐을 고치고 창을 닫는다(세이브 파일은 그대로)

#define ID_MARKET_OPEN 0xBD00u   // Trade=0xB10x, Char=0xB301/0xB310+, Ship=0xB410, Patch=0xB500,
                                 // Map=0xB600, Mod=0xB700, QMod=0xB800, Upd=0xB900,
                                 // Fatigue=0xBA00, Hotkey=0xBB00, Hint=0xBC00 과 안 겹치게.

#define WC_MARKET  L"MarketUtilKR_Window"
#define PIC        55                    // 줄 그림. 무엇인지 알아보게 46 에서 20% 키웠다
// 줄 높이 — 글씨 크기는 그대로 두고 줄 사이만 좁혔다(68 -> 54).
// 여덟 줄이 한눈에 들어와야 하는데 68 이면 목록이 너무 길어져 빈 칸만 보였다.
// 안쪽은 이름 18 + 단가·공급 18 + 무게·원산지 14 = 50 이라 글씨는 54 로도 되지만,
// 그림을 55 로 키우면서 60 으로 올렸다(그림 55 + 위 여백 2 = 57 = 줄 안쪽 높이).
#define ROW_H      60
#define ROWS_VIS   8                     // 짐 칸이 여덟이라 여덟 줄은 보여야 한 화면에 다 든다
#define COL_W      420
#define LIST_Y     (FRAME + TITLE_H + 30)
// 목록 테두리와 줄 사이 여백. 없으면 첫 줄이 테두리에 딱 붙어 답답해 보인다.
#define LIST_PAD   3
#define LIST_H     (ROW_H * ROWS_VIS + LIST_PAD * 2)
#define LEFT_X     (FRAME + 8)
#define RIGHT_X    (LEFT_X + COL_W + 12)
#define CLIENT_W   (RIGHT_X + COL_W + FRAME + 8)
#define CLIENT_H   (LIST_Y + LIST_H + 102)
#define BOT_Y      (LIST_Y + LIST_H)     // 목록 아래 — 여기부터 아래 칸이다

static HINSTANCE g_hinst = NULL;
// 단가 · 공급 전용 글씨 — 작은 글씨(12)보다 두 치수 크다. 사고 팔 때 제일 먼저 보는 값이라
// 나머지 잔글씨에 묻히면 안 된다. uikit 은 다른 플러그인도 같이 쓰므로 여기서만 만든다.
static HFONT     g_priceFont = NULL;
static HWND      g_wnd = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

#define MKT_ROWS_MAX 16
#define STEP_N 3
static const wchar_t* kStepName[STEP_N] = { L"1", L"10", L"100" };
static const int      kStepVal[STEP_N]  = { 1, 10, 100 };
// "절반" 은 단위에 두지 않는다 — 줄을 두 번 누르는 것이 그 몫이다.

static int g_city = -1;
static int g_rows = 0, g_cargo = 0;
static int g_qty[MKT_ROWS_MAX];          // 왼쪽 줄마다 담은 수량(살 것)
static int g_sell[MKT_CARGO_MAX];                   // 오른쪽 줄마다 담은 수량(팔 것)
// 아직 안 산 것(장바구니) 색 — 세피아 색표에 없어 여기서만 쓴다. 실은 짐(검정)과도,
// 팔 것(COL_WARN_TX 붉은색)과도 달라야 "이건 아직 내 것이 아니다" 가 한 눈에 잡힌다.
#define COL_CART_TX RGB(25, 95, 55)

// 오른쪽 칸에 그린 줄들. 그리는 쪽과 누르는 쪽이 같은 표를 봐야 단추가 엉뚱한 줄을 건드리지 않는다.
//   cargo = 짐 칸 번호(-1 이면 아직 없는데 담아 두기만 한 줄)
//   row   = 왼쪽 목록의 줄 번호(-1 이면 이 도시가 안 파는 것)
typedef struct { int kind, origin, cargo, row; } MktRight;
static MktRight g_right[ROWS_VIS];
static int g_rightRows = 0;              // 오른쪽에 실제로 그린 줄 수(짐 + 담는 중)
static wchar_t g_msg[160] = L"";
static int g_msgWarn = 0;

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

// 세 자리마다 쉼표. 한 줄에 여러 번 쓰므로 버퍼를 여섯 개 돌려 쓴다.
static const wchar_t* N(int v)
{
    static wchar_t ring[6][24];
    static int k = 0;
    wchar_t raw[16], *out = ring[k = (k + 1) % 6];
    int i, len, o = 0, neg = (v < 0);
    if (neg) v = -v;
    wsprintfW(raw, L"%d", v);
    len = lstrlenW(raw);
    if (neg) out[o++] = L'-';
    for (i = 0; i < len; i++) {
        if (i && (len - i) % 3 == 0) out[o++] = L',';
        out[o++] = raw[i];
    }
    out[o] = 0;
    return out;
}

// 도시 상태(도시struct +0x40). 게임 도시정보의 "상태" 줄과 같은 값이다.
// 이름표는 게임 안에 있다 — 0x429D60 이 [도시+0x40] 을 돌려주고, 0x429D70 이 그것으로
// 포인터 표 0x0053CE60(14칸) 을 찾아 이름을 낸다. 그 표를 그대로 옮겨 적었다.
// 살아 있는 값으로 대조: 226개 도시가 전부 0~13 안에 들었다(224개 통상, 2개 대조선).
#define MKT_STATE_N 14
static const wchar_t* kStateName[MKT_STATE_N] = {
    L"통상", L"전염병", L"기근", L"대기근", L"풍작", L"대풍작", L"대한파",
    L"혹서", L"노동력부족", L"전쟁", L"축제", L"호경기", L"불경기", L"대조선"
};
static const wchar_t* StateName(int v)
{
    static wchar_t buf[24];
    if (v >= 0 && v < MKT_STATE_N) return kStateName[v];
    if (v < 0) return L"상태 ?";
    wsprintfW(buf, L"상태 %d", v);       // 표 밖의 값 — 있으면 숫자로라도 보여 준다
    return buf;
}

static RECT RcRow(int side, int v)       // side 0 = 왼쪽(살 것), 1 = 오른쪽(내 짐)
{
    RECT r;
    r.left  = side ? RIGHT_X : LEFT_X;
    r.right = r.left + COL_W;
    r.top   = LIST_Y + LIST_PAD + v * ROW_H;
    r.bottom = r.top + ROW_H - 3;
    return r;
}

static void Say(const wchar_t* s, int warn) { lstrcpynW(g_msg, s, 160); g_msgWarn = warn; }

// 이 도시에는 교역소가 없다 — 파는 것이 하나도 없으면 사 주지도 않는다.
// 그런 곳에서는 왼쪽 목록이 비고 오른쪽(내 짐)도 잠근다.
static int NoPost(void) { return g_city >= 0 && g_rows <= 0; }

// 함대가 없다 — 육상으로 도시에 들어오면 배를 안 끌고 온다. 실을 데가 없으니 매매도 안 된다.
// 함대 여덟 칸이 다 비면 Mkt_Hold 가 0 을 돌려준다.
static int NoShip(void) { int n = 0; return !Mkt_Hold(&n, NULL, NULL) || n <= 0; }

// 지금 아무것도 만지면 안 되는 상태인가.
static int Locked(void) { return Mkt_TradeOpen() || NoPost() || NoShip(); }

// 줄 오른쪽 끝의 단추 넷 — [1] [10] [100] [모두]. 양쪽 칸이 같은 자리를 쓴다.
// 숫자는 "그만큼 담는다" 는 뜻이다. 예전에는 아래 칸에서 담기 단위를 고른 뒤 줄에서 [−][+] 를
// 눌렀는데, 한 번 담으려고 두 군데를 오가야 했다. 숫자를 줄로 내리고 [−][+] 는 걷어냈다.
// 도로 뺄 때는 [모두](다시 누르면 취소) 나 [비움] 을 쓴다.
static RECT RcBtn(int side, int v, int dxRight, int w)
{ RECT r = RcRow(side, v); RECT b; b.right = r.right - dxRight; b.left = b.right - w;
  b.top = r.top + 14; b.bottom = b.top + 24; return b; }
#define ROW_BTN_W  164                 // 단추 넷이 쓰는 오른쪽 자리(글씨는 여기까지만)
static RECT RcStepBtn(int side, int v, int k)   // k = 0·1·2 → 1 · 10 · 100
{ static const int dx[STEP_N] = { 132, 99, 62 };
  static const int w [STEP_N] = {  26, 30, 34 };
  return RcBtn(side, v, dx[k], w[k]); }
static RECT RcAllBuy(int v) { return RcBtn(0, v, 8, 50); }     // 공급 전량 담기
static RECT RcRowAll(int v) { return RcBtn(1, v, 8, 50); }     // 이 품목만 통째로
// [결정] — 지출 · 수입 · 수익 세 줄 오른쪽에 그 높이만큼 세워 둔다.
static RECT RcApply(void)
{ RECT r; r.right = RIGHT_X + COL_W; r.left = r.right - 90;
  r.top = BOT_Y + 32; r.bottom = BOT_Y + 92; return r; }
// [비우기] — 내 짐 칸 이름표 줄 오른쪽 끝. 담은 것을 통째로 되돌린다
// (아래 [결정] 옆에 있으면 지출 · 수입 줄과 섞여 무엇을 비우는 단추인지 흐려진다).
// [모두 팔기] 는 뺐다 — 줄마다 [모두] 가 있어서 같은 일을 두 군데서 하고 있었다.
static RECT RcClear(void)
{ RECT r; r.right = RIGHT_X + COL_W; r.left = r.right - 90;
  r.top = FRAME + TITLE_H + 3; r.bottom = r.top + 22; return r; }

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
    for (i = 0; i < MKT_CARGO_MAX; i++) g_sell[i] = 0;
}

// ── 흥정(값 깎기) ─────────────────────────────────────────────────────────────
// 게임 구입창도 [결정] 을 누르면 바로 사지 않는다 — "결정 / 값을 깎는다 / 돌아간다" 를
// 묻는다(게임 함수 0x4811E0). 그 규칙을 그대로 옮겨 왔다:
//   · 한 번 깎일 때마다 값이 90% 가 된다(여러 번 깎이면 곱해진다)
//   · 세 번째로 깎아 주면 더 못 깎고 그대로 산다
//   · 두 번째로 거절당하면 상인이 등을 돌린다 — 거래가 깨지고 창이 닫힌다
//   · 능력이 모자라면(게임 게이트 0x481400) 메뉴 없이 예전처럼 바로 산다
// 자세한 것은 note/TradeBargain-0x4811E0.md 에 적어 뒀다.
static int      g_barPct   = 100;   // 지금까지 깎인 값. 100 이면 아직 제 값이다
static int      g_barTries = 0;     // 이번 [결정] 에서 몇 번 걸었나
static int      g_barOn    = 0;     // 상인의 판이 떠 있나

// 흥정이 걸린 단가. 게임도 깎은 값이 1 닢 아래로는 안 내려간다.
static int BarUnit(int price)
{
    int p;
    if (price <= 0) return price;
    p = price * g_barPct / 100;
    return p > 0 ? p : 1;
}

// 지금 값으로 친 총액 — 깎였으면 깎인 값이다. 화면도 이 값을 보여 준다.
static int CartCostNow(void)
{
    int i, sum = 0;
    for (i = 0; i < g_rows && i < MKT_ROWS_MAX; i++) {
        const MktRow* w = Mkt_At(i);
        if (w && g_qty[i] > 0) sum += g_qty[i] * BarUnit(w->price);
    }
    return sum;
}

static void BarReset(void) { g_barPct = 100; g_barTries = 0; g_barOn = 0; }

// ── 상인의 판 ─────────────────────────────────────────────────────────────────
// 게임 것과 같은 모양이다 — 짙은 자주갈색 바탕에 크림 테두리, 그 안에 게임 띠 단추 셋이
// 세로로 쌓인다. 오리지널 화면에서 색을 집어 맞췄다(바탕 49,24,24 · 테두리 226,214,189).
#define BAR_BTN_H  24           // 띠의 제 높이
// 단추 사이는 2 픽셀뿐이다 — 게임 화면을 재보니 거의 붙어 있다(단추 자체에 위아래 테두리가
// 있어 0 으로 붙이면 그 선이 겹쳐 굵어 보인다).
#define BAR_GAP    2
#define BAR_PAD    12
#define BAR_H      (BAR_BTN_H * 3 + BAR_GAP * 2 + BAR_PAD * 2)
#define BAR_BG     RGB( 49, 24, 24)
#define BAR_EDGE   RGB(226, 214, 189)
#define BAR_MARGIN 24           // 글자 양옆 여백. ButtonMakerKR 의 자동 폭과 같은 값이다

static const wchar_t* kBarMenu[3] = { L"결정", L"값을 깎는다", L"돌아간다" };

// 단추 폭은 게임 띠 규칙(16 + 8n + 16)으로 잰다 — 셋 중 가장 긴 "값을 깎는다" 에 맞춰
// 셋을 같은 폭으로 세운다(게임 화면도 세 단추 폭이 같다). 글꼴을 못 읽으면 136 으로 둔다
// ("값을 깎는다" 가 글자폭 88 이라 16 + 8x13 + 16 = 136 인 그 값이다).
static int BarBtnW(void)
{
    int k, n = 0, w;
    for (k = 0; k < 3; k++) {
        int c = Band_AutoCells(kBarMenu[k], BAR_MARGIN);
        if (c > n) n = c;
    }
    w = Band_Width(n);
    return w >= 96 ? w : 136;
}

static RECT RcBarPanel(void)
{
    RECT r;
    int w = BarBtnW() + BAR_PAD * 2;
    r.left = (CLIENT_W - w) / 2;
    r.top  = (CLIENT_H - BAR_H) / 2;
    r.right = r.left + w; r.bottom = r.top + BAR_H;
    return r;
}
static RECT RcBarBtn(int k)
{
    RECT p = RcBarPanel(), r;
    r.left = p.left + BAR_PAD; r.right = p.right - BAR_PAD;
    r.top = p.top + BAR_PAD + k * (BAR_BTN_H + BAR_GAP);
    r.bottom = r.top + BAR_BTN_H;
    return r;
}
static void PaintBargain(HDC dc)
{
    RECT p = RcBarPanel(), r;
    HBRUSH br;
    int k;
    if (!g_barOn) return;
    br = CreateSolidBrush(BAR_BG); FillRect(dc, &p, br); DeleteObject(br);
    br = CreateSolidBrush(BAR_EDGE);
    r = p; FrameRect(dc, &r, br);
    InflateRect(&r, -1, -1); FrameRect(dc, &r, br);    // 두 줄 — 게임 테두리도 굵다
    DeleteObject(br);
    for (k = 0; k < 3; k++) UI_Button(dc, RcBarBtn(k), kBarMenu[k], FALSE);
}

// 매각 담기 — 짐 칸 하나에서 뺄 수량.
static void SellAdd(int v, int n)
{
    const MktCargo* c = Mkt_CargoAt(v);
    if (!c || v < 0 || v >= MKT_CARGO_MAX) return;
    g_sell[v] += n;
    if (g_sell[v] > c->count) g_sell[v] = c->count;
    if (g_sell[v] < 0) g_sell[v] = 0;
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

// 그 교역품 · 원산지에 해당하는 왼쪽 줄 번호. 없으면 -1.
static int RowIndexFor(int kind, int origin)
{
    int i;
    for (i = 0; i < g_rows && i < MKT_ROWS_MAX; i++) {
        const MktRow* w = Mkt_At(i);
        if (w && w->kind == kind && w->origin == origin) return i;
    }
    return -1;
}

// 오른쪽 칸에 그릴 줄을 모은다 — 먼저 실은 짐, 그 다음 아직 없는데 담아 둔 것.
// 그리기 전에도, 단추를 누를 때도 이것을 불러 같은 표를 본다.
static int BuildRight(void)
{
    int i, v = 0;
    for (i = 0; i < g_cargo && v < ROWS_VIS; i++) {
        const MktCargo* c = Mkt_CargoAt(i);
        if (!c) continue;
        g_right[v].kind = c->kind; g_right[v].origin = c->origin;
        g_right[v].cargo = i;      g_right[v].row = RowIndexFor(c->kind, c->origin);
        v++;
    }
    for (i = 0; i < g_rows && i < MKT_ROWS_MAX && v < ROWS_VIS; i++) {
        const MktRow* w = Mkt_At(i);
        int j, have = 0;
        if (!w || g_qty[i] <= 0) continue;
        for (j = 0; j < g_cargo; j++) {
            const MktCargo* c = Mkt_CargoAt(j);
            if (c && c->kind == w->kind && c->origin == w->origin) { have = 1; break; }
        }
        if (have) continue;
        g_right[v].kind = w->kind; g_right[v].origin = w->origin;
        g_right[v].cargo = -1;     g_right[v].row = i;
        v++;
    }
    g_rightRows = v;
    return v;
}

// 팔아서 남는 것 — (매각가 − 원산지 매입가) x 수량. 매입가를 모르면 그 줄은 뺀다.
static int SellProfit(void)
{
    int i, sum = 0;
    for (i = 0; i < g_cargo && i < MKT_CARGO_MAX; i++) {
        const MktCargo* c = Mkt_CargoAt(i);
        if (c && g_sell[i] > 0) {
            int sp = Mkt_SellPrice(g_city, c->kind);
            int bp = Mkt_BuyPriceAt(c->origin, c->kind);
            if (sp > 0 && bp > 0) sum += g_sell[i] * (sp - bp);
        }
    }
    return sum;
}

static int SellGain(void)
{
    int i, sum = 0;
    for (i = 0; i < g_cargo && i < MKT_CARGO_MAX; i++) {
        const MktCargo* c = Mkt_CargoAt(i);
        if (c && g_sell[i] > 0) {
            int pr = Mkt_SellPrice(g_city, c->kind);
            if (pr > 0) sum += g_sell[i] * pr;
        }
    }
    return sum;
}

// 짐칸 셈 — 지금 실은 갯수, 담아 둔 것(살 것 − 팔 것).
static int CargoCount(void)
{
    int i, sum = 0;
    for (i = 0; i < g_cargo && i < MKT_CARGO_MAX; i++) {
        const MktCargo* c = Mkt_CargoAt(i);
        if (c) sum += c->count;
    }
    return sum;
}
static int CartQty(void)
{
    int i, sum = 0;
    for (i = 0; i < g_rows && i < MKT_ROWS_MAX; i++) sum += g_qty[i];
    for (i = 0; i < g_cargo && i < MKT_CARGO_MAX; i++)          sum -= g_sell[i];
    return sum;
}
// 무게는 품목마다 다르다(교역품 레코드 +0x70). 갯수 x 그 무게를 더한다.
static int CargoMass(void)
{
    int i, sum = 0;
    for (i = 0; i < g_cargo && i < MKT_CARGO_MAX; i++) {
        const MktCargo* c = Mkt_CargoAt(i);
        int m = c ? Mkt_GoodsMass(c->kind) : -1;
        if (c && m > 0) sum += c->count * m;
    }
    return sum;
}
static int CartMass(void)
{
    int i, sum = 0;
    for (i = 0; i < g_rows && i < MKT_ROWS_MAX; i++) {
        const MktRow* w = Mkt_At(i);
        int m = w ? Mkt_GoodsMass(w->kind) : -1;
        if (w && m > 0) sum += g_qty[i] * m;
    }
    for (i = 0; i < g_cargo && i < MKT_CARGO_MAX; i++) {
        const MktCargo* c = Mkt_CargoAt(i);
        int m = c ? Mkt_GoodsMass(c->kind) : -1;
        if (c && m > 0) sum -= g_sell[i] * m;
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

static void Reload(HWND h)
{
    g_city = Mkt_CurrentCity();
    g_rows = (g_city >= 0) ? Mkt_BuildList(g_city) : 0;
    if (g_rows > MKT_ROWS_MAX) g_rows = MKT_ROWS_MAX;
    g_cargo = Mkt_LoadCargo();
    if (h) InvalidateRect(h, NULL, FALSE);
}

// 짐칸 막대 — [이름] [막대] [실은 것 → 담은 뒤 / 총량].
// used = 이미 실은 것, add = 이번에 담은 것(음수면 파는 것), max = 총량.
static void PaintBar(HDC dc, RECT r, const wchar_t* name, int used, int add, int max)
{
    RECT lb = r, bar = r, tx = r, in;
    HBRUSH br;
    wchar_t buf[96];
    int after = used + add, w, p1, p2;

    lb.right = lb.left + 44;
    UI_Text(dc, lb, name, g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    // 숫자는 막대 바로 뒤에 붙인다 — 오른쪽 끝에 밀어 두면 막대와 멀어져 눈이 헤맨다.
    tx.left = r.right - 150;
    bar.left = lb.right + 4; bar.right = tx.left - 8;
    bar.top += 3; bar.bottom -= 3;
    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &bar, br); DeleteObject(br);

    in = bar; InflateRect(&in, -2, -2);
    w = in.right - in.left;
    if (max <= 0) max = 1;
    p1 = used  >= max ? w : (int)((__int64)w * used  / max);
    p2 = after >= max ? w : (int)((__int64)w * after / max);
    if (p1 < 0) p1 = 0;
    if (p2 < 0) p2 = 0;
    if (p2 > p1) {                       // 담은 만큼 늘어난 자리
        RECT s = in; s.left = in.left + p1; s.right = in.left + p2;
        br = CreateSolidBrush(after > max ? COL_WARN_TX : COL_LANG_TX);
        FillRect(dc, &s, br); DeleteObject(br);
    }
    else if (p2 < p1) {                  // 파느라 비는 자리
        RECT s = in; s.left = in.left + p2; s.right = in.left + p1;
        br = CreateSolidBrush(COL_ROW_ALT); FillRect(dc, &s, br); DeleteObject(br);
    }
    { RECT s = in; s.right = in.left + (p1 < p2 ? p1 : p2);
      br = CreateSolidBrush(RGB(150, 55, 20)); FillRect(dc, &s, br); DeleteObject(br); }
    UI_Bevel(dc, bar, TRUE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &bar, br); DeleteObject(br);

    if (add) wsprintfW(buf, L"%s → %s / %s", N(used), N(after), N(max));
    else     wsprintfW(buf, L"%s / %s", N(used), N(max));
    UI_Text(dc, tx, buf, g_smallFont, after > max ? COL_WARN_TX : COL_TEXT,
            DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

// [이름] [값] 한 줄. 이름 자리를 고정해 두면 여러 줄이 세로로 딱 맞아 읽기 쉽다.
static void PaintKV(HDC dc, RECT r, const wchar_t* name, const wchar_t* val, COLORREF c)
{
    RECT lb = r, tx = r;
    lb.right = lb.left + 44;
    UI_Text(dc, lb, name, g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    tx.left = lb.right + 10;
    UI_Text(dc, tx, val, g_font, c, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
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

    t = r; t.left = box.right + 12;
    t.right = r.right - ROW_BTN_W;            // 단추 넷([1][10][100][모두]) 자리는 비워 둔다
    t.top = r.top + 1; t.bottom = t.top + 18;
    {   // 이 도시 특산품이면 이름 앞에 ★ — 게임 도시정보의 "특산품" 과 같은 자리(+0x10)다.
        wchar_t nm[80];
        if (g_city >= 0 && kind == Mkt_CitySpecial(g_city))
             wsprintfW(nm, L"★ %s", Mkt_GoodsName(kind));
        else lstrcpynW(nm, Mkt_GoodsName(kind), 80);
        UI_Text(dc, t, nm, g_font, COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }

    t.top = t.bottom; t.bottom = t.top + 18;
    if (side == 0) {
        // 공급은 담은 만큼 깎아 보여 준다 — 전량을 담으면 0 이 되어, 이 도시에 남은 것이
        // 얼마인지 한 눈에 잡힌다(담은 것은 오른쪽 칸의 갯수로 그만큼 옮겨 가 있다).
        int q = (v < MKT_ROWS_MAX) ? g_qty[v] : 0;
        int rest = b - q;
        if (rest < 0) rest = 0;
        // 무게는 아랫줄로 내렸다 — 이 줄은 단가와 공급만 큰 글씨로 둔다.
        wsprintfW(buf, L"단가 %s닢 · 공급 %s", N(a), N(rest));
        UI_Text(dc, t, buf, g_priceFont ? g_priceFont : g_smallFont,
                (q > 0 && rest == 0) ? COL_CART_TX : COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    else {
        // 갯수 · 매각가 · 원산지 매입가를 나란히 — 얼마 남고 얼마 밑지는지 바로 보이게.
        // 갯수는 따로 떼어 그린다 — 아직 안 산 것(담는 중)이 섞이면 그 색으로 알린다.
        int pend = PendingFor(kind, origin);
        int have = a + pend;
        int buyp = Mkt_BuyPriceAt(origin, kind), diff = (buyp > 0) ? b - buyp : 0;
        COLORREF col = (have > 0 && buyp > 0) ? (diff >= 0 ? COL_LANG_TX : COL_WARN_TX) : COL_TEXT;
        // 아직 안 산 채로 담아 두기만 한 줄. 여기서는 살지 말지만 정하면 되므로
        // 매각가도 손익도 안 보여 준다 — 팔 수 있는 물건이 아직 아니다.
        int cartOnly = ((v < ROWS_VIS ? g_right[v].cargo : -1) < 0 && pend > 0);
        // 갯수와 매각가는 왼쪽 칸의 "단가 · 공급" 과 같은 글씨를 쓴다 — 양쪽에서 제일 먼저
        // 보는 값인데 한쪽만 잔글씨면 눈이 한 번 더 간다. 손익(−50)만 작은 글씨로 남긴다.
        HFONT bigFont = g_priceFont ? g_priceFont : g_smallFont;
        RECT cr = t, pr = t;
        cr.right = t.left + 54; pr.left = cr.right + 4;
        if (have > 0) {
            wsprintfW(buf, L"%s개", N(have));
            UI_Text(dc, cr, buf, bigFont, pend > 0 ? COL_CART_TX : COL_TEXT,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        else UI_Text(dc, cr, L"아직 없음", bigFont, COL_DARK,
                     DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        // 이 줄에는 매각가만 둔다 — 매입가까지 넣으면 자리가 모자라 통째로 잘려 나갔다
        // ("매각 236..."). 매입가는 아랫줄로 내리고, 남는 값만 오른쪽 끝에 따로 붙인다.
        wsprintfW(buf, L"매각 %s닢", N(b > 0 ? b : 0));
        if (cartOnly) { /* 아무것도 안 그린다 */ }
        else if (have > 0 && buyp > 0 && b > 0) {
            // 매각가와 손익이 한 줄에 같이 선다. 손익 자리를 너무 넓게 잡으면 매각가가
            // "매각 1..." 로 잘린다 — 단추가 넷으로 늘면서 한 번 겪었다.
            RECT tx = pr, dr = t;
            dr.left = t.right - 46; tx.right = dr.left - 6;
            UI_Text(dc, tx, buf, bigFont, col,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            wsprintfW(buf, L"%s%s", diff >= 0 ? L"+" : L"−", N(diff >= 0 ? diff : -diff));
            UI_Text(dc, dr, buf, g_smallFont, col,
                    DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        else UI_Text(dc, pr, buf, bigFont, col,
                     DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }

    t.top = t.bottom; t.bottom = t.top + 14;
    if (side == 0) {
        int q = (v < MKT_ROWS_MAX) ? g_qty[v] : 0;
        int gm = Mkt_GoodsMass(kind);
        RECT tw = t, to = t;
        // 무게는 앞에 따로 떼어 둔다 — 담은 것에 붉은 색이 물들지 않게.
        tw.right = t.left + 58; to.left = tw.right + 4;
        if (gm > 0) {
            wsprintfW(buf, L"무게 %d", gm);
            UI_Text(dc, tw, buf, g_smallFont, COL_TEXT,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        else to = t;
        // "담은 것 100개" 는 뺐다 — 윗줄 공급이 그만큼 깎이고, 오른쪽 칸 갯수가 초록으로 알려 준다.
        // 담아 둔 줄이라는 표시는 원산지 글씨 색으로만 남긴다.
        wsprintfW(buf, L"%s산", Mkt_CityName(origin));
        UI_Text(dc, to, buf, g_smallFont, q > 0 ? COL_CART_TX : COL_DARK,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        { int k; for (k = 0; k < STEP_N; k++)
              UI_Button(dc, RcStepBtn(0, v, k), kStepName[k], FALSE); }
        UI_Button(dc, RcAllBuy(v), L"모두", q >= b && b > 0);
    } else {
        int ci   = (v < ROWS_VIS) ? g_right[v].cargo : -1;   // 짐 칸 번호(-1 = 담아 두기만 한 줄)
        int q    = (ci >= 0 && ci < MKT_CARGO_MAX) ? g_sell[ci] : 0;
        int pend = PendingFor(kind, origin);
        // "담는 중 +100개" 는 뺐다 — 윗줄 갯수가 이미 담은 것까지 세어 초록으로 알려 준다.
        // 대신 윗줄에서 자리가 없어 밀려난 원산지 매입가를 여기 둔다(팔 것을 담으면 그쪽이 먼저다).
        if (q > 0) wsprintfW(buf, L"%s산 · 팔 것 %s개 (%s닢)", Mkt_CityName(origin), N(q), N(q * b));
        else {
            int buyp = Mkt_BuyPriceAt(origin, kind);
            if (buyp > 0) wsprintfW(buf, L"%s산 · 매입 %s닢", Mkt_CityName(origin), N(buyp));
            else          wsprintfW(buf, L"%s산", origin >= 0 ? Mkt_CityName(origin) : L"?");
        }
        UI_Text(dc, t, buf, g_smallFont,
                q > 0 ? COL_WARN_TX : (pend > 0 ? COL_CART_TX : COL_DARK),
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if (ci >= 0) {           // 실은 것이 있는 줄 — 파는 단추
            int k; for (k = 0; k < STEP_N; k++)
                UI_Button(dc, RcStepBtn(1, v, k), kStepName[k], FALSE);
            UI_Button(dc, RcRowAll(v), L"모두", q >= a);
        }
        else if (pend > 0) {
            // 담아 두기만 한 줄 — [비움] 하나면 된다. 수량은 왼쪽 목록에서 정하는 것이고,
            // 여기 [−][+] 는 같은 일을 두 군데서 하게 만들어 줄만 복잡했다.
            UI_Button(dc, RcRowAll(v), L"비움", FALSE);
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
    if (g_city >= 0) {
        int sise = Mkt_CitySise(g_city);
        const wchar_t* st = StateName(Mkt_CityState(g_city));
        if (sise > 0) wsprintfW(buf, L"매매 — %s · 시세 %d · %s", Mkt_CityName(g_city), sise, st);
        else          wsprintfW(buf, L"매매 — %s · %s", Mkt_CityName(g_city), st);
    }
    else lstrcpyW(buf, L"매매");
    UI_WindowFrame(dc, rc, buf, &cb);

    r.left = LEFT_X; r.right = LEFT_X + COL_W; r.top = FRAME + TITLE_H + 4; r.bottom = r.top + 22;
    UI_Text(dc, r, L"이 도시가 파는 것", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    r.left = RIGHT_X; r.right = RcClear().left - 8;
    UI_Text(dc, r, L"내 짐", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    if (!Locked()) {
        UI_Button(dc, RcClear(), L"비우기", FALSE);
    }

    { RECT p; p.left = LEFT_X; p.right = LEFT_X + COL_W; p.top = LIST_Y; p.bottom = LIST_Y + LIST_H;
      br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
      // 배가 없으면 줄을 안 그린다 — 담을 수도 없는 단추를 늘어놓아 봐야 헷갈리기만 한다.
      if (!NoShip())
      for (i = 0; i < ROWS_VIS && i < g_rows; i++) {
          const MktRow* w = Mkt_At(i);
          if (w) PaintRow(dc, 0, i, w->kind, w->price, w->supply, w->origin);
      }
      br = CreateSolidBrush(COL_DARK); FrameRect(dc, &p, br); DeleteObject(br);
      if (g_city < 0)
          UI_Text(dc, p, L"항해 중입니다 — 도시에 정박해야 합니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
      else if (NoShip())
          UI_Text(dc, p, L"배가 없어 사고팔 수 없습니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
      else if (!g_rows)
          UI_Text(dc, p, L"이 도시에는 교역소가 없거나 파는 것이 없습니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }

    { RECT p; p.left = RIGHT_X; p.right = RIGHT_X + COL_W; p.top = LIST_Y; p.bottom = LIST_Y + LIST_H;
      br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
      { int v;
        BuildRight();
        if (!Locked())
        for (v = 0; v < g_rightRows; v++) {
            const MktCargo* c = (g_right[v].cargo >= 0) ? Mkt_CargoAt(g_right[v].cargo) : NULL;
            int here = Mkt_SellPrice(g_city, g_right[v].kind);
            PaintRow(dc, 1, v, g_right[v].kind, c ? c->count : 0,
                     here > 0 ? here : 0, g_right[v].origin);
        } }
      br = CreateSolidBrush(COL_DARK); FrameRect(dc, &p, br); DeleteObject(br);
      if (NoShip()) {
          RECT d = p;
          d.bottom = d.top + (p.bottom - p.top) / 2;
          UI_Text(dc, d, L"배가 없습니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_BOTTOM|DT_SINGLELINE|DT_NOPREFIX);
          d.top = d.bottom + 6; d.bottom = d.top + 44;
          UI_Text(dc, d, L"육상으로 들어온 도시에서는 실을 데가 없어\n사고팔 수 없습니다.",
                  g_smallFont, COL_TEXT, DT_CENTER|DT_TOP|DT_NOPREFIX);
      }
      else if (NoPost()) {
          RECT d = p;
          d.bottom = d.top + (p.bottom - p.top) / 2;
          UI_Text(dc, d, L"이 도시에는 교역소가 없습니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_BOTTOM|DT_SINGLELINE|DT_NOPREFIX);
          d.top = d.bottom + 6; d.bottom = d.top + 44;
          UI_Text(dc, d, L"사고팔 수 있는 곳으로 가야 합니다.",
                  g_smallFont, COL_TEXT, DT_CENTER|DT_TOP|DT_NOPREFIX);
      }
      else if (Mkt_TradeOpen()) {
          // 게임이 교역소를 열면서 짐 여덟 칸을 통째로 비워 놓았다. 지금 읽어 봐야 비어 있고,
          // 여기서 사고팔면 게임이 [결정] 때 되돌려 넣을 자리를 잃는다. marketdb.h 참고.
          RECT d = p;
          d.bottom = d.top + (p.bottom - p.top) / 2;
          UI_Text(dc, d, L"게임 교역소가 열려 있습니다.", g_font, COL_TEXT,
                  DT_CENTER|DT_BOTTOM|DT_SINGLELINE|DT_NOPREFIX);
          d.top = d.bottom + 6; d.bottom = d.top + 44;
          UI_Text(dc, d, L"그동안 짐은 게임 쪽 창이 들고 있습니다.\n게임 교역소를 닫은 뒤에 쓰세요.",
                  g_smallFont, COL_TEXT, DT_CENTER|DT_TOP|DT_NOPREFIX);
      }
      else if (!g_rightRows) {
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
              for (k = 0; k < MKT_CARGO_SLOTS; k++) {
                  d.top = d.bottom; d.bottom = d.top + 18;
                  Mkt_CargoRaw(k, raw);
                  wsprintfW(buf, L"짐 %d칸: %d %d %d %d", k, raw[0], raw[1], raw[2], raw[3]);
                  UI_Text(dc, d, buf, g_smallFont, COL_DARK,
                          DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
              }
          }
      } }

    {   // 아래 칸 — 왼쪽은 소지금 · 함대 짐칸 막대, 오른쪽은 지출 · 수입 · 수익.
        //   담기 단위 줄은 없앴다 — 줄마다 [1][10][100] 이 있어 여기서 미리 고를 것이 없다.
        int massMax = 0, capMax = 0;
        int hold = Mkt_Hold(NULL, &massMax, &capMax);   // 배 척수는 안 쓴다
        int supV = hold ? Mkt_SupplyVolume() : -1, supM = hold ? Mkt_SupplyMass() : -1;
        int cost = CartCostNow(), gain = SellGain(), prof = SellProfit();
        RECT lb;

        // ── 첫 줄: 소지금. 목록 바로 밑이다 — 예전에는 28 이나 떠 있어 허전했다.
        lb.left = LEFT_X; lb.right = LEFT_X + COL_W;
        lb.top = BOT_Y + 4; lb.bottom = lb.top + 20;
        if (money >= 0) wsprintfW(buf, L"소지금 %s닢", N(money));
        else            lstrcpyW(buf, L"소지금을 읽지 못했습니다 — 세이브를 불러온 뒤에 열어 주세요.");
        UI_Text(dc, lb, buf, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        // ── 셋째 · 넷째 줄 왼쪽: 함대 짐칸 막대. 소지금 바로 아래다.
        //   게임 함대 화면의 짐용량 · 짐중량과 같은 눈금이다(물·식량·자재·포탄까지 다 센 값).
        //   이미 실은 것 = 짙은 빨강, 담은 것 = 파랑. 예전에 여기 있던 "중량 0 / 13,795"
        //   식의 교역수지 두 줄은 이 막대가 같은 것을 더 잘 보여 줘서 걷어냈다.
        if (hold) {
            RECT b2;
            b2.left = LEFT_X; b2.right = LEFT_X + COL_W;
            b2.top = BOT_Y + 32; b2.bottom = b2.top + 18;
            if (supV >= 0) PaintBar(dc, b2, L"짐용량", supV + CargoCount(), CartQty(),  capMax);
            b2.top = BOT_Y + 54; b2.bottom = b2.top + 18;
            if (supM >= 0) PaintBar(dc, b2, L"짐중량", supM + CargoMass(),  CartMass(), massMax);
        }

        // ── 셋째~다섯째 줄 오른쪽: 지출 · 수입 · 수익을 한 줄에 하나씩.
        //    한 줄에 몰아 놓으니 길어서 눈에 안 들어왔다. 색은 안 입힌다 —
        //    수익만 부호에 따라(남으면 파랑, 밑지면 빨강) 물들여 그것만 눈에 띄게 한다.
        {   RECT kv;
            kv.left = RIGHT_X; kv.right = RcApply().left - 12;
            kv.top = BOT_Y + 30; kv.bottom = kv.top + 22;
            wsprintfW(buf, L"%s닢", N(cost));
            // 흥정으로 깎였으면 그렇게 적는다 — 지출 칸 숫자가 왜 줄었는지 보여야 한다.
            if (g_barPct < 100) {
                wchar_t lab[24];
                wsprintfW(lab, L"지출(%d%%↓)", 100 - g_barPct);
                PaintKV(dc, kv, lab, buf, COL_LANG_TX);
            }
            else PaintKV(dc, kv, L"지출", buf, COL_TEXT);
            kv.top = BOT_Y + 52; kv.bottom = kv.top + 22;
            wsprintfW(buf, L"%s닢", N(gain));
            PaintKV(dc, kv, L"수입", buf, COL_TEXT);
            kv.top = BOT_Y + 74; kv.bottom = kv.top + 22;
            if (gain > 0) {
                wsprintfW(buf, L"%s%s닢", prof >= 0 ? L"+" : L"−", N(prof >= 0 ? prof : -prof));
                PaintKV(dc, kv, L"수익", buf, prof >= 0 ? COL_LANG_TX : COL_WARN_TX);
            }
            else PaintKV(dc, kv, L"수익", L"—", COL_DARK);
        }
        UI_Button(dc, RcApply(), L"결정", (cost > 0 || gain > 0));   // [비우기] 는 위 이름표 줄에 있다

        // ── 다섯째 줄 왼쪽: 알림. 할 말이 있을 때만 쓴다.
        if (g_msg[0]) {
            lb.left = LEFT_X; lb.right = RIGHT_X - 12;
            lb.top = BOT_Y + 76; lb.bottom = lb.top + 18;
            UI_Text(dc, lb, g_msg, g_smallFont, g_msgWarn ? COL_WARN_TX : COL_DARK,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
    }

    PaintBargain(dc);       // 상인의 판은 맨 나중에 — 창 위에 얹힌다
    UI_BufEnd(&b);
    EndPaint(h, &ps);
}

// 담아 둔 것을 실제로 산다. 한 줄이라도 실패하면 그 줄에서 멈추고 무엇이 문제인지 적는다
// (이미 산 줄은 그대로 둔다 — 소지금과 짐은 줄 단위로 맞아 있다).
static void CartApply(HWND h, int keepOpen)
{
    int i, total = 0, bought = 0;
    (void)0;
    wchar_t buf[160];

    // 먼저 판다 — 번 돈으로 이어서 살 수 있게. 뒤에서부터 팔아야 칸이 당겨져도 번호가 안 어긋난다.
    for (i = (g_cargo < MKT_CARGO_MAX ? g_cargo : MKT_CARGO_MAX) - 1; i >= 0; i--) {
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
        got = Mkt_BuyAt(g_city, i, g_qty[i], BarUnit(w->price));
        if (got < 0) {
            const wchar_t* why =
                got == MKT_E_MONEY  ? L"소지금이 모자랍니다" :
                got == MKT_E_FULL   ? L"짐 칸이 없습니다" :
                got == MKT_E_SUPPLY ? L"공급량이 모자랍니다" : L"자리를 읽지 못했습니다";
            wsprintfW(buf, L"%s 에서 멈췄습니다 — %s. (%s개 %s닢까지 샀습니다)",
                      Mkt_GoodsName(w->kind), why, N(bought), N(total));
            Say(buf, 1);
            CartClear();
            BarReset();
            Reload(h);
            return;
        }
        total += got; bought += g_qty[i];
    }
    if (!bought) { Say(L"담은 것이 없습니다.", 1); InvalidateRect(h, NULL, FALSE); return; }
    if (g_barPct < 100)  wsprintfW(buf, L"%s개 · 지출 %s닢 (%d%% 깎았습니다).",
                                   N(bought), N(total >= 0 ? total : -total), 100 - g_barPct);
    else if (total >= 0) wsprintfW(buf, L"%s개 · 지출 %s닢.", N(bought), N(total));
    else                 wsprintfW(buf, L"%s개 · %s닢 남았습니다.", N(bought), N(-total));
    Say(buf, 0);
    CartClear();
    BarReset();
    Reload(h);
    // 다 사고 팔았으면 창을 닫는다 — 게임 구입창처럼 [결정] 한 번으로 끝난다.
    // (막힌 자리에서 멈췄을 때는 위에서 이미 돌아갔다 — 무엇이 문제였는지 창에 남는다.)
    // 흥정 끝에 산 것이면 상인이 한 말을 읽어야 하므로 창을 남긴다.
    if (!keepOpen) ShowWindow(h, SW_HIDE);
}

// [결정] 을 눌렀을 때. 살 것이 없거나 흥정이 안 되는 처지면 예전처럼 바로 산다.
static void CartDecide(HWND h)
{
    int cost = CartCost();
    BarReset();
    if (cost <= 0 || !Mkt_BargainAllowed(g_city, cost)) { CartApply(h, 0); return; }
    g_barOn = 1;
    InvalidateRect(h, NULL, FALSE);
}

// 판에서 고른 것. k 는 0 = 결정, 1 = 값을 깎는다, 2 = 돌아간다.
static void BargainPick(HWND h, int k)
{
    wchar_t buf[160];
    int ok, cut;

    if (k == 0) { g_barOn = 0; CartApply(h, 0); return; }
    if (k != 1) { g_barOn = 0; InvalidateRect(h, NULL, FALSE); return; }   // 돌아간다

    ok = Mkt_BargainRoll();
    if (ok) g_barPct = g_barPct * MKT_BARGAIN_PCT / 100;
    Say(Mkt_BargainLine(ok, g_barTries, CartCostNow()), !ok);
    g_barTries++;

    if (ok && g_barTries >= MKT_BARGAIN_WIN) {      // 더는 못 깎는다 — 그대로 산다
        g_barOn = 0;
        CartApply(h, 1);
        return;
    }
    if (!ok && g_barTries >= MKT_BARGAIN_LOSE) {    // 거래가 깨졌다
        // 상인은 말만 하고 마는 것이 아니라 물건을 거둬 간다 — 게임 0x481190 그대로,
        // 판매목록의 공급량이 그만큼 줄어든다(세 번 넘게 걸었으면 씨가 마른다).
        cut = (g_barTries >= 3) ? MKT_CUT_HARD : MKT_CUT_BREAK;
        lstrcpynW(buf, g_msg, 100);
        Mkt_SupplyCut(g_city, cut);
        CartClear();
        BarReset();
        Reload(h);
        {   wchar_t line[160];
            wsprintfW(line, L"%s (공급이 %d%% 줄었습니다)", buf, cut);
            Say(line, 1);
        }
        InvalidateRect(h, NULL, FALSE);
        return;
    }
    InvalidateRect(h, NULL, FALSE);                 // 말만 바뀌고 판은 그대로 선다
}

// 흥정만 걸어 놓고 안 사고 나가면 상인이 10% 를 거둬 간다(게임 0x481874).
// 그 말을 읽을 수 있게 첫 닫기는 삼키고 창을 남긴다 — 한 번 더 닫으면 그대로 닫힌다.
static int BargainQuit(HWND h)
{
    if (g_barTries <= 0) return 0;
    Mkt_SupplyCut(g_city, MKT_CUT_QUIT);
    CartClear();
    BarReset();
    Reload(h);
    {   wchar_t line[160];
        wsprintfW(line, L"%s (공급이 %d%% 줄었습니다)", Mkt_BargainQuitLine(), MKT_CUT_QUIT);
        Say(line, 1);
    }
    InvalidateRect(h, NULL, FALSE);
    return 1;
}

// 줄의 [모두] — 그 품목만 통째로 담는다(왼쪽은 공급 전량, 오른쪽은 짐 전량).
// 이미 다 담겨 있으면 도로 뺀다. 누르고 또 누르는 것이 더블클릭으로 가 버리므로
// 두 자리(WM_LBUTTONDOWN · DBLCLK)에서 다 본다.
static int RowAllHit(HWND h, POINT pt)
{
    int v;
    for (v = 0; v < ROWS_VIS && v < g_rows && v < MKT_ROWS_MAX; v++) {
        RECT b = RcAllBuy(v);
        const MktRow* w;
        if (!PtInRect(&b, pt)) continue;
        w = Mkt_At(v);
        if (w && w->supply > 0) g_qty[v] = (g_qty[v] >= w->supply) ? 0 : w->supply;
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    BuildRight();
    for (v = 0; v < g_rightRows; v++) {
        RECT b = RcRowAll(v);
        int ci = g_right[v].cargo, row = g_right[v].row;
        if (!PtInRect(&b, pt)) continue;
        if (ci >= 0) {                       // [모두] — 그 짐을 통째로 팔 것에 담는다
            const MktCargo* c = Mkt_CargoAt(ci);
            if (c && c->count > 0 && ci < MKT_CARGO_MAX)
                g_sell[ci] = (g_sell[ci] >= c->count) ? 0 : c->count;
        }
        else if (row >= 0 && row < MKT_ROWS_MAX) {   // [비움] — 담아 둔 것을 통째로 뺀다
            g_qty[row] = 0;
        }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    return 0;
}

static LRESULT CALLBACK MarketProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        // 단추를 게임 것과 같은 베이지 띠로 갈아 끼운다. MISC.CDS 를 못 읽으면
        // GameSkin_Button 이 0 을 돌려주므로 저절로 원래 모양으로 물러난다.
        UI_SetButtonDraw(GameSkin_Button);
        if (!g_priceFont)
            g_priceFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, 0, 0, 0, 0, L"바탕");
        ItemPic_Load();
        g_msg[0] = 0;
        CartClear();
        BarReset();
        Reload(NULL);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(h); return 0;
    case WM_LBUTTONDBLCLK:
    {
        POINT pt; int v;
        if (g_barOn) return 0;                // 판이 떠 있는 동안은 목록을 못 건드린다
        if (Locked()) return 0;               // 게임 교역소가 열려 있거나 여기가 교역소가 아니면 잠근다
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        if (RowAllHit(h, pt)) return 0;
        for (v = 0; v < ROWS_VIS && v < g_rows; v++) {
            RECT r = RcRow(0, v);
            const MktRow* w;
            if (!PtInRect(&r, pt)) continue;
            w = Mkt_At(v);
            if (w) CartAdd(v, (w->supply + 1) / 2);   // 총량의 절반 — 두 번이면 전량
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        BuildRight();
        for (v = 0; v < g_rightRows; v++) {
            RECT r = RcRow(1, v);
            int ci = g_right[v].cargo;
            const MktCargo* c;
            if (!PtInRect(&r, pt)) continue;
            if (ci >= 0 && (c = Mkt_CargoAt(ci)) != NULL) SellAdd(ci, (c->count + 1) / 2);
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        if (g_barOn) {   // 판이 떠 있으면 그것이 먼저 먹는다 — 그동안은 모달처럼 군다
            int k;
            for (k = 0; k < 3; k++) {
                RECT b = RcBarBtn(k);
                if (PtInRect(&b, pt)) { BargainPick(h, k); return 0; }
            }
            return 0;                     // 판 밖을 눌러도 아무 일 없다
        }
        GetClientRect(h, &rc);
        cb.right = rc.right - FRAME - 4; cb.left = cb.right - 22;
        cb.top = FRAME + 4; cb.bottom = cb.top + 18;
        if (PtInRect(&cb, pt)) { if (!BargainQuit(h)) ShowWindow(h, SW_HIDE); return 0; }
        // 닫기 말고는 다 잠근다 — 게임 교역소가 열려 있는 동안 짐 칸을 건드리면
        // 게임이 [결정] 때 물건을 되돌려 넣을 자리를 잃는다.
        if (Locked()) return 0;
        {   int v;
            for (v = 0; v < ROWS_VIS && v < g_rows; v++) {
                int k;
                for (k = 0; k < STEP_N; k++) {
                    RECT s = RcStepBtn(0, v, k);
                    if (!PtInRect(&s, pt)) continue;
                    CartAdd(v, kStepVal[k]);
                    InvalidateRect(h, NULL, FALSE);
                    return 0;
                }
            }
            if (RowAllHit(h, pt)) return 0;
            BuildRight();
            for (v = 0; v < g_rightRows; v++) {
                int ci = g_right[v].cargo, k;
                if (ci < 0) continue;      // 담아 두기만 한 줄엔 이 단추를 안 그린다([비움]뿐)
                for (k = 0; k < STEP_N; k++) {
                    RECT s = RcStepBtn(1, v, k);
                    if (!PtInRect(&s, pt)) continue;
                    SellAdd(ci, kStepVal[k]);     // 실은 것 — 팔 것을 그만큼 담는다
                    InvalidateRect(h, NULL, FALSE);
                    return 0;
                }
            }
        }
        { RECT a = RcApply(); if (PtInRect(&a, pt)) { CartDecide(h); return 0; } }
        // [비우기] — 알림은 안 띄운다. 담은 것이 화면에서 사라지는 것이 곧 답이다.
        { RECT c = RcClear(); if (PtInRect(&c, pt)) { CartClear(); Say(L"", 0);
                                                     InvalidateRect(h, NULL, FALSE); return 0; } }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        if (g_barOn) {                    // 판이 떠 있는 동안은 판이 키를 먹는다
            if (w >= '1' && w <= '3')  { BargainPick(h, (int)(w - '1')); return 0; }
            if (w == VK_RETURN)        { BargainPick(h, 0); return 0; }
            if (w == VK_ESCAPE)        { BargainPick(h, 2); return 0; }
            return 0;
        }
        if (w == VK_RETURN) { CartDecide(h); return 0; }
        if (w == VK_F5)     { Say(L"", 0); CartClear(); BarReset(); Reload(h); return 0; }
        if (w == VK_ESCAPE) { if (!BargainQuit(h)) ShowWindow(h, SW_HIDE); return 0; }
        return 0;
    case WM_CLOSE: if (!BargainQuit(h)) ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY:
        UI_DestroyFonts();
        if (g_priceFont) { DeleteObject(g_priceFont); g_priceFont = NULL; }
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
        g_msg[0] = 0;         // 지난번 [결정] 자국은 지우고 연다
        CartClear();
        BarReset();           // 흥정은 거래 한 판마다 처음부터다
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
