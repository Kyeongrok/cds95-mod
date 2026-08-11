#include "ocean.h"
#include "ls12.h"          // CharacterUtilKR/src
#include "ocean_palette.h"  // kOceanPalette[768] — 게임 해상 화면 팔레트(살아 있는 게임에서 뜬 것)

static unsigned char* g_tiles = NULL;   // 4MB, 16x16 8bpp x 16384
static unsigned       g_rgb[256];       // 색인 -> 0xRRGGBB (팔레트를 미리 풀어 둔다)
static wchar_t        g_why[160] = L"";  // 못 올린 이유(창에 띄운다)

int Ocean_Ready(void) { return g_tiles != NULL; }
const wchar_t* Ocean_Why(void) { return g_why; }

void Ocean_Free(void)
{
    if (g_tiles) { HeapFree(GetProcessHeap(), 0, g_tiles); g_tiles = NULL; }
}

static void GamePathA(char* out, const char* name)
{
    char exe[MAX_PATH];
    char* p; char* last;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    last = exe;
    for (p = exe; *p; p++) if (*p == '\\' || *p == '/') last = p;
    *last = 0;
    wsprintfA(out, "%s\\%s", exe, name);
}

int Ocean_Load(void)
{
    char path[MAX_PATH];
    Ls12File f;
    int i, n;

    if (g_tiles) return 1;

    GamePathA(path, "OCEAN.CDS");
    if (!Ls12_Open(&f, path)) {
        wsprintfW(g_why, L"OCEAN.CDS 를 열지 못했습니다 (%hs)", path);
        return 0;
    }
    // 파트0 이 타일이다. 크기가 다르면 다른 판이니 손대지 않는다.
    if (f.count < 1 || Ls12_PartSize(&f, 0) != OCEAN_DATA_SZ) {
        wsprintfW(g_why, L"OCEAN.CDS 파트0 크기가 다릅니다 (%u, 파트 %d개)",
                  f.count > 0 ? Ls12_PartSize(&f, 0) : 0, f.count);
        Ls12_Close(&f); return 0;
    }

    g_tiles = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, OCEAN_DATA_SZ);
    if (!g_tiles) { lstrcpyW(g_why, L"타일 4MB 를 잡지 못했습니다"); Ls12_Close(&f); return 0; }

    n = Ls12_DecodePart(&f, 0, g_tiles, OCEAN_DATA_SZ);
    Ls12_Close(&f);
    if (n != OCEAN_DATA_SZ) {
        wchar_t m[128];
        wsprintfW(m, L"[WorldMapKR] OCEAN.CDS 푸는 데 실패 — %d 바이트만 나왔다", n);
        OutputDebugStringW(m);
        wsprintfW(g_why, L"OCEAN.CDS 를 푸는 중 멈췄습니다 (%d / %d 바이트)", n, OCEAN_DATA_SZ);
        Ocean_Free(); return 0;
    }
    OutputDebugStringW(L"[WorldMapKR] OCEAN.CDS 타일 16384장 올림 — 지도를 게임 그림으로 그린다.");
    g_why[0] = 0;

    for (i = 0; i < 256; i++) {
        const unsigned char* c = kOceanPalette + i * 3;
        g_rgb[i] = ((unsigned)c[0] << 16) | ((unsigned)c[1] << 8) | (unsigned)c[2];
    }

    return 1;
}

unsigned Ocean_Pixel(int tile, int px, int py)
{
    if (!g_tiles) return 0;
    tile &= OCEAN_TILE_MASK;
    return g_rgb[g_tiles[(unsigned)tile * OCEAN_TILE_SZ + (unsigned)py * OCEAN_TILE_W + (unsigned)px]];
}
