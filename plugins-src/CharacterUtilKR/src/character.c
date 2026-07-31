#include "character.h"
#include "ui.h"
#include "faces.h"
#include "chardb.h"
#include "navview.h"
#include <windowsx.h>

// fb15/fb16: 인물(얼굴) 코드 브라우저 — 갤러리(2열, 스크롤) + 남/여/카테고리 필터.
//   얼굴 = 80x96 8bpp(LS12 디코드), kFacePalette 로 컬러화(게임 캡처 역산 근사 팔레트).
//   상단 "파일" 메뉴에 "인물" 항목 추가(서브클래싱으로 클릭 가로챔) → 브라우저 오픈.
// fb31: 창을 탭 2개로 나눔 — [도감] 은 기존 얼굴 브라우저, [항해사 찾기] 는
//   SAVEDATA.CDS 를 읽어 고용 가능한 인물을 특기/레벨로 추려 보여준다(navview.c, 읽기 전용).

#define ID_CHAR   0xB301
#define WC_CHAR   L"CharUtilKR_Browser"

#define TAB_GALLERY 0
#define TAB_NAV     1

static HINSTANCE g_hinst = NULL;
static HWND    g_gameHwnd = NULL;
static HWND    g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static HWND    g_wnd = NULL;
static int     g_tab = TAB_GALLERY;
static int     g_gender = FACE_MALE;
static int     g_scroll = 0;   // 맨 위에 보이는 행
static int     g_catFilter = 0;// 0=전체 1=인물 2=여급 3=스폰서 4=기타
static int     g_filt[600];    // 현재 필터에 맞는 얼굴코드 목록
static int     g_filtCount = 0;

static int TotalRows(void) { return (g_filtCount + COLS - 1) / COLS; }
static int MaxScroll(void) { int m = TotalRows() - ROWS_VIS; return m < 0 ? 0 : m; }

// 현재 (성별, 카테고리)에 맞는 얼굴코드 목록을 g_filt 에 채운다.
static void RebuildFilter(void)
{
    int total = Face_Count(g_gender);
    int i;
    g_filtCount = 0;
    for (i = 0; i < total && g_filtCount < (int)(sizeof(g_filt)/sizeof(g_filt[0])); i++) {
        int cat = CharDb_Cat(g_gender, i);
        int ok = (g_catFilter == 0) ? 1 :
                 (g_catFilter == 4) ? (cat == 0) : (cat == g_catFilter);
        if (ok) g_filt[g_filtCount++] = i;
    }
    g_scroll = 0;
}

static const wchar_t* kCatBtn[5] = { L"전체", L"인물", L"여급", L"스폰서", L"기타" };
static RECT CloseRect(RECT c) { RECT r; r.right=c.right-FRAME-4; r.left=r.right-22; r.top=FRAME+4; r.bottom=r.top+18; return r; }
static RECT TabRect(int i)   { RECT r; r.left = i ? 85 : 13; r.right = r.left + (i ? 110 : 70); r.top=TAB_Y+2; r.bottom=TAB_Y+TAB_H-2; return r; }
static RECT MaleRect(void)   { RECT r; r.left=FRAME+8;  r.top=FILTER_Y; r.right=r.left+40; r.bottom=r.top+22; return r; }
static RECT FemaleRect(void) { RECT r; r.left=FRAME+50; r.top=FILTER_Y; r.right=r.left+40; r.bottom=r.top+22; return r; }
static RECT CatRect(int i)   { RECT r; r.left=FRAME+100 + i*54; r.right=r.left+50; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT SbTrack(void)    { RECT r; r.right=WIN_W-FRAME-2; r.left=r.right-SB_W; r.top=GY; r.bottom=GY+GAL_H; return r; }

static void PaintGallery(HDC dc)
{
    int r, c;
    RECT ir;
    wchar_t cnt[32];

    UI_Button(dc, MaleRect(),   L"남", g_gender==FACE_MALE);
    UI_Button(dc, FemaleRect(), L"여", g_gender==FACE_FEMALE);
    { int bi; for (bi=0;bi<5;bi++) UI_Button(dc, CatRect(bi), kCatBtn[bi], g_catFilter==bi); }

    wsprintfW(cnt, L"%d명", g_filtCount);
    ir.left=FRAME+380; ir.right=WIN_W-FRAME-8; ir.top=FILTER_Y; ir.bottom=FILTER_Y+22;
    UI_Text(dc, ir, cnt, g_font, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    for (r = 0; r < ROWS_VIS; r++) {
        int row = g_scroll + r;
        for (c = 0; c < COLS; c++) {
            int gi = row*COLS + c, face;
            int x = GX + c*(CELL_W+GAP);
            int y = GY + r*ROW_PITCH;
            if (gi >= g_filtCount) continue;
            face = g_filt[gi];
            Face_Draw(dc, x, y, PORT_W, PORT_H, g_gender, face);
            // 초상화 오른쪽 정보 패널 (이름/코드 + 카테고리별 상세)
            { int ix = x + PORT_W + 8; RECT lr; wchar_t hd[80];
              const wchar_t* nm = CharDb_Name(g_gender, face);
              const wchar_t* nf = CharDb_Info(g_gender, face);
              lr.left=ix-2; lr.top=y; lr.right=ix+INFO_W; lr.bottom=y+PORT_H;
              { HBRUSH b2=CreateSolidBrush(COL_DISP_BG); FillRect(dc,&lr,b2); DeleteObject(b2);
                b2=CreateSolidBrush(COL_DARK); FrameRect(dc,&lr,b2); DeleteObject(b2); }
              wsprintfW(hd, L"%s  #%d", nm[0]?nm:L"(무명)", face);
              lr.left=ix+2; lr.right=ix+INFO_W-2; lr.top=y+3; lr.bottom=y+20;
              UI_Text(dc, lr, hd, g_font, COL_TEXT, DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
              lr.top=y+22; lr.bottom=y+PORT_H-3;
              UI_Text(dc, lr, nf, g_smallFont, COL_TEXT, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL); }
        }
    }

    UI_Scrollbar(dc, SbTrack(), g_scroll, MaxScroll(), ROWS_VIS, TotalRows());
}

static void OnPaint(HWND h)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc, tb, tr; HBRUSH br;

    GetClientRect(h, &rc);
    br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);

    // 타이틀바
    tb.left=FRAME; tb.top=FRAME; tb.right=rc.right-FRAME; tb.bottom=FRAME+TITLE_H;
    UI_VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT); UI_Bevel(dc, tb, FALSE);
    tr = tb; tr.left += 8;
    UI_Text(dc, tr, L"인물 브라우저", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    UI_Button(dc, CloseRect(rc), L"×", FALSE);

    // 탭바
    UI_Button(dc, TabRect(TAB_GALLERY), L"도감",      g_tab==TAB_GALLERY);
    UI_Button(dc, TabRect(TAB_NAV),     L"항해사 찾기", g_tab==TAB_NAV);

    if (g_tab == TAB_NAV) Nav_Paint(dc);
    else                  PaintGallery(dc);

    EndPaint(h, &ps);
}

static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    if (row < 0) row = 0;
    if (row > mx) row = mx;
    if (row != g_scroll) { g_scroll = row; InvalidateRect(h, NULL, FALSE); }
}
static void SetGender(HWND h, int g)
{
    if (g==g_gender) return;
    g_gender = g;
    if (g==FACE_MALE && g_catFilter==2) g_catFilter = 0;  // 남인데 여급 필터면 전체로(여급은 여성만)
    RebuildFilter(); InvalidateRect(h,NULL,FALSE);
}
static void SetCat(HWND h, int c)
{
    if (c==g_catFilter) return;
    g_catFilter = c;
    if (c==2) g_gender = FACE_FEMALE;   // 여급은 전원 여성 → 자동으로 여 선택
    RebuildFilter(); InvalidateRect(h,NULL,FALSE);
}
static void SetTab(HWND h, int t)
{
    if (t == g_tab) return;
    g_tab = t;
    Nav_Activate(h, t == TAB_NAV);
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK CharProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        Face_Load();
        RebuildFilter();
        Nav_Init(h, g_hinst);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_COMMAND:
        if (Nav_Command(h, wp)) return 0;
        return DefWindowProcW(h, m, wp, lp);
    case WM_MOUSEWHEEL: {
        int notches = GET_WHEEL_DELTA_WPARAM(wp) / 120;
        if (g_tab == TAB_NAV) Nav_Wheel(h, notches);
        else                  ScrollTo(h, g_scroll - notches);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt; RECT rc; pt.x=GET_X_LPARAM(lp); pt.y=GET_Y_LPARAM(lp);
        GetClientRect(h,&rc);
        // 이름 검색칸(EDIT)에 포커스가 남아 있으면 방향키 스크롤이 먹지 않는다.
        // 창 바닥을 누른 시점에 포커스를 되가져온다(자식 클릭은 여기로 오지 않는다).
        SetFocus(h);
        { RECT cb=CloseRect(rc); if (PtInRect(&cb,pt)) { DestroyWindow(h); return 0; } }
        { RECT r=TabRect(TAB_GALLERY); if (PtInRect(&r,pt)) { SetTab(h,TAB_GALLERY); return 0; } }
        { RECT r=TabRect(TAB_NAV);     if (PtInRect(&r,pt)) { SetTab(h,TAB_NAV); return 0; } }
        if (g_tab == TAB_NAV) {
            if (Nav_Click(h, pt)) return 0;
        } else {
            { RECT r=MaleRect();   if (PtInRect(&r,pt)) { SetGender(h,FACE_MALE); return 0; } }
            { RECT r=FemaleRect(); if (PtInRect(&r,pt)) { SetGender(h,FACE_FEMALE); return 0; } }
            { int bi; for (bi=0;bi<5;bi++){ RECT r=CatRect(bi); if (PtInRect(&r,pt)) { SetCat(h,bi); return 0; } } }
            { RECT sb=SbTrack(); if (PtInRect(&sb,pt)) {   // 트랙 클릭 = 페이지 이동
                int mid=(sb.top+sb.bottom)/2;
                ScrollTo(h, g_scroll + (pt.y<mid?-ROWS_VIS:ROWS_VIS)); return 0; } }
        }
        if (pt.y < FRAME+TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (wp == VK_TAB)    { SetTab(h, g_tab == TAB_NAV ? TAB_GALLERY : TAB_NAV); return 0; }
        if (g_tab == TAB_NAV) {
            if (Nav_Key(h, wp)) return 0;
            return 0;
        }
        switch (wp) {
        case VK_UP:    ScrollTo(h, g_scroll-1); return 0;
        case VK_DOWN:  ScrollTo(h, g_scroll+1); return 0;
        case VK_PRIOR: ScrollTo(h, g_scroll-ROWS_VIS); return 0;
        case VK_NEXT:  ScrollTo(h, g_scroll+ROWS_VIS); return 0;
        case VK_HOME:  ScrollTo(h, 0); return 0;
        case VK_END:   ScrollTo(h, MaxScroll()); return 0;
        case 'M':      SetGender(h,FACE_MALE); return 0;
        case 'F':      SetGender(h,FACE_FEMALE); return 0;
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
    g_wnd = CreateWindowExW(0, WC_CHAR, L"인물 브라우저", WS_POPUP, x, y, WIN_W, WIN_H, owner, NULL, hinst, NULL);
    if (g_wnd) {
        Nav_Activate(g_wnd, g_tab == TAB_NAV);
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
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, L"인물") == 0) return TRUE;
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
                    AppendMenuW(target, MF_STRING, ID_CHAR, L"인물");
                    DrawMenuBar(g_gameHwnd);
                    OutputDebugStringW(L"[CharacterUtilKR] 인물 menu (re)installed.");
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
