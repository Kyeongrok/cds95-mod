#include "miscskin.h"
#include "ls12.h"      // CharacterUtilKR/src

#define PART_MENUBAR  4
#define STYLE_BYTES   960
#define PART_BYTES    (STYLE_BYTES * SKIN_STYLES)

static unsigned char g_bar[PART_BYTES];
static int           g_ok = 0;
static int           g_tried = 0;

// 벌 안에서 조각이 앉는 자리와 폭. 게임 표 0x552898 과 같은 값이다(열 단위 {0,16,24} x 24행).
static const unsigned kPieceOff[3] = { 0u, 384u, 576u };
static const int      kPieceW[3]   = { SKIN_CAP_W, SKIN_MID_W, SKIN_CAP_W };

int MiscSkin_Ready(void) { return g_ok; }

int MiscSkin_Load(void)
{
    char exe[MAX_PATH], path[MAX_PATH];
    Ls12File f;
    int i, cut = -1;

    if (g_tried) return g_ok;
    g_tried = 1;

    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return 0;
    for (i = 0; exe[i]; i++) if (exe[i] == '\\' || exe[i] == '/') cut = i;
    if (cut < 0) return 0;
    exe[cut] = 0;
    wsprintfA(path, "%s\\MISC.CDS", exe);

    if (!Ls12_Open(&f, path)) return 0;
    if (f.count > PART_MENUBAR &&
        Ls12_DecodePart(&f, PART_MENUBAR, g_bar, PART_BYTES) == PART_BYTES)
        g_ok = 1;
    Ls12_Close(&f);      // 파트 하나만 쓰므로 파일은 바로 놓는다

    OutputDebugStringA(g_ok ? "[ButtonMakerKR] MISC.CDS part 4 loaded."
                            : "[ButtonMakerKR] MISC.CDS part 4 FAILED.");
    return g_ok;
}

void MiscSkin_Free(void) { g_ok = 0; g_tried = 0; }

const unsigned char* MiscSkin_Piece(int style, int k, int* w)
{
    if (!g_ok || style < 0 || style >= SKIN_STYLES || k < 0 || k > 2) return NULL;
    if (w) *w = kPieceW[k];
    return g_bar + (unsigned)style * STYLE_BYTES + kPieceOff[k];
}
