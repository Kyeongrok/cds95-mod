#include "cityfrm.h"
#include "citycg.h"
#include "ls12.h"
#include "game_palette.h"   // kGamePalette[768] — 액자 색인은 전부 이 공용 색표로 풀린다

// citycg.c 와 같은 모양이다. 다른 점은 팔레트 파트가 없다는 것과, 뚫린 자리에 도시 그림을
// 채워 넣고 한 번에 찍는다는 것뿐이다(마스크 블릿을 쓰지 않는 이유 — msimg32 의
// TransparentBlt 를 끌어오지 않아도 되고, 배율을 늘려도 액자와 그림이 어긋나지 않는다).

#define FRM_HOLE 74     // 이 색인 자리는 뚫려 있다(도시 그림 또는 바탕이 비친다)

static Ls12File g_f;
static int      g_opened = 0;      // Ls12_Open 을 이미 해 봤나(실패해도 1)
static int      g_count  = 0;

static unsigned char g_idx[CITYFRM_SZ];        // 푼 액자 색인
static unsigned char g_rgb[CITYFRM_SZ * 3];    // 액자 + 그림을 합친 것(B,G,R)
static int g_cachedFrm = -1;                   // g_idx 에 든 액자 번호
static int g_compFrm   = -1;                   // g_rgb 에 합쳐 둔 (액자, 그림, 바탕색)
static int g_compPic   = -1;
static COLORREF g_compBack = CLR_INVALID;

int CityFrm_Count(void) { return g_count; }

void CityFrm_Load(void)
{
    char exe[MAX_PATH], path[MAX_PATH];
    int i, cut = -1;

    if (g_opened) return;
    g_opened = 1;

    // citycg.c 와 같은 관용구 — 실행 파일이 있는 폴더에서 찾는다.
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return;
    for (i = 0; exe[i]; i++) if (exe[i] == '\\' || exe[i] == '/') cut = i;
    if (cut < 0) return;
    exe[cut] = 0;
    wsprintfA(path, "%s\\CITYFRM.CDS", exe);

    if (!Ls12_Open(&g_f, path)) return;
    g_count = g_f.count;
    OutputDebugStringA("[CityPicKR] CITYFRM.CDS opened.");
}

void CityFrm_Free(void)
{
    Ls12_Close(&g_f);
    g_opened = 0;
    g_count = 0;
    g_cachedFrm = -1;
    g_compFrm = -1; g_compPic = -1; g_compBack = CLR_INVALID;
}

// 액자 한 장을 g_idx 에 푼다. 성공 1.
static int DecodeFrame(int frame)
{
    if (frame == g_cachedFrm) return 1;
    if (!g_f.data || frame < 0 || frame >= g_f.count) return 0;
    if (Ls12_DecodePart(&g_f, frame, g_idx, CITYFRM_SZ) != CITYFRM_SZ) return 0;
    g_cachedFrm = frame;
    return 1;
}

int CityFrm_Draw(HDC dc, int x, int y, int w, int h, int pic, int frame, COLORREF back)
{
    const unsigned char* src;
    BITMAPINFO bi;

    // 액자가 없으면 그림만 그린다. 액자는 덤이다.
    if (!DecodeFrame(frame)) return CityCg_Draw(dc, x, y, w, h, pic);

    src = CityCg_Pixels(pic);
    if (!src) return 0;

    if (frame != g_compFrm || pic != g_compPic || back != g_compBack)
    {
        int px, py;
        unsigned char bb = GetBValue(back), bg = GetGValue(back), br = GetRValue(back);
        for (py = 0; py < CITYFRM_H; py++)
        {
            for (px = 0; px < CITYFRM_W; px++)
            {
                int o = (py * CITYFRM_W + px) * 3;
                unsigned char v = g_idx[py * CITYFRM_W + px];
                if (v != FRM_HOLE) {
                    g_rgb[o+0] = kGamePalette[v*3+2];
                    g_rgb[o+1] = kGamePalette[v*3+1];
                    g_rgb[o+2] = kGamePalette[v*3+0];
                } else {
                    int sx = px - CITYFRM_MARGIN, sy = py - CITYFRM_MARGIN;
                    if (sx >= 0 && sx < CITYPIC_W && sy >= 0 && sy < CITYPIC_H) {
                        const unsigned char* s = src + (sy * CITYPIC_W + sx) * 3;
                        g_rgb[o+0] = s[0]; g_rgb[o+1] = s[1]; g_rgb[o+2] = s[2];
                    } else {
                        g_rgb[o+0] = bb; g_rgb[o+1] = bg; g_rgb[o+2] = br;
                    }
                }
            }
        }
        g_compFrm = frame; g_compPic = pic; g_compBack = back;
    }

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = CITYFRM_W;
    bi.bmiHeader.biHeight = -CITYFRM_H;      // 음수 = 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    // citycg.c 와 같은 규칙 — 정수배로 늘릴 때는 도트를 살리고 줄일 때만 섞는다.
    if (w < CITYFRM_W || h < CITYFRM_H) { SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL); }
    else SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, CITYFRM_W, CITYFRM_H, g_rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
    return 1;
}
