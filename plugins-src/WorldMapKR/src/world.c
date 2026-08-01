#include "world.h"
#include <stdio.h>

// ---- 팔레트 ----
// 등급 구분은 cds-helper MapPalette 를 따르되, 색은 게임 항해 화면 스크린샷에서 뽑아 맞췄다.
// 게임은 두 색을 디더링해 한 색처럼 보이게 하는데, 여기선 한 칸이 한 픽셀이라 섞을 수 없다.
// 그래서 바다는 관측된 두 색(#506E81 밝은 쪽 / #35556B 어두운 쪽)의 중간값을 쓴다.
// 육지는 관측된 명도 램프(해안=어둡고 따뜻함 -> 내륙=밝고 탁함)를 등급에 그대로 배정했다.
// 0xRRGGBB 로 적고 쓸 때 BGR 로 풀어 넣는다.
#define C_SEA        0x426176   /* 관측 #506E81 / #35556B 의 중간 */
#define C_COAST      0x35556B   /* 관측 바다 어두운 쪽 */
#define C_LAND_66    0x9C7C58   /* 해안 근처 — 관측 램프에서 가장 어둡고 따뜻한 색 */
#define C_LAND_DEF   0x9F835F
#define C_LAND_64    0xA38C6A   /* 사막 속성 */
#define C_LAND_67    0xA79675   /* 중간 내륙 */
#define C_LAND_68    0xACA48C   /* 깊은 내륙 — 관측 램프에서 가장 밝은 색 */
// 사막/산은 항해 화면에 안 나와 관측값이 없다. 관측 램프의 따뜻한 색조를 유지한 채
// 위아래로 늘려 잡았다(사막은 밝은 모래, 산은 어두운 갈색).
#define C_DESERT     0xCBA172
#define C_MOUNTAIN   0x8A6845
#define MOUNTAIN_ATTR 100       /* 속성 86~116 중 이 값 이상이면 산, 미만이면 사막 */

// 지형별 육지 비율(백분율). cds-helper 의 GetCoastLandRatio 와 같다.
// -1 은 표에 없는 지형 — 그 경우 terrain/127 로 친다.
static const short kLandPct[128] = {
    /*  0 */  -1,  -1,  39,  18,  49,   0,  50,  43,  52,  50,
    /* 10 */  14,  55,  29,   0,  11,  55,  42,  35,  84,  43,
    /* 20 */  50,   3,   0,  29,  43,  31,   0,   0,   0,  29,
    /* 30 */  12,  11,  27,  27,  60,  51,  12,  54,  29,  66,
    /* 40 */   0,  25, 100,  78,   0,  57,  50,  -1,  92,  54,
    /* 50 */  28,  14,  -1,  99,  -1,  78,   0,  83,  34,  40,
    /* 60 */ 100,   4,  10,  -1,   0,   6,  97,  58,  17,  -1,
    /* 70 */  49,  26, 100,  12,  -1,  91,  96,  62, 100,  49,
    /* 80 */  -1,  66,  99,   3,  93,  23,  -1,  -1,  -1,  -1,
    /* 90 */  -1,  -1,  44,  -1,  -1,  -1,  96,  -1,  -1,  -1,
    /*100 */  -1,  -1,  -1, 100,  -1,  -1,  -1,  -1,  -1,  -1,
    /*110 */  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  94,  -1,
    /*120 */  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1
};

static unsigned char* g_raw = NULL;   // WORLD.CDS 원본(6.25MB)
static unsigned char* g_px = NULL;    // 화면 크기로 그려 둔 24bpp BGR
static int g_w = 0, g_h = 0;

int World_Loaded(void) { return g_raw != NULL; }
int World_W(void)      { return g_w; }
int World_H(void)      { return g_h; }
const unsigned char* World_Pixels(void) { return g_px; }

static unsigned LandColor(unsigned char attr)
{
    switch (attr) {
    case 64: return C_LAND_64;
    case 66: return C_LAND_66;
    case 67: return C_LAND_67;
    case 68: return C_LAND_68;
    default: break;
    }
    if (attr >= 86 && attr <= 116)
        return attr >= MOUNTAIN_ATTR ? C_MOUNTAIN : C_DESERT;
    return C_LAND_DEF;
}

static unsigned Blend(unsigned a, unsigned b, int pct)
{
    int r = ((a >> 16) & 0xFF) + (((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * pct) / 100;
    int g = ((a >>  8) & 0xFF) + (((int)((b >>  8) & 0xFF) - (int)((a >>  8) & 0xFF)) * pct) / 100;
    int bl= ((a      ) & 0xFF) + (((int)((b      ) & 0xFF) - (int)((a      ) & 0xFF)) * pct) / 100;
    return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)bl;
}

static unsigned CellColor(unsigned char terrain, unsigned char attr)
{
    int pct;
    if (terrain == 0) return attr == 0 ? C_COAST : C_SEA;
    if (terrain == 1) return LandColor(attr);
    pct = kLandPct[terrain];
    if (pct < 0) pct = (int)terrain * 100 / 127;
    return Blend(C_SEA, LandColor(attr <= 10 ? 0 : attr), pct);
}

static void GamePath(wchar_t* out, const wchar_t* name)
{
    wchar_t exe[MAX_PATH];
    wchar_t* p; wchar_t* last;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    last = exe;
    for (p = exe; *p; p++) if (*p == L'\\' || *p == L'/') last = p;
    *last = 0;
    wsprintfW(out, L"%s\\%s", exe, name);
}

int World_Load(void)
{
    wchar_t path[MAX_PATH];
    HANDLE fh;
    DWORD sz = 0, got = 0;

    if (g_raw) return 1;
    GamePath(path, L"WORLD.CDS");
    fh = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(fh, NULL);
    if (sz != WORLD_FILE_SIZE) { CloseHandle(fh); return 0; }

    g_raw = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!g_raw) { CloseHandle(fh); return 0; }
    if (!ReadFile(fh, g_raw, sz, &got, NULL) || got != sz) {
        HeapFree(GetProcessHeap(), 0, g_raw); g_raw = NULL;
        CloseHandle(fh); return 0;
    }
    CloseHandle(fh);
    return 1;
}

int World_RenderView(int w, int h, int x0, int y0, int vw, int vh)
{
    int x, y;

    if (!g_raw) return 0;
    // 24bpp DIB 는 한 줄이 4바이트 배수여야 한다. 폭을 4의 배수로 내려 맞추면 w*3 도 맞는다.
    w &= ~3;
    if (w <= 0 || h <= 0 || vw <= 0 || vh <= 0) return 0;

    if (!g_px || g_w != w || g_h != h) {
        if (g_px) HeapFree(GetProcessHeap(), 0, g_px);
        g_px = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h * 3);
        if (!g_px) { g_w = g_h = 0; return 0; }
        g_w = w; g_h = h;
    }

    // 보이는 영역만 화면 크기로 바로 줄여 그린다(가장 가까운 칸 하나를 골라 쓰는 방식).
    // 확대해도 뽑는 픽셀 수가 같아서 비용이 늘지 않는다.
    for (y = 0; y < h; y++) {
        int uy = y0 + (int)((long long)y * vh / h);
        unsigned char* dst = g_px + (SIZE_T)y * w * 3;
        if (uy < 0) uy = 0;
        if (uy >= WORLD_CELL_H) uy = WORLD_CELL_H - 1;
        for (x = 0; x < w; x++) {
            int ux = x0 + (int)((long long)x * vw / w);
            int cx, row;
            const unsigned char* cell;
            unsigned c;
            if (ux < 0) ux = 0;
            if (ux >= WORLD_UNFOLD_W) ux = WORLD_UNFOLD_W - 1;
            // 짝수 행이 왼쪽 절반, 홀수 행이 오른쪽 절반이다.
            cx  = (ux < WORLD_CELL_W) ? ux : ux - WORLD_CELL_W;
            row = (ux < WORLD_CELL_W) ? uy * 2 : uy * 2 + 1;
            cell = g_raw + (SIZE_T)row * WORLD_RAW_STRIDE + (SIZE_T)cx * 2;
            c = CellColor((unsigned char)(cell[0] & 0x7F), cell[1]);
            *dst++ = (unsigned char)(c & 0xFF);          // B
            *dst++ = (unsigned char)((c >> 8) & 0xFF);   // G
            *dst++ = (unsigned char)((c >> 16) & 0xFF);  // R
        }
    }
    return 1;
}

void World_Free(void)
{
    if (g_px)  { HeapFree(GetProcessHeap(), 0, g_px);  g_px = NULL; }
    if (g_raw) { HeapFree(GetProcessHeap(), 0, g_raw); g_raw = NULL; }
    g_w = g_h = 0;
}
