#include "navview.h"
#include "ui.h"
#include "faces.h"
#include "savedata.h"

// ---- 목록 레이아웃 ----
#define NAV_X      (FRAME + GAP)                            // 13
#define NAV_Y      (FRAME + TITLE_H + TAB_H + FILTER_H + 6) // 91
#define NAV_ROW_H  64
#define NAV_ROWS   8
#define NAV_H      (NAV_ROW_H * NAV_ROWS)                   // 512
#define NAV_W      (WIN_W - NAV_X - GAP - SB_W - FRAME)     // 722
#define ROW_PORT_W 40
#define ROW_PORT_H 48

// 드롭다운(직접 그림)
#define DD_ITEM_H  22
#define DD_SKILL_COLS 2
#define DD_SKILL_ROWS 14                    // (전체) + 특기 27종 = 28 = 2열 x 14행
#define DD_LV_N    6                        // 전체 / 1~5

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
static int   g_open = 0;          // 열려 있는 드롭다운: 0=없음 1=특기 2=Lv

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
static int IsGray(const SaveChar* c) { return c->age < 18 || c->age > 60; }

static int Pass(const SaveChar* c)
{
    if (g_onlyHirable && !(c->hire == 2 && c->loc != 255)) return 0;
    if (!g_showGray && IsGray(c)) return 0;
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
        if (Pass(&g_save.chars[i])) g_list[g_count++] = i;

    // 삽입 정렬. 많아야 수백 건이라 이 정도면 충분하다.
    for (i = 1; i < g_count; i++) {
        v = g_list[i];
        for (j = i - 1; j >= 0 && Before(&g_save.chars[v], &g_save.chars[g_list[j]]); j--)
            g_list[j+1] = g_list[j];
        g_list[j+1] = v;
    }

    g_scroll = 0;
    g_sel = -1;
}

static void Reload(HWND h)
{
    Save_Load(&g_save);
    Rebuild();
    InvalidateRect(h, NULL, FALSE);
}

// ---- 그리기 ----

// 콤보박스처럼 보이는 눌림 상자 + 오른쪽 ▼.
static void DrawSelect(HDC dc, RECT r, const wchar_t* text, BOOL open)
{
    RECT t = r, a;
    HBRUSH br = CreateSolidBrush(open ? COL_FACE_TOP : COL_DISP_BG);
    FillRect(dc, &r, br); DeleteObject(br);
    UI_Bevel(dc, r, TRUE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &r, br); DeleteObject(br);

    t.left += 6; t.right -= 20;
    UI_Text(dc, t, text, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    a = r; a.left = r.right - 18; a.right = r.right - 2; a.top += 2; a.bottom -= 2;
    UI_VGradient(dc, a, COL_FACE_TOP, COL_FACE_BOT);
    UI_Bevel(dc, a, open);
    UI_Text(dc, a, L"▼", g_smallFont, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

static const wchar_t* SkillText(int id) { return id == 0 ? L"(전체)" : Save_SkillName(id); }

// 열린 드롭다운 패널. 목록 위에 덮어 그리므로 Nav_Paint 의 맨 마지막에 호출한다.
static void DrawPanel(HDC dc)
{
    RECT p, it;
    HBRUSH br;
    int i, n, cols, itw;

    if (g_open == 1)      { p = RcSkillPanel(); n = 1 + SAVE_SKILL_MAX; cols = DD_SKILL_COLS; }
    else if (g_open == 2) { p = RcLvPanel();    n = DD_LV_N;            cols = 1; }
    else return;

    itw = (p.right - p.left) / cols;

    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &p, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK);    FrameRect(dc, &p, br); DeleteObject(br);

    for (i = 0; i < n; i++) {
        int col = (cols > 1) ? (i / DD_SKILL_ROWS) : 0;
        int row = (cols > 1) ? (i % DD_SKILL_ROWS) : i;
        int cur = (g_open == 1) ? (i == g_skill) : (i == g_lvMin);
        it.left   = p.left + col * itw + 1;
        it.right  = it.left + itw - 2;
        it.top    = p.top + row * DD_ITEM_H + 1;
        it.bottom = it.top + DD_ITEM_H - 1;
        if (cur) {
            br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &it, br); DeleteObject(br);
        }
        { RECT t = it; t.left += 6;
          UI_Text(dc, t, (g_open == 1) ? SkillText(i) : kLvText[i], g_font,
                  cur ? RGB(250,244,228) : COL_TEXT,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX); }
    }
}

static void PaintRow(HDC dc, int y, const SaveChar* c, int selected)
{
    RECT r, tr;
    HBRUSH br;
    COLORREF fg = COL_TEXT;
    wchar_t buf[192], gen[128], lang[128];
    int k, poor = (c->fame > g_save.playerFame);
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
    UI_Text(dc, tr, buf, g_smallFont, fg, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    // 3행: 소재 / 건물 / 나이 + 언어 특기
    {
        const wchar_t* bn = Save_BuildingName(c->bldg);
        if (bn[0]) wsprintfW(buf, L"%s · %s · %d세", Save_CityName(c->loc), bn, c->age);
        else       wsprintfW(buf, L"%s · %d세", Save_CityName(c->loc), c->age);
        tr.left = tx; tr.right = tx + 190; tr.top = y + 42; tr.bottom = y + 60;
        UI_Text(dc, tr, buf, g_smallFont, fg, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    lang[0] = 0;
    for (k = SAVE_SKILL_LANG0; k <= SAVE_SKILL_MAX; k++)
        if (c->skill[k]) AppendSkill(lang, 128, k, c->skill[k]);
    if (lang[0]) {
        tr.left = tx + 196; tr.right = right - 6;
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
    DrawSelect(dc, RcSkill(), SkillText(g_skill), g_open == 1);
    UI_Text(dc, RcLvLbl(), L"Lv", g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    DrawSelect(dc, RcLv(), g_skill > 0 ? kLvText[g_lvMin] : L"-", g_open == 2);

    if (g_save.loaded) wsprintfW(buf, L"내명성 %d · %d명", g_save.playerFame, g_count);
    else               lstrcpyW(buf, L"SAVEDATA.CDS 없음");
    UI_Text(dc, RcInfo(), buf, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

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
        PaintRow(dc, NAV_Y + r * NAV_ROW_H, &g_save.chars[g_list[i]], i == g_sel);
    }

    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), NAV_ROWS, g_count);
    DrawPanel(dc);   // 목록 위에 덮어 그린다
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
    else return -1;
    if (!PtInRect(&p, pt)) return -1;

    if (g_open == 1) {
        itw = (p.right - p.left) / DD_SKILL_COLS;
        col = (pt.x - p.left) / itw;
        if (col >= DD_SKILL_COLS) col = DD_SKILL_COLS - 1;
        row = (pt.y - p.top) / DD_ITEM_H;
        i = col * DD_SKILL_ROWS + row;
    } else {
        i = (pt.y - p.top) / DD_ITEM_H;
    }
    return (i >= 0 && i < n) ? i : -1;
}

int Nav_Click(HWND h, POINT pt)
{
    RECT r;

    // 드롭다운이 열려 있으면 그쪽이 모든 클릭을 먼저 먹는다(항목 선택 / 바깥 클릭은 닫기).
    if (g_open) {
        int i = PanelHit(pt);
        int was = g_open;
        g_open = 0;
        if (i >= 0) {
            if (was == 1) { g_skill = i; if (g_skill == 0) g_lvMin = 0; }
            else          { g_lvMin = i; }
            Rebuild();
        }
        InvalidateRect(h, NULL, FALSE);
        return 1;
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
        g_sel = (i < g_count) ? i : -1;
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    return 0;
}

int Nav_Key(HWND h, WPARAM wp)
{
    if (g_open) {   // 패널이 열려 있을 땐 ESC 로 닫기만 받는다
        if (wp == VK_ESCAPE) { g_open = 0; InvalidateRect(h, NULL, FALSE); return 1; }
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
    if (!active) { g_open = 0; return; }
    if (!g_save.loaded) {
        Save_Load(&g_save);
        Rebuild();
    }
}

void Nav_Destroy(void)
{
    Save_Free(&g_save);
    g_open = 0;
}
