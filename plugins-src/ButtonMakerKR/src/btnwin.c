#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdlib.h>
#include <wchar.h>
#include "btnwin.h"
#include "band.h"
#include "miscskin.h"
#include "gamefont.h"
#include "imgio.h"      // CharacterUtilKR/src — PNG 쓰기(GDI+ 를 실행 중에 부른다)

// ButtonMakerKR — 게임과 똑같은 메뉴 띠(타이틀·버튼)를 글자만 바꿔 만들어 준다.
//
//   껍데기  MISC.CDS 파트 4 (miscskin.c 머리말에 형식이 적혀 있다)
//   글자    ALL_FONT.16P / ANKFONT.DAT (gamefont.c)
//   색      게임 공용 색표
//
// 게임 화면은 건드리지 않는다. 만든 그림은 PNG 로 저장하거나 클립보드로 복사한다.

#define ID_BTN_OPEN 0xC500u   // "파일>모드>버튼 만들기"
                              // (… Skill=0xC200, Book=0xC300, ShipInfo=0xC400 과 안 겹치게)

#define ID_STYLE   1001
#define ID_TEXT    1002
#define ID_AUTO    1003
#define ID_CELLS   1004
#define ID_COLOR   1005
#define ID_SHADOW  1006
#define ID_ZOOM    1007
#define ID_SAVE    1010
#define ID_COPY    1011
#define ID_STATUS  1012

#define PREVIEW_X   16
#define PREVIEW_Y   118
#define PREVIEW_W   700
#define PREVIEW_H   130
#define TEXT_MARGIN 24        // 자동 폭일 때 글자 양옆으로 띄우는 픽셀.
                              // 끝 조각이 16px 이므로 24 면 장식과 글자 사이가 8px 뜬다.

static HINSTANCE g_hinst = NULL;
static HWND g_win = NULL, g_style = NULL, g_text = NULL, g_auto = NULL, g_cells = NULL;
static HWND g_color = NULL, g_shadow = NULL, g_zoom = NULL, g_status = NULL;

static unsigned char g_idx[BAND_MAX_PIX];
static unsigned char g_pix[BAND_MAX_PIX * 3];
static int           g_w = 0;                  // 마지막으로 지은 띠의 폭
static int           g_busy = 0;               // Rebuild 안에서 칸 수를 고쳐 쓸 때 EN_CHANGE 되돌이 막기

static const wchar_t* kStyleName[SKIN_STYLES] = {
    L"진홍 장식 — 타이틀", L"베이지 — 버튼", L"회녹색 — 다른 상태"
};

// 고를 수 있는 글자색(게임 공용 색표의 색인). 화면에서 되짚은 값이 첫 둘이다 —
// 타이틀 글자는 26, 베이지 버튼 글자는 17 이었다.
typedef struct { const wchar_t* name; unsigned char idx; } ColorRow;
static const ColorRow kColors[] = {
    { L"크림 (타이틀 기본)", 26 },
    { L"짙은 갈색 (버튼 기본)", 17 },
    { L"흰빛",  10 },
    { L"검정",  74 },
    { L"밝은 살구", 41 },
    { L"회색",  21 },
};
#define COLOR_N ((int)(sizeof(kColors)/sizeof(kColors[0])))

static const wchar_t* kShadowName[3] = { L"없음", L"어둡게", L"밝게" };
static const unsigned char kShadowIdx[3] = { 0, 74, 42 };

static int Sel(HWND cb) { int i = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0); return i < 0 ? 0 : i; }

static void SetStatus(const wchar_t* s) { if (g_status) SetWindowTextW(g_status, s); }

// 창의 값으로 띠를 다시 짓는다. 성공하면 g_w 가 0 보다 크다.
static void Rebuild(void)
{
    wchar_t text[256], num[32], msg[256];
    int style, cells, zoom, sh;

    if (g_busy) return;                 // 아래에서 칸 수를 고쳐 쓰면 EN_CHANGE 가 또 들어온다
    g_busy = 1;

    g_w = 0;
    if (!MiscSkin_Ready()) { SetStatus(L"MISC.CDS 파트 4 를 못 읽었습니다."); g_busy = 0; return; }

    GetWindowTextW(g_text, text, 256);
    style = Sel(g_style);
    sh = Sel(g_shadow);

    if (SendMessageW(g_auto, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        cells = Band_AutoCells(text, TEXT_MARGIN);
        wsprintfW(num, L"%d", cells);
        SetWindowTextW(g_cells, num);
        EnableWindow(g_cells, FALSE);
    } else {
        GetWindowTextW(g_cells, num, 32);
        cells = _wtoi(num);
        EnableWindow(g_cells, TRUE);
    }
    if (cells < 1) cells = 1;
    if (cells > BAND_MAX_CELLS) cells = BAND_MAX_CELLS;

    g_w = Band_Build(style, text, cells,
                     kColors[Sel(g_color)].idx,
                     sh != 0, kShadowIdx[sh], g_idx);
    if (!g_w) { SetStatus(L"띠를 못 지었습니다."); g_busy = 0; return; }

    Band_ToBgr(g_idx, g_w, SKIN_H, g_pix);
    zoom = Sel(g_zoom) + 1;
    wsprintfW(msg, L"%d x %d 픽셀 (가운데 조각 %d칸)   미리보기 %d배%s",
              g_w, SKIN_H, cells, zoom,
              GameFont_Ready() ? L"" : L"   ※ 글꼴 파일을 못 읽어 글자가 안 나옵니다");
    SetStatus(msg);
    g_busy = 0;
}

static void PaintPreview(HDC dc)
{
    BITMAPINFO bi;
    RECT r;
    HBRUSH bg;
    int zoom, dw, dh, x, y;

    r.left = PREVIEW_X; r.top = PREVIEW_Y;
    r.right = PREVIEW_X + PREVIEW_W; r.bottom = PREVIEW_Y + PREVIEW_H;
    bg = CreateSolidBrush(RGB(48, 44, 40));      // 게임 창 바탕과 비슷한 어두운 색
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    FrameRect(dc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));

    if (g_w <= 0) return;

    zoom = Sel(g_zoom) + 1;
    dw = g_w * zoom;
    dh = SKIN_H * zoom;
    x = PREVIEW_X + (PREVIEW_W - dw) / 2;
    y = PREVIEW_Y + (PREVIEW_H - dh) / 2;
    if (x < PREVIEW_X) x = PREVIEW_X;
    if (y < PREVIEW_Y) y = PREVIEW_Y;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_w;
    bi.bmiHeader.biHeight = -SKIN_H;             // 음수 = 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(dc, COLORONCOLOR);         // 도트를 그대로 살린다
    IntersectClipRect(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    StretchDIBits(dc, x, y, dw, dh, 0, 0, g_w, SKIN_H, g_pix, &bi, DIB_RGB_COLORS, SRCCOPY);
    SelectClipRgn(dc, NULL);
}

// ---------------------------------------------------------------- 내보내기

static void SavePng(HWND owner)
{
    OPENFILENAMEW ofn;
    wchar_t path[MAX_PATH];
    wchar_t text[64];

    if (g_w <= 0) return;
    if (!Img_Available()) {
        MessageBoxW(owner, L"gdiplus.dll 을 못 써서 PNG 로 저장할 수 없습니다.\n"
                           L"클립보드 복사는 됩니다.", L"버튼 만들기", MB_ICONWARNING);
        return;
    }

    GetWindowTextW(g_text, text, 40);
    if (!text[0]) lstrcpyW(text, L"button");
    {   // 파일 이름에 못 쓰는 글자를 걷어낸다
        int i;
        for (i = 0; text[i]; i++)
            if (wcschr(L"\\/:*?\"<>|", text[i])) text[i] = L'_';
    }
    wsprintfW(path, L"%s.png", text);

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PNG 그림\0*.png\0모든 파일\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    Band_ToRgb(g_idx, g_w, SKIN_H, g_pix);       // PNG 는 R,G,B 차례
    if (Img_SavePng(path, g_w, SKIN_H, g_pix)) SetStatus(L"PNG 로 저장했습니다.");
    else SetStatus(L"PNG 저장에 실패했습니다.");
    Band_ToBgr(g_idx, g_w, SKIN_H, g_pix);       // 화면용으로 되돌린다
}

static void CopyToClipboard(HWND owner)
{
    int stride, r, c;
    HGLOBAL h;
    unsigned char* p;
    BITMAPINFOHEADER* bh;

    if (g_w <= 0) return;

    stride = ((g_w * 3) + 3) & ~3;               // DIB 한 줄은 4바이트에 맞춘다
    h = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + (SIZE_T)stride * SKIN_H);
    if (!h) return;
    p = (unsigned char*)GlobalLock(h);
    if (!p) { GlobalFree(h); return; }

    bh = (BITMAPINFOHEADER*)p;
    ZeroMemory(bh, sizeof(*bh));
    bh->biSize = sizeof(BITMAPINFOHEADER);
    bh->biWidth = g_w;
    bh->biHeight = SKIN_H;                       // 양수 = 아래에서 위로(클립보드 관례)
    bh->biPlanes = 1;
    bh->biBitCount = 24;
    bh->biCompression = BI_RGB;
    bh->biSizeImage = (DWORD)stride * SKIN_H;

    ZeroMemory(p + sizeof(*bh), (SIZE_T)stride * SKIN_H);
    for (r = 0; r < SKIN_H; r++) {
        unsigned char* dst = p + sizeof(*bh) + (SIZE_T)(SKIN_H - 1 - r) * stride;
        const unsigned char* src = g_pix + (SIZE_T)r * g_w * 3;
        for (c = 0; c < g_w * 3; c++) dst[c] = src[c];
    }
    GlobalUnlock(h);

    if (OpenClipboard(owner)) {
        EmptyClipboard();
        if (SetClipboardData(CF_DIB, h)) { CloseClipboard(); SetStatus(L"클립보드에 복사했습니다."); return; }
        CloseClipboard();
    }
    GlobalFree(h);
    SetStatus(L"클립보드 복사에 실패했습니다.");
}

// ---------------------------------------------------------------- 창

static HWND Mk(const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id, HWND par)
{
    return CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st,
                           x, y, w, h, par, (HMENU)(INT_PTR)id, g_hinst, NULL);
}

static void MakeControls(HWND h)
{
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int i;
    HWND all[16];
    int n = 0;

    all[n++] = Mk(L"STATIC", L"종류", 0, 16, 16, 44, 20, 0, h);
    g_style = Mk(L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 64, 12, 200, 200, ID_STYLE, h);
    all[n++] = g_style;

    all[n++] = Mk(L"STATIC", L"글자", 0, 16, 48, 44, 20, 0, h);
    g_text = Mk(L"EDIT", L"귀족 저택", WS_BORDER | ES_AUTOHSCROLL, 64, 44, 200, 22, ID_TEXT, h);
    all[n++] = g_text;

    g_auto = Mk(L"BUTTON", L"폭 자동", BS_AUTOCHECKBOX, 16, 80, 80, 20, ID_AUTO, h);
    all[n++] = g_auto;
    g_cells = Mk(L"EDIT", L"15", WS_BORDER | ES_NUMBER, 104, 78, 48, 22, ID_CELLS, h);
    all[n++] = g_cells;
    all[n++] = Mk(L"STATIC", L"칸 (가운데 8픽셀 조각 수)", 0, 158, 82, 180, 20, 0, h);

    all[n++] = Mk(L"STATIC", L"글자색", 0, 300, 16, 52, 20, 0, h);
    g_color = Mk(L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 356, 12, 170, 220, ID_COLOR, h);
    all[n++] = g_color;

    all[n++] = Mk(L"STATIC", L"그림자", 0, 300, 48, 52, 20, 0, h);
    g_shadow = Mk(L"COMBOBOX", NULL, CBS_DROPDOWNLIST, 356, 44, 170, 160, ID_SHADOW, h);
    all[n++] = g_shadow;

    all[n++] = Mk(L"STATIC", L"미리보기", 0, 552, 16, 60, 20, 0, h);
    g_zoom = Mk(L"COMBOBOX", NULL, CBS_DROPDOWNLIST, 616, 12, 100, 160, ID_ZOOM, h);
    all[n++] = g_zoom;

    Mk(L"BUTTON", L"PNG 저장", 0, 16, 262, 110, 26, ID_SAVE, h);
    Mk(L"BUTTON", L"클립보드 복사", 0, 136, 262, 130, 26, ID_COPY, h);
    g_status = Mk(L"STATIC", L"", 0, 280, 268, 436, 20, ID_STATUS, h);

    for (i = 0; i < SKIN_STYLES; i++) SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)kStyleName[i]);
    for (i = 0; i < COLOR_N; i++)     SendMessageW(g_color, CB_ADDSTRING, 0, (LPARAM)kColors[i].name);
    for (i = 0; i < 3; i++)           SendMessageW(g_shadow, CB_ADDSTRING, 0, (LPARAM)kShadowName[i]);
    SendMessageW(g_zoom, CB_ADDSTRING, 0, (LPARAM)L"1배");
    SendMessageW(g_zoom, CB_ADDSTRING, 0, (LPARAM)L"2배");
    SendMessageW(g_zoom, CB_ADDSTRING, 0, (LPARAM)L"3배");
    SendMessageW(g_zoom, CB_ADDSTRING, 0, (LPARAM)L"4배");
    SendMessageW(g_style, CB_SETCURSEL, 0, 0);
    SendMessageW(g_color, CB_SETCURSEL, 0, 0);
    SendMessageW(g_shadow, CB_SETCURSEL, 0, 0);
    SendMessageW(g_zoom, CB_SETCURSEL, 2, 0);
    SendMessageW(g_auto, BM_SETCHECK, BST_CHECKED, 0);

    // 라벨과 입력칸 글꼴을 창 기본으로 맞춘다
    for (i = 0; i < n; i++) SendMessageW(all[i], WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageW(GetDlgItem(h, ID_SAVE),   WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageW(GetDlgItem(h, ID_COPY),   WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageW(g_status, WM_SETFONT, (WPARAM)f, TRUE);
}

static LRESULT CALLBACK WinProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        MakeControls(h);
        Rebuild();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == ID_SAVE && code == BN_CLICKED) { SavePng(h); return 0; }
        if (id == ID_COPY && code == BN_CLICKED) { CopyToClipboard(h); return 0; }
        if ((code == CBN_SELCHANGE) || (code == EN_CHANGE) || (id == ID_AUTO && code == BN_CLICKED)) {
            Rebuild();
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintPreview(dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_DESTROY:
        g_win = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void BtnWin_Show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_win) { SetForegroundWindow(g_win); return; }

    if (!MiscSkin_Load()) {
        MessageBoxW(owner, L"MISC.CDS 의 메뉴 띠(파트 4)를 못 읽었습니다.\n"
                           L"게임 폴더에 MISC.CDS 가 있는지 보세요.",
                    L"버튼 만들기", MB_ICONWARNING);
        return;
    }
    GameFont_Load();     // 글꼴이 없어도 띠는 보여 준다(글자만 안 나온다)

    if (!registered) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = WinProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ButtonMakerKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"ButtonMakerKRWin", L"버튼 만들기 — ButtonMakerKR",
                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT, 748, 350, owner, NULL, g_hinst, NULL);
    if (g_win) { ShowWindow(g_win, SW_SHOW); UpdateWindow(g_win); }
}

// ================================================================== 메뉴 설치 + 서브클래싱
// (BookUtilKR / SkillUtilKR 과 같은 관용구다 — 게임 창을 찾아 "파일>모드" 에 항목을 붙인다)

static HWND    g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static int     g_pass = 0;

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_BTN_OPEN) { BtnWin_Show(h); return 0; }
    if (m == WM_NCDESTROY) {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_gameHwnd = NULL;
        return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0; (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_gameHwnd = h; return FALSE; }
    return TRUE;
}

static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
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
static HMENU FindOrCreateModMenu(HMENU fileMenu, BOOL mayCreate)
{
    int i; WCHAR s[64]; HMENU first = NULL, sub;
    if (!fileMenu) return NULL;
    for (i = GetMenuItemCount(fileMenu) - 1; i >= 0; i--) {
        if (GetMenuStringW(fileMenu, (UINT)i, s, 64, MF_BYPOSITION) <= 0) continue;
        if (lstrcmpW(s, L"모드") != 0) continue;
        sub = GetSubMenu(fileMenu, (UINT)i);
        if (first && sub && GetMenuItemCount(sub) == 0) { RemoveMenu(fileMenu, (UINT)i, MF_BYPOSITION); continue; }
        first = sub;
    }
    if (first || !mayCreate) return first;
    sub = CreatePopupMenu();
    if (!sub) return NULL;
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)sub, L"모드");
    return sub;
}

static DWORD WINAPI MenuThread(LPVOID pv)
{
    (void)pv;
    OutputDebugStringW(L"[ButtonMakerKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_BTN_OPEN)) {
                HMENU modMenu;
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                if (!modMenu) { Sleep(1000); continue; }
                AppendMenuW(modMenu, MF_STRING, ID_BTN_OPEN, L"버튼 만들기");
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[ButtonMakerKR] menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[ButtonMakerKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void BtnKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
