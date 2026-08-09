#include "itempic.h"
#include "ls12.h"
#include "game_palette.h"   // kGamePalette[768] — 낮은 색인(10~73) 공용 색표

// faces.c 와 같은 모양이다: 아카이브 하나를 열어 두고, 한 장을 풀어 24bpp DIB 로 찍는다.
// 다른 점은 팔레트가 그림마다 따로 붙어 온다는 것뿐이다(itempic.h 의 규칙 참고).
//
// 이 파일은 TradeUtilKR 도 같이 빌드한다(교역품 그림). 그래서 창 꾸미기(액자·테두리·
// 안내문)는 여기 두지 않는다 — 세피아 색표가 플러그인마다 따로라서, 그리는 건 부르는 쪽 몫이다.

#define PAL_BASE  160          // 그림 제 팔레트가 얹히는 첫 색인
#define PAL_MAX   258          // 팔레트 파트 크기(86색 x 3바이트)

static Ls12File g_f;
static int      g_opened = 0;      // Ls12_Open 을 이미 해 봤나(실패해도 1 — 매번 다시 열지 않는다)
static int      g_count  = 0;

static unsigned char g_idx[ITEMPIC_SZ];
static unsigned char g_pal[PAL_MAX];
static unsigned char g_rgb[ITEMPIC_SZ * 3];
static int           g_cached = -1;   // g_rgb 에 들어 있는 그림 번호

int ItemPic_Count(void) { return g_count; }

void ItemPic_Load(void)
{
    char exe[MAX_PATH], path[MAX_PATH];
    int i, cut = -1;

    if (g_opened) return;
    g_opened = 1;

    // faces.c 와 같은 관용구 — 실행 파일이 있는 폴더에서 찾는다.
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return;
    for (i = 0; exe[i]; i++) if (exe[i] == '\\' || exe[i] == '/') cut = i;
    if (cut < 0) return;
    exe[cut] = 0;
    wsprintfA(path, "%s\\ITEM.CDS", exe);

    if (!Ls12_Open(&g_f, path)) return;
    g_count = g_f.count / 2;      // 그림 한 장이 (그림 + 팔레트) 두 파트를 쓴다
    OutputDebugStringA("[CharacterUtilKR] ITEM.CDS opened.");
}

// 한 장을 풀어 g_rgb 에 B,G,R 로 채운다. 성공 1.
static int Decode(int pic)
{
    int i, n, np;

    if (pic == g_cached) return 1;
    if (!g_f.data || pic < 0 || 2 * pic + 1 >= g_f.count) return 0;

    // Ls12_DecodePart 는 outcap 으로 조용히 잘라서 돌려주므로 길이를 직접 확인해야 한다.
    n = Ls12_DecodePart(&g_f, 2 * pic, g_idx, ITEMPIC_SZ);
    if (n != ITEMPIC_SZ) return 0;
    np = Ls12_DecodePart(&g_f, 2 * pic + 1, g_pal, PAL_MAX);
    if (np < 3) return 0;

    for (i = 0; i < ITEMPIC_SZ; i++) {
        unsigned char v = g_idx[i];
        int k = (int)v - PAL_BASE;
        if (v >= PAL_BASE && k * 3 + 2 < np) {
            g_rgb[i*3+0] = g_pal[k*3+0];    // B
            g_rgb[i*3+1] = g_pal[k*3+2];    // G
            g_rgb[i*3+2] = g_pal[k*3+1];    // R
        } else {
            g_rgb[i*3+0] = kGamePalette[v*3+2];
            g_rgb[i*3+1] = kGamePalette[v*3+1];
            g_rgb[i*3+2] = kGamePalette[v*3+0];
        }
    }
    g_cached = pic;
    return 1;
}

int ItemPic_Draw(HDC dc, int x, int y, int w, int h, int pic)
{
    BITMAPINFO bi;

    if (!Decode(pic)) return 0;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = ITEMPIC_W;
    bi.bmiHeader.biHeight = -ITEMPIC_H;      // 음수 = 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, ITEMPIC_W, ITEMPIC_H, g_rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
    return 1;
}
