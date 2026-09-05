#include <windows.h>
#include "discovl.h"

// 규율 숫자를 8bpp 색인 그림으로 굽는다. (discovl.h 의 설명 참고)
//
// 색인은 arrow.c 와 같은 것을 쓴다 — 해상 팔레트의 10 = 흰색, 50 = 짙은 남색.
// 색인만 넘기므로 게임이 그때그때 올린 팔레트로 나온다.

#define IDX_TEXT  10
#define IDX_EDGE  50

#define DISC_RVA  0x001B3954u    // 함대 정보 +0x2C — 규율(FatigueUtilKR/src/fleetmem.h 와 같은 자리)

static unsigned char g_pix[DISCOVL_W * DISCOVL_H];
static int  g_ready = 0;
static int  g_shownVal = -12345;
static int  g_serial = 0;

// 규율. 못 읽거나 말이 안 되면 -1(세이브를 아직 안 불러온 자리다).
static int DisciplineNow(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* base = (unsigned char*)GetModuleHandleW(NULL);
    const int* p;
    int v;
    if (!base) return -1;
    p = (const int*)(base + DISC_RVA);
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return -1;
    if (mbi.State != MEM_COMMIT) return -1;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return -1;
    // 검사와 읽기 사이에 자리가 사라져도 게임이 꺼지지는 않게 한다.
    __try { v = *p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (v < 0 || v > 1000) return -1;
    return v;
}

// GDI 로 글자를 찍어 색인 그림으로 옮긴다. 글자 둘레 한 점은 테두리 색인으로 채워
// 바다 위에서도 읽히게 한다(화살표와 같은 수법).
static int BuildBitmap(const wchar_t* text)
{
    static unsigned char body[DISCOVL_W * DISCOVL_H];
    BITMAPINFO bi;
    HDC dc;
    HBITMAP bmp, oldBmp;
    HFONT font, oldFont;
    DWORD* bits = NULL;
    RECT r;
    int x, y;

    dc = CreateCompatibleDC(NULL);
    if (!dc) return 0;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = DISCOVL_W;
    bi.bmiHeader.biHeight = -DISCOVL_H;          // 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    if (!bmp || !bits) { DeleteDC(dc); return 0; }
    oldBmp = (HBITMAP)SelectObject(dc, bmp);

    r.left = 0; r.top = 0; r.right = DISCOVL_W; r.bottom = DISCOVL_H;
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));

    font = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"맑은 고딕");
    oldFont = font ? (HFONT)SelectObject(dc, font) : NULL;
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, 1, 2, text, lstrlenW(text));
    GdiFlush();

    // 밝으면 글자, 아니면 빈 칸. 안티에일리어싱을 껐으니 문턱 하나로 갈린다.
    for (y = 0; y < DISCOVL_H; y++)
        for (x = 0; x < DISCOVL_W; x++) {
            DWORD px = bits[y * DISCOVL_W + x];
            body[y * DISCOVL_W + x] = ((px & 0xFF) > 96) ? IDX_TEXT : 0;
        }

    // 글자에 닿은 빈 칸을 테두리로.
    for (y = 0; y < DISCOVL_H; y++)
        for (x = 0; x < DISCOVL_W; x++) {
            int dx, dy, touch = 0;
            if (body[y * DISCOVL_W + x]) { g_pix[y * DISCOVL_W + x] = IDX_TEXT; continue; }
            for (dy = -1; dy <= 1 && !touch; dy++)
                for (dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= DISCOVL_W || ny >= DISCOVL_H) continue;
                    if (body[ny * DISCOVL_W + nx]) { touch = 1; break; }
                }
            g_pix[y * DISCOVL_W + x] = touch ? IDX_EDGE : 0;
        }

    if (oldFont) SelectObject(dc, oldFont);
    if (font) DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return 1;
}

const unsigned char* DiscOvl_Bitmap(void)
{
    int v = DisciplineNow();
    wchar_t text[32];

    if (v < 0) return NULL;
    if (g_ready && v == g_shownVal) return g_pix;

    wsprintfW(text, L"규율 %d", v);
    if (!BuildBitmap(text)) return NULL;
    g_shownVal = v;
    g_ready = 1;
    g_serial++;
    return g_pix;
}

int DiscOvl_Serial(void) { return g_serial; }
