#include "questview.h"
#include "questdb.h"
#include "ui.h"
#include "cities_data.h"    // TradeUtilKR/src — kCities[226]
#include "goods_names.h"    // kTradeGoods[70]
#include "item_names.h"     // kItemNames[286]

// 목록 + 값 편집 패널. 대사 편집과 퀘스트 추가는 여기 없다 — 길이가 변하면 파트 머리의
// (조건오프셋, 본문오프셋) 표를 다시 계산해야 해서 별건이다.
//
// 자식 컨트롤(EDIT/COMBOBOX)은 게임 DirectDraw 화면 위에서 불안정해서 navview·character 와
// 같은 방식으로 전부 직접 그린다. 숫자는 텍스트칸 대신 ± 버튼으로 넣는다.

static int g_scroll = 0;
static int g_loaded = 0;
static int g_sel  = -1;     // 편집 중인 퀘스트. -1 = 목록만
static int g_mode = 0;      // 패널 내용: 0 = 값 편집, 1 = 대사 보기
static int g_lnScroll = 0;
static int g_drop = -1;     // 펼쳐진 목록의 필드 번호. -1 = 없음
static int g_ddScroll = 0;
static RECT g_ddRc;
static wchar_t g_msg[192] = L"";
static int     g_saidRebased = 0;   // 원본 다시 잡았다는 알림을 이미 띄웠나
static int     g_ptrOpen = 0;       // 진행 포인터 고르는 격자가 펼쳐져 있나
static RECT    g_ptrRc;

// 진행 포인터 고르기 — 0~64. 퀘스트패치가 120파트라 앞쪽 절반이면 손으로 옮겨 볼
// 만한 자리는 다 든다. 65칸이 9x8 격자에 한 번에 들어가 스크롤이 필요 없다.
#define PTR_MAX    64
#define PTR_COLS   9
#define PTR_ROWS   8
#define PTR_ITEM_W 40

#define PANEL_X   (Q_X + 30)
#define PANEL_W   (Q_W - 60)
#define PANEL_Y   (Q_Y + 8)
#define PANEL_H   (Q_LIST_H - 16)
#define ROW_H     30
#define ROW0_Y    (PANEL_Y + 34)
#define LBL_W     106
#define CTRL_X    (PANEL_X + 126)
#define SEL_W     236
#define VAL_W     110
#define DD_ITEM_H 22
// 대사 보기
#define LN_H      18
#define LN_ROWS   19
#define LN_Y      (PANEL_Y + 34)

static RECT RcReload(void)
{ RECT r; r.left=FRAME+8; r.right=r.left+66; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcPtr(void)
{ RECT r; r.left=FRAME+82; r.right=r.left+108; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcInfo(void)
{ RECT r; r.left=RcPtr().right+8; r.right=WIN_W-FRAME-8; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcTrack(void)
{ RECT r; r.right=WIN_W-FRAME-2; r.left=r.right-SB_W; r.top=Q_Y; r.bottom=Q_Y+Q_LIST_H; return r; }
static RECT RcPanel(void)
{ RECT r; r.left=PANEL_X; r.right=PANEL_X+PANEL_W; r.top=PANEL_Y; r.bottom=PANEL_Y+PANEL_H; return r; }

static int MaxScroll(void)
{
    int m = Quest_Count() - Q_ROWS;
    return m > 0 ? m : 0;
}

// ---- 이름표 ----

#define CITY_N ((int)(sizeof(kCities)/sizeof(kCities[0])))
#define GOODS_N ((int)(sizeof(kTradeGoods)/sizeof(kTradeGoods[0])))
#define ITEM_N ((int)(sizeof(kItemNames)/sizeof(kItemNames[0])))

static const wchar_t* CityName(int id)  { return (id >= 0 && id < CITY_N)  ? kCities[id].name : L"?"; }
static const wchar_t* GoodsName(int id) { return (id >= 0 && id < GOODS_N) ? kTradeGoods[id] : L"?"; }
static const wchar_t* ItemName(int id)  { return (id >= 0 && id < ITEM_N)  ? kItemNames[id]  : L"?"; }

// 실제 파일에 쓰이는 기한 값들 + 쓸 만한 몇 개.
static const int kDays[] = { 30, 60, 91, 122, 182, 183, 270, 365, 548, 730, 1095, 1460, 1825 };
#define DAYS_N ((int)(sizeof(kDays)/sizeof(kDays[0])))
#define YEAR_MIN 1480
#define YEAR_N   81                       // 1480~1560

// 필드별 성질. sel = 목록에서 고름, 아니면 ± 로 값 조절.
static const struct {
    int f; const wchar_t* label; int sel; int step1, step2, vmax;
} kRow[] = {
    { QF_CITY,  L"도시",      1, 0,    0,     0      },
    { QF_BLDG,  L"건물",      1, 0,    0,     0      },
    { QF_YEAR,  L"개방 연도",  1, 0,    0,     0      },
    { QF_FAME,  L"개방 명성",  0, 100,  1000,  65535  },
    { QF_DAYS,  L"기한",      1, 0,    0,     0      },
    { QF_ADV,   L"선금",      0, 500,  5000,  999999 },
    { QF_REW,   L"보수",      0, 500,  5000,  999999 },
    { QF_PAY,   L"내가 내는 돈", 0, 100, 1000,  999999 },
    { QF_FGAIN, L"명성 보상",  0, 10,   100,   65535  },
    { QF_ITEM,  L"요구 아이템", 1, 0,    0,     0      },
    { QF_GORG,  L"요구 산지",  1, 0,    0,     0      },
    { QF_GOODS, L"요구 교역품", 1, 0,    0,     0      },
    { QF_GQTY,  L"요구 수량",  0, 5,    50,    9999   },
};
#define ROW_N ((int)(sizeof(kRow)/sizeof(kRow[0])))

// 펼침 목록의 격자. cols x rows 가 한 화면, n 이 전체 개수.
static void DropSpec(int f, int* cols, int* rows, int* itemW, int* n)
{
    switch (f) {
    case QF_CITY: case QF_GORG: *cols=4; *rows=12; *itemW=118; *n=CITY_N;  break;
    case QF_ITEM:               *cols=4; *rows=12; *itemW=140; *n=ITEM_N;  break;
    case QF_GOODS:              *cols=5; *rows=14; *itemW=88;  *n=GOODS_N; break;
    case QF_BLDG:               *cols=2; *rows=8;  *itemW=100; *n=16;      break;
    case QF_YEAR:               *cols=9; *rows=9;  *itemW=48;  *n=YEAR_N;  break;
    case QF_DAYS:               *cols=3; *rows=5;  *itemW=70;  *n=DAYS_N;  break;
    default:                    *cols=1; *rows=1;  *itemW=60;  *n=0;       break;
    }
}

// 목록 i번째 항목이 나타내는 실제 값.
static int DropValue(int f, int i)
{
    if (f == QF_YEAR) return YEAR_MIN + i;
    if (f == QF_DAYS) return (i >= 0 && i < DAYS_N) ? kDays[i] : 0;
    return i;
}
static int DropIndexOf(int f, int v)
{
    int i;
    if (f == QF_YEAR) return v - YEAR_MIN;
    if (f == QF_DAYS) { for (i = 0; i < DAYS_N; i++) if (kDays[i] == v) return i; return -1; }
    return v;
}
static void DropText(int f, int i, wchar_t* out)
{
    int v = DropValue(f, i);
    switch (f) {
    case QF_CITY: case QF_GORG: wsprintfW(out, L"%s", CityName(v)); break;
    case QF_ITEM:  wsprintfW(out, L"%s", ItemName(v)); break;
    case QF_GOODS: wsprintfW(out, L"%s", GoodsName(v)); break;
    case QF_BLDG:  wsprintfW(out, L"%s", Quest_BuildingName(v)); break;
    case QF_YEAR:  wsprintfW(out, L"%d", v); break;
    case QF_DAYS:  wsprintfW(out, L"%d일", v); break;
    default:       wsprintfW(out, L"%d", v); break;
    }
}
// 현재 값을 상자에 보여줄 문자열로.
static void ValueText(int f, int v, wchar_t* out)
{
    if (v < 0) { lstrcpyW(out, L"-"); return; }
    switch (f) {
    case QF_CITY: case QF_GORG: lstrcpyW(out, CityName(v)); break;
    case QF_ITEM:  lstrcpyW(out, ItemName(v)); break;
    case QF_GOODS: lstrcpyW(out, GoodsName(v)); break;
    case QF_BLDG:  lstrcpyW(out, Quest_BuildingName(v)); break;
    case QF_YEAR:  wsprintfW(out, L"%d년", v); break;
    case QF_DAYS:  wsprintfW(out, L"%d일", v); break;
    default:       wsprintfW(out, L"%d", v); break;
    }
}

// ---- 목록 ----

static const struct { const wchar_t* label; COLORREF bg, tx; } kState[5] = {
    { L"잠김",    RGB(170,155,132), RGB( 95, 82, 66) },   // QUEST_LOCKED
    { L"조건대기", RGB(178,160,136), RGB( 70, 55, 40) },   // QUEST_WAIT
    { L"수령가능", RGB(206,168, 96), RGB( 45, 32, 16) },   // QUEST_READY
    { L"진행중",  RGB(180, 90, 45), RGB(250,238,220) },   // QUEST_ACTIVE
    { L"완료",    RGB( 88,120, 78), RGB(235,240,225) },   // QUEST_DONE
};

static void CondText(const QuestInfo* q, wchar_t* out, int cap)
{
    wchar_t y[32], f[32];
    out[0] = 0;
    if (q->v[QF_YEAR] > 0) wsprintfW(y, L"%d년~", q->v[QF_YEAR]); else y[0] = 0;
    if (q->v[QF_FAME] > 0) wsprintfW(f, L"명성 %d", q->v[QF_FAME]); else f[0] = 0;
    if (y[0] && f[0]) wsprintfW(out, L"%s %s %s", y, q->condAnd ? L"그리고" : L"또는", f);
    else if (y[0])    lstrcpynW(out, y, cap);
    else if (f[0])    lstrcpynW(out, f, cap);
}

static void DetailText(const QuestInfo* q, wchar_t* out, int cap)
{
    wchar_t t[256];
    out[0] = 0;
    if (q->v[QF_ITEM] >= 0)
        wsprintfW(t, L"요구 %s", ItemName(q->v[QF_ITEM]));
    else if (q->v[QF_GOODS] >= 0)
        wsprintfW(t, L"요구 %s산 %s %d", CityName(q->v[QF_GORG]), GoodsName(q->v[QF_GOODS]), q->v[QF_GQTY]);
    else
        t[0] = 0;
    if (t[0]) lstrcpynW(out, t, cap);

    if (q->v[QF_DAYS]  > 0) { wsprintfW(t, L"%s기한 %d일", out[0] ? L" · " : L"", q->v[QF_DAYS]);  lstrcatW(out, t); }
    if (q->v[QF_ADV]   > 0) { wsprintfW(t, L"%s선금 %d",   out[0] ? L" · " : L"", q->v[QF_ADV]);   lstrcatW(out, t); }
    if (q->v[QF_REW]   > 0) { wsprintfW(t, L"%s보수 %d",   out[0] ? L" · " : L"", q->v[QF_REW]);   lstrcatW(out, t); }
    if (q->v[QF_PAY]   > 0) { wsprintfW(t, L"%s지출 %d",   out[0] ? L" · " : L"", q->v[QF_PAY]);   lstrcatW(out, t); }
    if (q->v[QF_FGAIN] > 0) { wsprintfW(t, L"%s명성 +%d",  out[0] ? L" · " : L"", q->v[QF_FGAIN]); lstrcatW(out, t); }
}

static void PaintRow(HDC dc, int y, int idx, const QuestInfo* q)
{
    RECT r, b;
    wchar_t line[320], t[256];
    int st = (q->state >= 0 && q->state <= 4) ? q->state : 0;
    HBRUSH br;

    r.left = Q_X; r.right = Q_X + Q_W; r.top = y; r.bottom = y + Q_ROW_H - 4;
    // 줄은 빠짐없이 칠한다. 예전에는 홀수 줄만 깔고 짝수 줄은 창 바탕이 비쳤는데,
    // 바탕이 어두워지면서 그 줄의 글자가 안 읽힌다(소지품에서 겪은 것과 같다).
    { COLORREF c = (idx == g_sel) ? COL_SEL_BG : ((idx & 1) ? COL_ROW_ALT : COL_DISP_BG);
      br = CreateSolidBrush(c); FillRect(dc, &r, br); DeleteObject(br); }
    UI_Bevel(dc, r, TRUE);

    b.left = r.left + 6; b.right = b.left + 64; b.top = r.top + 8; b.bottom = b.top + 20;
    br = CreateSolidBrush(kState[st].bg); FillRect(dc, &b, br); DeleteObject(br);
    UI_Text(dc, b, kState[st].label, g_smallFont, kState[st].tx,
            DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    r.left = b.right + 10;
    r.top = y + 3; r.bottom = r.top + 18;
    {
        // quests.json 이 손댄 퀘스트는 표가 붙는다 — 원본인지 아닌지 한눈에 보이게.
        const wchar_t* mark = q->addedFrom >= 0 ? L"＋ " : (q->edited ? L"✎ " : L"");
        // 아는 패치 파일이면 제작노트의 퀘스트 이름을 앞에 세운다 — 장소보다 이쪽이 눈에 든다.
        if (q->patch && q->v[QF_CITY] >= 0)
            wsprintfW(line, L"%s%d.  %s · %s %s", mark, idx + 1, q->patch,
                      CityName(q->v[QF_CITY]), Quest_BuildingName(q->v[QF_BLDG]));
        else if (q->patch)
            wsprintfW(line, L"%s%d.  %s", mark, idx + 1, q->patch);
        else if (q->v[QF_CITY] >= 0)
            wsprintfW(line, L"%s%d.  %s %s", mark, idx + 1,
                      CityName(q->v[QF_CITY]), Quest_BuildingName(q->v[QF_BLDG]));
        else
            wsprintfW(line, L"%s%d.  (발생 장소 미확인)", mark, idx + 1);
    }
    UI_Text(dc, r, line, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    CondText(q, t, 256);
    if (t[0]) {
        COLORREF c = (q->state == QUEST_WAIT) ? COL_WARN_TX : COL_TEXT;
        UI_Text(dc, r, t, g_smallFont, c, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    r.top = y + 21; r.bottom = r.top + 18;
    UI_Text(dc, r, q->summary, g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);

    r.top = y + 39; r.bottom = r.top + 18;
    DetailText(q, line, 320);
    UI_Text(dc, r, line, g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    if (q->state == QUEST_ACTIVE) {
        wsprintfW(t, L"남은 기한 %d일", Quest_DaysLeft());
        UI_Text(dc, r, t, g_smallFont, COL_WARN_TX, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
}

// ---- 편집 패널 ----

// 이 퀘스트에서 실제로 편집할 수 있는 줄만 추린다(요구 물품은 아이템형/교역품형 중 하나뿐).
static int VisRows(int qi, int* out)
{
    const QuestInfo* q = Quest_At(qi);
    int i, n = 0;
    if (!q) return 0;
    for (i = 0; i < ROW_N; i++) if (q->n[kRow[i].f] > 0) out[n++] = i;
    return n;
}

static RECT RcRow(int slot)
{ RECT r; r.left=PANEL_X; r.right=PANEL_X+PANEL_W; r.top=ROW0_Y+slot*ROW_H; r.bottom=r.top+ROW_H-4; return r; }
static RECT RcCtrl(int slot, int isSel)
{ RECT r; r.left=CTRL_X; r.right=CTRL_X+(isSel?SEL_W:VAL_W); r.top=ROW0_Y+slot*ROW_H+3; r.bottom=r.top+22; return r; }
// [−−][−] 값 [+][++]
static RECT RcStep(int slot, int k)
{
    static const int kx[4] = { 0, 40, 74, 108 };
    static const int kw[4] = { 36, 30, 30, 36 };
    RECT r;
    r.left  = CTRL_X + VAL_W + 8 + kx[k];
    r.right = r.left + kw[k];
    r.top   = ROW0_Y + slot * ROW_H + 3;
    r.bottom= r.top + 22;
    return r;
}
// [이걸 본떠 추가] 는 뺐다 — 퀘스트를 새로 붙이는 것은 quests.json 에 { "clone": N } 을
// 적는 쪽이 뭘 본떴는지 남고 되돌리기도 쉽다. 창에서는 있는 것을 고치기만 한다.
#define BTN_N 4
static const wchar_t* kBtn[BTN_N] = { L"저장", L"되돌리기", L"원래대로", L"닫기" };
static RECT RcBtn(int i)
{
    static const int kx[BTN_N] = { 0, 96, 192, 288 };
    static const int kw[BTN_N] = { 88, 88, 88, 88 };
    RECT r;
    r.left = PANEL_X + 14 + kx[i]; r.right = r.left + kw[i];
    r.bottom = PANEL_Y + PANEL_H - 10; r.top = r.bottom - 24;
    return r;
}

// 패널 오른쪽 위의 [값][대사] 전환 단추.
static RECT RcMode(int m)
{
    RECT r;
    r.right = PANEL_X + PANEL_W - 14 - m * 58;
    r.left = r.right - 54;
    r.top = PANEL_Y + 6; r.bottom = r.top + 22;
    return r;
}
static RECT RcLnTrack(void)
{
    RECT r;
    r.right = PANEL_X + PANEL_W - 8; r.left = r.right - SB_W;
    r.top = LN_Y; r.bottom = LN_Y + LN_ROWS * LN_H;
    return r;
}
static int LnMaxScroll(void)
{
    int m = Quest_ReadLines(g_sel) - LN_ROWS;
    return m > 0 ? m : 0;
}

// 능력치 항목 번호 -> 이름. 퀘스트 파일에 실제로 쓰이는 것만 있다(23·27은 정체 미상).
static const wchar_t* StatName(int id)
{
    switch (id) {
    case 4:  return L"악명";
    case 6:  return L"무력";
    case 17: return L"명성";
    case 18: return L"운";
    case 21: return L"지력";
    case 22: return L"매력";
    case 29: return L"기한";
    default: return L"?";
    }
}

// 줄 하나를 사람 말로. quests.json 에 쓰는 이름과 같은 낱말을 쓴다.
static void LineText(const QuestLine* l, wchar_t* out)
{
    static const wchar_t* kWhere[4] = { L"국가", L"도시", L"건물", L"지역" };
    switch (l->kind) {
    // 플래그는 그 대사가 어떤 창으로 뜨는지를 정한다(0=확인만, 11=예/아니오 …).
    // quests.json 에 세 번째 값으로 그대로 적어야 하므로 눈에 보이게 띄운다.
    case QL_TEXT: {
        wchar_t x[64];
        x[0] = 0;
        if (l->who[0]) {
            const wchar_t* kr = Quest_SpeakerKR(l->who);      // 아는 이름이면 한글로 보여준다
            wsprintfW(x, L"  ·화자 %s", kr ? kr : l->who);
        }
        if (l->a > 0)  wsprintfW(x + lstrlenW(x), L"  ·플래그 %d", l->a);
        wsprintfW(out, L"대사   \"%s\"%s", l->text, x);
        break; }
    case QL_RAW:   wsprintfW(out, L"??     %s", l->text); break;
    case QL_END:   lstrcpyW(out, L"끝"); break;
    case QL_WHERE: {
        const wchar_t* k = l->a == 0x00 ? kWhere[0] : l->a == 0x08 ? kWhere[1]
                         : l->a == 0x10 ? kWhere[2] : kWhere[3];
        if (l->a == 0x08)      wsprintfW(out, L"도시   = %s", CityName(l->b));
        else if (l->a == 0x10) wsprintfW(out, L"건물   = %s", Quest_BuildingName(l->b));
        else if (l->a == 0x19 && Quest_RegionName(l->b)[0])
                               wsprintfW(out, L"지역   = %s", Quest_RegionName(l->b));
        else                   wsprintfW(out, L"%s   = %d", k, l->b);
        break; }
    case QL_YEAR:  wsprintfW(out, L"연도   >= %d년", l->a); break;
    case QL_DISC:  wsprintfW(out, L"발견물 %s (%d)", l->a ? L"아직" : L"발견함", l->b); break;
    case QL_CMP:   wsprintfW(out, L"조건   %s >= %d", StatName(l->a), l->b); break;
    case QL_STAT:  wsprintfW(out, L"%s %s %d", StatName(l->b),
                             l->a == 0 ? L"+" : (l->a == 1 ? L"-" : L"="), l->c2); break;
    case QL_GOLD:  wsprintfW(out, L"금화   %s%d", l->a == 0 ? L"+" : L"-", l->b); break;
    case QL_ITEM:  wsprintfW(out, L"만약   %s 있으면 -> %d번", ItemName(l->a), l->c); break;
    case QL_GOODS: wsprintfW(out, L"만약   %s산 %s %d 있으면 -> %d번",
                             CityName(l->a), GoodsName(l->b), l->c2, l->c); break;
    case QL_BRANCH:wsprintfW(out, L"분기   [%s] -> %d번", l->text, l->c); break;
    case QL_FLAG:  wsprintfW(out, L"표식   [%s]", l->text); break;
    case QL_PAD:   wsprintfW(out, L"여백   %d바이트 (남은 자리)", l->a); break;
    case QL_ITEMOP: {
        static const wchar_t* kAct[4] = { L"획득", L"잃음", L"있으면", L"없으면" };
        wsprintfW(out, L"아이템 %s %s", ItemName(l->b), kAct[(l->a >= 0 && l->a < 4) ? l->a : 0]);
        break; }
    case QL_MAKE:
        if (l->a == 0) wsprintfW(out, L"만들기 신도시 %s", CityName(l->b));
        else           wsprintfW(out, L"만들기 %s 에 %s", CityName(l->b), Quest_BuildingName(l->c2));
        break;
    default:       lstrcpyW(out, L""); break;
    }
}

// "어디로 가면 무슨 일이 벌어지나" — 슬롯 머리글 + 그 아래 명령 줄.
static void PaintLines(HDC dc)
{
    static const wchar_t* kStep[3] = { L"조건", L"의뢰", L"완료·후속" };
    int n = Quest_ReadLines(g_sel), i;
    RECT r;
    int lastChunk = -1;

    for (i = 0; i < LN_ROWS; i++) {
        const QuestLine* l = Quest_LineAt(g_lnScroll + i);
        wchar_t t[224], addr[24];
        if (!l) break;
        r.left = PANEL_X + 14; r.right = PANEL_X + PANEL_W - 14 - SB_W;
        r.top = LN_Y + i * LN_H; r.bottom = r.top + LN_H;

        if (l->kind == QL_HEADER) {
            HBRUSH br = CreateSolidBrush(COL_FACE_BOT);
            RECT b = r; b.left -= 4;
            FillRect(dc, &b, br); DeleteObject(br);
            if (l->a >= 0)
                wsprintfW(t, L"▸ %s · %s %s", kStep[l->step < 2 ? l->step : 2],
                          CityName(l->a), Quest_BuildingName(l->b));
            else
                wsprintfW(t, L"▸ %s · 소문(주점·거리)", kStep[l->step < 2 ? l->step : 2]);
            UI_Text(dc, r, t, g_font, COL_LIGHT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            lastChunk = -1;
            continue;
        }
        // 조건 덩이 -> 본문 덩이로 넘어가는 자리를 표시해 준다.
        if (l->chunk != lastChunk) {
            RECT c = r; c.right = c.left + 52;
            UI_Text(dc, c, l->chunk == 0 ? L"[조건]" : L"[본문]", g_smallFont, COL_WARN_TX,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            lastChunk = l->chunk;
        }
        wsprintfW(addr, L"%d:%d:%02d", l->part, l->slot, l->idx);
        { RECT a = r; a.left += 56; a.right = a.left + 62;
          UI_Text(dc, a, addr, g_smallFont, COL_DARK,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
        LineText(l, t);
        r.left += 122;
        UI_Text(dc, r, t, g_smallFont, l->kind == QL_RAW ? COL_DARK : COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    }
    UI_Scrollbar(dc, RcLnTrack(), g_lnScroll, LnMaxScroll(), LN_ROWS, n);
}

static void PaintPanel(HDC dc)
{
    const QuestInfo* q = Quest_At(g_sel);
    RECT p = RcPanel(), r;
    int vis[ROW_N], nv, i;
    HBRUSH br;
    wchar_t t[192];

    if (!q) return;
    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
    UI_Bevel(dc, p, FALSE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &p, br); DeleteObject(br);

    r = p; r.left += 14; r.top += 8; r.bottom = r.top + 20;
    if (q->addedFrom >= 0)
        wsprintfW(t, L"%d번 (추가한 퀘스트 — %d번을 본뜸) — %s %s", g_sel + 1, q->addedFrom + 1,
                  CityName(q->v[QF_CITY]), Quest_BuildingName(q->v[QF_BLDG]));
    else
        wsprintfW(t, L"%d번 퀘스트 편집 — %s %s", g_sel + 1,
                  CityName(q->v[QF_CITY]), Quest_BuildingName(q->v[QF_BLDG]));
    UI_Text(dc, r, t, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    r.right = RcMode(1).left - 8;
    UI_Text(dc, r, Quest_Dirty() ? L"● 저장 안 됨" : L"", g_smallFont, COL_WARN_TX,
            DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    UI_Button(dc, RcMode(1), L"값", g_mode == 0);
    UI_Button(dc, RcMode(0), L"대사", g_mode == 1);

    if (g_mode == 1) {
        PaintLines(dc);
        r = p; r.left += 14; r.right -= 14;
        r.bottom = PANEL_Y + PANEL_H - 42; r.top = r.bottom - 36;
        UI_Text(dc, r,
                g_msg[0] ? g_msg
                         : L"왼쪽 번호가 quests.json 의 script 주소입니다 — 파트:슬롯:줄.\n"
                           L"?? 는 아직 뜻을 모르는 바이트로, 고칠 때 그대로 보존됩니다.",
                g_smallFont, g_msg[0] ? COL_WARN_TX : COL_TEXT, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        { int b;
          for (b = 0; b < BTN_N; b++)
              UI_Button(dc, RcBtn(b),
                        (b == 2 && q->addedFrom >= 0) ? L"이 퀘스트 삭제" : kBtn[b],
                        b == 0 && Quest_Dirty()); }
        return;
    }

    nv = VisRows(g_sel, vis);
    for (i = 0; i < nv; i++) {
        const int f = kRow[vis[i]].f;
        RECT lr = RcRow(i);
        lr.left += 14; lr.right = lr.left + LBL_W;
        lr.top += 4; lr.bottom = lr.top + 20;
        UI_Text(dc, lr, kRow[vis[i]].label, g_font, COL_TEXT,
                DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        ValueText(f, q->v[f], t);
        if (kRow[vis[i]].sel) {
            UI_Select(dc, RcCtrl(i, 1), t, g_drop == f);
        } else {
            RECT vr = RcCtrl(i, 0);
            UI_Bevel(dc, vr, TRUE);
            UI_Text(dc, vr, t, g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            UI_Button(dc, RcStep(i, 0), L"◀◀", FALSE);
            UI_Button(dc, RcStep(i, 1), L"◀",  FALSE);
            UI_Button(dc, RcStep(i, 2), L"▶",  FALSE);
            UI_Button(dc, RcStep(i, 3), L"▶▶", FALSE);
        }

        // 이 값이 파일 안 몇 군데에 박혀 있는지. 2곳 이상이면 한꺼번에 바뀐다.
        if (q->n[f] > 1) {
            RECT nr = RcRow(i);
            nr.left = nr.right - 200; nr.right -= 14; nr.top += 6; nr.bottom = nr.top + 18;
            wsprintfW(t, L"%d곳 동시 수정", q->n[f]);
            UI_Text(dc, nr, t, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
    }

    // 안내 + 결과 메시지
    r = p; r.left += 14; r.right -= 14;
    r.bottom = PANEL_Y + PANEL_H - 42; r.top = r.bottom - 36;
    UI_Text(dc, r,
            g_msg[0] ? g_msg
                     : L"고친 값은 CDS95Util\\quests.json 에 쌓이고, 원본은 <파일>.CDS.orig 로 남습니다.\n"
                       L"[원래대로]는 이 퀘스트의 항목만 지웁니다. 게임을 껐다 켜야 반영됩니다.",
            g_smallFont, g_msg[0] ? COL_WARN_TX : COL_TEXT, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);

    { int b;
      for (b = 0; b < BTN_N; b++)
          UI_Button(dc, RcBtn(b),
                    (b == 2 && q->addedFrom >= 0) ? L"이 퀘스트 삭제" : kBtn[b],
                    b == 0 && Quest_Dirty()); }
}

// 펼침 목록을 상자 아래에 편다. 창 밖으로 나가면 안으로/위로 밀어 넣는다.
static void OpenDrop(int f, RECT anchor)
{
    int cols, rows, itemW, n;
    int w, h;
    RECT p;
    DropSpec(f, &cols, &rows, &itemW, &n);
    w = cols * itemW; h = rows * DD_ITEM_H;
    p.left = anchor.left; p.right = p.left + w;
    p.top = anchor.bottom; p.bottom = p.top + h;
    if (p.right > WIN_W - FRAME)  { int d = p.right - (WIN_W - FRAME); p.left -= d; p.right -= d; }
    if (p.left < FRAME)           { p.left = FRAME; p.right = p.left + w; }
    if (p.bottom > WIN_H - FRAME) { p.top = anchor.top - h; p.bottom = p.top + h; }
    if (p.top < FRAME)            { p.top = FRAME; p.bottom = p.top + h; }
    g_ddRc = p;
    g_drop = f;
    {   // 지금 값이 보이는 자리에서 열리도록 스크롤을 맞춘다
        const QuestInfo* q = Quest_At(g_sel);
        int cur = q ? DropIndexOf(f, q->v[f]) : -1;
        int maxs = (n + cols - 1) / cols - rows;
        g_ddScroll = 0;
        if (cur >= 0) {
            int row = cur / cols;
            if (row >= rows) g_ddScroll = row - rows / 2;
        }
        if (maxs < 0) maxs = 0;
        if (g_ddScroll > maxs) g_ddScroll = maxs;
        if (g_ddScroll < 0) g_ddScroll = 0;
    }
}

static void PaintDrop(HDC dc)
{
    const QuestInfo* q = Quest_At(g_sel);
    int cols, rows, itemW, n, r, c;
    HBRUSH br;
    if (g_drop < 0 || !q) return;
    DropSpec(g_drop, &cols, &rows, &itemW, &n);
    br = CreateSolidBrush(COL_FACE_TOP); FillRect(dc, &g_ddRc, br); DeleteObject(br);
    UI_Bevel(dc, g_ddRc, FALSE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &g_ddRc, br); DeleteObject(br);

    for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) {
        int i = (g_ddScroll + r) * cols + c;
        RECT ir;
        wchar_t t[64];
        if (i >= n) continue;
        ir.left = g_ddRc.left + c * itemW; ir.right = ir.left + itemW;
        ir.top = g_ddRc.top + r * DD_ITEM_H; ir.bottom = ir.top + DD_ITEM_H;
        if (DropValue(g_drop, i) == q->v[g_drop]) {
            br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &ir, br); DeleteObject(br);
        }
        DropText(g_drop, i, t);
        ir.left += 4;
        UI_Text(dc, ir, t, g_smallFont,
                DropValue(g_drop, i) == q->v[g_drop] ? COL_LIGHT : COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    }
}

// 펼친 목록에서 클릭 지점의 항목 번호. 밖이면 -1.
static int DropHit(POINT pt)
{
    int cols, rows, itemW, n, c, r, i;
    if (g_drop < 0 || !PtInRect(&g_ddRc, pt)) return -1;
    DropSpec(g_drop, &cols, &rows, &itemW, &n);
    c = (pt.x - g_ddRc.left) / itemW; if (c >= cols) c = cols - 1;
    r = (pt.y - g_ddRc.top) / DD_ITEM_H;
    i = (g_ddScroll + r) * cols + c;
    return (i >= 0 && i < n) ? i : -1;
}

// ---- 진행 포인터 고르기 ----

// 이 파트에서 시작하는 퀘스트 번호(1부터). 없으면 0.
// 포인터는 아무 파트나 가리킬 수 있지만, 뜻이 통하는 자리는 퀘스트의 첫 파트(조건 파트)다.
// 중간 파트에 걸면 받은 적 없는 의뢰의 완료보고가 뜨는 식으로 어긋난다.
static int QuestStartingAt(int part)
{
    int i, n = Quest_Count();
    for (i = 0; i < n; i++) {
        const QuestInfo* q = Quest_At(i);
        if (q && q->first == part) return i + 1;
    }
    return 0;
}

static void OpenPtr(void)
{
    RECT a = RcPtr();
    int w = PTR_COLS * PTR_ITEM_W, h = PTR_ROWS * DD_ITEM_H;
    g_ptrRc.left = a.left; g_ptrRc.right = a.left + w;
    g_ptrRc.top  = a.bottom; g_ptrRc.bottom = a.bottom + h;
    if (g_ptrRc.right > WIN_W - FRAME) {
        int d = g_ptrRc.right - (WIN_W - FRAME);
        g_ptrRc.left -= d; g_ptrRc.right -= d;
    }
    g_ptrOpen = 1;
}

static void PaintPtr(HDC dc)
{
    int r, c, cur = Quest_Pointer();
    HBRUSH br;
    RECT lg;
    if (!g_ptrOpen) return;

    br = CreateSolidBrush(COL_FACE_TOP); FillRect(dc, &g_ptrRc, br); DeleteObject(br);
    UI_Bevel(dc, g_ptrRc, FALSE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &g_ptrRc, br); DeleteObject(br);

    for (r = 0; r < PTR_ROWS; r++) for (c = 0; c < PTR_COLS; c++) {
        int v = r * PTR_COLS + c;
        int qn;
        RECT ir;
        wchar_t t[16];
        if (v > PTR_MAX) continue;
        ir.left = g_ptrRc.left + c * PTR_ITEM_W; ir.right = ir.left + PTR_ITEM_W;
        ir.top  = g_ptrRc.top  + r * DD_ITEM_H;  ir.bottom = ir.top + DD_ITEM_H;
        qn = QuestStartingAt(v);
        if (v == cur) { br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &ir, br); DeleteObject(br); }
        wsprintfW(t, L"%d", v);
        // 퀘스트가 시작하는 파트는 진하게, 그 밖은 흐리게 — 골라도 되는 자리를 눈에 띄게 한다.
        UI_Text(dc, ir, t, g_smallFont,
                v == cur ? COL_LIGHT : (qn ? COL_TEXT : COL_FACE_BOT),
                DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    // 격자 아래에 지금 짚고 있는 자리를 한 줄로 풀어 준다.
    lg = g_ptrRc; lg.top = g_ptrRc.bottom + 2; lg.bottom = lg.top + 32; lg.right += 200;
    {
        int qn = QuestStartingAt(cur);
        wchar_t t[192];
        if (qn) wsprintfW(t, L"지금 %d — %d번 퀘스트의 첫 파트입니다.\n진한 숫자가 퀘스트 시작 자리입니다.", cur, qn);
        else    wsprintfW(t, L"지금 %d — 퀘스트 시작 자리가 아닙니다.\n진한 숫자가 퀘스트 시작 자리입니다.", cur);
        UI_Text(dc, lg, t, g_smallFont, COL_WARN_TX, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
    }
}

// 격자에서 클릭한 자리의 값. 밖이거나 빈 칸이면 -1.
static int PtrHit(POINT pt)
{
    int c, r, v;
    if (!g_ptrOpen || !PtInRect(&g_ptrRc, pt)) return -1;
    c = (pt.x - g_ptrRc.left) / PTR_ITEM_W; if (c >= PTR_COLS) c = PTR_COLS - 1;
    r = (pt.y - g_ptrRc.top) / DD_ITEM_H;
    v = r * PTR_COLS + c;
    return (v >= 0 && v <= PTR_MAX) ? v : -1;
}

// ---- 그리기 ----

void Quest_Paint(HDC dc)
{
    wchar_t buf[192];
    int i, n = Quest_Count();

    UI_Button(dc, RcReload(), L"새로고침", FALSE);

    if (!g_loaded || Quest_Status() != QUEST_OK) {
        const wchar_t* why;
        RECT e;
        switch (Quest_Status()) {
        case QUEST_E_NAME: why = L"세이브에 퀘스트 파일 이름이 없습니다. 게임을 한 번 저장한 뒤 새로고침하세요."; break;
        case QUEST_E_FILE: why = L"퀘스트 이벤트 파일(.CDS)을 게임 폴더에서 열지 못했습니다."; break;
        default:           why = L"SAVEDATA.CDS 를 읽지 못했습니다. 게임을 한 번 저장한 뒤 새로고침하세요."; break;
        }
        e.left = Q_X; e.right = Q_X + Q_W; e.top = Q_Y; e.bottom = Q_Y + 60;
        UI_Text(dc, e, why, g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        return;
    }

    wsprintfW(buf, L"포인터 %d", Quest_Pointer());
    UI_Select(dc, RcPtr(), buf, g_ptrOpen);

    wsprintfW(buf, L"%s · %s · %d개 중 %d 완료 · %d년 · 내명성 %d%s",
              Quest_FileName(), Quest_JobName(), n, Quest_DoneCount(),
              Quest_Year(), Quest_MyFame(),
              Quest_Dirty() ? L" · 저장 안 됨" : L"");
    UI_Text(dc, RcInfo(), buf, g_smallFont, Quest_Dirty() ? COL_WARN_TX : COL_TEXT,
            DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    for (i = 0; i < Q_ROWS; i++) {
        int k = g_scroll + i;
        if (k >= n) break;
        PaintRow(dc, Q_Y + i * Q_ROW_H, k, Quest_At(k));
    }
    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), Q_ROWS, n);

    // 편집 패널이 없을 때의 알림 자리. 패널이 떠 있으면 그 안에서 같은 글을 낸다.
    // 목록 첫 줄 위에 덮어 그린다 — 포인터를 옮긴 결과를 바로 봐야 하기 때문이다.
    if (g_sel < 0 && g_msg[0]) {
        RECT m;
        HBRUSH br;
        m.left = Q_X; m.right = Q_X + Q_W; m.top = Q_Y; m.bottom = Q_Y + 52;
        br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &m, br); DeleteObject(br);
        UI_Bevel(dc, m, FALSE);
        br = CreateSolidBrush(COL_DARK); FrameRect(dc, &m, br); DeleteObject(br);
        m.left += 10; m.right -= 10; m.top += 5;
        UI_Text(dc, m, g_msg, g_smallFont, COL_WARN_TX, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
    }

    if (g_sel >= 0) { PaintPanel(dc); PaintDrop(dc); }
    PaintPtr(dc);      // 격자는 패널 위에 뜬다
}

// ---- 입력 ----

static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    if (row < 0) row = 0;
    if (row > mx) row = mx;
    if (row != g_scroll) { g_scroll = row; InvalidateRect(h, NULL, FALSE); }
}

void Quest_Activate(HWND h, int active)
{
    if (!active) return;
    // 탭을 켤 때마다 다시 읽는다. 게임 안에서 퀘스트가 진행되면 세이브도 바뀌기 때문에,
    // 한 번 읽어두면 금방 낡은 내용이 된다. 편집하다 만 것이 있으면 그대로 둔다.
    if (Quest_Dirty()) { if (h) InvalidateRect(h, NULL, FALSE); return; }
    g_loaded = Quest_Load();
    g_sel = -1; g_drop = -1; g_ptrOpen = 0; g_msg[0] = 0;
    // 이벤트 파일이 밖에서 바뀌어 원본을 다시 잡았으면 한 번은 알려 준다.
    if (Quest_Rebased() && !g_saidRebased) {
        g_saidRebased = 1;
        lstrcpyW(g_msg, L"이벤트 파일이 바뀐 것을 보고 새 파일을 원본으로 다시 잡았습니다. "
                        L"전에 쓰던 원본은 <파일>.CDS.orig.old 로 남겨 뒀습니다. "
                        L"quests.json 에 남아 있던 항목은 이제 새 파일의 같은 번호에 걸립니다.");
    }
    if (g_scroll > MaxScroll()) g_scroll = MaxScroll();
    if (h) InvalidateRect(h, NULL, FALSE);
}

static void Bump(int f, int delta, int vmax)
{
    const QuestInfo* q = Quest_At(g_sel);
    int v;
    if (!q) return;
    v = q->v[f] + delta;
    if (v < 0) v = 0;
    if (vmax > 0 && v > vmax) v = vmax;
    Quest_SetField(g_sel, f, v);
}

// 편집 패널 안의 클릭. 처리했으면 1.
static int PanelClick(HWND h, POINT pt)
{
    int vis[ROW_N], nv, i;
    RECT r;

    for (i = 0; i < 2; i++) {
        r = RcMode(i);
        if (PtInRect(&r, pt)) {
            g_mode = (i == 1) ? 0 : 1;
            g_lnScroll = 0; g_msg[0] = 0;
            InvalidateRect(h, NULL, FALSE);
            return 1;
        }
    }
    if (g_mode == 1) {
        r = RcLnTrack();
        if (PtInRect(&r, pt)) {
            int mid = (r.top + r.bottom) / 2, s = g_lnScroll + (pt.y < mid ? -LN_ROWS : LN_ROWS);
            if (s < 0) s = 0;
            if (s > LnMaxScroll()) s = LnMaxScroll();
            g_lnScroll = s;
            InvalidateRect(h, NULL, FALSE);
            return 1;
        }
    }

    for (i = 0; i < BTN_N; i++) {
        r = RcBtn(i);
        if (!PtInRect(&r, pt)) continue;
        g_msg[0] = 0;
        if (i == 0) {                                   // 저장
            if (!Quest_Dirty()) lstrcpyW(g_msg, L"바뀐 것이 없습니다.");
            else if (Quest_Save()) lstrcpyW(g_msg, L"quests.json 에 저장했습니다. 게임을 껐다 켜면 반영됩니다.");
            else lstrcpynW(g_msg, Quest_LastError(), 192);
        } else if (i == 1) {                            // 되돌리기 = 저장한 목록대로 다시 읽기
            g_loaded = Quest_Load();
            lstrcpyW(g_msg, L"다시 읽었습니다.");
        } else if (i == 2) {                            // 이 퀘스트의 json 항목만 지우기
            int added = Quest_At(g_sel)->addedFrom >= 0;
            if (!Quest_Reset(g_sel)) lstrcpyW(g_msg, L"이 퀘스트는 고친 적이 없습니다.");
            else {
                g_loaded = 1;
                if (added) { g_sel = -1; lstrcpyW(g_msg, L"추가했던 퀘스트를 지웠습니다. [저장]을 눌러야 파일에 반영됩니다."); }
                else lstrcpyW(g_msg, L"원래 값으로 돌렸습니다. [저장]을 눌러야 파일에 반영됩니다.");
            }
        } else {                                        // 닫기
            g_sel = -1;
        }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    if (g_mode == 1) return 1;      // 대사 보기에는 고칠 칸이 없다

    nv = VisRows(g_sel, vis);
    for (i = 0; i < nv; i++) {
        int f = kRow[vis[i]].f;
        if (kRow[vis[i]].sel) {
            r = RcCtrl(i, 1);
            if (PtInRect(&r, pt)) { OpenDrop(f, r); InvalidateRect(h, NULL, FALSE); return 1; }
        } else {
            static const int kSign[4] = { -1, -1, 1, 1 };
            int k;
            for (k = 0; k < 4; k++) {
                r = RcStep(i, k);
                if (!PtInRect(&r, pt)) continue;
                Bump(f, kSign[k] * ((k == 0 || k == 3) ? kRow[vis[i]].step2 : kRow[vis[i]].step1),
                     kRow[vis[i]].vmax);
                g_msg[0] = 0;
                InvalidateRect(h, NULL, FALSE);
                return 1;
            }
        }
    }
    return 1;      // 패널 위 아무 데나 = 아래 목록으로 새지 않게 먹는다
}

int Quest_Click(HWND h, POINT pt)
{
    RECT r;

    // 포인터 격자가 제일 위에 뜬다. 하나 고르면 세이브에 바로 쓰고 닫힌다.
    if (g_ptrOpen) {
        int v = PtrHit(pt);
        g_ptrOpen = 0;
        g_msg[0] = 0;
        if (v >= 0) {
            if (!Quest_SetPointer(v)) lstrcpynW(g_msg, Quest_LastError(), 192);
            else {
                int qn = QuestStartingAt(v);
                if (qn) wsprintfW(g_msg, L"진행 포인터를 %d(%d번 퀘스트) 로 옮겼습니다. 남은 기한은 0 이 됐고, "
                                         L"고치기 전 세이브는 SAVEDATA.CDS.bak 에 있습니다. "
                                         L"게임에서 이 세이브를 다시 불러와야 반영됩니다.", v, qn);
                else    wsprintfW(g_msg, L"진행 포인터를 %d 로 옮겼습니다 — 퀘스트가 시작하는 자리가 아니라 "
                                         L"진행이 어긋날 수 있습니다. 고치기 전 세이브는 SAVEDATA.CDS.bak 에 있습니다. "
                                         L"게임에서 이 세이브를 다시 불러와야 반영됩니다.", v);
            }
        }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    r = RcPtr();
    if (g_loaded && Quest_Status() == QUEST_OK && PtInRect(&r, pt)) {
        OpenPtr();
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    // 펼친 목록이 먼저 먹는다. 하나 고르면 닫힌다.
    if (g_drop >= 0) {
        int i = DropHit(pt), f = g_drop;
        g_drop = -1;
        if (i >= 0) { Quest_SetField(g_sel, f, DropValue(f, i)); g_msg[0] = 0; }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    if (g_sel >= 0) {
        r = RcPanel();
        if (PtInRect(&r, pt)) return PanelClick(h, pt);
        g_sel = -1; g_msg[0] = 0;      // 패널 밖 = 닫기
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    r = RcReload();
    if (PtInRect(&r, pt)) { Quest_Activate(h, 1); return 1; }
    r = RcTrack();
    if (PtInRect(&r, pt)) {
        int mid = (r.top + r.bottom) / 2;
        ScrollTo(h, g_scroll + (pt.y < mid ? -Q_ROWS : Q_ROWS));
        return 1;
    }
    // 목록 줄 = 편집 패널 열기
    if (pt.x >= Q_X && pt.x < Q_X + Q_W && pt.y >= Q_Y && pt.y < Q_Y + Q_LIST_H) {
        int k = g_scroll + (pt.y - Q_Y) / Q_ROW_H;
        if (k >= 0 && k < Quest_Count()) {
            g_sel = k; g_msg[0] = 0; g_lnScroll = 0;
            InvalidateRect(h, NULL, FALSE);
        }
        return 1;
    }
    return 0;
}

int Quest_Key(HWND h, WPARAM wp)
{
    if (wp == VK_ESCAPE) {
        if (g_ptrOpen)       { g_ptrOpen = 0; InvalidateRect(h, NULL, FALSE); return 1; }
        if (g_drop >= 0)     { g_drop = -1; InvalidateRect(h, NULL, FALSE); return 1; }
        if (g_sel >= 0)      { g_sel = -1; g_msg[0] = 0; InvalidateRect(h, NULL, FALSE); return 1; }
        return 0;
    }
    if (g_sel >= 0) return 1;      // 패널이 떠 있으면 목록 스크롤은 막는다
    switch (wp) {
    case VK_UP:    ScrollTo(h, g_scroll - 1); return 1;
    case VK_DOWN:  ScrollTo(h, g_scroll + 1); return 1;
    case VK_PRIOR: ScrollTo(h, g_scroll - Q_ROWS); return 1;
    case VK_NEXT:  ScrollTo(h, g_scroll + Q_ROWS); return 1;
    case VK_HOME:  ScrollTo(h, 0); return 1;
    case VK_END:   ScrollTo(h, MaxScroll()); return 1;
    case 'R':      Quest_Activate(h, 1); return 1;
    }
    return 0;
}

void Quest_Wheel(HWND h, int notches)
{
    if (g_ptrOpen) return;      // 65칸이 한 화면에 다 들어와 굴릴 것이 없다
    if (g_sel >= 0 && g_drop < 0 && g_mode == 1) {
        int s = g_lnScroll - notches * 3;
        if (s < 0) s = 0;
        if (s > LnMaxScroll()) s = LnMaxScroll();
        if (s != g_lnScroll) { g_lnScroll = s; InvalidateRect(h, NULL, FALSE); }
        return;
    }
    if (g_drop >= 0) {          // 226개짜리 목록은 휠로 굴린다
        int cols, rows, itemW, n, maxs, s;
        DropSpec(g_drop, &cols, &rows, &itemW, &n);
        maxs = (n + cols - 1) / cols - rows;
        if (maxs < 0) maxs = 0;
        s = g_ddScroll - notches;
        if (s < 0) s = 0;
        if (s > maxs) s = maxs;
        if (s != g_ddScroll) { g_ddScroll = s; InvalidateRect(h, NULL, FALSE); }
        return;
    }
    if (g_sel >= 0) return;
    ScrollTo(h, g_scroll - notches);
}
