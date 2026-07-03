#include "character.h"
#include "ls12.h"
#include "face_palette.h"   // kFacePalette[768]
#include "char_names.h"     // kMaleNames[], kFemaleNames[]
#include <windowsx.h>
#include <stdio.h>

// fb15/fb16: 인물(얼굴) 코드 브라우저 — 갤러리(4x3 그리드, 스크롤) + 남/여 필터.
//   얼굴 = 80x96 8bpp(LS12 디코드), kFacePalette 로 컬러화(게임 캡처 역산 근사 팔레트).
//   상단 메뉴에 "인물" 항목 추가(서브클래싱으로 클릭 가로챔) → 브라우저 오픈.

#define ID_CHAR   0xB301
#define WC_CHAR   L"CharUtilKR_Browser"

#define FRAME     3
#define TITLE_H   26
#define FILTER_H  30
#define CELL_W    110                     // fb18: 6열에 맞춰 축소
#define CELL_H    132
#define LABEL_H   30                       // 코드 + 인물명 2줄
#define COLS      6
#define ROWS_VIS  4
#define GAP       8
#define ROW_PITCH (CELL_H + LABEL_H + GAP) // 216
#define GX        (FRAME + GAP)
#define GY        (FRAME + TITLE_H + FILTER_H + GAP)
#define GAL_H     (ROWS_VIS * ROW_PITCH)
#define SB_W      12                       // 스크롤바 폭
#define WIN_W     (GX + COLS*CELL_W + (COLS-1)*GAP + GAP + SB_W + FRAME)
#define WIN_H     (GY + GAL_H + FRAME)

#define COL_BG        RGB(150,130,105)
#define COL_FACE_TOP  RGB(216,201,176)
#define COL_FACE_BOT  RGB(158,138,113)
#define COL_LIGHT     RGB(238,228,208)
#define COL_DARK      RGB( 90, 75, 60)
#define COL_TEXT      RGB( 55, 40, 25)
#define COL_DISP_BG   RGB(206,194,171)
#define COL_SEL_BG    RGB(120,100, 80)   // 활성 필터 버튼

static HINSTANCE g_hinst = NULL;
static HWND    g_gameHwnd = NULL;
static HWND    g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static HWND    g_wnd = NULL;
static HFONT   g_font = NULL;
static HFONT   g_smallFont = NULL;
static Ls12File g_male, g_female;
static int     g_loaded = 0;
static int     g_gender = 0;   // 0=남(MALE), 1=여(FEMALE)
static int     g_scroll = 0;   // 맨 위에 보이는 행
static int     g_catFilter = 0;// 0=전체 1=인물 2=여급 3=스폰서 4=기타
static int     g_filt[600];    // 현재 필터에 맞는 얼굴코드 목록
static int     g_filtCount = 0;
static unsigned char g_idx[LS12_FACE_SZ];

static void VGradient(HDC dc, RECT r, COLORREF top, COLORREF bot)
{
    int h = r.bottom - r.top, i;
    if (h <= 0) return;
    for (i = 0; i < h; i++) {
        int rr = GetRValue(top) + (GetRValue(bot) - GetRValue(top)) * i / h;
        int gg = GetGValue(top) + (GetGValue(bot) - GetGValue(top)) * i / h;
        int bb = GetBValue(top) + (GetBValue(bot) - GetBValue(top)) * i / h;
        RECT ln; HBRUSH br = CreateSolidBrush(RGB(rr, gg, bb));
        ln.left = r.left; ln.right = r.right; ln.top = r.top + i; ln.bottom = r.top + i + 1;
        FillRect(dc, &ln, br); DeleteObject(br);
    }
}
static void Bevel(HDC dc, RECT r, BOOL sunken)
{
    COLORREF lt = sunken ? COL_DARK : COL_LIGHT, dk = sunken ? COL_LIGHT : COL_DARK;
    HPEN pl = CreatePen(PS_SOLID,1,lt), pd = CreatePen(PS_SOLID,1,dk);
    HPEN old = (HPEN)SelectObject(dc, pl);
    MoveToEx(dc, r.left, r.bottom-1, NULL);
    LineTo(dc, r.left, r.top); LineTo(dc, r.right-1, r.top);
    SelectObject(dc, pd);
    LineTo(dc, r.right-1, r.bottom-1); LineTo(dc, r.left, r.bottom-1);
    SelectObject(dc, old); DeleteObject(pl); DeleteObject(pd);
}

static Ls12File* CurFile(void) { return g_gender ? &g_female : &g_male; }

// 얼굴 코드 -> 인물명(없으면 빈 문자열)
static const wchar_t* NameOf(int idx)
{
    if (idx < 0) return L"";
    if (g_gender == 0) return idx < (int)(sizeof(kMaleNames)/sizeof(kMaleNames[0]))   ? kMaleNames[idx]   : L"";
    else               return idx < (int)(sizeof(kFemaleNames)/sizeof(kFemaleNames[0])) ? kFemaleNames[idx] : L"";
}

static int TotalRows(void) { return (g_filtCount + COLS - 1) / COLS; }
static int MaxScroll(void) { int m = TotalRows() - ROWS_VIS; return m < 0 ? 0 : m; }

// 현재 (성별, 카테고리)에 맞는 얼굴코드 목록을 g_filt 에 채운다.
static void RebuildFilter(void)
{
    Ls12File* f = CurFile();
    const unsigned char* cats = g_gender ? kFemaleCat : kMaleCat;
    int csz = g_gender ? (int)(sizeof(kFemaleCat)) : (int)(sizeof(kMaleCat));
    int i;
    g_filtCount = 0;
    for (i = 0; i < f->count && g_filtCount < (int)(sizeof(g_filt)/sizeof(g_filt[0])); i++) {
        int cat = (i < csz) ? cats[i] : 0;
        int ok = (g_catFilter == 0) ? 1 :
                 (g_catFilter == 4) ? (cat == 0) : (cat == g_catFilter);
        if (ok) g_filt[g_filtCount++] = i;
    }
    g_scroll = 0;
}

static void LoadFiles(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], path[MAX_PATH]; char* p;
    if (g_loaded) return;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    lstrcpynA(dir, exe, MAX_PATH);
    p = dir; { char* last = dir; while (*p) { if (*p=='\\'||*p=='/') last = p; p++; } *last = 0; }
    wsprintfA(path, "%s\\MALE.CDS", dir);   Ls12_Open(&g_male, path);
    wsprintfA(path, "%s\\FEMALE.CDS", dir); Ls12_Open(&g_female, path);
    g_loaded = 1;
}

static void DrawFaceAt(HDC dc, int x, int y, int index)
{
    static unsigned char rgb[LS12_FACE_SZ * 3];
    BITMAPINFO bi; int i;
    Ls12File* f = CurFile();
    RECT box; box.left=x-1; box.top=y-1; box.right=x+CELL_W+1; box.bottom=y+CELL_H+1;
    if (index < 0 || index >= f->count || !Ls12_DecodeFace(f, index, g_idx)) {
        HBRUSH br = CreateSolidBrush(COL_BG); FillRect(dc, &box, br); DeleteObject(br);
        return;
    }
    for (i = 0; i < LS12_FACE_SZ; i++) {
        unsigned char v = g_idx[i];
        rgb[i*3+0] = kFacePalette[v*3+2];
        rgb[i*3+1] = kFacePalette[v*3+1];
        rgb[i*3+2] = kFacePalette[v*3+0];
    }
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = LS12_FACE_W;
    bi.bmiHeader.biHeight = -LS12_FACE_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, CELL_W, CELL_H, 0, 0, LS12_FACE_W, LS12_FACE_H, rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
    { HBRUSH br = CreateSolidBrush(COL_DARK); FrameRect(dc, &box, br); DeleteObject(br); }
}

static const wchar_t* kCatBtn[5] = { L"전체", L"인물", L"여급", L"스폰서", L"기타" };
static RECT CloseRect(RECT c) { RECT r; r.right=c.right-FRAME-4; r.left=r.right-22; r.top=FRAME+4; r.bottom=r.top+18; return r; }
static RECT MaleRect(void)   { RECT r; r.left=FRAME+8;  r.top=FRAME+TITLE_H+5; r.right=r.left+40; r.bottom=r.top+20; return r; }
static RECT FemaleRect(void) { RECT r; r.left=FRAME+50; r.top=FRAME+TITLE_H+5; r.right=r.left+40; r.bottom=r.top+20; return r; }
static RECT CatRect(int i)   { RECT r; r.left=FRAME+100 + i*54; r.right=r.left+50; r.top=FRAME+TITLE_H+5; r.bottom=r.top+20; return r; }
static RECT SbTrack(void)    { RECT r; r.right=WIN_W-FRAME-2; r.left=r.right-SB_W; r.top=GY; r.bottom=GY+GAL_H; return r; }

static void DrawButton(HDC dc, RECT r, const wchar_t* t, BOOL active)
{
    HBRUSH br = CreateSolidBrush(COL_BG); FillRect(dc, &r, br); DeleteObject(br);
    br = CreateSolidBrush(COL_TEXT); FrameRect(dc, &r, br); DeleteObject(br);
    { RECT f=r; InflateRect(&f,-2,-2);
      if (active) { HBRUSH b2=CreateSolidBrush(COL_SEL_BG); FillRect(dc,&f,b2); DeleteObject(b2); Bevel(dc,f,TRUE); }
      else        { VGradient(dc,f,COL_FACE_TOP,COL_FACE_BOT); Bevel(dc,f,FALSE); } }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, active?RGB(250,244,228):COL_TEXT);
    { HFONT of=(HFONT)SelectObject(dc, g_font);
      DrawTextW(dc, t, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE); SelectObject(dc, of); }
}

static void OnPaint(HWND h)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc, tb, cb, tr, sb; HBRUSH br; HFONT of;
    int r, c, total, tr_rows, i;
    Ls12File* f = CurFile();
    GetClientRect(h, &rc);
    br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);
    // 타이틀바
    tb.left=FRAME; tb.top=FRAME; tb.right=rc.right-FRAME; tb.bottom=FRAME+TITLE_H;
    VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, tb, FALSE);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    of=(HFONT)SelectObject(dc,g_font); tr=tb; tr.left+=8;
    DrawTextW(dc, L"인물 브라우저", -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,of);
    cb = CloseRect(rc); DrawButton(dc, cb, L"×", FALSE);
    // 성별 + 카테고리 필터 버튼
    DrawButton(dc, MaleRect(),   L"남", g_gender==0);
    DrawButton(dc, FemaleRect(), L"여", g_gender==1);
    { int bi; for (bi=0;bi<5;bi++) DrawButton(dc, CatRect(bi), kCatBtn[bi], g_catFilter==bi); }
    { wchar_t cnt[32]; RECT ir;
      wsprintfW(cnt, L"%d명", g_filtCount);
      ir.left=FRAME+380; ir.right=rc.right-FRAME-8; ir.top=FRAME+TITLE_H+5; ir.bottom=ir.top+20;
      SetTextColor(dc,COL_TEXT); of=(HFONT)SelectObject(dc,g_font);
      DrawTextW(dc,cnt,-1,&ir,DT_RIGHT|DT_VCENTER|DT_SINGLELINE); SelectObject(dc,of); }
    (void)f;
    // 갤러리 (필터된 목록 g_filt 기준)
    for (r = 0; r < ROWS_VIS; r++) {
        int row = g_scroll + r;
        for (c = 0; c < COLS; c++) {
            int gi = row*COLS + c, face;
            int x = GX + c*(CELL_W+GAP);
            int y = GY + r*ROW_PITCH;
            if (gi >= g_filtCount) continue;
            face = g_filt[gi];
            DrawFaceAt(dc, x, y, face);
            { wchar_t lb[16]; RECT lr; const wchar_t* nm = NameOf(face);
              of=(HFONT)SelectObject(dc,g_smallFont); SetTextColor(dc,COL_TEXT);
              wsprintfW(lb, L"%d", face);
              lr.left=x; lr.right=x+CELL_W; lr.top=y+CELL_H+1; lr.bottom=y+CELL_H+15;
              DrawTextW(dc,lb,-1,&lr,DT_CENTER|DT_SINGLELINE);
              lr.top=y+CELL_H+15; lr.bottom=y+CELL_H+29;
              DrawTextW(dc,nm,-1,&lr,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
              SelectObject(dc,of); }
        }
    }
    // 스크롤바
    sb = SbTrack();
    br=CreateSolidBrush(COL_DISP_BG); FillRect(dc,&sb,br); DeleteObject(br);
    Bevel(dc, sb, TRUE);
    total = TotalRows(); tr_rows = total<1?1:total;
    { int trackh=sb.bottom-sb.top;
      int thumbh = trackh * (ROWS_VIS<tr_rows?ROWS_VIS:tr_rows) / tr_rows; if(thumbh<16)thumbh=16;
      int maxs=MaxScroll(); int ty = sb.top + (maxs>0 ? (trackh-thumbh)*g_scroll/maxs : 0);
      RECT th; th.left=sb.left+1; th.right=sb.right-1; th.top=ty; th.bottom=ty+thumbh;
      VGradient(dc,th,COL_FACE_TOP,COL_FACE_BOT); Bevel(dc,th,FALSE); }
    (void)i;
    EndPaint(h, &ps);
}

static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    if (row < 0) row = 0; if (row > mx) row = mx;
    if (row != g_scroll) { g_scroll = row; InvalidateRect(h, NULL, FALSE); }
}
static void SetGender(HWND h, int g)
{
    if (g==g_gender) return;
    g_gender = g;
    if (g==0 && g_catFilter==2) g_catFilter = 0;  // 남인데 여급 필터면 전체로(여급은 여성만)
    RebuildFilter(); InvalidateRect(h,NULL,FALSE);
}
static void SetCat(HWND h, int c)
{
    if (c==g_catFilter) return;
    g_catFilter = c;
    if (c==2) g_gender = 1;   // 여급은 전원 여성 → 자동으로 여 선택
    RebuildFilter(); InvalidateRect(h,NULL,FALSE);
}

static LRESULT CALLBACK CharProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g_font      = CreateFontW(-14,0,0,0,FW_BOLD,  FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"바탕");
        g_smallFont = CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"바탕");
        LoadFiles(); RebuildFilter();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_MOUSEWHEEL:
        ScrollTo(h, g_scroll - (GET_WHEEL_DELTA_WPARAM(wp)/120));
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt; RECT rc; pt.x=GET_X_LPARAM(lp); pt.y=GET_Y_LPARAM(lp);
        GetClientRect(h,&rc);
        { RECT cb=CloseRect(rc); if (PtInRect(&cb,pt)) { DestroyWindow(h); return 0; } }
        { RECT r=MaleRect();   if (PtInRect(&r,pt)) { SetGender(h,0); return 0; } }
        { RECT r=FemaleRect(); if (PtInRect(&r,pt)) { SetGender(h,1); return 0; } }
        { int bi; for (bi=0;bi<5;bi++){ RECT r=CatRect(bi); if (PtInRect(&r,pt)) { SetCat(h,bi); return 0; } } }
        { RECT sb=SbTrack(); if (PtInRect(&sb,pt)) {   // 트랙 클릭 = 페이지 이동
            int mid=(sb.top+sb.bottom)/2;
            ScrollTo(h, g_scroll + (pt.y<mid?-ROWS_VIS:ROWS_VIS)); return 0; } }
        if (pt.y < FRAME+TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP:    ScrollTo(h, g_scroll-1); return 0;
        case VK_DOWN:  ScrollTo(h, g_scroll+1); return 0;
        case VK_PRIOR: ScrollTo(h, g_scroll-ROWS_VIS); return 0;
        case VK_NEXT:  ScrollTo(h, g_scroll+ROWS_VIS); return 0;
        case VK_HOME:  ScrollTo(h, 0); return 0;
        case VK_END:   ScrollTo(h, MaxScroll()); return 0;
        case 'M':      SetGender(h,0); return 0;
        case 'F':      SetGender(h,1); return 0;
        case VK_ESCAPE:DestroyWindow(h); return 0;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_font)      { DeleteObject(g_font);      g_font = NULL; }
        if (g_smallFont) { DeleteObject(g_smallFont); g_smallFont = NULL; }
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
    if (g_wnd) { ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd); SetFocus(g_wnd); }
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
                if (!HasOurMenu(bar)) {
                    AppendMenuW(bar, MF_STRING, ID_CHAR, L"인물");
                    DrawMenuBar(g_gameHwnd);
                    OutputDebugStringW(L"[CharacterUtilKR] 인물 menu (re)installed.");
                }
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
