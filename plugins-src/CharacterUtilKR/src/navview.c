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

#define IDC_NAME   0x2101

static HWND  g_edit = NULL;

static SaveData g_save;
static int   g_list[512];      // 필터를 통과한 인물의 g_save.chars 인덱스
static int   g_count = 0;
static int   g_scroll = 0;
static int   g_sel = -1;       // g_list 상의 선택 위치

// ---- 필터 상태 ----
static int   g_onlyHirable = 1;   // 고용가능(2) & 함대소속 아님
static int   g_showGray = 0;      // 18세 미만 / 60세 초과 포함
static int   g_skill = 1;         // 정렬/필터 기준 특기 (1=항해술)
static int   g_lvMin = 3;         // 0 = 레벨 제한 없음
static wchar_t g_name[32] = L"";

// ---- 필터바 버튼 위치 ----
static RECT Rc(int x, int w) { RECT r; r.left=x; r.right=x+w; r.top=FILTER_Y; r.bottom=FILTER_Y+22; return r; }
static RECT RcReload(void)  { return Rc(13, 64); }
static RECT RcHirable(void) { return Rc(81, 78); }
static RECT RcGray(void)    { return Rc(163, 66); }
static RECT RcSkillL(void)  { return Rc(235, 18); }
static RECT RcSkill(void)   { return Rc(253, 88); }
static RECT RcSkillR(void)  { return Rc(341, 18); }
static RECT RcLvL(void)     { return Rc(365, 18); }
static RECT RcLv(void)      { return Rc(383, 46); }
static RECT RcLvR(void)     { return Rc(429, 18); }
static RECT RcNameLbl(void) { return Rc(453, 34); }
static RECT RcInfo(void)    { return Rc(605, WIN_W - FRAME - 8 - 605); }
static RECT RcTrack(void)
{
    RECT r;
    r.right = WIN_W - FRAME - 2; r.left = r.right - SB_W;
    r.top = NAV_Y; r.bottom = NAV_Y + NAV_H;
    return r;
}

static int MaxScroll(void) { int m = g_count - NAV_ROWS; return m < 0 ? 0 : m; }

// ---- 문자열 도우미 ----
static int Contains(const wchar_t* hay, const wchar_t* needle)
{
    int nl = lstrlenW(needle), i, j;
    if (nl == 0) return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; j < nl && hay[i+j] == needle[j]; j++) {}
        if (j == nl) return 1;
    }
    return 0;
}

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
    if (g_lvMin > 0 && c->skill[g_skill] < g_lvMin) return 0;
    if (g_name[0] && !Contains(c->name, g_name)) return 0;
    return 1;
}

// 선택 특기 레벨 내림차순 → 명성 오름차순(고용 문턱이 낮은 쪽 먼저) → 등장 순.
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

    UI_Button(dc, RcSkillL(), L"◀", FALSE);
    UI_Button(dc, RcSkill(),  Save_SkillName(g_skill), TRUE);
    UI_Button(dc, RcSkillR(), L"▶", FALSE);

    UI_Button(dc, RcLvL(), L"◀", FALSE);
    if (g_lvMin > 0) wsprintfW(buf, L"Lv%d↑", g_lvMin); else lstrcpyW(buf, L"전체");
    UI_Button(dc, RcLv(),  buf, g_lvMin > 0);
    UI_Button(dc, RcLvR(), L"▶", FALSE);

    UI_Text(dc, RcNameLbl(), L"이름", g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    if (g_save.loaded) wsprintfW(buf, L"내명성 %d · %d명", g_save.playerFame, g_count);
    else               lstrcpyW(buf, L"SAVEDATA.CDS 없음");
    UI_Text(dc, RcInfo(), buf, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    if (!g_save.loaded || g_count == 0) {
        RECT e;
        e.left = NAV_X; e.right = NAV_X + NAV_W; e.top = NAV_Y; e.bottom = NAV_Y + 60;
        UI_Text(dc, e,
                g_save.loaded ? L"조건에 맞는 인물이 없습니다. 특기/레벨을 낮춰 보세요."
                              : L"게임 폴더에서 SAVEDATA.CDS 를 읽지 못했습니다. 한 번 저장한 뒤 새로고침하세요.",
                g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        return;
    }

    for (r = 0; r < NAV_ROWS; r++) {
        int i = g_scroll + r;
        if (i >= g_count) break;
        PaintRow(dc, NAV_Y + r * NAV_ROW_H, &g_save.chars[g_list[i]], i == g_sel);
    }

    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), NAV_ROWS, g_count);
}

// ---- 입력 ----
static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    if (row < 0) row = 0;
    if (row > mx) row = mx;
    if (row != g_scroll) { g_scroll = row; InvalidateRect(h, NULL, FALSE); }
}

static void SetSkill(HWND h, int delta)
{
    g_skill += delta;
    if (g_skill < 1) g_skill = SAVE_SKILL_MAX;
    if (g_skill > SAVE_SKILL_MAX) g_skill = 1;
    Rebuild();
    InvalidateRect(h, NULL, FALSE);
}

static void SetLv(HWND h, int delta)
{
    g_lvMin += delta;
    if (g_lvMin < 0) g_lvMin = 5;
    if (g_lvMin > 5) g_lvMin = 0;
    Rebuild();
    InvalidateRect(h, NULL, FALSE);
}

int Nav_Click(HWND h, POINT pt)
{
    RECT r;

    r = RcReload();  if (PtInRect(&r, pt)) { Reload(h); return 1; }
    r = RcHirable(); if (PtInRect(&r, pt)) { g_onlyHirable = !g_onlyHirable; Rebuild(); InvalidateRect(h,NULL,FALSE); return 1; }
    r = RcGray();    if (PtInRect(&r, pt)) { g_showGray = !g_showGray; Rebuild(); InvalidateRect(h,NULL,FALSE); return 1; }
    r = RcSkillL();  if (PtInRect(&r, pt)) { SetSkill(h, -1); return 1; }
    r = RcSkillR();  if (PtInRect(&r, pt)) { SetSkill(h, +1); return 1; }
    r = RcSkill();   if (PtInRect(&r, pt)) { SetSkill(h, +1); return 1; }
    r = RcLvL();     if (PtInRect(&r, pt)) { SetLv(h, -1); return 1; }
    r = RcLvR();     if (PtInRect(&r, pt)) { SetLv(h, +1); return 1; }
    r = RcLv();      if (PtInRect(&r, pt)) { SetLv(h, +1); return 1; }

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

void Nav_Wheel(HWND h, int notches) { ScrollTo(h, g_scroll - notches); }

int Nav_Command(HWND h, WPARAM wp)
{
    if (LOWORD(wp) == IDC_NAME && HIWORD(wp) == EN_CHANGE) {
        GetWindowTextW(g_edit, g_name, 32);
        Rebuild();
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    return 0;
}

// ---- 수명 주기 ----
void Nav_Init(HWND parent, HINSTANCE hinst)
{
    if (g_edit) return;
    // 이름 검색만 진짜 EDIT 컨트롤을 쓴다. 한글 입력(IME)/캐럿을 직접 구현하는 것보다
    // 이쪽이 훨씬 짧고 안전하다. 나머지 필터는 창 그림과 같은 결로 직접 그린다.
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | ES_AUTOHSCROLL,
                             489, FILTER_Y + 1, 110, 20,
                             parent, (HMENU)(UINT_PTR)IDC_NAME, hinst, NULL);
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
}

void Nav_Activate(HWND h, int active)
{
    if (g_edit) ShowWindow(g_edit, active ? SW_SHOW : SW_HIDE);
    if (active && !g_save.loaded) {
        Save_Load(&g_save);
        Rebuild();
    }
}

void Nav_Destroy(void)
{
    Save_Free(&g_save);
    g_edit = NULL;   // 부모가 없어질 때 같이 파괴된다
}
