#include "invview.h"
#include "inventory.h"
#include "ui.h"
#include "itemdb.h"        // exe 안 아이템 표 — 그림 번호 · 분류 · 설명문
#include "itempic.h"       // ITEM.CDS 그림
#include "fleet.h"         // 함대 피로도 · 선원 수 — 아래 kItemUse 가 쓴다
#include "askbox.h"        // 게임 다이얼로그 모양의 [YES][NO] 판
#include "livechar.h"      // 부관이 누구인지 — 판에 그 얼굴을 넣는다
#include "faces.h"         // 그 얼굴 그림
#include "item_names.h"    // TradeUtilKR/src — kItemNames[286]

// 칸마다 한 줄. 소지 16칸을 먼저 늘어놓고 보관 99칸이 이어진다.
// 자식 컨트롤은 안 쓴다(게임 DirectDraw 화면 위에서 불안정) — 목록은 직접 그린다.

// 도감처럼 두 열로 늘어놓는다. 한 칸에 그림 80x80 + 이름 + 단추.
// 창 높이가 634 이고 목록이 121 에서 시작하므로 92x5=460 줄까지 들어간다(= 한 판에 10칸).
#define IV_COLS    2
#define IV_CELL_W  ((Q_W - 6) / IV_COLS)      // 358
#define IV_CELL_H  92
#define IV_ROWS    5                          // 보이는 줄 수(칸 수는 IV_ROWS * IV_COLS)
#define IV_PAGE    (IV_ROWS * IV_COLS)
#define IV_PIC     80                         // 칸 그림. 원본 120x120 을 줄여 그린다
#define IV_Y       (Q_Y + 30)                 // 위 한 줄은 소지금
#define IV_LIST_H  (IV_CELL_H * IV_ROWS)
#define IV_N       (INV_HELD_N + INV_STORE_N)
#define IV_LINES   ((IV_N + IV_COLS - 1) / IV_COLS)   // 전체 줄 수(58)

#define DD_ITEM_H  22
#define DD_COLS    4
#define DD_ROWS    12
#define DD_ITEM_W  150
#define DD_N       (INV_ITEM_N + 1)           // 맨 앞이 "(비움)"

// [정보] 판. 목록 위에 덮어 그린다(questview 의 상세 판과 같은 방식).
#define IP_W 520
#define IP_H 210
#define IP_X ((WIN_W - IP_W) / 2)
#define IP_Y (IV_Y + 40)

// 정보 판에서 바로 써먹을 수 있는 아이템. 여기 적힌 것만 판에 단추가 하나 더 붙는다.
// 아이템을 없애지는 않는다 — 몇 번이고 누를 수 있다. 대신 값을 치른다:
// 피로를 더는 것은 선원들에게 금화를 돌리는 일이라, 한 사람당 coin 닢씩 소지금에서 나간다.
static const struct { int item; int fatigue; int coin; const wchar_t* label; } kItemUse[] = {
    { 34, 20, 100, L"피로 20 줄이기" },      // 육분의 — 선원 한 사람에 100닢
};
#define ITEM_USE_N ((int)(sizeof(kItemUse)/sizeof(kItemUse[0])))

static int  g_tab = 0;          // 0 = 소지품(16칸), 1 = 보관함(99칸)
static int  g_scroll = 0;
static int  g_open = -1;        // 아이템 목록을 펼친 칸 번호. -1 = 안 펼침
static int  g_info = -1;        // [정보] 판에 띄운 아이템 번호. -1 = 안 띄움
static wchar_t g_useMsg[160] = L"";  // 판 안에서 단추를 누른 결과 한 줄
static int  g_ask = -1;         // 부관에게 묻는 중인 kItemUse 번호. -1 = 안 묻는 중
static int  g_ddScroll = 0;
static RECT g_ddRc;
static wchar_t g_msg[128] = L"";
static int  g_msgWarn = 0;      // g_msg 가 안 된 일을 알리는 말인가(빨간 글씨)

// 세 자리마다 쉼표. 한 줄에 여러 번 쓸 수 있게 버퍼를 넷 돌려 쓴다.
static const wchar_t* Comma(int v)
{
    static wchar_t ring[4][24];
    static int k = 0;
    wchar_t raw[16], *out = ring[k = (k + 1) % 4];
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

static const wchar_t* ItemName(int id)
{
    return (id >= 0 && id < (int)(sizeof(kItemNames)/sizeof(kItemNames[0]))) ? kItemNames[id] : L"?";
}

// 목록 i번째 항목이 나타내는 아이템 번호. 0번은 "(비움)".
static int DropValue(int i) { return i - 1; }

// [소지품] [보관함] — 새로고침 단추가 있던 자리다(그 단추는 뺐다. 창을 열 때마다 새로 읽는다).
static RECT RcTab(int k)
{ RECT r; r.left=FRAME+8+k*94; r.right=r.left+88; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcInfo(void)
{ RECT r; r.left=FRAME+200; r.right=WIN_W-FRAME-8; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcMoney(void)
{ RECT r; r.left=Q_X+70; r.right=r.left+140; r.top=Q_Y+2; r.bottom=r.top+22; return r; }
// vis = 화면에 보이는 칸 번호(0 ~ IV_PAGE-1). 왼쪽 위부터 오른쪽으로 채운다.
static RECT RcCell(int vis)
{
    RECT r;
    r.left = Q_X + (vis % IV_COLS) * (IV_CELL_W + 6);
    r.right = r.left + IV_CELL_W;
    r.top = IV_Y + (vis / IV_COLS) * IV_CELL_H;
    r.bottom = r.top + IV_CELL_H - 4;
    return r;
}
static RECT RcThumb(int vis)
{ RECT c = RcCell(vis); RECT r; r.left=c.left+5; r.right=r.left+IV_PIC; r.top=c.top+4; r.bottom=r.top+IV_PIC; return r; }
// 아이템칸(펼치는 목록)은 빈 칸에만 둔다 — 든 칸은 이름을 그대로 적고,
// 바꾸려면 [판매] 로 비운 뒤 고르면 된다.
static RECT RcSlot(int vis)
{ RECT c = RcCell(vis); RECT r; r.left=c.left+IV_PIC+14; r.right=c.right-8; r.top=c.top+10; r.bottom=r.top+22; return r; }
static RECT RcSell(int vis)
{ RECT c = RcCell(vis); RECT r; r.left=c.left+IV_PIC+14; r.right=r.left+52; r.top=c.top+56; r.bottom=r.top+22; return r; }
static RECT RcDetail(int vis)
{ RECT c = RcCell(vis); RECT r; r.left=c.left+IV_PIC+72; r.right=r.left+52; r.top=c.top+56; r.bottom=r.top+22; return r; }
// [정보] 오른쪽 — 소지 <-> 보관 옮기기.
static RECT RcMove(int vis)
{ RECT c = RcCell(vis); RECT r; r.left=c.left+IV_PIC+130; r.right=r.left+86; r.top=c.top+56; r.bottom=r.top+22; return r; }
static RECT RcItemPanel(void)
{ RECT r; r.left=IP_X; r.right=IP_X+IP_W; r.top=IP_Y; r.bottom=IP_Y+IP_H; return r; }
static RECT RcItemClose(void)
{ RECT r; r.right=IP_X+IP_W-6; r.left=r.right-22; r.top=IP_Y+4; r.bottom=r.top+18; return r; }
static RECT RcItemUse(void)
{ RECT r; r.right=IP_X+IP_W-16; r.left=r.right-132; r.bottom=IP_Y+IP_H-10; r.top=r.bottom-26; return r; }

// 이 아이템이 kItemUse 에 있나. 없으면 -1.
static int UseOf(int item)
{
    int i;
    for (i = 0; i < ITEM_USE_N; i++) if (kItemUse[i].item == item) return i;
    return -1;
}
static RECT RcTrack(void)
{ RECT r; r.right=WIN_W-FRAME-2; r.left=r.right-SB_W; r.top=IV_Y; r.bottom=IV_Y+IV_LIST_H; return r; }

static int KindOf(int i)   { return i < INV_HELD_N ? INV_HELD : INV_STORE; }
static int IndexOf(int i)  { return i < INV_HELD_N ? i : i - INV_HELD_N; }

// 소지품 · 보관함을 탭으로 가른다. 예전에는 16칸 뒤에 99칸이 그냥 이어져 있어서
// 어디까지가 소지품인지 스크롤을 세어 봐야 알았다.
static int SlotBase(void)  { return g_tab ? INV_HELD_N : 0; }
static int SlotCount(void) { return g_tab ? INV_STORE_N : INV_HELD_N; }
static int TabLines(void)  { return (SlotCount() + IV_COLS - 1) / IV_COLS; }
static int MaxScroll(void) { int m = TabLines() - IV_ROWS; return m > 0 ? m : 0; }   // 줄 단위

// 소지 <-> 보관 옮기기. 받는 쪽 첫 빈 칸에 넣고 원래 칸을 비운다.
// 게임 메모리에 바로 쓰므로 창을 닫아도 그대로다(세이브 파일은 안 건드린다).
static void MoveSlot(int slot)
{
    int from = KindOf(slot), fi = IndexOf(slot);
    int to   = (from == INV_HELD) ? INV_STORE : INV_HELD;
    int item = Inv_Get(from, fi), ti;

    if (item < 0) return;
    ti = Inv_FirstEmpty(to);
    if (ti < 0) {
        wsprintfW(g_msg, L"%s이 꽉 찼습니다.", to == INV_STORE ? L"보관함" : L"소지품");
        g_msgWarn = 1;
        return;
    }
    if (!Inv_Set(to, ti, item) ) { lstrcpyW(g_msg, L"옮기지 못했습니다."); g_msgWarn = 1; return; }
    Inv_Set(from, fi, INV_EMPTY);
    wsprintfW(g_msg, L"%s → %s %d칸", ItemName(item), to == INV_STORE ? L"보관" : L"소지", ti + 1);
    g_msgWarn = 0;
}

// 판다. 값은 아이템 표의 값B — 286개를 훑어 보면 대개 값A(사는 값)의 절반쯤이고,
// 향수·상아처럼 교역품에 가까운 것만 값A보다 비싸다. 게임 상점 시세와는 별개로
// 이 표의 값을 그대로 쳐서 소지금에 더한다.
static void SellSlot(int slot)
{
    int kind = KindOf(slot), idx = IndexOf(slot);
    int item = Inv_Get(kind, idx), money, price;
    const ItemRec* rec;

    if (item < 0) return;
    rec = ItemDb_At(item);
    if (!rec) {
        lstrcpyW(g_msg, L"아이템 표를 못 찾아 값을 모릅니다 — 팔지 않았습니다.");
        g_msgWarn = 1;
        return;
    }
    price = rec->valB;
    money = Inv_Money();
    if (money >= 0 && price > 0) {
        double sum = (double)money + price;      // 9999만을 넘기지 않게
        Inv_SetMoney(sum > INV_MONEY_MAX ? INV_MONEY_MAX : money + price);
    }
    Inv_Set(kind, idx, INV_EMPTY);
    wsprintfW(g_msg, L"%s 팔았습니다 — %d", ItemName(item), price);
    g_msgWarn = 0;
}

static void OpenDrop(int slot, RECT anchor)
{
    int w = DD_COLS * DD_ITEM_W, h = DD_ROWS * DD_ITEM_H;
    RECT p;
    int cur = Inv_Get(KindOf(slot), IndexOf(slot));
    g_info = -1;                    // 목록과 정보 판이 같이 뜨는 일은 없게 한다
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

// 말하는 사람은 부관이다. 자리가 비었으면 -1 이 나가 판에 초상화가 빠진다.
static void DeputyFace(int* gender, int* face)
{
    int dep;
    LiveChar_Load();
    Face_Load();
    dep = LiveChar_Crew(0);
    *gender = dep >= 0 ? LiveChar_Gender(dep) : -1;
    *face   = dep >= 0 ? LiveChar_Face(dep)   : -1;
}

// 판에 쓴 글을 판 아래 한 줄로도 남긴다 — 판을 닫은 뒤에 무슨 일이 있었는지 보이게.
// 판에서는 줄을 나눠 놓았으니 한 줄로 이어 붙인다.
static void Echo(const wchar_t* s)
{
    int i = 0;
    while (*s && i < 159) { g_useMsg[i++] = (*s == L'\n') ? L' ' : *s; s++; }
    g_useMsg[i] = 0;
}

// 부관이 한마디 하고 [확인] 하나로 닫는 판.
static void Say(const wchar_t* msg)
{
    int gender, face;
    DeputyFace(&gender, &face);
    Echo(msg);
    Ask_Info(msg, gender, face);
}

// 부관 말이 아니라 이쪽 사정(자리를 못 읽었다 같은)일 때. 초상화 없이 글만 띄운다.
static void Warn(const wchar_t* msg)
{
    Echo(msg);
    Ask_Info(msg, -1, -1);
}

// 이 아이템을 쓰면 얼마가 드나. 선원 수를 못 읽으면 -1.
static int UseCost(int u)
{
    int crew = Fleet_Crew();
    if (crew < 0) return -1;
    return crew * kItemUse[u].coin;
}

// kItemUse[u] 를 써먹기 전에 부관이 값을 알리고 물어 본다.
// 답을 기다리는 동안은 g_ask 에 무엇을 묻는지 담아 두고, 판이 닫히면 UseCommit 이 치른다.
static void UseItem(int u)
{
    int cur = Fleet_Fatigue(), crew, cost, money, gender, face;
    wchar_t ask[320];

    if (cur < 0) {
        Warn(L"피로도를 읽지 못했습니다.\n세이브를 불러온 뒤에 눌러 주세요.");
        return;
    }
    if (cur == 0) { Say(L"지금은 금화를 분배하지 않아도 됩니다."); return; }

    crew = Fleet_Crew();
    if (crew < 0) {
        Warn(L"선원 수를 읽지 못했습니다.\n세이브를 불러온 뒤에 눌러 주세요.");
        return;
    }
    cost = crew * kItemUse[u].coin;

    money = Inv_Money();
    if (money < 0) { Warn(L"소지금을 읽지 못했습니다."); return; }
    if (money < cost) {
        wsprintfW(ask, L"소지금 %s닢으로는\n%s닢을 못 냅니다.", Comma(money), Comma(cost));
        Say(ask);
        return;
    }

    DeputyFace(&gender, &face);
    // 셈을 다 늘어놓지 않는다 — 드는 값과 물음만 있으면 된다.
    wsprintfW(ask, L"선원들에게 한사람당 %d닢씩 나누어 주려면 %s닢이 듭니다.\n"
                   L"금화를 나눠 주시겠습니까?",
              kItemUse[u].coin, Comma(cost));
    Ask_Open(ask, gender, face);
    g_ask = u;
    g_useMsg[0] = 0;
}

// 부관에게 그러라고 한 뒤. 물을 때 본 값이 그 사이 달라졌을 수 있으니 다시 읽어 확인한다.
static void UseCommit(int u)
{
    int cur = Fleet_Fatigue(), crew = Fleet_Crew(), money = Inv_Money(), cost, next;
    wchar_t msg[192];

    if (cur < 0 || crew < 0 || money < 0) {
        Warn(L"값을 다시 읽지 못했습니다.\n그대로 두었습니다.");
        return;
    }
    cost = crew * kItemUse[u].coin;
    if (money < cost) {
        wsprintfW(msg, L"소지금 %s닢으로는\n%s닢을 못 냅니다.", Comma(money), Comma(cost));
        Say(msg);
        return;
    }

    next = cur - kItemUse[u].fatigue;
    if (next < 0) next = 0;
    if (!Inv_SetMoney(money - cost)) { Warn(L"소지금을 쓰지 못했습니다."); return; }
    if (!Fleet_SetFatigue(next)) {
        Inv_SetMoney(money);            // 피로도를 못 고쳤으면 낸 돈도 되돌린다
        Warn(L"피로도를 쓰지 못했습니다.");
        return;
    }
    // 판에는 한 줄만. 얼마를 쓰고 피로도가 어떻게 됐는지는 판을 닫은 뒤
    // 정보 판 아래 줄에 남는다(Say 가 적어 둔 것을 여기서 자세한 쪽으로 바꾼다).
    Say(L"금화를 나눠 주어 선원들이 기뻐합니다!");
    wsprintfW(msg, L"%s닢 씀 · 피로도 %d → %d", Comma(cost), cur, next);
    Echo(msg);
}

// 판에서 나온 답을 받는다. 1 = 예 / 0 = 아니오 / -1 = 아직.
static void AskAnswer(int answer)
{
    int u = g_ask;
    if (answer < 0) return;
    g_ask = -1;
    if (u < 0 || u >= ITEM_USE_N) return;
    if (answer) UseCommit(u);
    else        Say(L"알겠습니다. 다음 기회에 하지요.");
}

static void OpenItemInfo(int item)
{
    g_open = -1;
    g_useMsg[0] = 0;
    ItemDb_Load();      // 둘 다 여러 번 불러도 되고, 처음 한 번만 실제로 일한다
    ItemPic_Load();
    g_info = item;
}

// 그림 한 장 + 설명 한 토막. 바깥 상태는 안 본다 — 아이템 번호만 받는다.
static void PaintItemPanel(HDC dc, int itemId)
{
    const ItemRec* rec = ItemDb_At(itemId);
    const wchar_t* desc = ItemDb_Desc(itemId);
    RECT p = RcItemPanel(), r;
    HBRUSH br;
    wchar_t buf[160];
    int px = IP_X + 16, py = IP_Y + 38;      // 그림 자리
    int tx = IP_X + 152, tw = IP_X + IP_W - 16;

    UI_VGradient(dc, p, COL_FACE_TOP, COL_FACE_BOT);
    UI_Bevel(dc, p, FALSE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &p, br); DeleteObject(br);

    r = p; r.left += 10; r.bottom = r.top + 26;
    UI_Text(dc, r, L"아이템 정보", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    UI_Button(dc, RcItemClose(), L"×", FALSE);

    // 액자와 테두리는 여기서 그린다 — itempic.c 는 그림만 찍는다(TradeUtilKR 도 같이 쓴다).
    r.left = px; r.right = px + ITEMPIC_W; r.top = py; r.bottom = py + ITEMPIC_H;
    if (!ItemPic_Draw(dc, px, py, ITEMPIC_W, ITEMPIC_H, rec ? rec->pic : -1)) {
        br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &r, br); DeleteObject(br);
        UI_Bevel(dc, r, TRUE);
        // UI_Text 는 DrawTextW 한 줄짜리라 DT_VCENTER 는 DT_SINGLELINE 과만 먹는다.
        UI_Text(dc, r, ItemPic_Count() > 0 ? L"그림 없음" : L"ITEM.CDS 없음",
                g_smallFont, COL_DARK, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    { RECT box = r; InflateRect(&box, 1, 1);
      br = CreateSolidBrush(COL_DARK); FrameRect(dc, &box, br); DeleteObject(br); }

    r.left = tx; r.right = tw; r.top = IP_Y + 36; r.bottom = r.top + 24;
    wsprintfW(buf, L"%d  %s", itemId, ItemName(itemId));
    UI_Text(dc, r, buf, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    r.top = r.bottom; r.bottom = r.top + 20;
    if (rec) wsprintfW(buf, L"%s · 값 %d / %d", ItemDb_CatName(rec->cat), rec->valA, rec->valB);
    else     lstrcpyW(buf, L"이 실행 파일에서는 아이템 표를 찾지 못했습니다(다른 판인 듯합니다).");
    UI_Text(dc, r, buf, g_smallFont, COL_DARK, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    if (desc) {
        r.top = IP_Y + 84; r.bottom = IP_Y + IP_H - 42;
        UI_Text(dc, r, desc, g_smallFont, COL_TEXT,
                DT_LEFT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
    }

    { int u = UseOf(itemId);
      RECT bottom;
      bottom.left = IP_X + 16; bottom.top = IP_Y + IP_H - 34; bottom.bottom = bottom.top + 24;
      bottom.right = IP_X + IP_W - 16;
      if (u >= 0) {
          RECT b = RcItemUse();
          UI_Button(dc, b, kItemUse[u].label, FALSE);
          bottom.right = b.left - 8;       // 안내문이 단추를 파고들지 않게
      }
      if (g_useMsg[0])
          UI_Text(dc, bottom, g_useMsg, g_smallFont, COL_TEXT,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
      else if (u >= 0) {
          // 누르기 전에 값을 먼저 보여 준다 — 선원이 늘면 값도 같이 오른다.
          int cost = UseCost(u);
          wchar_t hint[160];
          if (cost >= 0) wsprintfW(hint, L"선원 한 사람에 %d닢 — 지금 나눠 주면 %s닢이 듭니다.",
                                   kItemUse[u].coin, Comma(cost));
          else           lstrcpyW(hint, L"선원 수를 아직 못 읽었습니다(세이브를 불러오면 값이 나옵니다).");
          UI_Text(dc, bottom, hint, g_smallFont, cost >= 0 ? COL_DARK : COL_WARN_TX,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
      }
      else if (ItemPic_Count() <= 0)
          UI_Text(dc, bottom, L"게임 폴더의 ITEM.CDS 를 열지 못해 그림은 못 보여 줍니다.",
                  g_smallFont, COL_WARN_TX, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
}

void Inv_Paint(HDC dc)
{
    wchar_t buf[160];
    int v;

    UI_Button(dc, RcTab(0), L"소지품", g_tab == 0);
    UI_Button(dc, RcTab(1), L"보관함", g_tab == 1);

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
        UI_Text(dc, e, why, g_font, COL_GAME_TX, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        return;
    }

    wsprintfW(buf, L"소지 %d/%d칸 · 보관 %d/%d칸 · 값은 게임 메모리에 바로 들어갑니다",
              Inv_Used(INV_HELD), INV_HELD_N, Inv_Used(INV_STORE), INV_STORE_N);
    UI_Text(dc, RcInfo(), buf, g_smallFont, COL_GAME_TX, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    // 소지금 — 보여 주기만 한다. 더하고 빼던 [◀◀][◀][▶][▶▶] 는 뺐다.
    { RECT l; l.left = Q_X; l.right = Q_X + 62; l.top = Q_Y + 2; l.bottom = l.top + 22;
      UI_Text(dc, l, L"소지금", g_font, COL_GAME_TX, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
    v = Inv_Money();
    { RECT m = RcMoney();
      HBRUSH mb = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &m, mb); DeleteObject(mb);
      UI_Bevel(dc, m, TRUE);
      UI_Text(dc, m, Comma(v), g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
    if (g_msg[0]) {
        RECT m; m.left = Q_X + 350; m.right = Q_X + Q_W; m.top = Q_Y + 2; m.bottom = m.top + 22;
        UI_Text(dc, m, g_msg, g_smallFont, g_msgWarn ? COL_WARN_TX : COL_GAME_TX,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    // 칸 — 두 열, 한 칸에 그림 + 이름 + 단추
    { int v;
      for (v = 0; v < IV_PAGE; v++) {
        int local = g_scroll * IV_COLS + v, i = SlotBase() + local, item;
        RECT cell, tb, box, t;
        wchar_t name[96];
        HBRUSH br;
        const ItemRec* rec;
        if (local >= SlotCount()) break;

        cell = RcCell(v);
        br = CreateSolidBrush(((v / IV_COLS) & 1) ? COL_ROW_ALT : COL_DISP_BG);
        FillRect(dc, &cell, br); DeleteObject(br);

        item = Inv_Get(KindOf(i), IndexOf(i));
        rec  = item >= 0 ? ItemDb_At(item) : NULL;

        // 그림. 빈 칸이나 그림 없는 아이템도 액자는 그대로 둔다 —
        // 있다 없다 하면 칸이 들쭉날쭉해 보인다.
        tb = RcThumb(v); box = tb;
        if (!ItemPic_Draw(dc, tb.left, tb.top, IV_PIC, IV_PIC, rec ? rec->pic : -1)) {
            br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &tb, br); DeleteObject(br);
            UI_Bevel(dc, tb, TRUE);
        }
        InflateRect(&box, 1, 1);
        br = CreateSolidBrush(COL_DARK); FrameRect(dc, &box, br); DeleteObject(br);

        if (item < 0) {
            // 빈 칸에만 목록을 편다 — 여기서 고르면 그 아이템이 생긴다.
            UI_Select(dc, RcSlot(v), L"(비움)", g_open == i);
            continue;
        }

        t = RcSlot(v);
        wsprintfW(name, L"%d  %s", item, ItemName(item));
        UI_Text(dc, t, name, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        t.top += 22; t.bottom += 22;
        if (rec) wsprintfW(name, L"%s · 팔면 %d", ItemDb_CatName(rec->cat), rec->valB);
        else     lstrcpyW(name, L"아이템 표를 못 찾았습니다");
        UI_Text(dc, t, name, g_smallFont, COL_DARK, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        UI_Button(dc, RcSell(v), L"판매", FALSE);
        // 그림이 없는 아이템에도 그대로 둔다 — 있다 없다 하면 칸이 들쭉날쭉해진다.
        UI_Button(dc, RcDetail(v), L"정보", FALSE);
        UI_Button(dc, RcMove(v), KindOf(i) == INV_HELD ? L"보관함 ▶" : L"◀ 소지품", FALSE);
      } }

    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), IV_ROWS, TabLines());
    PaintDrop(dc);
    if (g_info >= 0) PaintItemPanel(dc, g_info);
    Ask_Paint(dc);              // 묻는 판은 맨 위에 — 뜬 동안 아래는 손댈 수 없다
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
    ItemDb_Reset();     // 지난번에 표를 못 찾았어도 다시 해 본다
    ItemDb_Load();      // 줄마다 그림을 그리려면 그림 번호가 필요하다
    ItemPic_Load();
    g_open = -1; g_info = -1; g_msg[0] = 0; g_useMsg[0] = 0; g_msgWarn = 0;
    g_ask = -1; Ask_Close();        // 탭을 옮기면 묻던 것은 없던 일이 된다
    if (g_scroll > MaxScroll()) g_scroll = MaxScroll();
    if (h) InvalidateRect(h, NULL, FALSE);
}

int Inv_Click(HWND h, POINT pt)
{
    RECT r;
    int i;

    // 부관이 묻는 중이면 그 판이 다 먹는다(모달). 답이 나면 그때 값을 치른다.
    { int answer;
      if (Ask_Click(pt, &answer)) {
          AskAnswer(answer);
          InvalidateRect(h, NULL, FALSE);
          return 1;
      } }

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

    // 정보 판이 떠 있으면 그 다음. × 나 판 바깥을 누르면 닫고, 판 안은 삼킨다.
    if (g_info >= 0) {
        RECT p = RcItemPanel(), x = RcItemClose(), b = RcItemUse();
        int u = UseOf(g_info);
        if (PtInRect(&x, pt) || !PtInRect(&p, pt)) { g_info = -1; g_useMsg[0] = 0; }
        else if (u >= 0 && PtInRect(&b, pt))       UseItem(u);
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    for (i = 0; i < 2; i++) {          // [소지품] [보관함]
        r = RcTab(i);
        if (!PtInRect(&r, pt)) continue;
        if (g_tab != i) { g_tab = i; g_scroll = 0; g_open = -1; g_msg[0] = 0; }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    if (!Inv_Ready()) return 0;

    r = RcTrack();
    if (PtInRect(&r, pt)) {
        int mid = (r.top + r.bottom) / 2;
        ScrollTo(h, g_scroll + (pt.y < mid ? -IV_ROWS : IV_ROWS));
        return 1;
    }

    { int v;
      for (v = 0; v < IV_PAGE; v++) {
        int local = g_scroll * IV_COLS + v, slot = SlotBase() + local, item;
        if (local >= SlotCount()) break;
        item = Inv_Get(KindOf(slot), IndexOf(slot));
        if (item < 0) {                      // 빈 칸 — 목록을 펴서 아이템을 고른다
            r = RcSlot(v);
            if (PtInRect(&r, pt)) { OpenDrop(slot, r); InvalidateRect(h, NULL, FALSE); return 1; }
            continue;
        }
        r = RcSell(v);
        if (PtInRect(&r, pt)) { SellSlot(slot); InvalidateRect(h, NULL, FALSE); return 1; }
        r = RcDetail(v);
        if (PtInRect(&r, pt)) { OpenItemInfo(item); InvalidateRect(h, NULL, FALSE); return 1; }
        r = RcMove(v);
        if (PtInRect(&r, pt)) { MoveSlot(slot); InvalidateRect(h, NULL, FALSE); return 1; }
      } }
    return 0;
}

int Inv_Key(HWND h, WPARAM wp)
{
    { int answer;
      if (Ask_Key(wp, &answer)) {
          AskAnswer(answer);
          InvalidateRect(h, NULL, FALSE);
          return 1;
      } }
    if (wp == VK_ESCAPE && (g_open >= 0 || g_info >= 0)) {
        g_open = -1; g_info = -1; g_useMsg[0] = 0; InvalidateRect(h, NULL, FALSE); return 1;
    }
    // 판은 고정 자리에 그리므로 떠 있는 동안 목록이 움직이면 어긋난다.
    if (g_info >= 0) return 1;
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
    if (g_info >= 0) return;        // 판이 떠 있는 동안은 목록을 안 움직인다
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
