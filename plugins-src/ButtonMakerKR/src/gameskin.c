#include "gameskin.h"
#include "band.h"
#include "miscskin.h"
#include "gamefont.h"

// 띠 한 장을 만들 자리. 창 하나가 한 번에 단추 하나씩만 그리므로 공용으로 둔다.
static unsigned char g_idx[BAND_MAX_PIX];
static unsigned char g_pix[BAND_MAX_PIX * 3];
static int           g_tried = 0;
static int           g_ok = 0;

// 글자색 — 화면에서 되짚은 값이다(21번 노트).
#define COL_ON_BEIGE  17   // 짙은 갈색  — 베이지 단추 위
#define COL_ON_TITLE  26   // 크림       — 진홍 제목 띠 위

int GameSkin_Ready(void)
{
    if (g_tried) return g_ok;
    g_tried = 1;
    g_ok = MiscSkin_Load() ? 1 : 0;
    GameFont_Load();               // 글꼴이 없어도 띠는 그린다(글자만 안 나온다)
    return g_ok;
}

// 띠를 지어 (x,y,w,h) 안에 그린다. 성공 1.
//
// **세로로는 늘리지 않는다.** 띠는 원래 24픽셀인데 단추 칸이 그보다 높다고 늘려 그렸더니
// 글자와 테두리가 위아래로 퍼져 어색했다(매매 창 [결정]). 칸이 더 높으면 제 높이 그대로
// 가운데에 놓고, 남는 위아래는 부르는 쪽이 이미 칠해 둔 바탕이 비치게 둔다.
// 칸이 24보다 낮을 때만 그 높이에 맞춰 줄인다.
static int DrawBand(HDC dc, int x, int y, int w, int h, int style,
                    const wchar_t* text, unsigned char color)
{
    BITMAPINFO bi;
    int cells, bw;

    if (!GameSkin_Ready() || w <= 0 || h <= 0) return 0;

    if (h > SKIN_H) { y += (h - SKIN_H) / 2; h = SKIN_H; }

    // 폭에 맞는 칸 수 — 끝 조각 16+16 을 뺀 나머지를 8 로 나눈다.
    cells = (w - SKIN_CAP_W * 2 + SKIN_MID_W / 2) / SKIN_MID_W;
    if (cells < 1) cells = 1;
    if (cells > BAND_MAX_CELLS) cells = BAND_MAX_CELLS;

    bw = Band_Build(style, text, cells, color, 0, 0, g_idx);
    if (!bw) return 0;
    Band_ToBgr(g_idx, bw, SKIN_H, g_pix);

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -SKIN_H;          // 음수 = 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(dc, COLORONCOLOR);      // 도트를 살린다
    StretchDIBits(dc, x, y, w, h, 0, 0, bw, SKIN_H, g_pix, &bi, DIB_RGB_COLORS, SRCCOPY);
    return 1;
}

void GameSkin_Title(HDC dc, RECT r, const wchar_t* text)
{
    DrawBand(dc, r.left, r.top, r.right - r.left, r.bottom - r.top,
             0 /* 진홍 장식 */, text, COL_ON_TITLE);
}

int GameSkin_Button(HDC dc, RECT r, const wchar_t* t, BOOL active)
{
    return DrawBand(dc, r.left, r.top, r.right - r.left, r.bottom - r.top,
                    active ? 0 : 1,                         // 강조는 진홍 장식 띠
                    t ? t : L"",
                    active ? COL_ON_TITLE : COL_ON_BEIGE);
}

// 밀어넣기 단추만 고른다. 라디오·체크·그룹상자는 그대로 둔다.
static int IsPushButton(HWND c)
{
    wchar_t cls[16];
    LONG st;
    if (!GetClassNameW(c, cls, 16)) return 0;
    if (lstrcmpiW(cls, L"Button") != 0) return 0;
    st = GetWindowLongW(c, GWL_STYLE) & 0x0F;      // BS_TYPEMASK 아래 네 비트
    return st == BS_PUSHBUTTON || st == BS_DEFPUSHBUTTON;
}

void GameSkin_Apply(HWND win)
{
    HWND c;
    if (!GameSkin_Ready() || !win) return;
    for (c = GetWindow(win, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        LONG st;
        if (!IsPushButton(c)) continue;
        st = GetWindowLongW(c, GWL_STYLE);
        SetWindowLongW(c, GWL_STYLE, st | BS_OWNERDRAW);
        InvalidateRect(c, NULL, TRUE);
    }
}

int GameSkin_DrawItem(const DRAWITEMSTRUCT* di)
{
    wchar_t text[64];
    RECT r;
    int pressed;

    if (!di || di->CtlType != ODT_BUTTON || !GameSkin_Ready()) return 0;

    text[0] = 0;
    GetWindowTextW(di->hwndItem, text, 64);
    r = di->rcItem;
    pressed = (di->itemState & ODS_SELECTED) != 0;

    // 눌린 동안은 회녹색 벌로 바꿔 눌린 티를 낸다(게임도 상태마다 벌이 다르다).
    if (!DrawBand(di->hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                  pressed ? 2 : 1, text, COL_ON_BEIGE))
        return 0;

    if (di->itemState & ODS_FOCUS) {
        RECT f = r;
        InflateRect(&f, -3, -3);
        DrawFocusRect(di->hDC, &f);
    }
    if (di->itemState & ODS_DISABLED) {
        // 흐리게 — 반투명 대신 촘촘한 회색 무늬를 덮는다(8bpp 화면에서도 안전하다).
        HBRUSH br = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
        int old = SetROP2(di->hDC, R2_MASKPEN);
        FillRect(di->hDC, &r, br);
        SetROP2(di->hDC, old);
    }
    return 1;
}
