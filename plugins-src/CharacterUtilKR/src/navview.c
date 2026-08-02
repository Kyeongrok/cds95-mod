#include "navview.h"
#include "ui.h"
#include "faces.h"
#include "savedata.h"
#include "livechar.h"

// ---- 목록 레이아웃 ----
// NAV_Y / NAV_ROW_H / NAV_ROWS / NAV_H / CREW_* 는 ui.h 에 있다 — 창 높이가 그 값에 딸려 있어서다.
#define NAV_X      (FRAME + GAP)                            // 13
#define NAV_W      (WIN_W - NAV_X - GAP - SB_W - FRAME)     // 722
#define ROW_PORT_W 40
#define ROW_PORT_H 48

// 승무원 네 자리 줄 — 275명 + "없음" 중에서 고른다. 목록이 길어 휠로 굴린다.
#define CR_COLS    2
#define CR_ROWS    14
#define CR_ITEM_W  200
#define CR_BOX_W   140

// 드롭다운(직접 그림)
#define DD_ITEM_H  22
#define DD_SKILL_COLS 2
#define DD_SKILL_ROWS 14                    // (전체) + 특기 27종 = 28 = 2열 x 14행
#define DD_LV_N    6                        // 전체 / 1~5
#define DD_YR_COLS 6
#define DD_YR_ROWS 12                       // 6x12=72 칸에 67개 연도(1460~1526)
#define DD_YR_ITEM_W 46
typedef char NavYearGridFits[(DD_YR_COLS * DD_YR_ROWS >= LIVECHAR_YEAR_N) ? 1 : -1];

// 줄마다 붙는 생년 상자. 목록은 실행 중인 인물 배열(livechar.c)에 곧바로 써넣는다 —
// 세이브 파일은 건드리지 않는다(savedata.h 의 원칙).
#define YR_BOX_X   190
#define YR_BOX_W   104

// 특기 편집 패널 — 27종을 2열 14행으로 늘어놓고, 칸마다 0~3 을 한 줄로 붙여 고른다.
// 레벨이 넷뿐이라 접었다 펴는 상자보다 이렇게 늘어놓는 쪽이 한 번에 눌러진다.
#define SK_COLS    2
#define SK_ROWS    14                       // 2x14=28 칸에 27종
#define SK_NAME_W  86
#define SK_LV_W    20
#define SK_ITEM_W  (SK_NAME_W + (LIVECHAR_SKILL_MAX + 1) * SK_LV_W + 8)
#define SK_BTN_W   52                       // 줄에 붙는 [특기] 버튼
typedef char NavSkillGridFits[(SK_COLS * SK_ROWS >= LIVECHAR_SKILL_N) ? 1 : -1];

static SaveData g_save;
static int   g_list[512];      // 필터를 통과한 인물의 g_save.chars 인덱스
static int   g_count = 0;
static int   g_scroll = 0;
static int   g_sel = -1;       // g_list 상의 선택 위치

// ---- 필터 상태 ----
static int   g_onlyHirable = 1;   // 고용가능(2) & 함대소속 아님
static int   g_showGray = 0;      // 18세 미만 / 60세 초과 포함
static int   g_skill = 0;         // 정렬/필터 기준 특기. 0 = 미선택(특기 조건 없음)
static int   g_lvMin = 0;         // 0 = 레벨 제한 없음. g_skill==0 이면 쓰이지 않는다
static int   g_open = 0;          // 열려 있는 드롭다운: 0=없음 1=특기 2=Lv 3=생년 4=특기수정

// 생년/특기수정 패널 상태. 대상은 g_save.chars 의 색인이다(g_list 위치가 아니다 —
// 스크롤과 무관하게 대상이 안 흔들리도록).
static int   g_yrChar = -1;
static RECT  g_yrPanel;
static int   g_skChar = -1;
static RECT  g_skPanel;
// 승무원 자리 목록(g_open==5). g_crWhich = 고르는 중인 자리(0~3), g_crScroll = 맨 위 행.
static int   g_crWhich = -1;
static RECT  g_crPanel;
static int   g_crScroll = 0;

// g_save.chars 색인 -> 실행 중 인물 배열의 칸 번호.
// 0 = 아직 안 찾음 / -1 = 못 찾음 / 그 외 = 칸 번호 + 1.
// 0 을 "아직"으로 둬야 static 기본값(0)이 그대로 맞는다.
static int   g_liveSlot[512];

static void ClearLiveCache(void)
{
    int i;
    for (i = 0; i < (int)(sizeof(g_liveSlot)/sizeof(g_liveSlot[0])); i++) g_liveSlot[i] = 0;
}

// 이 인물의 실행 중 배열 칸. 없으면 -1.
static int LiveSlotOf(int ci)
{
    if (!LiveChar_Ready()) return -1;
    if (ci < 0 || ci >= (int)(sizeof(g_liveSlot)/sizeof(g_liveSlot[0]))) return -1;
    if (g_liveSlot[ci] == 0) {
        const SaveChar* c = &g_save.chars[ci];
        int s = LiveChar_Find(c->name, c->faceCode);
        g_liveSlot[ci] = (s >= 0) ? s + 1 : -1;
    }
    return g_liveSlot[ci] > 0 ? g_liveSlot[ci] - 1 : -1;
}

// 실행 중 배열에서 고용상태 자리를 찾아낸다(세이브의 같은 값과 대조).
// 짝을 찾은 인물이 있어야 대조가 되므로 목록을 만든 뒤에 부른다.
static void CalibrateHire(void)
{
    static int hire[512], slot[512];
    int i, n = 0;
    if (!LiveChar_Ready() || LiveChar_HireOffset() >= 0) return;
    for (i = 0; i < g_save.count && n < 512; i++) {
        int s = LiveSlotOf(i);
        if (s < 0) continue;
        hire[n] = g_save.chars[i].hire;
        slot[n] = s;
        n++;
    }
    LiveChar_CalibrateHire(hire, slot, n);
}

// 목록에 있는 인물 중 실행 중 배열에서 짝을 찾은 수. 진단 표시용.
static int MatchedCount(void)
{
    int i, n = 0;
    if (!LiveChar_Ready()) return 0;
    for (i = 0; i < g_count; i++) if (LiveSlotOf(g_list[i]) >= 0) n++;
    return n;
}

// 화면에 쓸 나이/생년. 실행 중 배열이 있으면 그쪽이 진짜다(세이브 파일은 뒤처져 있을 수 있다).
static int AgeOf(int ci)
{
    int s = LiveSlotOf(ci);
    if (s >= 0) { int a = LiveChar_Age(s); if (a != -9999) return a; }
    return g_save.chars[ci].age;
}
static int NowYear(void)
{
    int y = LiveChar_Year();
    return y ? y : (int)g_save.year;
}

// 내 명성. 세이브 쪽은 2바이트라 65535 를 넘으면 잘리고 저장 시점 값이라 뒤처지므로,
// 실행 중 값(4바이트)이 읽히면 그쪽을 쓴다.
static int MyFame(void)
{
    int f = LiveChar_PlayerFame();
    return f >= 0 ? f : (int)g_save.playerFame;
}

static const wchar_t* kLvText[DD_LV_N] = { L"전체", L"1↑", L"2↑", L"3↑", L"4↑", L"5" };

// ---- 필터바 위치 ----
static RECT Rc(int x, int w) { RECT r; r.left=x; r.right=x+w; r.top=FILTER_Y; r.bottom=FILTER_Y+22; return r; }
static RECT RcReload(void)  { return Rc(13, 72); }
static RECT RcHirable(void) { return Rc(89, 86); }
static RECT RcGray(void)    { return Rc(179, 86); }
static RECT RcSkillLbl(void){ return Rc(271, 32); }
static RECT RcSkill(void)   { return Rc(307, 120); }
static RECT RcLvLbl(void)   { return Rc(433, 22); }
static RECT RcLv(void)      { return Rc(459, 70); }
static RECT RcInfo(void)    { return Rc(541, WIN_W - FRAME - 8 - 541); }
// 승무원 네 자리 줄. [부관 ...][항해사 ...][측량사 ...][통역 ...] 을 나란히 놓는다.
static RECT RcCrewBox(int which)
{
    RECT r;
    int w = (NAV_W - 8) / LIVECHAR_CREW_N;
    r.left  = NAV_X + which * w + 44;
    r.right = NAV_X + which * w + w - 8;
    r.top = CREW_Y; r.bottom = CREW_Y + 22;
    return r;
}
static RECT RcCrewLabel(int which)
{
    RECT r = RcCrewBox(which);
    r.right = r.left - 4;
    r.left  = r.right - 44;
    return r;
}

static RECT RcTrack(void)
{
    RECT r;
    r.right = WIN_W - FRAME - 2; r.left = r.right - SB_W;
    r.top = NAV_Y; r.bottom = NAV_Y + NAV_H;
    return r;
}

// 드롭다운 패널은 select box 바로 아래에 겹쳐 그린다(별도 창을 만들지 않는다).
static RECT RcSkillPanel(void)
{
    RECT b = RcSkill(), r;
    r.left = b.left; r.top = b.bottom;
    r.right = b.left + DD_SKILL_COLS * (b.right - b.left);
    r.bottom = r.top + DD_SKILL_ROWS * DD_ITEM_H;
    return r;
}
static RECT RcLvPanel(void)
{
    RECT b = RcLv(), r;
    r.left = b.left; r.top = b.bottom; r.right = b.right;
    r.bottom = r.top + DD_LV_N * DD_ITEM_H;
    return r;
}

static int MaxScroll(void) { int m = g_count - NAV_ROWS; return m < 0 ? 0 : m; }

static void AppendSkill(wchar_t* buf, int cap, int id, int lv)
{
    wchar_t t[16];
    wsprintfW(t, L"%s%d ", Save_SkillShort(id), lv);
    if (lstrlenW(buf) + lstrlenW(t) < cap - 1) lstrcatW(buf, t);
}

// 등장 전(18세 미만)이거나 은퇴(60세 초과)한 인물. 기본은 감춘다.
// 나이는 세이브가 아니라 실행 중 값으로 본다 — 생년을 고치면 곧바로 목록에 반영되도록.
static int IsGray(int ci) { int a = AgeOf(ci); return a < 18 || a > 60; }

static int Pass(int ci)
{
    const SaveChar* c = &g_save.chars[ci];
    if (g_onlyHirable && !(c->hire == 2 && c->loc != 255)) return 0;
    if (!g_showGray && IsGray(ci)) return 0;
    // 특기 미선택(0)이면 레벨 조건도 의미가 없으므로 통째로 건너뛴다.
    if (g_skill > 0 && g_lvMin > 0 && c->skill[g_skill] < g_lvMin) return 0;
    return 1;
}

// 선택 특기 레벨 내림차순 → 명성 오름차순(고용 문턱이 낮은 쪽 먼저) → 등장 순.
// 특기 미선택이면 skill[0] 이 늘 0 이라 자연히 명성 오름차순이 된다.
static int Before(const SaveChar* a, const SaveChar* b)
{
    if (a->skill[g_skill] != b->skill[g_skill]) return a->skill[g_skill] > b->skill[g_skill];
    if (a->fame != b->fame) return a->fame < b->fame;
    return a->index < b->index;
}

static void Rebuild(void)
{
    int i, j, v;
    g_count = 0;
    for (i = 0; i < g_save.count && g_count < (int)(sizeof(g_list)/sizeof(g_list[0])); i++)
        if (Pass(i)) g_list[g_count++] = i;

    // 삽입 정렬. 많아야 수백 건이라 이 정도면 충분하다.
    for (i = 1; i < g_count; i++) {
        v = g_list[i];
        for (j = i - 1; j >= 0 && Before(&g_save.chars[v], &g_save.chars[g_list[j]]); j--)
            g_list[j+1] = g_list[j];
        g_list[j+1] = v;
    }

    g_scroll = 0;
    g_sel = -1;
    CalibrateHire();
}

static void Reload(HWND h)
{
    Save_Load(&g_save);
    LiveChar_Load();     // 아직 세이브를 안 불러온 상태에서 창을 열었을 수 있으니 매번 다시 본다
    ClearLiveCache();
    Rebuild();
    InvalidateRect(h, NULL, FALSE);
}

// ---- 그리기 ----

static const wchar_t* SkillText(int id) { return id == 0 ? L"(전체)" : Save_SkillName(id); }

// 열린 드롭다운 패널. 목록 위에 덮어 그리므로 Nav_Paint 의 맨 마지막에 호출한다.
static void DrawPanel(HDC dc)
{
    RECT p, it;
    HBRUSH br;
    int i, n, cols, itw;

    if (g_open == 1)      { p = RcSkillPanel(); n = 1 + SAVE_SKILL_MAX; cols = DD_SKILL_COLS; }
    else if (g_open == 2) { p = RcLvPanel();    n = DD_LV_N;            cols = 1; }
    else if (g_open == 3) { p = g_yrPanel;      n = LIVECHAR_YEAR_N;    cols = DD_YR_COLS; }
    else return;

    itw = (p.right - p.left) / cols;

    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK);    FrameRect(dc, &p, br); DeleteObject(br);

    for (i = 0; i < n; i++) {
        // 특기는 열 우선(위에서 아래로 채우고 다음 열), 생년은 행 우선(달력처럼 가로로).
        int col = (g_open == 3) ? (i % DD_YR_COLS) : (cols > 1 ? i / DD_SKILL_ROWS : 0);
        int row = (g_open == 3) ? (i / DD_YR_COLS) : (cols > 1 ? i % DD_SKILL_ROWS : i);
        int cur;
        wchar_t yb[8];
        const wchar_t* label;

        if (g_open == 1)      { cur = (i == g_skill); label = SkillText(i); }
        else if (g_open == 2) { cur = (i == g_lvMin); label = kLvText[i]; }
        else {
            int yr = LIVECHAR_YEAR_MIN + i;
            cur = (g_yrChar >= 0 && NowYear() - AgeOf(g_yrChar) == yr);
            wsprintfW(yb, L"%d", yr);
            label = yb;
        }

        it.left   = p.left + col * itw + 1;
        it.right  = it.left + itw - 2;
        it.top    = p.top + row * DD_ITEM_H + 1;
        it.bottom = it.top + DD_ITEM_H - 1;
        if (cur) {
            br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &it, br); DeleteObject(br);
        }
        { RECT t = it;
          if (g_open != 3) t.left += 6;
          UI_Text(dc, t, label, g_open == 3 ? g_smallFont : g_font,
                  cur ? RGB(250,244,228) : COL_TEXT,
                  (g_open == 3 ? DT_CENTER : DT_LEFT)
                  | DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX); }
    }
}

// 줄 y 의 생년 상자. 목록 줄 3행 오른쪽, 언어 특기 왼쪽에 놓는다.
static RECT RcYearBox(int y)
{
    RECT r;
    r.left = NAV_X + 52 + YR_BOX_X; r.right = r.left + YR_BOX_W;
    r.top = y + 40; r.bottom = y + 60;
    return r;
}
// 줄 y 의 [특기] 버튼. 2행(능력치 줄) 오른쪽 끝.
static RECT RcSkillBtn(int y)
{
    RECT r;
    r.right = NAV_X + NAV_W - 6; r.left = r.right - SK_BTN_W;
    r.top = y + 24; r.bottom = y + 42;
    return r;
}

// 특기 편집 패널을 버튼 아래에 편다. 창 밖으로 나가면 안으로/위로 밀어 넣는다.
static void OpenSkillPanel(int ci, RECT anchor)
{
    int w = SK_COLS * SK_ITEM_W, hgt = SK_ROWS * DD_ITEM_H;
    RECT p;
    p.left = anchor.right - w; p.right = p.left + w;
    p.top  = anchor.bottom;    p.bottom = p.top + hgt;
    if (p.right > WIN_W - FRAME) { int d = p.right - (WIN_W - FRAME); p.left -= d; p.right -= d; }
    if (p.left < FRAME)          { p.left = FRAME; p.right = p.left + w; }
    if (p.bottom > WIN_H - FRAME) { p.top = anchor.top - hgt; p.bottom = p.top + hgt; }
    if (p.top < FRAME)            { p.top = FRAME; p.bottom = p.top + hgt; }
    g_skPanel = p;
    g_skChar  = ci;
    g_open    = 4;
}

// 특기 편집 패널. 칸마다 이름 + [0][1][2][3], 지금 레벨이 눌린 모양으로 칠해진다.
static void DrawSkillPanel(HDC dc)
{
    RECT p = g_skPanel, it;
    HBRUSH br;
    int i, slot;

    if (g_open != 4 || g_skChar < 0) return;
    slot = LiveSlotOf(g_skChar);

    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK);    FrameRect(dc, &p, br); DeleteObject(br);

    for (i = 0; i < LIVECHAR_SKILL_N; i++) {
        int id = i + 1, lv = LiveChar_Skill(slot, id), k;
        RECT nr;
        it.left   = p.left + (i / SK_ROWS) * SK_ITEM_W + 1;
        it.right  = it.left + SK_ITEM_W - 2;
        it.top    = p.top  + (i % SK_ROWS) * DD_ITEM_H + 1;
        it.bottom = it.top + DD_ITEM_H - 1;

        nr = it; nr.left += 4; nr.right = nr.left + SK_NAME_W;
        UI_Text(dc, nr, Save_SkillName(id), g_smallFont,
                lv > 0 ? COL_TEXT : RGB(130, 115, 95),
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

        for (k = 0; k <= LIVECHAR_SKILL_MAX; k++) {
            RECT b;
            wchar_t t[4];
            b.left = it.left + SK_NAME_W + 6 + k * SK_LV_W;
            b.right = b.left + SK_LV_W - 2;
            b.top = it.top + 2; b.bottom = it.bottom - 2;
            if (k == lv) { br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &b, br); DeleteObject(br); }
            UI_Bevel(dc, b, k == lv);
            wsprintfW(t, L"%d", k);
            UI_Text(dc, b, t, g_smallFont, k == lv ? RGB(250,244,228) : COL_TEXT,
                    DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
    }
}

// ---- 승무원 자리 ----

static int CrewItemCount(void) { return LIVECHAR_COUNT + 1; }   // "없음" + 275명
static int CrewMaxScroll(void)
{
    int m = (CrewItemCount() + CR_COLS - 1) / CR_COLS - CR_ROWS;
    return m < 0 ? 0 : m;
}
// 목록의 i 번째: 0 = 없음, 그 밖은 인물 칸 (i-1).
static void CrewItemText(int i, wchar_t* out, int cap)
{
    if (i == 0) { lstrcpynW(out, L"(없음)", cap); return; }
    LiveChar_NameAt(i - 1, out, cap);
    if (!out[0]) wsprintfW(out, L"#%d", i - 1);
}

static void OpenCrewPanel(int which, RECT anchor)
{
    int w = CR_COLS * CR_ITEM_W + SB_W, hgt = CR_ROWS * DD_ITEM_H;
    RECT p;
    int cur = LiveChar_Crew(which), want, mx;
    p.left = anchor.left; p.right = p.left + w;
    p.top  = anchor.bottom; p.bottom = p.top + hgt;
    if (p.right > WIN_W - FRAME) { int d = p.right - (WIN_W - FRAME); p.left -= d; p.right -= d; }
    if (p.left < FRAME)          { p.left = FRAME; p.right = p.left + w; }
    if (p.bottom > WIN_H - FRAME) { p.top = anchor.top - hgt; p.bottom = p.top + hgt; }
    if (p.top < FRAME)            { p.top = FRAME; p.bottom = p.top + hgt; }
    g_crPanel = p;
    g_crWhich = which;
    g_open    = 5;
    // 지금 앉아 있는 사람이 보이는 자리에서 연다(275명을 처음부터 훑지 않도록).
    want = (cur + 1) / CR_COLS - CR_ROWS / 2;
    mx = CrewMaxScroll();
    g_crScroll = want < 0 ? 0 : (want > mx ? mx : want);
}

static void DrawCrewPanel(HDC dc)
{
    RECT p = g_crPanel, it;
    HBRUSH br;
    int i, n, first, cur;

    if (g_open != 5 || g_crWhich < 0) return;
    n = CrewItemCount();
    cur = LiveChar_Crew(g_crWhich) + 1;      // 없음 = 0
    first = g_crScroll * CR_COLS;

    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK);    FrameRect(dc, &p, br); DeleteObject(br);

    for (i = first; i < n && i - first < CR_COLS * CR_ROWS; i++) {
        int k = i - first;
        wchar_t nm[64];
        it.left   = p.left + (k % CR_COLS) * CR_ITEM_W + 1;
        it.right  = it.left + CR_ITEM_W - 2;
        it.top    = p.top  + (k / CR_COLS) * DD_ITEM_H + 1;
        it.bottom = it.top + DD_ITEM_H - 1;
        if (i == cur) { br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &it, br); DeleteObject(br); }
        CrewItemText(i, nm, 64);
        it.left += 6;
        UI_Text(dc, it, nm, g_smallFont, i == cur ? RGB(250,244,228) : COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    { RECT tr = p;
      tr.left = p.left + CR_COLS * CR_ITEM_W; tr.right = p.right;
      UI_Scrollbar(dc, tr, g_crScroll, CrewMaxScroll(), CR_ROWS,
                   (n + CR_COLS - 1) / CR_COLS); }
}

// 승무원 목록에서 클릭 지점의 항목 번호(0 = 없음). 밖이면 -1.
static int CrewPanelHit(POINT pt)
{
    int col, row, i;
    if (g_open != 5 || !PtInRect(&g_crPanel, pt)) return -1;
    col = (pt.x - g_crPanel.left) / CR_ITEM_W;
    row = (pt.y - g_crPanel.top)  / DD_ITEM_H;
    if (col < 0 || col >= CR_COLS || row < 0 || row >= CR_ROWS) return -1;
    i = (g_crScroll + row) * CR_COLS + col;
    return (i >= 0 && i < CrewItemCount()) ? i : -1;
}

// 특기 패널에서 클릭 지점의 (특기 id, 레벨). 못 맞히면 id 0.
static void SkillPanelHit(POINT pt, int* id, int* lv)
{
    int col, row, i, k, x;
    *id = 0; *lv = 0;
    if (g_open != 4 || !PtInRect(&g_skPanel, pt)) return;
    col = (pt.x - g_skPanel.left) / SK_ITEM_W;
    row = (pt.y - g_skPanel.top)  / DD_ITEM_H;
    if (col < 0 || col >= SK_COLS || row < 0 || row >= SK_ROWS) return;
    i = col * SK_ROWS + row;
    if (i >= LIVECHAR_SKILL_N) return;
    x = pt.x - (g_skPanel.left + col * SK_ITEM_W) - SK_NAME_W - 6;
    if (x < 0) return;                       // 이름 쪽을 눌렀다
    k = x / SK_LV_W;
    if (k < 0 || k > LIVECHAR_SKILL_MAX) return;
    *id = i + 1; *lv = k;
}

static void PaintRow(HDC dc, int y, int ci, const SaveChar* c, int selected)
{
    RECT r, tr;
    HBRUSH br;
    COLORREF fg = COL_TEXT;
    wchar_t buf[192], gen[128], lang[128];
    int k, poor = (c->fame > MyFame());
    int tx = NAV_X + 52;
    int right = NAV_X + NAV_W;

    r.left = NAV_X; r.right = right; r.top = y; r.bottom = y + NAV_ROW_H;
    if (selected)   { br = CreateSolidBrush(COL_SEL_BG); fg = RGB(250,244,228); }
    else if (poor)  { br = CreateSolidBrush(COL_WARN_BG); fg = COL_WARN_TX; }
    else            { br = CreateSolidBrush(COL_DISP_BG); }
    FillRect(dc, &r, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &r, br); DeleteObject(br);

    Face_Draw(dc, NAV_X + 5, y + 8, ROW_PORT_W, ROW_PORT_H, c->faceGender, c->faceCode);

    // 1행: 이름 / 명성
    tr.left = tx; tr.right = right - 150; tr.top = y + 5; tr.bottom = y + 24;
    UI_Text(dc, tr, c->name, g_font, fg, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    wsprintfW(buf, poor ? L"명성 %d (부족)" : L"명성 %d", c->fame);
    tr.left = right - 148; tr.right = right - 6;
    UI_Text(dc, tr, buf, g_font, fg, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    // 2행: 능력치 + 일반 특기
    gen[0] = 0;
    for (k = 1; k < SAVE_SKILL_LANG0; k++)
        if (c->skill[k]) AppendSkill(gen, 128, k, c->skill[k]);
    wsprintfW(buf, L"체%d 지%d 무%d 매%d 운%d   %s",
              c->hp, c->intel, c->str, c->chm, c->luk, gen);
    tr.left = tx; tr.right = right - 6; tr.top = y + 24; tr.bottom = y + 42;
    if (LiveSlotOf(ci) >= 0) {                       // 고칠 수 있는 인물만 버튼을 준다
        tr.right = right - SK_BTN_W - 12;
        UI_Button(dc, RcSkillBtn(y), L"특기", g_open == 4 && g_skChar == ci);
    }
    UI_Text(dc, tr, buf, g_smallFont, fg, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    // 3행: 소재 / 건물 / 나이 + [생년] + 언어 특기
    {
        const wchar_t* bn = Save_BuildingName(c->bldg);
        int age = AgeOf(ci), now = NowYear();
        if (bn[0]) wsprintfW(buf, L"%s · %s · %d세", Save_CityName(c->loc), bn, age);
        else       wsprintfW(buf, L"%s · %d세", Save_CityName(c->loc), age);
        tr.left = tx; tr.right = tx + YR_BOX_X - 6; tr.top = y + 42; tr.bottom = y + 60;
        UI_Text(dc, tr, buf, g_smallFont, fg, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

        // 나이는 해가 바뀌면 오르므로 (지금연도 - 나이) 가 곧 생년이다. 등장연도가 아니다 —
        // 인물은 18세가 되어야 술집에 나오니 등장은 생년 + 18 쯤이 된다(IsGray 가 18세
        // 미만을 접어 두는 것도 같은 이유). 여급 표(maids.c)의 값도 같은 뜻이다.
        if (now) {
            RECT yb = RcYearBox(y);
            wchar_t ys[24];
            wsprintfW(ys, L"생년 %d", now - age);
            if (LiveSlotOf(ci) >= 0)
                UI_Select(dc, yb, ys, g_open == 3 && g_yrChar == ci);
            else {   // 실행 중 배열에서 짝을 못 찾은 인물은 고칠 수 없다. 글자만 둔다.
                yb.left += 6;
                UI_Text(dc, yb, ys, g_smallFont, fg,
                        DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
        }
    }
    lang[0] = 0;
    for (k = SAVE_SKILL_LANG0; k <= SAVE_SKILL_MAX; k++)
        if (c->skill[k]) AppendSkill(lang, 128, k, c->skill[k]);
    if (lang[0]) {
        tr.left = tx + YR_BOX_X + YR_BOX_W + 6; tr.right = right - 6;
        UI_Text(dc, tr, lang, g_smallFont, selected ? fg : COL_LANG_TX,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
}

void Nav_Paint(HDC dc)
{
    wchar_t buf[96];
    int r;

    UI_Button(dc, RcReload(),  L"새로고침", FALSE);
    UI_Button(dc, RcHirable(), L"고용가능만", g_onlyHirable);
    UI_Button(dc, RcGray(),    L"미등장포함", g_showGray);

    UI_Text(dc, RcSkillLbl(), L"특기", g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    UI_Select(dc, RcSkill(), SkillText(g_skill), g_open == 1);
    UI_Text(dc, RcLvLbl(), L"Lv", g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    UI_Select(dc, RcLv(), g_skill > 0 ? kLvText[g_lvMin] : L"-", g_open == 2);

    // 각 줄의 생년을 견주려면 지금이 몇 년인지가 있어야 한다.
    // 생년을 못 고치는 상태면 그 자리에 왜 그런지를 대신 띄운다(배열X=사유, 짝=이름 매칭 수).
    if (!g_save.loaded)            lstrcpyW(buf, L"SAVEDATA.CDS 없음");
    else if (!LiveChar_Ready())    wsprintfW(buf, L"%d년 · %d명 · 배열X %d (%d/%d)",
                                             NowYear(), g_count, LiveChar_Status(),
                                             LiveChar_OkCount(), LiveChar_NamedCount());
    else {
        int m = MatchedCount();
        if (m < g_count) wsprintfW(buf, L"%d년 · %d명 · 짝 %d", NowYear(), g_count, m);
        else if (LiveChar_HireOffset() < 0)
                         wsprintfW(buf, L"%d년 · 내명성 %d · %d명 · 고용상태자리X",
                                   NowYear(), MyFame(), g_count);
        else             wsprintfW(buf, L"%d년 · 내명성 %d · %d명",
                                   NowYear(), MyFame(), g_count);
    }
    UI_Text(dc, RcInfo(), buf, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    // 주인공이 데리고 다니는 네 자리. 실행 중 인물 배열을 못 읽으면 고를 것도 없어 감춘다.
    if (LiveChar_Ready()) {
        int w;
        for (w = 0; w < LIVECHAR_CREW_N; w++) {
            int cur = LiveChar_Crew(w);
            wchar_t nm[64];
            if (cur < 0) lstrcpyW(nm, L"(없음)");
            else { LiveChar_NameAt(cur, nm, 64); if (!nm[0]) wsprintfW(nm, L"#%d", cur); }
            UI_Text(dc, RcCrewLabel(w), LiveChar_CrewLabel(w), g_smallFont, COL_TEXT,
                    DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            UI_Select(dc, RcCrewBox(w), nm, g_open == 5 && g_crWhich == w);
        }
    }

    if (!g_save.loaded || g_count == 0) {
        RECT e;
        e.left = NAV_X; e.right = NAV_X + NAV_W; e.top = NAV_Y; e.bottom = NAV_Y + 60;
        UI_Text(dc, e,
                g_save.loaded ? L"조건에 맞는 인물이 없습니다. 특기나 레벨을 낮춰 보세요."
                              : L"게임 폴더에서 SAVEDATA.CDS 를 읽지 못했습니다. 한 번 저장한 뒤 새로고침하세요.",
                g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        DrawPanel(dc);
        return;
    }

    for (r = 0; r < NAV_ROWS; r++) {
        int i = g_scroll + r;
        if (i >= g_count) break;
        PaintRow(dc, NAV_Y + r * NAV_ROW_H, g_list[i], &g_save.chars[g_list[i]], i == g_sel);
    }

    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), NAV_ROWS, g_count);
    DrawPanel(dc);        // 목록 위에 덮어 그린다
    DrawSkillPanel(dc);
    DrawCrewPanel(dc);
}

// ---- 입력 ----
static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    if (row < 0) row = 0;
    if (row > mx) row = mx;
    if (row != g_scroll) { g_scroll = row; InvalidateRect(h, NULL, FALSE); }
}

// 열린 패널에서 클릭 지점의 항목 번호. 패널 밖이면 -1.
static int PanelHit(POINT pt)
{
    RECT p;
    int itw, col, row, i, n;

    if (g_open == 1)      { p = RcSkillPanel(); n = 1 + SAVE_SKILL_MAX; }
    else if (g_open == 2) { p = RcLvPanel();    n = DD_LV_N; }
    else if (g_open == 3) { p = g_yrPanel;      n = LIVECHAR_YEAR_N; }
    else return -1;
    if (!PtInRect(&p, pt)) return -1;

    if (g_open == 1) {
        itw = (p.right - p.left) / DD_SKILL_COLS;
        col = (pt.x - p.left) / itw;
        if (col >= DD_SKILL_COLS) col = DD_SKILL_COLS - 1;
        row = (pt.y - p.top) / DD_ITEM_H;
        i = col * DD_SKILL_ROWS + row;
    } else if (g_open == 3) {
        itw = (p.right - p.left) / DD_YR_COLS;
        col = (pt.x - p.left) / itw;
        if (col >= DD_YR_COLS) col = DD_YR_COLS - 1;
        row = (pt.y - p.top) / DD_ITEM_H;
        i = row * DD_YR_COLS + col;
    } else {
        i = (pt.y - p.top) / DD_ITEM_H;
    }
    return (i >= 0 && i < n) ? i : -1;
}

// 생년 목록을 상자 아래에 펼친다. 창 밖으로 나가면 안으로/위로 밀어 넣는다.
static void OpenYearPanel(int ci, RECT anchor)
{
    int w = DD_YR_COLS * DD_YR_ITEM_W, hgt = DD_YR_ROWS * DD_ITEM_H;
    RECT p;
    p.left = anchor.left;   p.right  = p.left + w;
    p.top  = anchor.bottom; p.bottom = p.top + hgt;
    if (p.right > WIN_W - FRAME) { int d = p.right - (WIN_W - FRAME); p.left -= d; p.right -= d; }
    if (p.left < FRAME)          { p.left = FRAME; p.right = p.left + w; }
    if (p.bottom > WIN_H - FRAME) { p.top = anchor.top - hgt; p.bottom = p.top + hgt; }
    if (p.top < FRAME)            { p.top = FRAME; p.bottom = p.top + hgt; }
    g_yrPanel = p;
    g_yrChar  = ci;
    g_open    = 3;
}

int Nav_Click(HWND h, POINT pt)
{
    RECT r;

    // 승무원 목록은 하나 고르면 닫는다.
    if (g_open == 5) {
        int i = CrewPanelHit(pt), which = g_crWhich;
        g_open = 0; g_crWhich = -1;
        if (i >= 0) {
            // 자리만 비우면 그 인물은 "고용중인데 아무 데도 없는" 상태로 남아 술집에도
            // 안 나온다. 자리에서 내릴 땐 고용가능으로, 태울 땐 고용중으로 같이 맞춘다.
            int prev = LiveChar_Crew(which);
            int now  = i - 1;                          // 0 = 없음 -> -1
            if (LiveChar_SetCrew(which, now)) {
                if (prev >= 0 && prev != now) LiveChar_SetHire(prev, LIVECHAR_HIRE_FREE);
                if (now  >= 0)                LiveChar_SetHire(now,  LIVECHAR_HIRE_TAKEN);
            }
        }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    // 특기 편집 패널은 여러 개를 잇달아 고치는 판이라 열어 둔 채 값만 바꾼다(바깥 = 닫기).
    if (g_open == 4) {
        int id, lv;
        SkillPanelHit(pt, &id, &lv);
        if (id) LiveChar_SetSkill(LiveSlotOf(g_skChar), id, lv);
        else if (!PtInRect(&g_skPanel, pt)) { g_open = 0; g_skChar = -1; Rebuild(); }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    // 드롭다운이 열려 있으면 그쪽이 모든 클릭을 먼저 먹는다(항목 선택 / 바깥 클릭은 닫기).
    if (g_open) {
        int i = PanelHit(pt);
        int was = g_open, ci = g_yrChar;
        g_open = 0; g_yrChar = -1;
        if (i >= 0) {
            if      (was == 1) { g_skill = i; if (g_skill == 0) g_lvMin = 0; }
            else if (was == 2) { g_lvMin = i; }
            else {
                // 생년 -> 나이로 바꿔 실행 중 배열에 써넣는다. 세이브 파일은 그대로다.
                int slot = LiveSlotOf(ci);
                if (slot >= 0) LiveChar_SetBirthYear(slot, LIVECHAR_YEAR_MIN + i);
            }
            Rebuild();   // 나이가 바뀌면 미등장/은퇴 필터에 걸리는 사람이 달라진다
        }
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    if (LiveChar_Ready()) {
        int w;
        for (w = 0; w < LIVECHAR_CREW_N; w++) {
            RECT cb = RcCrewBox(w);
            if (PtInRect(&cb, pt)) { OpenCrewPanel(w, cb); InvalidateRect(h,NULL,FALSE); return 1; }
        }
    }

    r = RcReload();  if (PtInRect(&r, pt)) { Reload(h); return 1; }
    r = RcHirable(); if (PtInRect(&r, pt)) { g_onlyHirable = !g_onlyHirable; Rebuild(); InvalidateRect(h,NULL,FALSE); return 1; }
    r = RcGray();    if (PtInRect(&r, pt)) { g_showGray = !g_showGray; Rebuild(); InvalidateRect(h,NULL,FALSE); return 1; }
    r = RcSkill();   if (PtInRect(&r, pt)) { g_open = 1; InvalidateRect(h,NULL,FALSE); return 1; }
    // 특기 미선택이면 레벨은 의미가 없으므로 열지 않는다.
    r = RcLv();      if (PtInRect(&r, pt)) { if (g_skill > 0) { g_open = 2; InvalidateRect(h,NULL,FALSE); } return 1; }

    r = RcTrack();
    if (PtInRect(&r, pt)) {
        int mid = (r.top + r.bottom) / 2;
        ScrollTo(h, g_scroll + (pt.y < mid ? -NAV_ROWS : NAV_ROWS));
        return 1;
    }

    if (pt.x >= NAV_X && pt.x < NAV_X + NAV_W && pt.y >= NAV_Y && pt.y < NAV_Y + NAV_H) {
        int i = g_scroll + (pt.y - NAV_Y) / NAV_ROW_H;
        if (i < g_count) {
            // 그 줄의 생년 상자 / [특기] 버튼(실행 중 배열에서 짝을 찾은 인물만).
            int ry = NAV_Y + (i - g_scroll) * NAV_ROW_H;
            RECT yb = RcYearBox(ry), sb = RcSkillBtn(ry);
            int ci = g_list[i];
            if (LiveSlotOf(ci) >= 0) {
                if (PtInRect(&yb, pt)) { OpenYearPanel(ci, yb); InvalidateRect(h,NULL,FALSE); return 1; }
                if (PtInRect(&sb, pt)) { OpenSkillPanel(ci, sb); InvalidateRect(h,NULL,FALSE); return 1; }
            }
        }
        g_sel = (i < g_count) ? i : -1;
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    return 0;
}

int Nav_Key(HWND h, WPARAM wp)
{
    if (g_open) {   // 패널이 열려 있을 땐 ESC 로 닫기만 받는다
        if (wp == VK_ESCAPE) { g_open = 0; g_yrChar = -1; g_skChar = -1; g_crWhich = -1; InvalidateRect(h, NULL, FALSE); return 1; }
        return 1;
    }
    switch (wp) {
    case VK_UP:    ScrollTo(h, g_scroll - 1); return 1;
    case VK_DOWN:  ScrollTo(h, g_scroll + 1); return 1;
    case VK_PRIOR: ScrollTo(h, g_scroll - NAV_ROWS); return 1;
    case VK_NEXT:  ScrollTo(h, g_scroll + NAV_ROWS); return 1;
    case VK_HOME:  ScrollTo(h, 0); return 1;
    case VK_END:   ScrollTo(h, MaxScroll()); return 1;
    case VK_F5:    Reload(h); return 1;
    }
    return 0;
}

void Nav_Wheel(HWND h, int notches)
{
    if (g_open == 5) {   // 275명짜리 목록은 휠로 굴린다
        int mx = CrewMaxScroll(), s = g_crScroll - notches;
        if (s < 0) s = 0;
        if (s > mx) s = mx;
        if (s != g_crScroll) { g_crScroll = s; InvalidateRect(h, NULL, FALSE); }
        return;
    }
    if (g_open) return;
    ScrollTo(h, g_scroll - notches);
}

int Nav_Command(HWND h, WPARAM wp)
{
    // 자식 컨트롤을 두지 않으므로 받을 알림이 없다.
    // (EDIT/COMBOBOX 를 쓰던 판이 게임의 DirectDraw 화면 위에서 불안정해 전부 직접 그리기로 되돌렸다.)
    (void)h; (void)wp;
    return 0;
}

// ---- 수명 주기 ----
void Nav_Init(HWND parent, HINSTANCE hinst)
{
    (void)parent; (void)hinst;
}

void Nav_Activate(HWND h, int active)
{
    (void)h;
    if (!active) { g_open = 0; g_yrChar = -1; g_skChar = -1; g_crWhich = -1; return; }
    // 인물 배열은 게임이 세이브를 불러온 뒤에야 채워진다. 탭에 들어올 때마다 다시 확인한다.
    if (!LiveChar_Ready() && LiveChar_Load()) { ClearLiveCache(); Rebuild(); }
    if (!g_save.loaded) {
        Save_Load(&g_save);
        ClearLiveCache();
        Rebuild();
    }
}

void Nav_Destroy(void)
{
    Save_Free(&g_save);
    g_open = 0;
    g_yrChar = -1;
    g_skChar = -1;
    g_crWhich = -1;
}
