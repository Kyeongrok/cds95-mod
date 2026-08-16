#include "shipstil.h"
#include "ls12.h"     // CharacterUtilKR/src — LS12 디코더

#define PAL_SZ 768

static Ls12File g_f;
static int      g_opened = 0;      // 열어 봤나(실패해도 1 — 매번 다시 열지 않는다)
static int      g_count  = 0;

static unsigned char g_pal[PAL_SZ];
static int           g_palOk = 0;
static unsigned char g_idx[SHIPSTIL_SZ];
static unsigned char g_rgb[SHIPSTIL_SZ * 3];
static int           g_cached = -1;
static COLORREF      g_cachedBg = 0;

int ShipStil_Count(void) { return g_count; }

void ShipStil_Load(void)
{
    char exe[MAX_PATH], path[MAX_PATH];
    int i, cut = -1;

    if (g_opened) return;
    g_opened = 1;

    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return;
    for (i = 0; exe[i]; i++) if (exe[i] == '\\' || exe[i] == '/') cut = i;
    if (cut < 0) return;
    exe[cut] = 0;
    wsprintfA(path, "%s\\SHIPSTIL.CDS", exe);

    if (!Ls12_Open(&g_f, path)) return;
    if (g_f.count < SHIPSTIL_N + 1) { Ls12_Close(&g_f); return; }
    if (Ls12_DecodePart(&g_f, 0, g_pal, PAL_SZ) != PAL_SZ) { Ls12_Close(&g_f); return; }
    g_palOk = 1;
    g_count = SHIPSTIL_N;
    OutputDebugStringA("[ShipInfoKR] SHIPSTIL.CDS opened.");
}

void ShipStil_Free(void)
{
    if (g_f.data) Ls12_Close(&g_f);
    g_opened = 0; g_count = 0; g_palOk = 0; g_cached = -1;
}

static int Decode(int pic, COLORREF bg)
{
    int i, n;

    if (pic == g_cached && bg == g_cachedBg) return 1;
    if (!g_f.data || !g_palOk || pic < 0 || pic >= SHIPSTIL_N) return 0;

    n = Ls12_DecodePart(&g_f, 1 + pic, g_idx, SHIPSTIL_SZ);
    if (n != SHIPSTIL_SZ) return 0;

    for (i = 0; i < SHIPSTIL_SZ; i++) {
        unsigned char v = g_idx[i];
        int k = (int)v - SHIPSTIL_PAL_BASE;
        if (v == SHIPSTIL_KEY || k < 0 || k >= 256) {       // 투명 자리 · 표 밖
            g_rgb[i*3+0] = GetBValue(bg);
            g_rgb[i*3+1] = GetGValue(bg);
            g_rgb[i*3+2] = GetRValue(bg);
        } else {
            g_rgb[i*3+0] = g_pal[k*3+0];   // B
            g_rgb[i*3+1] = g_pal[k*3+2];   // G
            g_rgb[i*3+2] = g_pal[k*3+1];   // R
        }
    }
    g_cached = pic;
    g_cachedBg = bg;
    return 1;
}

int ShipStil_Draw(HDC dc, int x, int y, int w, int h, int pic, COLORREF bg)
{
    BITMAPINFOHEADER bi;
    int old;

    ShipStil_Load();
    if (!Decode(pic, bg)) return 0;

    ZeroMemory(&bi, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = SHIPSTIL_W;
    bi.biHeight = -SHIPSTIL_H;      // 위에서 아래로
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    old = SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, SHIPSTIL_W, SHIPSTIL_H,
                  g_rgb, (BITMAPINFO*)&bi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc, old);
    return 1;
}
