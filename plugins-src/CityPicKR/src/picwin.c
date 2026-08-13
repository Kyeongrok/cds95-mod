#include "picwin.h"
#include "citycg.h"
#include "uikit.h"
#include "cities_data.h"   // TradeUtilKR 의 kCities[226] — 도시 ID 순서. 그림 번호와 그대로 맞는다
#include <windowsx.h>

// CityPicKR — CITYCG.CDS 의 도시 그림 226장을 왼쪽 목록에서 골라 보는 창.
// 읽기만 한다(게임 메모리에 쓰지 않는다). 세이브를 안 불러온 상태에서도 열린다 —
// 그림은 게임 파일에 들어 있는 것이라 진행 상황과 무관하다.

#define WC_PIC   L"CityPicKR_Window"

// [넣기] 를 감춰 둔다 — 넣은 CITYCG.CDS 로 게임이 죽었다(2026-08-13, 리스본).
// 파일 형식은 멀쩡하고 우리 디코더로도 그대로 풀리지만, 되쓰기에 쓰는 LS12 인코더가
// 실제로는 압축을 하지 않아 파트가 원본 관례를 벗어난다.
//   팔레트 파트  원본은 226장 전부 258바이트(= 압축크기 == 원본크기, 무압축 저장)인데
//                우리가 쓴 것은 371바이트짜리 압축 형식이었다. 게임이 258 버퍼에 읽으면 넘친다.
//   그림 파트    원본 최대가 113,474 인데 122,790 이 되었다.
// 인코더에 LZ 매치를 넣어 원본 수준으로 줄이고(그리고 팔레트는 무압축 그대로 두고)
// 게임에서 확인한 뒤에 다시 연다. 내보내기는 파일을 건드리지 않으므로 그대로 둔다.
#define IMPORT_ENABLED 0

#define PAD      8
// 목록 칸 — 번호(26) + 이름 + 문화권. 문화권은 "중앙아시아" 다섯 글자가 제일 길고
// 이름은 "로우렌스마르케스" 여덟 글자가 제일 길다. 둘 다 안 잘리게 잡는다.
#define NUM_W    26
#define SPHERE_W 64
#define LIST_W   (NUM_W + 8 + 108 + SPHERE_W + 4)
#define SB_W     12
#define ROW_H    18
#define INFO_H   26
#define BTN_W    78
#define BTN_H    20

#define LIST_X   (FRAME + PAD)
#define LIST_Y   (FRAME + TITLE_H + 6)
#define SB_X     (LIST_X + LIST_W)
#define PIC_X    (SB_X + SB_W + PAD)

#define CITY_N   ((int)(sizeof(kCities) / sizeof(kCities[0])))

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL;
static RECT      g_closeRect;

static int g_scale  = 1;    // 1 = 원본 도트(400x320), 2 = 두 배
static int g_sel    = 0;    // 보고 있는 도시
static int g_scroll = 0;    // 목록 맨 위에 보이는 행
static wchar_t g_msg[120];  // 내보내기·넣기 결과. 비어 있으면 조작 안내를 대신 띄운다

static int PicW(void)  { return CITYPIC_W * g_scale; }
static int PicH(void)  { return CITYPIC_H * g_scale; }
static int VisRows(void) { return PicH() / ROW_H; }
static int MaxScroll(void)
{
    int m = CITY_N - VisRows();
    return m < 0 ? 0 : m;
}
static int WinW(void) { return PIC_X + PicW() + PAD + FRAME; }
static int WinH(void) { return LIST_Y + PicH() + INFO_H + FRAME; }

// 아래 정보줄 오른쪽 끝의 단추. 0 = 내보내기, 1 = 넣기(왼쪽부터 이 순서로 놓는다).
// 넣기를 감춰 둔 동안에는 내보내기 하나만 오른쪽 끝에 놓는다.
#define BTN_N (IMPORT_ENABLED ? 2 : 1)
static RECT RcBtn(int i)
{
    RECT r;
    r.left  = PIC_X + PicW() - (BTN_W * BTN_N + (BTN_N - 1) * 4) + i * (BTN_W + 4);
    r.right = r.left + BTN_W;
    r.top   = LIST_Y + PicH() + 3;
    r.bottom = r.top + BTN_H;
    return r;
}

static void ClampScroll(void)
{
    if (g_scroll > MaxScroll()) g_scroll = MaxScroll();
    if (g_scroll < 0) g_scroll = 0;
}

// 고른 도시가 목록에 보이도록 스크롤을 맞춘다.
static void EnsureVisible(void)
{
    int vis = VisRows();
    if (g_sel < g_scroll) g_scroll = g_sel;
    if (g_sel >= g_scroll + vis) g_scroll = g_sel - vis + 1;
    ClampScroll();
}

static void Select(HWND h, int i)
{
    if (i < 0) i = 0;
    if (i >= CITY_N) i = CITY_N - 1;
    if (i == g_sel) return;
    g_sel = i;
    g_msg[0] = 0;          // 다른 도시로 옮기면 앞선 내보내기·넣기 알림은 치운다
    EnsureVisible();
    InvalidateRect(h, NULL, FALSE);
}

// 배율을 바꾸면 창 크기도 같이 바뀐다. 창 가운데를 그대로 두고 늘렸다 줄인다.
static void SetScale(HWND h, int s)
{
    RECT rc;
    int cx, cy;
    if (s < 1) s = 1;
    if (s > 2) s = 2;
    if (s == g_scale) return;
    if (GetWindowRect(h, &rc)) {
        cx = (rc.left + rc.right) / 2;
        cy = (rc.top + rc.bottom) / 2;
    } else {
        cx = cy = 0;
    }
    g_scale = s;
    EnsureVisible();
    { int w = WinW(), hh = WinH();
      int x = cx - w / 2, y = cy - hh / 2;
      if (x < 0) x = 0;
      if (y < 0) y = 0;
      SetWindowPos(h, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE); }
    InvalidateRect(h, NULL, TRUE);
}

// 도시 한 줄 — 왼쪽에 이름, 오른쪽에 문화권. 고른 줄은 바탕을 어둡게 깔고 밝은 글씨로 쓴다.
static void DrawRow(HDC dc, int i, int row)
{
    RECT r, t;
    const CityInfo* c = &kCities[i];
    COLORREF tx = COL_TEXT;
    wchar_t num[16];

    r.left = LIST_X; r.right = LIST_X + LIST_W;
    r.top = LIST_Y + row * ROW_H; r.bottom = r.top + ROW_H;

    if (i == g_sel) {
        HBRUSH b = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &r, b); DeleteObject(b);
        tx = RGB(245, 238, 220);
    } else if (i & 1) {
        HBRUSH b = CreateSolidBrush(COL_ROW_ALT); FillRect(dc, &r, b); DeleteObject(b);
    }

    wsprintfW(num, L"%d", i);
    t = r; t.left += 4; t.right = t.left + NUM_W;
    UI_Text(dc, t, num, g_smallFont, tx, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    t = r; t.left += 4 + NUM_W + 8; t.right -= SPHERE_W + 4;
    UI_Text(dc, t, c->name, g_font, tx, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    t = r; t.right -= 4; t.left = t.right - SPHERE_W;
    UI_Text(dc, t, c->sphere, g_smallFont, tx, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

static void OnPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc, lr, pr, ir, sb;
    UiBuf ub; HDC dc;
    int i, vis = VisRows();
    wchar_t buf[160];

    GetClientRect(h, &rc);
    dc = UI_BufBegin(&ub, hdc, rc.right, rc.bottom);
    UI_WindowFrame(dc, rc, L"도시 그림", &g_closeRect);

    // 목록
    lr.left = LIST_X; lr.right = LIST_X + LIST_W;
    lr.top = LIST_Y;  lr.bottom = LIST_Y + PicH();
    { HBRUSH b = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &lr, b); DeleteObject(b); }
    for (i = 0; i < vis && g_scroll + i < CITY_N; i++) DrawRow(dc, g_scroll + i, i);
    { HBRUSH b = CreateSolidBrush(COL_DARK); FrameRect(dc, &lr, b); DeleteObject(b); }

    sb.left = SB_X; sb.right = SB_X + SB_W;
    sb.top = LIST_Y; sb.bottom = LIST_Y + PicH();
    UI_Scrollbar(dc, sb, g_scroll, MaxScroll(), vis, CITY_N);

    // 그림
    pr.left = PIC_X; pr.right = PIC_X + PicW();
    pr.top = LIST_Y; pr.bottom = LIST_Y + PicH();
    if (!CityCg_Draw(dc, pr.left, pr.top, PicW(), PicH(), g_sel)) {
        HBRUSH b = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &pr, b); DeleteObject(b);
        UI_Text(dc, pr,
                CityCg_Count() ? L"이 도시의 그림을 풀지 못했습니다."
                               : L"게임 폴더에서 CITYCG.CDS 를 읽지 못했습니다.",
                g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    { HBRUSH b = CreateSolidBrush(COL_DARK); FrameRect(dc, &pr, b); DeleteObject(b); }

    // 아래 정보줄 — 왼쪽은 고른 도시, 가운데는 결과 알림(없으면 조작법), 오른쪽은 단추 둘
    ir.left = LIST_X; ir.right = PIC_X + PicW() - (BTN_W * BTN_N + (BTN_N - 1) * 4 + 8);
    ir.top = LIST_Y + PicH() + 2; ir.bottom = ir.top + INFO_H - 4;
    wsprintfW(buf, L"#%d  %s · %s%s%s%s",
              g_sel, kCities[g_sel].name, kCities[g_sel].sphere,
              kCities[g_sel].lib   ? L" · 도서관" : L"",
              kCities[g_sel].ship  ? L" · 조선소" : L"",
              kCities[g_sel].guild ? L" · 길드"   : L"");
    UI_Text(dc, ir, buf, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    if (g_msg[0]) {
        UI_Text(dc, ir, g_msg, g_smallFont, COL_WARN_TX, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    } else {
        wsprintfW(buf, L"%d장 · ↑↓ 고르기 / Z 배율 x%d / E 내보내기%s / ESC 닫기",
                  CityCg_Count(), g_scale, IMPORT_ENABLED ? L" / I 넣기" : L"");
        UI_Text(dc, ir, buf, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    UI_Button(dc, RcBtn(0), L"내보내기", FALSE);
#if IMPORT_ENABLED
    UI_Button(dc, RcBtn(1), L"넣기", FALSE);
#endif

    UI_BufEnd(&ub);
    EndPaint(h, &ps);
}

// ---- 내보내기 / 넣기 ----
static const wchar_t* ErrText(int rc)
{
    switch (rc) {
    case CITYPIC_ERR_GDIP:    return L"gdiplus.dll 을 못 써서 PNG 를 다룰 수 없습니다";
    case CITYPIC_ERR_IMAGE:   return L"그림 파일을 읽지 못했습니다";
    case CITYPIC_ERR_ARCHIVE: return L"CITYCG.CDS 가 안 열려 있습니다";
    case CITYPIC_ERR_ENCODE:  return L"다시 묶는 데 실패했습니다";
    case CITYPIC_ERR_VERIFY:  return L"검사에 걸려 파일을 건드리지 않았습니다";
    case CITYPIC_ERR_WRITE:   return L"파일을 쓰지 못했습니다(게임이 쥐고 있을 수 있습니다)";
    case CITYPIC_ERR_RANGE:   return L"그림 번호가 표 밖입니다";
    default:                  return L"실패했습니다";
    }
}

static int PickFile(HWND h, wchar_t* path, int save)
{
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = h;
    ofn.lpstrFilter  = save ? L"PNG 그림\0*.png\0"
                            : L"그림 파일\0*.png;*.bmp;*.jpg;*.jpeg;*.gif\0모든 파일\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrDefExt  = L"png";
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER)
                     : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

static void DoExport(HWND h)
{
    wchar_t path[MAX_PATH];
    int rc;

    wsprintfW(path, L"CITYCG_%03d_%s.png", g_sel, kCities[g_sel].name);
    if (!PickFile(h, path, 1)) return;
    rc = CityCg_ExportPng(g_sel, path);
    if (rc == CITYPIC_ERR_OK) wsprintfW(g_msg, L"#%d %s 를 PNG 로 내보냈습니다", g_sel, kCities[g_sel].name);
    else                      lstrcpynW(g_msg, ErrText(rc), 100);
}

#if IMPORT_ENABLED
static void DoImport(HWND h)
{
    wchar_t path[MAX_PATH], msg[512];
    int rc, exact = 0;

    path[0] = 0;
    if (!PickFile(h, path, 0)) return;

    // 게임 파일을 다시 쓰는 일이라 한 번 묻는다. 되돌릴 길(.orig)도 같이 알려 준다.
    wsprintfW(msg,
        L"CITYCG.CDS 의 #%d %s 그림을 이 파일로 바꿉니다.\n\n"
        L"그림은 400x320 으로 맞춰 들어가고, 색은 그 그림 전용 86색 팔레트를 새로 짜서 넣습니다.\n"
        L"다른 도시 225장은 손대지 않습니다.\n"
        L"원본은 CITYCG.CDS.orig 로 남깁니다(처음 한 번만).\n\n계속할까요?",
        g_sel, kCities[g_sel].name);
    if (MessageBoxW(h, msg, L"도시 그림 바꾸기", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;

    rc = CityCg_ImportPng(g_sel, path, &exact);
    if (rc != CITYPIC_ERR_OK) { lstrcpynW(g_msg, ErrText(rc), 100); return; }

    wsprintfW(g_msg, L"#%d 를 바꿨습니다%s", g_sel, exact ? L"(색 그대로)" : L"(색은 가까운 값으로)");
    // 게임 화면에 이미 떠 있는 그림은 그대로다 — 그 도시에 다시 들어가야 새 그림을 읽는다.
    MessageBoxW(h, L"바꿨습니다.\n\n게임 쪽 그림은 그 도시에 다시 들어갈 때 새로 읽습니다.\n"
                   L"그래도 옛 그림이 나오면 게임을 껐다 켜 보세요.", L"도시 그림 바꾸기", MB_OK | MB_ICONINFORMATION);
}
#endif  // IMPORT_ENABLED

static LRESULT CALLBACK PicProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        CityCg_Load();          // 20MB 짜리 파일이라 창을 열 때 읽고 닫을 때 놓는다
        EnsureVisible();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -3 : 3;
        g_scroll += d;
        ClampScroll();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt; RECT lr, sb, br;
        pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        SetFocus(h);
        if (PtInRect(&g_closeRect, pt)) { DestroyWindow(h); return 0; }

        br = RcBtn(0);
        if (PtInRect(&br, pt)) { g_msg[0] = 0; DoExport(h); InvalidateRect(h, NULL, FALSE); return 0; }
#if IMPORT_ENABLED
        br = RcBtn(1);
        if (PtInRect(&br, pt)) { g_msg[0] = 0; DoImport(h); InvalidateRect(h, NULL, FALSE); return 0; }
#endif

        lr.left = LIST_X; lr.right = LIST_X + LIST_W;
        lr.top = LIST_Y;  lr.bottom = LIST_Y + PicH();
        if (PtInRect(&lr, pt)) {
            int row = (pt.y - LIST_Y) / ROW_H;
            if (g_scroll + row < CITY_N) Select(h, g_scroll + row);
            return 0;
        }
        // 스크롤바는 누른 자리로 바로 옮긴다(썸을 끄는 것까지는 하지 않는다).
        sb.left = SB_X; sb.right = SB_X + SB_W;
        sb.top = LIST_Y; sb.bottom = LIST_Y + PicH();
        if (PtInRect(&sb, pt)) {
            int hgt = sb.bottom - sb.top;
            g_scroll = hgt > 0 ? (pt.y - sb.top) * MaxScroll() / hgt : 0;
            ClampScroll();
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE: DestroyWindow(h); return 0;
        case VK_UP:   case VK_LEFT:  Select(h, g_sel - 1); return 0;
        case VK_DOWN: case VK_RIGHT: Select(h, g_sel + 1); return 0;
        case VK_PRIOR: Select(h, g_sel - VisRows()); return 0;
        case VK_NEXT:  Select(h, g_sel + VisRows()); return 0;
        case VK_HOME:  Select(h, 0); return 0;
        case VK_END:   Select(h, CITY_N - 1); return 0;
        case 'Z':      SetScale(h, g_scale == 1 ? 2 : 1); return 0;
        case 'E':      g_msg[0] = 0; DoExport(h); InvalidateRect(h, NULL, FALSE); return 0;
#if IMPORT_ENABLED
        case 'I':      g_msg[0] = 0; DoImport(h); InvalidateRect(h, NULL, FALSE); return 0;
#endif
        }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        CityCg_Free();
        UI_DestroyFonts();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void PicWin_Show(HWND owner, HINSTANCE hinst)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT orc;

    g_hinst = hinst;
    if (g_wnd) { SetForegroundWindow(g_wnd); return; }
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = PicProc; wc.hInstance = hinst; wc.lpszClassName = WC_PIC;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc); reg = TRUE;
    }
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - WinW()) / 2;
        y = orc.top  + ((orc.bottom - orc.top) - WinH()) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
    g_wnd = CreateWindowExW(0, WC_PIC, L"도시 그림", WS_POPUP, x, y, WinW(), WinH(), owner, NULL, hinst, NULL);
    if (g_wnd) { ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd); SetFocus(g_wnd); }
}
